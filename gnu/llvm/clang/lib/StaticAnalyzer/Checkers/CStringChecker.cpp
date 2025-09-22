//= CStringChecker.cpp - Checks calls to C string functions --------*- C++ -*-//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This defines CStringChecker, which is an assortment of checks on calls
// to functions in <string.h>.
//
//===----------------------------------------------------------------------===//

#include "InterCheckerAPI.h"
#include "clang/AST/OperationKinds.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/CharInfo.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallDescription.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicExtent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/MemRegion.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"
#include <functional>
#include <optional>

using namespace clang;
using namespace ento;
using namespace std::placeholders;

namespace {
struct AnyArgExpr {
  const Expr *Expression;
  unsigned ArgumentIndex;
};
struct SourceArgExpr : AnyArgExpr {};
struct DestinationArgExpr : AnyArgExpr {};
struct SizeArgExpr : AnyArgExpr {};

using ErrorMessage = SmallString<128>;
enum class AccessKind { write, read };

static ErrorMessage createOutOfBoundErrorMsg(StringRef FunctionDescription,
                                             AccessKind Access) {
  ErrorMessage Message;
  llvm::raw_svector_ostream Os(Message);

  // Function classification like: Memory copy function
  Os << toUppercase(FunctionDescription.front())
     << &FunctionDescription.data()[1];

  if (Access == AccessKind::write) {
    Os << " overflows the destination buffer";
  } else { // read access
    Os << " accesses out-of-bound array element";
  }

  return Message;
}

enum class ConcatFnKind { none = 0, strcat = 1, strlcat = 2 };

enum class CharKind { Regular = 0, Wide };
constexpr CharKind CK_Regular = CharKind::Regular;
constexpr CharKind CK_Wide = CharKind::Wide;

static QualType getCharPtrType(ASTContext &Ctx, CharKind CK) {
  return Ctx.getPointerType(CK == CharKind::Regular ? Ctx.CharTy
                                                    : Ctx.WideCharTy);
}

class CStringChecker : public Checker< eval::Call,
                                         check::PreStmt<DeclStmt>,
                                         check::LiveSymbols,
                                         check::DeadSymbols,
                                         check::RegionChanges
                                         > {
  mutable std::unique_ptr<BugType> BT_Null, BT_Bounds, BT_Overlap,
      BT_NotCString, BT_AdditionOverflow, BT_UninitRead;

  mutable const char *CurrentFunctionDescription = nullptr;

public:
  /// The filter is used to filter out the diagnostics which are not enabled by
  /// the user.
  struct CStringChecksFilter {
    bool CheckCStringNullArg = false;
    bool CheckCStringOutOfBounds = false;
    bool CheckCStringBufferOverlap = false;
    bool CheckCStringNotNullTerm = false;
    bool CheckCStringUninitializedRead = false;

    CheckerNameRef CheckNameCStringNullArg;
    CheckerNameRef CheckNameCStringOutOfBounds;
    CheckerNameRef CheckNameCStringBufferOverlap;
    CheckerNameRef CheckNameCStringNotNullTerm;
    CheckerNameRef CheckNameCStringUninitializedRead;
  };

  CStringChecksFilter Filter;

  static void *getTag() { static int tag; return &tag; }

  bool evalCall(const CallEvent &Call, CheckerContext &C) const;
  void checkPreStmt(const DeclStmt *DS, CheckerContext &C) const;
  void checkLiveSymbols(ProgramStateRef state, SymbolReaper &SR) const;
  void checkDeadSymbols(SymbolReaper &SR, CheckerContext &C) const;

  ProgramStateRef
    checkRegionChanges(ProgramStateRef state,
                       const InvalidatedSymbols *,
                       ArrayRef<const MemRegion *> ExplicitRegions,
                       ArrayRef<const MemRegion *> Regions,
                       const LocationContext *LCtx,
                       const CallEvent *Call) const;

  using FnCheck = std::function<void(const CStringChecker *, CheckerContext &,
                                     const CallEvent &)>;

  CallDescriptionMap<FnCheck> Callbacks = {
      {{CDM::CLibraryMaybeHardened, {"memcpy"}, 3},
       std::bind(&CStringChecker::evalMemcpy, _1, _2, _3, CK_Regular)},
      {{CDM::CLibraryMaybeHardened, {"wmemcpy"}, 3},
       std::bind(&CStringChecker::evalMemcpy, _1, _2, _3, CK_Wide)},
      {{CDM::CLibraryMaybeHardened, {"mempcpy"}, 3},
       std::bind(&CStringChecker::evalMempcpy, _1, _2, _3, CK_Regular)},
      {{CDM::CLibraryMaybeHardened, {"wmempcpy"}, 3},
       std::bind(&CStringChecker::evalMempcpy, _1, _2, _3, CK_Wide)},
      {{CDM::CLibrary, {"memcmp"}, 3},
       std::bind(&CStringChecker::evalMemcmp, _1, _2, _3, CK_Regular)},
      {{CDM::CLibrary, {"wmemcmp"}, 3},
       std::bind(&CStringChecker::evalMemcmp, _1, _2, _3, CK_Wide)},
      {{CDM::CLibraryMaybeHardened, {"memmove"}, 3},
       std::bind(&CStringChecker::evalMemmove, _1, _2, _3, CK_Regular)},
      {{CDM::CLibraryMaybeHardened, {"wmemmove"}, 3},
       std::bind(&CStringChecker::evalMemmove, _1, _2, _3, CK_Wide)},
      {{CDM::CLibraryMaybeHardened, {"memset"}, 3},
       &CStringChecker::evalMemset},
      {{CDM::CLibrary, {"explicit_memset"}, 3}, &CStringChecker::evalMemset},
      // FIXME: C23 introduces 'memset_explicit', maybe also model that
      {{CDM::CLibraryMaybeHardened, {"strcpy"}, 2},
       &CStringChecker::evalStrcpy},
      {{CDM::CLibraryMaybeHardened, {"strncpy"}, 3},
       &CStringChecker::evalStrncpy},
      {{CDM::CLibraryMaybeHardened, {"stpcpy"}, 2},
       &CStringChecker::evalStpcpy},
      {{CDM::CLibraryMaybeHardened, {"strlcpy"}, 3},
       &CStringChecker::evalStrlcpy},
      {{CDM::CLibraryMaybeHardened, {"strcat"}, 2},
       &CStringChecker::evalStrcat},
      {{CDM::CLibraryMaybeHardened, {"strncat"}, 3},
       &CStringChecker::evalStrncat},
      {{CDM::CLibraryMaybeHardened, {"strlcat"}, 3},
       &CStringChecker::evalStrlcat},
      {{CDM::CLibraryMaybeHardened, {"strlen"}, 1},
       &CStringChecker::evalstrLength},
      {{CDM::CLibrary, {"wcslen"}, 1}, &CStringChecker::evalstrLength},
      {{CDM::CLibraryMaybeHardened, {"strnlen"}, 2},
       &CStringChecker::evalstrnLength},
      {{CDM::CLibrary, {"wcsnlen"}, 2}, &CStringChecker::evalstrnLength},
      {{CDM::CLibrary, {"strcmp"}, 2}, &CStringChecker::evalStrcmp},
      {{CDM::CLibrary, {"strncmp"}, 3}, &CStringChecker::evalStrncmp},
      {{CDM::CLibrary, {"strcasecmp"}, 2}, &CStringChecker::evalStrcasecmp},
      {{CDM::CLibrary, {"strncasecmp"}, 3}, &CStringChecker::evalStrncasecmp},
      {{CDM::CLibrary, {"strsep"}, 2}, &CStringChecker::evalStrsep},
      {{CDM::CLibrary, {"bcopy"}, 3}, &CStringChecker::evalBcopy},
      {{CDM::CLibrary, {"bcmp"}, 3},
       std::bind(&CStringChecker::evalMemcmp, _1, _2, _3, CK_Regular)},
      {{CDM::CLibrary, {"bzero"}, 2}, &CStringChecker::evalBzero},
      {{CDM::CLibraryMaybeHardened, {"explicit_bzero"}, 2},
       &CStringChecker::evalBzero},

      // When recognizing calls to the following variadic functions, we accept
      // any number of arguments in the call (std::nullopt = accept any
      // number), but check that in the declaration there are 2 and 3
      // parameters respectively. (Note that the parameter count does not
      // include the "...". Calls where the number of arguments is too small
      // will be discarded by the callback.)
      {{CDM::CLibraryMaybeHardened, {"sprintf"}, std::nullopt, 2},
       &CStringChecker::evalSprintf},
      {{CDM::CLibraryMaybeHardened, {"snprintf"}, std::nullopt, 3},
       &CStringChecker::evalSnprintf},
  };

  // These require a bit of special handling.
  CallDescription StdCopy{CDM::SimpleFunc, {"std", "copy"}, 3},
      StdCopyBackward{CDM::SimpleFunc, {"std", "copy_backward"}, 3};

  FnCheck identifyCall(const CallEvent &Call, CheckerContext &C) const;
  void evalMemcpy(CheckerContext &C, const CallEvent &Call, CharKind CK) const;
  void evalMempcpy(CheckerContext &C, const CallEvent &Call, CharKind CK) const;
  void evalMemmove(CheckerContext &C, const CallEvent &Call, CharKind CK) const;
  void evalBcopy(CheckerContext &C, const CallEvent &Call) const;
  void evalCopyCommon(CheckerContext &C, const CallEvent &Call,
                      ProgramStateRef state, SizeArgExpr Size,
                      DestinationArgExpr Dest, SourceArgExpr Source,
                      bool Restricted, bool IsMempcpy, CharKind CK) const;

  void evalMemcmp(CheckerContext &C, const CallEvent &Call, CharKind CK) const;

  void evalstrLength(CheckerContext &C, const CallEvent &Call) const;
  void evalstrnLength(CheckerContext &C, const CallEvent &Call) const;
  void evalstrLengthCommon(CheckerContext &C, const CallEvent &Call,
                           bool IsStrnlen = false) const;

  void evalStrcpy(CheckerContext &C, const CallEvent &Call) const;
  void evalStrncpy(CheckerContext &C, const CallEvent &Call) const;
  void evalStpcpy(CheckerContext &C, const CallEvent &Call) const;
  void evalStrlcpy(CheckerContext &C, const CallEvent &Call) const;
  void evalStrcpyCommon(CheckerContext &C, const CallEvent &Call,
                        bool ReturnEnd, bool IsBounded, ConcatFnKind appendK,
                        bool returnPtr = true) const;

  void evalStrcat(CheckerContext &C, const CallEvent &Call) const;
  void evalStrncat(CheckerContext &C, const CallEvent &Call) const;
  void evalStrlcat(CheckerContext &C, const CallEvent &Call) const;

  void evalStrcmp(CheckerContext &C, const CallEvent &Call) const;
  void evalStrncmp(CheckerContext &C, const CallEvent &Call) const;
  void evalStrcasecmp(CheckerContext &C, const CallEvent &Call) const;
  void evalStrncasecmp(CheckerContext &C, const CallEvent &Call) const;
  void evalStrcmpCommon(CheckerContext &C, const CallEvent &Call,
                        bool IsBounded = false, bool IgnoreCase = false) const;

  void evalStrsep(CheckerContext &C, const CallEvent &Call) const;

  void evalStdCopy(CheckerContext &C, const CallEvent &Call) const;
  void evalStdCopyBackward(CheckerContext &C, const CallEvent &Call) const;
  void evalStdCopyCommon(CheckerContext &C, const CallEvent &Call) const;
  void evalMemset(CheckerContext &C, const CallEvent &Call) const;
  void evalBzero(CheckerContext &C, const CallEvent &Call) const;

  void evalSprintf(CheckerContext &C, const CallEvent &Call) const;
  void evalSnprintf(CheckerContext &C, const CallEvent &Call) const;
  void evalSprintfCommon(CheckerContext &C, const CallEvent &Call,
                         bool IsBounded) const;

  // Utility methods
  std::pair<ProgramStateRef , ProgramStateRef >
  static assumeZero(CheckerContext &C,
                    ProgramStateRef state, SVal V, QualType Ty);

  static ProgramStateRef setCStringLength(ProgramStateRef state,
                                              const MemRegion *MR,
                                              SVal strLength);
  static SVal getCStringLengthForRegion(CheckerContext &C,
                                        ProgramStateRef &state,
                                        const Expr *Ex,
                                        const MemRegion *MR,
                                        bool hypothetical);
  SVal getCStringLength(CheckerContext &C,
                        ProgramStateRef &state,
                        const Expr *Ex,
                        SVal Buf,
                        bool hypothetical = false) const;

  const StringLiteral *getCStringLiteral(CheckerContext &C,
                                         ProgramStateRef &state,
                                         const Expr *expr,
                                         SVal val) const;

  /// Invalidate the destination buffer determined by characters copied.
  static ProgramStateRef
  invalidateDestinationBufferBySize(CheckerContext &C, ProgramStateRef S,
                                    const Expr *BufE, SVal BufV, SVal SizeV,
                                    QualType SizeTy);

  /// Operation never overflows, do not invalidate the super region.
  static ProgramStateRef invalidateDestinationBufferNeverOverflows(
      CheckerContext &C, ProgramStateRef S, const Expr *BufE, SVal BufV);

  /// We do not know whether the operation can overflow (e.g. size is unknown),
  /// invalidate the super region and escape related pointers.
  static ProgramStateRef invalidateDestinationBufferAlwaysEscapeSuperRegion(
      CheckerContext &C, ProgramStateRef S, const Expr *BufE, SVal BufV);

  /// Invalidate the source buffer for escaping pointers.
  static ProgramStateRef invalidateSourceBuffer(CheckerContext &C,
                                                ProgramStateRef S,
                                                const Expr *BufE, SVal BufV);

  /// @param InvalidationTraitOperations Determine how to invlidate the
  /// MemRegion by setting the invalidation traits. Return true to cause pointer
  /// escape, or false otherwise.
  static ProgramStateRef invalidateBufferAux(
      CheckerContext &C, ProgramStateRef State, const Expr *Ex, SVal V,
      llvm::function_ref<bool(RegionAndSymbolInvalidationTraits &,
                              const MemRegion *)>
          InvalidationTraitOperations);

  static bool SummarizeRegion(raw_ostream &os, ASTContext &Ctx,
                              const MemRegion *MR);

  static bool memsetAux(const Expr *DstBuffer, SVal CharE,
                        const Expr *Size, CheckerContext &C,
                        ProgramStateRef &State);

  // Re-usable checks
  ProgramStateRef checkNonNull(CheckerContext &C, ProgramStateRef State,
                               AnyArgExpr Arg, SVal l) const;
  // Check whether the origin region behind \p Element (like the actual array
  // region \p Element is from) is initialized.
  ProgramStateRef checkInit(CheckerContext &C, ProgramStateRef state,
                            AnyArgExpr Buffer, SVal Element, SVal Size) const;
  ProgramStateRef CheckLocation(CheckerContext &C, ProgramStateRef state,
                                AnyArgExpr Buffer, SVal Element,
                                AccessKind Access,
                                CharKind CK = CharKind::Regular) const;
  ProgramStateRef CheckBufferAccess(CheckerContext &C, ProgramStateRef State,
                                    AnyArgExpr Buffer, SizeArgExpr Size,
                                    AccessKind Access,
                                    CharKind CK = CharKind::Regular) const;
  ProgramStateRef CheckOverlap(CheckerContext &C, ProgramStateRef state,
                               SizeArgExpr Size, AnyArgExpr First,
                               AnyArgExpr Second,
                               CharKind CK = CharKind::Regular) const;
  void emitOverlapBug(CheckerContext &C,
                      ProgramStateRef state,
                      const Stmt *First,
                      const Stmt *Second) const;

  void emitNullArgBug(CheckerContext &C, ProgramStateRef State, const Stmt *S,
                      StringRef WarningMsg) const;
  void emitOutOfBoundsBug(CheckerContext &C, ProgramStateRef State,
                          const Stmt *S, StringRef WarningMsg) const;
  void emitNotCStringBug(CheckerContext &C, ProgramStateRef State,
                         const Stmt *S, StringRef WarningMsg) const;
  void emitAdditionOverflowBug(CheckerContext &C, ProgramStateRef State) const;
  void emitUninitializedReadBug(CheckerContext &C, ProgramStateRef State,
                                const Expr *E, StringRef Msg) const;
  ProgramStateRef checkAdditionOverflow(CheckerContext &C,
                                            ProgramStateRef state,
                                            NonLoc left,
                                            NonLoc right) const;

  // Return true if the destination buffer of the copy function may be in bound.
  // Expects SVal of Size to be positive and unsigned.
  // Expects SVal of FirstBuf to be a FieldRegion.
  static bool isFirstBufInBound(CheckerContext &C, ProgramStateRef State,
                                SVal BufVal, QualType BufTy, SVal LengthVal,
                                QualType LengthTy);
};

} //end anonymous namespace

REGISTER_MAP_WITH_PROGRAMSTATE(CStringLength, const MemRegion *, SVal)

//===----------------------------------------------------------------------===//
// Individual checks and utility methods.
//===----------------------------------------------------------------------===//

std::pair<ProgramStateRef, ProgramStateRef>
CStringChecker::assumeZero(CheckerContext &C, ProgramStateRef State, SVal V,
                           QualType Ty) {
  std::optional<DefinedSVal> val = V.getAs<DefinedSVal>();
  if (!val)
    return std::pair<ProgramStateRef, ProgramStateRef>(State, State);

  SValBuilder &svalBuilder = C.getSValBuilder();
  DefinedOrUnknownSVal zero = svalBuilder.makeZeroVal(Ty);
  return State->assume(svalBuilder.evalEQ(State, *val, zero));
}

ProgramStateRef CStringChecker::checkNonNull(CheckerContext &C,
                                             ProgramStateRef State,
                                             AnyArgExpr Arg, SVal l) const {
  // If a previous check has failed, propagate the failure.
  if (!State)
    return nullptr;

  ProgramStateRef stateNull, stateNonNull;
  std::tie(stateNull, stateNonNull) =
      assumeZero(C, State, l, Arg.Expression->getType());

  if (stateNull && !stateNonNull) {
    if (Filter.CheckCStringNullArg) {
      SmallString<80> buf;
      llvm::raw_svector_ostream OS(buf);
      assert(CurrentFunctionDescription);
      OS << "Null pointer passed as " << (Arg.ArgumentIndex + 1)
         << llvm::getOrdinalSuffix(Arg.ArgumentIndex + 1) << " argument to "
         << CurrentFunctionDescription;

      emitNullArgBug(C, stateNull, Arg.Expression, OS.str());
    }
    return nullptr;
  }

  // From here on, assume that the value is non-null.
  assert(stateNonNull);
  return stateNonNull;
}

static std::optional<NonLoc> getIndex(ProgramStateRef State,
                                      const ElementRegion *ER, CharKind CK) {
  SValBuilder &SVB = State->getStateManager().getSValBuilder();
  ASTContext &Ctx = SVB.getContext();

  if (CK == CharKind::Regular) {
    if (ER->getValueType() != Ctx.CharTy)
      return {};
    return ER->getIndex();
  }

  if (ER->getValueType() != Ctx.WideCharTy)
    return {};

  QualType SizeTy = Ctx.getSizeType();
  NonLoc WideSize =
      SVB.makeIntVal(Ctx.getTypeSizeInChars(Ctx.WideCharTy).getQuantity(),
                     SizeTy)
          .castAs<NonLoc>();
  SVal Offset =
      SVB.evalBinOpNN(State, BO_Mul, ER->getIndex(), WideSize, SizeTy);
  if (Offset.isUnknown())
    return {};
  return Offset.castAs<NonLoc>();
}

// Basically 1 -> 1st, 12 -> 12th, etc.
static void printIdxWithOrdinalSuffix(llvm::raw_ostream &Os, unsigned Idx) {
  Os << Idx << llvm::getOrdinalSuffix(Idx);
}

ProgramStateRef CStringChecker::checkInit(CheckerContext &C,
                                          ProgramStateRef State,
                                          AnyArgExpr Buffer, SVal Element,
                                          SVal Size) const {

  // If a previous check has failed, propagate the failure.
  if (!State)
    return nullptr;

  const MemRegion *R = Element.getAsRegion();
  const auto *ER = dyn_cast_or_null<ElementRegion>(R);
  if (!ER)
    return State;

  const auto *SuperR = ER->getSuperRegion()->getAs<TypedValueRegion>();
  if (!SuperR)
    return State;

  // FIXME: We ought to able to check objects as well. Maybe
  // UninitializedObjectChecker could help?
  if (!SuperR->getValueType()->isArrayType())
    return State;

  SValBuilder &SVB = C.getSValBuilder();
  ASTContext &Ctx = SVB.getContext();

  const QualType ElemTy = Ctx.getBaseElementType(SuperR->getValueType());
  const NonLoc Zero = SVB.makeZeroArrayIndex();

  std::optional<Loc> FirstElementVal =
      State->getLValue(ElemTy, Zero, loc::MemRegionVal(SuperR)).getAs<Loc>();
  if (!FirstElementVal)
    return State;

  // Ensure that we wouldn't read uninitialized value.
  if (Filter.CheckCStringUninitializedRead &&
      State->getSVal(*FirstElementVal).isUndef()) {
    llvm::SmallString<258> Buf;
    llvm::raw_svector_ostream OS(Buf);
    OS << "The first element of the ";
    printIdxWithOrdinalSuffix(OS, Buffer.ArgumentIndex + 1);
    OS << " argument is undefined";
    emitUninitializedReadBug(C, State, Buffer.Expression, OS.str());
    return nullptr;
  }

  // We won't check whether the entire region is fully initialized -- lets just
  // check that the first and the last element is. So, onto checking the last
  // element:
  const QualType IdxTy = SVB.getArrayIndexType();

  NonLoc ElemSize =
      SVB.makeIntVal(Ctx.getTypeSizeInChars(ElemTy).getQuantity(), IdxTy)
          .castAs<NonLoc>();

  // FIXME: Check that the size arg to the cstring function is divisible by
  // size of the actual element type?

  // The type of the argument to the cstring function is either char or wchar,
  // but thats not the type of the original array (or memory region).
  // Suppose the following:
  //   int t[5];
  //   memcpy(dst, t, sizeof(t) / sizeof(t[0]));
  // When checking whether t is fully initialized, we see it as char array of
  // size sizeof(int)*5. If we check the last element as a character, we read
  // the last byte of an integer, which will be undefined. But just because
  // that value is undefined, it doesn't mean that the element is uninitialized!
  // For this reason, we need to retrieve the actual last element with the
  // correct type.

  // Divide the size argument to the cstring function by the actual element
  // type. This value will be size of the array, or the index to the
  // past-the-end element.
  std::optional<NonLoc> Offset =
      SVB.evalBinOpNN(State, clang::BO_Div, Size.castAs<NonLoc>(), ElemSize,
                      IdxTy)
          .getAs<NonLoc>();

  // Retrieve the index of the last element.
  const NonLoc One = SVB.makeIntVal(1, IdxTy).castAs<NonLoc>();
  SVal LastIdx = SVB.evalBinOpNN(State, BO_Sub, *Offset, One, IdxTy);

  if (!Offset)
    return State;

  SVal LastElementVal =
      State->getLValue(ElemTy, LastIdx, loc::MemRegionVal(SuperR));
  if (!isa<Loc>(LastElementVal))
    return State;

  if (Filter.CheckCStringUninitializedRead &&
      State->getSVal(LastElementVal.castAs<Loc>()).isUndef()) {
    const llvm::APSInt *IdxInt = LastIdx.getAsInteger();
    // If we can't get emit a sensible last element index, just bail out --
    // prefer to emit nothing in favour of emitting garbage quality reports.
    if (!IdxInt) {
      C.addSink();
      return nullptr;
    }
    llvm::SmallString<258> Buf;
    llvm::raw_svector_ostream OS(Buf);
    OS << "The last accessed element (at index ";
    OS << IdxInt->getExtValue();
    OS << ") in the ";
    printIdxWithOrdinalSuffix(OS, Buffer.ArgumentIndex + 1);
    OS << " argument is undefined";
    emitUninitializedReadBug(C, State, Buffer.Expression, OS.str());
    return nullptr;
  }
  return State;
}

// FIXME: This was originally copied from ArrayBoundChecker.cpp. Refactor?
ProgramStateRef CStringChecker::CheckLocation(CheckerContext &C,
                                              ProgramStateRef state,
                                              AnyArgExpr Buffer, SVal Element,
                                              AccessKind Access,
                                              CharKind CK) const {

  // If a previous check has failed, propagate the failure.
  if (!state)
    return nullptr;

  // Check for out of bound array element access.
  const MemRegion *R = Element.getAsRegion();
  if (!R)
    return state;

  const auto *ER = dyn_cast<ElementRegion>(R);
  if (!ER)
    return state;

  // Get the index of the accessed element.
  std::optional<NonLoc> Idx = getIndex(state, ER, CK);
  if (!Idx)
    return state;

  // Get the size of the array.
  const auto *superReg = cast<SubRegion>(ER->getSuperRegion());
  DefinedOrUnknownSVal Size =
      getDynamicExtent(state, superReg, C.getSValBuilder());

  auto [StInBound, StOutBound] = state->assumeInBoundDual(*Idx, Size);
  if (StOutBound && !StInBound) {
    // These checks are either enabled by the CString out-of-bounds checker
    // explicitly or implicitly by the Malloc checker.
    // In the latter case we only do modeling but do not emit warning.
    if (!Filter.CheckCStringOutOfBounds)
      return nullptr;

    // Emit a bug report.
    ErrorMessage Message =
        createOutOfBoundErrorMsg(CurrentFunctionDescription, Access);
    emitOutOfBoundsBug(C, StOutBound, Buffer.Expression, Message);
    return nullptr;
  }

  // Array bound check succeeded.  From this point forward the array bound
  // should always succeed.
  return StInBound;
}

ProgramStateRef
CStringChecker::CheckBufferAccess(CheckerContext &C, ProgramStateRef State,
                                  AnyArgExpr Buffer, SizeArgExpr Size,
                                  AccessKind Access, CharKind CK) const {
  // If a previous check has failed, propagate the failure.
  if (!State)
    return nullptr;

  SValBuilder &svalBuilder = C.getSValBuilder();
  ASTContext &Ctx = svalBuilder.getContext();

  QualType SizeTy = Size.Expression->getType();
  QualType PtrTy = getCharPtrType(Ctx, CK);

  // Check that the first buffer is non-null.
  SVal BufVal = C.getSVal(Buffer.Expression);
  State = checkNonNull(C, State, Buffer, BufVal);
  if (!State)
    return nullptr;

  // If out-of-bounds checking is turned off, skip the rest.
  if (!Filter.CheckCStringOutOfBounds)
    return State;

  SVal BufStart =
      svalBuilder.evalCast(BufVal, PtrTy, Buffer.Expression->getType());

  // Check if the first byte of the buffer is accessible.
  State = CheckLocation(C, State, Buffer, BufStart, Access, CK);

  if (!State)
    return nullptr;

  // Get the access length and make sure it is known.
  // FIXME: This assumes the caller has already checked that the access length
  // is positive. And that it's unsigned.
  SVal LengthVal = C.getSVal(Size.Expression);
  std::optional<NonLoc> Length = LengthVal.getAs<NonLoc>();
  if (!Length)
    return State;

  // Compute the offset of the last element to be accessed: size-1.
  NonLoc One = svalBuilder.makeIntVal(1, SizeTy).castAs<NonLoc>();
  SVal Offset = svalBuilder.evalBinOpNN(State, BO_Sub, *Length, One, SizeTy);
  if (Offset.isUnknown())
    return nullptr;
  NonLoc LastOffset = Offset.castAs<NonLoc>();

  // Check that the first buffer is sufficiently long.
  if (std::optional<Loc> BufLoc = BufStart.getAs<Loc>()) {

    SVal BufEnd =
        svalBuilder.evalBinOpLN(State, BO_Add, *BufLoc, LastOffset, PtrTy);
    State = CheckLocation(C, State, Buffer, BufEnd, Access, CK);
    if (Access == AccessKind::read)
      State = checkInit(C, State, Buffer, BufEnd, *Length);

    // If the buffer isn't large enough, abort.
    if (!State)
      return nullptr;
  }

  // Large enough or not, return this state!
  return State;
}

ProgramStateRef CStringChecker::CheckOverlap(CheckerContext &C,
                                             ProgramStateRef state,
                                             SizeArgExpr Size, AnyArgExpr First,
                                             AnyArgExpr Second,
                                             CharKind CK) const {
  if (!Filter.CheckCStringBufferOverlap)
    return state;

  // Do a simple check for overlap: if the two arguments are from the same
  // buffer, see if the end of the first is greater than the start of the second
  // or vice versa.

  // If a previous check has failed, propagate the failure.
  if (!state)
    return nullptr;

  ProgramStateRef stateTrue, stateFalse;

  // Assume different address spaces cannot overlap.
  if (First.Expression->getType()->getPointeeType().getAddressSpace() !=
      Second.Expression->getType()->getPointeeType().getAddressSpace())
    return state;

  // Get the buffer values and make sure they're known locations.
  const LocationContext *LCtx = C.getLocationContext();
  SVal firstVal = state->getSVal(First.Expression, LCtx);
  SVal secondVal = state->getSVal(Second.Expression, LCtx);

  std::optional<Loc> firstLoc = firstVal.getAs<Loc>();
  if (!firstLoc)
    return state;

  std::optional<Loc> secondLoc = secondVal.getAs<Loc>();
  if (!secondLoc)
    return state;

  // Are the two values the same?
  SValBuilder &svalBuilder = C.getSValBuilder();
  std::tie(stateTrue, stateFalse) =
      state->assume(svalBuilder.evalEQ(state, *firstLoc, *secondLoc));

  if (stateTrue && !stateFalse) {
    // If the values are known to be equal, that's automatically an overlap.
    emitOverlapBug(C, stateTrue, First.Expression, Second.Expression);
    return nullptr;
  }

  // assume the two expressions are not equal.
  assert(stateFalse);
  state = stateFalse;

  // Which value comes first?
  QualType cmpTy = svalBuilder.getConditionType();
  SVal reverse =
      svalBuilder.evalBinOpLL(state, BO_GT, *firstLoc, *secondLoc, cmpTy);
  std::optional<DefinedOrUnknownSVal> reverseTest =
      reverse.getAs<DefinedOrUnknownSVal>();
  if (!reverseTest)
    return state;

  std::tie(stateTrue, stateFalse) = state->assume(*reverseTest);
  if (stateTrue) {
    if (stateFalse) {
      // If we don't know which one comes first, we can't perform this test.
      return state;
    } else {
      // Switch the values so that firstVal is before secondVal.
      std::swap(firstLoc, secondLoc);

      // Switch the Exprs as well, so that they still correspond.
      std::swap(First, Second);
    }
  }

  // Get the length, and make sure it too is known.
  SVal LengthVal = state->getSVal(Size.Expression, LCtx);
  std::optional<NonLoc> Length = LengthVal.getAs<NonLoc>();
  if (!Length)
    return state;

  // Convert the first buffer's start address to char*.
  // Bail out if the cast fails.
  ASTContext &Ctx = svalBuilder.getContext();
  QualType CharPtrTy = getCharPtrType(Ctx, CK);
  SVal FirstStart =
      svalBuilder.evalCast(*firstLoc, CharPtrTy, First.Expression->getType());
  std::optional<Loc> FirstStartLoc = FirstStart.getAs<Loc>();
  if (!FirstStartLoc)
    return state;

  // Compute the end of the first buffer. Bail out if THAT fails.
  SVal FirstEnd = svalBuilder.evalBinOpLN(state, BO_Add, *FirstStartLoc,
                                          *Length, CharPtrTy);
  std::optional<Loc> FirstEndLoc = FirstEnd.getAs<Loc>();
  if (!FirstEndLoc)
    return state;

  // Is the end of the first buffer past the start of the second buffer?
  SVal Overlap =
      svalBuilder.evalBinOpLL(state, BO_GT, *FirstEndLoc, *secondLoc, cmpTy);
  std::optional<DefinedOrUnknownSVal> OverlapTest =
      Overlap.getAs<DefinedOrUnknownSVal>();
  if (!OverlapTest)
    return state;

  std::tie(stateTrue, stateFalse) = state->assume(*OverlapTest);

  if (stateTrue && !stateFalse) {
    // Overlap!
    emitOverlapBug(C, stateTrue, First.Expression, Second.Expression);
    return nullptr;
  }

  // assume the two expressions don't overlap.
  assert(stateFalse);
  return stateFalse;
}

void CStringChecker::emitOverlapBug(CheckerContext &C, ProgramStateRef state,
                                  const Stmt *First, const Stmt *Second) const {
  ExplodedNode *N = C.generateErrorNode(state);
  if (!N)
    return;

  if (!BT_Overlap)
    BT_Overlap.reset(new BugType(Filter.CheckNameCStringBufferOverlap,
                                 categories::UnixAPI, "Improper arguments"));

  // Generate a report for this bug.
  auto report = std::make_unique<PathSensitiveBugReport>(
      *BT_Overlap, "Arguments must not be overlapping buffers", N);
  report->addRange(First->getSourceRange());
  report->addRange(Second->getSourceRange());

  C.emitReport(std::move(report));
}

void CStringChecker::emitNullArgBug(CheckerContext &C, ProgramStateRef State,
                                    const Stmt *S, StringRef WarningMsg) const {
  if (ExplodedNode *N = C.generateErrorNode(State)) {
    if (!BT_Null) {
      // FIXME: This call uses the string constant 'categories::UnixAPI' as the
      // description of the bug; it should be replaced by a real description.
      BT_Null.reset(
          new BugType(Filter.CheckNameCStringNullArg, categories::UnixAPI));
    }

    auto Report =
        std::make_unique<PathSensitiveBugReport>(*BT_Null, WarningMsg, N);
    Report->addRange(S->getSourceRange());
    if (const auto *Ex = dyn_cast<Expr>(S))
      bugreporter::trackExpressionValue(N, Ex, *Report);
    C.emitReport(std::move(Report));
  }
}

void CStringChecker::emitUninitializedReadBug(CheckerContext &C,
                                              ProgramStateRef State,
                                              const Expr *E,
                                              StringRef Msg) const {
  if (ExplodedNode *N = C.generateErrorNode(State)) {
    if (!BT_UninitRead)
      BT_UninitRead.reset(new BugType(Filter.CheckNameCStringUninitializedRead,
                                      "Accessing unitialized/garbage values"));

    auto Report =
        std::make_unique<PathSensitiveBugReport>(*BT_UninitRead, Msg, N);
    Report->addNote("Other elements might also be undefined",
                    Report->getLocation());
    Report->addRange(E->getSourceRange());
    bugreporter::trackExpressionValue(N, E, *Report);
    C.emitReport(std::move(Report));
  }
}

void CStringChecker::emitOutOfBoundsBug(CheckerContext &C,
                                        ProgramStateRef State, const Stmt *S,
                                        StringRef WarningMsg) const {
  if (ExplodedNode *N = C.generateErrorNode(State)) {
    if (!BT_Bounds)
      BT_Bounds.reset(new BugType(Filter.CheckCStringOutOfBounds
                                      ? Filter.CheckNameCStringOutOfBounds
                                      : Filter.CheckNameCStringNullArg,
                                  "Out-of-bound array access"));

    // FIXME: It would be nice to eventually make this diagnostic more clear,
    // e.g., by referencing the original declaration or by saying *why* this
    // reference is outside the range.
    auto Report =
        std::make_unique<PathSensitiveBugReport>(*BT_Bounds, WarningMsg, N);
    Report->addRange(S->getSourceRange());
    C.emitReport(std::move(Report));
  }
}

void CStringChecker::emitNotCStringBug(CheckerContext &C, ProgramStateRef State,
                                       const Stmt *S,
                                       StringRef WarningMsg) const {
  if (ExplodedNode *N = C.generateNonFatalErrorNode(State)) {
    if (!BT_NotCString) {
      // FIXME: This call uses the string constant 'categories::UnixAPI' as the
      // description of the bug; it should be replaced by a real description.
      BT_NotCString.reset(
          new BugType(Filter.CheckNameCStringNotNullTerm, categories::UnixAPI));
    }

    auto Report =
        std::make_unique<PathSensitiveBugReport>(*BT_NotCString, WarningMsg, N);

    Report->addRange(S->getSourceRange());
    C.emitReport(std::move(Report));
  }
}

void CStringChecker::emitAdditionOverflowBug(CheckerContext &C,
                                             ProgramStateRef State) const {
  if (ExplodedNode *N = C.generateErrorNode(State)) {
    if (!BT_AdditionOverflow) {
      // FIXME: This call uses the word "API" as the description of the bug;
      // it should be replaced by a better error message (if this unlikely
      // situation continues to exist as a separate bug type).
      BT_AdditionOverflow.reset(
          new BugType(Filter.CheckNameCStringOutOfBounds, "API"));
    }

    // This isn't a great error message, but this should never occur in real
    // code anyway -- you'd have to create a buffer longer than a size_t can
    // represent, which is sort of a contradiction.
    const char *WarningMsg =
        "This expression will create a string whose length is too big to "
        "be represented as a size_t";

    auto Report = std::make_unique<PathSensitiveBugReport>(*BT_AdditionOverflow,
                                                           WarningMsg, N);
    C.emitReport(std::move(Report));
  }
}

ProgramStateRef CStringChecker::checkAdditionOverflow(CheckerContext &C,
                                                     ProgramStateRef state,
                                                     NonLoc left,
                                                     NonLoc right) const {
  // If out-of-bounds checking is turned off, skip the rest.
  if (!Filter.CheckCStringOutOfBounds)
    return state;

  // If a previous check has failed, propagate the failure.
  if (!state)
    return nullptr;

  SValBuilder &svalBuilder = C.getSValBuilder();
  BasicValueFactory &BVF = svalBuilder.getBasicValueFactory();

  QualType sizeTy = svalBuilder.getContext().getSizeType();
  const llvm::APSInt &maxValInt = BVF.getMaxValue(sizeTy);
  NonLoc maxVal = svalBuilder.makeIntVal(maxValInt);

  SVal maxMinusRight;
  if (isa<nonloc::ConcreteInt>(right)) {
    maxMinusRight = svalBuilder.evalBinOpNN(state, BO_Sub, maxVal, right,
                                                 sizeTy);
  } else {
    // Try switching the operands. (The order of these two assignments is
    // important!)
    maxMinusRight = svalBuilder.evalBinOpNN(state, BO_Sub, maxVal, left,
                                            sizeTy);
    left = right;
  }

  if (std::optional<NonLoc> maxMinusRightNL = maxMinusRight.getAs<NonLoc>()) {
    QualType cmpTy = svalBuilder.getConditionType();
    // If left > max - right, we have an overflow.
    SVal willOverflow = svalBuilder.evalBinOpNN(state, BO_GT, left,
                                                *maxMinusRightNL, cmpTy);

    ProgramStateRef stateOverflow, stateOkay;
    std::tie(stateOverflow, stateOkay) =
      state->assume(willOverflow.castAs<DefinedOrUnknownSVal>());

    if (stateOverflow && !stateOkay) {
      // We have an overflow. Emit a bug report.
      emitAdditionOverflowBug(C, stateOverflow);
      return nullptr;
    }

    // From now on, assume an overflow didn't occur.
    assert(stateOkay);
    state = stateOkay;
  }

  return state;
}

ProgramStateRef CStringChecker::setCStringLength(ProgramStateRef state,
                                                const MemRegion *MR,
                                                SVal strLength) {
  assert(!strLength.isUndef() && "Attempt to set an undefined string length");

  MR = MR->StripCasts();

  switch (MR->getKind()) {
  case MemRegion::StringRegionKind:
    // FIXME: This can happen if we strcpy() into a string region. This is
    // undefined [C99 6.4.5p6], but we should still warn about it.
    return state;

  case MemRegion::SymbolicRegionKind:
  case MemRegion::AllocaRegionKind:
  case MemRegion::NonParamVarRegionKind:
  case MemRegion::ParamVarRegionKind:
  case MemRegion::FieldRegionKind:
  case MemRegion::ObjCIvarRegionKind:
    // These are the types we can currently track string lengths for.
    break;

  case MemRegion::ElementRegionKind:
    // FIXME: Handle element regions by upper-bounding the parent region's
    // string length.
    return state;

  default:
    // Other regions (mostly non-data) can't have a reliable C string length.
    // For now, just ignore the change.
    // FIXME: These are rare but not impossible. We should output some kind of
    // warning for things like strcpy((char[]){'a', 0}, "b");
    return state;
  }

  if (strLength.isUnknown())
    return state->remove<CStringLength>(MR);

  return state->set<CStringLength>(MR, strLength);
}

SVal CStringChecker::getCStringLengthForRegion(CheckerContext &C,
                                               ProgramStateRef &state,
                                               const Expr *Ex,
                                               const MemRegion *MR,
                                               bool hypothetical) {
  if (!hypothetical) {
    // If there's a recorded length, go ahead and return it.
    const SVal *Recorded = state->get<CStringLength>(MR);
    if (Recorded)
      return *Recorded;
  }

  // Otherwise, get a new symbol and update the state.
  SValBuilder &svalBuilder = C.getSValBuilder();
  QualType sizeTy = svalBuilder.getContext().getSizeType();
  SVal strLength = svalBuilder.getMetadataSymbolVal(CStringChecker::getTag(),
                                                    MR, Ex, sizeTy,
                                                    C.getLocationContext(),
                                                    C.blockCount());

  if (!hypothetical) {
    if (std::optional<NonLoc> strLn = strLength.getAs<NonLoc>()) {
      // In case of unbounded calls strlen etc bound the range to SIZE_MAX/4
      BasicValueFactory &BVF = svalBuilder.getBasicValueFactory();
      const llvm::APSInt &maxValInt = BVF.getMaxValue(sizeTy);
      llvm::APSInt fourInt = APSIntType(maxValInt).getValue(4);
      const llvm::APSInt *maxLengthInt = BVF.evalAPSInt(BO_Div, maxValInt,
                                                        fourInt);
      NonLoc maxLength = svalBuilder.makeIntVal(*maxLengthInt);
      SVal evalLength = svalBuilder.evalBinOpNN(state, BO_LE, *strLn, maxLength,
                                                svalBuilder.getConditionType());
      state = state->assume(evalLength.castAs<DefinedOrUnknownSVal>(), true);
    }
    state = state->set<CStringLength>(MR, strLength);
  }

  return strLength;
}

SVal CStringChecker::getCStringLength(CheckerContext &C, ProgramStateRef &state,
                                      const Expr *Ex, SVal Buf,
                                      bool hypothetical) const {
  const MemRegion *MR = Buf.getAsRegion();
  if (!MR) {
    // If we can't get a region, see if it's something we /know/ isn't a
    // C string. In the context of locations, the only time we can issue such
    // a warning is for labels.
    if (std::optional<loc::GotoLabel> Label = Buf.getAs<loc::GotoLabel>()) {
      if (Filter.CheckCStringNotNullTerm) {
        SmallString<120> buf;
        llvm::raw_svector_ostream os(buf);
        assert(CurrentFunctionDescription);
        os << "Argument to " << CurrentFunctionDescription
           << " is the address of the label '" << Label->getLabel()->getName()
           << "', which is not a null-terminated string";

        emitNotCStringBug(C, state, Ex, os.str());
      }
      return UndefinedVal();
    }

    // If it's not a region and not a label, give up.
    return UnknownVal();
  }

  // If we have a region, strip casts from it and see if we can figure out
  // its length. For anything we can't figure out, just return UnknownVal.
  MR = MR->StripCasts();

  switch (MR->getKind()) {
  case MemRegion::StringRegionKind: {
    // Modifying the contents of string regions is undefined [C99 6.4.5p6],
    // so we can assume that the byte length is the correct C string length.
    SValBuilder &svalBuilder = C.getSValBuilder();
    QualType sizeTy = svalBuilder.getContext().getSizeType();
    const StringLiteral *strLit = cast<StringRegion>(MR)->getStringLiteral();
    return svalBuilder.makeIntVal(strLit->getLength(), sizeTy);
  }
  case MemRegion::NonParamVarRegionKind: {
    // If we have a global constant with a string literal initializer,
    // compute the initializer's length.
    const VarDecl *Decl = cast<NonParamVarRegion>(MR)->getDecl();
    if (Decl->getType().isConstQualified() && Decl->hasGlobalStorage()) {
      if (const Expr *Init = Decl->getInit()) {
        if (auto *StrLit = dyn_cast<StringLiteral>(Init)) {
          SValBuilder &SvalBuilder = C.getSValBuilder();
          QualType SizeTy = SvalBuilder.getContext().getSizeType();
          return SvalBuilder.makeIntVal(StrLit->getLength(), SizeTy);
        }
      }
    }
    [[fallthrough]];
  }
  case MemRegion::SymbolicRegionKind:
  case MemRegion::AllocaRegionKind:
  case MemRegion::ParamVarRegionKind:
  case MemRegion::FieldRegionKind:
  case MemRegion::ObjCIvarRegionKind:
    return getCStringLengthForRegion(C, state, Ex, MR, hypothetical);
  case MemRegion::CompoundLiteralRegionKind:
    // FIXME: Can we track this? Is it necessary?
    return UnknownVal();
  case MemRegion::ElementRegionKind:
    // FIXME: How can we handle this? It's not good enough to subtract the
    // offset from the base string length; consider "123\x00567" and &a[5].
    return UnknownVal();
  default:
    // Other regions (mostly non-data) can't have a reliable C string length.
    // In this case, an error is emitted and UndefinedVal is returned.
    // The caller should always be prepared to handle this case.
    if (Filter.CheckCStringNotNullTerm) {
      SmallString<120> buf;
      llvm::raw_svector_ostream os(buf);

      assert(CurrentFunctionDescription);
      os << "Argument to " << CurrentFunctionDescription << " is ";

      if (SummarizeRegion(os, C.getASTContext(), MR))
        os << ", which is not a null-terminated string";
      else
        os << "not a null-terminated string";

      emitNotCStringBug(C, state, Ex, os.str());
    }
    return UndefinedVal();
  }
}

const StringLiteral *CStringChecker::getCStringLiteral(CheckerContext &C,
  ProgramStateRef &state, const Expr *expr, SVal val) const {

  // Get the memory region pointed to by the val.
  const MemRegion *bufRegion = val.getAsRegion();
  if (!bufRegion)
    return nullptr;

  // Strip casts off the memory region.
  bufRegion = bufRegion->StripCasts();

  // Cast the memory region to a string region.
  const StringRegion *strRegion= dyn_cast<StringRegion>(bufRegion);
  if (!strRegion)
    return nullptr;

  // Return the actual string in the string region.
  return strRegion->getStringLiteral();
}

bool CStringChecker::isFirstBufInBound(CheckerContext &C, ProgramStateRef State,
                                       SVal BufVal, QualType BufTy,
                                       SVal LengthVal, QualType LengthTy) {
  // If we do not know that the buffer is long enough we return 'true'.
  // Otherwise the parent region of this field region would also get
  // invalidated, which would lead to warnings based on an unknown state.

  if (LengthVal.isUnknown())
    return false;

  // Originally copied from CheckBufferAccess and CheckLocation.
  SValBuilder &SB = C.getSValBuilder();
  ASTContext &Ctx = C.getASTContext();

  QualType PtrTy = Ctx.getPointerType(Ctx.CharTy);

  std::optional<NonLoc> Length = LengthVal.getAs<NonLoc>();
  if (!Length)
    return true; // cf top comment.

  // Compute the offset of the last element to be accessed: size-1.
  NonLoc One = SB.makeIntVal(1, LengthTy).castAs<NonLoc>();
  SVal Offset = SB.evalBinOpNN(State, BO_Sub, *Length, One, LengthTy);
  if (Offset.isUnknown())
    return true; // cf top comment
  NonLoc LastOffset = Offset.castAs<NonLoc>();

  // Check that the first buffer is sufficiently long.
  SVal BufStart = SB.evalCast(BufVal, PtrTy, BufTy);
  std::optional<Loc> BufLoc = BufStart.getAs<Loc>();
  if (!BufLoc)
    return true; // cf top comment.

  SVal BufEnd = SB.evalBinOpLN(State, BO_Add, *BufLoc, LastOffset, PtrTy);

  // Check for out of bound array element access.
  const MemRegion *R = BufEnd.getAsRegion();
  if (!R)
    return true; // cf top comment.

  const ElementRegion *ER = dyn_cast<ElementRegion>(R);
  if (!ER)
    return true; // cf top comment.

  // FIXME: Does this crash when a non-standard definition
  // of a library function is encountered?
  assert(ER->getValueType() == C.getASTContext().CharTy &&
         "isFirstBufInBound should only be called with char* ElementRegions");

  // Get the size of the array.
  const SubRegion *superReg = cast<SubRegion>(ER->getSuperRegion());
  DefinedOrUnknownSVal SizeDV = getDynamicExtent(State, superReg, SB);

  // Get the index of the accessed element.
  DefinedOrUnknownSVal Idx = ER->getIndex().castAs<DefinedOrUnknownSVal>();

  ProgramStateRef StInBound = State->assumeInBound(Idx, SizeDV, true);

  return static_cast<bool>(StInBound);
}

ProgramStateRef CStringChecker::invalidateDestinationBufferBySize(
    CheckerContext &C, ProgramStateRef S, const Expr *BufE, SVal BufV,
    SVal SizeV, QualType SizeTy) {
  auto InvalidationTraitOperations =
      [&C, S, BufTy = BufE->getType(), BufV, SizeV,
       SizeTy](RegionAndSymbolInvalidationTraits &ITraits, const MemRegion *R) {
        // If destination buffer is a field region and access is in bound, do
        // not invalidate its super region.
        if (MemRegion::FieldRegionKind == R->getKind() &&
            isFirstBufInBound(C, S, BufV, BufTy, SizeV, SizeTy)) {
          ITraits.setTrait(
              R,
              RegionAndSymbolInvalidationTraits::TK_DoNotInvalidateSuperRegion);
        }
        return false;
      };

  return invalidateBufferAux(C, S, BufE, BufV, InvalidationTraitOperations);
}

ProgramStateRef
CStringChecker::invalidateDestinationBufferAlwaysEscapeSuperRegion(
    CheckerContext &C, ProgramStateRef S, const Expr *BufE, SVal BufV) {
  auto InvalidationTraitOperations = [](RegionAndSymbolInvalidationTraits &,
                                        const MemRegion *R) {
    return isa<FieldRegion>(R);
  };

  return invalidateBufferAux(C, S, BufE, BufV, InvalidationTraitOperations);
}

ProgramStateRef CStringChecker::invalidateDestinationBufferNeverOverflows(
    CheckerContext &C, ProgramStateRef S, const Expr *BufE, SVal BufV) {
  auto InvalidationTraitOperations =
      [](RegionAndSymbolInvalidationTraits &ITraits, const MemRegion *R) {
        if (MemRegion::FieldRegionKind == R->getKind())
          ITraits.setTrait(
              R,
              RegionAndSymbolInvalidationTraits::TK_DoNotInvalidateSuperRegion);
        return false;
      };

  return invalidateBufferAux(C, S, BufE, BufV, InvalidationTraitOperations);
}

ProgramStateRef CStringChecker::invalidateSourceBuffer(CheckerContext &C,
                                                       ProgramStateRef S,
                                                       const Expr *BufE,
                                                       SVal BufV) {
  auto InvalidationTraitOperations =
      [](RegionAndSymbolInvalidationTraits &ITraits, const MemRegion *R) {
        ITraits.setTrait(
            R->getBaseRegion(),
            RegionAndSymbolInvalidationTraits::TK_PreserveContents);
        ITraits.setTrait(R,
                         RegionAndSymbolInvalidationTraits::TK_SuppressEscape);
        return true;
      };

  return invalidateBufferAux(C, S, BufE, BufV, InvalidationTraitOperations);
}

ProgramStateRef CStringChecker::invalidateBufferAux(
    CheckerContext &C, ProgramStateRef State, const Expr *E, SVal V,
    llvm::function_ref<bool(RegionAndSymbolInvalidationTraits &,
                            const MemRegion *)>
        InvalidationTraitOperations) {
  std::optional<Loc> L = V.getAs<Loc>();
  if (!L)
    return State;

  // FIXME: This is a simplified version of what's in CFRefCount.cpp -- it makes
  // some assumptions about the value that CFRefCount can't. Even so, it should
  // probably be refactored.
  if (std::optional<loc::MemRegionVal> MR = L->getAs<loc::MemRegionVal>()) {
    const MemRegion *R = MR->getRegion()->StripCasts();

    // Are we dealing with an ElementRegion?  If so, we should be invalidating
    // the super-region.
    if (const ElementRegion *ER = dyn_cast<ElementRegion>(R)) {
      R = ER->getSuperRegion();
      // FIXME: What about layers of ElementRegions?
    }

    // Invalidate this region.
    const LocationContext *LCtx = C.getPredecessor()->getLocationContext();
    RegionAndSymbolInvalidationTraits ITraits;
    bool CausesPointerEscape = InvalidationTraitOperations(ITraits, R);

    return State->invalidateRegions(R, E, C.blockCount(), LCtx,
                                    CausesPointerEscape, nullptr, nullptr,
                                    &ITraits);
  }

  // If we have a non-region value by chance, just remove the binding.
  // FIXME: is this necessary or correct? This handles the non-Region
  //  cases.  Is it ever valid to store to these?
  return State->killBinding(*L);
}

bool CStringChecker::SummarizeRegion(raw_ostream &os, ASTContext &Ctx,
                                     const MemRegion *MR) {
  switch (MR->getKind()) {
  case MemRegion::FunctionCodeRegionKind: {
    if (const auto *FD = cast<FunctionCodeRegion>(MR)->getDecl())
      os << "the address of the function '" << *FD << '\'';
    else
      os << "the address of a function";
    return true;
  }
  case MemRegion::BlockCodeRegionKind:
    os << "block text";
    return true;
  case MemRegion::BlockDataRegionKind:
    os << "a block";
    return true;
  case MemRegion::CXXThisRegionKind:
  case MemRegion::CXXTempObjectRegionKind:
    os << "a C++ temp object of type "
       << cast<TypedValueRegion>(MR)->getValueType();
    return true;
  case MemRegion::NonParamVarRegionKind:
    os << "a variable of type" << cast<TypedValueRegion>(MR)->getValueType();
    return true;
  case MemRegion::ParamVarRegionKind:
    os << "a parameter of type" << cast<TypedValueRegion>(MR)->getValueType();
    return true;
  case MemRegion::FieldRegionKind:
    os << "a field of type " << cast<TypedValueRegion>(MR)->getValueType();
    return true;
  case MemRegion::ObjCIvarRegionKind:
    os << "an instance variable of type "
       << cast<TypedValueRegion>(MR)->getValueType();
    return true;
  default:
    return false;
  }
}

bool CStringChecker::memsetAux(const Expr *DstBuffer, SVal CharVal,
                               const Expr *Size, CheckerContext &C,
                               ProgramStateRef &State) {
  SVal MemVal = C.getSVal(DstBuffer);
  SVal SizeVal = C.getSVal(Size);
  const MemRegion *MR = MemVal.getAsRegion();
  if (!MR)
    return false;

  // We're about to model memset by producing a "default binding" in the Store.
  // Our current implementation - RegionStore - doesn't support default bindings
  // that don't cover the whole base region. So we should first get the offset
  // and the base region to figure out whether the offset of buffer is 0.
  RegionOffset Offset = MR->getAsOffset();
  const MemRegion *BR = Offset.getRegion();

  std::optional<NonLoc> SizeNL = SizeVal.getAs<NonLoc>();
  if (!SizeNL)
    return false;

  SValBuilder &svalBuilder = C.getSValBuilder();
  ASTContext &Ctx = C.getASTContext();

  // void *memset(void *dest, int ch, size_t count);
  // For now we can only handle the case of offset is 0 and concrete char value.
  if (Offset.isValid() && !Offset.hasSymbolicOffset() &&
      Offset.getOffset() == 0) {
    // Get the base region's size.
    DefinedOrUnknownSVal SizeDV = getDynamicExtent(State, BR, svalBuilder);

    ProgramStateRef StateWholeReg, StateNotWholeReg;
    std::tie(StateWholeReg, StateNotWholeReg) =
        State->assume(svalBuilder.evalEQ(State, SizeDV, *SizeNL));

    // With the semantic of 'memset()', we should convert the CharVal to
    // unsigned char.
    CharVal = svalBuilder.evalCast(CharVal, Ctx.UnsignedCharTy, Ctx.IntTy);

    ProgramStateRef StateNullChar, StateNonNullChar;
    std::tie(StateNullChar, StateNonNullChar) =
        assumeZero(C, State, CharVal, Ctx.UnsignedCharTy);

    if (StateWholeReg && !StateNotWholeReg && StateNullChar &&
        !StateNonNullChar) {
      // If the 'memset()' acts on the whole region of destination buffer and
      // the value of the second argument of 'memset()' is zero, bind the second
      // argument's value to the destination buffer with 'default binding'.
      // FIXME: Since there is no perfect way to bind the non-zero character, we
      // can only deal with zero value here. In the future, we need to deal with
      // the binding of non-zero value in the case of whole region.
      State = State->bindDefaultZero(svalBuilder.makeLoc(BR),
                                     C.getLocationContext());
    } else {
      // If the destination buffer's extent is not equal to the value of
      // third argument, just invalidate buffer.
      State = invalidateDestinationBufferBySize(C, State, DstBuffer, MemVal,
                                                SizeVal, Size->getType());
    }

    if (StateNullChar && !StateNonNullChar) {
      // If the value of the second argument of 'memset()' is zero, set the
      // string length of destination buffer to 0 directly.
      State = setCStringLength(State, MR,
                               svalBuilder.makeZeroVal(Ctx.getSizeType()));
    } else if (!StateNullChar && StateNonNullChar) {
      SVal NewStrLen = svalBuilder.getMetadataSymbolVal(
          CStringChecker::getTag(), MR, DstBuffer, Ctx.getSizeType(),
          C.getLocationContext(), C.blockCount());

      // If the value of second argument is not zero, then the string length
      // is at least the size argument.
      SVal NewStrLenGESize = svalBuilder.evalBinOp(
          State, BO_GE, NewStrLen, SizeVal, svalBuilder.getConditionType());

      State = setCStringLength(
          State->assume(NewStrLenGESize.castAs<DefinedOrUnknownSVal>(), true),
          MR, NewStrLen);
    }
  } else {
    // If the offset is not zero and char value is not concrete, we can do
    // nothing but invalidate the buffer.
    State = invalidateDestinationBufferBySize(C, State, DstBuffer, MemVal,
                                              SizeVal, Size->getType());
  }
  return true;
}

//===----------------------------------------------------------------------===//
// evaluation of individual function calls.
//===----------------------------------------------------------------------===//

void CStringChecker::evalCopyCommon(CheckerContext &C, const CallEvent &Call,
                                    ProgramStateRef state, SizeArgExpr Size,
                                    DestinationArgExpr Dest,
                                    SourceArgExpr Source, bool Restricted,
                                    bool IsMempcpy, CharKind CK) const {
  CurrentFunctionDescription = "memory copy function";

  // See if the size argument is zero.
  const LocationContext *LCtx = C.getLocationContext();
  SVal sizeVal = state->getSVal(Size.Expression, LCtx);
  QualType sizeTy = Size.Expression->getType();

  ProgramStateRef stateZeroSize, stateNonZeroSize;
  std::tie(stateZeroSize, stateNonZeroSize) =
      assumeZero(C, state, sizeVal, sizeTy);

  // Get the value of the Dest.
  SVal destVal = state->getSVal(Dest.Expression, LCtx);

  // If the size is zero, there won't be any actual memory access, so
  // just bind the return value to the destination buffer and return.
  if (stateZeroSize && !stateNonZeroSize) {
    stateZeroSize =
        stateZeroSize->BindExpr(Call.getOriginExpr(), LCtx, destVal);
    C.addTransition(stateZeroSize);
    return;
  }

  // If the size can be nonzero, we have to check the other arguments.
  if (stateNonZeroSize) {
    // TODO: If Size is tainted and we cannot prove that it is smaller or equal
    // to the size of the destination buffer, then emit a warning
    // that an attacker may provoke a buffer overflow error.
    state = stateNonZeroSize;

    // Ensure the destination is not null. If it is NULL there will be a
    // NULL pointer dereference.
    state = checkNonNull(C, state, Dest, destVal);
    if (!state)
      return;

    // Get the value of the Src.
    SVal srcVal = state->getSVal(Source.Expression, LCtx);

    // Ensure the source is not null. If it is NULL there will be a
    // NULL pointer dereference.
    state = checkNonNull(C, state, Source, srcVal);
    if (!state)
      return;

    // Ensure the accesses are valid and that the buffers do not overlap.
    state = CheckBufferAccess(C, state, Dest, Size, AccessKind::write, CK);
    state = CheckBufferAccess(C, state, Source, Size, AccessKind::read, CK);

    if (Restricted)
      state = CheckOverlap(C, state, Size, Dest, Source, CK);

    if (!state)
      return;

    // If this is mempcpy, get the byte after the last byte copied and
    // bind the expr.
    if (IsMempcpy) {
      // Get the byte after the last byte copied.
      SValBuilder &SvalBuilder = C.getSValBuilder();
      ASTContext &Ctx = SvalBuilder.getContext();
      QualType CharPtrTy = getCharPtrType(Ctx, CK);
      SVal DestRegCharVal =
          SvalBuilder.evalCast(destVal, CharPtrTy, Dest.Expression->getType());
      SVal lastElement = C.getSValBuilder().evalBinOp(
          state, BO_Add, DestRegCharVal, sizeVal, Dest.Expression->getType());
      // If we don't know how much we copied, we can at least
      // conjure a return value for later.
      if (lastElement.isUnknown())
        lastElement = C.getSValBuilder().conjureSymbolVal(
            nullptr, Call.getOriginExpr(), LCtx, C.blockCount());

      // The byte after the last byte copied is the return value.
      state = state->BindExpr(Call.getOriginExpr(), LCtx, lastElement);
    } else {
      // All other copies return the destination buffer.
      // (Well, bcopy() has a void return type, but this won't hurt.)
      state = state->BindExpr(Call.getOriginExpr(), LCtx, destVal);
    }

    // Invalidate the destination (regular invalidation without pointer-escaping
    // the address of the top-level region).
    // FIXME: Even if we can't perfectly model the copy, we should see if we
    // can use LazyCompoundVals to copy the source values into the destination.
    // This would probably remove any existing bindings past the end of the
    // copied region, but that's still an improvement over blank invalidation.
    state = invalidateDestinationBufferBySize(
        C, state, Dest.Expression, C.getSVal(Dest.Expression), sizeVal,
        Size.Expression->getType());

    // Invalidate the source (const-invalidation without const-pointer-escaping
    // the address of the top-level region).
    state = invalidateSourceBuffer(C, state, Source.Expression,
                                   C.getSVal(Source.Expression));

    C.addTransition(state);
  }
}

void CStringChecker::evalMemcpy(CheckerContext &C, const CallEvent &Call,
                                CharKind CK) const {
  // void *memcpy(void *restrict dst, const void *restrict src, size_t n);
  // The return value is the address of the destination buffer.
  DestinationArgExpr Dest = {{Call.getArgExpr(0), 0}};
  SourceArgExpr Src = {{Call.getArgExpr(1), 1}};
  SizeArgExpr Size = {{Call.getArgExpr(2), 2}};

  ProgramStateRef State = C.getState();

  constexpr bool IsRestricted = true;
  constexpr bool IsMempcpy = false;
  evalCopyCommon(C, Call, State, Size, Dest, Src, IsRestricted, IsMempcpy, CK);
}

void CStringChecker::evalMempcpy(CheckerContext &C, const CallEvent &Call,
                                 CharKind CK) const {
  // void *mempcpy(void *restrict dst, const void *restrict src, size_t n);
  // The return value is a pointer to the byte following the last written byte.
  DestinationArgExpr Dest = {{Call.getArgExpr(0), 0}};
  SourceArgExpr Src = {{Call.getArgExpr(1), 1}};
  SizeArgExpr Size = {{Call.getArgExpr(2), 2}};

  constexpr bool IsRestricted = true;
  constexpr bool IsMempcpy = true;
  evalCopyCommon(C, Call, C.getState(), Size, Dest, Src, IsRestricted,
                 IsMempcpy, CK);
}

void CStringChecker::evalMemmove(CheckerContext &C, const CallEvent &Call,
                                 CharKind CK) const {
  // void *memmove(void *dst, const void *src, size_t n);
  // The return value is the address of the destination buffer.
  DestinationArgExpr Dest = {{Call.getArgExpr(0), 0}};
  SourceArgExpr Src = {{Call.getArgExpr(1), 1}};
  SizeArgExpr Size = {{Call.getArgExpr(2), 2}};

  constexpr bool IsRestricted = false;
  constexpr bool IsMempcpy = false;
  evalCopyCommon(C, Call, C.getState(), Size, Dest, Src, IsRestricted,
                 IsMempcpy, CK);
}

void CStringChecker::evalBcopy(CheckerContext &C, const CallEvent &Call) const {
  // void bcopy(const void *src, void *dst, size_t n);
  SourceArgExpr Src{{Call.getArgExpr(0), 0}};
  DestinationArgExpr Dest = {{Call.getArgExpr(1), 1}};
  SizeArgExpr Size = {{Call.getArgExpr(2), 2}};

  constexpr bool IsRestricted = false;
  constexpr bool IsMempcpy = false;
  evalCopyCommon(C, Call, C.getState(), Size, Dest, Src, IsRestricted,
                 IsMempcpy, CharKind::Regular);
}

void CStringChecker::evalMemcmp(CheckerContext &C, const CallEvent &Call,
                                CharKind CK) const {
  // int memcmp(const void *s1, const void *s2, size_t n);
  CurrentFunctionDescription = "memory comparison function";

  AnyArgExpr Left = {Call.getArgExpr(0), 0};
  AnyArgExpr Right = {Call.getArgExpr(1), 1};
  SizeArgExpr Size = {{Call.getArgExpr(2), 2}};

  ProgramStateRef State = C.getState();
  SValBuilder &Builder = C.getSValBuilder();
  const LocationContext *LCtx = C.getLocationContext();

  // See if the size argument is zero.
  SVal sizeVal = State->getSVal(Size.Expression, LCtx);
  QualType sizeTy = Size.Expression->getType();

  ProgramStateRef stateZeroSize, stateNonZeroSize;
  std::tie(stateZeroSize, stateNonZeroSize) =
      assumeZero(C, State, sizeVal, sizeTy);

  // If the size can be zero, the result will be 0 in that case, and we don't
  // have to check either of the buffers.
  if (stateZeroSize) {
    State = stateZeroSize;
    State = State->BindExpr(Call.getOriginExpr(), LCtx,
                            Builder.makeZeroVal(Call.getResultType()));
    C.addTransition(State);
  }

  // If the size can be nonzero, we have to check the other arguments.
  if (stateNonZeroSize) {
    State = stateNonZeroSize;
    // If we know the two buffers are the same, we know the result is 0.
    // First, get the two buffers' addresses. Another checker will have already
    // made sure they're not undefined.
    DefinedOrUnknownSVal LV =
        State->getSVal(Left.Expression, LCtx).castAs<DefinedOrUnknownSVal>();
    DefinedOrUnknownSVal RV =
        State->getSVal(Right.Expression, LCtx).castAs<DefinedOrUnknownSVal>();

    // See if they are the same.
    ProgramStateRef SameBuffer, NotSameBuffer;
    std::tie(SameBuffer, NotSameBuffer) =
        State->assume(Builder.evalEQ(State, LV, RV));

    // If the two arguments are the same buffer, we know the result is 0,
    // and we only need to check one size.
    if (SameBuffer && !NotSameBuffer) {
      State = SameBuffer;
      State = CheckBufferAccess(C, State, Left, Size, AccessKind::read);
      if (State) {
        State = SameBuffer->BindExpr(Call.getOriginExpr(), LCtx,
                                     Builder.makeZeroVal(Call.getResultType()));
        C.addTransition(State);
      }
      return;
    }

    // If the two arguments might be different buffers, we have to check
    // the size of both of them.
    assert(NotSameBuffer);
    State = CheckBufferAccess(C, State, Right, Size, AccessKind::read, CK);
    State = CheckBufferAccess(C, State, Left, Size, AccessKind::read, CK);
    if (State) {
      // The return value is the comparison result, which we don't know.
      SVal CmpV = Builder.conjureSymbolVal(nullptr, Call.getOriginExpr(), LCtx,
                                           C.blockCount());
      State = State->BindExpr(Call.getOriginExpr(), LCtx, CmpV);
      C.addTransition(State);
    }
  }
}

void CStringChecker::evalstrLength(CheckerContext &C,
                                   const CallEvent &Call) const {
  // size_t strlen(const char *s);
  evalstrLengthCommon(C, Call, /* IsStrnlen = */ false);
}

void CStringChecker::evalstrnLength(CheckerContext &C,
                                    const CallEvent &Call) const {
  // size_t strnlen(const char *s, size_t maxlen);
  evalstrLengthCommon(C, Call, /* IsStrnlen = */ true);
}

void CStringChecker::evalstrLengthCommon(CheckerContext &C,
                                         const CallEvent &Call,
                                         bool IsStrnlen) const {
  CurrentFunctionDescription = "string length function";
  ProgramStateRef state = C.getState();
  const LocationContext *LCtx = C.getLocationContext();

  if (IsStrnlen) {
    const Expr *maxlenExpr = Call.getArgExpr(1);
    SVal maxlenVal = state->getSVal(maxlenExpr, LCtx);

    ProgramStateRef stateZeroSize, stateNonZeroSize;
    std::tie(stateZeroSize, stateNonZeroSize) =
      assumeZero(C, state, maxlenVal, maxlenExpr->getType());

    // If the size can be zero, the result will be 0 in that case, and we don't
    // have to check the string itself.
    if (stateZeroSize) {
      SVal zero = C.getSValBuilder().makeZeroVal(Call.getResultType());
      stateZeroSize = stateZeroSize->BindExpr(Call.getOriginExpr(), LCtx, zero);
      C.addTransition(stateZeroSize);
    }

    // If the size is GUARANTEED to be zero, we're done!
    if (!stateNonZeroSize)
      return;

    // Otherwise, record the assumption that the size is nonzero.
    state = stateNonZeroSize;
  }

  // Check that the string argument is non-null.
  AnyArgExpr Arg = {Call.getArgExpr(0), 0};
  SVal ArgVal = state->getSVal(Arg.Expression, LCtx);
  state = checkNonNull(C, state, Arg, ArgVal);

  if (!state)
    return;

  SVal strLength = getCStringLength(C, state, Arg.Expression, ArgVal);

  // If the argument isn't a valid C string, there's no valid state to
  // transition to.
  if (strLength.isUndef())
    return;

  DefinedOrUnknownSVal result = UnknownVal();

  // If the check is for strnlen() then bind the return value to no more than
  // the maxlen value.
  if (IsStrnlen) {
    QualType cmpTy = C.getSValBuilder().getConditionType();

    // It's a little unfortunate to be getting this again,
    // but it's not that expensive...
    const Expr *maxlenExpr = Call.getArgExpr(1);
    SVal maxlenVal = state->getSVal(maxlenExpr, LCtx);

    std::optional<NonLoc> strLengthNL = strLength.getAs<NonLoc>();
    std::optional<NonLoc> maxlenValNL = maxlenVal.getAs<NonLoc>();

    if (strLengthNL && maxlenValNL) {
      ProgramStateRef stateStringTooLong, stateStringNotTooLong;

      // Check if the strLength is greater than the maxlen.
      std::tie(stateStringTooLong, stateStringNotTooLong) = state->assume(
          C.getSValBuilder()
              .evalBinOpNN(state, BO_GT, *strLengthNL, *maxlenValNL, cmpTy)
              .castAs<DefinedOrUnknownSVal>());

      if (stateStringTooLong && !stateStringNotTooLong) {
        // If the string is longer than maxlen, return maxlen.
        result = *maxlenValNL;
      } else if (stateStringNotTooLong && !stateStringTooLong) {
        // If the string is shorter than maxlen, return its length.
        result = *strLengthNL;
      }
    }

    if (result.isUnknown()) {
      // If we don't have enough information for a comparison, there's
      // no guarantee the full string length will actually be returned.
      // All we know is the return value is the min of the string length
      // and the limit. This is better than nothing.
      result = C.getSValBuilder().conjureSymbolVal(
          nullptr, Call.getOriginExpr(), LCtx, C.blockCount());
      NonLoc resultNL = result.castAs<NonLoc>();

      if (strLengthNL) {
        state = state->assume(C.getSValBuilder().evalBinOpNN(
                                  state, BO_LE, resultNL, *strLengthNL, cmpTy)
                                  .castAs<DefinedOrUnknownSVal>(), true);
      }

      if (maxlenValNL) {
        state = state->assume(C.getSValBuilder().evalBinOpNN(
                                  state, BO_LE, resultNL, *maxlenValNL, cmpTy)
                                  .castAs<DefinedOrUnknownSVal>(), true);
      }
    }

  } else {
    // This is a plain strlen(), not strnlen().
    result = strLength.castAs<DefinedOrUnknownSVal>();

    // If we don't know the length of the string, conjure a return
    // value, so it can be used in constraints, at least.
    if (result.isUnknown()) {
      result = C.getSValBuilder().conjureSymbolVal(
          nullptr, Call.getOriginExpr(), LCtx, C.blockCount());
    }
  }

  // Bind the return value.
  assert(!result.isUnknown() && "Should have conjured a value by now");
  state = state->BindExpr(Call.getOriginExpr(), LCtx, result);
  C.addTransition(state);
}

void CStringChecker::evalStrcpy(CheckerContext &C,
                                const CallEvent &Call) const {
  // char *strcpy(char *restrict dst, const char *restrict src);
  evalStrcpyCommon(C, Call,
                   /* ReturnEnd = */ false,
                   /* IsBounded = */ false,
                   /* appendK = */ ConcatFnKind::none);
}

void CStringChecker::evalStrncpy(CheckerContext &C,
                                 const CallEvent &Call) const {
  // char *strncpy(char *restrict dst, const char *restrict src, size_t n);
  evalStrcpyCommon(C, Call,
                   /* ReturnEnd = */ false,
                   /* IsBounded = */ true,
                   /* appendK = */ ConcatFnKind::none);
}

void CStringChecker::evalStpcpy(CheckerContext &C,
                                const CallEvent &Call) const {
  // char *stpcpy(char *restrict dst, const char *restrict src);
  evalStrcpyCommon(C, Call,
                   /* ReturnEnd = */ true,
                   /* IsBounded = */ false,
                   /* appendK = */ ConcatFnKind::none);
}

void CStringChecker::evalStrlcpy(CheckerContext &C,
                                 const CallEvent &Call) const {
  // size_t strlcpy(char *dest, const char *src, size_t size);
  evalStrcpyCommon(C, Call,
                   /* ReturnEnd = */ true,
                   /* IsBounded = */ true,
                   /* appendK = */ ConcatFnKind::none,
                   /* returnPtr = */ false);
}

void CStringChecker::evalStrcat(CheckerContext &C,
                                const CallEvent &Call) const {
  // char *strcat(char *restrict s1, const char *restrict s2);
  evalStrcpyCommon(C, Call,
                   /* ReturnEnd = */ false,
                   /* IsBounded = */ false,
                   /* appendK = */ ConcatFnKind::strcat);
}

void CStringChecker::evalStrncat(CheckerContext &C,
                                 const CallEvent &Call) const {
  // char *strncat(char *restrict s1, const char *restrict s2, size_t n);
  evalStrcpyCommon(C, Call,
                   /* ReturnEnd = */ false,
                   /* IsBounded = */ true,
                   /* appendK = */ ConcatFnKind::strcat);
}

void CStringChecker::evalStrlcat(CheckerContext &C,
                                 const CallEvent &Call) const {
  // size_t strlcat(char *dst, const char *src, size_t size);
  // It will append at most size - strlen(dst) - 1 bytes,
  // NULL-terminating the result.
  evalStrcpyCommon(C, Call,
                   /* ReturnEnd = */ false,
                   /* IsBounded = */ true,
                   /* appendK = */ ConcatFnKind::strlcat,
                   /* returnPtr = */ false);
}

void CStringChecker::evalStrcpyCommon(CheckerContext &C, const CallEvent &Call,
                                      bool ReturnEnd, bool IsBounded,
                                      ConcatFnKind appendK,
                                      bool returnPtr) const {
  if (appendK == ConcatFnKind::none)
    CurrentFunctionDescription = "string copy function";
  else
    CurrentFunctionDescription = "string concatenation function";

  ProgramStateRef state = C.getState();
  const LocationContext *LCtx = C.getLocationContext();

  // Check that the destination is non-null.
  DestinationArgExpr Dst = {{Call.getArgExpr(0), 0}};
  SVal DstVal = state->getSVal(Dst.Expression, LCtx);
  state = checkNonNull(C, state, Dst, DstVal);
  if (!state)
    return;

  // Check that the source is non-null.
  SourceArgExpr srcExpr = {{Call.getArgExpr(1), 1}};
  SVal srcVal = state->getSVal(srcExpr.Expression, LCtx);
  state = checkNonNull(C, state, srcExpr, srcVal);
  if (!state)
    return;

  // Get the string length of the source.
  SVal strLength = getCStringLength(C, state, srcExpr.Expression, srcVal);
  std::optional<NonLoc> strLengthNL = strLength.getAs<NonLoc>();

  // Get the string length of the destination buffer.
  SVal dstStrLength = getCStringLength(C, state, Dst.Expression, DstVal);
  std::optional<NonLoc> dstStrLengthNL = dstStrLength.getAs<NonLoc>();

  // If the source isn't a valid C string, give up.
  if (strLength.isUndef())
    return;

  SValBuilder &svalBuilder = C.getSValBuilder();
  QualType cmpTy = svalBuilder.getConditionType();
  QualType sizeTy = svalBuilder.getContext().getSizeType();

  // These two values allow checking two kinds of errors:
  // - actual overflows caused by a source that doesn't fit in the destination
  // - potential overflows caused by a bound that could exceed the destination
  SVal amountCopied = UnknownVal();
  SVal maxLastElementIndex = UnknownVal();
  const char *boundWarning = nullptr;

  // FIXME: Why do we choose the srcExpr if the access has no size?
  //  Note that the 3rd argument of the call would be the size parameter.
  SizeArgExpr SrcExprAsSizeDummy = {
      {srcExpr.Expression, srcExpr.ArgumentIndex}};
  state = CheckOverlap(
      C, state,
      (IsBounded ? SizeArgExpr{{Call.getArgExpr(2), 2}} : SrcExprAsSizeDummy),
      Dst, srcExpr);

  if (!state)
    return;

  // If the function is strncpy, strncat, etc... it is bounded.
  if (IsBounded) {
    // Get the max number of characters to copy.
    SizeArgExpr lenExpr = {{Call.getArgExpr(2), 2}};
    SVal lenVal = state->getSVal(lenExpr.Expression, LCtx);

    // Protect against misdeclared strncpy().
    lenVal =
        svalBuilder.evalCast(lenVal, sizeTy, lenExpr.Expression->getType());

    std::optional<NonLoc> lenValNL = lenVal.getAs<NonLoc>();

    // If we know both values, we might be able to figure out how much
    // we're copying.
    if (strLengthNL && lenValNL) {
      switch (appendK) {
      case ConcatFnKind::none:
      case ConcatFnKind::strcat: {
        ProgramStateRef stateSourceTooLong, stateSourceNotTooLong;
        // Check if the max number to copy is less than the length of the src.
        // If the bound is equal to the source length, strncpy won't null-
        // terminate the result!
        std::tie(stateSourceTooLong, stateSourceNotTooLong) = state->assume(
            svalBuilder
                .evalBinOpNN(state, BO_GE, *strLengthNL, *lenValNL, cmpTy)
                .castAs<DefinedOrUnknownSVal>());

        if (stateSourceTooLong && !stateSourceNotTooLong) {
          // Max number to copy is less than the length of the src, so the
          // actual strLength copied is the max number arg.
          state = stateSourceTooLong;
          amountCopied = lenVal;

        } else if (!stateSourceTooLong && stateSourceNotTooLong) {
          // The source buffer entirely fits in the bound.
          state = stateSourceNotTooLong;
          amountCopied = strLength;
        }
        break;
      }
      case ConcatFnKind::strlcat:
        if (!dstStrLengthNL)
          return;

        // amountCopied = min (size - dstLen - 1 , srcLen)
        SVal freeSpace = svalBuilder.evalBinOpNN(state, BO_Sub, *lenValNL,
                                                 *dstStrLengthNL, sizeTy);
        if (!isa<NonLoc>(freeSpace))
          return;
        freeSpace =
            svalBuilder.evalBinOp(state, BO_Sub, freeSpace,
                                  svalBuilder.makeIntVal(1, sizeTy), sizeTy);
        std::optional<NonLoc> freeSpaceNL = freeSpace.getAs<NonLoc>();

        // While unlikely, it is possible that the subtraction is
        // too complex to compute, let's check whether it succeeded.
        if (!freeSpaceNL)
          return;
        SVal hasEnoughSpace = svalBuilder.evalBinOpNN(
            state, BO_LE, *strLengthNL, *freeSpaceNL, cmpTy);

        ProgramStateRef TrueState, FalseState;
        std::tie(TrueState, FalseState) =
            state->assume(hasEnoughSpace.castAs<DefinedOrUnknownSVal>());

        // srcStrLength <= size - dstStrLength -1
        if (TrueState && !FalseState) {
          amountCopied = strLength;
        }

        // srcStrLength > size - dstStrLength -1
        if (!TrueState && FalseState) {
          amountCopied = freeSpace;
        }

        if (TrueState && FalseState)
          amountCopied = UnknownVal();
        break;
      }
    }
    // We still want to know if the bound is known to be too large.
    if (lenValNL) {
      switch (appendK) {
      case ConcatFnKind::strcat:
        // For strncat, the check is strlen(dst) + lenVal < sizeof(dst)

        // Get the string length of the destination. If the destination is
        // memory that can't have a string length, we shouldn't be copying
        // into it anyway.
        if (dstStrLength.isUndef())
          return;

        if (dstStrLengthNL) {
          maxLastElementIndex = svalBuilder.evalBinOpNN(
              state, BO_Add, *lenValNL, *dstStrLengthNL, sizeTy);

          boundWarning = "Size argument is greater than the free space in the "
                         "destination buffer";
        }
        break;
      case ConcatFnKind::none:
      case ConcatFnKind::strlcat:
        // For strncpy and strlcat, this is just checking
        //  that lenVal <= sizeof(dst).
        // (Yes, strncpy and strncat differ in how they treat termination.
        // strncat ALWAYS terminates, but strncpy doesn't.)

        // We need a special case for when the copy size is zero, in which
        // case strncpy will do no work at all. Our bounds check uses n-1
        // as the last element accessed, so n == 0 is problematic.
        ProgramStateRef StateZeroSize, StateNonZeroSize;
        std::tie(StateZeroSize, StateNonZeroSize) =
            assumeZero(C, state, *lenValNL, sizeTy);

        // If the size is known to be zero, we're done.
        if (StateZeroSize && !StateNonZeroSize) {
          if (returnPtr) {
            StateZeroSize =
                StateZeroSize->BindExpr(Call.getOriginExpr(), LCtx, DstVal);
          } else {
            if (appendK == ConcatFnKind::none) {
              // strlcpy returns strlen(src)
              StateZeroSize = StateZeroSize->BindExpr(Call.getOriginExpr(),
                                                      LCtx, strLength);
            } else {
              // strlcat returns strlen(src) + strlen(dst)
              SVal retSize = svalBuilder.evalBinOp(
                  state, BO_Add, strLength, dstStrLength, sizeTy);
              StateZeroSize =
                  StateZeroSize->BindExpr(Call.getOriginExpr(), LCtx, retSize);
            }
          }
          C.addTransition(StateZeroSize);
          return;
        }

        // Otherwise, go ahead and figure out the last element we'll touch.
        // We don't record the non-zero assumption here because we can't
        // be sure. We won't warn on a possible zero.
        NonLoc one = svalBuilder.makeIntVal(1, sizeTy).castAs<NonLoc>();
        maxLastElementIndex =
            svalBuilder.evalBinOpNN(state, BO_Sub, *lenValNL, one, sizeTy);
        boundWarning = "Size argument is greater than the length of the "
                       "destination buffer";
        break;
      }
    }
  } else {
    // The function isn't bounded. The amount copied should match the length
    // of the source buffer.
    amountCopied = strLength;
  }

  assert(state);

  // This represents the number of characters copied into the destination
  // buffer. (It may not actually be the strlen if the destination buffer
  // is not terminated.)
  SVal finalStrLength = UnknownVal();
  SVal strlRetVal = UnknownVal();

  if (appendK == ConcatFnKind::none && !returnPtr) {
    // strlcpy returns the sizeof(src)
    strlRetVal = strLength;
  }

  // If this is an appending function (strcat, strncat...) then set the
  // string length to strlen(src) + strlen(dst) since the buffer will
  // ultimately contain both.
  if (appendK != ConcatFnKind::none) {
    // Get the string length of the destination. If the destination is memory
    // that can't have a string length, we shouldn't be copying into it anyway.
    if (dstStrLength.isUndef())
      return;

    if (appendK == ConcatFnKind::strlcat && dstStrLengthNL && strLengthNL) {
      strlRetVal = svalBuilder.evalBinOpNN(state, BO_Add, *strLengthNL,
                                           *dstStrLengthNL, sizeTy);
    }

    std::optional<NonLoc> amountCopiedNL = amountCopied.getAs<NonLoc>();

    // If we know both string lengths, we might know the final string length.
    if (amountCopiedNL && dstStrLengthNL) {
      // Make sure the two lengths together don't overflow a size_t.
      state = checkAdditionOverflow(C, state, *amountCopiedNL, *dstStrLengthNL);
      if (!state)
        return;

      finalStrLength = svalBuilder.evalBinOpNN(state, BO_Add, *amountCopiedNL,
                                               *dstStrLengthNL, sizeTy);
    }

    // If we couldn't get a single value for the final string length,
    // we can at least bound it by the individual lengths.
    if (finalStrLength.isUnknown()) {
      // Try to get a "hypothetical" string length symbol, which we can later
      // set as a real value if that turns out to be the case.
      finalStrLength =
          getCStringLength(C, state, Call.getOriginExpr(), DstVal, true);
      assert(!finalStrLength.isUndef());

      if (std::optional<NonLoc> finalStrLengthNL =
              finalStrLength.getAs<NonLoc>()) {
        if (amountCopiedNL && appendK == ConcatFnKind::none) {
          // we overwrite dst string with the src
          // finalStrLength >= srcStrLength
          SVal sourceInResult = svalBuilder.evalBinOpNN(
              state, BO_GE, *finalStrLengthNL, *amountCopiedNL, cmpTy);
          state = state->assume(sourceInResult.castAs<DefinedOrUnknownSVal>(),
                                true);
          if (!state)
            return;
        }

        if (dstStrLengthNL && appendK != ConcatFnKind::none) {
          // we extend the dst string with the src
          // finalStrLength >= dstStrLength
          SVal destInResult = svalBuilder.evalBinOpNN(state, BO_GE,
                                                      *finalStrLengthNL,
                                                      *dstStrLengthNL,
                                                      cmpTy);
          state =
              state->assume(destInResult.castAs<DefinedOrUnknownSVal>(), true);
          if (!state)
            return;
        }
      }
    }

  } else {
    // Otherwise, this is a copy-over function (strcpy, strncpy, ...), and
    // the final string length will match the input string length.
    finalStrLength = amountCopied;
  }

  SVal Result;

  if (returnPtr) {
    // The final result of the function will either be a pointer past the last
    // copied element, or a pointer to the start of the destination buffer.
    Result = (ReturnEnd ? UnknownVal() : DstVal);
  } else {
    if (appendK == ConcatFnKind::strlcat || appendK == ConcatFnKind::none)
      //strlcpy, strlcat
      Result = strlRetVal;
    else
      Result = finalStrLength;
  }

  assert(state);

  // If the destination is a MemRegion, try to check for a buffer overflow and
  // record the new string length.
  if (std::optional<loc::MemRegionVal> dstRegVal =
          DstVal.getAs<loc::MemRegionVal>()) {
    QualType ptrTy = Dst.Expression->getType();

    // If we have an exact value on a bounded copy, use that to check for
    // overflows, rather than our estimate about how much is actually copied.
    if (std::optional<NonLoc> maxLastNL = maxLastElementIndex.getAs<NonLoc>()) {
      SVal maxLastElement =
          svalBuilder.evalBinOpLN(state, BO_Add, *dstRegVal, *maxLastNL, ptrTy);

      // Check if the first byte of the destination is writable.
      state = CheckLocation(C, state, Dst, DstVal, AccessKind::write);
      if (!state)
        return;
      // Check if the last byte of the destination is writable.
      state = CheckLocation(C, state, Dst, maxLastElement, AccessKind::write);
      if (!state)
        return;
    }

    // Then, if the final length is known...
    if (std::optional<NonLoc> knownStrLength = finalStrLength.getAs<NonLoc>()) {
      SVal lastElement = svalBuilder.evalBinOpLN(state, BO_Add, *dstRegVal,
          *knownStrLength, ptrTy);

      // ...and we haven't checked the bound, we'll check the actual copy.
      if (!boundWarning) {
        // Check if the first byte of the destination is writable.
        state = CheckLocation(C, state, Dst, DstVal, AccessKind::write);
        if (!state)
          return;
        // Check if the last byte of the destination is writable.
        state = CheckLocation(C, state, Dst, lastElement, AccessKind::write);
        if (!state)
          return;
      }

      // If this is a stpcpy-style copy, the last element is the return value.
      if (returnPtr && ReturnEnd)
        Result = lastElement;
    }

    // Invalidate the destination (regular invalidation without pointer-escaping
    // the address of the top-level region). This must happen before we set the
    // C string length because invalidation will clear the length.
    // FIXME: Even if we can't perfectly model the copy, we should see if we
    // can use LazyCompoundVals to copy the source values into the destination.
    // This would probably remove any existing bindings past the end of the
    // string, but that's still an improvement over blank invalidation.
    state = invalidateDestinationBufferBySize(C, state, Dst.Expression,
                                              *dstRegVal, amountCopied,
                                              C.getASTContext().getSizeType());

    // Invalidate the source (const-invalidation without const-pointer-escaping
    // the address of the top-level region).
    state = invalidateSourceBuffer(C, state, srcExpr.Expression, srcVal);

    // Set the C string length of the destination, if we know it.
    if (IsBounded && (appendK == ConcatFnKind::none)) {
      // strncpy is annoying in that it doesn't guarantee to null-terminate
      // the result string. If the original string didn't fit entirely inside
      // the bound (including the null-terminator), we don't know how long the
      // result is.
      if (amountCopied != strLength)
        finalStrLength = UnknownVal();
    }
    state = setCStringLength(state, dstRegVal->getRegion(), finalStrLength);
  }

  assert(state);

  if (returnPtr) {
    // If this is a stpcpy-style copy, but we were unable to check for a buffer
    // overflow, we still need a result. Conjure a return value.
    if (ReturnEnd && Result.isUnknown()) {
      Result = svalBuilder.conjureSymbolVal(nullptr, Call.getOriginExpr(), LCtx,
                                            C.blockCount());
    }
  }
  // Set the return value.
  state = state->BindExpr(Call.getOriginExpr(), LCtx, Result);
  C.addTransition(state);
}

void CStringChecker::evalStrcmp(CheckerContext &C,
                                const CallEvent &Call) const {
  //int strcmp(const char *s1, const char *s2);
  evalStrcmpCommon(C, Call, /* IsBounded = */ false, /* IgnoreCase = */ false);
}

void CStringChecker::evalStrncmp(CheckerContext &C,
                                 const CallEvent &Call) const {
  //int strncmp(const char *s1, const char *s2, size_t n);
  evalStrcmpCommon(C, Call, /* IsBounded = */ true, /* IgnoreCase = */ false);
}

void CStringChecker::evalStrcasecmp(CheckerContext &C,
                                    const CallEvent &Call) const {
  //int strcasecmp(const char *s1, const char *s2);
  evalStrcmpCommon(C, Call, /* IsBounded = */ false, /* IgnoreCase = */ true);
}

void CStringChecker::evalStrncasecmp(CheckerContext &C,
                                     const CallEvent &Call) const {
  //int strncasecmp(const char *s1, const char *s2, size_t n);
  evalStrcmpCommon(C, Call, /* IsBounded = */ true, /* IgnoreCase = */ true);
}

void CStringChecker::evalStrcmpCommon(CheckerContext &C, const CallEvent &Call,
                                      bool IsBounded, bool IgnoreCase) const {
  CurrentFunctionDescription = "string comparison function";
  ProgramStateRef state = C.getState();
  const LocationContext *LCtx = C.getLocationContext();

  // Check that the first string is non-null
  AnyArgExpr Left = {Call.getArgExpr(0), 0};
  SVal LeftVal = state->getSVal(Left.Expression, LCtx);
  state = checkNonNull(C, state, Left, LeftVal);
  if (!state)
    return;

  // Check that the second string is non-null.
  AnyArgExpr Right = {Call.getArgExpr(1), 1};
  SVal RightVal = state->getSVal(Right.Expression, LCtx);
  state = checkNonNull(C, state, Right, RightVal);
  if (!state)
    return;

  // Get the string length of the first string or give up.
  SVal LeftLength = getCStringLength(C, state, Left.Expression, LeftVal);
  if (LeftLength.isUndef())
    return;

  // Get the string length of the second string or give up.
  SVal RightLength = getCStringLength(C, state, Right.Expression, RightVal);
  if (RightLength.isUndef())
    return;

  // If we know the two buffers are the same, we know the result is 0.
  // First, get the two buffers' addresses. Another checker will have already
  // made sure they're not undefined.
  DefinedOrUnknownSVal LV = LeftVal.castAs<DefinedOrUnknownSVal>();
  DefinedOrUnknownSVal RV = RightVal.castAs<DefinedOrUnknownSVal>();

  // See if they are the same.
  SValBuilder &svalBuilder = C.getSValBuilder();
  DefinedOrUnknownSVal SameBuf = svalBuilder.evalEQ(state, LV, RV);
  ProgramStateRef StSameBuf, StNotSameBuf;
  std::tie(StSameBuf, StNotSameBuf) = state->assume(SameBuf);

  // If the two arguments might be the same buffer, we know the result is 0,
  // and we only need to check one size.
  if (StSameBuf) {
    StSameBuf =
        StSameBuf->BindExpr(Call.getOriginExpr(), LCtx,
                            svalBuilder.makeZeroVal(Call.getResultType()));
    C.addTransition(StSameBuf);

    // If the two arguments are GUARANTEED to be the same, we're done!
    if (!StNotSameBuf)
      return;
  }

  assert(StNotSameBuf);
  state = StNotSameBuf;

  // At this point we can go about comparing the two buffers.
  // For now, we only do this if they're both known string literals.

  // Attempt to extract string literals from both expressions.
  const StringLiteral *LeftStrLiteral =
      getCStringLiteral(C, state, Left.Expression, LeftVal);
  const StringLiteral *RightStrLiteral =
      getCStringLiteral(C, state, Right.Expression, RightVal);
  bool canComputeResult = false;
  SVal resultVal = svalBuilder.conjureSymbolVal(nullptr, Call.getOriginExpr(),
                                                LCtx, C.blockCount());

  if (LeftStrLiteral && RightStrLiteral) {
    StringRef LeftStrRef = LeftStrLiteral->getString();
    StringRef RightStrRef = RightStrLiteral->getString();

    if (IsBounded) {
      // Get the max number of characters to compare.
      const Expr *lenExpr = Call.getArgExpr(2);
      SVal lenVal = state->getSVal(lenExpr, LCtx);

      // If the length is known, we can get the right substrings.
      if (const llvm::APSInt *len = svalBuilder.getKnownValue(state, lenVal)) {
        // Create substrings of each to compare the prefix.
        LeftStrRef = LeftStrRef.substr(0, (size_t)len->getZExtValue());
        RightStrRef = RightStrRef.substr(0, (size_t)len->getZExtValue());
        canComputeResult = true;
      }
    } else {
      // This is a normal, unbounded strcmp.
      canComputeResult = true;
    }

    if (canComputeResult) {
      // Real strcmp stops at null characters.
      size_t s1Term = LeftStrRef.find('\0');
      if (s1Term != StringRef::npos)
        LeftStrRef = LeftStrRef.substr(0, s1Term);

      size_t s2Term = RightStrRef.find('\0');
      if (s2Term != StringRef::npos)
        RightStrRef = RightStrRef.substr(0, s2Term);

      // Use StringRef's comparison methods to compute the actual result.
      int compareRes = IgnoreCase ? LeftStrRef.compare_insensitive(RightStrRef)
                                  : LeftStrRef.compare(RightStrRef);

      // The strcmp function returns an integer greater than, equal to, or less
      // than zero, [c11, p7.24.4.2].
      if (compareRes == 0) {
        resultVal = svalBuilder.makeIntVal(compareRes, Call.getResultType());
      }
      else {
        DefinedSVal zeroVal = svalBuilder.makeIntVal(0, Call.getResultType());
        // Constrain strcmp's result range based on the result of StringRef's
        // comparison methods.
        BinaryOperatorKind op = (compareRes > 0) ? BO_GT : BO_LT;
        SVal compareWithZero =
          svalBuilder.evalBinOp(state, op, resultVal, zeroVal,
              svalBuilder.getConditionType());
        DefinedSVal compareWithZeroVal = compareWithZero.castAs<DefinedSVal>();
        state = state->assume(compareWithZeroVal, true);
      }
    }
  }

  state = state->BindExpr(Call.getOriginExpr(), LCtx, resultVal);

  // Record this as a possible path.
  C.addTransition(state);
}

void CStringChecker::evalStrsep(CheckerContext &C,
                                const CallEvent &Call) const {
  // char *strsep(char **stringp, const char *delim);
  // Verify whether the search string parameter matches the return type.
  SourceArgExpr SearchStrPtr = {{Call.getArgExpr(0), 0}};

  QualType CharPtrTy = SearchStrPtr.Expression->getType()->getPointeeType();
  if (CharPtrTy.isNull() || Call.getResultType().getUnqualifiedType() !=
                                CharPtrTy.getUnqualifiedType())
    return;

  CurrentFunctionDescription = "strsep()";
  ProgramStateRef State = C.getState();
  const LocationContext *LCtx = C.getLocationContext();

  // Check that the search string pointer is non-null (though it may point to
  // a null string).
  SVal SearchStrVal = State->getSVal(SearchStrPtr.Expression, LCtx);
  State = checkNonNull(C, State, SearchStrPtr, SearchStrVal);
  if (!State)
    return;

  // Check that the delimiter string is non-null.
  AnyArgExpr DelimStr = {Call.getArgExpr(1), 1};
  SVal DelimStrVal = State->getSVal(DelimStr.Expression, LCtx);
  State = checkNonNull(C, State, DelimStr, DelimStrVal);
  if (!State)
    return;

  SValBuilder &SVB = C.getSValBuilder();
  SVal Result;
  if (std::optional<Loc> SearchStrLoc = SearchStrVal.getAs<Loc>()) {
    // Get the current value of the search string pointer, as a char*.
    Result = State->getSVal(*SearchStrLoc, CharPtrTy);

    // Invalidate the search string, representing the change of one delimiter
    // character to NUL.
    // As the replacement never overflows, do not invalidate its super region.
    State = invalidateDestinationBufferNeverOverflows(
        C, State, SearchStrPtr.Expression, Result);

    // Overwrite the search string pointer. The new value is either an address
    // further along in the same string, or NULL if there are no more tokens.
    State =
        State->bindLoc(*SearchStrLoc,
                       SVB.conjureSymbolVal(getTag(), Call.getOriginExpr(),
                                            LCtx, CharPtrTy, C.blockCount()),
                       LCtx);
  } else {
    assert(SearchStrVal.isUnknown());
    // Conjure a symbolic value. It's the best we can do.
    Result = SVB.conjureSymbolVal(nullptr, Call.getOriginExpr(), LCtx,
                                  C.blockCount());
  }

  // Set the return value, and finish.
  State = State->BindExpr(Call.getOriginExpr(), LCtx, Result);
  C.addTransition(State);
}

// These should probably be moved into a C++ standard library checker.
void CStringChecker::evalStdCopy(CheckerContext &C,
                                 const CallEvent &Call) const {
  evalStdCopyCommon(C, Call);
}

void CStringChecker::evalStdCopyBackward(CheckerContext &C,
                                         const CallEvent &Call) const {
  evalStdCopyCommon(C, Call);
}

void CStringChecker::evalStdCopyCommon(CheckerContext &C,
                                       const CallEvent &Call) const {
  if (!Call.getArgExpr(2)->getType()->isPointerType())
    return;

  ProgramStateRef State = C.getState();

  const LocationContext *LCtx = C.getLocationContext();

  // template <class _InputIterator, class _OutputIterator>
  // _OutputIterator
  // copy(_InputIterator __first, _InputIterator __last,
  //        _OutputIterator __result)

  // Invalidate the destination buffer
  const Expr *Dst = Call.getArgExpr(2);
  SVal DstVal = State->getSVal(Dst, LCtx);
  // FIXME: As we do not know how many items are copied, we also invalidate the
  // super region containing the target location.
  State =
      invalidateDestinationBufferAlwaysEscapeSuperRegion(C, State, Dst, DstVal);

  SValBuilder &SVB = C.getSValBuilder();

  SVal ResultVal =
      SVB.conjureSymbolVal(nullptr, Call.getOriginExpr(), LCtx, C.blockCount());
  State = State->BindExpr(Call.getOriginExpr(), LCtx, ResultVal);

  C.addTransition(State);
}

void CStringChecker::evalMemset(CheckerContext &C,
                                const CallEvent &Call) const {
  // void *memset(void *s, int c, size_t n);
  CurrentFunctionDescription = "memory set function";

  DestinationArgExpr Buffer = {{Call.getArgExpr(0), 0}};
  AnyArgExpr CharE = {Call.getArgExpr(1), 1};
  SizeArgExpr Size = {{Call.getArgExpr(2), 2}};

  ProgramStateRef State = C.getState();

  // See if the size argument is zero.
  const LocationContext *LCtx = C.getLocationContext();
  SVal SizeVal = C.getSVal(Size.Expression);
  QualType SizeTy = Size.Expression->getType();

  ProgramStateRef ZeroSize, NonZeroSize;
  std::tie(ZeroSize, NonZeroSize) = assumeZero(C, State, SizeVal, SizeTy);

  // Get the value of the memory area.
  SVal BufferPtrVal = C.getSVal(Buffer.Expression);

  // If the size is zero, there won't be any actual memory access, so
  // just bind the return value to the buffer and return.
  if (ZeroSize && !NonZeroSize) {
    ZeroSize = ZeroSize->BindExpr(Call.getOriginExpr(), LCtx, BufferPtrVal);
    C.addTransition(ZeroSize);
    return;
  }

  // Ensure the memory area is not null.
  // If it is NULL there will be a NULL pointer dereference.
  State = checkNonNull(C, NonZeroSize, Buffer, BufferPtrVal);
  if (!State)
    return;

  State = CheckBufferAccess(C, State, Buffer, Size, AccessKind::write);
  if (!State)
    return;

  // According to the values of the arguments, bind the value of the second
  // argument to the destination buffer and set string length, or just
  // invalidate the destination buffer.
  if (!memsetAux(Buffer.Expression, C.getSVal(CharE.Expression),
                 Size.Expression, C, State))
    return;

  State = State->BindExpr(Call.getOriginExpr(), LCtx, BufferPtrVal);
  C.addTransition(State);
}

void CStringChecker::evalBzero(CheckerContext &C, const CallEvent &Call) const {
  CurrentFunctionDescription = "memory clearance function";

  DestinationArgExpr Buffer = {{Call.getArgExpr(0), 0}};
  SizeArgExpr Size = {{Call.getArgExpr(1), 1}};
  SVal Zero = C.getSValBuilder().makeZeroVal(C.getASTContext().IntTy);

  ProgramStateRef State = C.getState();

  // See if the size argument is zero.
  SVal SizeVal = C.getSVal(Size.Expression);
  QualType SizeTy = Size.Expression->getType();

  ProgramStateRef StateZeroSize, StateNonZeroSize;
  std::tie(StateZeroSize, StateNonZeroSize) =
    assumeZero(C, State, SizeVal, SizeTy);

  // If the size is zero, there won't be any actual memory access,
  // In this case we just return.
  if (StateZeroSize && !StateNonZeroSize) {
    C.addTransition(StateZeroSize);
    return;
  }

  // Get the value of the memory area.
  SVal MemVal = C.getSVal(Buffer.Expression);

  // Ensure the memory area is not null.
  // If it is NULL there will be a NULL pointer dereference.
  State = checkNonNull(C, StateNonZeroSize, Buffer, MemVal);
  if (!State)
    return;

  State = CheckBufferAccess(C, State, Buffer, Size, AccessKind::write);
  if (!State)
    return;

  if (!memsetAux(Buffer.Expression, Zero, Size.Expression, C, State))
    return;

  C.addTransition(State);
}

void CStringChecker::evalSprintf(CheckerContext &C,
                                 const CallEvent &Call) const {
  CurrentFunctionDescription = "'sprintf'";
  evalSprintfCommon(C, Call, /* IsBounded = */ false);
}

void CStringChecker::evalSnprintf(CheckerContext &C,
                                  const CallEvent &Call) const {
  CurrentFunctionDescription = "'snprintf'";
  evalSprintfCommon(C, Call, /* IsBounded = */ true);
}

void CStringChecker::evalSprintfCommon(CheckerContext &C, const CallEvent &Call,
                                       bool IsBounded) const {
  ProgramStateRef State = C.getState();
  const auto *CE = cast<CallExpr>(Call.getOriginExpr());
  DestinationArgExpr Dest = {{Call.getArgExpr(0), 0}};

  const auto NumParams = Call.parameters().size();
  if (CE->getNumArgs() < NumParams) {
    // This is an invalid call, let's just ignore it.
    return;
  }

  const auto AllArguments =
      llvm::make_range(CE->getArgs(), CE->getArgs() + CE->getNumArgs());
  const auto VariadicArguments = drop_begin(enumerate(AllArguments), NumParams);

  for (const auto &[ArgIdx, ArgExpr] : VariadicArguments) {
    // We consider only string buffers
    if (const QualType type = ArgExpr->getType();
        !type->isAnyPointerType() ||
        !type->getPointeeType()->isAnyCharacterType())
      continue;
    SourceArgExpr Source = {{ArgExpr, unsigned(ArgIdx)}};

    // Ensure the buffers do not overlap.
    SizeArgExpr SrcExprAsSizeDummy = {
        {Source.Expression, Source.ArgumentIndex}};
    State = CheckOverlap(
        C, State,
        (IsBounded ? SizeArgExpr{{Call.getArgExpr(1), 1}} : SrcExprAsSizeDummy),
        Dest, Source);
    if (!State)
      return;
  }

  C.addTransition(State);
}

//===----------------------------------------------------------------------===//
// The driver method, and other Checker callbacks.
//===----------------------------------------------------------------------===//

CStringChecker::FnCheck CStringChecker::identifyCall(const CallEvent &Call,
                                                     CheckerContext &C) const {
  const auto *CE = dyn_cast_or_null<CallExpr>(Call.getOriginExpr());
  if (!CE)
    return nullptr;

  const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
  if (!FD)
    return nullptr;

  if (StdCopy.matches(Call))
    return &CStringChecker::evalStdCopy;
  if (StdCopyBackward.matches(Call))
    return &CStringChecker::evalStdCopyBackward;

  // Pro-actively check that argument types are safe to do arithmetic upon.
  // We do not want to crash if someone accidentally passes a structure
  // into, say, a C++ overload of any of these functions. We could not check
  // that for std::copy because they may have arguments of other types.
  for (auto I : CE->arguments()) {
    QualType T = I->getType();
    if (!T->isIntegralOrEnumerationType() && !T->isPointerType())
      return nullptr;
  }

  const FnCheck *Callback = Callbacks.lookup(Call);
  if (Callback)
    return *Callback;

  return nullptr;
}

bool CStringChecker::evalCall(const CallEvent &Call, CheckerContext &C) const {
  FnCheck Callback = identifyCall(Call, C);

  // If the callee isn't a string function, let another checker handle it.
  if (!Callback)
    return false;

  // Check and evaluate the call.
  assert(isa<CallExpr>(Call.getOriginExpr()));
  Callback(this, C, Call);

  // If the evaluate call resulted in no change, chain to the next eval call
  // handler.
  // Note, the custom CString evaluation calls assume that basic safety
  // properties are held. However, if the user chooses to turn off some of these
  // checks, we ignore the issues and leave the call evaluation to a generic
  // handler.
  return C.isDifferent();
}

void CStringChecker::checkPreStmt(const DeclStmt *DS, CheckerContext &C) const {
  // Record string length for char a[] = "abc";
  ProgramStateRef state = C.getState();

  for (const auto *I : DS->decls()) {
    const VarDecl *D = dyn_cast<VarDecl>(I);
    if (!D)
      continue;

    // FIXME: Handle array fields of structs.
    if (!D->getType()->isArrayType())
      continue;

    const Expr *Init = D->getInit();
    if (!Init)
      continue;
    if (!isa<StringLiteral>(Init))
      continue;

    Loc VarLoc = state->getLValue(D, C.getLocationContext());
    const MemRegion *MR = VarLoc.getAsRegion();
    if (!MR)
      continue;

    SVal StrVal = C.getSVal(Init);
    assert(StrVal.isValid() && "Initializer string is unknown or undefined");
    DefinedOrUnknownSVal strLength =
      getCStringLength(C, state, Init, StrVal).castAs<DefinedOrUnknownSVal>();

    state = state->set<CStringLength>(MR, strLength);
  }

  C.addTransition(state);
}

ProgramStateRef
CStringChecker::checkRegionChanges(ProgramStateRef state,
    const InvalidatedSymbols *,
    ArrayRef<const MemRegion *> ExplicitRegions,
    ArrayRef<const MemRegion *> Regions,
    const LocationContext *LCtx,
    const CallEvent *Call) const {
  CStringLengthTy Entries = state->get<CStringLength>();
  if (Entries.isEmpty())
    return state;

  llvm::SmallPtrSet<const MemRegion *, 8> Invalidated;
  llvm::SmallPtrSet<const MemRegion *, 32> SuperRegions;

  // First build sets for the changed regions and their super-regions.
  for (const MemRegion *MR : Regions) {
    Invalidated.insert(MR);

    SuperRegions.insert(MR);
    while (const SubRegion *SR = dyn_cast<SubRegion>(MR)) {
      MR = SR->getSuperRegion();
      SuperRegions.insert(MR);
    }
  }

  CStringLengthTy::Factory &F = state->get_context<CStringLength>();

  // Then loop over the entries in the current state.
  for (const MemRegion *MR : llvm::make_first_range(Entries)) {
    // Is this entry for a super-region of a changed region?
    if (SuperRegions.count(MR)) {
      Entries = F.remove(Entries, MR);
      continue;
    }

    // Is this entry for a sub-region of a changed region?
    const MemRegion *Super = MR;
    while (const SubRegion *SR = dyn_cast<SubRegion>(Super)) {
      Super = SR->getSuperRegion();
      if (Invalidated.count(Super)) {
        Entries = F.remove(Entries, MR);
        break;
      }
    }
  }

  return state->set<CStringLength>(Entries);
}

void CStringChecker::checkLiveSymbols(ProgramStateRef state,
    SymbolReaper &SR) const {
  // Mark all symbols in our string length map as valid.
  CStringLengthTy Entries = state->get<CStringLength>();

  for (SVal Len : llvm::make_second_range(Entries)) {
    for (SymbolRef Sym : Len.symbols())
      SR.markInUse(Sym);
  }
}

void CStringChecker::checkDeadSymbols(SymbolReaper &SR,
    CheckerContext &C) const {
  ProgramStateRef state = C.getState();
  CStringLengthTy Entries = state->get<CStringLength>();
  if (Entries.isEmpty())
    return;

  CStringLengthTy::Factory &F = state->get_context<CStringLength>();
  for (auto [Reg, Len] : Entries) {
    if (SymbolRef Sym = Len.getAsSymbol()) {
      if (SR.isDead(Sym))
        Entries = F.remove(Entries, Reg);
    }
  }

  state = state->set<CStringLength>(Entries);
  C.addTransition(state);
}

void ento::registerCStringModeling(CheckerManager &Mgr) {
  Mgr.registerChecker<CStringChecker>();
}

bool ento::shouldRegisterCStringModeling(const CheckerManager &mgr) {
  return true;
}

#define REGISTER_CHECKER(name)                                                 \
  void ento::register##name(CheckerManager &mgr) {                             \
    CStringChecker *checker = mgr.getChecker<CStringChecker>();                \
    checker->Filter.Check##name = true;                                        \
    checker->Filter.CheckName##name = mgr.getCurrentCheckerName();             \
  }                                                                            \
                                                                               \
  bool ento::shouldRegister##name(const CheckerManager &mgr) { return true; }

REGISTER_CHECKER(CStringNullArg)
REGISTER_CHECKER(CStringOutOfBounds)
REGISTER_CHECKER(CStringBufferOverlap)
REGISTER_CHECKER(CStringNotNullTerm)
REGISTER_CHECKER(CStringUninitializedRead)

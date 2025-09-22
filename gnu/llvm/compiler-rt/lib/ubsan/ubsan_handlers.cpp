//===-- ubsan_handlers.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Error logging entry points for the UBSan runtime.
//
//===----------------------------------------------------------------------===//

#include "ubsan_platform.h"
#if CAN_SANITIZE_UB
#include "ubsan_handlers.h"
#include "ubsan_diag.h"
#include "ubsan_flags.h"
#include "ubsan_monitor.h"
#include "ubsan_value.h"

#include "sanitizer_common/sanitizer_common.h"

using namespace __sanitizer;
using namespace __ubsan;

namespace __ubsan {
bool ignoreReport(SourceLocation SLoc, ReportOptions Opts, ErrorType ET) {
  // We are not allowed to skip error report: if we are in unrecoverable
  // handler, we have to terminate the program right now, and therefore
  // have to print some diagnostic.
  //
  // Even if source location is disabled, it doesn't mean that we have
  // already report an error to the user: some concurrently running
  // thread could have acquired it, but not yet printed the report.
  if (Opts.FromUnrecoverableHandler)
    return false;
  return SLoc.isDisabled() || IsPCSuppressed(ET, Opts.pc, SLoc.getFilename());
}

/// Situations in which we might emit a check for the suitability of a
/// pointer or glvalue. Needs to be kept in sync with CodeGenFunction.h in
/// clang.
enum TypeCheckKind {
  /// Checking the operand of a load. Must be suitably sized and aligned.
  TCK_Load,
  /// Checking the destination of a store. Must be suitably sized and aligned.
  TCK_Store,
  /// Checking the bound value in a reference binding. Must be suitably sized
  /// and aligned, but is not required to refer to an object (until the
  /// reference is used), per core issue 453.
  TCK_ReferenceBinding,
  /// Checking the object expression in a non-static data member access. Must
  /// be an object within its lifetime.
  TCK_MemberAccess,
  /// Checking the 'this' pointer for a call to a non-static member function.
  /// Must be an object within its lifetime.
  TCK_MemberCall,
  /// Checking the 'this' pointer for a constructor call.
  TCK_ConstructorCall,
  /// Checking the operand of a static_cast to a derived pointer type. Must be
  /// null or an object within its lifetime.
  TCK_DowncastPointer,
  /// Checking the operand of a static_cast to a derived reference type. Must
  /// be an object within its lifetime.
  TCK_DowncastReference,
  /// Checking the operand of a cast to a base object. Must be suitably sized
  /// and aligned.
  TCK_Upcast,
  /// Checking the operand of a cast to a virtual base object. Must be an
  /// object within its lifetime.
  TCK_UpcastToVirtualBase,
  /// Checking the value assigned to a _Nonnull pointer. Must not be null.
  TCK_NonnullAssign,
  /// Checking the operand of a dynamic_cast or a typeid expression.  Must be
  /// null or an object within its lifetime.
  TCK_DynamicOperation
};

extern const char *const TypeCheckKinds[] = {
    "load of", "store to", "reference binding to", "member access within",
    "member call on", "constructor call on", "downcast of", "downcast of",
    "upcast of", "cast to virtual base of", "_Nonnull binding to",
    "dynamic operation on"};
}

static void handleTypeMismatchImpl(TypeMismatchData *Data, ValueHandle Pointer,
                                   ReportOptions Opts) {
  Location Loc = Data->Loc.acquire();

  uptr Alignment = (uptr)1 << Data->LogAlignment;
  ErrorType ET;
  if (!Pointer)
    ET = (Data->TypeCheckKind == TCK_NonnullAssign)
             ? ErrorType::NullPointerUseWithNullability
             : ErrorType::NullPointerUse;
  else if (Pointer & (Alignment - 1))
    ET = ErrorType::MisalignedPointerUse;
  else
    ET = ErrorType::InsufficientObjectSize;

  // Use the SourceLocation from Data to track deduplication, even if it's
  // invalid.
  if (ignoreReport(Loc.getSourceLocation(), Opts, ET))
    return;

  SymbolizedStackHolder FallbackLoc;
  if (Data->Loc.isInvalid()) {
    FallbackLoc.reset(getCallerLocation(Opts.pc));
    Loc = FallbackLoc;
  }

  ScopedReport R(Opts, Loc, ET);

  switch (ET) {
  case ErrorType::NullPointerUse:
  case ErrorType::NullPointerUseWithNullability:
    Diag(Loc, DL_Error, ET, "%0 null pointer of type %1")
        << TypeCheckKinds[Data->TypeCheckKind] << Data->Type;
    break;
  case ErrorType::MisalignedPointerUse:
    Diag(Loc, DL_Error, ET, "%0 misaligned address %1 for type %3, "
                        "which requires %2 byte alignment")
        << TypeCheckKinds[Data->TypeCheckKind] << (void *)Pointer << Alignment
        << Data->Type;
    break;
  case ErrorType::InsufficientObjectSize:
    Diag(Loc, DL_Error, ET, "%0 address %1 with insufficient space "
                        "for an object of type %2")
        << TypeCheckKinds[Data->TypeCheckKind] << (void *)Pointer << Data->Type;
    break;
  default:
    UNREACHABLE("unexpected error type!");
  }

  if (Pointer)
    Diag(Pointer, DL_Note, ET, "pointer points here");
}

void __ubsan::__ubsan_handle_type_mismatch_v1(TypeMismatchData *Data,
                                              ValueHandle Pointer) {
  GET_REPORT_OPTIONS(false);
  handleTypeMismatchImpl(Data, Pointer, Opts);
}
void __ubsan::__ubsan_handle_type_mismatch_v1_abort(TypeMismatchData *Data,
                                                    ValueHandle Pointer) {
  GET_REPORT_OPTIONS(true);
  handleTypeMismatchImpl(Data, Pointer, Opts);
  Die();
}

static void handleAlignmentAssumptionImpl(AlignmentAssumptionData *Data,
                                          ValueHandle Pointer,
                                          ValueHandle Alignment,
                                          ValueHandle Offset,
                                          ReportOptions Opts) {
  Location Loc = Data->Loc.acquire();
  SourceLocation AssumptionLoc = Data->AssumptionLoc.acquire();

  ErrorType ET = ErrorType::AlignmentAssumption;

  if (ignoreReport(Loc.getSourceLocation(), Opts, ET))
    return;

  ScopedReport R(Opts, Loc, ET);

  uptr RealPointer = Pointer - Offset;
  uptr LSB = LeastSignificantSetBitIndex(RealPointer);
  uptr ActualAlignment = uptr(1) << LSB;

  uptr Mask = Alignment - 1;
  uptr MisAlignmentOffset = RealPointer & Mask;

  if (!Offset) {
    Diag(Loc, DL_Error, ET,
         "assumption of %0 byte alignment for pointer of type %1 failed")
        << Alignment << Data->Type;
  } else {
    Diag(Loc, DL_Error, ET,
         "assumption of %0 byte alignment (with offset of %1 byte) for pointer "
         "of type %2 failed")
        << Alignment << Offset << Data->Type;
  }

  if (!AssumptionLoc.isInvalid())
    Diag(AssumptionLoc, DL_Note, ET, "alignment assumption was specified here");

  Diag(RealPointer, DL_Note, ET,
       "%0address is %1 aligned, misalignment offset is %2 bytes")
      << (Offset ? "offset " : "") << ActualAlignment << MisAlignmentOffset;
}

void __ubsan::__ubsan_handle_alignment_assumption(AlignmentAssumptionData *Data,
                                                  ValueHandle Pointer,
                                                  ValueHandle Alignment,
                                                  ValueHandle Offset) {
  GET_REPORT_OPTIONS(false);
  handleAlignmentAssumptionImpl(Data, Pointer, Alignment, Offset, Opts);
}
void __ubsan::__ubsan_handle_alignment_assumption_abort(
    AlignmentAssumptionData *Data, ValueHandle Pointer, ValueHandle Alignment,
    ValueHandle Offset) {
  GET_REPORT_OPTIONS(true);
  handleAlignmentAssumptionImpl(Data, Pointer, Alignment, Offset, Opts);
  Die();
}

/// \brief Common diagnostic emission for various forms of integer overflow.
template <typename T>
static void handleIntegerOverflowImpl(OverflowData *Data, ValueHandle LHS,
                                      const char *Operator, T RHS,
                                      ReportOptions Opts) {
  SourceLocation Loc = Data->Loc.acquire();
  bool IsSigned = Data->Type.isSignedIntegerTy();
  ErrorType ET = IsSigned ? ErrorType::SignedIntegerOverflow
                          : ErrorType::UnsignedIntegerOverflow;

  if (ignoreReport(Loc, Opts, ET))
    return;

  // If this is an unsigned overflow in non-fatal mode, potentially ignore it.
  if (!IsSigned && !Opts.FromUnrecoverableHandler &&
      flags()->silence_unsigned_overflow)
    return;

  ScopedReport R(Opts, Loc, ET);

  Diag(Loc, DL_Error, ET, "%0 integer overflow: "
                          "%1 %2 %3 cannot be represented in type %4")
      << (IsSigned ? "signed" : "unsigned") << Value(Data->Type, LHS)
      << Operator << RHS << Data->Type;
}

#define UBSAN_OVERFLOW_HANDLER(handler_name, op, unrecoverable)                \
  void __ubsan::handler_name(OverflowData *Data, ValueHandle LHS,              \
                             ValueHandle RHS) {                                \
    GET_REPORT_OPTIONS(unrecoverable);                                         \
    handleIntegerOverflowImpl(Data, LHS, op, Value(Data->Type, RHS), Opts);    \
    if (unrecoverable)                                                         \
      Die();                                                                   \
  }

UBSAN_OVERFLOW_HANDLER(__ubsan_handle_add_overflow, "+", false)
UBSAN_OVERFLOW_HANDLER(__ubsan_handle_add_overflow_abort, "+", true)
UBSAN_OVERFLOW_HANDLER(__ubsan_handle_sub_overflow, "-", false)
UBSAN_OVERFLOW_HANDLER(__ubsan_handle_sub_overflow_abort, "-", true)
UBSAN_OVERFLOW_HANDLER(__ubsan_handle_mul_overflow, "*", false)
UBSAN_OVERFLOW_HANDLER(__ubsan_handle_mul_overflow_abort, "*", true)

static void handleNegateOverflowImpl(OverflowData *Data, ValueHandle OldVal,
                                     ReportOptions Opts) {
  SourceLocation Loc = Data->Loc.acquire();
  bool IsSigned = Data->Type.isSignedIntegerTy();
  ErrorType ET = IsSigned ? ErrorType::SignedIntegerOverflow
                          : ErrorType::UnsignedIntegerOverflow;

  if (ignoreReport(Loc, Opts, ET))
    return;

  if (!IsSigned && flags()->silence_unsigned_overflow)
    return;

  ScopedReport R(Opts, Loc, ET);

  if (IsSigned)
    Diag(Loc, DL_Error, ET,
         "negation of %0 cannot be represented in type %1; "
         "cast to an unsigned type to negate this value to itself")
        << Value(Data->Type, OldVal) << Data->Type;
  else
    Diag(Loc, DL_Error, ET, "negation of %0 cannot be represented in type %1")
        << Value(Data->Type, OldVal) << Data->Type;
}

void __ubsan::__ubsan_handle_negate_overflow(OverflowData *Data,
                                             ValueHandle OldVal) {
  GET_REPORT_OPTIONS(false);
  handleNegateOverflowImpl(Data, OldVal, Opts);
}
void __ubsan::__ubsan_handle_negate_overflow_abort(OverflowData *Data,
                                                    ValueHandle OldVal) {
  GET_REPORT_OPTIONS(true);
  handleNegateOverflowImpl(Data, OldVal, Opts);
  Die();
}

static void handleDivremOverflowImpl(OverflowData *Data, ValueHandle LHS,
                                     ValueHandle RHS, ReportOptions Opts) {
  SourceLocation Loc = Data->Loc.acquire();
  Value LHSVal(Data->Type, LHS);
  Value RHSVal(Data->Type, RHS);

  ErrorType ET;
  if (RHSVal.isMinusOne())
    ET = ErrorType::SignedIntegerOverflow;
  else if (Data->Type.isIntegerTy())
    ET = ErrorType::IntegerDivideByZero;
  else
    ET = ErrorType::FloatDivideByZero;

  if (ignoreReport(Loc, Opts, ET))
    return;

  ScopedReport R(Opts, Loc, ET);

  switch (ET) {
  case ErrorType::SignedIntegerOverflow:
    Diag(Loc, DL_Error, ET,
         "division of %0 by -1 cannot be represented in type %1")
        << LHSVal << Data->Type;
    break;
  default:
    Diag(Loc, DL_Error, ET, "division by zero");
    break;
  }
}

void __ubsan::__ubsan_handle_divrem_overflow(OverflowData *Data,
                                             ValueHandle LHS, ValueHandle RHS) {
  GET_REPORT_OPTIONS(false);
  handleDivremOverflowImpl(Data, LHS, RHS, Opts);
}
void __ubsan::__ubsan_handle_divrem_overflow_abort(OverflowData *Data,
                                                    ValueHandle LHS,
                                                    ValueHandle RHS) {
  GET_REPORT_OPTIONS(true);
  handleDivremOverflowImpl(Data, LHS, RHS, Opts);
  Die();
}

static void handleShiftOutOfBoundsImpl(ShiftOutOfBoundsData *Data,
                                       ValueHandle LHS, ValueHandle RHS,
                                       ReportOptions Opts) {
  SourceLocation Loc = Data->Loc.acquire();
  Value LHSVal(Data->LHSType, LHS);
  Value RHSVal(Data->RHSType, RHS);

  ErrorType ET;
  if (RHSVal.isNegative() ||
      RHSVal.getPositiveIntValue() >= Data->LHSType.getIntegerBitWidth())
    ET = ErrorType::InvalidShiftExponent;
  else
    ET = ErrorType::InvalidShiftBase;

  if (ignoreReport(Loc, Opts, ET))
    return;

  ScopedReport R(Opts, Loc, ET);

  if (ET == ErrorType::InvalidShiftExponent) {
    if (RHSVal.isNegative())
      Diag(Loc, DL_Error, ET, "shift exponent %0 is negative") << RHSVal;
    else
      Diag(Loc, DL_Error, ET,
           "shift exponent %0 is too large for %1-bit type %2")
          << RHSVal << Data->LHSType.getIntegerBitWidth() << Data->LHSType;
  } else {
    if (LHSVal.isNegative())
      Diag(Loc, DL_Error, ET, "left shift of negative value %0") << LHSVal;
    else
      Diag(Loc, DL_Error, ET,
           "left shift of %0 by %1 places cannot be represented in type %2")
          << LHSVal << RHSVal << Data->LHSType;
  }
}

void __ubsan::__ubsan_handle_shift_out_of_bounds(ShiftOutOfBoundsData *Data,
                                                 ValueHandle LHS,
                                                 ValueHandle RHS) {
  GET_REPORT_OPTIONS(false);
  handleShiftOutOfBoundsImpl(Data, LHS, RHS, Opts);
}
void __ubsan::__ubsan_handle_shift_out_of_bounds_abort(
                                                     ShiftOutOfBoundsData *Data,
                                                     ValueHandle LHS,
                                                     ValueHandle RHS) {
  GET_REPORT_OPTIONS(true);
  handleShiftOutOfBoundsImpl(Data, LHS, RHS, Opts);
  Die();
}

static void handleOutOfBoundsImpl(OutOfBoundsData *Data, ValueHandle Index,
                                  ReportOptions Opts) {
  SourceLocation Loc = Data->Loc.acquire();
  ErrorType ET = ErrorType::OutOfBoundsIndex;

  if (ignoreReport(Loc, Opts, ET))
    return;

  ScopedReport R(Opts, Loc, ET);

  Value IndexVal(Data->IndexType, Index);
  Diag(Loc, DL_Error, ET, "index %0 out of bounds for type %1")
    << IndexVal << Data->ArrayType;
}

void __ubsan::__ubsan_handle_out_of_bounds(OutOfBoundsData *Data,
                                           ValueHandle Index) {
  GET_REPORT_OPTIONS(false);
  handleOutOfBoundsImpl(Data, Index, Opts);
}
void __ubsan::__ubsan_handle_out_of_bounds_abort(OutOfBoundsData *Data,
                                                 ValueHandle Index) {
  GET_REPORT_OPTIONS(true);
  handleOutOfBoundsImpl(Data, Index, Opts);
  Die();
}

static void handleBuiltinUnreachableImpl(UnreachableData *Data,
                                         ReportOptions Opts) {
  ErrorType ET = ErrorType::UnreachableCall;
  ScopedReport R(Opts, Data->Loc, ET);
  Diag(Data->Loc, DL_Error, ET,
       "execution reached an unreachable program point");
}

void __ubsan::__ubsan_handle_builtin_unreachable(UnreachableData *Data) {
  GET_REPORT_OPTIONS(true);
  handleBuiltinUnreachableImpl(Data, Opts);
  Die();
}

static void handleMissingReturnImpl(UnreachableData *Data, ReportOptions Opts) {
  ErrorType ET = ErrorType::MissingReturn;
  ScopedReport R(Opts, Data->Loc, ET);
  Diag(Data->Loc, DL_Error, ET,
       "execution reached the end of a value-returning function "
       "without returning a value");
}

void __ubsan::__ubsan_handle_missing_return(UnreachableData *Data) {
  GET_REPORT_OPTIONS(true);
  handleMissingReturnImpl(Data, Opts);
  Die();
}

static void handleVLABoundNotPositive(VLABoundData *Data, ValueHandle Bound,
                                      ReportOptions Opts) {
  SourceLocation Loc = Data->Loc.acquire();
  ErrorType ET = ErrorType::NonPositiveVLAIndex;

  if (ignoreReport(Loc, Opts, ET))
    return;

  ScopedReport R(Opts, Loc, ET);

  Diag(Loc, DL_Error, ET, "variable length array bound evaluates to "
                          "non-positive value %0")
      << Value(Data->Type, Bound);
}

void __ubsan::__ubsan_handle_vla_bound_not_positive(VLABoundData *Data,
                                                    ValueHandle Bound) {
  GET_REPORT_OPTIONS(false);
  handleVLABoundNotPositive(Data, Bound, Opts);
}
void __ubsan::__ubsan_handle_vla_bound_not_positive_abort(VLABoundData *Data,
                                                          ValueHandle Bound) {
  GET_REPORT_OPTIONS(true);
  handleVLABoundNotPositive(Data, Bound, Opts);
  Die();
}

static bool looksLikeFloatCastOverflowDataV1(void *Data) {
  // First field is either a pointer to filename or a pointer to a
  // TypeDescriptor.
  u8 *FilenameOrTypeDescriptor;
  internal_memcpy(&FilenameOrTypeDescriptor, Data,
                  sizeof(FilenameOrTypeDescriptor));

  // Heuristic: For float_cast_overflow, the TypeKind will be either TK_Integer
  // (0x0), TK_Float (0x1) or TK_Unknown (0xff). If both types are known,
  // adding both bytes will be 0 or 1 (for BE or LE). If it were a filename,
  // adding two printable characters will not yield such a value. Otherwise,
  // if one of them is 0xff, this is most likely TK_Unknown type descriptor.
  u16 MaybeFromTypeKind =
      FilenameOrTypeDescriptor[0] + FilenameOrTypeDescriptor[1];
  return MaybeFromTypeKind < 2 || FilenameOrTypeDescriptor[0] == 0xff ||
         FilenameOrTypeDescriptor[1] == 0xff;
}

static void handleFloatCastOverflow(void *DataPtr, ValueHandle From,
                                    ReportOptions Opts) {
  SymbolizedStackHolder CallerLoc;
  Location Loc;
  const TypeDescriptor *FromType, *ToType;
  ErrorType ET = ErrorType::FloatCastOverflow;

  if (looksLikeFloatCastOverflowDataV1(DataPtr)) {
    auto Data = reinterpret_cast<FloatCastOverflowData *>(DataPtr);
    CallerLoc.reset(getCallerLocation(Opts.pc));
    Loc = CallerLoc;
    FromType = &Data->FromType;
    ToType = &Data->ToType;
  } else {
    auto Data = reinterpret_cast<FloatCastOverflowDataV2 *>(DataPtr);
    SourceLocation SLoc = Data->Loc.acquire();
    if (ignoreReport(SLoc, Opts, ET))
      return;
    Loc = SLoc;
    FromType = &Data->FromType;
    ToType = &Data->ToType;
  }

  ScopedReport R(Opts, Loc, ET);

  Diag(Loc, DL_Error, ET,
       "%0 is outside the range of representable values of type %2")
      << Value(*FromType, From) << *FromType << *ToType;
}

void __ubsan::__ubsan_handle_float_cast_overflow(void *Data, ValueHandle From) {
  GET_REPORT_OPTIONS(false);
  handleFloatCastOverflow(Data, From, Opts);
}
void __ubsan::__ubsan_handle_float_cast_overflow_abort(void *Data,
                                                       ValueHandle From) {
  GET_REPORT_OPTIONS(true);
  handleFloatCastOverflow(Data, From, Opts);
  Die();
}

static void handleLoadInvalidValue(InvalidValueData *Data, ValueHandle Val,
                                   ReportOptions Opts) {
  SourceLocation Loc = Data->Loc.acquire();
  // This check could be more precise if we used different handlers for
  // -fsanitize=bool and -fsanitize=enum.
  bool IsBool = (0 == internal_strcmp(Data->Type.getTypeName(), "'bool'")) ||
                (0 == internal_strncmp(Data->Type.getTypeName(), "'BOOL'", 6));
  ErrorType ET =
      IsBool ? ErrorType::InvalidBoolLoad : ErrorType::InvalidEnumLoad;

  if (ignoreReport(Loc, Opts, ET))
    return;

  ScopedReport R(Opts, Loc, ET);

  Diag(Loc, DL_Error, ET,
       "load of value %0, which is not a valid value for type %1")
      << Value(Data->Type, Val) << Data->Type;
}

void __ubsan::__ubsan_handle_load_invalid_value(InvalidValueData *Data,
                                                ValueHandle Val) {
  GET_REPORT_OPTIONS(false);
  handleLoadInvalidValue(Data, Val, Opts);
}
void __ubsan::__ubsan_handle_load_invalid_value_abort(InvalidValueData *Data,
                                                      ValueHandle Val) {
  GET_REPORT_OPTIONS(true);
  handleLoadInvalidValue(Data, Val, Opts);
  Die();
}

static void handleImplicitConversion(ImplicitConversionData *Data,
                                     ReportOptions Opts, ValueHandle Src,
                                     ValueHandle Dst) {
  SourceLocation Loc = Data->Loc.acquire();
  const TypeDescriptor &SrcTy = Data->FromType;
  const TypeDescriptor &DstTy = Data->ToType;
  bool SrcSigned = SrcTy.isSignedIntegerTy();
  bool DstSigned = DstTy.isSignedIntegerTy();
  ErrorType ET = ErrorType::GenericUB;

  switch (Data->Kind) {
  case ICCK_IntegerTruncation: { // Legacy, no longer used.
    // Let's figure out what it should be as per the new types, and upgrade.
    // If both types are unsigned, then it's an unsigned truncation.
    // Else, it is a signed truncation.
    if (!SrcSigned && !DstSigned) {
      ET = ErrorType::ImplicitUnsignedIntegerTruncation;
    } else {
      ET = ErrorType::ImplicitSignedIntegerTruncation;
    }
    break;
  }
  case ICCK_UnsignedIntegerTruncation:
    ET = ErrorType::ImplicitUnsignedIntegerTruncation;
    break;
  case ICCK_SignedIntegerTruncation:
    ET = ErrorType::ImplicitSignedIntegerTruncation;
    break;
  case ICCK_IntegerSignChange:
    ET = ErrorType::ImplicitIntegerSignChange;
    break;
  case ICCK_SignedIntegerTruncationOrSignChange:
    ET = ErrorType::ImplicitSignedIntegerTruncationOrSignChange;
    break;
  }

  if (ignoreReport(Loc, Opts, ET))
    return;

  ScopedReport R(Opts, Loc, ET);

  // In the case we have a bitfield, we want to explicitly say so in the
  // error message.
  // FIXME: is it possible to dump the values as hex with fixed width?
  if (Data->BitfieldBits)
    Diag(Loc, DL_Error, ET,
         "implicit conversion from type %0 of value %1 (%2-bit, %3signed) to "
         "type %4 changed the value to %5 (%6-bit bitfield, %7signed)")
        << SrcTy << Value(SrcTy, Src) << SrcTy.getIntegerBitWidth()
        << (SrcSigned ? "" : "un") << DstTy << Value(DstTy, Dst)
        << Data->BitfieldBits << (DstSigned ? "" : "un");
  else
    Diag(Loc, DL_Error, ET,
         "implicit conversion from type %0 of value %1 (%2-bit, %3signed) to "
         "type %4 changed the value to %5 (%6-bit, %7signed)")
        << SrcTy << Value(SrcTy, Src) << SrcTy.getIntegerBitWidth()
        << (SrcSigned ? "" : "un") << DstTy << Value(DstTy, Dst)
        << DstTy.getIntegerBitWidth() << (DstSigned ? "" : "un");
}

void __ubsan::__ubsan_handle_implicit_conversion(ImplicitConversionData *Data,
                                                 ValueHandle Src,
                                                 ValueHandle Dst) {
  GET_REPORT_OPTIONS(false);
  handleImplicitConversion(Data, Opts, Src, Dst);
}
void __ubsan::__ubsan_handle_implicit_conversion_abort(
    ImplicitConversionData *Data, ValueHandle Src, ValueHandle Dst) {
  GET_REPORT_OPTIONS(true);
  handleImplicitConversion(Data, Opts, Src, Dst);
  Die();
}

static void handleInvalidBuiltin(InvalidBuiltinData *Data, ReportOptions Opts) {
  SourceLocation Loc = Data->Loc.acquire();
  ErrorType ET = ErrorType::InvalidBuiltin;

  if (ignoreReport(Loc, Opts, ET))
    return;

  ScopedReport R(Opts, Loc, ET);

  Diag(Loc, DL_Error, ET,
       "passing zero to %0, which is not a valid argument")
    << ((Data->Kind == BCK_CTZPassedZero) ? "ctz()" : "clz()");
}

void __ubsan::__ubsan_handle_invalid_builtin(InvalidBuiltinData *Data) {
  GET_REPORT_OPTIONS(true);
  handleInvalidBuiltin(Data, Opts);
}
void __ubsan::__ubsan_handle_invalid_builtin_abort(InvalidBuiltinData *Data) {
  GET_REPORT_OPTIONS(true);
  handleInvalidBuiltin(Data, Opts);
  Die();
}

static void handleInvalidObjCCast(InvalidObjCCast *Data, ValueHandle Pointer,
                                  ReportOptions Opts) {
  SourceLocation Loc = Data->Loc.acquire();
  ErrorType ET = ErrorType::InvalidObjCCast;

  if (ignoreReport(Loc, Opts, ET))
    return;

  ScopedReport R(Opts, Loc, ET);

  const char *GivenClass = getObjCClassName(Pointer);
  const char *GivenClassStr = GivenClass ? GivenClass : "<unknown type>";

  Diag(Loc, DL_Error, ET,
       "invalid ObjC cast, object is a '%0', but expected a %1")
      << GivenClassStr << Data->ExpectedType;
}

void __ubsan::__ubsan_handle_invalid_objc_cast(InvalidObjCCast *Data,
                                               ValueHandle Pointer) {
  GET_REPORT_OPTIONS(false);
  handleInvalidObjCCast(Data, Pointer, Opts);
}
void __ubsan::__ubsan_handle_invalid_objc_cast_abort(InvalidObjCCast *Data,
                                                     ValueHandle Pointer) {
  GET_REPORT_OPTIONS(true);
  handleInvalidObjCCast(Data, Pointer, Opts);
  Die();
}

static void handleNonNullReturn(NonNullReturnData *Data, SourceLocation *LocPtr,
                                ReportOptions Opts, bool IsAttr) {
  if (!LocPtr)
    UNREACHABLE("source location pointer is null!");

  SourceLocation Loc = LocPtr->acquire();
  ErrorType ET = IsAttr ? ErrorType::InvalidNullReturn
                        : ErrorType::InvalidNullReturnWithNullability;

  if (ignoreReport(Loc, Opts, ET))
    return;

  ScopedReport R(Opts, Loc, ET);

  Diag(Loc, DL_Error, ET,
       "null pointer returned from function declared to never return null");
  if (!Data->AttrLoc.isInvalid())
    Diag(Data->AttrLoc, DL_Note, ET, "%0 specified here")
        << (IsAttr ? "returns_nonnull attribute"
                   : "_Nonnull return type annotation");
}

void __ubsan::__ubsan_handle_nonnull_return_v1(NonNullReturnData *Data,
                                               SourceLocation *LocPtr) {
  GET_REPORT_OPTIONS(false);
  handleNonNullReturn(Data, LocPtr, Opts, true);
}

void __ubsan::__ubsan_handle_nonnull_return_v1_abort(NonNullReturnData *Data,
                                                     SourceLocation *LocPtr) {
  GET_REPORT_OPTIONS(true);
  handleNonNullReturn(Data, LocPtr, Opts, true);
  Die();
}

void __ubsan::__ubsan_handle_nullability_return_v1(NonNullReturnData *Data,
                                                   SourceLocation *LocPtr) {
  GET_REPORT_OPTIONS(false);
  handleNonNullReturn(Data, LocPtr, Opts, false);
}

void __ubsan::__ubsan_handle_nullability_return_v1_abort(
    NonNullReturnData *Data, SourceLocation *LocPtr) {
  GET_REPORT_OPTIONS(true);
  handleNonNullReturn(Data, LocPtr, Opts, false);
  Die();
}

static void handleNonNullArg(NonNullArgData *Data, ReportOptions Opts,
                             bool IsAttr) {
  SourceLocation Loc = Data->Loc.acquire();
  ErrorType ET = IsAttr ? ErrorType::InvalidNullArgument
                        : ErrorType::InvalidNullArgumentWithNullability;

  if (ignoreReport(Loc, Opts, ET))
    return;

  ScopedReport R(Opts, Loc, ET);

  Diag(Loc, DL_Error, ET,
       "null pointer passed as argument %0, which is declared to "
       "never be null")
      << Data->ArgIndex;
  if (!Data->AttrLoc.isInvalid())
    Diag(Data->AttrLoc, DL_Note, ET, "%0 specified here")
        << (IsAttr ? "nonnull attribute" : "_Nonnull type annotation");
}

void __ubsan::__ubsan_handle_nonnull_arg(NonNullArgData *Data) {
  GET_REPORT_OPTIONS(false);
  handleNonNullArg(Data, Opts, true);
}

void __ubsan::__ubsan_handle_nonnull_arg_abort(NonNullArgData *Data) {
  GET_REPORT_OPTIONS(true);
  handleNonNullArg(Data, Opts, true);
  Die();
}

void __ubsan::__ubsan_handle_nullability_arg(NonNullArgData *Data) {
  GET_REPORT_OPTIONS(false);
  handleNonNullArg(Data, Opts, false);
}

void __ubsan::__ubsan_handle_nullability_arg_abort(NonNullArgData *Data) {
  GET_REPORT_OPTIONS(true);
  handleNonNullArg(Data, Opts, false);
  Die();
}

static void handlePointerOverflowImpl(PointerOverflowData *Data,
                                      ValueHandle Base,
                                      ValueHandle Result,
                                      ReportOptions Opts) {
  SourceLocation Loc = Data->Loc.acquire();
  ErrorType ET;

  if (Base == 0 && Result == 0)
    ET = ErrorType::NullptrWithOffset;
  else if (Base == 0 && Result != 0)
    ET = ErrorType::NullptrWithNonZeroOffset;
  else if (Base != 0 && Result == 0)
    ET = ErrorType::NullptrAfterNonZeroOffset;
  else
    ET = ErrorType::PointerOverflow;

  if (ignoreReport(Loc, Opts, ET))
    return;

  ScopedReport R(Opts, Loc, ET);

  if (ET == ErrorType::NullptrWithOffset) {
    Diag(Loc, DL_Error, ET, "applying zero offset to null pointer");
  } else if (ET == ErrorType::NullptrWithNonZeroOffset) {
    Diag(Loc, DL_Error, ET, "applying non-zero offset %0 to null pointer")
        << Result;
  } else if (ET == ErrorType::NullptrAfterNonZeroOffset) {
    Diag(
        Loc, DL_Error, ET,
        "applying non-zero offset to non-null pointer %0 produced null pointer")
        << (void *)Base;
  } else if ((sptr(Base) >= 0) == (sptr(Result) >= 0)) {
    if (Base > Result)
      Diag(Loc, DL_Error, ET,
           "addition of unsigned offset to %0 overflowed to %1")
          << (void *)Base << (void *)Result;
    else
      Diag(Loc, DL_Error, ET,
           "subtraction of unsigned offset from %0 overflowed to %1")
          << (void *)Base << (void *)Result;
  } else {
    Diag(Loc, DL_Error, ET,
         "pointer index expression with base %0 overflowed to %1")
        << (void *)Base << (void *)Result;
  }
}

void __ubsan::__ubsan_handle_pointer_overflow(PointerOverflowData *Data,
                                              ValueHandle Base,
                                              ValueHandle Result) {
  GET_REPORT_OPTIONS(false);
  handlePointerOverflowImpl(Data, Base, Result, Opts);
}

void __ubsan::__ubsan_handle_pointer_overflow_abort(PointerOverflowData *Data,
                                                    ValueHandle Base,
                                                    ValueHandle Result) {
  GET_REPORT_OPTIONS(true);
  handlePointerOverflowImpl(Data, Base, Result, Opts);
  Die();
}

static void handleCFIBadIcall(CFICheckFailData *Data, ValueHandle Function,
                              ReportOptions Opts) {
  if (Data->CheckKind != CFITCK_ICall && Data->CheckKind != CFITCK_NVMFCall)
    Die();

  SourceLocation Loc = Data->Loc.acquire();
  ErrorType ET = ErrorType::CFIBadType;

  if (ignoreReport(Loc, Opts, ET))
    return;

  ScopedReport R(Opts, Loc, ET);

  const char *CheckKindStr = Data->CheckKind == CFITCK_NVMFCall
                                 ? "non-virtual pointer to member function call"
                                 : "indirect function call";
  Diag(Loc, DL_Error, ET,
       "control flow integrity check for type %0 failed during %1")
      << Data->Type << CheckKindStr;

  SymbolizedStackHolder FLoc(getSymbolizedLocation(Function));
  const char *FName = FLoc.get()->info.function;
  if (!FName)
    FName = "(unknown)";
  Diag(FLoc, DL_Note, ET, "%0 defined here") << FName;

  // If the failure involved different DSOs for the check location and icall
  // target, report the DSO names.
  const char *DstModule = FLoc.get()->info.module;
  if (!DstModule)
    DstModule = "(unknown)";

  const char *SrcModule = Symbolizer::GetOrInit()->GetModuleNameForPc(Opts.pc);
  if (!SrcModule)
    SrcModule = "(unknown)";

  if (internal_strcmp(SrcModule, DstModule))
    Diag(Loc, DL_Note, ET,
         "check failed in %0, destination function located in %1")
        << SrcModule << DstModule;
}

namespace __ubsan {

#ifdef UBSAN_CAN_USE_CXXABI

#ifdef _WIN32

extern "C" void __ubsan_handle_cfi_bad_type_default(CFICheckFailData *Data,
                                                    ValueHandle Vtable,
                                                    bool ValidVtable,
                                                    ReportOptions Opts) {
  Die();
}

WIN_WEAK_ALIAS(__ubsan_handle_cfi_bad_type, __ubsan_handle_cfi_bad_type_default)
#else
SANITIZER_WEAK_ATTRIBUTE
#endif
void __ubsan_handle_cfi_bad_type(CFICheckFailData *Data, ValueHandle Vtable,
                                 bool ValidVtable, ReportOptions Opts);

#else
void __ubsan_handle_cfi_bad_type(CFICheckFailData *Data, ValueHandle Vtable,
                                 bool ValidVtable, ReportOptions Opts) {
  Die();
}
#endif

}  // namespace __ubsan

void __ubsan::__ubsan_handle_cfi_check_fail(CFICheckFailData *Data,
                                            ValueHandle Value,
                                            uptr ValidVtable) {
  GET_REPORT_OPTIONS(false);
  if (Data->CheckKind == CFITCK_ICall || Data->CheckKind == CFITCK_NVMFCall)
    handleCFIBadIcall(Data, Value, Opts);
  else
    __ubsan_handle_cfi_bad_type(Data, Value, ValidVtable, Opts);
}

void __ubsan::__ubsan_handle_cfi_check_fail_abort(CFICheckFailData *Data,
                                                  ValueHandle Value,
                                                  uptr ValidVtable) {
  GET_REPORT_OPTIONS(true);
  if (Data->CheckKind == CFITCK_ICall || Data->CheckKind == CFITCK_NVMFCall)
    handleCFIBadIcall(Data, Value, Opts);
  else
    __ubsan_handle_cfi_bad_type(Data, Value, ValidVtable, Opts);
  Die();
}

static bool handleFunctionTypeMismatch(FunctionTypeMismatchData *Data,
                                       ValueHandle Function,
                                       ReportOptions Opts) {
  SourceLocation CallLoc = Data->Loc.acquire();
  ErrorType ET = ErrorType::FunctionTypeMismatch;
  if (ignoreReport(CallLoc, Opts, ET))
    return true;

  ScopedReport R(Opts, CallLoc, ET);

  SymbolizedStackHolder FLoc(getSymbolizedLocation(Function));
  const char *FName = FLoc.get()->info.function;
  if (!FName)
    FName = "(unknown)";

  Diag(CallLoc, DL_Error, ET,
       "call to function %0 through pointer to incorrect function type %1")
      << FName << Data->Type;
  Diag(FLoc, DL_Note, ET, "%0 defined here") << FName;
  return true;
}

void __ubsan::__ubsan_handle_function_type_mismatch(
    FunctionTypeMismatchData *Data, ValueHandle Function) {
  GET_REPORT_OPTIONS(false);
  handleFunctionTypeMismatch(Data, Function, Opts);
}

void __ubsan::__ubsan_handle_function_type_mismatch_abort(
    FunctionTypeMismatchData *Data, ValueHandle Function) {
  GET_REPORT_OPTIONS(true);
  if (handleFunctionTypeMismatch(Data, Function, Opts))
    Die();
}

#endif  // CAN_SANITIZE_UB

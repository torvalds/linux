//===-- ubsan_handlers.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Entry points to the runtime library for Clang's undefined behavior sanitizer.
//
//===----------------------------------------------------------------------===//
#ifndef UBSAN_HANDLERS_H
#define UBSAN_HANDLERS_H

#include "ubsan_value.h"

namespace __ubsan {

struct TypeMismatchData {
  SourceLocation Loc;
  const TypeDescriptor &Type;
  unsigned char LogAlignment;
  unsigned char TypeCheckKind;
};

#define UNRECOVERABLE(checkname, ...) \
  extern "C" SANITIZER_INTERFACE_ATTRIBUTE NORETURN \
    void __ubsan_handle_ ## checkname( __VA_ARGS__ );

#define RECOVERABLE(checkname, ...) \
  extern "C" SANITIZER_INTERFACE_ATTRIBUTE \
    void __ubsan_handle_ ## checkname( __VA_ARGS__ ); \
  extern "C" SANITIZER_INTERFACE_ATTRIBUTE NORETURN \
    void __ubsan_handle_ ## checkname ## _abort( __VA_ARGS__ );

/// \brief Handle a runtime type check failure, caused by either a misaligned
/// pointer, a null pointer, or a pointer to insufficient storage for the
/// type.
RECOVERABLE(type_mismatch_v1, TypeMismatchData *Data, ValueHandle Pointer)

struct AlignmentAssumptionData {
  SourceLocation Loc;
  SourceLocation AssumptionLoc;
  const TypeDescriptor &Type;
};

/// \brief Handle a runtime alignment assumption check failure,
/// caused by a misaligned pointer.
RECOVERABLE(alignment_assumption, AlignmentAssumptionData *Data,
            ValueHandle Pointer, ValueHandle Alignment, ValueHandle Offset)

struct OverflowData {
  SourceLocation Loc;
  const TypeDescriptor &Type;
};

/// \brief Handle an integer addition overflow.
RECOVERABLE(add_overflow, OverflowData *Data, ValueHandle LHS, ValueHandle RHS)

/// \brief Handle an integer subtraction overflow.
RECOVERABLE(sub_overflow, OverflowData *Data, ValueHandle LHS, ValueHandle RHS)

/// \brief Handle an integer multiplication overflow.
RECOVERABLE(mul_overflow, OverflowData *Data, ValueHandle LHS, ValueHandle RHS)

/// \brief Handle a signed integer overflow for a unary negate operator.
RECOVERABLE(negate_overflow, OverflowData *Data, ValueHandle OldVal)

/// \brief Handle an INT_MIN/-1 overflow or division by zero.
RECOVERABLE(divrem_overflow, OverflowData *Data,
            ValueHandle LHS, ValueHandle RHS)

struct ShiftOutOfBoundsData {
  SourceLocation Loc;
  const TypeDescriptor &LHSType;
  const TypeDescriptor &RHSType;
};

/// \brief Handle a shift where the RHS is out of bounds or a left shift where
/// the LHS is negative or overflows.
RECOVERABLE(shift_out_of_bounds, ShiftOutOfBoundsData *Data,
            ValueHandle LHS, ValueHandle RHS)

struct OutOfBoundsData {
  SourceLocation Loc;
  const TypeDescriptor &ArrayType;
  const TypeDescriptor &IndexType;
};

/// \brief Handle an array index out of bounds error.
RECOVERABLE(out_of_bounds, OutOfBoundsData *Data, ValueHandle Index)

struct UnreachableData {
  SourceLocation Loc;
};

/// \brief Handle a __builtin_unreachable which is reached.
UNRECOVERABLE(builtin_unreachable, UnreachableData *Data)
/// \brief Handle reaching the end of a value-returning function.
UNRECOVERABLE(missing_return, UnreachableData *Data)

struct VLABoundData {
  SourceLocation Loc;
  const TypeDescriptor &Type;
};

/// \brief Handle a VLA with a non-positive bound.
RECOVERABLE(vla_bound_not_positive, VLABoundData *Data, ValueHandle Bound)

// Keeping this around for binary compatibility with (sanitized) programs
// compiled with older compilers.
struct FloatCastOverflowData {
  const TypeDescriptor &FromType;
  const TypeDescriptor &ToType;
};

struct FloatCastOverflowDataV2 {
  SourceLocation Loc;
  const TypeDescriptor &FromType;
  const TypeDescriptor &ToType;
};

/// Handle overflow in a conversion to or from a floating-point type.
/// void *Data is one of FloatCastOverflowData* or FloatCastOverflowDataV2*
RECOVERABLE(float_cast_overflow, void *Data, ValueHandle From)

struct InvalidValueData {
  SourceLocation Loc;
  const TypeDescriptor &Type;
};

/// \brief Handle a load of an invalid value for the type.
RECOVERABLE(load_invalid_value, InvalidValueData *Data, ValueHandle Val)

/// Known implicit conversion check kinds.
/// Keep in sync with the enum of the same name in CGExprScalar.cpp
enum ImplicitConversionCheckKind : unsigned char {
  ICCK_IntegerTruncation = 0, // Legacy, was only used by clang 7.
  ICCK_UnsignedIntegerTruncation = 1,
  ICCK_SignedIntegerTruncation = 2,
  ICCK_IntegerSignChange = 3,
  ICCK_SignedIntegerTruncationOrSignChange = 4,
};

struct ImplicitConversionData {
  SourceLocation Loc;
  const TypeDescriptor &FromType;
  const TypeDescriptor &ToType;
  /* ImplicitConversionCheckKind */ unsigned char Kind;
  unsigned int BitfieldBits;
};

/// \brief Implict conversion that changed the value.
RECOVERABLE(implicit_conversion, ImplicitConversionData *Data, ValueHandle Src,
            ValueHandle Dst)

/// Known builtin check kinds.
/// Keep in sync with the enum of the same name in CodeGenFunction.h
enum BuiltinCheckKind : unsigned char {
  BCK_CTZPassedZero,
  BCK_CLZPassedZero,
};

struct InvalidBuiltinData {
  SourceLocation Loc;
  unsigned char Kind;
};

/// Handle a builtin called in an invalid way.
RECOVERABLE(invalid_builtin, InvalidBuiltinData *Data)

struct InvalidObjCCast {
  SourceLocation Loc;
  const TypeDescriptor &ExpectedType;
};

/// Handle an invalid ObjC cast.
RECOVERABLE(invalid_objc_cast, InvalidObjCCast *Data, ValueHandle Pointer)

struct NonNullReturnData {
  SourceLocation AttrLoc;
};

/// \brief Handle returning null from function with the returns_nonnull
/// attribute, or a return type annotated with _Nonnull.
RECOVERABLE(nonnull_return_v1, NonNullReturnData *Data, SourceLocation *Loc)
RECOVERABLE(nullability_return_v1, NonNullReturnData *Data, SourceLocation *Loc)

struct NonNullArgData {
  SourceLocation Loc;
  SourceLocation AttrLoc;
  int ArgIndex;
};

/// \brief Handle passing null pointer to a function parameter with the nonnull
/// attribute, or a _Nonnull type annotation.
RECOVERABLE(nonnull_arg, NonNullArgData *Data)
RECOVERABLE(nullability_arg, NonNullArgData *Data)

struct PointerOverflowData {
  SourceLocation Loc;
};

RECOVERABLE(pointer_overflow, PointerOverflowData *Data, ValueHandle Base,
            ValueHandle Result)

/// \brief Known CFI check kinds.
/// Keep in sync with the enum of the same name in CodeGenFunction.h
enum CFITypeCheckKind : unsigned char {
  CFITCK_VCall,
  CFITCK_NVCall,
  CFITCK_DerivedCast,
  CFITCK_UnrelatedCast,
  CFITCK_ICall,
  CFITCK_NVMFCall,
  CFITCK_VMFCall,
};

struct CFICheckFailData {
  CFITypeCheckKind CheckKind;
  SourceLocation Loc;
  const TypeDescriptor &Type;
};

/// \brief Handle control flow integrity failures.
RECOVERABLE(cfi_check_fail, CFICheckFailData *Data, ValueHandle Function,
            uptr VtableIsValid)

struct ReportOptions;

extern "C" SANITIZER_INTERFACE_ATTRIBUTE void __ubsan_handle_cfi_bad_type(
    CFICheckFailData *Data, ValueHandle Vtable, bool ValidVtable,
    ReportOptions Opts);

struct FunctionTypeMismatchData {
  SourceLocation Loc;
  const TypeDescriptor &Type;
};

extern "C" SANITIZER_INTERFACE_ATTRIBUTE void
__ubsan_handle_function_type_mismatch(FunctionTypeMismatchData *Data,
                                      ValueHandle Val);
extern "C" SANITIZER_INTERFACE_ATTRIBUTE void
__ubsan_handle_function_type_mismatch_abort(FunctionTypeMismatchData *Data,
                                            ValueHandle Val);
}

#endif // UBSAN_HANDLERS_H

//===-- ubsan_checks.inc ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// List of checks handled by UBSan runtime.
//
//===----------------------------------------------------------------------===//
#ifndef UBSAN_CHECK
# error "Define UBSAN_CHECK prior to including this file!"
#endif

// UBSAN_CHECK(Name, SummaryKind, FSanitizeFlagName)
// SummaryKind and FSanitizeFlagName should be string literals.

UBSAN_CHECK(GenericUB, "undefined-behavior", "undefined")
UBSAN_CHECK(NullPointerUse, "null-pointer-use", "null")
UBSAN_CHECK(NullPointerUseWithNullability, "null-pointer-use",
            "nullability-assign")
UBSAN_CHECK(NullptrWithOffset, "nullptr-with-offset", "pointer-overflow")
UBSAN_CHECK(NullptrWithNonZeroOffset, "nullptr-with-nonzero-offset",
            "pointer-overflow")
UBSAN_CHECK(NullptrAfterNonZeroOffset, "nullptr-after-nonzero-offset",
            "pointer-overflow")
UBSAN_CHECK(PointerOverflow, "pointer-overflow", "pointer-overflow")
UBSAN_CHECK(MisalignedPointerUse, "misaligned-pointer-use", "alignment")
UBSAN_CHECK(AlignmentAssumption, "alignment-assumption", "alignment")
UBSAN_CHECK(InsufficientObjectSize, "insufficient-object-size", "object-size")
UBSAN_CHECK(SignedIntegerOverflow, "signed-integer-overflow",
            "signed-integer-overflow")
UBSAN_CHECK(UnsignedIntegerOverflow, "unsigned-integer-overflow",
            "unsigned-integer-overflow")
UBSAN_CHECK(IntegerDivideByZero, "integer-divide-by-zero",
            "integer-divide-by-zero")
UBSAN_CHECK(FloatDivideByZero, "float-divide-by-zero", "float-divide-by-zero")
UBSAN_CHECK(InvalidBuiltin, "invalid-builtin-use", "invalid-builtin-use")
UBSAN_CHECK(InvalidObjCCast, "invalid-objc-cast", "invalid-objc-cast")
UBSAN_CHECK(ImplicitUnsignedIntegerTruncation,
            "implicit-unsigned-integer-truncation",
            "implicit-unsigned-integer-truncation")
UBSAN_CHECK(ImplicitSignedIntegerTruncation,
            "implicit-signed-integer-truncation",
            "implicit-signed-integer-truncation")
UBSAN_CHECK(ImplicitIntegerSignChange,
            "implicit-integer-sign-change",
            "implicit-integer-sign-change")
UBSAN_CHECK(ImplicitSignedIntegerTruncationOrSignChange,
            "implicit-signed-integer-truncation-or-sign-change",
            "implicit-signed-integer-truncation,implicit-integer-sign-change")
UBSAN_CHECK(InvalidShiftBase, "invalid-shift-base", "shift-base")
UBSAN_CHECK(InvalidShiftExponent, "invalid-shift-exponent", "shift-exponent")
UBSAN_CHECK(OutOfBoundsIndex, "out-of-bounds-index", "bounds")
UBSAN_CHECK(UnreachableCall, "unreachable-call", "unreachable")
UBSAN_CHECK(MissingReturn, "missing-return", "return")
UBSAN_CHECK(NonPositiveVLAIndex, "non-positive-vla-index", "vla-bound")
UBSAN_CHECK(FloatCastOverflow, "float-cast-overflow", "float-cast-overflow")
UBSAN_CHECK(InvalidBoolLoad, "invalid-bool-load", "bool")
UBSAN_CHECK(InvalidEnumLoad, "invalid-enum-load", "enum")
UBSAN_CHECK(FunctionTypeMismatch, "function-type-mismatch", "function")
UBSAN_CHECK(InvalidNullReturn, "invalid-null-return",
            "returns-nonnull-attribute")
UBSAN_CHECK(InvalidNullReturnWithNullability, "invalid-null-return",
            "nullability-return")
UBSAN_CHECK(InvalidNullArgument, "invalid-null-argument", "nonnull-attribute")
UBSAN_CHECK(InvalidNullArgumentWithNullability, "invalid-null-argument",
            "nullability-arg")
UBSAN_CHECK(DynamicTypeMismatch, "dynamic-type-mismatch", "vptr")
UBSAN_CHECK(CFIBadType, "cfi-bad-type", "cfi")

//===- FPEnv.h ---- FP Environment ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// @file
/// This file contains the declarations of entities that describe floating
/// point environment and related functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_FPENV_H
#define LLVM_IR_FPENV_H

#include "llvm/ADT/FloatingPointMode.h"
#include "llvm/IR/FMF.h"
#include <optional>

namespace llvm {
class StringRef;

namespace Intrinsic {
typedef unsigned ID;
}

class Instruction;

namespace fp {

/// Exception behavior used for floating point operations.
///
/// Each of these values correspond to some metadata argument value of a
/// constrained floating point intrinsic. See the LLVM Language Reference Manual
/// for details.
enum ExceptionBehavior : uint8_t {
  ebIgnore,  ///< This corresponds to "fpexcept.ignore".
  ebMayTrap, ///< This corresponds to "fpexcept.maytrap".
  ebStrict   ///< This corresponds to "fpexcept.strict".
};

}

/// Returns a valid RoundingMode enumerator when given a string
/// that is valid as input in constrained intrinsic rounding mode
/// metadata.
std::optional<RoundingMode> convertStrToRoundingMode(StringRef);

/// For any RoundingMode enumerator, returns a string valid as input in
/// constrained intrinsic rounding mode metadata.
std::optional<StringRef> convertRoundingModeToStr(RoundingMode);

/// Returns a valid ExceptionBehavior enumerator when given a string
/// valid as input in constrained intrinsic exception behavior metadata.
std::optional<fp::ExceptionBehavior> convertStrToExceptionBehavior(StringRef);

/// For any ExceptionBehavior enumerator, returns a string valid as
/// input in constrained intrinsic exception behavior metadata.
std::optional<StringRef> convertExceptionBehaviorToStr(fp::ExceptionBehavior);

/// Returns true if the exception handling behavior and rounding mode
/// match what is used in the default floating point environment.
inline bool isDefaultFPEnvironment(fp::ExceptionBehavior EB, RoundingMode RM) {
  return EB == fp::ebIgnore && RM == RoundingMode::NearestTiesToEven;
}

/// Returns constrained intrinsic id to represent the given instruction in
/// strictfp function. If the instruction is already a constrained intrinsic or
/// does not have a constrained intrinsic counterpart, the function returns
/// zero.
Intrinsic::ID getConstrainedIntrinsicID(const Instruction &Instr);

/// Returns true if the rounding mode RM may be QRM at compile time or
/// at run time.
inline bool canRoundingModeBe(RoundingMode RM, RoundingMode QRM) {
  return RM == QRM || RM == RoundingMode::Dynamic;
}

/// Returns true if the possibility of a signaling NaN can be safely
/// ignored.
inline bool canIgnoreSNaN(fp::ExceptionBehavior EB, FastMathFlags FMF) {
  return (EB == fp::ebIgnore || FMF.noNaNs());
}
}
#endif

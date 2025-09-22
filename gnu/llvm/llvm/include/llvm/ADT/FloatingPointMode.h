//===- llvm/Support/FloatingPointMode.h -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Utilities for dealing with flags related to floating point properties and
/// mode controls.
///
//===----------------------------------------------------------------------===/

#ifndef LLVM_ADT_FLOATINGPOINTMODE_H
#define LLVM_ADT_FLOATINGPOINTMODE_H

#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

/// Rounding mode.
///
/// Enumerates supported rounding modes, as well as some special values. The set
/// of the modes must agree with IEEE-754, 4.3.1 and 4.3.2. The constants
/// assigned to the IEEE rounding modes must agree with the values used by
/// FLT_ROUNDS (C11, 5.2.4.2.2p8).
///
/// This value is packed into bitfield in some cases, including \c FPOptions, so
/// the rounding mode values and the special value \c Dynamic must fit into the
/// the bit field (now - 3 bits). The value \c Invalid is used only in values
/// returned by intrinsics to indicate errors, it should never be stored as
/// rounding mode value, so it does not need to fit the bit fields.
///
enum class RoundingMode : int8_t {
  // Rounding mode defined in IEEE-754.
  TowardZero        = 0,    ///< roundTowardZero.
  NearestTiesToEven = 1,    ///< roundTiesToEven.
  TowardPositive    = 2,    ///< roundTowardPositive.
  TowardNegative    = 3,    ///< roundTowardNegative.
  NearestTiesToAway = 4,    ///< roundTiesToAway.

  // Special values.
  Dynamic = 7,    ///< Denotes mode unknown at compile time.
  Invalid = -1    ///< Denotes invalid value.
};

/// Returns text representation of the given rounding mode.
inline StringRef spell(RoundingMode RM) {
  switch (RM) {
  case RoundingMode::TowardZero: return "towardzero";
  case RoundingMode::NearestTiesToEven: return "tonearest";
  case RoundingMode::TowardPositive: return "upward";
  case RoundingMode::TowardNegative: return "downward";
  case RoundingMode::NearestTiesToAway: return "tonearestaway";
  case RoundingMode::Dynamic: return "dynamic";
  default: return "invalid";
  }
}

inline raw_ostream &operator << (raw_ostream &OS, RoundingMode RM) {
  OS << spell(RM);
  return OS;
}

/// Represent subnormal handling kind for floating point instruction inputs and
/// outputs.
struct DenormalMode {
  /// Represent handled modes for denormal (aka subnormal) modes in the floating
  /// point environment.
  enum DenormalModeKind : int8_t {
    Invalid = -1,

    /// IEEE-754 denormal numbers preserved.
    IEEE,

    /// The sign of a flushed-to-zero number is preserved in the sign of 0
    PreserveSign,

    /// Denormals are flushed to positive zero.
    PositiveZero,

    /// Denormals have unknown treatment.
    Dynamic
  };

  /// Denormal flushing mode for floating point instruction results in the
  /// default floating point environment.
  DenormalModeKind Output = DenormalModeKind::Invalid;

  /// Denormal treatment kind for floating point instruction inputs in the
  /// default floating-point environment. If this is not DenormalModeKind::IEEE,
  /// floating-point instructions implicitly treat the input value as 0.
  DenormalModeKind Input = DenormalModeKind::Invalid;

  constexpr DenormalMode() = default;
  constexpr DenormalMode(const DenormalMode &) = default;
  constexpr DenormalMode(DenormalModeKind Out, DenormalModeKind In) :
    Output(Out), Input(In) {}

  DenormalMode &operator=(const DenormalMode &) = default;

  static constexpr DenormalMode getInvalid() {
    return DenormalMode(DenormalModeKind::Invalid, DenormalModeKind::Invalid);
  }

  /// Return the assumed default mode for a function without denormal-fp-math.
  static constexpr DenormalMode getDefault() {
    return getIEEE();
  }

  static constexpr DenormalMode getIEEE() {
    return DenormalMode(DenormalModeKind::IEEE, DenormalModeKind::IEEE);
  }

  static constexpr DenormalMode getPreserveSign() {
    return DenormalMode(DenormalModeKind::PreserveSign,
                        DenormalModeKind::PreserveSign);
  }

  static constexpr DenormalMode getPositiveZero() {
    return DenormalMode(DenormalModeKind::PositiveZero,
                        DenormalModeKind::PositiveZero);
  }

  static constexpr DenormalMode getDynamic() {
    return DenormalMode(DenormalModeKind::Dynamic, DenormalModeKind::Dynamic);
  }

  bool operator==(DenormalMode Other) const {
    return Output == Other.Output && Input == Other.Input;
  }

  bool operator!=(DenormalMode Other) const {
    return !(*this == Other);
  }

  bool isSimple() const {
    return Input == Output;
  }

  bool isValid() const {
    return Output != DenormalModeKind::Invalid &&
           Input != DenormalModeKind::Invalid;
  }

  /// Return true if input denormals must be implicitly treated as 0.
  constexpr bool inputsAreZero() const {
    return Input == DenormalModeKind::PreserveSign ||
           Input == DenormalModeKind::PositiveZero;
  }

  /// Return true if output denormals should be flushed to 0.
  constexpr bool outputsAreZero() const {
    return Output == DenormalModeKind::PreserveSign ||
           Output == DenormalModeKind::PositiveZero;
  }

  /// Get the effective denormal mode if the mode if this caller calls into a
  /// function with \p Callee. This promotes dynamic modes to the mode of the
  /// caller.
  DenormalMode mergeCalleeMode(DenormalMode Callee) const {
    DenormalMode MergedMode = Callee;
    if (Callee.Input == DenormalMode::Dynamic)
      MergedMode.Input = Input;
    if (Callee.Output == DenormalMode::Dynamic)
      MergedMode.Output = Output;
    return MergedMode;
  }

  inline void print(raw_ostream &OS) const;

  inline std::string str() const {
    std::string storage;
    raw_string_ostream OS(storage);
    print(OS);
    return storage;
  }
};

inline raw_ostream& operator<<(raw_ostream &OS, DenormalMode Mode) {
  Mode.print(OS);
  return OS;
}

/// Parse the expected names from the denormal-fp-math attribute.
inline DenormalMode::DenormalModeKind
parseDenormalFPAttributeComponent(StringRef Str) {
  // Assume ieee on unspecified attribute.
  return StringSwitch<DenormalMode::DenormalModeKind>(Str)
      .Cases("", "ieee", DenormalMode::IEEE)
      .Case("preserve-sign", DenormalMode::PreserveSign)
      .Case("positive-zero", DenormalMode::PositiveZero)
      .Case("dynamic", DenormalMode::Dynamic)
      .Default(DenormalMode::Invalid);
}

/// Return the name used for the denormal handling mode used by the
/// expected names from the denormal-fp-math attribute.
inline StringRef denormalModeKindName(DenormalMode::DenormalModeKind Mode) {
  switch (Mode) {
  case DenormalMode::IEEE:
    return "ieee";
  case DenormalMode::PreserveSign:
    return "preserve-sign";
  case DenormalMode::PositiveZero:
    return "positive-zero";
  case DenormalMode::Dynamic:
    return "dynamic";
  default:
    return "";
  }
}

/// Returns the denormal mode to use for inputs and outputs.
inline DenormalMode parseDenormalFPAttribute(StringRef Str) {
  StringRef OutputStr, InputStr;
  std::tie(OutputStr, InputStr) = Str.split(',');

  DenormalMode Mode;
  Mode.Output = parseDenormalFPAttributeComponent(OutputStr);

  // Maintain compatibility with old form of the attribute which only specified
  // one component.
  Mode.Input = InputStr.empty() ? Mode.Output  :
               parseDenormalFPAttributeComponent(InputStr);

  return Mode;
}

void DenormalMode::print(raw_ostream &OS) const {
  OS << denormalModeKindName(Output) << ',' << denormalModeKindName(Input);
}

/// Floating-point class tests, supported by 'is_fpclass' intrinsic. Actual
/// test may be an OR combination of basic tests.
enum FPClassTest : unsigned {
  fcNone = 0,

  fcSNan = 0x0001,
  fcQNan = 0x0002,
  fcNegInf = 0x0004,
  fcNegNormal = 0x0008,
  fcNegSubnormal = 0x0010,
  fcNegZero = 0x0020,
  fcPosZero = 0x0040,
  fcPosSubnormal = 0x0080,
  fcPosNormal = 0x0100,
  fcPosInf = 0x0200,

  fcNan = fcSNan | fcQNan,
  fcInf = fcPosInf | fcNegInf,
  fcNormal = fcPosNormal | fcNegNormal,
  fcSubnormal = fcPosSubnormal | fcNegSubnormal,
  fcZero = fcPosZero | fcNegZero,
  fcPosFinite = fcPosNormal | fcPosSubnormal | fcPosZero,
  fcNegFinite = fcNegNormal | fcNegSubnormal | fcNegZero,
  fcFinite = fcPosFinite | fcNegFinite,
  fcPositive = fcPosFinite | fcPosInf,
  fcNegative = fcNegFinite | fcNegInf,

  fcAllFlags = fcNan | fcInf | fcFinite,
};

LLVM_DECLARE_ENUM_AS_BITMASK(FPClassTest, /* LargestValue */ fcPosInf);

/// Return the test mask which returns true if the value's sign bit is flipped.
FPClassTest fneg(FPClassTest Mask);

/// Return the test mask which returns true after fabs is applied to the value.
FPClassTest inverse_fabs(FPClassTest Mask);

/// Return the test mask which returns true if the value could have the same set
/// of classes, but with a different sign.
FPClassTest unknown_sign(FPClassTest Mask);

/// Write a human readable form of \p Mask to \p OS
raw_ostream &operator<<(raw_ostream &OS, FPClassTest Mask);

} // namespace llvm

#endif // LLVM_ADT_FLOATINGPOINTMODE_H

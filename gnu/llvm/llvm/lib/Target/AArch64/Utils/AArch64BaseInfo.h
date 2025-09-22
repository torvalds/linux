//===-- AArch64BaseInfo.h - Top level definitions for AArch64 ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains small standalone helper functions and enum definitions for
// the AArch64 target useful for the compiler back-end and the MC libraries.
// As such, it deliberately does not include references to LLVM core
// code gen types, passes, etc..
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_UTILS_AARCH64BASEINFO_H
#define LLVM_LIB_TARGET_AARCH64_UTILS_AARCH64BASEINFO_H

// FIXME: Is it easiest to fix this layering violation by moving the .inc
// #includes from AArch64MCTargetDesc.h to here?
#include "MCTargetDesc/AArch64MCTargetDesc.h" // For AArch64::X0 and friends.
#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/TargetParser/SubtargetFeature.h"

namespace llvm {

inline static unsigned getWRegFromXReg(unsigned Reg) {
  switch (Reg) {
  case AArch64::X0: return AArch64::W0;
  case AArch64::X1: return AArch64::W1;
  case AArch64::X2: return AArch64::W2;
  case AArch64::X3: return AArch64::W3;
  case AArch64::X4: return AArch64::W4;
  case AArch64::X5: return AArch64::W5;
  case AArch64::X6: return AArch64::W6;
  case AArch64::X7: return AArch64::W7;
  case AArch64::X8: return AArch64::W8;
  case AArch64::X9: return AArch64::W9;
  case AArch64::X10: return AArch64::W10;
  case AArch64::X11: return AArch64::W11;
  case AArch64::X12: return AArch64::W12;
  case AArch64::X13: return AArch64::W13;
  case AArch64::X14: return AArch64::W14;
  case AArch64::X15: return AArch64::W15;
  case AArch64::X16: return AArch64::W16;
  case AArch64::X17: return AArch64::W17;
  case AArch64::X18: return AArch64::W18;
  case AArch64::X19: return AArch64::W19;
  case AArch64::X20: return AArch64::W20;
  case AArch64::X21: return AArch64::W21;
  case AArch64::X22: return AArch64::W22;
  case AArch64::X23: return AArch64::W23;
  case AArch64::X24: return AArch64::W24;
  case AArch64::X25: return AArch64::W25;
  case AArch64::X26: return AArch64::W26;
  case AArch64::X27: return AArch64::W27;
  case AArch64::X28: return AArch64::W28;
  case AArch64::FP: return AArch64::W29;
  case AArch64::LR: return AArch64::W30;
  case AArch64::SP: return AArch64::WSP;
  case AArch64::XZR: return AArch64::WZR;
  }
  // For anything else, return it unchanged.
  return Reg;
}

inline static unsigned getXRegFromWReg(unsigned Reg) {
  switch (Reg) {
  case AArch64::W0: return AArch64::X0;
  case AArch64::W1: return AArch64::X1;
  case AArch64::W2: return AArch64::X2;
  case AArch64::W3: return AArch64::X3;
  case AArch64::W4: return AArch64::X4;
  case AArch64::W5: return AArch64::X5;
  case AArch64::W6: return AArch64::X6;
  case AArch64::W7: return AArch64::X7;
  case AArch64::W8: return AArch64::X8;
  case AArch64::W9: return AArch64::X9;
  case AArch64::W10: return AArch64::X10;
  case AArch64::W11: return AArch64::X11;
  case AArch64::W12: return AArch64::X12;
  case AArch64::W13: return AArch64::X13;
  case AArch64::W14: return AArch64::X14;
  case AArch64::W15: return AArch64::X15;
  case AArch64::W16: return AArch64::X16;
  case AArch64::W17: return AArch64::X17;
  case AArch64::W18: return AArch64::X18;
  case AArch64::W19: return AArch64::X19;
  case AArch64::W20: return AArch64::X20;
  case AArch64::W21: return AArch64::X21;
  case AArch64::W22: return AArch64::X22;
  case AArch64::W23: return AArch64::X23;
  case AArch64::W24: return AArch64::X24;
  case AArch64::W25: return AArch64::X25;
  case AArch64::W26: return AArch64::X26;
  case AArch64::W27: return AArch64::X27;
  case AArch64::W28: return AArch64::X28;
  case AArch64::W29: return AArch64::FP;
  case AArch64::W30: return AArch64::LR;
  case AArch64::WSP: return AArch64::SP;
  case AArch64::WZR: return AArch64::XZR;
  }
  // For anything else, return it unchanged.
  return Reg;
}

inline static unsigned getXRegFromXRegTuple(unsigned RegTuple) {
  switch (RegTuple) {
  case AArch64::X0_X1_X2_X3_X4_X5_X6_X7: return AArch64::X0;
  case AArch64::X2_X3_X4_X5_X6_X7_X8_X9: return AArch64::X2;
  case AArch64::X4_X5_X6_X7_X8_X9_X10_X11: return AArch64::X4;
  case AArch64::X6_X7_X8_X9_X10_X11_X12_X13: return AArch64::X6;
  case AArch64::X8_X9_X10_X11_X12_X13_X14_X15: return AArch64::X8;
  case AArch64::X10_X11_X12_X13_X14_X15_X16_X17: return AArch64::X10;
  case AArch64::X12_X13_X14_X15_X16_X17_X18_X19: return AArch64::X12;
  case AArch64::X14_X15_X16_X17_X18_X19_X20_X21: return AArch64::X14;
  case AArch64::X16_X17_X18_X19_X20_X21_X22_X23: return AArch64::X16;
  case AArch64::X18_X19_X20_X21_X22_X23_X24_X25: return AArch64::X18;
  case AArch64::X20_X21_X22_X23_X24_X25_X26_X27: return AArch64::X20;
  case AArch64::X22_X23_X24_X25_X26_X27_X28_FP: return AArch64::X22;
  }
  // For anything else, return it unchanged.
  return RegTuple;
}

static inline unsigned getBRegFromDReg(unsigned Reg) {
  switch (Reg) {
  case AArch64::D0:  return AArch64::B0;
  case AArch64::D1:  return AArch64::B1;
  case AArch64::D2:  return AArch64::B2;
  case AArch64::D3:  return AArch64::B3;
  case AArch64::D4:  return AArch64::B4;
  case AArch64::D5:  return AArch64::B5;
  case AArch64::D6:  return AArch64::B6;
  case AArch64::D7:  return AArch64::B7;
  case AArch64::D8:  return AArch64::B8;
  case AArch64::D9:  return AArch64::B9;
  case AArch64::D10: return AArch64::B10;
  case AArch64::D11: return AArch64::B11;
  case AArch64::D12: return AArch64::B12;
  case AArch64::D13: return AArch64::B13;
  case AArch64::D14: return AArch64::B14;
  case AArch64::D15: return AArch64::B15;
  case AArch64::D16: return AArch64::B16;
  case AArch64::D17: return AArch64::B17;
  case AArch64::D18: return AArch64::B18;
  case AArch64::D19: return AArch64::B19;
  case AArch64::D20: return AArch64::B20;
  case AArch64::D21: return AArch64::B21;
  case AArch64::D22: return AArch64::B22;
  case AArch64::D23: return AArch64::B23;
  case AArch64::D24: return AArch64::B24;
  case AArch64::D25: return AArch64::B25;
  case AArch64::D26: return AArch64::B26;
  case AArch64::D27: return AArch64::B27;
  case AArch64::D28: return AArch64::B28;
  case AArch64::D29: return AArch64::B29;
  case AArch64::D30: return AArch64::B30;
  case AArch64::D31: return AArch64::B31;
  }
  // For anything else, return it unchanged.
  return Reg;
}


static inline unsigned getDRegFromBReg(unsigned Reg) {
  switch (Reg) {
  case AArch64::B0:  return AArch64::D0;
  case AArch64::B1:  return AArch64::D1;
  case AArch64::B2:  return AArch64::D2;
  case AArch64::B3:  return AArch64::D3;
  case AArch64::B4:  return AArch64::D4;
  case AArch64::B5:  return AArch64::D5;
  case AArch64::B6:  return AArch64::D6;
  case AArch64::B7:  return AArch64::D7;
  case AArch64::B8:  return AArch64::D8;
  case AArch64::B9:  return AArch64::D9;
  case AArch64::B10: return AArch64::D10;
  case AArch64::B11: return AArch64::D11;
  case AArch64::B12: return AArch64::D12;
  case AArch64::B13: return AArch64::D13;
  case AArch64::B14: return AArch64::D14;
  case AArch64::B15: return AArch64::D15;
  case AArch64::B16: return AArch64::D16;
  case AArch64::B17: return AArch64::D17;
  case AArch64::B18: return AArch64::D18;
  case AArch64::B19: return AArch64::D19;
  case AArch64::B20: return AArch64::D20;
  case AArch64::B21: return AArch64::D21;
  case AArch64::B22: return AArch64::D22;
  case AArch64::B23: return AArch64::D23;
  case AArch64::B24: return AArch64::D24;
  case AArch64::B25: return AArch64::D25;
  case AArch64::B26: return AArch64::D26;
  case AArch64::B27: return AArch64::D27;
  case AArch64::B28: return AArch64::D28;
  case AArch64::B29: return AArch64::D29;
  case AArch64::B30: return AArch64::D30;
  case AArch64::B31: return AArch64::D31;
  }
  // For anything else, return it unchanged.
  return Reg;
}

static inline bool atomicBarrierDroppedOnZero(unsigned Opcode) {
  switch (Opcode) {
  case AArch64::LDADDAB:   case AArch64::LDADDAH:
  case AArch64::LDADDAW:   case AArch64::LDADDAX:
  case AArch64::LDADDALB:  case AArch64::LDADDALH:
  case AArch64::LDADDALW:  case AArch64::LDADDALX:
  case AArch64::LDCLRAB:   case AArch64::LDCLRAH:
  case AArch64::LDCLRAW:   case AArch64::LDCLRAX:
  case AArch64::LDCLRALB:  case AArch64::LDCLRALH:
  case AArch64::LDCLRALW:  case AArch64::LDCLRALX:
  case AArch64::LDEORAB:   case AArch64::LDEORAH:
  case AArch64::LDEORAW:   case AArch64::LDEORAX:
  case AArch64::LDEORALB:  case AArch64::LDEORALH:
  case AArch64::LDEORALW:  case AArch64::LDEORALX:
  case AArch64::LDSETAB:   case AArch64::LDSETAH:
  case AArch64::LDSETAW:   case AArch64::LDSETAX:
  case AArch64::LDSETALB:  case AArch64::LDSETALH:
  case AArch64::LDSETALW:  case AArch64::LDSETALX:
  case AArch64::LDSMAXAB:  case AArch64::LDSMAXAH:
  case AArch64::LDSMAXAW:  case AArch64::LDSMAXAX:
  case AArch64::LDSMAXALB: case AArch64::LDSMAXALH:
  case AArch64::LDSMAXALW: case AArch64::LDSMAXALX:
  case AArch64::LDSMINAB:  case AArch64::LDSMINAH:
  case AArch64::LDSMINAW:  case AArch64::LDSMINAX:
  case AArch64::LDSMINALB: case AArch64::LDSMINALH:
  case AArch64::LDSMINALW: case AArch64::LDSMINALX:
  case AArch64::LDUMAXAB:  case AArch64::LDUMAXAH:
  case AArch64::LDUMAXAW:  case AArch64::LDUMAXAX:
  case AArch64::LDUMAXALB: case AArch64::LDUMAXALH:
  case AArch64::LDUMAXALW: case AArch64::LDUMAXALX:
  case AArch64::LDUMINAB:  case AArch64::LDUMINAH:
  case AArch64::LDUMINAW:  case AArch64::LDUMINAX:
  case AArch64::LDUMINALB: case AArch64::LDUMINALH:
  case AArch64::LDUMINALW: case AArch64::LDUMINALX:
  case AArch64::SWPAB:     case AArch64::SWPAH:
  case AArch64::SWPAW:     case AArch64::SWPAX:
  case AArch64::SWPALB:    case AArch64::SWPALH:
  case AArch64::SWPALW:    case AArch64::SWPALX:
    return true;
  }
  return false;
}

namespace AArch64CC {

// The CondCodes constants map directly to the 4-bit encoding of the condition
// field for predicated instructions.
enum CondCode {  // Meaning (integer)          Meaning (floating-point)
  EQ = 0x0,      // Equal                      Equal
  NE = 0x1,      // Not equal                  Not equal, or unordered
  HS = 0x2,      // Unsigned higher or same    >, ==, or unordered
  LO = 0x3,      // Unsigned lower             Less than
  MI = 0x4,      // Minus, negative            Less than
  PL = 0x5,      // Plus, positive or zero     >, ==, or unordered
  VS = 0x6,      // Overflow                   Unordered
  VC = 0x7,      // No overflow                Not unordered
  HI = 0x8,      // Unsigned higher            Greater than, or unordered
  LS = 0x9,      // Unsigned lower or same     Less than or equal
  GE = 0xa,      // Greater than or equal      Greater than or equal
  LT = 0xb,      // Less than                  Less than, or unordered
  GT = 0xc,      // Greater than               Greater than
  LE = 0xd,      // Less than or equal         <, ==, or unordered
  AL = 0xe,      // Always (unconditional)     Always (unconditional)
  NV = 0xf,      // Always (unconditional)     Always (unconditional)
  // Note the NV exists purely to disassemble 0b1111. Execution is "always".
  Invalid,

  // Common aliases used for SVE.
  ANY_ACTIVE   = NE, // (!Z)
  FIRST_ACTIVE = MI, // ( N)
  LAST_ACTIVE  = LO, // (!C)
  NONE_ACTIVE  = EQ  // ( Z)
};

inline static const char *getCondCodeName(CondCode Code) {
  switch (Code) {
  default: llvm_unreachable("Unknown condition code");
  case EQ:  return "eq";
  case NE:  return "ne";
  case HS:  return "hs";
  case LO:  return "lo";
  case MI:  return "mi";
  case PL:  return "pl";
  case VS:  return "vs";
  case VC:  return "vc";
  case HI:  return "hi";
  case LS:  return "ls";
  case GE:  return "ge";
  case LT:  return "lt";
  case GT:  return "gt";
  case LE:  return "le";
  case AL:  return "al";
  case NV:  return "nv";
  }
}

inline static CondCode getInvertedCondCode(CondCode Code) {
  // To reverse a condition it's necessary to only invert the low bit:

  return static_cast<CondCode>(static_cast<unsigned>(Code) ^ 0x1);
}

/// Given a condition code, return NZCV flags that would satisfy that condition.
/// The flag bits are in the format expected by the ccmp instructions.
/// Note that many different flag settings can satisfy a given condition code,
/// this function just returns one of them.
inline static unsigned getNZCVToSatisfyCondCode(CondCode Code) {
  // NZCV flags encoded as expected by ccmp instructions, ARMv8 ISA 5.5.7.
  enum { N = 8, Z = 4, C = 2, V = 1 };
  switch (Code) {
  default: llvm_unreachable("Unknown condition code");
  case EQ: return Z; // Z == 1
  case NE: return 0; // Z == 0
  case HS: return C; // C == 1
  case LO: return 0; // C == 0
  case MI: return N; // N == 1
  case PL: return 0; // N == 0
  case VS: return V; // V == 1
  case VC: return 0; // V == 0
  case HI: return C; // C == 1 && Z == 0
  case LS: return 0; // C == 0 || Z == 1
  case GE: return 0; // N == V
  case LT: return N; // N != V
  case GT: return 0; // Z == 0 && N == V
  case LE: return Z; // Z == 1 || N != V
  }
}

} // end namespace AArch64CC

struct SysAlias {
  const char *Name;
  uint16_t Encoding;
  FeatureBitset FeaturesRequired;

  constexpr SysAlias(const char *N, uint16_t E) : Name(N), Encoding(E) {}
  constexpr SysAlias(const char *N, uint16_t E, FeatureBitset F)
      : Name(N), Encoding(E), FeaturesRequired(F) {}

  bool haveFeatures(FeatureBitset ActiveFeatures) const {
    return ActiveFeatures[llvm::AArch64::FeatureAll] ||
           (FeaturesRequired & ActiveFeatures) == FeaturesRequired;
  }

  FeatureBitset getRequiredFeatures() const { return FeaturesRequired; }
};

struct SysAliasReg : SysAlias {
  bool NeedsReg;
  constexpr SysAliasReg(const char *N, uint16_t E, bool R)
      : SysAlias(N, E), NeedsReg(R) {}
  constexpr SysAliasReg(const char *N, uint16_t E, bool R, FeatureBitset F)
      : SysAlias(N, E, F), NeedsReg(R) {}
};

struct SysAliasImm : SysAlias {
  uint16_t ImmValue;
  constexpr SysAliasImm(const char *N, uint16_t E, uint16_t I)
      : SysAlias(N, E), ImmValue(I) {}
  constexpr SysAliasImm(const char *N, uint16_t E, uint16_t I, FeatureBitset F)
      : SysAlias(N, E, F), ImmValue(I) {}
};

namespace AArch64SVCR {
  struct SVCR : SysAlias{
    using SysAlias::SysAlias;
  };
  #define GET_SVCR_DECL
  #include "AArch64GenSystemOperands.inc"
}

namespace AArch64AT{
  struct AT : SysAlias {
    using SysAlias::SysAlias;
  };
  #define GET_AT_DECL
  #include "AArch64GenSystemOperands.inc"
}

namespace AArch64DB {
  struct DB : SysAlias {
    using SysAlias::SysAlias;
  };
  #define GET_DB_DECL
  #include "AArch64GenSystemOperands.inc"
}

namespace AArch64DBnXS {
  struct DBnXS : SysAliasImm {
    using SysAliasImm::SysAliasImm;
  };
  #define GET_DBNXS_DECL
  #include "AArch64GenSystemOperands.inc"
}

namespace  AArch64DC {
  struct DC : SysAlias {
    using SysAlias::SysAlias;
  };
  #define GET_DC_DECL
  #include "AArch64GenSystemOperands.inc"
}

namespace  AArch64IC {
  struct IC : SysAliasReg {
    using SysAliasReg::SysAliasReg;
  };
  #define GET_IC_DECL
  #include "AArch64GenSystemOperands.inc"
}

namespace  AArch64ISB {
  struct ISB : SysAlias {
    using SysAlias::SysAlias;
  };
  #define GET_ISB_DECL
  #include "AArch64GenSystemOperands.inc"
}

namespace  AArch64TSB {
  struct TSB : SysAlias {
    using SysAlias::SysAlias;
  };
  #define GET_TSB_DECL
  #include "AArch64GenSystemOperands.inc"
}

namespace AArch64PRFM {
  struct PRFM : SysAlias {
    using SysAlias::SysAlias;
  };
  #define GET_PRFM_DECL
  #include "AArch64GenSystemOperands.inc"
}

namespace AArch64SVEPRFM {
  struct SVEPRFM : SysAlias {
    using SysAlias::SysAlias;
  };
#define GET_SVEPRFM_DECL
#include "AArch64GenSystemOperands.inc"
}

namespace AArch64RPRFM {
struct RPRFM : SysAlias {
  using SysAlias::SysAlias;
};
#define GET_RPRFM_DECL
#include "AArch64GenSystemOperands.inc"
} // namespace AArch64RPRFM

namespace AArch64SVEPredPattern {
  struct SVEPREDPAT {
    const char *Name;
    uint16_t Encoding;
  };
#define GET_SVEPREDPAT_DECL
#include "AArch64GenSystemOperands.inc"
}

namespace AArch64SVEVecLenSpecifier {
  struct SVEVECLENSPECIFIER {
    const char *Name;
    uint16_t Encoding;
  };
#define GET_SVEVECLENSPECIFIER_DECL
#include "AArch64GenSystemOperands.inc"
} // namespace AArch64SVEVecLenSpecifier

/// Return the number of active elements for VL1 to VL256 predicate pattern,
/// zero for all other patterns.
inline unsigned getNumElementsFromSVEPredPattern(unsigned Pattern) {
  switch (Pattern) {
  default:
    return 0;
  case AArch64SVEPredPattern::vl1:
  case AArch64SVEPredPattern::vl2:
  case AArch64SVEPredPattern::vl3:
  case AArch64SVEPredPattern::vl4:
  case AArch64SVEPredPattern::vl5:
  case AArch64SVEPredPattern::vl6:
  case AArch64SVEPredPattern::vl7:
  case AArch64SVEPredPattern::vl8:
    return Pattern;
  case AArch64SVEPredPattern::vl16:
    return 16;
  case AArch64SVEPredPattern::vl32:
    return 32;
  case AArch64SVEPredPattern::vl64:
    return 64;
  case AArch64SVEPredPattern::vl128:
    return 128;
  case AArch64SVEPredPattern::vl256:
    return 256;
  }
}

/// Return specific VL predicate pattern based on the number of elements.
inline std::optional<unsigned>
getSVEPredPatternFromNumElements(unsigned MinNumElts) {
  switch (MinNumElts) {
  default:
    return std::nullopt;
  case 1:
  case 2:
  case 3:
  case 4:
  case 5:
  case 6:
  case 7:
  case 8:
    return MinNumElts;
  case 16:
    return AArch64SVEPredPattern::vl16;
  case 32:
    return AArch64SVEPredPattern::vl32;
  case 64:
    return AArch64SVEPredPattern::vl64;
  case 128:
    return AArch64SVEPredPattern::vl128;
  case 256:
    return AArch64SVEPredPattern::vl256;
  }
}

/// An enum to describe what types of loops we should attempt to tail-fold:
///   Disabled:    None
///   Reductions:  Loops containing reductions
///   Recurrences: Loops with first-order recurrences, i.e. that would
///                  require a SVE splice instruction
///   Reverse:     Reverse loops
///   Simple:      Loops that are not reversed and don't contain reductions
///                  or first-order recurrences.
///   All:         All
enum class TailFoldingOpts : uint8_t {
  Disabled = 0x00,
  Simple = 0x01,
  Reductions = 0x02,
  Recurrences = 0x04,
  Reverse = 0x08,
  All = Reductions | Recurrences | Simple | Reverse
};

LLVM_DECLARE_ENUM_AS_BITMASK(TailFoldingOpts,
                             /* LargestValue */ (long)TailFoldingOpts::Reverse);

namespace AArch64ExactFPImm {
  struct ExactFPImm {
    const char *Name;
    int Enum;
    const char *Repr;
  };
#define GET_EXACTFPIMM_DECL
#include "AArch64GenSystemOperands.inc"
}

namespace AArch64PState {
  struct PStateImm0_15 : SysAlias{
    using SysAlias::SysAlias;
  };
  #define GET_PSTATEIMM0_15_DECL
  #include "AArch64GenSystemOperands.inc"

  struct PStateImm0_1 : SysAlias{
    using SysAlias::SysAlias;
  };
  #define GET_PSTATEIMM0_1_DECL
  #include "AArch64GenSystemOperands.inc"
}

namespace AArch64PSBHint {
  struct PSB : SysAlias {
    using SysAlias::SysAlias;
  };
  #define GET_PSB_DECL
  #include "AArch64GenSystemOperands.inc"
}

namespace AArch64BTIHint {
  struct BTI : SysAlias {
    using SysAlias::SysAlias;
  };
  #define GET_BTI_DECL
  #include "AArch64GenSystemOperands.inc"
}

namespace AArch64SME {
enum ToggleCondition : unsigned {
  Always,
  IfCallerIsStreaming,
  IfCallerIsNonStreaming
};
}

namespace AArch64SE {
    enum ShiftExtSpecifiers {
        Invalid = -1,
        LSL,
        MSL,
        LSR,
        ASR,
        ROR,

        UXTB,
        UXTH,
        UXTW,
        UXTX,

        SXTB,
        SXTH,
        SXTW,
        SXTX
    };
}

namespace AArch64Layout {
    enum VectorLayout {
        Invalid = -1,
        VL_8B,
        VL_4H,
        VL_2S,
        VL_1D,

        VL_16B,
        VL_8H,
        VL_4S,
        VL_2D,

        // Bare layout for the 128-bit vector
        // (only show ".b", ".h", ".s", ".d" without vector number)
        VL_B,
        VL_H,
        VL_S,
        VL_D
    };
}

inline static const char *
AArch64VectorLayoutToString(AArch64Layout::VectorLayout Layout) {
  switch (Layout) {
  case AArch64Layout::VL_8B:  return ".8b";
  case AArch64Layout::VL_4H:  return ".4h";
  case AArch64Layout::VL_2S:  return ".2s";
  case AArch64Layout::VL_1D:  return ".1d";
  case AArch64Layout::VL_16B:  return ".16b";
  case AArch64Layout::VL_8H:  return ".8h";
  case AArch64Layout::VL_4S:  return ".4s";
  case AArch64Layout::VL_2D:  return ".2d";
  case AArch64Layout::VL_B:  return ".b";
  case AArch64Layout::VL_H:  return ".h";
  case AArch64Layout::VL_S:  return ".s";
  case AArch64Layout::VL_D:  return ".d";
  default: llvm_unreachable("Unknown Vector Layout");
  }
}

inline static AArch64Layout::VectorLayout
AArch64StringToVectorLayout(StringRef LayoutStr) {
  return StringSwitch<AArch64Layout::VectorLayout>(LayoutStr)
             .Case(".8b", AArch64Layout::VL_8B)
             .Case(".4h", AArch64Layout::VL_4H)
             .Case(".2s", AArch64Layout::VL_2S)
             .Case(".1d", AArch64Layout::VL_1D)
             .Case(".16b", AArch64Layout::VL_16B)
             .Case(".8h", AArch64Layout::VL_8H)
             .Case(".4s", AArch64Layout::VL_4S)
             .Case(".2d", AArch64Layout::VL_2D)
             .Case(".b", AArch64Layout::VL_B)
             .Case(".h", AArch64Layout::VL_H)
             .Case(".s", AArch64Layout::VL_S)
             .Case(".d", AArch64Layout::VL_D)
             .Default(AArch64Layout::Invalid);
}

namespace AArch64SysReg {
  struct SysReg {
    const char *Name;
    const char *AltName;
    unsigned Encoding;
    bool Readable;
    bool Writeable;
    FeatureBitset FeaturesRequired;

    bool haveFeatures(FeatureBitset ActiveFeatures) const {
      return ActiveFeatures[llvm::AArch64::FeatureAll] ||
             (FeaturesRequired & ActiveFeatures) == FeaturesRequired;
    }
  };

  #define GET_SYSREG_DECL
  #include "AArch64GenSystemOperands.inc"

  const SysReg *lookupSysRegByName(StringRef);
  const SysReg *lookupSysRegByEncoding(uint16_t);

  uint32_t parseGenericRegister(StringRef Name);
  std::string genericRegisterString(uint32_t Bits);
}

namespace AArch64TLBI {
  struct TLBI : SysAliasReg {
    using SysAliasReg::SysAliasReg;
  };
  #define GET_TLBITable_DECL
  #include "AArch64GenSystemOperands.inc"
}

namespace AArch64PRCTX {
  struct PRCTX : SysAliasReg {
    using SysAliasReg::SysAliasReg;
  };
  #define GET_PRCTX_DECL
  #include "AArch64GenSystemOperands.inc"
}

namespace AArch64II {
/// Target Operand Flag enum.
enum TOF {
  //===------------------------------------------------------------------===//
  // AArch64 Specific MachineOperand flags.

  MO_NO_FLAG,

  MO_FRAGMENT = 0x7,

  /// MO_PAGE - A symbol operand with this flag represents the pc-relative
  /// offset of the 4K page containing the symbol.  This is used with the
  /// ADRP instruction.
  MO_PAGE = 1,

  /// MO_PAGEOFF - A symbol operand with this flag represents the offset of
  /// that symbol within a 4K page.  This offset is added to the page address
  /// to produce the complete address.
  MO_PAGEOFF = 2,

  /// MO_G3 - A symbol operand with this flag (granule 3) represents the high
  /// 16-bits of a 64-bit address, used in a MOVZ or MOVK instruction
  MO_G3 = 3,

  /// MO_G2 - A symbol operand with this flag (granule 2) represents the bits
  /// 32-47 of a 64-bit address, used in a MOVZ or MOVK instruction
  MO_G2 = 4,

  /// MO_G1 - A symbol operand with this flag (granule 1) represents the bits
  /// 16-31 of a 64-bit address, used in a MOVZ or MOVK instruction
  MO_G1 = 5,

  /// MO_G0 - A symbol operand with this flag (granule 0) represents the bits
  /// 0-15 of a 64-bit address, used in a MOVZ or MOVK instruction
  MO_G0 = 6,

  /// MO_HI12 - This flag indicates that a symbol operand represents the bits
  /// 13-24 of a 64-bit address, used in a arithmetic immediate-shifted-left-
  /// by-12-bits instruction.
  MO_HI12 = 7,

  /// MO_COFFSTUB - On a symbol operand "FOO", this indicates that the
  /// reference is actually to the ".refptr.FOO" symbol.  This is used for
  /// stub symbols on windows.
  MO_COFFSTUB = 0x8,

  /// MO_GOT - This flag indicates that a symbol operand represents the
  /// address of the GOT entry for the symbol, rather than the address of
  /// the symbol itself.
  MO_GOT = 0x10,

  /// MO_NC - Indicates whether the linker is expected to check the symbol
  /// reference for overflow. For example in an ADRP/ADD pair of relocations
  /// the ADRP usually does check, but not the ADD.
  MO_NC = 0x20,

  /// MO_TLS - Indicates that the operand being accessed is some kind of
  /// thread-local symbol. On Darwin, only one type of thread-local access
  /// exists (pre linker-relaxation), but on ELF the TLSModel used for the
  /// referee will affect interpretation.
  MO_TLS = 0x40,

  /// MO_DLLIMPORT - On a symbol operand, this represents that the reference
  /// to the symbol is for an import stub.  This is used for DLL import
  /// storage class indication on Windows.
  MO_DLLIMPORT = 0x80,

  /// MO_S - Indicates that the bits of the symbol operand represented by
  /// MO_G0 etc are signed.
  MO_S = 0x100,

  /// MO_PREL - Indicates that the bits of the symbol operand represented by
  /// MO_G0 etc are PC relative.
  MO_PREL = 0x200,

  /// MO_TAGGED - With MO_PAGE, indicates that the page includes a memory tag
  /// in bits 56-63.
  /// On a FrameIndex operand, indicates that the underlying memory is tagged
  /// with an unknown tag value (MTE); this needs to be lowered either to an
  /// SP-relative load or store instruction (which do not check tags), or to
  /// an LDG instruction to obtain the tag value.
  MO_TAGGED = 0x400,

  /// MO_ARM64EC_CALLMANGLE - Operand refers to the Arm64EC-mangled version
  /// of a symbol, not the original. For dllimport symbols, this means it
  /// uses "__imp_aux".  For other symbols, this means it uses the mangled
  /// ("#" prefix for C) name.
  MO_ARM64EC_CALLMANGLE = 0x800,
};
} // end namespace AArch64II

//===----------------------------------------------------------------------===//
// v8.3a Pointer Authentication
//

namespace AArch64PACKey {
enum ID : uint8_t {
  IA = 0,
  IB = 1,
  DA = 2,
  DB = 3,
  LAST = DB
};
} // namespace AArch64PACKey

/// Return 2-letter identifier string for numeric key ID.
inline static StringRef AArch64PACKeyIDToString(AArch64PACKey::ID KeyID) {
  switch (KeyID) {
  case AArch64PACKey::IA:
    return StringRef("ia");
  case AArch64PACKey::IB:
    return StringRef("ib");
  case AArch64PACKey::DA:
    return StringRef("da");
  case AArch64PACKey::DB:
    return StringRef("db");
  }
  llvm_unreachable("Unhandled AArch64PACKey::ID enum");
}

/// Return numeric key ID for 2-letter identifier string.
inline static std::optional<AArch64PACKey::ID>
AArch64StringToPACKeyID(StringRef Name) {
  if (Name == "ia")
    return AArch64PACKey::IA;
  if (Name == "ib")
    return AArch64PACKey::IB;
  if (Name == "da")
    return AArch64PACKey::DA;
  if (Name == "db")
    return AArch64PACKey::DB;
  return std::nullopt;
}

namespace AArch64 {
// The number of bits in a SVE register is architecturally defined
// to be a multiple of this value.  If <M x t> has this number of bits,
// a <n x M x t> vector can be stored in a SVE register without any
// redundant bits.  If <M x t> has this number of bits divided by P,
// a <n x M x t> vector is stored in a SVE register by placing index i
// in index i*P of a <n x (M*P) x t> vector.  The other elements of the
// <n x (M*P) x t> vector (such as index 1) are undefined.
static constexpr unsigned SVEBitsPerBlock = 128;
static constexpr unsigned SVEMaxBitsPerVector = 2048;
} // end namespace AArch64
} // end namespace llvm

#endif

//===- AArch64GlobalISelUtils.h ----------------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file APIs for AArch64-specific helper functions used in the GlobalISel
/// pipeline.
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_GISEL_AARCH64GLOBALISELUTILS_H
#define LLVM_LIB_TARGET_AARCH64_GISEL_AARCH64GLOBALISELUTILS_H
#include "MCTargetDesc/AArch64AddressingModes.h"
#include "Utils/AArch64BaseInfo.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/IR/InstrTypes.h"
#include <cstdint>

namespace llvm {

namespace AArch64GISelUtils {

/// \returns true if \p C is a legal immediate operand for an arithmetic
/// instruction.
constexpr bool isLegalArithImmed(const uint64_t C) {
  return (C >> 12 == 0) || ((C & 0xFFFULL) == 0 && C >> 24 == 0);
}

/// \returns A value when \p MI is a vector splat of a Register or constant.
/// Checks for generic opcodes and AArch64-specific generic opcodes.
std::optional<RegOrConstant>
getAArch64VectorSplat(const MachineInstr &MI, const MachineRegisterInfo &MRI);

/// \returns A value when \p MI is a constant vector splat.
/// Checks for generic opcodes and AArch64-specific generic opcodes.
std::optional<int64_t>
getAArch64VectorSplatScalar(const MachineInstr &MI,
                            const MachineRegisterInfo &MRI);

/// \returns true if \p MaybeSub and \p Pred are part of a CMN tree for an
/// integer compare.
bool isCMN(const MachineInstr *MaybeSub, const CmpInst::Predicate &Pred,
           const MachineRegisterInfo &MRI);

/// Replace a G_MEMSET with a value of 0 with a G_BZERO instruction if it is
/// supported and beneficial to do so.
///
/// \note This only applies on Darwin.
///
/// \returns true if \p MI was replaced with a G_BZERO.
bool tryEmitBZero(MachineInstr &MI, MachineIRBuilder &MIRBuilder, bool MinSize);

/// Analyze a ptrauth discriminator value to try to find the constant integer
/// and address parts, cracking a ptrauth_blend intrinsic if there is one.
/// \returns integer/address disc. parts, with NoRegister if no address disc.
std::tuple<uint16_t, Register>
extractPtrauthBlendDiscriminators(Register Disc, MachineRegisterInfo &MRI);

/// Find the AArch64 condition codes necessary to represent \p P for a scalar
/// floating point comparison.
///
/// \param [out] CondCode is the first condition code.
/// \param [out] CondCode2 is the second condition code if necessary.
/// AArch64CC::AL otherwise.
void changeFCMPPredToAArch64CC(const CmpInst::Predicate P,
                               AArch64CC::CondCode &CondCode,
                               AArch64CC::CondCode &CondCode2);

/// Find the AArch64 condition codes necessary to represent \p P for a vector
/// floating point comparison.
///
/// \param [out] CondCode - The first condition code.
/// \param [out] CondCode2 - The second condition code if necessary.
/// AArch64CC::AL otherwise.
/// \param [out] Invert - True if the comparison must be inverted with a NOT.
void changeVectorFCMPPredToAArch64CC(const CmpInst::Predicate P,
                                     AArch64CC::CondCode &CondCode,
                                     AArch64CC::CondCode &CondCode2,
                                     bool &Invert);

} // namespace AArch64GISelUtils
} // namespace llvm

#endif

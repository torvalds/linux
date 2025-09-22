//===-- SystemZMCFixups.h - SystemZ-specific fixup entries ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SYSTEMZ_MCTARGETDESC_SYSTEMZMCFIXUPS_H
#define LLVM_LIB_TARGET_SYSTEMZ_MCTARGETDESC_SYSTEMZMCFIXUPS_H

#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCFixupKindInfo.h"

namespace llvm {
namespace SystemZ {
enum FixupKind {
  // These correspond directly to R_390_* relocations.
  FK_390_PC12DBL = FirstTargetFixupKind,
  FK_390_PC16DBL,
  FK_390_PC24DBL,
  FK_390_PC32DBL,
  FK_390_TLS_CALL,

  FK_390_S8Imm,
  FK_390_S16Imm,
  FK_390_S20Imm,
  FK_390_S32Imm,
  FK_390_U1Imm,
  FK_390_U2Imm,
  FK_390_U3Imm,
  FK_390_U4Imm,
  FK_390_U8Imm,
  FK_390_U12Imm,
  FK_390_U16Imm,
  FK_390_U32Imm,
  FK_390_U48Imm,

  // Marker
  LastTargetFixupKind,
  NumTargetFixupKinds = LastTargetFixupKind - FirstTargetFixupKind
};

const static MCFixupKindInfo MCFixupKindInfos[SystemZ::NumTargetFixupKinds] = {
    {"FK_390_PC12DBL", 4, 12, MCFixupKindInfo::FKF_IsPCRel},
    {"FK_390_PC16DBL", 0, 16, MCFixupKindInfo::FKF_IsPCRel},
    {"FK_390_PC24DBL", 0, 24, MCFixupKindInfo::FKF_IsPCRel},
    {"FK_390_PC32DBL", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
    {"FK_390_TLS_CALL", 0, 0, 0},
    {"FK_390_S8Imm", 0, 8, 0},
    {"FK_390_S16Imm", 0, 16, 0},
    {"FK_390_S20Imm", 4, 20, 0},
    {"FK_390_S32Imm", 0, 32, 0},
    {"FK_390_U1Imm", 0, 1, 0},
    {"FK_390_U2Imm", 0, 2, 0},
    {"FK_390_U3Imm", 0, 3, 0},
    {"FK_390_U4Imm", 0, 4, 0},
    {"FK_390_U8Imm", 0, 8, 0},
    {"FK_390_U12Imm", 4, 12, 0},
    {"FK_390_U16Imm", 0, 16, 0},
    {"FK_390_U32Imm", 0, 32, 0},
    {"FK_390_U48Imm", 0, 48, 0},
};
} // end namespace SystemZ
} // end namespace llvm

#endif

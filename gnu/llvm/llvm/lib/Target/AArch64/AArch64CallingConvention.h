//=== AArch64CallingConvention.h - AArch64 CC entry points ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the entry points for AArch64 calling convention analysis.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_AARCH64CALLINGCONVENTION_H
#define LLVM_LIB_TARGET_AARCH64_AARCH64CALLINGCONVENTION_H

#include "llvm/CodeGen/CallingConvLower.h"

namespace llvm {
bool CC_AArch64_AAPCS(unsigned ValNo, MVT ValVT, MVT LocVT,
                      CCValAssign::LocInfo LocInfo, ISD::ArgFlagsTy ArgFlags,
                      CCState &State);
bool CC_AArch64_Arm64EC_VarArg(unsigned ValNo, MVT ValVT, MVT LocVT,
                               CCValAssign::LocInfo LocInfo,
                               ISD::ArgFlagsTy ArgFlags, CCState &State);
bool CC_AArch64_Arm64EC_Thunk(unsigned ValNo, MVT ValVT, MVT LocVT,
                              CCValAssign::LocInfo LocInfo,
                              ISD::ArgFlagsTy ArgFlags, CCState &State);
bool CC_AArch64_Arm64EC_Thunk_Native(unsigned ValNo, MVT ValVT, MVT LocVT,
                                     CCValAssign::LocInfo LocInfo,
                                     ISD::ArgFlagsTy ArgFlags, CCState &State);
bool CC_AArch64_DarwinPCS_VarArg(unsigned ValNo, MVT ValVT, MVT LocVT,
                                 CCValAssign::LocInfo LocInfo,
                                 ISD::ArgFlagsTy ArgFlags, CCState &State);
bool CC_AArch64_DarwinPCS(unsigned ValNo, MVT ValVT, MVT LocVT,
                          CCValAssign::LocInfo LocInfo,
                          ISD::ArgFlagsTy ArgFlags, CCState &State);
bool CC_AArch64_DarwinPCS_ILP32_VarArg(unsigned ValNo, MVT ValVT, MVT LocVT,
                          CCValAssign::LocInfo LocInfo,
                          ISD::ArgFlagsTy ArgFlags, CCState &State);
bool CC_AArch64_Win64PCS(unsigned ValNo, MVT ValVT, MVT LocVT,
                         CCValAssign::LocInfo LocInfo, ISD::ArgFlagsTy ArgFlags,
                         CCState &State);
bool CC_AArch64_Win64_VarArg(unsigned ValNo, MVT ValVT, MVT LocVT,
                             CCValAssign::LocInfo LocInfo,
                             ISD::ArgFlagsTy ArgFlags, CCState &State);
bool CC_AArch64_Win64_CFGuard_Check(unsigned ValNo, MVT ValVT, MVT LocVT,
                                    CCValAssign::LocInfo LocInfo,
                                    ISD::ArgFlagsTy ArgFlags, CCState &State);
bool CC_AArch64_Arm64EC_CFGuard_Check(unsigned ValNo, MVT ValVT, MVT LocVT,
                                      CCValAssign::LocInfo LocInfo,
                                      ISD::ArgFlagsTy ArgFlags, CCState &State);
bool CC_AArch64_GHC(unsigned ValNo, MVT ValVT, MVT LocVT,
                    CCValAssign::LocInfo LocInfo, ISD::ArgFlagsTy ArgFlags,
                    CCState &State);
bool CC_AArch64_Preserve_None(unsigned ValNo, MVT ValVT, MVT LocVT,
                              CCValAssign::LocInfo LocInfo,
                              ISD::ArgFlagsTy ArgFlags, CCState &State);
bool RetCC_AArch64_AAPCS(unsigned ValNo, MVT ValVT, MVT LocVT,
                         CCValAssign::LocInfo LocInfo, ISD::ArgFlagsTy ArgFlags,
                         CCState &State);
bool RetCC_AArch64_Arm64EC_Thunk(unsigned ValNo, MVT ValVT, MVT LocVT,
                                 CCValAssign::LocInfo LocInfo,
                                 ISD::ArgFlagsTy ArgFlags, CCState &State);
bool RetCC_AArch64_Arm64EC_CFGuard_Check(unsigned ValNo, MVT ValVT, MVT LocVT,
                                         CCValAssign::LocInfo LocInfo,
                                         ISD::ArgFlagsTy ArgFlags,
                                         CCState &State);
} // namespace llvm

#endif

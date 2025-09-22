//= LoongArchBaseInfo.cpp - Top level definitions for LoongArch MC -*- C++ -*-//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements helper functions for the LoongArch target useful for the
// compiler back-end and the MC libraries.
//
//===----------------------------------------------------------------------===//

#include "LoongArchBaseInfo.h"
#include "LoongArchMCTargetDesc.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"

namespace llvm {

namespace LoongArchABI {

// Check if ABI has been standardized; issue a warning if it hasn't.
// FIXME: Once all ABIs are standardized, this will be removed.
static ABI checkABIStandardized(ABI Abi) {
  StringRef ABIName;
  switch (Abi) {
  case ABI_ILP32S:
    ABIName = "ilp32s";
    break;
  case ABI_ILP32F:
    ABIName = "ilp32f";
    break;
  case ABI_ILP32D:
    ABIName = "ilp32d";
    break;
  case ABI_LP64F:
    ABIName = "lp64f";
    break;
  case ABI_LP64S:
  case ABI_LP64D:
    return Abi;
  default:
    llvm_unreachable("");
  }
  errs() << "warning: '" << ABIName << "' has not been standardized\n";
  return Abi;
}

static ABI getTripleABI(const Triple &TT) {
  bool Is64Bit = TT.isArch64Bit();
  ABI TripleABI;
  switch (TT.getEnvironment()) {
  case llvm::Triple::EnvironmentType::GNUSF:
    TripleABI = Is64Bit ? ABI_LP64S : ABI_ILP32S;
    break;
  case llvm::Triple::EnvironmentType::GNUF32:
    TripleABI = Is64Bit ? ABI_LP64F : ABI_ILP32F;
    break;
  // Let the fallback case behave like {ILP32,LP64}D.
  case llvm::Triple::EnvironmentType::GNUF64:
  default:
    TripleABI = Is64Bit ? ABI_LP64D : ABI_ILP32D;
    break;
  }
  return TripleABI;
}

ABI computeTargetABI(const Triple &TT, const FeatureBitset &FeatureBits,
                     StringRef ABIName) {
  bool Is64Bit = TT.isArch64Bit();
  ABI ArgProvidedABI = getTargetABI(ABIName);
  ABI TripleABI = getTripleABI(TT);

  auto IsABIValidForFeature = [=](ABI Abi) {
    switch (Abi) {
    default:
      return false;
    case ABI_ILP32S:
      return !Is64Bit;
    case ABI_ILP32F:
      return !Is64Bit && FeatureBits[LoongArch::FeatureBasicF];
    case ABI_ILP32D:
      return !Is64Bit && FeatureBits[LoongArch::FeatureBasicD];
    case ABI_LP64S:
      return Is64Bit;
    case ABI_LP64F:
      return Is64Bit && FeatureBits[LoongArch::FeatureBasicF];
    case ABI_LP64D:
      return Is64Bit && FeatureBits[LoongArch::FeatureBasicD];
    }
  };

  // 1. If the '-target-abi' is valid, use it.
  if (IsABIValidForFeature(ArgProvidedABI)) {
    if (TT.hasEnvironment() && ArgProvidedABI != TripleABI)
      errs()
          << "warning: triple-implied ABI conflicts with provided target-abi '"
          << ABIName << "', using target-abi\n";
    return checkABIStandardized(ArgProvidedABI);
  }

  // 2. If the triple-implied ABI is valid, use it.
  if (IsABIValidForFeature(TripleABI)) {
    // If target-abi is not specified, use the valid triple-implied ABI.
    if (ABIName.empty())
      return checkABIStandardized(TripleABI);

    switch (ArgProvidedABI) {
    case ABI_Unknown:
      // Fallback to the triple-implied ABI if ABI name is specified but
      // invalid.
      errs() << "warning: the '" << ABIName
             << "' is not a recognized ABI for this target, ignoring and "
                "using triple-implied ABI\n";
      return checkABIStandardized(TripleABI);
    case ABI_ILP32S:
    case ABI_ILP32F:
    case ABI_ILP32D:
      if (Is64Bit) {
        errs() << "warning: 32-bit ABIs are not supported for 64-bit targets, "
                  "ignoring and using triple-implied ABI\n";
        return checkABIStandardized(TripleABI);
      }
      break;
    case ABI_LP64S:
    case ABI_LP64F:
    case ABI_LP64D:
      if (!Is64Bit) {
        errs() << "warning: 64-bit ABIs are not supported for 32-bit targets, "
                  "ignoring and using triple-implied ABI\n";
        return checkABIStandardized(TripleABI);
      }
      break;
    }

    switch (ArgProvidedABI) {
    case ABI_ILP32F:
    case ABI_LP64F:
      errs() << "warning: the '" << ABIName
             << "' ABI can't be used for a target that doesn't support the 'F' "
                "instruction set, ignoring and using triple-implied ABI\n";
      break;
    case ABI_ILP32D:
    case ABI_LP64D:
      errs() << "warning: the '" << ABIName
             << "' ABI can't be used for a target that doesn't support the 'D' "
                "instruction set, ignoring and using triple-implied ABI\n";
      break;
    default:
      llvm_unreachable("");
    }
    return checkABIStandardized(TripleABI);
  }

  // 3. Parse the 'feature-abi', and use it.
  auto GetFeatureABI = [=]() {
    if (FeatureBits[LoongArch::FeatureBasicD])
      return Is64Bit ? ABI_LP64D : ABI_ILP32D;
    if (FeatureBits[LoongArch::FeatureBasicF])
      return Is64Bit ? ABI_LP64F : ABI_ILP32F;
    return Is64Bit ? ABI_LP64S : ABI_ILP32S;
  };
  if (ABIName.empty())
    errs() << "warning: the triple-implied ABI is invalid, ignoring and using "
              "feature-implied ABI\n";
  else
    errs() << "warning: both target-abi and the triple-implied ABI are "
              "invalid, ignoring and using feature-implied ABI\n";
  return checkABIStandardized(GetFeatureABI());
}

ABI getTargetABI(StringRef ABIName) {
  auto TargetABI = StringSwitch<ABI>(ABIName)
                       .Case("ilp32s", ABI_ILP32S)
                       .Case("ilp32f", ABI_ILP32F)
                       .Case("ilp32d", ABI_ILP32D)
                       .Case("lp64s", ABI_LP64S)
                       .Case("lp64f", ABI_LP64F)
                       .Case("lp64d", ABI_LP64D)
                       .Default(ABI_Unknown);
  return TargetABI;
}

// To avoid the BP value clobbered by a function call, we need to choose a
// callee saved register to save the value. The `last` `S` register (s9) is
// used for FP. So we choose the previous (s8) as BP.
MCRegister getBPReg() { return LoongArch::R31; }

} // end namespace LoongArchABI

} // end namespace llvm

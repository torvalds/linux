//===--- CSKY.cpp - CSKY Helpers for Tools --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CSKY.h"
#include "ToolChains/CommonArgs.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/CSKYTargetParser.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/TargetParser.h"

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

std::optional<llvm::StringRef>
csky::getCSKYArchName(const Driver &D, const ArgList &Args,
                      const llvm::Triple &Triple) {
  if (const Arg *A = Args.getLastArg(options::OPT_march_EQ)) {
    llvm::CSKY::ArchKind ArchKind = llvm::CSKY::parseArch(A->getValue());

    if (ArchKind == llvm::CSKY::ArchKind::INVALID) {
      D.Diag(clang::diag::err_drv_invalid_arch_name) << A->getAsString(Args);
      return std::nullopt;
    }
    return std::optional<llvm::StringRef>(A->getValue());
  }

  if (const Arg *A = Args.getLastArg(clang::driver::options::OPT_mcpu_EQ)) {
    llvm::CSKY::ArchKind ArchKind = llvm::CSKY::parseCPUArch(A->getValue());
    if (ArchKind == llvm::CSKY::ArchKind::INVALID) {
      D.Diag(clang::diag::err_drv_clang_unsupported) << A->getAsString(Args);
      return std::nullopt;
    }
    return std::optional<llvm::StringRef>(llvm::CSKY::getArchName(ArchKind));
  }

  return std::optional<llvm::StringRef>("ck810");
}

csky::FloatABI csky::getCSKYFloatABI(const Driver &D, const ArgList &Args) {
  csky::FloatABI ABI = FloatABI::Soft;
  if (Arg *A =
          Args.getLastArg(options::OPT_msoft_float, options::OPT_mhard_float,
                          options::OPT_mfloat_abi_EQ)) {
    if (A->getOption().matches(options::OPT_msoft_float)) {
      ABI = FloatABI::Soft;
    } else if (A->getOption().matches(options::OPT_mhard_float)) {
      ABI = FloatABI::Hard;
    } else {
      ABI = llvm::StringSwitch<csky::FloatABI>(A->getValue())
                .Case("soft", FloatABI::Soft)
                .Case("softfp", FloatABI::SoftFP)
                .Case("hard", FloatABI::Hard)
                .Default(FloatABI::Invalid);
      if (ABI == FloatABI::Invalid) {
        D.Diag(diag::err_drv_invalid_mfloat_abi) << A->getAsString(Args);
        ABI = FloatABI::Soft;
      }
    }
  }

  return ABI;
}

// Handle -mfpu=.
static llvm::CSKY::CSKYFPUKind
getCSKYFPUFeatures(const Driver &D, const Arg *A, const ArgList &Args,
                   StringRef FPU, std::vector<StringRef> &Features) {

  llvm::CSKY::CSKYFPUKind FPUID =
      llvm::StringSwitch<llvm::CSKY::CSKYFPUKind>(FPU)
          .Case("auto", llvm::CSKY::FK_AUTO)
          .Case("fpv2", llvm::CSKY::FK_FPV2)
          .Case("fpv2_divd", llvm::CSKY::FK_FPV2_DIVD)
          .Case("fpv2_sf", llvm::CSKY::FK_FPV2_SF)
          .Case("fpv3", llvm::CSKY::FK_FPV3)
          .Case("fpv3_hf", llvm::CSKY::FK_FPV3_HF)
          .Case("fpv3_hsf", llvm::CSKY::FK_FPV3_HSF)
          .Case("fpv3_sdf", llvm::CSKY::FK_FPV3_SDF)
          .Default(llvm::CSKY::FK_INVALID);
  if (FPUID == llvm::CSKY::FK_INVALID) {
    D.Diag(clang::diag::err_drv_clang_unsupported) << A->getAsString(Args);
    return llvm::CSKY::FK_INVALID;
  }

  auto RemoveTargetFPUFeature =
      [&Features](ArrayRef<const char *> FPUFeatures) {
        for (auto FPUFeature : FPUFeatures) {
          auto it = llvm::find(Features, FPUFeature);
          if (it != Features.end())
            Features.erase(it);
        }
      };

  RemoveTargetFPUFeature({"+fpuv2_sf", "+fpuv2_df", "+fdivdu", "+fpuv3_hi",
                          "+fpuv3_hf", "+fpuv3_sf", "+fpuv3_df"});

  if (!llvm::CSKY::getFPUFeatures(FPUID, Features)) {
    D.Diag(clang::diag::err_drv_clang_unsupported) << A->getAsString(Args);
    return llvm::CSKY::FK_INVALID;
  }

  return FPUID;
}

void csky::getCSKYTargetFeatures(const Driver &D, const llvm::Triple &Triple,
                                 const ArgList &Args, ArgStringList &CmdArgs,
                                 std::vector<llvm::StringRef> &Features) {
  llvm::StringRef archName;
  llvm::StringRef cpuName;
  llvm::CSKY::ArchKind ArchKind = llvm::CSKY::ArchKind::INVALID;
  if (const Arg *A = Args.getLastArg(options::OPT_march_EQ)) {
    ArchKind = llvm::CSKY::parseArch(A->getValue());
    if (ArchKind == llvm::CSKY::ArchKind::INVALID) {
      D.Diag(clang::diag::err_drv_invalid_arch_name) << A->getAsString(Args);
      return;
    }
    archName = A->getValue();
  }

  if (const Arg *A = Args.getLastArg(clang::driver::options::OPT_mcpu_EQ)) {
    llvm::CSKY::ArchKind Kind = llvm::CSKY::parseCPUArch(A->getValue());
    if (Kind == llvm::CSKY::ArchKind::INVALID) {
      D.Diag(clang::diag::err_drv_clang_unsupported) << A->getAsString(Args);
      return;
    }
    if (!archName.empty() && Kind != ArchKind) {
      D.Diag(clang::diag::err_drv_clang_unsupported) << A->getAsString(Args);
      return;
    }
    cpuName = A->getValue();
    if (archName.empty())
      archName = llvm::CSKY::getArchName(Kind);
  }

  if (archName.empty() && cpuName.empty()) {
    archName = "ck810";
    cpuName = "ck810";
  } else if (!archName.empty() && cpuName.empty()) {
    cpuName = archName;
  }

  csky::FloatABI FloatABI = csky::getCSKYFloatABI(D, Args);

  if (FloatABI == csky::FloatABI::Hard) {
    Features.push_back("+hard-float-abi");
    Features.push_back("+hard-float");
  } else if (FloatABI == csky::FloatABI::SoftFP) {
    Features.push_back("+hard-float");
  }

  uint64_t Extension = llvm::CSKY::getDefaultExtensions(cpuName);
  llvm::CSKY::getExtensionFeatures(Extension, Features);

  if (const Arg *FPUArg = Args.getLastArg(options::OPT_mfpu_EQ))
    getCSKYFPUFeatures(D, FPUArg, Args, FPUArg->getValue(), Features);
}

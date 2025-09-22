//===--- XRayArgs.cpp - Arguments for XRay --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "clang/Driver/XRayArgs.h"
#include "ToolChains/CommonArgs.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "clang/Driver/ToolChain.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/SpecialCaseList.h"
#include "llvm/Support/VirtualFileSystem.h"

using namespace clang;
using namespace clang::driver;
using namespace llvm::opt;

constexpr const char *XRaySupportedModes[] = {"xray-fdr", "xray-basic"};

XRayArgs::XRayArgs(const ToolChain &TC, const ArgList &Args) {
  const Driver &D = TC.getDriver();
  const llvm::Triple &Triple = TC.getTriple();
  if (!Args.hasFlag(options::OPT_fxray_instrument,
                    options::OPT_fno_xray_instrument, false))
    return;
  XRayInstrument = Args.getLastArg(options::OPT_fxray_instrument);
  if (Triple.isMacOSX()) {
    switch (Triple.getArch()) {
    case llvm::Triple::aarch64:
    case llvm::Triple::x86_64:
      break;
    default:
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << XRayInstrument->getSpelling() << Triple.str();
      break;
    }
  } else if (Triple.isOSBinFormatELF()) {
    switch (Triple.getArch()) {
    case llvm::Triple::x86_64:
    case llvm::Triple::arm:
    case llvm::Triple::aarch64:
    case llvm::Triple::hexagon:
    case llvm::Triple::ppc64le:
    case llvm::Triple::loongarch64:
    case llvm::Triple::mips:
    case llvm::Triple::mipsel:
    case llvm::Triple::mips64:
    case llvm::Triple::mips64el:
      break;
    default:
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << XRayInstrument->getSpelling() << Triple.str();
    }
  } else {
    D.Diag(diag::err_drv_unsupported_opt_for_target)
        << XRayInstrument->getSpelling() << Triple.str();
  }

  // Both XRay and -fpatchable-function-entry use
  // TargetOpcode::PATCHABLE_FUNCTION_ENTER.
  if (Arg *A = Args.getLastArg(options::OPT_fpatchable_function_entry_EQ))
    D.Diag(diag::err_drv_argument_not_allowed_with)
        << XRayInstrument->getSpelling() << A->getSpelling();

  if (!Args.hasFlag(options::OPT_fxray_link_deps,
                    options::OPT_fno_xray_link_deps, true))
    XRayRT = false;

  auto Bundles =
      Args.getAllArgValues(options::OPT_fxray_instrumentation_bundle);
  if (Bundles.empty())
    InstrumentationBundle.Mask = XRayInstrKind::All;
  else
    for (const auto &B : Bundles) {
      llvm::SmallVector<StringRef, 2> BundleParts;
      llvm::SplitString(B, BundleParts, ",");
      for (const auto &P : BundleParts) {
        // TODO: Automate the generation of the string case table.
        auto Valid = llvm::StringSwitch<bool>(P)
                         .Cases("none", "all", "function", "function-entry",
                                "function-exit", "custom", true)
                         .Default(false);

        if (!Valid) {
          D.Diag(clang::diag::err_drv_invalid_value)
              << "-fxray-instrumentation-bundle=" << P;
          continue;
        }

        auto Mask = parseXRayInstrValue(P);
        if (Mask == XRayInstrKind::None) {
          InstrumentationBundle.clear();
          break;
        }

        InstrumentationBundle.Mask |= Mask;
      }
    }

  // Validate the always/never attribute files. We also make sure that they
  // are treated as actual dependencies.
  for (const auto &Filename :
       Args.getAllArgValues(options::OPT_fxray_always_instrument)) {
    if (D.getVFS().exists(Filename)) {
      AlwaysInstrumentFiles.push_back(Filename);
      ExtraDeps.push_back(Filename);
    } else
      D.Diag(clang::diag::err_drv_no_such_file) << Filename;
  }

  for (const auto &Filename :
       Args.getAllArgValues(options::OPT_fxray_never_instrument)) {
    if (D.getVFS().exists(Filename)) {
      NeverInstrumentFiles.push_back(Filename);
      ExtraDeps.push_back(Filename);
    } else
      D.Diag(clang::diag::err_drv_no_such_file) << Filename;
  }

  for (const auto &Filename :
       Args.getAllArgValues(options::OPT_fxray_attr_list)) {
    if (D.getVFS().exists(Filename)) {
      AttrListFiles.push_back(Filename);
      ExtraDeps.push_back(Filename);
    } else
      D.Diag(clang::diag::err_drv_no_such_file) << Filename;
  }

  // Get the list of modes we want to support.
  auto SpecifiedModes = Args.getAllArgValues(options::OPT_fxray_modes);
  if (SpecifiedModes.empty())
    llvm::copy(XRaySupportedModes, std::back_inserter(Modes));
  else
    for (const auto &Arg : SpecifiedModes) {
      // Parse CSV values for -fxray-modes=...
      llvm::SmallVector<StringRef, 2> ModeParts;
      llvm::SplitString(Arg, ModeParts, ",");
      for (const auto &M : ModeParts)
        if (M == "none")
          Modes.clear();
        else if (M == "all")
          llvm::copy(XRaySupportedModes, std::back_inserter(Modes));
        else
          Modes.push_back(std::string(M));
    }

  // Then we want to sort and unique the modes we've collected.
  llvm::sort(Modes);
  Modes.erase(std::unique(Modes.begin(), Modes.end()), Modes.end());
}

void XRayArgs::addArgs(const ToolChain &TC, const ArgList &Args,
                       ArgStringList &CmdArgs, types::ID InputType) const {
  if (!XRayInstrument)
    return;
  const Driver &D = TC.getDriver();
  XRayInstrument->render(Args, CmdArgs);

  // By default, the back-end will not emit the lowering for XRay customevent
  // calls if the function is not instrumented. In the future we will change
  // this default to be the reverse, but in the meantime we're going to
  // introduce the new functionality behind a flag.
  Args.addOptInFlag(CmdArgs, options::OPT_fxray_always_emit_customevents,
                    options::OPT_fno_xray_always_emit_customevents);

  Args.addOptInFlag(CmdArgs, options::OPT_fxray_always_emit_typedevents,
                    options::OPT_fno_xray_always_emit_typedevents);
  Args.addOptInFlag(CmdArgs, options::OPT_fxray_ignore_loops,
                    options::OPT_fno_xray_ignore_loops);
  Args.addOptOutFlag(CmdArgs, options::OPT_fxray_function_index,
                     options::OPT_fno_xray_function_index);

  if (const Arg *A =
          Args.getLastArg(options::OPT_fxray_instruction_threshold_EQ)) {
    int Value;
    StringRef S = A->getValue();
    if (S.getAsInteger(0, Value) || Value < 0)
      D.Diag(clang::diag::err_drv_invalid_value) << A->getAsString(Args) << S;
    else
      A->render(Args, CmdArgs);
  }

  int XRayFunctionGroups = 1;
  int XRaySelectedFunctionGroup = 0;
  if (const Arg *A = Args.getLastArg(options::OPT_fxray_function_groups)) {
    StringRef S = A->getValue();
    if (S.getAsInteger(0, XRayFunctionGroups) || XRayFunctionGroups < 1)
      D.Diag(clang::diag::err_drv_invalid_value) << A->getAsString(Args) << S;
    if (XRayFunctionGroups > 1)
      A->render(Args, CmdArgs);
  }
  if (const Arg *A =
          Args.getLastArg(options::OPT_fxray_selected_function_group)) {
    StringRef S = A->getValue();
    if (S.getAsInteger(0, XRaySelectedFunctionGroup) ||
        XRaySelectedFunctionGroup < 0 ||
        XRaySelectedFunctionGroup >= XRayFunctionGroups)
      D.Diag(clang::diag::err_drv_invalid_value) << A->getAsString(Args) << S;
    if (XRaySelectedFunctionGroup != 0)
      A->render(Args, CmdArgs);
  }

  for (const auto &Always : AlwaysInstrumentFiles) {
    SmallString<64> AlwaysInstrumentOpt("-fxray-always-instrument=");
    AlwaysInstrumentOpt += Always;
    CmdArgs.push_back(Args.MakeArgString(AlwaysInstrumentOpt));
  }

  for (const auto &Never : NeverInstrumentFiles) {
    SmallString<64> NeverInstrumentOpt("-fxray-never-instrument=");
    NeverInstrumentOpt += Never;
    CmdArgs.push_back(Args.MakeArgString(NeverInstrumentOpt));
  }

  for (const auto &AttrFile : AttrListFiles) {
    SmallString<64> AttrListFileOpt("-fxray-attr-list=");
    AttrListFileOpt += AttrFile;
    CmdArgs.push_back(Args.MakeArgString(AttrListFileOpt));
  }

  for (const auto &Dep : ExtraDeps) {
    SmallString<64> ExtraDepOpt("-fdepfile-entry=");
    ExtraDepOpt += Dep;
    CmdArgs.push_back(Args.MakeArgString(ExtraDepOpt));
  }

  for (const auto &Mode : Modes) {
    SmallString<64> ModeOpt("-fxray-modes=");
    ModeOpt += Mode;
    CmdArgs.push_back(Args.MakeArgString(ModeOpt));
  }

  SmallString<64> Bundle("-fxray-instrumentation-bundle=");
  if (InstrumentationBundle.full()) {
    Bundle += "all";
  } else if (InstrumentationBundle.empty()) {
    Bundle += "none";
  } else {
    if (InstrumentationBundle.has(XRayInstrKind::FunctionEntry) &&
        InstrumentationBundle.has(XRayInstrKind::FunctionExit))
      Bundle += "function";
    else if (InstrumentationBundle.has(XRayInstrKind::FunctionEntry))
      Bundle += "function-entry";
    else if (InstrumentationBundle.has(XRayInstrKind::FunctionExit))
      Bundle += "function-exit";

    if (InstrumentationBundle.has(XRayInstrKind::Custom))
      Bundle += "custom";
    if (InstrumentationBundle.has(XRayInstrKind::Typed))
      Bundle += "typed";
  }
  CmdArgs.push_back(Args.MakeArgString(Bundle));
}

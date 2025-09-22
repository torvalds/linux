//===-- TargetParser - Parser for target features ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a target parser to recognise CSKY hardware features
// such as CPU/ARCH names.
//
//===----------------------------------------------------------------------===//

#include "llvm/TargetParser/CSKYTargetParser.h"
#include "llvm/ADT/StringSwitch.h"

using namespace llvm;

bool CSKY::getFPUFeatures(CSKYFPUKind CSKYFPUKind,
                          std::vector<StringRef> &Features) {

  if (CSKYFPUKind >= FK_LAST || CSKYFPUKind == FK_INVALID)
    return false;

  switch (CSKYFPUKind) {
  case FK_AUTO:
    Features.push_back("+fpuv2_sf");
    Features.push_back("+fpuv2_df");
    Features.push_back("+fdivdu");
    break;
  case FK_FPV2:
    Features.push_back("+fpuv2_sf");
    Features.push_back("+fpuv2_df");
    break;
  case FK_FPV2_DIVD:
    Features.push_back("+fpuv2_sf");
    Features.push_back("+fpuv2_df");
    Features.push_back("+fdivdu");
    break;
  case FK_FPV2_SF:
    Features.push_back("+fpuv2_sf");
    break;
  case FK_FPV3:
    Features.push_back("+fpuv3_hf");
    Features.push_back("+fpuv3_hi");
    Features.push_back("+fpuv3_sf");
    Features.push_back("+fpuv3_df");
    break;
  case FK_FPV3_HF:
    Features.push_back("+fpuv3_hf");
    Features.push_back("+fpuv3_hi");
    break;
  case FK_FPV3_HSF:
    Features.push_back("+fpuv3_hf");
    Features.push_back("+fpuv3_hi");
    Features.push_back("+fpuv3_sf");
    break;
  case FK_FPV3_SDF:
    Features.push_back("+fpuv3_sf");
    Features.push_back("+fpuv3_df");
    break;
  default:
    llvm_unreachable("Unknown FPU Kind");
    return false;
  }

  return true;
}

// ======================================================= //
// Information by ID
// ======================================================= //

StringRef CSKY::getArchName(ArchKind AK) {
  return ARCHNames[static_cast<unsigned>(AK)].getName();
}

// The default cpu's name is same as arch name.
StringRef CSKY::getDefaultCPU(StringRef Arch) {
  ArchKind AK = parseArch(Arch);
  if (AK == CSKY::ArchKind::INVALID)
    return StringRef();

  return Arch;
}

// ======================================================= //
// Parsers
// ======================================================= //
CSKY::ArchKind CSKY::parseArch(StringRef Arch) {
  for (const auto A : ARCHNames) {
    if (A.getName() == Arch)
      return A.ID;
  }

  return CSKY::ArchKind::INVALID;
}

CSKY::ArchKind CSKY::parseCPUArch(StringRef CPU) {
  for (const auto C : CPUNames) {
    if (CPU == C.getName())
      return C.ArchID;
  }

  return CSKY::ArchKind::INVALID;
}

uint64_t CSKY::parseArchExt(StringRef ArchExt) {
  for (const auto &A : CSKYARCHExtNames) {
    if (ArchExt == A.getName())
      return A.ID;
  }
  return AEK_INVALID;
}

void CSKY::fillValidCPUArchList(SmallVectorImpl<StringRef> &Values) {
  for (const CpuNames<CSKY::ArchKind> &Arch : CPUNames) {
    if (Arch.ArchID != CSKY::ArchKind::INVALID)
      Values.push_back(Arch.getName());
  }
}

StringRef CSKY::getFPUName(unsigned FPUKind) {
  if (FPUKind >= FK_LAST)
    return StringRef();
  return FPUNames[FPUKind].getName();
}

CSKY::FPUVersion CSKY::getFPUVersion(unsigned FPUKind) {
  if (FPUKind >= FK_LAST)
    return FPUVersion::NONE;
  return FPUNames[FPUKind].FPUVer;
}

uint64_t CSKY::getDefaultExtensions(StringRef CPU) {
  return StringSwitch<uint64_t>(CPU)
#define CSKY_CPU_NAME(NAME, ID, DEFAULT_EXT)                                   \
  .Case(NAME, ARCHNames[static_cast<unsigned>(ArchKind::ID)].archBaseExt |     \
                  DEFAULT_EXT)
#include "llvm/TargetParser/CSKYTargetParser.def"
      .Default(CSKY::AEK_INVALID);
}

StringRef CSKY::getArchExtName(uint64_t ArchExtKind) {
  for (const auto &AE : CSKYARCHExtNames)
    if (ArchExtKind == AE.ID)
      return AE.getName();
  return StringRef();
}

static bool stripNegationPrefix(StringRef &Name) {
  if (Name.starts_with("no")) {
    Name = Name.substr(2);
    return true;
  }
  return false;
}

StringRef CSKY::getArchExtFeature(StringRef ArchExt) {
  bool Negated = stripNegationPrefix(ArchExt);
  for (const auto &AE : CSKYARCHExtNames) {
    if (AE.Feature && ArchExt == AE.getName())
      return StringRef(Negated ? AE.NegFeature : AE.Feature);
  }

  return StringRef();
}

bool CSKY::getExtensionFeatures(uint64_t Extensions,
                                std::vector<StringRef> &Features) {
  if (Extensions == CSKY::AEK_INVALID)
    return false;

  for (const auto &AE : CSKYARCHExtNames) {
    if ((Extensions & AE.ID) == AE.ID && AE.Feature)
      Features.push_back(AE.Feature);
  }

  return true;
}

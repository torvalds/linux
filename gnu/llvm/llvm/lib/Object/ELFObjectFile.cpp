//===- ELFObjectFile.cpp - ELF object file implementation -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Part of the ELFObjectFile class implementation.
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/ELFObjectFile.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/ELF.h"
#include "llvm/Object/ELFTypes.h"
#include "llvm/Object/Error.h"
#include "llvm/Support/ARMAttributeParser.h"
#include "llvm/Support/ARMBuildAttributes.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/HexagonAttributeParser.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/RISCVAttributeParser.h"
#include "llvm/Support/RISCVAttributes.h"
#include "llvm/TargetParser/RISCVISAInfo.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

using namespace llvm;
using namespace object;

const EnumEntry<unsigned> llvm::object::ElfSymbolTypes[NumElfSymbolTypes] = {
    {"None", "NOTYPE", ELF::STT_NOTYPE},
    {"Object", "OBJECT", ELF::STT_OBJECT},
    {"Function", "FUNC", ELF::STT_FUNC},
    {"Section", "SECTION", ELF::STT_SECTION},
    {"File", "FILE", ELF::STT_FILE},
    {"Common", "COMMON", ELF::STT_COMMON},
    {"TLS", "TLS", ELF::STT_TLS},
    {"Unknown", "<unknown>: 7", 7},
    {"Unknown", "<unknown>: 8", 8},
    {"Unknown", "<unknown>: 9", 9},
    {"GNU_IFunc", "IFUNC", ELF::STT_GNU_IFUNC},
    {"OS Specific", "<OS specific>: 11", 11},
    {"OS Specific", "<OS specific>: 12", 12},
    {"Proc Specific", "<processor specific>: 13", 13},
    {"Proc Specific", "<processor specific>: 14", 14},
    {"Proc Specific", "<processor specific>: 15", 15}
};

ELFObjectFileBase::ELFObjectFileBase(unsigned int Type, MemoryBufferRef Source)
    : ObjectFile(Type, Source) {}

template <class ELFT>
static Expected<std::unique_ptr<ELFObjectFile<ELFT>>>
createPtr(MemoryBufferRef Object, bool InitContent) {
  auto Ret = ELFObjectFile<ELFT>::create(Object, InitContent);
  if (Error E = Ret.takeError())
    return std::move(E);
  return std::make_unique<ELFObjectFile<ELFT>>(std::move(*Ret));
}

Expected<std::unique_ptr<ObjectFile>>
ObjectFile::createELFObjectFile(MemoryBufferRef Obj, bool InitContent) {
  std::pair<unsigned char, unsigned char> Ident =
      getElfArchType(Obj.getBuffer());
  std::size_t MaxAlignment =
      1ULL << llvm::countr_zero(
          reinterpret_cast<uintptr_t>(Obj.getBufferStart()));

  if (MaxAlignment < 2)
    return createError("Insufficient alignment");

  if (Ident.first == ELF::ELFCLASS32) {
    if (Ident.second == ELF::ELFDATA2LSB)
      return createPtr<ELF32LE>(Obj, InitContent);
    else if (Ident.second == ELF::ELFDATA2MSB)
      return createPtr<ELF32BE>(Obj, InitContent);
    else
      return createError("Invalid ELF data");
  } else if (Ident.first == ELF::ELFCLASS64) {
    if (Ident.second == ELF::ELFDATA2LSB)
      return createPtr<ELF64LE>(Obj, InitContent);
    else if (Ident.second == ELF::ELFDATA2MSB)
      return createPtr<ELF64BE>(Obj, InitContent);
    else
      return createError("Invalid ELF data");
  }
  return createError("Invalid ELF class");
}

SubtargetFeatures ELFObjectFileBase::getMIPSFeatures() const {
  SubtargetFeatures Features;
  unsigned PlatformFlags = getPlatformFlags();

  switch (PlatformFlags & ELF::EF_MIPS_ARCH) {
  case ELF::EF_MIPS_ARCH_1:
    break;
  case ELF::EF_MIPS_ARCH_2:
    Features.AddFeature("mips2");
    break;
  case ELF::EF_MIPS_ARCH_3:
    Features.AddFeature("mips3");
    break;
  case ELF::EF_MIPS_ARCH_4:
    Features.AddFeature("mips4");
    break;
  case ELF::EF_MIPS_ARCH_5:
    Features.AddFeature("mips5");
    break;
  case ELF::EF_MIPS_ARCH_32:
    Features.AddFeature("mips32");
    break;
  case ELF::EF_MIPS_ARCH_64:
    Features.AddFeature("mips64");
    break;
  case ELF::EF_MIPS_ARCH_32R2:
    Features.AddFeature("mips32r2");
    break;
  case ELF::EF_MIPS_ARCH_64R2:
    Features.AddFeature("mips64r2");
    break;
  case ELF::EF_MIPS_ARCH_32R6:
    Features.AddFeature("mips32r6");
    break;
  case ELF::EF_MIPS_ARCH_64R6:
    Features.AddFeature("mips64r6");
    break;
  default:
    llvm_unreachable("Unknown EF_MIPS_ARCH value");
  }

  switch (PlatformFlags & ELF::EF_MIPS_MACH) {
  case ELF::EF_MIPS_MACH_NONE:
    // No feature associated with this value.
    break;
  case ELF::EF_MIPS_MACH_OCTEON:
    Features.AddFeature("cnmips");
    break;
  default:
    llvm_unreachable("Unknown EF_MIPS_ARCH value");
  }

  if (PlatformFlags & ELF::EF_MIPS_ARCH_ASE_M16)
    Features.AddFeature("mips16");
  if (PlatformFlags & ELF::EF_MIPS_MICROMIPS)
    Features.AddFeature("micromips");

  return Features;
}

SubtargetFeatures ELFObjectFileBase::getARMFeatures() const {
  SubtargetFeatures Features;
  ARMAttributeParser Attributes;
  if (Error E = getBuildAttributes(Attributes)) {
    consumeError(std::move(E));
    return SubtargetFeatures();
  }

  // both ARMv7-M and R have to support thumb hardware div
  bool isV7 = false;
  std::optional<unsigned> Attr =
      Attributes.getAttributeValue(ARMBuildAttrs::CPU_arch);
  if (Attr)
    isV7 = *Attr == ARMBuildAttrs::v7;

  Attr = Attributes.getAttributeValue(ARMBuildAttrs::CPU_arch_profile);
  if (Attr) {
    switch (*Attr) {
    case ARMBuildAttrs::ApplicationProfile:
      Features.AddFeature("aclass");
      break;
    case ARMBuildAttrs::RealTimeProfile:
      Features.AddFeature("rclass");
      if (isV7)
        Features.AddFeature("hwdiv");
      break;
    case ARMBuildAttrs::MicroControllerProfile:
      Features.AddFeature("mclass");
      if (isV7)
        Features.AddFeature("hwdiv");
      break;
    }
  }

  Attr = Attributes.getAttributeValue(ARMBuildAttrs::THUMB_ISA_use);
  if (Attr) {
    switch (*Attr) {
    default:
      break;
    case ARMBuildAttrs::Not_Allowed:
      Features.AddFeature("thumb", false);
      Features.AddFeature("thumb2", false);
      break;
    case ARMBuildAttrs::AllowThumb32:
      Features.AddFeature("thumb2");
      break;
    }
  }

  Attr = Attributes.getAttributeValue(ARMBuildAttrs::FP_arch);
  if (Attr) {
    switch (*Attr) {
    default:
      break;
    case ARMBuildAttrs::Not_Allowed:
      Features.AddFeature("vfp2sp", false);
      Features.AddFeature("vfp3d16sp", false);
      Features.AddFeature("vfp4d16sp", false);
      break;
    case ARMBuildAttrs::AllowFPv2:
      Features.AddFeature("vfp2");
      break;
    case ARMBuildAttrs::AllowFPv3A:
    case ARMBuildAttrs::AllowFPv3B:
      Features.AddFeature("vfp3");
      break;
    case ARMBuildAttrs::AllowFPv4A:
    case ARMBuildAttrs::AllowFPv4B:
      Features.AddFeature("vfp4");
      break;
    }
  }

  Attr = Attributes.getAttributeValue(ARMBuildAttrs::Advanced_SIMD_arch);
  if (Attr) {
    switch (*Attr) {
    default:
      break;
    case ARMBuildAttrs::Not_Allowed:
      Features.AddFeature("neon", false);
      Features.AddFeature("fp16", false);
      break;
    case ARMBuildAttrs::AllowNeon:
      Features.AddFeature("neon");
      break;
    case ARMBuildAttrs::AllowNeon2:
      Features.AddFeature("neon");
      Features.AddFeature("fp16");
      break;
    }
  }

  Attr = Attributes.getAttributeValue(ARMBuildAttrs::MVE_arch);
  if (Attr) {
    switch (*Attr) {
    default:
      break;
    case ARMBuildAttrs::Not_Allowed:
      Features.AddFeature("mve", false);
      Features.AddFeature("mve.fp", false);
      break;
    case ARMBuildAttrs::AllowMVEInteger:
      Features.AddFeature("mve.fp", false);
      Features.AddFeature("mve");
      break;
    case ARMBuildAttrs::AllowMVEIntegerAndFloat:
      Features.AddFeature("mve.fp");
      break;
    }
  }

  Attr = Attributes.getAttributeValue(ARMBuildAttrs::DIV_use);
  if (Attr) {
    switch (*Attr) {
    default:
      break;
    case ARMBuildAttrs::DisallowDIV:
      Features.AddFeature("hwdiv", false);
      Features.AddFeature("hwdiv-arm", false);
      break;
    case ARMBuildAttrs::AllowDIVExt:
      Features.AddFeature("hwdiv");
      Features.AddFeature("hwdiv-arm");
      break;
    }
  }

  return Features;
}

static std::optional<std::string> hexagonAttrToFeatureString(unsigned Attr) {
  switch (Attr) {
  case 5:
    return "v5";
  case 55:
    return "v55";
  case 60:
    return "v60";
  case 62:
    return "v62";
  case 65:
    return "v65";
  case 67:
    return "v67";
  case 68:
    return "v68";
  case 69:
    return "v69";
  case 71:
    return "v71";
  case 73:
    return "v73";
  default:
    return {};
  }
}

SubtargetFeatures ELFObjectFileBase::getHexagonFeatures() const {
  SubtargetFeatures Features;
  HexagonAttributeParser Parser;
  if (Error E = getBuildAttributes(Parser)) {
    // Return no attributes if none can be read.
    // This behavior is important for backwards compatibility.
    consumeError(std::move(E));
    return Features;
  }
  std::optional<unsigned> Attr;

  if ((Attr = Parser.getAttributeValue(HexagonAttrs::ARCH))) {
    if (std::optional<std::string> FeatureString =
            hexagonAttrToFeatureString(*Attr))
      Features.AddFeature(*FeatureString);
  }

  if ((Attr = Parser.getAttributeValue(HexagonAttrs::HVXARCH))) {
    std::optional<std::string> FeatureString =
        hexagonAttrToFeatureString(*Attr);
    // There is no corresponding hvx arch for v5 and v55.
    if (FeatureString && *Attr >= 60)
      Features.AddFeature("hvx" + *FeatureString);
  }

  if ((Attr = Parser.getAttributeValue(HexagonAttrs::HVXIEEEFP)))
    if (*Attr)
      Features.AddFeature("hvx-ieee-fp");

  if ((Attr = Parser.getAttributeValue(HexagonAttrs::HVXQFLOAT)))
    if (*Attr)
      Features.AddFeature("hvx-qfloat");

  if ((Attr = Parser.getAttributeValue(HexagonAttrs::ZREG)))
    if (*Attr)
      Features.AddFeature("zreg");

  if ((Attr = Parser.getAttributeValue(HexagonAttrs::AUDIO)))
    if (*Attr)
      Features.AddFeature("audio");

  if ((Attr = Parser.getAttributeValue(HexagonAttrs::CABAC)))
    if (*Attr)
      Features.AddFeature("cabac");

  return Features;
}

Expected<SubtargetFeatures> ELFObjectFileBase::getRISCVFeatures() const {
  SubtargetFeatures Features;
  unsigned PlatformFlags = getPlatformFlags();

  if (PlatformFlags & ELF::EF_RISCV_RVC) {
    Features.AddFeature("zca");
  }

  RISCVAttributeParser Attributes;
  if (Error E = getBuildAttributes(Attributes)) {
    return std::move(E);
  }

  std::optional<StringRef> Attr =
      Attributes.getAttributeString(RISCVAttrs::ARCH);
  if (Attr) {
    auto ParseResult = RISCVISAInfo::parseNormalizedArchString(*Attr);
    if (!ParseResult)
      return ParseResult.takeError();
    auto &ISAInfo = *ParseResult;

    if (ISAInfo->getXLen() == 32)
      Features.AddFeature("64bit", false);
    else if (ISAInfo->getXLen() == 64)
      Features.AddFeature("64bit");
    else
      llvm_unreachable("XLEN should be 32 or 64.");

    Features.addFeaturesVector(ISAInfo->toFeatures());
  }

  return Features;
}

SubtargetFeatures ELFObjectFileBase::getLoongArchFeatures() const {
  SubtargetFeatures Features;

  switch (getPlatformFlags() & ELF::EF_LOONGARCH_ABI_MODIFIER_MASK) {
  case ELF::EF_LOONGARCH_ABI_SOFT_FLOAT:
    break;
  case ELF::EF_LOONGARCH_ABI_DOUBLE_FLOAT:
    Features.AddFeature("d");
    // D implies F according to LoongArch ISA spec.
    [[fallthrough]];
  case ELF::EF_LOONGARCH_ABI_SINGLE_FLOAT:
    Features.AddFeature("f");
    break;
  }

  return Features;
}

Expected<SubtargetFeatures> ELFObjectFileBase::getFeatures() const {
  switch (getEMachine()) {
  case ELF::EM_MIPS:
    return getMIPSFeatures();
  case ELF::EM_ARM:
    return getARMFeatures();
  case ELF::EM_RISCV:
    return getRISCVFeatures();
  case ELF::EM_LOONGARCH:
    return getLoongArchFeatures();
  case ELF::EM_HEXAGON:
    return getHexagonFeatures();
  default:
    return SubtargetFeatures();
  }
}

std::optional<StringRef> ELFObjectFileBase::tryGetCPUName() const {
  switch (getEMachine()) {
  case ELF::EM_AMDGPU:
    return getAMDGPUCPUName();
  case ELF::EM_CUDA:
    return getNVPTXCPUName();
  case ELF::EM_PPC:
  case ELF::EM_PPC64:
    return StringRef("future");
  default:
    return std::nullopt;
  }
}

StringRef ELFObjectFileBase::getAMDGPUCPUName() const {
  assert(getEMachine() == ELF::EM_AMDGPU);
  unsigned CPU = getPlatformFlags() & ELF::EF_AMDGPU_MACH;

  switch (CPU) {
  // Radeon HD 2000/3000 Series (R600).
  case ELF::EF_AMDGPU_MACH_R600_R600:
    return "r600";
  case ELF::EF_AMDGPU_MACH_R600_R630:
    return "r630";
  case ELF::EF_AMDGPU_MACH_R600_RS880:
    return "rs880";
  case ELF::EF_AMDGPU_MACH_R600_RV670:
    return "rv670";

  // Radeon HD 4000 Series (R700).
  case ELF::EF_AMDGPU_MACH_R600_RV710:
    return "rv710";
  case ELF::EF_AMDGPU_MACH_R600_RV730:
    return "rv730";
  case ELF::EF_AMDGPU_MACH_R600_RV770:
    return "rv770";

  // Radeon HD 5000 Series (Evergreen).
  case ELF::EF_AMDGPU_MACH_R600_CEDAR:
    return "cedar";
  case ELF::EF_AMDGPU_MACH_R600_CYPRESS:
    return "cypress";
  case ELF::EF_AMDGPU_MACH_R600_JUNIPER:
    return "juniper";
  case ELF::EF_AMDGPU_MACH_R600_REDWOOD:
    return "redwood";
  case ELF::EF_AMDGPU_MACH_R600_SUMO:
    return "sumo";

  // Radeon HD 6000 Series (Northern Islands).
  case ELF::EF_AMDGPU_MACH_R600_BARTS:
    return "barts";
  case ELF::EF_AMDGPU_MACH_R600_CAICOS:
    return "caicos";
  case ELF::EF_AMDGPU_MACH_R600_CAYMAN:
    return "cayman";
  case ELF::EF_AMDGPU_MACH_R600_TURKS:
    return "turks";

  // AMDGCN GFX6.
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX600:
    return "gfx600";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX601:
    return "gfx601";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX602:
    return "gfx602";

  // AMDGCN GFX7.
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX700:
    return "gfx700";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX701:
    return "gfx701";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX702:
    return "gfx702";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX703:
    return "gfx703";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX704:
    return "gfx704";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX705:
    return "gfx705";

  // AMDGCN GFX8.
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX801:
    return "gfx801";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX802:
    return "gfx802";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX803:
    return "gfx803";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX805:
    return "gfx805";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX810:
    return "gfx810";

  // AMDGCN GFX9.
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX900:
    return "gfx900";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX902:
    return "gfx902";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX904:
    return "gfx904";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX906:
    return "gfx906";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX908:
    return "gfx908";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX909:
    return "gfx909";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX90A:
    return "gfx90a";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX90C:
    return "gfx90c";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX940:
    return "gfx940";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX941:
    return "gfx941";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX942:
    return "gfx942";

  // AMDGCN GFX10.
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX1010:
    return "gfx1010";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX1011:
    return "gfx1011";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX1012:
    return "gfx1012";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX1013:
    return "gfx1013";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX1030:
    return "gfx1030";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX1031:
    return "gfx1031";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX1032:
    return "gfx1032";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX1033:
    return "gfx1033";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX1034:
    return "gfx1034";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX1035:
    return "gfx1035";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX1036:
    return "gfx1036";

  // AMDGCN GFX11.
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX1100:
    return "gfx1100";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX1101:
    return "gfx1101";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX1102:
    return "gfx1102";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX1103:
    return "gfx1103";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX1150:
    return "gfx1150";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX1151:
    return "gfx1151";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX1152:
    return "gfx1152";

  // AMDGCN GFX12.
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX1200:
    return "gfx1200";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX1201:
    return "gfx1201";

  // Generic AMDGCN targets
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX9_GENERIC:
    return "gfx9-generic";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX10_1_GENERIC:
    return "gfx10-1-generic";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX10_3_GENERIC:
    return "gfx10-3-generic";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX11_GENERIC:
    return "gfx11-generic";
  case ELF::EF_AMDGPU_MACH_AMDGCN_GFX12_GENERIC:
    return "gfx12-generic";
  default:
    llvm_unreachable("Unknown EF_AMDGPU_MACH value");
  }
}

StringRef ELFObjectFileBase::getNVPTXCPUName() const {
  assert(getEMachine() == ELF::EM_CUDA);
  unsigned SM = getPlatformFlags() & ELF::EF_CUDA_SM;

  switch (SM) {
  // Fermi architecture.
  case ELF::EF_CUDA_SM20:
    return "sm_20";
  case ELF::EF_CUDA_SM21:
    return "sm_21";

  // Kepler architecture.
  case ELF::EF_CUDA_SM30:
    return "sm_30";
  case ELF::EF_CUDA_SM32:
    return "sm_32";
  case ELF::EF_CUDA_SM35:
    return "sm_35";
  case ELF::EF_CUDA_SM37:
    return "sm_37";

  // Maxwell architecture.
  case ELF::EF_CUDA_SM50:
    return "sm_50";
  case ELF::EF_CUDA_SM52:
    return "sm_52";
  case ELF::EF_CUDA_SM53:
    return "sm_53";

  // Pascal architecture.
  case ELF::EF_CUDA_SM60:
    return "sm_60";
  case ELF::EF_CUDA_SM61:
    return "sm_61";
  case ELF::EF_CUDA_SM62:
    return "sm_62";

  // Volta architecture.
  case ELF::EF_CUDA_SM70:
    return "sm_70";
  case ELF::EF_CUDA_SM72:
    return "sm_72";

  // Turing architecture.
  case ELF::EF_CUDA_SM75:
    return "sm_75";

  // Ampere architecture.
  case ELF::EF_CUDA_SM80:
    return "sm_80";
  case ELF::EF_CUDA_SM86:
    return "sm_86";
  case ELF::EF_CUDA_SM87:
    return "sm_87";

  // Ada architecture.
  case ELF::EF_CUDA_SM89:
    return "sm_89";

  // Hopper architecture.
  case ELF::EF_CUDA_SM90:
    return getPlatformFlags() & ELF::EF_CUDA_ACCELERATORS ? "sm_90a" : "sm_90";
  default:
    llvm_unreachable("Unknown EF_CUDA_SM value");
  }
}

// FIXME Encode from a tablegen description or target parser.
void ELFObjectFileBase::setARMSubArch(Triple &TheTriple) const {
  if (TheTriple.getSubArch() != Triple::NoSubArch)
    return;

  ARMAttributeParser Attributes;
  if (Error E = getBuildAttributes(Attributes)) {
    // TODO Propagate Error.
    consumeError(std::move(E));
    return;
  }

  std::string Triple;
  // Default to ARM, but use the triple if it's been set.
  if (TheTriple.isThumb())
    Triple = "thumb";
  else
    Triple = "arm";

  std::optional<unsigned> Attr =
      Attributes.getAttributeValue(ARMBuildAttrs::CPU_arch);
  if (Attr) {
    switch (*Attr) {
    case ARMBuildAttrs::v4:
      Triple += "v4";
      break;
    case ARMBuildAttrs::v4T:
      Triple += "v4t";
      break;
    case ARMBuildAttrs::v5T:
      Triple += "v5t";
      break;
    case ARMBuildAttrs::v5TE:
      Triple += "v5te";
      break;
    case ARMBuildAttrs::v5TEJ:
      Triple += "v5tej";
      break;
    case ARMBuildAttrs::v6:
      Triple += "v6";
      break;
    case ARMBuildAttrs::v6KZ:
      Triple += "v6kz";
      break;
    case ARMBuildAttrs::v6T2:
      Triple += "v6t2";
      break;
    case ARMBuildAttrs::v6K:
      Triple += "v6k";
      break;
    case ARMBuildAttrs::v7: {
      std::optional<unsigned> ArchProfileAttr =
          Attributes.getAttributeValue(ARMBuildAttrs::CPU_arch_profile);
      if (ArchProfileAttr &&
          *ArchProfileAttr == ARMBuildAttrs::MicroControllerProfile)
        Triple += "v7m";
      else
        Triple += "v7";
      break;
    }
    case ARMBuildAttrs::v6_M:
      Triple += "v6m";
      break;
    case ARMBuildAttrs::v6S_M:
      Triple += "v6sm";
      break;
    case ARMBuildAttrs::v7E_M:
      Triple += "v7em";
      break;
    case ARMBuildAttrs::v8_A:
      Triple += "v8a";
      break;
    case ARMBuildAttrs::v8_R:
      Triple += "v8r";
      break;
    case ARMBuildAttrs::v8_M_Base:
      Triple += "v8m.base";
      break;
    case ARMBuildAttrs::v8_M_Main:
      Triple += "v8m.main";
      break;
    case ARMBuildAttrs::v8_1_M_Main:
      Triple += "v8.1m.main";
      break;
    case ARMBuildAttrs::v9_A:
      Triple += "v9a";
      break;
    }
  }
  if (!isLittleEndian())
    Triple += "eb";

  TheTriple.setArchName(Triple);
}

std::vector<ELFPltEntry> ELFObjectFileBase::getPltEntries() const {
  std::string Err;
  const auto Triple = makeTriple();
  const auto *T = TargetRegistry::lookupTarget(Triple.str(), Err);
  if (!T)
    return {};
  uint32_t JumpSlotReloc = 0, GlobDatReloc = 0;
  switch (Triple.getArch()) {
    case Triple::x86:
      JumpSlotReloc = ELF::R_386_JUMP_SLOT;
      GlobDatReloc = ELF::R_386_GLOB_DAT;
      break;
    case Triple::x86_64:
      JumpSlotReloc = ELF::R_X86_64_JUMP_SLOT;
      GlobDatReloc = ELF::R_X86_64_GLOB_DAT;
      break;
    case Triple::aarch64:
    case Triple::aarch64_be:
      JumpSlotReloc = ELF::R_AARCH64_JUMP_SLOT;
      break;
    default:
      return {};
  }
  std::unique_ptr<const MCInstrInfo> MII(T->createMCInstrInfo());
  std::unique_ptr<const MCInstrAnalysis> MIA(
      T->createMCInstrAnalysis(MII.get()));
  if (!MIA)
    return {};
  std::vector<std::pair<uint64_t, uint64_t>> PltEntries;
  std::optional<SectionRef> RelaPlt, RelaDyn;
  uint64_t GotBaseVA = 0;
  for (const SectionRef &Section : sections()) {
    Expected<StringRef> NameOrErr = Section.getName();
    if (!NameOrErr) {
      consumeError(NameOrErr.takeError());
      continue;
    }
    StringRef Name = *NameOrErr;

    if (Name == ".rela.plt" || Name == ".rel.plt") {
      RelaPlt = Section;
    } else if (Name == ".rela.dyn" || Name == ".rel.dyn") {
      RelaDyn = Section;
    } else if (Name == ".got.plt") {
      GotBaseVA = Section.getAddress();
    } else if (Name == ".plt" || Name == ".plt.got") {
      Expected<StringRef> PltContents = Section.getContents();
      if (!PltContents) {
        consumeError(PltContents.takeError());
        return {};
      }
      llvm::append_range(
          PltEntries,
          MIA->findPltEntries(Section.getAddress(),
                              arrayRefFromStringRef(*PltContents), Triple));
    }
  }

  // Build a map from GOT entry virtual address to PLT entry virtual address.
  DenseMap<uint64_t, uint64_t> GotToPlt;
  for (auto [Plt, GotPlt] : PltEntries) {
    uint64_t GotPltEntry = GotPlt;
    // An x86-32 PIC PLT uses jmp DWORD PTR [ebx-offset]. Add
    // _GLOBAL_OFFSET_TABLE_ (EBX) to get the .got.plt (or .got) entry address.
    // See X86MCTargetDesc.cpp:findPltEntries for the 1 << 32 bit.
    if (GotPltEntry & (uint64_t(1) << 32) && getEMachine() == ELF::EM_386)
      GotPltEntry = static_cast<int32_t>(GotPltEntry) + GotBaseVA;
    GotToPlt.insert(std::make_pair(GotPltEntry, Plt));
  }

  // Find the relocations in the dynamic relocation table that point to
  // locations in the GOT for which we know the corresponding PLT entry.
  std::vector<ELFPltEntry> Result;
  auto handleRels = [&](iterator_range<relocation_iterator> Rels,
                        uint32_t RelType, StringRef PltSec) {
    for (const auto &R : Rels) {
      if (R.getType() != RelType)
        continue;
      auto PltEntryIter = GotToPlt.find(R.getOffset());
      if (PltEntryIter != GotToPlt.end()) {
        symbol_iterator Sym = R.getSymbol();
        if (Sym == symbol_end())
          Result.push_back(
              ELFPltEntry{PltSec, std::nullopt, PltEntryIter->second});
        else
          Result.push_back(ELFPltEntry{PltSec, Sym->getRawDataRefImpl(),
                                       PltEntryIter->second});
      }
    }
  };

  if (RelaPlt)
    handleRels(RelaPlt->relocations(), JumpSlotReloc, ".plt");

  // If a symbol needing a PLT entry also needs a GLOB_DAT relocation, GNU ld's
  // x86 port places the PLT entry in the .plt.got section.
  if (RelaDyn)
    handleRels(RelaDyn->relocations(), GlobDatReloc, ".plt.got");

  return Result;
}

template <class ELFT>
Expected<std::vector<BBAddrMap>> static readBBAddrMapImpl(
    const ELFFile<ELFT> &EF, std::optional<unsigned> TextSectionIndex,
    std::vector<PGOAnalysisMap> *PGOAnalyses) {
  using Elf_Shdr = typename ELFT::Shdr;
  bool IsRelocatable = EF.getHeader().e_type == ELF::ET_REL;
  std::vector<BBAddrMap> BBAddrMaps;
  if (PGOAnalyses)
    PGOAnalyses->clear();

  const auto &Sections = cantFail(EF.sections());
  auto IsMatch = [&](const Elf_Shdr &Sec) -> Expected<bool> {
    if (Sec.sh_type != ELF::SHT_LLVM_BB_ADDR_MAP &&
        Sec.sh_type != ELF::SHT_LLVM_BB_ADDR_MAP_V0)
      return false;
    if (!TextSectionIndex)
      return true;
    Expected<const Elf_Shdr *> TextSecOrErr = EF.getSection(Sec.sh_link);
    if (!TextSecOrErr)
      return createError("unable to get the linked-to section for " +
                         describe(EF, Sec) + ": " +
                         toString(TextSecOrErr.takeError()));
    assert(*TextSecOrErr >= Sections.begin() &&
           "Text section pointer outside of bounds");
    if (*TextSectionIndex !=
        (unsigned)std::distance(Sections.begin(), *TextSecOrErr))
      return false;
    return true;
  };

  Expected<MapVector<const Elf_Shdr *, const Elf_Shdr *>> SectionRelocMapOrErr =
      EF.getSectionAndRelocations(IsMatch);
  if (!SectionRelocMapOrErr)
    return SectionRelocMapOrErr.takeError();

  for (auto const &[Sec, RelocSec] : *SectionRelocMapOrErr) {
    if (IsRelocatable && !RelocSec)
      return createError("unable to get relocation section for " +
                         describe(EF, *Sec));
    Expected<std::vector<BBAddrMap>> BBAddrMapOrErr =
        EF.decodeBBAddrMap(*Sec, RelocSec, PGOAnalyses);
    if (!BBAddrMapOrErr) {
      if (PGOAnalyses)
        PGOAnalyses->clear();
      return createError("unable to read " + describe(EF, *Sec) + ": " +
                         toString(BBAddrMapOrErr.takeError()));
    }
    std::move(BBAddrMapOrErr->begin(), BBAddrMapOrErr->end(),
              std::back_inserter(BBAddrMaps));
  }
  if (PGOAnalyses)
    assert(PGOAnalyses->size() == BBAddrMaps.size() &&
           "The same number of BBAddrMaps and PGOAnalysisMaps should be "
           "returned when PGO information is requested");
  return BBAddrMaps;
}

template <class ELFT>
static Expected<std::vector<VersionEntry>>
readDynsymVersionsImpl(const ELFFile<ELFT> &EF,
                       ELFObjectFileBase::elf_symbol_iterator_range Symbols) {
  using Elf_Shdr = typename ELFT::Shdr;
  const Elf_Shdr *VerSec = nullptr;
  const Elf_Shdr *VerNeedSec = nullptr;
  const Elf_Shdr *VerDefSec = nullptr;
  // The user should ensure sections() can't fail here.
  for (const Elf_Shdr &Sec : cantFail(EF.sections())) {
    if (Sec.sh_type == ELF::SHT_GNU_versym)
      VerSec = &Sec;
    else if (Sec.sh_type == ELF::SHT_GNU_verdef)
      VerDefSec = &Sec;
    else if (Sec.sh_type == ELF::SHT_GNU_verneed)
      VerNeedSec = &Sec;
  }
  if (!VerSec)
    return std::vector<VersionEntry>();

  Expected<SmallVector<std::optional<VersionEntry>, 0>> MapOrErr =
      EF.loadVersionMap(VerNeedSec, VerDefSec);
  if (!MapOrErr)
    return MapOrErr.takeError();

  std::vector<VersionEntry> Ret;
  size_t I = 0;
  for (const ELFSymbolRef &Sym : Symbols) {
    ++I;
    Expected<const typename ELFT::Versym *> VerEntryOrErr =
        EF.template getEntry<typename ELFT::Versym>(*VerSec, I);
    if (!VerEntryOrErr)
      return createError("unable to read an entry with index " + Twine(I) +
                         " from " + describe(EF, *VerSec) + ": " +
                         toString(VerEntryOrErr.takeError()));

    Expected<uint32_t> FlagsOrErr = Sym.getFlags();
    if (!FlagsOrErr)
      return createError("unable to read flags for symbol with index " +
                         Twine(I) + ": " + toString(FlagsOrErr.takeError()));

    bool IsDefault;
    Expected<StringRef> VerOrErr = EF.getSymbolVersionByIndex(
        (*VerEntryOrErr)->vs_index, IsDefault, *MapOrErr,
        (*FlagsOrErr) & SymbolRef::SF_Undefined);
    if (!VerOrErr)
      return createError("unable to get a version for entry " + Twine(I) +
                         " of " + describe(EF, *VerSec) + ": " +
                         toString(VerOrErr.takeError()));

    Ret.push_back({(*VerOrErr).str(), IsDefault});
  }

  return Ret;
}

Expected<std::vector<VersionEntry>>
ELFObjectFileBase::readDynsymVersions() const {
  elf_symbol_iterator_range Symbols = getDynamicSymbolIterators();
  if (const auto *Obj = dyn_cast<ELF32LEObjectFile>(this))
    return readDynsymVersionsImpl(Obj->getELFFile(), Symbols);
  if (const auto *Obj = dyn_cast<ELF32BEObjectFile>(this))
    return readDynsymVersionsImpl(Obj->getELFFile(), Symbols);
  if (const auto *Obj = dyn_cast<ELF64LEObjectFile>(this))
    return readDynsymVersionsImpl(Obj->getELFFile(), Symbols);
  return readDynsymVersionsImpl(cast<ELF64BEObjectFile>(this)->getELFFile(),
                                Symbols);
}

Expected<std::vector<BBAddrMap>> ELFObjectFileBase::readBBAddrMap(
    std::optional<unsigned> TextSectionIndex,
    std::vector<PGOAnalysisMap> *PGOAnalyses) const {
  if (const auto *Obj = dyn_cast<ELF32LEObjectFile>(this))
    return readBBAddrMapImpl(Obj->getELFFile(), TextSectionIndex, PGOAnalyses);
  if (const auto *Obj = dyn_cast<ELF64LEObjectFile>(this))
    return readBBAddrMapImpl(Obj->getELFFile(), TextSectionIndex, PGOAnalyses);
  if (const auto *Obj = dyn_cast<ELF32BEObjectFile>(this))
    return readBBAddrMapImpl(Obj->getELFFile(), TextSectionIndex, PGOAnalyses);
  return readBBAddrMapImpl(cast<ELF64BEObjectFile>(this)->getELFFile(),
                           TextSectionIndex, PGOAnalyses);
}

StringRef ELFObjectFileBase::getCrelDecodeProblem(SectionRef Sec) const {
  auto Data = Sec.getRawDataRefImpl();
  if (const auto *Obj = dyn_cast<ELF32LEObjectFile>(this))
    return Obj->getCrelDecodeProblem(Data);
  if (const auto *Obj = dyn_cast<ELF32BEObjectFile>(this))
    return Obj->getCrelDecodeProblem(Data);
  if (const auto *Obj = dyn_cast<ELF64LEObjectFile>(this))
    return Obj->getCrelDecodeProblem(Data);
  return cast<ELF64BEObjectFile>(this)->getCrelDecodeProblem(Data);
}

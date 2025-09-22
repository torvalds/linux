//===-- CSKYELFStreamer.cpp - CSKY ELF Target Streamer Methods ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides CSKY specific target streamer methods.
//
//===----------------------------------------------------------------------===//

#include "CSKYELFStreamer.h"
#include "CSKYMCTargetDesc.h"
#include "MCTargetDesc/CSKYAsmBackend.h"
#include "MCTargetDesc/CSKYBaseInfo.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/Support/CSKYAttributes.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/LEB128.h"
#include "llvm/TargetParser/CSKYTargetParser.h"

using namespace llvm;

// This part is for ELF object output.
CSKYTargetELFStreamer::CSKYTargetELFStreamer(MCStreamer &S,
                                             const MCSubtargetInfo &STI)
    : CSKYTargetStreamer(S), CurrentVendor("csky") {
  ELFObjectWriter &W = getStreamer().getWriter();
  const FeatureBitset &Features = STI.getFeatureBits();

  unsigned EFlags = W.getELFHeaderEFlags();

  EFlags |= ELF::EF_CSKY_ABIV2;

  if (Features[CSKY::ProcCK801])
    EFlags |= ELF::EF_CSKY_801;
  else if (Features[CSKY::ProcCK802])
    EFlags |= ELF::EF_CSKY_802;
  else if (Features[CSKY::ProcCK803])
    EFlags |= ELF::EF_CSKY_803;
  else if (Features[CSKY::ProcCK804])
    EFlags |= ELF::EF_CSKY_803;
  else if (Features[CSKY::ProcCK805])
    EFlags |= ELF::EF_CSKY_805;
  else if (Features[CSKY::ProcCK807])
    EFlags |= ELF::EF_CSKY_807;
  else if (Features[CSKY::ProcCK810])
    EFlags |= ELF::EF_CSKY_810;
  else if (Features[CSKY::ProcCK860])
    EFlags |= ELF::EF_CSKY_860;
  else
    EFlags |= ELF::EF_CSKY_810;

  if (Features[CSKY::FeatureFPUV2_SF] || Features[CSKY::FeatureFPUV3_SF])
    EFlags |= ELF::EF_CSKY_FLOAT;

  EFlags |= ELF::EF_CSKY_EFV1;

  W.setELFHeaderEFlags(EFlags);
}

MCELFStreamer &CSKYTargetELFStreamer::getStreamer() {
  return static_cast<MCELFStreamer &>(Streamer);
}

void CSKYTargetELFStreamer::emitAttribute(unsigned Attribute, unsigned Value) {
  setAttributeItem(Attribute, Value, /*OverwriteExisting=*/true);
}

void CSKYTargetELFStreamer::emitTextAttribute(unsigned Attribute,
                                              StringRef String) {
  setAttributeItem(Attribute, String, /*OverwriteExisting=*/true);
}

void CSKYTargetELFStreamer::finishAttributeSection() {
  if (Contents.empty())
    return;

  if (AttributeSection) {
    Streamer.switchSection(AttributeSection);
  } else {
    MCAssembler &MCA = getStreamer().getAssembler();
    AttributeSection = MCA.getContext().getELFSection(
        ".csky.attributes", ELF::SHT_CSKY_ATTRIBUTES, 0);
    Streamer.switchSection(AttributeSection);
    Streamer.emitInt8(ELFAttrs::Format_Version);
  }

  // Vendor size + Vendor name + '\0'
  const size_t VendorHeaderSize = 4 + CurrentVendor.size() + 1;

  // Tag + Tag Size
  const size_t TagHeaderSize = 1 + 4;

  const size_t ContentsSize = calculateContentSize();

  Streamer.emitInt32(VendorHeaderSize + TagHeaderSize + ContentsSize);
  Streamer.emitBytes(CurrentVendor);
  Streamer.emitInt8(0); // '\0'

  Streamer.emitInt8(ELFAttrs::File);
  Streamer.emitInt32(TagHeaderSize + ContentsSize);

  // Size should have been accounted for already, now
  // emit each field as its type (ULEB or String).
  for (AttributeItem item : Contents) {
    Streamer.emitULEB128IntValue(item.Tag);
    switch (item.Type) {
    default:
      llvm_unreachable("Invalid attribute type");
    case AttributeType::Numeric:
      Streamer.emitULEB128IntValue(item.IntValue);
      break;
    case AttributeType::Text:
      Streamer.emitBytes(item.StringValue);
      Streamer.emitInt8(0); // '\0'
      break;
    case AttributeType::NumericAndText:
      Streamer.emitULEB128IntValue(item.IntValue);
      Streamer.emitBytes(item.StringValue);
      Streamer.emitInt8(0); // '\0'
      break;
    }
  }

  Contents.clear();
}

size_t CSKYTargetELFStreamer::calculateContentSize() const {
  size_t Result = 0;
  for (AttributeItem item : Contents) {
    switch (item.Type) {
    case AttributeType::Hidden:
      break;
    case AttributeType::Numeric:
      Result += getULEB128Size(item.Tag);
      Result += getULEB128Size(item.IntValue);
      break;
    case AttributeType::Text:
      Result += getULEB128Size(item.Tag);
      Result += item.StringValue.size() + 1; // string + '\0'
      break;
    case AttributeType::NumericAndText:
      Result += getULEB128Size(item.Tag);
      Result += getULEB128Size(item.IntValue);
      Result += item.StringValue.size() + 1; // string + '\0';
      break;
    }
  }
  return Result;
}

void CSKYELFStreamer::EmitMappingSymbol(StringRef Name) {
  if (Name == "$d" && State == EMS_Data)
    return;
  if (Name == "$t" && State == EMS_Text)
    return;
  if (Name == "$t" && State == EMS_None) {
    State = EMS_Text;
    return;
  }

  State = (Name == "$t" ? EMS_Text : EMS_Data);

  auto *Symbol = cast<MCSymbolELF>(getContext().createLocalSymbol(Name));
  emitLabel(Symbol);

  Symbol->setType(ELF::STT_NOTYPE);
  Symbol->setBinding(ELF::STB_LOCAL);
}

void CSKYTargetELFStreamer::emitTargetAttributes(const MCSubtargetInfo &STI) {
  StringRef CPU = STI.getCPU();
  CSKY::ArchKind ArchID = CSKY::parseCPUArch(CPU);

  if (ArchID == CSKY::ArchKind::CK804)
    ArchID = CSKY::ArchKind::CK803;

  StringRef CPU_ARCH = CSKY::getArchName(ArchID);

  if (ArchID == CSKY::ArchKind::INVALID) {
    CPU = "ck810";
    CPU_ARCH = "ck810";
  }
  emitTextAttribute(CSKYAttrs::CSKY_ARCH_NAME, CPU_ARCH);
  emitTextAttribute(CSKYAttrs::CSKY_CPU_NAME, CPU);

  unsigned ISAFlag = 0;
  if (STI.hasFeature(CSKY::HasE1))
    ISAFlag |= CSKYAttrs::V2_ISA_E1;

  if (STI.hasFeature(CSKY::HasE2))
    ISAFlag |= CSKYAttrs::V2_ISA_1E2;

  if (STI.hasFeature(CSKY::Has2E3))
    ISAFlag |= CSKYAttrs::V2_ISA_2E3;

  if (STI.hasFeature(CSKY::HasMP))
    ISAFlag |= CSKYAttrs::ISA_MP;

  if (STI.hasFeature(CSKY::Has3E3r1))
    ISAFlag |= CSKYAttrs::V2_ISA_3E3R1;

  if (STI.hasFeature(CSKY::Has3r1E3r2))
    ISAFlag |= CSKYAttrs::V2_ISA_3E3R2;

  if (STI.hasFeature(CSKY::Has3r2E3r3))
    ISAFlag |= CSKYAttrs::V2_ISA_3E3R3;

  if (STI.hasFeature(CSKY::Has3E7))
    ISAFlag |= CSKYAttrs::V2_ISA_3E7;

  if (STI.hasFeature(CSKY::HasMP1E2))
    ISAFlag |= CSKYAttrs::ISA_MP_1E2;

  if (STI.hasFeature(CSKY::Has7E10))
    ISAFlag |= CSKYAttrs::V2_ISA_7E10;

  if (STI.hasFeature(CSKY::Has10E60))
    ISAFlag |= CSKYAttrs::V2_ISA_10E60;

  if (STI.hasFeature(CSKY::FeatureTrust))
    ISAFlag |= CSKYAttrs::ISA_TRUST;

  if (STI.hasFeature(CSKY::FeatureJAVA))
    ISAFlag |= CSKYAttrs::ISA_JAVA;

  if (STI.hasFeature(CSKY::FeatureCache))
    ISAFlag |= CSKYAttrs::ISA_CACHE;

  if (STI.hasFeature(CSKY::FeatureNVIC))
    ISAFlag |= CSKYAttrs::ISA_NVIC;

  if (STI.hasFeature(CSKY::FeatureDSP))
    ISAFlag |= CSKYAttrs::ISA_DSP;

  if (STI.hasFeature(CSKY::HasDSP1E2))
    ISAFlag |= CSKYAttrs::ISA_DSP_1E2;

  if (STI.hasFeature(CSKY::HasDSPE60))
    ISAFlag |= CSKYAttrs::V2_ISA_DSPE60;

  if (STI.hasFeature(CSKY::FeatureDSPV2))
    ISAFlag |= CSKYAttrs::ISA_DSP_ENHANCE;

  if (STI.hasFeature(CSKY::FeatureDSP_Silan))
    ISAFlag |= CSKYAttrs::ISA_DSP_SILAN;

  if (STI.hasFeature(CSKY::FeatureVDSPV1_128))
    ISAFlag |= CSKYAttrs::ISA_VDSP;

  if (STI.hasFeature(CSKY::FeatureVDSPV2))
    ISAFlag |= CSKYAttrs::ISA_VDSP_2;

  if (STI.hasFeature(CSKY::HasVDSP2E3))
    ISAFlag |= CSKYAttrs::ISA_VDSP_2E3;

  if (STI.hasFeature(CSKY::HasVDSP2E60F))
    ISAFlag |= CSKYAttrs::ISA_VDSP_2E60F;

  emitAttribute(CSKYAttrs::CSKY_ISA_FLAGS, ISAFlag);

  unsigned ISAExtFlag = 0;
  if (STI.hasFeature(CSKY::HasFLOATE1))
    ISAExtFlag |= CSKYAttrs::ISA_FLOAT_E1;

  if (STI.hasFeature(CSKY::HasFLOAT1E2))
    ISAExtFlag |= CSKYAttrs::ISA_FLOAT_1E2;

  if (STI.hasFeature(CSKY::HasFLOAT1E3))
    ISAExtFlag |= CSKYAttrs::ISA_FLOAT_1E3;

  if (STI.hasFeature(CSKY::HasFLOAT3E4))
    ISAExtFlag |= CSKYAttrs::ISA_FLOAT_3E4;

  if (STI.hasFeature(CSKY::HasFLOAT7E60))
    ISAExtFlag |= CSKYAttrs::ISA_FLOAT_7E60;

  emitAttribute(CSKYAttrs::CSKY_ISA_EXT_FLAGS, ISAExtFlag);

  if (STI.hasFeature(CSKY::FeatureDSP))
    emitAttribute(CSKYAttrs::CSKY_DSP_VERSION,
                  CSKYAttrs::DSP_VERSION_EXTENSION);
  if (STI.hasFeature(CSKY::FeatureDSPV2))
    emitAttribute(CSKYAttrs::CSKY_DSP_VERSION, CSKYAttrs::DSP_VERSION_2);

  if (STI.hasFeature(CSKY::FeatureVDSPV2))
    emitAttribute(CSKYAttrs::CSKY_VDSP_VERSION, CSKYAttrs::VDSP_VERSION_2);

  if (STI.hasFeature(CSKY::FeatureFPUV2_SF) ||
      STI.hasFeature(CSKY::FeatureFPUV2_DF))
    emitAttribute(CSKYAttrs::CSKY_FPU_VERSION, CSKYAttrs::FPU_VERSION_2);
  else if (STI.hasFeature(CSKY::FeatureFPUV3_HF) ||
           STI.hasFeature(CSKY::FeatureFPUV3_SF) ||
           STI.hasFeature(CSKY::FeatureFPUV3_DF))
    emitAttribute(CSKYAttrs::CSKY_FPU_VERSION, CSKYAttrs::FPU_VERSION_3);

  bool hasAnyFloatExt = STI.hasFeature(CSKY::FeatureFPUV2_SF) ||
                        STI.hasFeature(CSKY::FeatureFPUV2_DF) ||
                        STI.hasFeature(CSKY::FeatureFPUV3_HF) ||
                        STI.hasFeature(CSKY::FeatureFPUV3_SF) ||
                        STI.hasFeature(CSKY::FeatureFPUV3_DF);

  if (hasAnyFloatExt && STI.hasFeature(CSKY::ModeHardFloat) &&
      STI.hasFeature(CSKY::ModeHardFloatABI))
    emitAttribute(CSKYAttrs::CSKY_FPU_ABI, CSKYAttrs::FPU_ABI_HARD);
  else if (hasAnyFloatExt && STI.hasFeature(CSKY::ModeHardFloat))
    emitAttribute(CSKYAttrs::CSKY_FPU_ABI, CSKYAttrs::FPU_ABI_SOFTFP);
  else
    emitAttribute(CSKYAttrs::CSKY_FPU_ABI, CSKYAttrs::FPU_ABI_SOFT);

  unsigned HardFPFlag = 0;
  if (STI.hasFeature(CSKY::FeatureFPUV3_HF))
    HardFPFlag |= CSKYAttrs::FPU_HARDFP_HALF;
  if (STI.hasFeature(CSKY::FeatureFPUV2_SF) ||
      STI.hasFeature(CSKY::FeatureFPUV3_SF))
    HardFPFlag |= CSKYAttrs::FPU_HARDFP_SINGLE;
  if (STI.hasFeature(CSKY::FeatureFPUV2_DF) ||
      STI.hasFeature(CSKY::FeatureFPUV3_DF))
    HardFPFlag |= CSKYAttrs::FPU_HARDFP_DOUBLE;

  if (HardFPFlag != 0) {
    emitAttribute(CSKYAttrs::CSKY_FPU_DENORMAL, CSKYAttrs::NEEDED);
    emitAttribute(CSKYAttrs::CSKY_FPU_EXCEPTION, CSKYAttrs::NEEDED);
    emitTextAttribute(CSKYAttrs::CSKY_FPU_NUMBER_MODULE, "IEEE 754");
    emitAttribute(CSKYAttrs::CSKY_FPU_HARDFP, HardFPFlag);
  }
}

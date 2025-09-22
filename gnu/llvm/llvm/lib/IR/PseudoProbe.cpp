//===- PseudoProbe.cpp - Pseudo Probe Helpers -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the helpers to manipulate pseudo probe IR intrinsic
// calls.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/PseudoProbe.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/IntrinsicInst.h"

using namespace llvm;

namespace llvm {

std::optional<PseudoProbe>
extractProbeFromDiscriminator(const DILocation *DIL) {
  if (DIL) {
    auto Discriminator = DIL->getDiscriminator();
    if (DILocation::isPseudoProbeDiscriminator(Discriminator)) {
      PseudoProbe Probe;
      Probe.Id =
          PseudoProbeDwarfDiscriminator::extractProbeIndex(Discriminator);
      Probe.Type =
          PseudoProbeDwarfDiscriminator::extractProbeType(Discriminator);
      Probe.Attr =
          PseudoProbeDwarfDiscriminator::extractProbeAttributes(Discriminator);
      Probe.Factor =
          PseudoProbeDwarfDiscriminator::extractProbeFactor(Discriminator) /
          (float)PseudoProbeDwarfDiscriminator::FullDistributionFactor;
      Probe.Discriminator = 0;
      return Probe;
    }
  }
  return std::nullopt;
}

std::optional<PseudoProbe>
extractProbeFromDiscriminator(const Instruction &Inst) {
  assert(isa<CallBase>(&Inst) && !isa<IntrinsicInst>(&Inst) &&
         "Only call instructions should have pseudo probe encodes as their "
         "Dwarf discriminators");
  if (const DebugLoc &DLoc = Inst.getDebugLoc())
    return extractProbeFromDiscriminator(DLoc);
  return std::nullopt;
}

std::optional<PseudoProbe> extractProbe(const Instruction &Inst) {
  if (const auto *II = dyn_cast<PseudoProbeInst>(&Inst)) {
    PseudoProbe Probe;
    Probe.Id = II->getIndex()->getZExtValue();
    Probe.Type = (uint32_t)PseudoProbeType::Block;
    Probe.Attr = II->getAttributes()->getZExtValue();
    Probe.Factor = II->getFactor()->getZExtValue() /
                   (float)PseudoProbeFullDistributionFactor;
    Probe.Discriminator = 0;
    if (const DebugLoc &DLoc = Inst.getDebugLoc())
      Probe.Discriminator = DLoc->getDiscriminator();
    return Probe;
  }

  if (isa<CallBase>(&Inst) && !isa<IntrinsicInst>(&Inst))
    return extractProbeFromDiscriminator(Inst);

  return std::nullopt;
}

void setProbeDistributionFactor(Instruction &Inst, float Factor) {
  assert(Factor >= 0 && Factor <= 1 &&
         "Distribution factor must be in [0, 1.0]");
  if (auto *II = dyn_cast<PseudoProbeInst>(&Inst)) {
    IRBuilder<> Builder(&Inst);
    uint64_t IntFactor = PseudoProbeFullDistributionFactor;
    if (Factor < 1)
      IntFactor *= Factor;
    auto OrigFactor = II->getFactor()->getZExtValue();
    if (IntFactor != OrigFactor)
      II->replaceUsesOfWith(II->getFactor(), Builder.getInt64(IntFactor));
  } else if (isa<CallBase>(&Inst) && !isa<IntrinsicInst>(&Inst)) {
    if (const DebugLoc &DLoc = Inst.getDebugLoc()) {
      const DILocation *DIL = DLoc;
      auto Discriminator = DIL->getDiscriminator();
      if (DILocation::isPseudoProbeDiscriminator(Discriminator)) {
        auto Index =
            PseudoProbeDwarfDiscriminator::extractProbeIndex(Discriminator);
        auto Type =
            PseudoProbeDwarfDiscriminator::extractProbeType(Discriminator);
        auto Attr = PseudoProbeDwarfDiscriminator::extractProbeAttributes(
            Discriminator);
        auto DwarfBaseDiscriminator =
            PseudoProbeDwarfDiscriminator::extractDwarfBaseDiscriminator(
                Discriminator);
        // Round small factors to 0 to avoid over-counting.
        uint32_t IntFactor =
            PseudoProbeDwarfDiscriminator::FullDistributionFactor;
        if (Factor < 1)
          IntFactor *= Factor;
        uint32_t V = PseudoProbeDwarfDiscriminator::packProbeData(
            Index, Type, Attr, IntFactor, DwarfBaseDiscriminator);
        DIL = DIL->cloneWithDiscriminator(V);
        Inst.setDebugLoc(DIL);
      }
    }
  }
}

} // namespace llvm

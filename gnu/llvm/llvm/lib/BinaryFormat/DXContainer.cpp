
//===-- llvm/BinaryFormat/DXContainer.cpp - DXContainer Utils ----*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains utility functions for working with DXContainers.
//
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/DXContainer.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/ScopedPrinter.h"

using namespace llvm;
using namespace llvm::dxbc;

dxbc::PartType dxbc::parsePartType(StringRef S) {
#define CONTAINER_PART(PartName) .Case(#PartName, PartType::PartName)
  return StringSwitch<dxbc::PartType>(S)
#include "llvm/BinaryFormat/DXContainerConstants.def"
      .Default(dxbc::PartType::Unknown);
}

bool ShaderHash::isPopulated() {
  static uint8_t Zeros[16] = {0};
  return Flags > 0 || 0 != memcmp(&Digest, &Zeros, 16);
}

#define COMPONENT_PRECISION(Val, Enum) {#Enum, SigMinPrecision::Enum},

static const EnumEntry<SigMinPrecision> SigMinPrecisionNames[] = {
#include "llvm/BinaryFormat/DXContainerConstants.def"
};

ArrayRef<EnumEntry<SigMinPrecision>> dxbc::getSigMinPrecisions() {
  return ArrayRef(SigMinPrecisionNames);
}

#define D3D_SYSTEM_VALUE(Val, Enum) {#Enum, D3DSystemValue::Enum},

static const EnumEntry<D3DSystemValue> D3DSystemValueNames[] = {
#include "llvm/BinaryFormat/DXContainerConstants.def"
};

ArrayRef<EnumEntry<D3DSystemValue>> dxbc::getD3DSystemValues() {
  return ArrayRef(D3DSystemValueNames);
}

#define COMPONENT_TYPE(Val, Enum) {#Enum, SigComponentType::Enum},

static const EnumEntry<SigComponentType> SigComponentTypes[] = {
#include "llvm/BinaryFormat/DXContainerConstants.def"
};

ArrayRef<EnumEntry<SigComponentType>> dxbc::getSigComponentTypes() {
  return ArrayRef(SigComponentTypes);
}

#define SEMANTIC_KIND(Val, Enum) {#Enum, PSV::SemanticKind::Enum},

static const EnumEntry<PSV::SemanticKind> SemanticKindNames[] = {
#include "llvm/BinaryFormat/DXContainerConstants.def"
};

ArrayRef<EnumEntry<PSV::SemanticKind>> PSV::getSemanticKinds() {
  return ArrayRef(SemanticKindNames);
}

#define COMPONENT_TYPE(Val, Enum) {#Enum, PSV::ComponentType::Enum},

static const EnumEntry<PSV::ComponentType> ComponentTypeNames[] = {
#include "llvm/BinaryFormat/DXContainerConstants.def"
};

ArrayRef<EnumEntry<PSV::ComponentType>> PSV::getComponentTypes() {
  return ArrayRef(ComponentTypeNames);
}

#define INTERPOLATION_MODE(Val, Enum) {#Enum, PSV::InterpolationMode::Enum},

static const EnumEntry<PSV::InterpolationMode> InterpolationModeNames[] = {
#include "llvm/BinaryFormat/DXContainerConstants.def"
};

ArrayRef<EnumEntry<PSV::InterpolationMode>> PSV::getInterpolationModes() {
  return ArrayRef(InterpolationModeNames);
}

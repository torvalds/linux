//===-- MSP430Attributes.h - MSP430 Attributes ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===-----------------------------------------------------------------------===//
///
/// \file
/// This file contains enumerations for MSP430 ELF build attributes as
/// defined in the MSP430 ELF psABI specification.
///
/// MSP430 ELF psABI specification
///
/// https://www.ti.com/lit/pdf/slaa534
///
//===----------------------------------------------------------------------===//
#ifndef LLVM_SUPPORT_MSP430ATTRIBUTES_H
#define LLVM_SUPPORT_MSP430ATTRIBUTES_H

#include "llvm/Support/ELFAttributes.h"

namespace llvm {
namespace MSP430Attrs {

const TagNameMap &getMSP430AttributeTags();

enum AttrType : unsigned {
  // Attribute types in ELF/.MSP430.attributes.
  TagISA = 4,
  TagCodeModel = 6,
  TagDataModel = 8,
  TagEnumSize = 10
};

enum ISA { ISAMSP430 = 1, ISAMSP430X = 2 };
enum CodeModel { CMSmall = 1, CMLarge = 2 };
enum DataModel { DMSmall = 1, DMLarge = 2, DMRestricted = 3 };
enum EnumSize { ESSmall = 1, ESInteger = 2, ESDontCare = 3 };

} // namespace MSP430Attrs
} // namespace llvm

#endif

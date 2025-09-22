//===-- SlotMapping.h - Slot number mapping for unnamed values --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the SlotMapping struct.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ASMPARSER_SLOTMAPPING_H
#define LLVM_ASMPARSER_SLOTMAPPING_H

#include "llvm/ADT/StringMap.h"
#include "llvm/AsmParser/NumberedValues.h"
#include "llvm/IR/TrackingMDRef.h"
#include <map>
#include <vector>

namespace llvm {

class GlobalValue;
class Type;

/// This struct contains the mappings from the slot numbers to unnamed metadata
/// nodes, global values and types. It also contains the mapping for the named
/// types.
/// It can be used to save the parsing state of an LLVM IR module so that the
/// textual references to the values in the module can be parsed outside of the
/// module's source.
struct SlotMapping {
  NumberedValues<GlobalValue *> GlobalValues;
  std::map<unsigned, TrackingMDNodeRef> MetadataNodes;
  StringMap<Type *> NamedTypes;
  std::map<unsigned, Type *> Types;
};

} // end namespace llvm

#endif

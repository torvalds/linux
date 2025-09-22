//=----------- ELFLinkGraphBuilder.cpp - ELF LinkGraph builder ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic ELF LinkGraph building code.
//
//===----------------------------------------------------------------------===//

#include "ELFLinkGraphBuilder.h"

#define DEBUG_TYPE "jitlink"

static const char *DWSecNames[] = {
#define HANDLE_DWARF_SECTION(ENUM_NAME, ELF_NAME, CMDLINE_NAME, OPTION)        \
  ELF_NAME,
#include "llvm/BinaryFormat/Dwarf.def"
#undef HANDLE_DWARF_SECTION
};

namespace llvm {
namespace jitlink {

StringRef ELFLinkGraphBuilderBase::CommonSectionName(".common");
ArrayRef<const char *> ELFLinkGraphBuilderBase::DwarfSectionNames = DWSecNames;

ELFLinkGraphBuilderBase::~ELFLinkGraphBuilderBase() = default;

} // end namespace jitlink
} // end namespace llvm

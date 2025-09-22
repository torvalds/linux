//=== DWARFLinkerBase.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DWARFLinker/DWARFLinkerBase.h"
#include "llvm/ADT/StringSwitch.h"

using namespace llvm;
using namespace llvm::dwarf_linker;

std::optional<DebugSectionKind>
llvm::dwarf_linker::parseDebugTableName(llvm::StringRef SecName) {
  return llvm::StringSwitch<std::optional<DebugSectionKind>>(
             SecName.substr(SecName.find_first_not_of("._")))
      .Case(getSectionName(DebugSectionKind::DebugInfo),
            DebugSectionKind::DebugInfo)
      .Case(getSectionName(DebugSectionKind::DebugLine),
            DebugSectionKind::DebugLine)
      .Case(getSectionName(DebugSectionKind::DebugFrame),
            DebugSectionKind::DebugFrame)
      .Case(getSectionName(DebugSectionKind::DebugRange),
            DebugSectionKind::DebugRange)
      .Case(getSectionName(DebugSectionKind::DebugRngLists),
            DebugSectionKind::DebugRngLists)
      .Case(getSectionName(DebugSectionKind::DebugLoc),
            DebugSectionKind::DebugLoc)
      .Case(getSectionName(DebugSectionKind::DebugLocLists),
            DebugSectionKind::DebugLocLists)
      .Case(getSectionName(DebugSectionKind::DebugARanges),
            DebugSectionKind::DebugARanges)
      .Case(getSectionName(DebugSectionKind::DebugAbbrev),
            DebugSectionKind::DebugAbbrev)
      .Case(getSectionName(DebugSectionKind::DebugMacinfo),
            DebugSectionKind::DebugMacinfo)
      .Case(getSectionName(DebugSectionKind::DebugMacro),
            DebugSectionKind::DebugMacro)
      .Case(getSectionName(DebugSectionKind::DebugAddr),
            DebugSectionKind::DebugAddr)
      .Case(getSectionName(DebugSectionKind::DebugStr),
            DebugSectionKind::DebugStr)
      .Case(getSectionName(DebugSectionKind::DebugLineStr),
            DebugSectionKind::DebugLineStr)
      .Case(getSectionName(DebugSectionKind::DebugStrOffsets),
            DebugSectionKind::DebugStrOffsets)
      .Case(getSectionName(DebugSectionKind::DebugPubNames),
            DebugSectionKind::DebugPubNames)
      .Case(getSectionName(DebugSectionKind::DebugPubTypes),
            DebugSectionKind::DebugPubTypes)
      .Case(getSectionName(DebugSectionKind::DebugNames),
            DebugSectionKind::DebugNames)
      .Case(getSectionName(DebugSectionKind::AppleNames),
            DebugSectionKind::AppleNames)
      .Case(getSectionName(DebugSectionKind::AppleNamespaces),
            DebugSectionKind::AppleNamespaces)
      .Case(getSectionName(DebugSectionKind::AppleObjC),
            DebugSectionKind::AppleObjC)
      .Case(getSectionName(DebugSectionKind::AppleTypes),
            DebugSectionKind::AppleTypes)
      .Default(std::nullopt);
}

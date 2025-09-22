//===- ConfigManager.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ObjCopy/ConfigManager.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace objcopy {

Expected<const COFFConfig &> ConfigManager::getCOFFConfig() const {
  if (!Common.SplitDWO.empty() || !Common.SymbolsPrefix.empty() ||
      !Common.SymbolsPrefixRemove.empty() || !Common.SymbolsToSkip.empty() ||
      !Common.AllocSectionsPrefix.empty() || !Common.KeepSection.empty() ||
      !Common.SymbolsToGlobalize.empty() || !Common.SymbolsToKeep.empty() ||
      !Common.SymbolsToLocalize.empty() || !Common.SymbolsToWeaken.empty() ||
      !Common.SymbolsToKeepGlobal.empty() || !Common.SectionsToRename.empty() ||
      !Common.SetSectionAlignment.empty() || !Common.SetSectionType.empty() ||
      Common.ExtractDWO || Common.PreserveDates || Common.StripDWO ||
      Common.StripNonAlloc || Common.StripSections || Common.Weaken ||
      Common.DecompressDebugSections ||
      Common.DiscardMode == DiscardType::Locals ||
      !Common.SymbolsToAdd.empty() || Common.GapFill != 0 ||
      Common.PadTo != 0 || Common.ChangeSectionLMAValAll != 0)
    return createStringError(llvm::errc::invalid_argument,
                             "option is not supported for COFF");

  return COFF;
}

Expected<const MachOConfig &> ConfigManager::getMachOConfig() const {
  if (!Common.SplitDWO.empty() || !Common.SymbolsPrefix.empty() ||
      !Common.SymbolsPrefixRemove.empty() || !Common.SymbolsToSkip.empty() ||
      !Common.AllocSectionsPrefix.empty() || !Common.KeepSection.empty() ||
      !Common.SymbolsToGlobalize.empty() || !Common.SymbolsToKeep.empty() ||
      !Common.SymbolsToLocalize.empty() ||
      !Common.SymbolsToKeepGlobal.empty() || !Common.SectionsToRename.empty() ||
      !Common.UnneededSymbolsToRemove.empty() ||
      !Common.SetSectionAlignment.empty() || !Common.SetSectionFlags.empty() ||
      !Common.SetSectionType.empty() || Common.ExtractDWO ||
      Common.PreserveDates || Common.StripAllGNU || Common.StripDWO ||
      Common.StripNonAlloc || Common.StripSections ||
      Common.DecompressDebugSections || Common.StripUnneeded ||
      Common.DiscardMode == DiscardType::Locals ||
      !Common.SymbolsToAdd.empty() || Common.GapFill != 0 ||
      Common.PadTo != 0 || Common.ChangeSectionLMAValAll != 0)
    return createStringError(llvm::errc::invalid_argument,
                             "option is not supported for MachO");

  return MachO;
}

Expected<const WasmConfig &> ConfigManager::getWasmConfig() const {
  if (!Common.AddGnuDebugLink.empty() || Common.ExtractPartition ||
      !Common.SplitDWO.empty() || !Common.SymbolsPrefix.empty() ||
      !Common.SymbolsPrefixRemove.empty() || !Common.SymbolsToSkip.empty() ||
      !Common.AllocSectionsPrefix.empty() ||
      Common.DiscardMode != DiscardType::None || !Common.SymbolsToAdd.empty() ||
      !Common.SymbolsToGlobalize.empty() || !Common.SymbolsToLocalize.empty() ||
      !Common.SymbolsToKeep.empty() || !Common.SymbolsToRemove.empty() ||
      !Common.UnneededSymbolsToRemove.empty() ||
      !Common.SymbolsToWeaken.empty() || !Common.SymbolsToKeepGlobal.empty() ||
      !Common.SectionsToRename.empty() || !Common.SetSectionAlignment.empty() ||
      !Common.SetSectionFlags.empty() || !Common.SetSectionType.empty() ||
      !Common.SymbolsToRename.empty() || Common.GapFill != 0 ||
      Common.PadTo != 0 || Common.ChangeSectionLMAValAll != 0)
    return createStringError(llvm::errc::invalid_argument,
                             "only flags for section dumping, removal, and "
                             "addition are supported");

  return Wasm;
}

Expected<const XCOFFConfig &> ConfigManager::getXCOFFConfig() const {
  if (!Common.AddGnuDebugLink.empty() || Common.ExtractPartition ||
      !Common.SplitDWO.empty() || !Common.SymbolsPrefix.empty() ||
      !Common.SymbolsPrefixRemove.empty() || !Common.SymbolsToSkip.empty() ||
      !Common.AllocSectionsPrefix.empty() ||
      Common.DiscardMode != DiscardType::None || !Common.AddSection.empty() ||
      !Common.DumpSection.empty() || !Common.SymbolsToAdd.empty() ||
      !Common.KeepSection.empty() || !Common.OnlySection.empty() ||
      !Common.ToRemove.empty() || !Common.SymbolsToGlobalize.empty() ||
      !Common.SymbolsToKeep.empty() || !Common.SymbolsToLocalize.empty() ||
      !Common.SymbolsToRemove.empty() ||
      !Common.UnneededSymbolsToRemove.empty() ||
      !Common.SymbolsToWeaken.empty() || !Common.SymbolsToKeepGlobal.empty() ||
      !Common.SectionsToRename.empty() || !Common.SetSectionAlignment.empty() ||
      !Common.SetSectionFlags.empty() || !Common.SetSectionType.empty() ||
      !Common.SymbolsToRename.empty() || Common.ExtractDWO ||
      Common.ExtractMainPartition || Common.OnlyKeepDebug ||
      Common.PreserveDates || Common.StripAllGNU || Common.StripDWO ||
      Common.StripDebug || Common.StripNonAlloc || Common.StripSections ||
      Common.Weaken || Common.StripUnneeded || Common.DecompressDebugSections ||
      Common.GapFill != 0 || Common.PadTo != 0 ||
      Common.ChangeSectionLMAValAll != 0) {
    return createStringError(
        llvm::errc::invalid_argument,
        "no flags are supported yet, only basic copying is allowed");
  }

  return XCOFF;
}

} // end namespace objcopy
} // end namespace llvm

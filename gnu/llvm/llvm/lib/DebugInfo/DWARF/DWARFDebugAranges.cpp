//===- DWARFDebugAranges.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARF/DWARFDebugAranges.h"
#include "llvm/DebugInfo/DWARF/DWARFAddressRange.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDataExtractor.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugArangeSet.h"
#include "llvm/DebugInfo/DWARF/DWARFObject.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <set>

using namespace llvm;

void DWARFDebugAranges::extract(
    DWARFDataExtractor DebugArangesData,
    function_ref<void(Error)> RecoverableErrorHandler,
    function_ref<void(Error)> WarningHandler) {
  if (!DebugArangesData.isValidOffset(0))
    return;
  uint64_t Offset = 0;
  DWARFDebugArangeSet Set;

  while (DebugArangesData.isValidOffset(Offset)) {
    if (Error E = Set.extract(DebugArangesData, &Offset, WarningHandler)) {
      RecoverableErrorHandler(std::move(E));
      return;
    }
    uint64_t CUOffset = Set.getCompileUnitDIEOffset();
    for (const auto &Desc : Set.descriptors()) {
      uint64_t LowPC = Desc.Address;
      uint64_t HighPC = Desc.getEndAddress();
      appendRange(CUOffset, LowPC, HighPC);
    }
    ParsedCUOffsets.insert(CUOffset);
  }
}

void DWARFDebugAranges::generate(DWARFContext *CTX) {
  clear();
  if (!CTX)
    return;

  // Extract aranges from .debug_aranges section.
  DWARFDataExtractor ArangesData(CTX->getDWARFObj().getArangesSection(),
                                 CTX->isLittleEndian(), 0);
  extract(ArangesData, CTX->getRecoverableErrorHandler(),
          CTX->getWarningHandler());

  // Generate aranges from DIEs: even if .debug_aranges section is present,
  // it may describe only a small subset of compilation units, so we need to
  // manually build aranges for the rest of them.
  for (const auto &CU : CTX->compile_units()) {
    uint64_t CUOffset = CU->getOffset();
    if (ParsedCUOffsets.insert(CUOffset).second) {
      Expected<DWARFAddressRangesVector> CURanges = CU->collectAddressRanges();
      if (!CURanges)
        CTX->getRecoverableErrorHandler()(CURanges.takeError());
      else
        for (const auto &R : *CURanges)
          appendRange(CUOffset, R.LowPC, R.HighPC);
    }
  }

  construct();
}

void DWARFDebugAranges::clear() {
  Endpoints.clear();
  Aranges.clear();
  ParsedCUOffsets.clear();
}

void DWARFDebugAranges::appendRange(uint64_t CUOffset, uint64_t LowPC,
                                    uint64_t HighPC) {
  if (LowPC >= HighPC)
    return;
  Endpoints.emplace_back(LowPC, CUOffset, true);
  Endpoints.emplace_back(HighPC, CUOffset, false);
}

void DWARFDebugAranges::construct() {
  std::multiset<uint64_t> ValidCUs;  // Maintain the set of CUs describing
                                     // a current address range.
  llvm::sort(Endpoints);
  uint64_t PrevAddress = -1ULL;
  for (const auto &E : Endpoints) {
    if (PrevAddress < E.Address && !ValidCUs.empty()) {
      // If the address range between two endpoints is described by some
      // CU, first try to extend the last range in Aranges. If we can't
      // do it, start a new range.
      if (!Aranges.empty() && Aranges.back().HighPC() == PrevAddress &&
          ValidCUs.find(Aranges.back().CUOffset) != ValidCUs.end()) {
        Aranges.back().setHighPC(E.Address);
      } else {
        Aranges.emplace_back(PrevAddress, E.Address, *ValidCUs.begin());
      }
    }
    // Update the set of valid CUs.
    if (E.IsRangeStart) {
      ValidCUs.insert(E.CUOffset);
    } else {
      auto CUPos = ValidCUs.find(E.CUOffset);
      assert(CUPos != ValidCUs.end());
      ValidCUs.erase(CUPos);
    }
    PrevAddress = E.Address;
  }
  assert(ValidCUs.empty());

  // Endpoints are not needed now.
  Endpoints.clear();
  Endpoints.shrink_to_fit();
}

uint64_t DWARFDebugAranges::findAddress(uint64_t Address) const {
  RangeCollIterator It =
      partition_point(Aranges, [=](Range R) { return R.HighPC() <= Address; });
  if (It != Aranges.end() && It->LowPC <= Address)
    return It->CUOffset;
  return -1ULL;
}

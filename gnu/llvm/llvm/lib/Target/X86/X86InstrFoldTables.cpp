//===-- X86InstrFoldTables.cpp - X86 Instruction Folding Tables -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the X86 memory folding tables.
//
//===----------------------------------------------------------------------===//

#include "X86InstrFoldTables.h"
#include "X86InstrInfo.h"
#include "llvm/ADT/STLExtras.h"
#include <atomic>
#include <vector>

using namespace llvm;

// These tables are sorted by their RegOp value allowing them to be binary
// searched at runtime without the need for additional storage. The enum values
// are currently emitted in X86GenInstrInfo.inc in alphabetical order. Which
// makes sorting these tables a simple matter of alphabetizing the table.
#include "X86GenFoldTables.inc"

// Table to map instructions safe to broadcast using a different width from the
// element width.
static const X86FoldTableEntry BroadcastSizeTable2[] = {
  { X86::VANDNPDZ128rr,        X86::VANDNPSZ128rmb,       TB_BCAST_SS },
  { X86::VANDNPDZ256rr,        X86::VANDNPSZ256rmb,       TB_BCAST_SS },
  { X86::VANDNPDZrr,           X86::VANDNPSZrmb,          TB_BCAST_SS },
  { X86::VANDNPSZ128rr,        X86::VANDNPDZ128rmb,       TB_BCAST_SD },
  { X86::VANDNPSZ256rr,        X86::VANDNPDZ256rmb,       TB_BCAST_SD },
  { X86::VANDNPSZrr,           X86::VANDNPDZrmb,          TB_BCAST_SD },
  { X86::VANDPDZ128rr,         X86::VANDPSZ128rmb,        TB_BCAST_SS },
  { X86::VANDPDZ256rr,         X86::VANDPSZ256rmb,        TB_BCAST_SS },
  { X86::VANDPDZrr,            X86::VANDPSZrmb,           TB_BCAST_SS },
  { X86::VANDPSZ128rr,         X86::VANDPDZ128rmb,        TB_BCAST_SD },
  { X86::VANDPSZ256rr,         X86::VANDPDZ256rmb,        TB_BCAST_SD },
  { X86::VANDPSZrr,            X86::VANDPDZrmb,           TB_BCAST_SD },
  { X86::VORPDZ128rr,          X86::VORPSZ128rmb,         TB_BCAST_SS },
  { X86::VORPDZ256rr,          X86::VORPSZ256rmb,         TB_BCAST_SS },
  { X86::VORPDZrr,             X86::VORPSZrmb,            TB_BCAST_SS },
  { X86::VORPSZ128rr,          X86::VORPDZ128rmb,         TB_BCAST_SD },
  { X86::VORPSZ256rr,          X86::VORPDZ256rmb,         TB_BCAST_SD },
  { X86::VORPSZrr,             X86::VORPDZrmb,            TB_BCAST_SD },
  { X86::VPANDDZ128rr,         X86::VPANDQZ128rmb,        TB_BCAST_Q },
  { X86::VPANDDZ256rr,         X86::VPANDQZ256rmb,        TB_BCAST_Q },
  { X86::VPANDDZrr,            X86::VPANDQZrmb,           TB_BCAST_Q },
  { X86::VPANDNDZ128rr,        X86::VPANDNQZ128rmb,       TB_BCAST_Q },
  { X86::VPANDNDZ256rr,        X86::VPANDNQZ256rmb,       TB_BCAST_Q },
  { X86::VPANDNDZrr,           X86::VPANDNQZrmb,          TB_BCAST_Q },
  { X86::VPANDNQZ128rr,        X86::VPANDNDZ128rmb,       TB_BCAST_D },
  { X86::VPANDNQZ256rr,        X86::VPANDNDZ256rmb,       TB_BCAST_D },
  { X86::VPANDNQZrr,           X86::VPANDNDZrmb,          TB_BCAST_D },
  { X86::VPANDQZ128rr,         X86::VPANDDZ128rmb,        TB_BCAST_D },
  { X86::VPANDQZ256rr,         X86::VPANDDZ256rmb,        TB_BCAST_D },
  { X86::VPANDQZrr,            X86::VPANDDZrmb,           TB_BCAST_D },
  { X86::VPORDZ128rr,          X86::VPORQZ128rmb,         TB_BCAST_Q },
  { X86::VPORDZ256rr,          X86::VPORQZ256rmb,         TB_BCAST_Q },
  { X86::VPORDZrr,             X86::VPORQZrmb,            TB_BCAST_Q },
  { X86::VPORQZ128rr,          X86::VPORDZ128rmb,         TB_BCAST_D },
  { X86::VPORQZ256rr,          X86::VPORDZ256rmb,         TB_BCAST_D },
  { X86::VPORQZrr,             X86::VPORDZrmb,            TB_BCAST_D },
  { X86::VPXORDZ128rr,         X86::VPXORQZ128rmb,        TB_BCAST_Q },
  { X86::VPXORDZ256rr,         X86::VPXORQZ256rmb,        TB_BCAST_Q },
  { X86::VPXORDZrr,            X86::VPXORQZrmb,           TB_BCAST_Q },
  { X86::VPXORQZ128rr,         X86::VPXORDZ128rmb,        TB_BCAST_D },
  { X86::VPXORQZ256rr,         X86::VPXORDZ256rmb,        TB_BCAST_D },
  { X86::VPXORQZrr,            X86::VPXORDZrmb,           TB_BCAST_D },
  { X86::VXORPDZ128rr,         X86::VXORPSZ128rmb,        TB_BCAST_SS },
  { X86::VXORPDZ256rr,         X86::VXORPSZ256rmb,        TB_BCAST_SS },
  { X86::VXORPDZrr,            X86::VXORPSZrmb,           TB_BCAST_SS },
  { X86::VXORPSZ128rr,         X86::VXORPDZ128rmb,        TB_BCAST_SD },
  { X86::VXORPSZ256rr,         X86::VXORPDZ256rmb,        TB_BCAST_SD },
  { X86::VXORPSZrr,            X86::VXORPDZrmb,           TB_BCAST_SD },
};

static const X86FoldTableEntry BroadcastSizeTable3[] = {
  { X86::VPTERNLOGDZ128rri,    X86::VPTERNLOGQZ128rmbi,   TB_BCAST_Q },
  { X86::VPTERNLOGDZ256rri,    X86::VPTERNLOGQZ256rmbi,   TB_BCAST_Q },
  { X86::VPTERNLOGDZrri,       X86::VPTERNLOGQZrmbi,      TB_BCAST_Q },
  { X86::VPTERNLOGQZ128rri,    X86::VPTERNLOGDZ128rmbi,   TB_BCAST_D },
  { X86::VPTERNLOGQZ256rri,    X86::VPTERNLOGDZ256rmbi,   TB_BCAST_D },
  { X86::VPTERNLOGQZrri,       X86::VPTERNLOGDZrmbi,      TB_BCAST_D },
};

static const X86FoldTableEntry *
lookupFoldTableImpl(ArrayRef<X86FoldTableEntry> Table, unsigned RegOp) {
#ifndef NDEBUG
#define CHECK_SORTED_UNIQUE(TABLE)                                             \
  assert(llvm::is_sorted(TABLE) && #TABLE " is not sorted");                   \
  assert(std::adjacent_find(std::begin(Table), std::end(Table)) ==             \
             std::end(Table) &&                                                \
         #TABLE " is not unique");

  // Make sure the tables are sorted.
  static std::atomic<bool> FoldTablesChecked(false);
  if (!FoldTablesChecked.load(std::memory_order_relaxed)) {
    CHECK_SORTED_UNIQUE(Table2Addr)
    CHECK_SORTED_UNIQUE(Table0)
    CHECK_SORTED_UNIQUE(Table1)
    CHECK_SORTED_UNIQUE(Table2)
    CHECK_SORTED_UNIQUE(Table3)
    CHECK_SORTED_UNIQUE(Table4)
    CHECK_SORTED_UNIQUE(BroadcastTable1)
    CHECK_SORTED_UNIQUE(BroadcastTable2)
    CHECK_SORTED_UNIQUE(BroadcastTable3)
    CHECK_SORTED_UNIQUE(BroadcastTable4)
    CHECK_SORTED_UNIQUE(BroadcastSizeTable2)
    CHECK_SORTED_UNIQUE(BroadcastSizeTable3)
    FoldTablesChecked.store(true, std::memory_order_relaxed);
  }
#endif

  const X86FoldTableEntry *Data = llvm::lower_bound(Table, RegOp);
  if (Data != Table.end() && Data->KeyOp == RegOp &&
      !(Data->Flags & TB_NO_FORWARD))
    return Data;
  return nullptr;
}

const X86FoldTableEntry *llvm::lookupTwoAddrFoldTable(unsigned RegOp) {
  return lookupFoldTableImpl(Table2Addr, RegOp);
}

const X86FoldTableEntry *llvm::lookupFoldTable(unsigned RegOp, unsigned OpNum) {
  ArrayRef<X86FoldTableEntry> FoldTable;
  if (OpNum == 0)
    FoldTable = ArrayRef(Table0);
  else if (OpNum == 1)
    FoldTable = ArrayRef(Table1);
  else if (OpNum == 2)
    FoldTable = ArrayRef(Table2);
  else if (OpNum == 3)
    FoldTable = ArrayRef(Table3);
  else if (OpNum == 4)
    FoldTable = ArrayRef(Table4);
  else
    return nullptr;

  return lookupFoldTableImpl(FoldTable, RegOp);
}

const X86FoldTableEntry *llvm::lookupBroadcastFoldTable(unsigned RegOp,
                                                        unsigned OpNum) {
  ArrayRef<X86FoldTableEntry> FoldTable;
  if (OpNum == 1)
    FoldTable = ArrayRef(BroadcastTable1);
  else if (OpNum == 2)
    FoldTable = ArrayRef(BroadcastTable2);
  else if (OpNum == 3)
    FoldTable = ArrayRef(BroadcastTable3);
  else if (OpNum == 4)
    FoldTable = ArrayRef(BroadcastTable4);
  else
    return nullptr;

  return lookupFoldTableImpl(FoldTable, RegOp);
}

namespace {

// This class stores the memory unfolding tables. It is instantiated as a
// function scope static variable to lazily init the unfolding table.
struct X86MemUnfoldTable {
  // Stores memory unfolding tables entries sorted by opcode.
  std::vector<X86FoldTableEntry> Table;

  X86MemUnfoldTable() {
    for (const X86FoldTableEntry &Entry : Table2Addr)
      // Index 0, folded load and store, no alignment requirement.
      addTableEntry(Entry, TB_INDEX_0 | TB_FOLDED_LOAD | TB_FOLDED_STORE);

    for (const X86FoldTableEntry &Entry : Table0)
      // Index 0, mix of loads and stores.
      addTableEntry(Entry, TB_INDEX_0);

    for (const X86FoldTableEntry &Entry : Table1)
      // Index 1, folded load
      addTableEntry(Entry, TB_INDEX_1 | TB_FOLDED_LOAD);

    for (const X86FoldTableEntry &Entry : Table2)
      // Index 2, folded load
      addTableEntry(Entry, TB_INDEX_2 | TB_FOLDED_LOAD);

    for (const X86FoldTableEntry &Entry : Table3)
      // Index 3, folded load
      addTableEntry(Entry, TB_INDEX_3 | TB_FOLDED_LOAD);

    for (const X86FoldTableEntry &Entry : Table4)
      // Index 4, folded load
      addTableEntry(Entry, TB_INDEX_4 | TB_FOLDED_LOAD);

    // Broadcast tables.
    for (const X86FoldTableEntry &Entry : BroadcastTable1)
      // Index 1, folded broadcast
      addTableEntry(Entry, TB_INDEX_1 | TB_FOLDED_LOAD);

    for (const X86FoldTableEntry &Entry : BroadcastTable2)
      // Index 2, folded broadcast
      addTableEntry(Entry, TB_INDEX_2 | TB_FOLDED_LOAD);

    for (const X86FoldTableEntry &Entry : BroadcastTable3)
      // Index 3, folded broadcast
      addTableEntry(Entry, TB_INDEX_3 | TB_FOLDED_LOAD);

    for (const X86FoldTableEntry &Entry : BroadcastTable4)
      // Index 4, folded broadcast
      addTableEntry(Entry, TB_INDEX_4 | TB_FOLDED_LOAD);

    // Sort the memory->reg unfold table.
    array_pod_sort(Table.begin(), Table.end());

    // Now that it's sorted, ensure its unique.
    assert(std::adjacent_find(Table.begin(), Table.end()) == Table.end() &&
           "Memory unfolding table is not unique!");
  }

  void addTableEntry(const X86FoldTableEntry &Entry, uint16_t ExtraFlags) {
    // NOTE: This swaps the KeyOp and DstOp in the table so we can sort it.
    if ((Entry.Flags & TB_NO_REVERSE) == 0)
      Table.push_back({Entry.DstOp, Entry.KeyOp,
                       static_cast<uint16_t>(Entry.Flags | ExtraFlags)});
  }
};
} // namespace

const X86FoldTableEntry *llvm::lookupUnfoldTable(unsigned MemOp) {
  static X86MemUnfoldTable MemUnfoldTable;
  auto &Table = MemUnfoldTable.Table;
  auto I = llvm::lower_bound(Table, MemOp);
  if (I != Table.end() && I->KeyOp == MemOp)
    return &*I;
  return nullptr;
}

namespace {

// This class stores the memory -> broadcast folding tables. It is instantiated
// as a function scope static variable to lazily init the folding table.
struct X86BroadcastFoldTable {
  // Stores memory broadcast folding tables entries sorted by opcode.
  std::vector<X86FoldTableEntry> Table;

  X86BroadcastFoldTable() {
    // Broadcast tables.
    for (const X86FoldTableEntry &Reg2Bcst : BroadcastTable2) {
      unsigned RegOp = Reg2Bcst.KeyOp;
      unsigned BcstOp = Reg2Bcst.DstOp;
      if (const X86FoldTableEntry *Reg2Mem = lookupFoldTable(RegOp, 2)) {
        unsigned MemOp = Reg2Mem->DstOp;
        uint16_t Flags =
            Reg2Mem->Flags | Reg2Bcst.Flags | TB_INDEX_2 | TB_FOLDED_LOAD;
        Table.push_back({MemOp, BcstOp, Flags});
      }
    }
    for (const X86FoldTableEntry &Reg2Bcst : BroadcastSizeTable2) {
      unsigned RegOp = Reg2Bcst.KeyOp;
      unsigned BcstOp = Reg2Bcst.DstOp;
      if (const X86FoldTableEntry *Reg2Mem = lookupFoldTable(RegOp, 2)) {
        unsigned MemOp = Reg2Mem->DstOp;
        uint16_t Flags =
            Reg2Mem->Flags | Reg2Bcst.Flags | TB_INDEX_2 | TB_FOLDED_LOAD;
        Table.push_back({MemOp, BcstOp, Flags});
      }
    }

    for (const X86FoldTableEntry &Reg2Bcst : BroadcastTable3) {
      unsigned RegOp = Reg2Bcst.KeyOp;
      unsigned BcstOp = Reg2Bcst.DstOp;
      if (const X86FoldTableEntry *Reg2Mem = lookupFoldTable(RegOp, 3)) {
        unsigned MemOp = Reg2Mem->DstOp;
        uint16_t Flags =
            Reg2Mem->Flags | Reg2Bcst.Flags | TB_INDEX_3 | TB_FOLDED_LOAD;
        Table.push_back({MemOp, BcstOp, Flags});
      }
    }
    for (const X86FoldTableEntry &Reg2Bcst : BroadcastSizeTable3) {
      unsigned RegOp = Reg2Bcst.KeyOp;
      unsigned BcstOp = Reg2Bcst.DstOp;
      if (const X86FoldTableEntry *Reg2Mem = lookupFoldTable(RegOp, 3)) {
        unsigned MemOp = Reg2Mem->DstOp;
        uint16_t Flags =
            Reg2Mem->Flags | Reg2Bcst.Flags | TB_INDEX_3 | TB_FOLDED_LOAD;
        Table.push_back({MemOp, BcstOp, Flags});
      }
    }

    for (const X86FoldTableEntry &Reg2Bcst : BroadcastTable4) {
      unsigned RegOp = Reg2Bcst.KeyOp;
      unsigned BcstOp = Reg2Bcst.DstOp;
      if (const X86FoldTableEntry *Reg2Mem = lookupFoldTable(RegOp, 4)) {
        unsigned MemOp = Reg2Mem->DstOp;
        uint16_t Flags =
            Reg2Mem->Flags | Reg2Bcst.Flags | TB_INDEX_4 | TB_FOLDED_LOAD;
        Table.push_back({MemOp, BcstOp, Flags});
      }
    }

    // Sort the memory->broadcast fold table.
    array_pod_sort(Table.begin(), Table.end());
  }
};
} // namespace

bool llvm::matchBroadcastSize(const X86FoldTableEntry &Entry,
                              unsigned BroadcastBits) {
  switch (Entry.Flags & TB_BCAST_MASK) {
  case TB_BCAST_W:
  case TB_BCAST_SH:
    return BroadcastBits == 16;
  case TB_BCAST_D:
  case TB_BCAST_SS:
    return BroadcastBits == 32;
  case TB_BCAST_Q:
  case TB_BCAST_SD:
    return BroadcastBits == 64;
  }
  return false;
}

const X86FoldTableEntry *
llvm::lookupBroadcastFoldTableBySize(unsigned MemOp, unsigned BroadcastBits) {
  static X86BroadcastFoldTable BroadcastFoldTable;
  auto &Table = BroadcastFoldTable.Table;
  for (auto I = llvm::lower_bound(Table, MemOp);
       I != Table.end() && I->KeyOp == MemOp; ++I) {
    if (matchBroadcastSize(*I, BroadcastBits))
      return &*I;
  }
  return nullptr;
}

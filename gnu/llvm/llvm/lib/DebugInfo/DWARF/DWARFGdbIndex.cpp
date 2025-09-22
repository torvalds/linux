//===- DWARFGdbIndex.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARF/DWARFGdbIndex.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cinttypes>
#include <cstdint>
#include <set>
#include <utility>

using namespace llvm;

// .gdb_index section format reference:
// https://sourceware.org/gdb/onlinedocs/gdb/Index-Section-Format.html

void DWARFGdbIndex::dumpCUList(raw_ostream &OS) const {
  OS << format("\n  CU list offset = 0x%x, has %" PRId64 " entries:",
               CuListOffset, (uint64_t)CuList.size())
     << '\n';
  uint32_t I = 0;
  for (const CompUnitEntry &CU : CuList)
    OS << format("    %d: Offset = 0x%llx, Length = 0x%llx\n", I++, CU.Offset,
                 CU.Length);
}

void DWARFGdbIndex::dumpTUList(raw_ostream &OS) const {
  OS << formatv("\n  Types CU list offset = {0:x}, has {1} entries:\n",
                TuListOffset, TuList.size());
  uint32_t I = 0;
  for (const TypeUnitEntry &TU : TuList)
    OS << formatv("    {0}: offset = {1:x8}, type_offset = {2:x8}, "
                  "type_signature = {3:x16}\n",
                  I++, TU.Offset, TU.TypeOffset, TU.TypeSignature);
}

void DWARFGdbIndex::dumpAddressArea(raw_ostream &OS) const {
  OS << format("\n  Address area offset = 0x%x, has %" PRId64 " entries:",
               AddressAreaOffset, (uint64_t)AddressArea.size())
     << '\n';
  for (const AddressEntry &Addr : AddressArea)
    OS << format(
        "    Low/High address = [0x%llx, 0x%llx) (Size: 0x%llx), CU id = %d\n",
        Addr.LowAddress, Addr.HighAddress, Addr.HighAddress - Addr.LowAddress,
        Addr.CuIndex);
}

void DWARFGdbIndex::dumpSymbolTable(raw_ostream &OS) const {
  OS << format("\n  Symbol table offset = 0x%x, size = %" PRId64
               ", filled slots:",
               SymbolTableOffset, (uint64_t)SymbolTable.size())
     << '\n';
  uint32_t I = -1;
  for (const SymTableEntry &E : SymbolTable) {
    ++I;
    if (!E.NameOffset && !E.VecOffset)
      continue;

    OS << format("    %d: Name offset = 0x%x, CU vector offset = 0x%x\n", I,
                 E.NameOffset, E.VecOffset);

    StringRef Name = ConstantPoolStrings.substr(
        ConstantPoolOffset - StringPoolOffset + E.NameOffset);

    auto CuVector = llvm::find_if(
        ConstantPoolVectors,
        [&](const std::pair<uint32_t, SmallVector<uint32_t, 0>> &V) {
          return V.first == E.VecOffset;
        });
    assert(CuVector != ConstantPoolVectors.end() && "Invalid symbol table");
    uint32_t CuVectorId = CuVector - ConstantPoolVectors.begin();
    OS << format("      String name: %s, CU vector index: %d\n", Name.data(),
                 CuVectorId);
  }
}

void DWARFGdbIndex::dumpConstantPool(raw_ostream &OS) const {
  OS << format("\n  Constant pool offset = 0x%x, has %" PRId64 " CU vectors:",
               ConstantPoolOffset, (uint64_t)ConstantPoolVectors.size());
  uint32_t I = 0;
  for (const auto &V : ConstantPoolVectors) {
    OS << format("\n    %d(0x%x): ", I++, V.first);
    for (uint32_t Val : V.second)
      OS << format("0x%x ", Val);
  }
  OS << '\n';
}

void DWARFGdbIndex::dump(raw_ostream &OS) {
  if (HasError) {
    OS << "\n<error parsing>\n";
    return;
  }

  if (HasContent) {
    OS << "  Version = " << Version << '\n';
    dumpCUList(OS);
    dumpTUList(OS);
    dumpAddressArea(OS);
    dumpSymbolTable(OS);
    dumpConstantPool(OS);
  }
}

bool DWARFGdbIndex::parseImpl(DataExtractor Data) {
  uint64_t Offset = 0;

  // Only version 7 and 8 are supported at this moment.
  Version = Data.getU32(&Offset);
  if (Version != 7 && Version != 8)
    return false;

  CuListOffset = Data.getU32(&Offset);
  TuListOffset = Data.getU32(&Offset);
  AddressAreaOffset = Data.getU32(&Offset);
  SymbolTableOffset = Data.getU32(&Offset);
  ConstantPoolOffset = Data.getU32(&Offset);

  if (Offset != CuListOffset)
    return false;

  uint32_t CuListSize = (TuListOffset - CuListOffset) / 16;
  CuList.reserve(CuListSize);
  for (uint32_t i = 0; i < CuListSize; ++i) {
    uint64_t CuOffset = Data.getU64(&Offset);
    uint64_t CuLength = Data.getU64(&Offset);
    CuList.push_back({CuOffset, CuLength});
  }

  // CU Types are no longer needed as DWARF skeleton type units never made it
  // into the standard.
  uint32_t TuListSize = (AddressAreaOffset - TuListOffset) / 24;
  TuList.resize(TuListSize);
  for (uint32_t I = 0; I < TuListSize; ++I) {
    uint64_t CuOffset = Data.getU64(&Offset);
    uint64_t TypeOffset = Data.getU64(&Offset);
    uint64_t Signature = Data.getU64(&Offset);
    TuList[I] = {CuOffset, TypeOffset, Signature};
  }

  uint32_t AddressAreaSize = (SymbolTableOffset - AddressAreaOffset) / 20;
  AddressArea.reserve(AddressAreaSize);
  for (uint32_t i = 0; i < AddressAreaSize; ++i) {
    uint64_t LowAddress = Data.getU64(&Offset);
    uint64_t HighAddress = Data.getU64(&Offset);
    uint32_t CuIndex = Data.getU32(&Offset);
    AddressArea.push_back({LowAddress, HighAddress, CuIndex});
  }

  // The symbol table. This is an open addressed hash table. The size of the
  // hash table is always a power of 2.
  // Each slot in the hash table consists of a pair of offset_type values. The
  // first value is the offset of the symbol's name in the constant pool. The
  // second value is the offset of the CU vector in the constant pool.
  // If both values are 0, then this slot in the hash table is empty. This is ok
  // because while 0 is a valid constant pool index, it cannot be a valid index
  // for both a string and a CU vector.
  uint32_t SymTableSize = (ConstantPoolOffset - SymbolTableOffset) / 8;
  SymbolTable.reserve(SymTableSize);
  std::set<uint32_t> CUOffsets;
  for (uint32_t i = 0; i < SymTableSize; ++i) {
    uint32_t NameOffset = Data.getU32(&Offset);
    uint32_t CuVecOffset = Data.getU32(&Offset);
    SymbolTable.push_back({NameOffset, CuVecOffset});
    if (NameOffset || CuVecOffset)
      CUOffsets.insert(CuVecOffset);
  }

  // The constant pool. CU vectors are stored first, followed by strings.
  // The first value is the number of CU indices in the vector. Each subsequent
  // value is the index and symbol attributes of a CU in the CU list.
  for (auto CUOffset : CUOffsets) {
    Offset = ConstantPoolOffset + CUOffset;
    ConstantPoolVectors.emplace_back(0, SmallVector<uint32_t, 0>());
    auto &Vec = ConstantPoolVectors.back();
    Vec.first = Offset - ConstantPoolOffset;

    uint32_t Num = Data.getU32(&Offset);
    for (uint32_t J = 0; J < Num; ++J)
      Vec.second.push_back(Data.getU32(&Offset));
  }

  ConstantPoolStrings = Data.getData().drop_front(Offset);
  StringPoolOffset = Offset;
  return true;
}

void DWARFGdbIndex::parse(DataExtractor Data) {
  HasContent = !Data.getData().empty();
  HasError = HasContent && !parseImpl(Data);
}

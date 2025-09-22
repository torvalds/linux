//===-- PdbIndex.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PdbIndex.h"
#include "PdbUtil.h"

#include "llvm/DebugInfo/CodeView/SymbolDeserializer.h"
#include "llvm/DebugInfo/PDB/Native/DbiStream.h"
#include "llvm/DebugInfo/PDB/Native/GlobalsStream.h"
#include "llvm/DebugInfo/PDB/Native/ISectionContribVisitor.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/Native/PublicsStream.h"
#include "llvm/DebugInfo/PDB/Native/SymbolStream.h"
#include "llvm/DebugInfo/PDB/Native/TpiStream.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/Error.h"

#include "lldb/Utility/LLDBAssert.h"
#include "lldb/lldb-defines.h"
#include <optional>

using namespace lldb_private;
using namespace lldb_private::npdb;
using namespace llvm::codeview;
using namespace llvm::pdb;

PdbIndex::PdbIndex() : m_cus(*this), m_va_to_modi(m_allocator) {}

#define ASSIGN_PTR_OR_RETURN(result_ptr, expr)                                 \
  {                                                                            \
    auto expected_result = expr;                                               \
    if (!expected_result)                                                      \
      return expected_result.takeError();                                      \
    result_ptr = &expected_result.get();                                       \
  }

llvm::Expected<std::unique_ptr<PdbIndex>>
PdbIndex::create(llvm::pdb::PDBFile *file) {
  lldbassert(file);

  std::unique_ptr<PdbIndex> result(new PdbIndex());
  ASSIGN_PTR_OR_RETURN(result->m_dbi, file->getPDBDbiStream());
  ASSIGN_PTR_OR_RETURN(result->m_tpi, file->getPDBTpiStream());
  ASSIGN_PTR_OR_RETURN(result->m_ipi, file->getPDBIpiStream());
  ASSIGN_PTR_OR_RETURN(result->m_info, file->getPDBInfoStream());
  ASSIGN_PTR_OR_RETURN(result->m_publics, file->getPDBPublicsStream());
  ASSIGN_PTR_OR_RETURN(result->m_globals, file->getPDBGlobalsStream());
  ASSIGN_PTR_OR_RETURN(result->m_symrecords, file->getPDBSymbolStream());

  result->m_tpi->buildHashMap();

  result->m_file = file;

  return std::move(result);
}

lldb::addr_t PdbIndex::MakeVirtualAddress(uint16_t segment,
                                          uint32_t offset) const {
  uint32_t max_section = dbi().getSectionHeaders().size();
  // Segment indices are 1-based.
  // If this is an absolute symbol, it's indicated by the magic section index
  // |max_section+1|.  In this case, the offset is meaningless, so just return.
  if (segment == 0 || segment > max_section)
    return LLDB_INVALID_ADDRESS;

  const llvm::object::coff_section &cs = dbi().getSectionHeaders()[segment - 1];
  return m_load_address + static_cast<lldb::addr_t>(cs.VirtualAddress) +
         static_cast<lldb::addr_t>(offset);
}

std::optional<uint16_t> PdbIndex::GetModuleIndexForAddr(uint16_t segment,
                                                        uint32_t offset) const {
  return GetModuleIndexForVa(MakeVirtualAddress(segment, offset));
}

std::optional<uint16_t> PdbIndex::GetModuleIndexForVa(lldb::addr_t va) const {
  auto iter = m_va_to_modi.find(va);
  if (iter == m_va_to_modi.end())
    return std::nullopt;

  return iter.value();
}

void PdbIndex::ParseSectionContribs() {
  class Visitor : public ISectionContribVisitor {
    PdbIndex &m_ctx;
    llvm::IntervalMap<uint64_t, uint16_t> &m_imap;

  public:
    Visitor(PdbIndex &ctx, llvm::IntervalMap<uint64_t, uint16_t> &imap)
        : m_ctx(ctx), m_imap(imap) {}

    void visit(const SectionContrib &C) override {
      if (C.Size == 0)
        return;

      uint64_t va = m_ctx.MakeVirtualAddress(C.ISect, C.Off);
      if (va == LLDB_INVALID_ADDRESS)
        return;
      uint64_t end = va + C.Size;
      // IntervalMap's start and end represent a closed range, not a half-open
      // range, so we have to subtract 1.
      m_imap.insert(va, end - 1, C.Imod);
    }
    void visit(const SectionContrib2 &C) override { visit(C.Base); }
  };
  Visitor v(*this, m_va_to_modi);
  dbi().visitSectionContributions(v);
}

void PdbIndex::BuildAddrToSymbolMap(CompilandIndexItem &cci) {
  lldbassert(cci.m_symbols_by_va.empty() &&
             "Addr to symbol map is already built!");
  uint16_t modi = cci.m_id.modi;
  const CVSymbolArray &syms = cci.m_debug_stream.getSymbolArray();
  for (auto iter = syms.begin(); iter != syms.end(); ++iter) {
    if (!SymbolHasAddress(*iter))
      continue;

    SegmentOffset so = GetSegmentAndOffset(*iter);
    lldb::addr_t va = MakeVirtualAddress(so.segment, so.offset);
    if (va == LLDB_INVALID_ADDRESS)
      continue;

    PdbCompilandSymId cu_sym_id(modi, iter.offset());

    // It's rare, but we could have multiple symbols with the same address
    // because of identical comdat folding.  Right now, the first one will win.
    cci.m_symbols_by_va.insert(std::make_pair(va, PdbSymUid(cu_sym_id)));
  }
}

std::vector<SymbolAndUid> PdbIndex::FindSymbolsByVa(lldb::addr_t va) {
  std::vector<SymbolAndUid> result;

  std::optional<uint16_t> modi = GetModuleIndexForVa(va);
  if (!modi)
    return result;

  CompilandIndexItem &cci = compilands().GetOrCreateCompiland(*modi);
  if (cci.m_symbols_by_va.empty())
    BuildAddrToSymbolMap(cci);

  // The map is sorted by starting address of the symbol.  So for example
  // we could (in theory) have this situation
  //
  // [------------------]
  //    [----------]
  //      [-----------]
  //          [-------------]
  //            [----]
  //               [-----]
  //             ^ Address we're searching for
  // In order to find this, we use the upper_bound of the key value which would
  // be the first symbol whose starting address is higher than the element we're
  // searching for.

  auto ub = cci.m_symbols_by_va.upper_bound(va);

  for (auto iter = cci.m_symbols_by_va.begin(); iter != ub; ++iter) {
    PdbCompilandSymId cu_sym_id = iter->second.asCompilandSym();
    CVSymbol sym = ReadSymbolRecord(cu_sym_id);

    SegmentOffsetLength sol;
    if (SymbolIsCode(sym))
      sol = GetSegmentOffsetAndLength(sym);
    else
      sol.so = GetSegmentAndOffset(sym);

    lldb::addr_t start = MakeVirtualAddress(sol.so.segment, sol.so.offset);
    if (start == LLDB_INVALID_ADDRESS)
      continue;

    lldb::addr_t end = start + sol.length;
    if (va >= start && va < end)
      result.push_back({std::move(sym), iter->second});
  }

  return result;
}

CVSymbol PdbIndex::ReadSymbolRecord(PdbCompilandSymId cu_sym) const {
  const CompilandIndexItem *cci = compilands().GetCompiland(cu_sym.modi);
  auto iter = cci->m_debug_stream.getSymbolArray().at(cu_sym.offset);
  lldbassert(iter != cci->m_debug_stream.getSymbolArray().end());
  return *iter;
}

CVSymbol PdbIndex::ReadSymbolRecord(PdbGlobalSymId global) const {
  return symrecords().readRecord(global.offset);
}

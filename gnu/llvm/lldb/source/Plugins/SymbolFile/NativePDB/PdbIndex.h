//===-- PdbIndex.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_NATIVEPDB_PDBINDEX_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_NATIVEPDB_PDBINDEX_H

#include "lldb/lldb-types.h"
#include "llvm/ADT/IntervalMap.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

#include "CompileUnitIndex.h"
#include "PdbSymUid.h"

#include <map>
#include <memory>
#include <optional>

namespace llvm {
namespace pdb {
class DbiStream;
class TpiStream;
class InfoStream;
class PublicsStream;
class GlobalsStream;
class SymbolStream;
} // namespace pdb
} // namespace llvm

namespace lldb_private {
namespace npdb {
struct SegmentOffset;

/// PdbIndex - Lazy access to the important parts of a PDB file.
///
/// This is a layer on top of LLVM's native PDB support libraries which cache
/// certain data when it is accessed the first time.  The entire PDB file is
/// mapped into memory, and the underlying support libraries vend out memory
/// that is always backed by the file, so it is safe to hold StringRefs and
/// ArrayRefs into the backing memory as long as the PdbIndex instance is
/// alive.
class PdbIndex {

  /// The underlying PDB file.
  llvm::pdb::PDBFile *m_file = nullptr;

  /// The DBI stream.  This contains general high level information about the
  /// features present in the PDB file, compile units (such as the information
  /// necessary to locate full symbol information for each compile unit),
  /// section contributions, and other data which is not specifically symbol or
  /// type records.
  llvm::pdb::DbiStream *m_dbi = nullptr;

  /// TPI (types) and IPI (indices) streams.  These are both in the exact same
  /// format with different data.  Most type records are stored in the TPI
  /// stream but certain specific types of records are stored in the IPI stream.
  /// The IPI stream records can refer to the records in the TPI stream, but not
  /// the other way around.
  llvm::pdb::TpiStream *m_tpi = nullptr;
  llvm::pdb::TpiStream *m_ipi = nullptr;

  /// This is called the "PDB Stream" in the Microsoft reference implementation.
  /// It contains information about the structure of the file, as well as fields
  /// used to match EXE and PDB.
  llvm::pdb::InfoStream *m_info = nullptr;

  /// Publics stream.  Is actually a serialized hash table where the keys are
  /// addresses of symbols in the executable, and values are a record containing
  /// mangled names and an index which can be used to locate more detailed info
  /// about the symbol in the Symbol Records stream.  The publics stream only
  /// contains info about externally visible symbols.
  llvm::pdb::PublicsStream *m_publics = nullptr;

  /// Globals stream.  Contrary to its name, this does not contain information
  /// about all "global variables" or "global functions".  Rather, it is the
  /// "global symbol table", i.e. it contains information about *every* symbol
  /// in the executable.  It is a hash table keyed on name, whose values are
  /// indices into the symbol records stream to find the full record.
  llvm::pdb::GlobalsStream *m_globals = nullptr;

  /// Symbol records stream.  The publics and globals stream refer to records
  /// in this stream.  For some records, like constants and typedefs, the
  /// complete record lives in this stream.  For other symbol types, such as
  /// functions, data, and other things that have been materialied into a
  /// specific compile unit, the records here simply provide a reference
  /// necessary to locate the full information.
  llvm::pdb::SymbolStream *m_symrecords = nullptr;

  /// Index of all compile units, mapping identifier to |CompilandIndexItem|
  /// instance.
  CompileUnitIndex m_cus;

  /// An allocator for the interval maps
  llvm::IntervalMap<lldb::addr_t, uint32_t>::Allocator m_allocator;

  /// Maps virtual address to module index
  llvm::IntervalMap<lldb::addr_t, uint16_t> m_va_to_modi;

  /// The address at which the program has been loaded into memory.
  lldb::addr_t m_load_address = 0;

  PdbIndex();

  void BuildAddrToSymbolMap(CompilandIndexItem &cci);

public:
  static llvm::Expected<std::unique_ptr<PdbIndex>> create(llvm::pdb::PDBFile *);

  void SetLoadAddress(lldb::addr_t addr) { m_load_address = addr; }
  lldb::addr_t GetLoadAddress() const { return m_load_address; }
  void ParseSectionContribs();

  llvm::pdb::PDBFile &pdb() { return *m_file; }
  const llvm::pdb::PDBFile &pdb() const { return *m_file; }

  llvm::pdb::DbiStream &dbi() { return *m_dbi; }
  const llvm::pdb::DbiStream &dbi() const { return *m_dbi; }

  llvm::pdb::TpiStream &tpi() { return *m_tpi; }
  const llvm::pdb::TpiStream &tpi() const { return *m_tpi; }

  llvm::pdb::TpiStream &ipi() { return *m_ipi; }
  const llvm::pdb::TpiStream &ipi() const { return *m_ipi; }

  llvm::pdb::InfoStream &info() { return *m_info; }
  const llvm::pdb::InfoStream &info() const { return *m_info; }

  llvm::pdb::PublicsStream &publics() { return *m_publics; }
  const llvm::pdb::PublicsStream &publics() const { return *m_publics; }

  llvm::pdb::GlobalsStream &globals() { return *m_globals; }
  const llvm::pdb::GlobalsStream &globals() const { return *m_globals; }

  llvm::pdb::SymbolStream &symrecords() { return *m_symrecords; }
  const llvm::pdb::SymbolStream &symrecords() const { return *m_symrecords; }

  CompileUnitIndex &compilands() { return m_cus; }
  const CompileUnitIndex &compilands() const { return m_cus; }

  lldb::addr_t MakeVirtualAddress(uint16_t segment, uint32_t offset) const;

  std::vector<SymbolAndUid> FindSymbolsByVa(lldb::addr_t va);

  llvm::codeview::CVSymbol ReadSymbolRecord(PdbCompilandSymId cu_sym) const;
  llvm::codeview::CVSymbol ReadSymbolRecord(PdbGlobalSymId global) const;

  std::optional<uint16_t> GetModuleIndexForAddr(uint16_t segment,
                                                uint32_t offset) const;
  std::optional<uint16_t> GetModuleIndexForVa(lldb::addr_t va) const;
};
} // namespace npdb
} // namespace lldb_private

#endif

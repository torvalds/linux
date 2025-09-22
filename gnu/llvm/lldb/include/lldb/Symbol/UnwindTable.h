//===-- UnwindTable.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SYMBOL_UNWINDTABLE_H
#define LLDB_SYMBOL_UNWINDTABLE_H

#include <map>
#include <mutex>
#include <optional>

#include "lldb/lldb-private.h"

namespace lldb_private {

// A class which holds all the FuncUnwinders objects for a given ObjectFile.
// The UnwindTable is populated with FuncUnwinders objects lazily during the
// debug session.

class UnwindTable {
public:
  /// Create an Unwind table using the data in the given module.
  explicit UnwindTable(Module &module);

  ~UnwindTable();

  lldb_private::CallFrameInfo *GetObjectFileUnwindInfo();

  lldb_private::DWARFCallFrameInfo *GetEHFrameInfo();
  lldb_private::DWARFCallFrameInfo *GetDebugFrameInfo();

  lldb_private::CompactUnwindInfo *GetCompactUnwindInfo();

  ArmUnwindInfo *GetArmUnwindInfo();
  SymbolFile *GetSymbolFile();

  lldb::FuncUnwindersSP GetFuncUnwindersContainingAddress(const Address &addr,
                                                          SymbolContext &sc);

  bool GetAllowAssemblyEmulationUnwindPlans();

  // Normally when we create a new FuncUnwinders object we track it in this
  // UnwindTable so it can be reused later.  But for the target modules show-
  // unwind we want to create brand new UnwindPlans for the function of
  // interest - so ignore any existing FuncUnwinders for that function and
  // don't add this new one to our UnwindTable. This FuncUnwinders object does
  // have a reference to the UnwindTable but the lifetime of this uncached
  // FuncUnwinders is expected to be short so in practice this will not be a
  // problem.
  lldb::FuncUnwindersSP
  GetUncachedFuncUnwindersContainingAddress(const Address &addr,
                                            const SymbolContext &sc);

  ArchSpec GetArchitecture();

  /// Called after a SymbolFile has been added to a Module to add any new
  /// unwind sections that may now be available.
  void Update();

private:
  void Dump(Stream &s);

  void Initialize();
  std::optional<AddressRange> GetAddressRange(const Address &addr,
                                              const SymbolContext &sc);

  typedef std::map<lldb::addr_t, lldb::FuncUnwindersSP> collection;
  typedef collection::iterator iterator;
  typedef collection::const_iterator const_iterator;

  Module &m_module;
  collection m_unwinds;

  bool m_initialized; // delay some initialization until ObjectFile is set up
  std::mutex m_mutex;

  std::unique_ptr<CallFrameInfo> m_object_file_unwind_up;
  std::unique_ptr<DWARFCallFrameInfo> m_eh_frame_up;
  std::unique_ptr<DWARFCallFrameInfo> m_debug_frame_up;
  std::unique_ptr<CompactUnwindInfo> m_compact_unwind_up;
  std::unique_ptr<ArmUnwindInfo> m_arm_unwind_up;

  UnwindTable(const UnwindTable &) = delete;
  const UnwindTable &operator=(const UnwindTable &) = delete;
};

} // namespace lldb_private

#endif // LLDB_SYMBOL_UNWINDTABLE_H

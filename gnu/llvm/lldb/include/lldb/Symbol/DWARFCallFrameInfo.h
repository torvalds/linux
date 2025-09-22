//===-- DWARFCallFrameInfo.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SYMBOL_DWARFCALLFRAMEINFO_H
#define LLDB_SYMBOL_DWARFCALLFRAMEINFO_H

#include <map>
#include <mutex>
#include <optional>

#include "lldb/Core/AddressRange.h"
#include "lldb/Core/dwarf.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Utility/Flags.h"
#include "lldb/Utility/RangeMap.h"
#include "lldb/Utility/VMRange.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

// DWARFCallFrameInfo is a class which can read eh_frame and DWARF Call Frame
// Information FDEs.  It stores little information internally. Only two APIs
// are exported - one to find the high/low pc values of a function given a text
// address via the information in the eh_frame / debug_frame, and one to
// generate an UnwindPlan based on the FDE in the eh_frame / debug_frame
// section.

class DWARFCallFrameInfo {
public:
  enum Type { EH, DWARF };

  DWARFCallFrameInfo(ObjectFile &objfile, lldb::SectionSP &section, Type type);

  ~DWARFCallFrameInfo() = default;

  // Locate an AddressRange that includes the provided Address in this object's
  // eh_frame/debug_info Returns true if a range is found to cover that
  // address.
  bool GetAddressRange(Address addr, AddressRange &range);

  /// Return an UnwindPlan based on the call frame information encoded in the
  /// FDE of this DWARFCallFrameInfo section. The returned plan will be valid
  /// (at least) for the given address.
  bool GetUnwindPlan(const Address &addr, UnwindPlan &unwind_plan);

  /// Return an UnwindPlan based on the call frame information encoded in the
  /// FDE of this DWARFCallFrameInfo section. The returned plan will be valid
  /// (at least) for some address in the given range.
  bool GetUnwindPlan(const AddressRange &range, UnwindPlan &unwind_plan);

  typedef RangeVector<lldb::addr_t, uint32_t> FunctionAddressAndSizeVector;

  // Build a vector of file address and size for all functions in this Module
  // based on the eh_frame FDE entries.
  //
  // The eh_frame information can be a useful source of file address and size
  // of the functions in a Module.  Often a binary's non-exported symbols are
  // stripped before shipping so lldb won't know the start addr / size of many
  // functions in the Module.  But the eh_frame can help to give the addresses
  // of these stripped symbols, at least.
  //
  // \param[out] function_info
  //      A vector provided by the caller is filled out.  May be empty if no
  //      FDEs/no eh_frame
  //      is present in this Module.

  void
  GetFunctionAddressAndSizeVector(FunctionAddressAndSizeVector &function_info);

  void ForEachFDEEntries(
      const std::function<bool(lldb::addr_t, uint32_t, dw_offset_t)> &callback);

private:
  enum { CFI_AUG_MAX_SIZE = 8, CFI_HEADER_SIZE = 8 };
  enum CFIVersion {
    CFI_VERSION1 = 1, // DWARF v.2
    CFI_VERSION3 = 3, // DWARF v.3
    CFI_VERSION4 = 4  // DWARF v.4, v.5
  };

  struct CIE {
    dw_offset_t cie_offset;
    uint8_t version;
    char augmentation[CFI_AUG_MAX_SIZE]; // This is typically empty or very
                                         // short.
    uint8_t address_size = sizeof(uint32_t); // The size of a target address.
    uint8_t segment_size = 0;                // The size of a segment selector.

    uint32_t code_align;
    int32_t data_align;
    uint32_t return_addr_reg_num;
    dw_offset_t inst_offset; // offset of CIE instructions in mCFIData
    uint32_t inst_length;    // length of CIE instructions in mCFIData
    uint8_t ptr_encoding;
    uint8_t lsda_addr_encoding;   // The encoding of the LSDA address in the FDE
                                  // augmentation data
    lldb::addr_t personality_loc; // (file) address of the pointer to the
                                  // personality routine
    lldb_private::UnwindPlan::Row initial_row;

    CIE(dw_offset_t offset)
        : cie_offset(offset), version(-1), code_align(0), data_align(0),
          return_addr_reg_num(LLDB_INVALID_REGNUM), inst_offset(0),
          inst_length(0), ptr_encoding(0),
          lsda_addr_encoding(llvm::dwarf::DW_EH_PE_omit),
          personality_loc(LLDB_INVALID_ADDRESS) {}
  };

  typedef std::shared_ptr<CIE> CIESP;

  typedef std::map<dw_offset_t, CIESP> cie_map_t;

  // Start address (file address), size, offset of FDE location used for
  // finding an FDE for a given File address; the start address field is an
  // offset into an individual Module.
  typedef RangeDataVector<lldb::addr_t, uint32_t, dw_offset_t> FDEEntryMap;

  bool IsEHFrame() const;

  std::optional<FDEEntryMap::Entry>
  GetFirstFDEEntryInRange(const AddressRange &range);

  void GetFDEIndex();

  bool FDEToUnwindPlan(dw_offset_t offset, Address startaddr,
                       UnwindPlan &unwind_plan);

  const CIE *GetCIE(dw_offset_t cie_offset);

  void GetCFIData();

  // Applies the specified DWARF opcode to the given row. This function handle
  // the commands operates only on a single row (these are the ones what can
  // appear both in
  // CIE and in FDE).
  // Returns true if the opcode is handled and false otherwise.
  bool HandleCommonDwarfOpcode(uint8_t primary_opcode, uint8_t extended_opcode,
                               int32_t data_align, lldb::offset_t &offset,
                               UnwindPlan::Row &row);

  ObjectFile &m_objfile;
  lldb::SectionSP m_section_sp;
  Flags m_flags = 0;
  cie_map_t m_cie_map;

  DataExtractor m_cfi_data;
  bool m_cfi_data_initialized = false; // only copy the section into the DE once

  FDEEntryMap m_fde_index;
  bool m_fde_index_initialized = false; // only scan the section for FDEs once
  std::mutex m_fde_index_mutex; // and isolate the thread that does it

  Type m_type;

  CIESP
  ParseCIE(const dw_offset_t cie_offset);

  lldb::RegisterKind GetRegisterKind() const {
    return m_type == EH ? lldb::eRegisterKindEHFrame : lldb::eRegisterKindDWARF;
  }
};

} // namespace lldb_private

#endif // LLDB_SYMBOL_DWARFCALLFRAMEINFO_H

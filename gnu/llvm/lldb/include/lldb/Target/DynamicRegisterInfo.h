//===-- DynamicRegisterInfo.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_DYNAMICREGISTERINFO_H
#define LLDB_TARGET_DYNAMICREGISTERINFO_H

#include <map>
#include <vector>

#include "lldb/Target/RegisterFlags.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/StructuredData.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class DynamicRegisterInfo {
protected:
  DynamicRegisterInfo(DynamicRegisterInfo &) = default;
  DynamicRegisterInfo &operator=(DynamicRegisterInfo &) = default;

public:
  struct Register {
    ConstString name;
    ConstString alt_name;
    ConstString set_name;
    uint32_t byte_size = LLDB_INVALID_INDEX32;
    uint32_t byte_offset = LLDB_INVALID_INDEX32;
    lldb::Encoding encoding = lldb::eEncodingUint;
    lldb::Format format = lldb::eFormatHex;
    uint32_t regnum_dwarf = LLDB_INVALID_REGNUM;
    uint32_t regnum_ehframe = LLDB_INVALID_REGNUM;
    uint32_t regnum_generic = LLDB_INVALID_REGNUM;
    uint32_t regnum_remote = LLDB_INVALID_REGNUM;
    std::vector<uint32_t> value_regs;
    std::vector<uint32_t> invalidate_regs;
    uint32_t value_reg_offset = 0;
    // Non-null if there is an XML provided type.
    const RegisterFlags *flags_type = nullptr;
  };

  DynamicRegisterInfo() = default;

  static std::unique_ptr<DynamicRegisterInfo>
  Create(const StructuredData::Dictionary &dict, const ArchSpec &arch);

  virtual ~DynamicRegisterInfo() = default;

  DynamicRegisterInfo(DynamicRegisterInfo &&info);
  DynamicRegisterInfo &operator=(DynamicRegisterInfo &&info);

  size_t SetRegisterInfo(const lldb_private::StructuredData::Dictionary &dict,
                         const lldb_private::ArchSpec &arch);

  size_t SetRegisterInfo(std::vector<Register> &&regs,
                         const lldb_private::ArchSpec &arch);

  size_t GetNumRegisters() const;

  size_t GetNumRegisterSets() const;

  size_t GetRegisterDataByteSize() const;

  const lldb_private::RegisterInfo *GetRegisterInfoAtIndex(uint32_t i) const;

  const lldb_private::RegisterSet *GetRegisterSet(uint32_t i) const;

  uint32_t GetRegisterSetIndexByName(const lldb_private::ConstString &set_name,
                                     bool can_create);

  uint32_t ConvertRegisterKindToRegisterNumber(uint32_t kind,
                                               uint32_t num) const;

  const lldb_private::RegisterInfo *GetRegisterInfo(uint32_t kind,
                                                    uint32_t num) const;

  void Dump() const;

  void Clear();

  bool IsReconfigurable();

  const lldb_private::RegisterInfo *
  GetRegisterInfo(llvm::StringRef reg_name) const;

  typedef std::vector<lldb_private::RegisterInfo> reg_collection;
  typedef llvm::iterator_range<reg_collection::const_iterator>
      reg_collection_const_range;
  typedef llvm::iterator_range<reg_collection::iterator> reg_collection_range;

  template <typename T> T registers() = delete;

  void ConfigureOffsets();

protected:
  // Classes that inherit from DynamicRegisterInfo can see and modify these
  typedef std::vector<lldb_private::RegisterSet> set_collection;
  typedef std::vector<uint32_t> reg_num_collection;
  typedef std::vector<reg_num_collection> set_reg_num_collection;
  typedef std::vector<lldb_private::ConstString> name_collection;
  typedef std::map<uint32_t, reg_num_collection> reg_to_regs_map;
  typedef std::map<uint32_t, uint32_t> reg_offset_map;

  llvm::Expected<uint32_t> ByteOffsetFromSlice(uint32_t index,
                                               llvm::StringRef slice_str,
                                               lldb::ByteOrder byte_order);
  llvm::Expected<uint32_t> ByteOffsetFromComposite(
      uint32_t index, lldb_private::StructuredData::Array &composite_reg_list,
      lldb::ByteOrder byte_order);
  llvm::Expected<uint32_t> ByteOffsetFromRegInfoDict(
      uint32_t index, lldb_private::StructuredData::Dictionary &reg_info_dict,
      lldb::ByteOrder byte_order);

  void MoveFrom(DynamicRegisterInfo &&info);

  void Finalize(const lldb_private::ArchSpec &arch);

  reg_collection m_regs;
  set_collection m_sets;
  set_reg_num_collection m_set_reg_nums;
  name_collection m_set_names;
  reg_to_regs_map m_value_regs_map;
  reg_to_regs_map m_invalidate_regs_map;
  reg_offset_map m_value_reg_offset_map;
  size_t m_reg_data_byte_size = 0u; // The number of bytes required to store
                                    // all registers
  bool m_finalized = false;
  bool m_is_reconfigurable = false;
};

template <>
inline DynamicRegisterInfo::reg_collection_const_range
DynamicRegisterInfo::registers() {
  return reg_collection_const_range(m_regs);
}

template <>
inline DynamicRegisterInfo::reg_collection_range
DynamicRegisterInfo::registers() {
  return reg_collection_range(m_regs);
}

void addSupplementaryRegister(std::vector<DynamicRegisterInfo::Register> &regs,
                              DynamicRegisterInfo::Register new_reg_info);

} // namespace lldb_private

#endif // LLDB_TARGET_DYNAMICREGISTERINFO_H

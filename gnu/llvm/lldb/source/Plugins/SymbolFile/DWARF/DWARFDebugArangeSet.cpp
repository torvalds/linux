//===-- DWARFDebugArangeSet.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DWARFDebugArangeSet.h"
#include "DWARFDataExtractor.h"
#include "LogChannelDWARF.h"
#include "llvm/Object/Error.h"
#include <cassert>

using namespace lldb_private;
using namespace lldb_private::plugin::dwarf;

DWARFDebugArangeSet::DWARFDebugArangeSet()
    : m_offset(DW_INVALID_OFFSET), m_next_offset(DW_INVALID_OFFSET) {}

void DWARFDebugArangeSet::Clear() {
  m_offset = DW_INVALID_OFFSET;
  m_next_offset = DW_INVALID_OFFSET;
  m_header.length = 0;
  m_header.version = 0;
  m_header.cu_offset = 0;
  m_header.addr_size = 0;
  m_header.seg_size = 0;
  m_arange_descriptors.clear();
}

llvm::Error DWARFDebugArangeSet::extract(const DWARFDataExtractor &data,
                                         lldb::offset_t *offset_ptr) {
  assert(data.ValidOffset(*offset_ptr));

  m_arange_descriptors.clear();
  m_offset = *offset_ptr;

  // 7.20 Address Range Table
  //
  // Each set of entries in the table of address ranges contained in the
  // .debug_aranges section begins with a header consisting of: a 4-byte
  // length containing the length of the set of entries for this compilation
  // unit, not including the length field itself; a 2-byte version identifier
  // containing the value 2 for DWARF Version 2; a 4-byte offset into
  // the.debug_infosection; a 1-byte unsigned integer containing the size in
  // bytes of an address (or the offset portion of an address for segmented
  // addressing) on the target system; and a 1-byte unsigned integer
  // containing the size in bytes of a segment descriptor on the target
  // system. This header is followed by a series of tuples. Each tuple
  // consists of an address and a length, each in the size appropriate for an
  // address on the target architecture.
  m_header.length = data.GetDWARFInitialLength(offset_ptr);
  // The length could be 4 bytes or 12 bytes, so use the current offset to
  // determine the next offset correctly.
  if (m_header.length > 0)
    m_next_offset = *offset_ptr + m_header.length;
  else
    m_next_offset = DW_INVALID_OFFSET;
  m_header.version = data.GetU16(offset_ptr);
  m_header.cu_offset = data.GetDWARFOffset(offset_ptr);
  m_header.addr_size = data.GetU8(offset_ptr);
  m_header.seg_size = data.GetU8(offset_ptr);

  // Try to avoid reading invalid arange sets by making sure:
  // 1 - the version looks good
  // 2 - the address byte size looks plausible
  // 3 - the length seems to make sense
  // 4 - size looks plausible
  // 5 - the arange tuples do not contain a segment field
  if (m_header.version < 2 || m_header.version > 5)
    return llvm::make_error<llvm::object::GenericBinaryError>(
        "Invalid arange header version");

  if (m_header.addr_size != 4 && m_header.addr_size != 8)
    return llvm::make_error<llvm::object::GenericBinaryError>(
        "Invalid arange header address size");

  if (m_header.length == 0)
    return llvm::make_error<llvm::object::GenericBinaryError>(
        "Invalid arange header length");

  if (!data.ValidOffset(m_offset + sizeof(m_header.length) + m_header.length -
                        1))
    return llvm::make_error<llvm::object::GenericBinaryError>(
        "Invalid arange header length");

  if (m_header.seg_size)
    return llvm::make_error<llvm::object::GenericBinaryError>(
        "segmented arange entries are not supported");

  // The first tuple following the header in each set begins at an offset
  // that is a multiple of the size of a single tuple (that is, twice the
  // size of an address). The header is padded, if necessary, to the
  // appropriate boundary.
  const uint32_t header_size = *offset_ptr - m_offset;
  const uint32_t tuple_size = m_header.addr_size << 1;
  uint32_t first_tuple_offset = 0;
  while (first_tuple_offset < header_size)
    first_tuple_offset += tuple_size;

  *offset_ptr = m_offset + first_tuple_offset;

  Descriptor arangeDescriptor;

  static_assert(sizeof(arangeDescriptor.address) ==
                    sizeof(arangeDescriptor.length),
                "DWARFDebugArangeSet::Descriptor.address and "
                "DWARFDebugArangeSet::Descriptor.length must have same size");

  const lldb::offset_t next_offset = GetNextOffset();
  assert(next_offset != DW_INVALID_OFFSET);
  uint32_t num_terminators = 0;
  bool last_was_terminator = false;
  while (*offset_ptr < next_offset) {
    arangeDescriptor.address = data.GetMaxU64(offset_ptr, m_header.addr_size);
    arangeDescriptor.length = data.GetMaxU64(offset_ptr, m_header.addr_size);

    // Each set of tuples is terminated by a 0 for the address and 0 for
    // the length. Some linkers can emit .debug_aranges with multiple
    // terminator pair entries that are still withing the length of the
    // DWARFDebugArangeSet. We want to be sure to parse all entries for
    // this DWARFDebugArangeSet so that we don't stop parsing early and end up
    // treating addresses as a header of the next DWARFDebugArangeSet. We also
    // need to make sure we parse all valid address pairs so we don't omit them
    // from the aranges result, so we can't stop at the first terminator entry
    // we find.
    if (arangeDescriptor.address == 0 && arangeDescriptor.length == 0) {
      ++num_terminators;
      last_was_terminator = true;
    } else {
      last_was_terminator = false;
      // Only add .debug_aranges address entries that have a non zero size.
      // Some linkers will zero out the length field for some .debug_aranges
      // entries if they were stripped. We also could watch out for multiple
      // entries at address zero and remove those as well.
      if (arangeDescriptor.length > 0)
        m_arange_descriptors.push_back(arangeDescriptor);
    }
  }
  if (num_terminators > 1) {
    Log *log = GetLog(DWARFLog::DebugInfo);
    LLDB_LOG(log,
             "warning: DWARFDebugArangeSet at %#" PRIx64 " contains %u "
             "terminator entries",
             m_offset, num_terminators);
  }
  if (last_was_terminator)
    return llvm::ErrorSuccess();

  return llvm::make_error<llvm::object::GenericBinaryError>(
      "arange descriptors not terminated by null entry");
}

class DescriptorContainsAddress {
public:
  DescriptorContainsAddress(dw_addr_t address) : m_address(address) {}
  bool operator()(const DWARFDebugArangeSet::Descriptor &desc) const {
    return (m_address >= desc.address) &&
           (m_address < (desc.address + desc.length));
  }

private:
  const dw_addr_t m_address;
};

dw_offset_t DWARFDebugArangeSet::FindAddress(dw_addr_t address) const {
  DescriptorConstIter end = m_arange_descriptors.end();
  DescriptorConstIter pos =
      std::find_if(m_arange_descriptors.begin(), end,   // Range
                   DescriptorContainsAddress(address)); // Predicate
  if (pos != end)
    return m_header.cu_offset;

  return DW_INVALID_OFFSET;
}

//===-- AddressRange.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/AddressRange.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/Section.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Stream.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-types.h"

#include "llvm/Support/Compiler.h"

#include <memory>

#include <cinttypes>

namespace lldb_private {
class SectionList;
}

using namespace lldb;
using namespace lldb_private;

AddressRange::AddressRange() : m_base_addr() {}

AddressRange::AddressRange(addr_t file_addr, addr_t byte_size,
                           const SectionList *section_list)
    : m_base_addr(file_addr, section_list), m_byte_size(byte_size) {}

AddressRange::AddressRange(const lldb::SectionSP &section, addr_t offset,
                           addr_t byte_size)
    : m_base_addr(section, offset), m_byte_size(byte_size) {}

AddressRange::AddressRange(const Address &so_addr, addr_t byte_size)
    : m_base_addr(so_addr), m_byte_size(byte_size) {}

AddressRange::~AddressRange() = default;

bool AddressRange::Contains(const Address &addr) const {
  SectionSP range_sect_sp = GetBaseAddress().GetSection();
  SectionSP addr_sect_sp = addr.GetSection();
  if (range_sect_sp) {
    if (!addr_sect_sp ||
        range_sect_sp->GetModule() != addr_sect_sp->GetModule())
      return false; // Modules do not match.
  } else if (addr_sect_sp) {
    return false; // Range has no module but "addr" does because addr has a
                  // section
  }
  // Either the modules match, or both have no module, so it is ok to compare
  // the file addresses in this case only.
  return ContainsFileAddress(addr);
}

bool AddressRange::ContainsFileAddress(const Address &addr) const {
  if (addr.GetSection() == m_base_addr.GetSection())
    return (addr.GetOffset() - m_base_addr.GetOffset()) < GetByteSize();
  addr_t file_base_addr = GetBaseAddress().GetFileAddress();
  if (file_base_addr == LLDB_INVALID_ADDRESS)
    return false;

  addr_t file_addr = addr.GetFileAddress();
  if (file_addr == LLDB_INVALID_ADDRESS)
    return false;

  if (file_base_addr <= file_addr)
    return (file_addr - file_base_addr) < GetByteSize();

  return false;
}

bool AddressRange::ContainsFileAddress(addr_t file_addr) const {
  if (file_addr == LLDB_INVALID_ADDRESS)
    return false;

  addr_t file_base_addr = GetBaseAddress().GetFileAddress();
  if (file_base_addr == LLDB_INVALID_ADDRESS)
    return false;

  if (file_base_addr <= file_addr)
    return (file_addr - file_base_addr) < GetByteSize();

  return false;
}

bool AddressRange::ContainsLoadAddress(const Address &addr,
                                       Target *target) const {
  if (addr.GetSection() == m_base_addr.GetSection())
    return (addr.GetOffset() - m_base_addr.GetOffset()) < GetByteSize();
  addr_t load_base_addr = GetBaseAddress().GetLoadAddress(target);
  if (load_base_addr == LLDB_INVALID_ADDRESS)
    return false;

  addr_t load_addr = addr.GetLoadAddress(target);
  if (load_addr == LLDB_INVALID_ADDRESS)
    return false;

  if (load_base_addr <= load_addr)
    return (load_addr - load_base_addr) < GetByteSize();

  return false;
}

bool AddressRange::ContainsLoadAddress(addr_t load_addr, Target *target) const {
  if (load_addr == LLDB_INVALID_ADDRESS)
    return false;

  addr_t load_base_addr = GetBaseAddress().GetLoadAddress(target);
  if (load_base_addr == LLDB_INVALID_ADDRESS)
    return false;

  if (load_base_addr <= load_addr)
    return (load_addr - load_base_addr) < GetByteSize();

  return false;
}

bool AddressRange::Extend(const AddressRange &rhs_range) {
  addr_t lhs_end_addr = GetBaseAddress().GetFileAddress() + GetByteSize();
  addr_t rhs_base_addr = rhs_range.GetBaseAddress().GetFileAddress();

  if (!ContainsFileAddress(rhs_range.GetBaseAddress()) &&
      lhs_end_addr != rhs_base_addr)
    // The ranges don't intersect at all on the right side of this range.
    return false;

  addr_t rhs_end_addr = rhs_base_addr + rhs_range.GetByteSize();
  if (lhs_end_addr >= rhs_end_addr)
    // The rhs range totally overlaps this one, nothing to add.
    return false;

  m_byte_size += rhs_end_addr - lhs_end_addr;
  return true;
}

void AddressRange::Clear() {
  m_base_addr.Clear();
  m_byte_size = 0;
}

bool AddressRange::IsValid() const {
  return m_base_addr.IsValid() && (m_byte_size > 0);
}

bool AddressRange::Dump(Stream *s, Target *target, Address::DumpStyle style,
                        Address::DumpStyle fallback_style) const {
  addr_t vmaddr = LLDB_INVALID_ADDRESS;
  int addr_size = sizeof(addr_t);
  if (target)
    addr_size = target->GetArchitecture().GetAddressByteSize();

  bool show_module = false;
  switch (style) {
  default:
    break;
  case Address::DumpStyleSectionNameOffset:
  case Address::DumpStyleSectionPointerOffset:
    s->PutChar('[');
    m_base_addr.Dump(s, target, style, fallback_style);
    s->PutChar('-');
    DumpAddress(s->AsRawOstream(), m_base_addr.GetOffset() + GetByteSize(),
                addr_size);
    s->PutChar(')');
    return true;
    break;

  case Address::DumpStyleModuleWithFileAddress:
    show_module = true;
    [[fallthrough]];
  case Address::DumpStyleFileAddress:
    vmaddr = m_base_addr.GetFileAddress();
    break;

  case Address::DumpStyleLoadAddress:
    vmaddr = m_base_addr.GetLoadAddress(target);
    break;
  }

  if (vmaddr != LLDB_INVALID_ADDRESS) {
    if (show_module) {
      ModuleSP module_sp(GetBaseAddress().GetModule());
      if (module_sp)
        s->Printf("%s", module_sp->GetFileSpec().GetFilename().AsCString(
                            "<Unknown>"));
    }
    DumpAddressRange(s->AsRawOstream(), vmaddr, vmaddr + GetByteSize(),
                     addr_size);
    return true;
  } else if (fallback_style != Address::DumpStyleInvalid) {
    return Dump(s, target, fallback_style, Address::DumpStyleInvalid);
  }

  return false;
}

void AddressRange::DumpDebug(Stream *s) const {
  s->Printf("%p: AddressRange section = %p, offset = 0x%16.16" PRIx64
            ", byte_size = 0x%16.16" PRIx64 "\n",
            static_cast<const void *>(this),
            static_cast<void *>(m_base_addr.GetSection().get()),
            m_base_addr.GetOffset(), GetByteSize());
}

bool AddressRange::GetDescription(Stream *s, Target *target) const {
  addr_t start_addr = m_base_addr.GetLoadAddress(target);
  if (start_addr != LLDB_INVALID_ADDRESS) {
    // We have a valid target and the address was resolved, or we have a base
    // address with no section. Just print out a raw address range: [<addr>,
    // <addr>)
    s->Printf("[0x%" PRIx64 "-0x%" PRIx64 ")", start_addr,
              start_addr + GetByteSize());
    return true;
  }

  // Either no target or the address wasn't resolved, print as
  // <module>[<file-addr>-<file-addr>)
  const char *file_name = "";
  const auto section_sp = m_base_addr.GetSection();
  if (section_sp) {
    if (const auto object_file = section_sp->GetObjectFile())
      file_name = object_file->GetFileSpec().GetFilename().AsCString();
  }
  start_addr = m_base_addr.GetFileAddress();
  const addr_t end_addr = (start_addr == LLDB_INVALID_ADDRESS)
                              ? LLDB_INVALID_ADDRESS
                              : start_addr + GetByteSize();
  s->Printf("%s[0x%" PRIx64 "-0x%" PRIx64 ")", file_name, start_addr, end_addr);
  return true;
}

bool AddressRange::operator==(const AddressRange &rhs) {
  if (!IsValid() || !rhs.IsValid())
    return false;
  return m_base_addr == rhs.GetBaseAddress() &&
         m_byte_size == rhs.GetByteSize();
}

bool AddressRange::operator!=(const AddressRange &rhs) {
  return !(*this == rhs);
}

//===-- NativeProcessELF.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "NativeProcessELF.h"

#include "lldb/Utility/DataExtractor.h"
#include <optional>

namespace lldb_private {

std::optional<uint64_t>
NativeProcessELF::GetAuxValue(enum AuxVector::EntryType type) {
  if (m_aux_vector == nullptr) {
    auto buffer_or_error = GetAuxvData();
    if (!buffer_or_error)
      return std::nullopt;
    DataExtractor auxv_data(buffer_or_error.get()->getBufferStart(),
                            buffer_or_error.get()->getBufferSize(),
                            GetByteOrder(), GetAddressByteSize());
    m_aux_vector = std::make_unique<AuxVector>(auxv_data);
  }

  return m_aux_vector->GetAuxValue(type);
}

lldb::addr_t NativeProcessELF::GetSharedLibraryInfoAddress() {
  if (!m_shared_library_info_addr) {
    if (GetAddressByteSize() == 8)
      m_shared_library_info_addr =
          GetELFImageInfoAddress<llvm::ELF::Elf64_Ehdr, llvm::ELF::Elf64_Phdr,
                                 llvm::ELF::Elf64_Dyn>();
    else
      m_shared_library_info_addr =
          GetELFImageInfoAddress<llvm::ELF::Elf32_Ehdr, llvm::ELF::Elf32_Phdr,
                                 llvm::ELF::Elf32_Dyn>();
  }

  return *m_shared_library_info_addr;
}

template <typename ELF_EHDR, typename ELF_PHDR, typename ELF_DYN>
lldb::addr_t NativeProcessELF::GetELFImageInfoAddress() {
  std::optional<uint64_t> maybe_phdr_addr =
      GetAuxValue(AuxVector::AUXV_AT_PHDR);
  std::optional<uint64_t> maybe_phdr_entry_size =
      GetAuxValue(AuxVector::AUXV_AT_PHENT);
  std::optional<uint64_t> maybe_phdr_num_entries =
      GetAuxValue(AuxVector::AUXV_AT_PHNUM);
  if (!maybe_phdr_addr || !maybe_phdr_entry_size || !maybe_phdr_num_entries)
    return LLDB_INVALID_ADDRESS;
  lldb::addr_t phdr_addr = *maybe_phdr_addr;
  size_t phdr_entry_size = *maybe_phdr_entry_size;
  size_t phdr_num_entries = *maybe_phdr_num_entries;

  // Find the PT_DYNAMIC segment (.dynamic section) in the program header and
  // what the load bias by calculating the difference of the program header
  // load address and its virtual address.
  lldb::offset_t load_bias;
  bool found_load_bias = false;
  lldb::addr_t dynamic_section_addr = 0;
  uint64_t dynamic_section_size = 0;
  bool found_dynamic_section = false;
  ELF_PHDR phdr_entry;
  for (size_t i = 0; i < phdr_num_entries; i++) {
    size_t bytes_read;
    auto error = ReadMemory(phdr_addr + i * phdr_entry_size, &phdr_entry,
                            sizeof(phdr_entry), bytes_read);
    if (!error.Success())
      return LLDB_INVALID_ADDRESS;
    if (phdr_entry.p_type == llvm::ELF::PT_PHDR) {
      load_bias = phdr_addr - phdr_entry.p_vaddr;
      found_load_bias = true;
    }

    if (phdr_entry.p_type == llvm::ELF::PT_DYNAMIC) {
      dynamic_section_addr = phdr_entry.p_vaddr;
      dynamic_section_size = phdr_entry.p_memsz;
      found_dynamic_section = true;
    }
  }

  if (!found_load_bias || !found_dynamic_section)
    return LLDB_INVALID_ADDRESS;

  // Find the DT_DEBUG entry in the .dynamic section
  dynamic_section_addr += load_bias;
  ELF_DYN dynamic_entry;
  size_t dynamic_num_entries = dynamic_section_size / sizeof(dynamic_entry);
  for (size_t i = 0; i < dynamic_num_entries; i++) {
    size_t bytes_read;
    auto error = ReadMemory(dynamic_section_addr + i * sizeof(dynamic_entry),
                            &dynamic_entry, sizeof(dynamic_entry), bytes_read);
    if (!error.Success())
      return LLDB_INVALID_ADDRESS;
    // Return the &DT_DEBUG->d_ptr which points to r_debug which contains the
    // link_map.
    if (dynamic_entry.d_tag == llvm::ELF::DT_DEBUG) {
      return dynamic_section_addr + i * sizeof(dynamic_entry) +
             sizeof(dynamic_entry.d_tag);
    }
  }

  return LLDB_INVALID_ADDRESS;
}

template lldb::addr_t NativeProcessELF::GetELFImageInfoAddress<
    llvm::ELF::Elf32_Ehdr, llvm::ELF::Elf32_Phdr, llvm::ELF::Elf32_Dyn>();
template lldb::addr_t NativeProcessELF::GetELFImageInfoAddress<
    llvm::ELF::Elf64_Ehdr, llvm::ELF::Elf64_Phdr, llvm::ELF::Elf64_Dyn>();

template <typename T>
llvm::Expected<SVR4LibraryInfo>
NativeProcessELF::ReadSVR4LibraryInfo(lldb::addr_t link_map_addr) {
  ELFLinkMap<T> link_map;
  size_t bytes_read;
  auto error =
      ReadMemory(link_map_addr, &link_map, sizeof(link_map), bytes_read);
  if (!error.Success())
    return error.ToError();

  char name_buffer[PATH_MAX];
  llvm::Expected<llvm::StringRef> string_or_error = ReadCStringFromMemory(
      link_map.l_name, &name_buffer[0], sizeof(name_buffer), bytes_read);
  if (!string_or_error)
    return string_or_error.takeError();

  SVR4LibraryInfo info;
  info.name = string_or_error->str();
  info.link_map = link_map_addr;
  info.base_addr = link_map.l_addr;
  info.ld_addr = link_map.l_ld;
  info.next = link_map.l_next;

  return info;
}

llvm::Expected<std::vector<SVR4LibraryInfo>>
NativeProcessELF::GetLoadedSVR4Libraries() {
  // Address of DT_DEBUG.d_ptr which points to r_debug
  lldb::addr_t info_address = GetSharedLibraryInfoAddress();
  if (info_address == LLDB_INVALID_ADDRESS)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "Invalid shared library info address");
  // Address of r_debug
  lldb::addr_t address = 0;
  size_t bytes_read;
  auto status =
      ReadMemory(info_address, &address, GetAddressByteSize(), bytes_read);
  if (!status.Success())
    return status.ToError();
  if (address == 0)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "Invalid r_debug address");
  // Read r_debug.r_map
  lldb::addr_t link_map = 0;
  status = ReadMemory(address + GetAddressByteSize(), &link_map,
                      GetAddressByteSize(), bytes_read);
  if (!status.Success())
    return status.ToError();
  if (link_map == 0)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "Invalid link_map address");

  std::vector<SVR4LibraryInfo> library_list;
  while (link_map) {
    llvm::Expected<SVR4LibraryInfo> info =
        GetAddressByteSize() == 8 ? ReadSVR4LibraryInfo<uint64_t>(link_map)
                                  : ReadSVR4LibraryInfo<uint32_t>(link_map);
    if (!info)
      return info.takeError();
    if (!info->name.empty() && info->base_addr != 0)
      library_list.push_back(*info);
    link_map = info->next;
  }

  return library_list;
}

void NativeProcessELF::NotifyDidExec() {
  NativeProcessProtocol::NotifyDidExec();
  m_shared_library_info_addr.reset();
}

} // namespace lldb_private

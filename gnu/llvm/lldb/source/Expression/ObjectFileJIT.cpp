//===-- ObjectFileJIT.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringRef.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"
#include "lldb/Expression/ObjectFileJIT.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/DataBuffer.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/FileSpecList.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Timer.h"
#include "lldb/Utility/UUID.h"

using namespace lldb;
using namespace lldb_private;

char ObjectFileJIT::ID;

void ObjectFileJIT::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance,
                                CreateMemoryInstance, GetModuleSpecifications);
}

void ObjectFileJIT::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

ObjectFile *ObjectFileJIT::CreateInstance(const lldb::ModuleSP &module_sp,
                                          DataBufferSP data_sp,
                                          lldb::offset_t data_offset,
                                          const FileSpec *file,
                                          lldb::offset_t file_offset,
                                          lldb::offset_t length) {
  // JIT'ed object file is backed by the ObjectFileJITDelegate, never read from
  // a file
  return nullptr;
}

ObjectFile *ObjectFileJIT::CreateMemoryInstance(const lldb::ModuleSP &module_sp,
                                                WritableDataBufferSP data_sp,
                                                const ProcessSP &process_sp,
                                                lldb::addr_t header_addr) {
  // JIT'ed object file is backed by the ObjectFileJITDelegate, never read from
  // memory
  return nullptr;
}

size_t ObjectFileJIT::GetModuleSpecifications(
    const lldb_private::FileSpec &file, lldb::DataBufferSP &data_sp,
    lldb::offset_t data_offset, lldb::offset_t file_offset,
    lldb::offset_t length, lldb_private::ModuleSpecList &specs) {
  // JIT'ed object file can't be read from a file on disk
  return 0;
}

ObjectFileJIT::ObjectFileJIT(const lldb::ModuleSP &module_sp,
                             const ObjectFileJITDelegateSP &delegate_sp)
    : ObjectFile(module_sp, nullptr, 0, 0, DataBufferSP(), 0), m_delegate_wp() {
  if (delegate_sp) {
    m_delegate_wp = delegate_sp;
    m_data.SetByteOrder(delegate_sp->GetByteOrder());
    m_data.SetAddressByteSize(delegate_sp->GetAddressByteSize());
  }
}

ObjectFileJIT::~ObjectFileJIT() = default;

bool ObjectFileJIT::ParseHeader() {
  // JIT code is never in a file, nor is it required to have any header
  return false;
}

ByteOrder ObjectFileJIT::GetByteOrder() const { return m_data.GetByteOrder(); }

bool ObjectFileJIT::IsExecutable() const { return false; }

uint32_t ObjectFileJIT::GetAddressByteSize() const {
  return m_data.GetAddressByteSize();
}

void ObjectFileJIT::ParseSymtab(Symtab &symtab) {
  ObjectFileJITDelegateSP delegate_sp(m_delegate_wp.lock());
  if (delegate_sp)
    delegate_sp->PopulateSymtab(this, symtab);
}

bool ObjectFileJIT::IsStripped() {
  return false; // JIT code that is in a module is never stripped
}

void ObjectFileJIT::CreateSections(SectionList &unified_section_list) {
  if (!m_sections_up) {
    m_sections_up = std::make_unique<SectionList>();
    ObjectFileJITDelegateSP delegate_sp(m_delegate_wp.lock());
    if (delegate_sp) {
      delegate_sp->PopulateSectionList(this, *m_sections_up);
      unified_section_list = *m_sections_up;
    }
  }
}

void ObjectFileJIT::Dump(Stream *s) {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    s->Printf("%p: ", static_cast<void *>(this));
    s->Indent();
    s->PutCString("ObjectFileJIT");

    if (ArchSpec arch = GetArchitecture())
      *s << ", arch = " << arch.GetArchitectureName();

    s->EOL();

    SectionList *sections = GetSectionList();
    if (sections)
      sections->Dump(s->AsRawOstream(), s->GetIndentLevel(), nullptr, true,
                     UINT32_MAX);

    if (m_symtab_up)
      m_symtab_up->Dump(s, nullptr, eSortOrderNone);
  }
}

UUID ObjectFileJIT::GetUUID() {
  // TODO: maybe get from delegate, not needed for first pass
  return UUID();
}

uint32_t ObjectFileJIT::GetDependentModules(FileSpecList &files) {
  // JIT modules don't have dependencies, but they could
  // if external functions are called and we know where they are
  files.Clear();
  return 0;
}

lldb_private::Address ObjectFileJIT::GetEntryPointAddress() {
  return Address();
}

lldb_private::Address ObjectFileJIT::GetBaseAddress() { return Address(); }

ObjectFile::Type ObjectFileJIT::CalculateType() { return eTypeJIT; }

ObjectFile::Strata ObjectFileJIT::CalculateStrata() { return eStrataJIT; }

ArchSpec ObjectFileJIT::GetArchitecture() {
  if (ObjectFileJITDelegateSP delegate_sp = m_delegate_wp.lock())
    return delegate_sp->GetArchitecture();
  return ArchSpec();
}

bool ObjectFileJIT::SetLoadAddress(Target &target, lldb::addr_t value,
                                   bool value_is_offset) {
  size_t num_loaded_sections = 0;
  SectionList *section_list = GetSectionList();
  if (section_list) {
    const size_t num_sections = section_list->GetSize();
    // "value" is an offset to apply to each top level segment
    for (size_t sect_idx = 0; sect_idx < num_sections; ++sect_idx) {
      // Iterate through the object file sections to find all of the sections
      // that size on disk (to avoid __PAGEZERO) and load them
      SectionSP section_sp(section_list->GetSectionAtIndex(sect_idx));
      if (section_sp && section_sp->GetFileSize() > 0 &&
          !section_sp->IsThreadSpecific()) {
        if (target.GetSectionLoadList().SetSectionLoadAddress(
                section_sp, section_sp->GetFileAddress() + value))
          ++num_loaded_sections;
      }
    }
  }
  return num_loaded_sections > 0;
}

size_t ObjectFileJIT::ReadSectionData(lldb_private::Section *section,
                                      lldb::offset_t section_offset, void *dst,
                                      size_t dst_len) {
  lldb::offset_t file_size = section->GetFileSize();
  if (section_offset < file_size) {
    size_t src_len = file_size - section_offset;
    if (src_len > dst_len)
      src_len = dst_len;
    const uint8_t *src =
        ((uint8_t *)(uintptr_t)section->GetFileOffset()) + section_offset;

    memcpy(dst, src, src_len);
    return src_len;
  }
  return 0;
}

size_t
ObjectFileJIT::ReadSectionData(lldb_private::Section *section,
                               lldb_private::DataExtractor &section_data) {
  if (section->GetFileSize()) {
    const void *src = (void *)(uintptr_t)section->GetFileOffset();

    DataBufferSP data_sp =
        std::make_shared<DataBufferHeap>(src, section->GetFileSize());
    section_data.SetData(data_sp, 0, data_sp->GetByteSize());
    section_data.SetByteOrder(GetByteOrder());
    section_data.SetAddressByteSize(GetAddressByteSize());
    return section_data.GetByteSize();
  }
  section_data.Clear();
  return 0;
}

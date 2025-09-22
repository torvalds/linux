//===-- ObjectFileBreakpad.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/ObjectFile/Breakpad/ObjectFileBreakpad.h"
#include "Plugins/ObjectFile/Breakpad/BreakpadRecords.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::breakpad;

LLDB_PLUGIN_DEFINE(ObjectFileBreakpad)

namespace {
struct Header {
  ArchSpec arch;
  UUID uuid;
  static std::optional<Header> parse(llvm::StringRef text);
};
} // namespace

std::optional<Header> Header::parse(llvm::StringRef text) {
  llvm::StringRef line;
  std::tie(line, text) = text.split('\n');
  auto Module = ModuleRecord::parse(line);
  if (!Module)
    return std::nullopt;

  llvm::Triple triple;
  triple.setArch(Module->Arch);
  triple.setOS(Module->OS);

  std::tie(line, text) = text.split('\n');

  auto Info = InfoRecord::parse(line);
  UUID uuid = Info && Info->ID ? Info->ID : Module->ID;
  return Header{ArchSpec(triple), std::move(uuid)};
}

char ObjectFileBreakpad::ID;

void ObjectFileBreakpad::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance,
                                CreateMemoryInstance, GetModuleSpecifications);
}

void ObjectFileBreakpad::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

ObjectFile *ObjectFileBreakpad::CreateInstance(
    const ModuleSP &module_sp, DataBufferSP data_sp, offset_t data_offset,
    const FileSpec *file, offset_t file_offset, offset_t length) {
  if (!data_sp) {
    data_sp = MapFileData(*file, length, file_offset);
    if (!data_sp)
      return nullptr;
    data_offset = 0;
  }
  auto text = toStringRef(data_sp->GetData());
  std::optional<Header> header = Header::parse(text);
  if (!header)
    return nullptr;

  // Update the data to contain the entire file if it doesn't already
  if (data_sp->GetByteSize() < length) {
    data_sp = MapFileData(*file, length, file_offset);
    if (!data_sp)
      return nullptr;
    data_offset = 0;
  }

  return new ObjectFileBreakpad(module_sp, data_sp, data_offset, file,
                                file_offset, length, std::move(header->arch),
                                std::move(header->uuid));
}

ObjectFile *ObjectFileBreakpad::CreateMemoryInstance(
    const ModuleSP &module_sp, WritableDataBufferSP data_sp,
    const ProcessSP &process_sp, addr_t header_addr) {
  return nullptr;
}

size_t ObjectFileBreakpad::GetModuleSpecifications(
    const FileSpec &file, DataBufferSP &data_sp, offset_t data_offset,
    offset_t file_offset, offset_t length, ModuleSpecList &specs) {
  auto text = toStringRef(data_sp->GetData());
  std::optional<Header> header = Header::parse(text);
  if (!header)
    return 0;
  ModuleSpec spec(file, std::move(header->arch));
  spec.GetUUID() = std::move(header->uuid);
  specs.Append(spec);
  return 1;
}

ObjectFileBreakpad::ObjectFileBreakpad(const ModuleSP &module_sp,
                                       DataBufferSP &data_sp,
                                       offset_t data_offset,
                                       const FileSpec *file, offset_t offset,
                                       offset_t length, ArchSpec arch,
                                       UUID uuid)
    : ObjectFile(module_sp, file, offset, length, data_sp, data_offset),
      m_arch(std::move(arch)), m_uuid(std::move(uuid)) {}

bool ObjectFileBreakpad::ParseHeader() {
  // We already parsed the header during initialization.
  return true;
}

void ObjectFileBreakpad::ParseSymtab(Symtab &symtab) {
  // Nothing to do for breakpad files, all information is parsed as debug info
  // which means "lldb_private::Function" objects are used, or symbols are added
  // by the SymbolFileBreakpad::AddSymbols(...) function in the symbol file.
}

void ObjectFileBreakpad::CreateSections(SectionList &unified_section_list) {
  if (m_sections_up)
    return;
  m_sections_up = std::make_unique<SectionList>();

  std::optional<Record::Kind> current_section;
  offset_t section_start;
  llvm::StringRef text = toStringRef(m_data.GetData());
  uint32_t next_section_id = 1;
  auto maybe_add_section = [&](const uint8_t *end_ptr) {
    if (!current_section)
      return; // We have been called before parsing the first line.

    offset_t end_offset = end_ptr - m_data.GetDataStart();
    auto section_sp = std::make_shared<Section>(
        GetModule(), this, next_section_id++,
        ConstString(toString(*current_section)), eSectionTypeOther,
        /*file_vm_addr*/ 0, /*vm_size*/ 0, section_start,
        end_offset - section_start, /*log2align*/ 0, /*flags*/ 0);
    m_sections_up->AddSection(section_sp);
    unified_section_list.AddSection(section_sp);
  };
  while (!text.empty()) {
    llvm::StringRef line;
    std::tie(line, text) = text.split('\n');

    std::optional<Record::Kind> next_section = Record::classify(line);
    if (next_section == Record::Line || next_section == Record::Inline) {
      // Line/Inline records logically belong to the preceding Func record, so
      // we put them in the same section.
      next_section = Record::Func;
    }
    if (next_section == current_section)
      continue;

    // Changing sections, finish off the previous one, if there was any.
    maybe_add_section(line.bytes_begin());
    // And start a new one.
    current_section = next_section;
    section_start = line.bytes_begin() - m_data.GetDataStart();
  }
  // Finally, add the last section.
  maybe_add_section(m_data.GetDataEnd());
}

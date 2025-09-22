//===-- Section.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/Section.h"
#include "lldb/Core/Address.h"
#include "lldb/Core/Module.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/VMRange.h"

#include <cinttypes>
#include <limits>
#include <utility>

namespace lldb_private {
class DataExtractor;
}
using namespace lldb;
using namespace lldb_private;

const char *Section::GetTypeAsCString() const {
  switch (m_type) {
  case eSectionTypeInvalid:
    return "invalid";
  case eSectionTypeCode:
    return "code";
  case eSectionTypeContainer:
    return "container";
  case eSectionTypeData:
    return "data";
  case eSectionTypeDataCString:
    return "data-cstr";
  case eSectionTypeDataCStringPointers:
    return "data-cstr-ptr";
  case eSectionTypeDataSymbolAddress:
    return "data-symbol-addr";
  case eSectionTypeData4:
    return "data-4-byte";
  case eSectionTypeData8:
    return "data-8-byte";
  case eSectionTypeData16:
    return "data-16-byte";
  case eSectionTypeDataPointers:
    return "data-ptrs";
  case eSectionTypeDebug:
    return "debug";
  case eSectionTypeZeroFill:
    return "zero-fill";
  case eSectionTypeDataObjCMessageRefs:
    return "objc-message-refs";
  case eSectionTypeDataObjCCFStrings:
    return "objc-cfstrings";
  case eSectionTypeDWARFDebugAbbrev:
    return "dwarf-abbrev";
  case eSectionTypeDWARFDebugAbbrevDwo:
    return "dwarf-abbrev-dwo";
  case eSectionTypeDWARFDebugAddr:
    return "dwarf-addr";
  case eSectionTypeDWARFDebugAranges:
    return "dwarf-aranges";
  case eSectionTypeDWARFDebugCuIndex:
    return "dwarf-cu-index";
  case eSectionTypeDWARFDebugTuIndex:
    return "dwarf-tu-index";
  case eSectionTypeDWARFDebugFrame:
    return "dwarf-frame";
  case eSectionTypeDWARFDebugInfo:
    return "dwarf-info";
  case eSectionTypeDWARFDebugInfoDwo:
    return "dwarf-info-dwo";
  case eSectionTypeDWARFDebugLine:
    return "dwarf-line";
  case eSectionTypeDWARFDebugLineStr:
    return "dwarf-line-str";
  case eSectionTypeDWARFDebugLoc:
    return "dwarf-loc";
  case eSectionTypeDWARFDebugLocDwo:
    return "dwarf-loc-dwo";
  case eSectionTypeDWARFDebugLocLists:
    return "dwarf-loclists";
  case eSectionTypeDWARFDebugLocListsDwo:
    return "dwarf-loclists-dwo";
  case eSectionTypeDWARFDebugMacInfo:
    return "dwarf-macinfo";
  case eSectionTypeDWARFDebugMacro:
    return "dwarf-macro";
  case eSectionTypeDWARFDebugPubNames:
    return "dwarf-pubnames";
  case eSectionTypeDWARFDebugPubTypes:
    return "dwarf-pubtypes";
  case eSectionTypeDWARFDebugRanges:
    return "dwarf-ranges";
  case eSectionTypeDWARFDebugRngLists:
    return "dwarf-rnglists";
  case eSectionTypeDWARFDebugRngListsDwo:
    return "dwarf-rnglists-dwo";
  case eSectionTypeDWARFDebugStr:
    return "dwarf-str";
  case eSectionTypeDWARFDebugStrDwo:
    return "dwarf-str-dwo";
  case eSectionTypeDWARFDebugStrOffsets:
    return "dwarf-str-offsets";
  case eSectionTypeDWARFDebugStrOffsetsDwo:
    return "dwarf-str-offsets-dwo";
  case eSectionTypeDWARFDebugTypes:
    return "dwarf-types";
  case eSectionTypeDWARFDebugTypesDwo:
    return "dwarf-types-dwo";
  case eSectionTypeDWARFDebugNames:
    return "dwarf-names";
  case eSectionTypeELFSymbolTable:
    return "elf-symbol-table";
  case eSectionTypeELFDynamicSymbols:
    return "elf-dynamic-symbols";
  case eSectionTypeELFRelocationEntries:
    return "elf-relocation-entries";
  case eSectionTypeELFDynamicLinkInfo:
    return "elf-dynamic-link-info";
  case eSectionTypeDWARFAppleNames:
    return "apple-names";
  case eSectionTypeDWARFAppleTypes:
    return "apple-types";
  case eSectionTypeDWARFAppleNamespaces:
    return "apple-namespaces";
  case eSectionTypeDWARFAppleObjC:
    return "apple-objc";
  case eSectionTypeEHFrame:
    return "eh-frame";
  case eSectionTypeARMexidx:
    return "ARM.exidx";
  case eSectionTypeARMextab:
    return "ARM.extab";
  case eSectionTypeCompactUnwind:
    return "compact-unwind";
  case eSectionTypeGoSymtab:
    return "go-symtab";
  case eSectionTypeAbsoluteAddress:
    return "absolute";
  case eSectionTypeDWARFGNUDebugAltLink:
    return "dwarf-gnu-debugaltlink";
  case eSectionTypeCTF:
    return "ctf";
  case eSectionTypeOther:
    return "regular";
  case eSectionTypeSwiftModules:
    return "swift-modules";
  }
  return "unknown";
}

Section::Section(const ModuleSP &module_sp, ObjectFile *obj_file,
                 user_id_t sect_id, ConstString name,
                 SectionType sect_type, addr_t file_addr, addr_t byte_size,
                 lldb::offset_t file_offset, lldb::offset_t file_size,
                 uint32_t log2align, uint32_t flags,
                 uint32_t target_byte_size /*=1*/)
    : ModuleChild(module_sp), UserID(sect_id), Flags(flags),
      m_obj_file(obj_file), m_type(sect_type), m_parent_wp(), m_name(name),
      m_file_addr(file_addr), m_byte_size(byte_size),
      m_file_offset(file_offset), m_file_size(file_size),
      m_log2align(log2align), m_children(), m_fake(false), m_encrypted(false),
      m_thread_specific(false), m_readable(false), m_writable(false),
      m_executable(false), m_relocated(false), m_target_byte_size(target_byte_size) {
}

Section::Section(const lldb::SectionSP &parent_section_sp,
                 const ModuleSP &module_sp, ObjectFile *obj_file,
                 user_id_t sect_id, ConstString name,
                 SectionType sect_type, addr_t file_addr, addr_t byte_size,
                 lldb::offset_t file_offset, lldb::offset_t file_size,
                 uint32_t log2align, uint32_t flags,
                 uint32_t target_byte_size /*=1*/)
    : ModuleChild(module_sp), UserID(sect_id), Flags(flags),
      m_obj_file(obj_file), m_type(sect_type), m_parent_wp(), m_name(name),
      m_file_addr(file_addr), m_byte_size(byte_size),
      m_file_offset(file_offset), m_file_size(file_size),
      m_log2align(log2align), m_children(), m_fake(false), m_encrypted(false),
      m_thread_specific(false), m_readable(false), m_writable(false),
      m_executable(false), m_relocated(false), m_target_byte_size(target_byte_size) {
  if (parent_section_sp)
    m_parent_wp = parent_section_sp;
}

Section::~Section() = default;

addr_t Section::GetFileAddress() const {
  SectionSP parent_sp(GetParent());
  if (parent_sp) {
    // This section has a parent which means m_file_addr is an offset into the
    // parent section, so the file address for this section is the file address
    // of the parent plus the offset
    return parent_sp->GetFileAddress() + m_file_addr;
  }
  // This section has no parent, so m_file_addr is the file base address
  return m_file_addr;
}

bool Section::SetFileAddress(lldb::addr_t file_addr) {
  SectionSP parent_sp(GetParent());
  if (parent_sp) {
    if (m_file_addr >= file_addr)
      return parent_sp->SetFileAddress(m_file_addr - file_addr);
    return false;
  } else {
    // This section has no parent, so m_file_addr is the file base address
    m_file_addr = file_addr;
    return true;
  }
}

lldb::addr_t Section::GetOffset() const {
  // This section has a parent which means m_file_addr is an offset.
  SectionSP parent_sp(GetParent());
  if (parent_sp)
    return m_file_addr;

  // This section has no parent, so there is no offset to be had
  return 0;
}

addr_t Section::GetLoadBaseAddress(Target *target) const {
  addr_t load_base_addr = LLDB_INVALID_ADDRESS;
  SectionSP parent_sp(GetParent());
  if (parent_sp) {
    load_base_addr = parent_sp->GetLoadBaseAddress(target);
    if (load_base_addr != LLDB_INVALID_ADDRESS)
      load_base_addr += GetOffset();
  }
  if (load_base_addr == LLDB_INVALID_ADDRESS) {
    load_base_addr = target->GetSectionLoadList().GetSectionLoadAddress(
        const_cast<Section *>(this)->shared_from_this());
  }
  return load_base_addr;
}

bool Section::ResolveContainedAddress(addr_t offset, Address &so_addr,
                                      bool allow_section_end) const {
  const size_t num_children = m_children.GetSize();
  for (size_t i = 0; i < num_children; i++) {
    Section *child_section = m_children.GetSectionAtIndex(i).get();

    addr_t child_offset = child_section->GetOffset();
    if (child_offset <= offset &&
        offset - child_offset <
            child_section->GetByteSize() + (allow_section_end ? 1 : 0))
      return child_section->ResolveContainedAddress(offset - child_offset,
                                                    so_addr, allow_section_end);
  }
  so_addr.SetOffset(offset);
  so_addr.SetSection(const_cast<Section *>(this)->shared_from_this());

  // Ensure that there are no orphaned (i.e., moduleless) sections.
  assert(GetModule().get());
  return true;
}

bool Section::ContainsFileAddress(addr_t vm_addr) const {
  const addr_t file_addr = GetFileAddress();
  if (file_addr != LLDB_INVALID_ADDRESS && !IsThreadSpecific()) {
    if (file_addr <= vm_addr) {
      const addr_t offset = (vm_addr - file_addr) * m_target_byte_size;
      return offset < GetByteSize();
    }
  }
  return false;
}

void Section::Dump(llvm::raw_ostream &s, unsigned indent, Target *target,
                   uint32_t depth) const {
  s.indent(indent);
  s << llvm::format("0x%16.16" PRIx64 " %-22s ", GetID(), GetTypeAsCString());
  bool resolved = true;
  addr_t addr = LLDB_INVALID_ADDRESS;

  if (GetByteSize() == 0)
    s.indent(39);
  else {
    if (target)
      addr = GetLoadBaseAddress(target);

    if (addr == LLDB_INVALID_ADDRESS) {
      if (target)
        resolved = false;
      addr = GetFileAddress();
    }

    VMRange range(addr, addr + m_byte_size);
    range.Dump(s, 0);
  }

  s << llvm::format("%c %c%c%c  0x%8.8" PRIx64 " 0x%8.8" PRIx64 " 0x%8.8x ",
                    resolved ? ' ' : '*', m_readable ? 'r' : '-',
                    m_writable ? 'w' : '-', m_executable ? 'x' : '-',
                    m_file_offset, m_file_size, Get());

  DumpName(s);

  s << "\n";

  if (depth > 0)
    m_children.Dump(s, indent, target, false, depth - 1);
}

void Section::DumpName(llvm::raw_ostream &s) const {
  SectionSP parent_sp(GetParent());
  if (parent_sp) {
    parent_sp->DumpName(s);
    s << '.';
  } else {
    // The top most section prints the module basename
    const char *name = nullptr;
    ModuleSP module_sp(GetModule());

    if (m_obj_file) {
      const FileSpec &file_spec = m_obj_file->GetFileSpec();
      name = file_spec.GetFilename().AsCString();
    }
    if ((!name || !name[0]) && module_sp)
      name = module_sp->GetFileSpec().GetFilename().AsCString();
    if (name && name[0])
      s << name << '.';
  }
  s << m_name;
}

bool Section::IsDescendant(const Section *section) {
  if (this == section)
    return true;
  SectionSP parent_sp(GetParent());
  if (parent_sp)
    return parent_sp->IsDescendant(section);
  return false;
}

bool Section::Slide(addr_t slide_amount, bool slide_children) {
  if (m_file_addr != LLDB_INVALID_ADDRESS) {
    if (slide_amount == 0)
      return true;

    m_file_addr += slide_amount;

    if (slide_children)
      m_children.Slide(slide_amount, slide_children);

    return true;
  }
  return false;
}

/// Get the permissions as OR'ed bits from lldb::Permissions
uint32_t Section::GetPermissions() const {
  uint32_t permissions = 0;
  if (m_readable)
    permissions |= ePermissionsReadable;
  if (m_writable)
    permissions |= ePermissionsWritable;
  if (m_executable)
    permissions |= ePermissionsExecutable;
  return permissions;
}

/// Set the permissions using bits OR'ed from lldb::Permissions
void Section::SetPermissions(uint32_t permissions) {
  m_readable = (permissions & ePermissionsReadable) != 0;
  m_writable = (permissions & ePermissionsWritable) != 0;
  m_executable = (permissions & ePermissionsExecutable) != 0;
}

lldb::offset_t Section::GetSectionData(void *dst, lldb::offset_t dst_len,
                                       lldb::offset_t offset) {
  if (m_obj_file)
    return m_obj_file->ReadSectionData(this, offset, dst, dst_len);
  return 0;
}

lldb::offset_t Section::GetSectionData(DataExtractor &section_data) {
  if (m_obj_file)
    return m_obj_file->ReadSectionData(this, section_data);
  return 0;
}

bool Section::ContainsOnlyDebugInfo() const {
  switch (m_type) {
  case eSectionTypeInvalid:
  case eSectionTypeCode:
  case eSectionTypeContainer:
  case eSectionTypeData:
  case eSectionTypeDataCString:
  case eSectionTypeDataCStringPointers:
  case eSectionTypeDataSymbolAddress:
  case eSectionTypeData4:
  case eSectionTypeData8:
  case eSectionTypeData16:
  case eSectionTypeDataPointers:
  case eSectionTypeZeroFill:
  case eSectionTypeDataObjCMessageRefs:
  case eSectionTypeDataObjCCFStrings:
  case eSectionTypeELFSymbolTable:
  case eSectionTypeELFDynamicSymbols:
  case eSectionTypeELFRelocationEntries:
  case eSectionTypeELFDynamicLinkInfo:
  case eSectionTypeEHFrame:
  case eSectionTypeARMexidx:
  case eSectionTypeARMextab:
  case eSectionTypeCompactUnwind:
  case eSectionTypeGoSymtab:
  case eSectionTypeAbsoluteAddress:
  case eSectionTypeOther:
  // Used for "__dof_cache" in mach-o or ".debug" for COFF which isn't debug
  // information that we parse at all. This was causing system files with no
  // debug info to show debug info byte sizes in the "statistics dump" output
  // for each module. New "eSectionType" enums should be created for dedicated
  // debug info that has a predefined format if we wish for these sections to
  // show up as debug info.
  case eSectionTypeDebug:
    return false;

  case eSectionTypeDWARFDebugAbbrev:
  case eSectionTypeDWARFDebugAbbrevDwo:
  case eSectionTypeDWARFDebugAddr:
  case eSectionTypeDWARFDebugAranges:
  case eSectionTypeDWARFDebugCuIndex:
  case eSectionTypeDWARFDebugTuIndex:
  case eSectionTypeDWARFDebugFrame:
  case eSectionTypeDWARFDebugInfo:
  case eSectionTypeDWARFDebugInfoDwo:
  case eSectionTypeDWARFDebugLine:
  case eSectionTypeDWARFDebugLineStr:
  case eSectionTypeDWARFDebugLoc:
  case eSectionTypeDWARFDebugLocDwo:
  case eSectionTypeDWARFDebugLocLists:
  case eSectionTypeDWARFDebugLocListsDwo:
  case eSectionTypeDWARFDebugMacInfo:
  case eSectionTypeDWARFDebugMacro:
  case eSectionTypeDWARFDebugPubNames:
  case eSectionTypeDWARFDebugPubTypes:
  case eSectionTypeDWARFDebugRanges:
  case eSectionTypeDWARFDebugRngLists:
  case eSectionTypeDWARFDebugRngListsDwo:
  case eSectionTypeDWARFDebugStr:
  case eSectionTypeDWARFDebugStrDwo:
  case eSectionTypeDWARFDebugStrOffsets:
  case eSectionTypeDWARFDebugStrOffsetsDwo:
  case eSectionTypeDWARFDebugTypes:
  case eSectionTypeDWARFDebugTypesDwo:
  case eSectionTypeDWARFDebugNames:
  case eSectionTypeDWARFAppleNames:
  case eSectionTypeDWARFAppleTypes:
  case eSectionTypeDWARFAppleNamespaces:
  case eSectionTypeDWARFAppleObjC:
  case eSectionTypeDWARFGNUDebugAltLink:
  case eSectionTypeCTF:
  case eSectionTypeSwiftModules:
    return true;
  }
  return false;
}


#pragma mark SectionList

SectionList &SectionList::operator=(const SectionList &rhs) {
  if (this != &rhs)
    m_sections = rhs.m_sections;
  return *this;
}

size_t SectionList::AddSection(const lldb::SectionSP &section_sp) {
  if (section_sp) {
    size_t section_index = m_sections.size();
    m_sections.push_back(section_sp);
    return section_index;
  }

  return std::numeric_limits<size_t>::max();
}

// Warning, this can be slow as it's removing items from a std::vector.
bool SectionList::DeleteSection(size_t idx) {
  if (idx < m_sections.size()) {
    m_sections.erase(m_sections.begin() + idx);
    return true;
  }
  return false;
}

size_t SectionList::FindSectionIndex(const Section *sect) {
  iterator sect_iter;
  iterator begin = m_sections.begin();
  iterator end = m_sections.end();
  for (sect_iter = begin; sect_iter != end; ++sect_iter) {
    if (sect_iter->get() == sect) {
      // The secton was already in this section list
      return std::distance(begin, sect_iter);
    }
  }
  return UINT32_MAX;
}

size_t SectionList::AddUniqueSection(const lldb::SectionSP &sect_sp) {
  size_t sect_idx = FindSectionIndex(sect_sp.get());
  if (sect_idx == UINT32_MAX) {
    sect_idx = AddSection(sect_sp);
  }
  return sect_idx;
}

bool SectionList::ReplaceSection(user_id_t sect_id,
                                 const lldb::SectionSP &sect_sp,
                                 uint32_t depth) {
  iterator sect_iter, end = m_sections.end();
  for (sect_iter = m_sections.begin(); sect_iter != end; ++sect_iter) {
    if ((*sect_iter)->GetID() == sect_id) {
      *sect_iter = sect_sp;
      return true;
    } else if (depth > 0) {
      if ((*sect_iter)
              ->GetChildren()
              .ReplaceSection(sect_id, sect_sp, depth - 1))
        return true;
    }
  }
  return false;
}

size_t SectionList::GetNumSections(uint32_t depth) const {
  size_t count = m_sections.size();
  if (depth > 0) {
    const_iterator sect_iter, end = m_sections.end();
    for (sect_iter = m_sections.begin(); sect_iter != end; ++sect_iter) {
      count += (*sect_iter)->GetChildren().GetNumSections(depth - 1);
    }
  }
  return count;
}

SectionSP SectionList::GetSectionAtIndex(size_t idx) const {
  SectionSP sect_sp;
  if (idx < m_sections.size())
    sect_sp = m_sections[idx];
  return sect_sp;
}

SectionSP
SectionList::FindSectionByName(ConstString section_dstr) const {
  SectionSP sect_sp;
  // Check if we have a valid section string
  if (section_dstr && !m_sections.empty()) {
    const_iterator sect_iter;
    const_iterator end = m_sections.end();
    for (sect_iter = m_sections.begin();
         sect_iter != end && sect_sp.get() == nullptr; ++sect_iter) {
      Section *child_section = sect_iter->get();
      if (child_section) {
        if (child_section->GetName() == section_dstr) {
          sect_sp = *sect_iter;
        } else {
          sect_sp =
              child_section->GetChildren().FindSectionByName(section_dstr);
        }
      }
    }
  }
  return sect_sp;
}

SectionSP SectionList::FindSectionByID(user_id_t sect_id) const {
  SectionSP sect_sp;
  if (sect_id) {
    const_iterator sect_iter;
    const_iterator end = m_sections.end();
    for (sect_iter = m_sections.begin();
         sect_iter != end && sect_sp.get() == nullptr; ++sect_iter) {
      if ((*sect_iter)->GetID() == sect_id) {
        sect_sp = *sect_iter;
        break;
      } else {
        sect_sp = (*sect_iter)->GetChildren().FindSectionByID(sect_id);
      }
    }
  }
  return sect_sp;
}

SectionSP SectionList::FindSectionByType(SectionType sect_type,
                                         bool check_children,
                                         size_t start_idx) const {
  SectionSP sect_sp;
  size_t num_sections = m_sections.size();
  for (size_t idx = start_idx; idx < num_sections; ++idx) {
    if (m_sections[idx]->GetType() == sect_type) {
      sect_sp = m_sections[idx];
      break;
    } else if (check_children) {
      sect_sp = m_sections[idx]->GetChildren().FindSectionByType(
          sect_type, check_children, 0);
      if (sect_sp)
        break;
    }
  }
  return sect_sp;
}

SectionSP SectionList::FindSectionContainingFileAddress(addr_t vm_addr,
                                                        uint32_t depth) const {
  SectionSP sect_sp;
  const_iterator sect_iter;
  const_iterator end = m_sections.end();
  for (sect_iter = m_sections.begin();
       sect_iter != end && sect_sp.get() == nullptr; ++sect_iter) {
    Section *sect = sect_iter->get();
    if (sect->ContainsFileAddress(vm_addr)) {
      // The file address is in this section. We need to make sure one of our
      // child sections doesn't contain this address as well as obeying the
      // depth limit that was passed in.
      if (depth > 0)
        sect_sp = sect->GetChildren().FindSectionContainingFileAddress(
            vm_addr, depth - 1);

      if (sect_sp.get() == nullptr && !sect->IsFake())
        sect_sp = *sect_iter;
    }
  }
  return sect_sp;
}

bool SectionList::ContainsSection(user_id_t sect_id) const {
  return FindSectionByID(sect_id).get() != nullptr;
}

void SectionList::Dump(llvm::raw_ostream &s, unsigned indent, Target *target,
                       bool show_header, uint32_t depth) const {
  bool target_has_loaded_sections =
      target && !target->GetSectionLoadList().IsEmpty();
  if (show_header && !m_sections.empty()) {
    s.indent(indent);
    s << llvm::formatv(
        "SectID             Type                   {0} Address                "
        "             Perm File Off.  File Size  Flags      Section Name\n",
        target_has_loaded_sections ? "Load" : "File");
    s.indent(indent);
    s << "------------------ ---------------------- "
         "---------------------------------------  ---- ---------- ---------- "
         "---------- ----------------------------\n";
  }

  for (const auto &section_sp : m_sections)
    section_sp->Dump(s, indent, target_has_loaded_sections ? target : nullptr,
                     depth);
}

size_t SectionList::Slide(addr_t slide_amount, bool slide_children) {
  size_t count = 0;
  const_iterator pos, end = m_sections.end();
  for (pos = m_sections.begin(); pos != end; ++pos) {
    if ((*pos)->Slide(slide_amount, slide_children))
      ++count;
  }
  return count;
}

uint64_t SectionList::GetDebugInfoSize() const {
  uint64_t debug_info_size = 0;
  for (const auto &section : m_sections) {
    const SectionList &sub_sections = section->GetChildren();
    if (sub_sections.GetSize() > 0)
      debug_info_size += sub_sections.GetDebugInfoSize();
    else if (section->ContainsOnlyDebugInfo())
      debug_info_size += section->GetFileSize();
  }
  return debug_info_size;
}

namespace llvm {
namespace json {

bool fromJSON(const llvm::json::Value &value,
              lldb_private::JSONSection &section, llvm::json::Path path) {
  llvm::json::ObjectMapper o(value, path);
  return o && o.map("name", section.name) && o.map("type", section.type) &&
         o.map("size", section.address) && o.map("size", section.size);
}

bool fromJSON(const llvm::json::Value &value, lldb::SectionType &type,
              llvm::json::Path path) {
  if (auto str = value.getAsString()) {
    type = llvm::StringSwitch<lldb::SectionType>(*str)
               .Case("code", eSectionTypeCode)
               .Case("container", eSectionTypeContainer)
               .Case("data", eSectionTypeData)
               .Case("debug", eSectionTypeDebug)
               .Default(eSectionTypeInvalid);

    if (type == eSectionTypeInvalid) {
      path.report("invalid section type");
      return false;
    }

    return true;
  }
  path.report("expected string");
  return false;
}
} // namespace json
} // namespace llvm

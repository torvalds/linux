//===-- ObjectFilePlaceholder.cpp----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ObjectFilePlaceholder.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/Target.h"

#include <memory>

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(ObjectFilePlaceholder)

ObjectFilePlaceholder::ObjectFilePlaceholder(
    const lldb::ModuleSP &module_sp,
    const lldb_private::ModuleSpec &module_spec, lldb::addr_t base,
    lldb::addr_t size)
    : ObjectFile(module_sp, &module_spec.GetFileSpec(), /*file_offset*/ 0,
                 /*length*/ 0, /*data_sp*/ nullptr, /*data_offset*/ 0),
      m_arch(module_spec.GetArchitecture()), m_uuid(module_spec.GetUUID()),
      m_base(base), m_size(size) {
  m_symtab_up = std::make_unique<lldb_private::Symtab>(this);
}

void ObjectFilePlaceholder::CreateSections(
    lldb_private::SectionList &unified_section_list) {
  m_sections_up = std::make_unique<lldb_private::SectionList>();
  auto section_sp = std::make_shared<lldb_private::Section>(
      GetModule(), this, /*sect_id*/ 0,
      lldb_private::ConstString(".module_image"), eSectionTypeOther, m_base,
      m_size, /*file_offset*/ 0, /*file_size*/ 0,
      /*log2align*/ 0, /*flags*/ 0);
  section_sp->SetPermissions(ePermissionsReadable | ePermissionsExecutable);
  m_sections_up->AddSection(section_sp);
  unified_section_list.AddSection(std::move(section_sp));
}

lldb_private::Address ObjectFilePlaceholder::GetBaseAddress() {
  return lldb_private::Address(m_sections_up->GetSectionAtIndex(0), 0);
}

bool ObjectFilePlaceholder::SetLoadAddress(Target &target, addr_t value,
                                           bool value_is_offset) {
  assert(!value_is_offset);
  assert(value == m_base);

  // Create sections if they haven't been created already.
  GetModule()->GetSectionList();
  assert(m_sections_up->GetNumSections(0) == 1);

  target.GetSectionLoadList().SetSectionLoadAddress(
      m_sections_up->GetSectionAtIndex(0), m_base);
  return true;
}

void ObjectFilePlaceholder::Dump(lldb_private::Stream *s) {
  s->Format("Placeholder object file for {0} loaded at [{1:x}-{2:x})\n",
            GetFileSpec(), m_base, m_base + m_size);
}

//===-- SBCompileUnit.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBCompileUnit.h"
#include "lldb/API/SBLineEntry.h"
#include "lldb/API/SBStream.h"
#include "lldb/Core/Module.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/LineEntry.h"
#include "lldb/Symbol/LineTable.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/TypeList.h"
#include "lldb/Utility/Instrumentation.h"

using namespace lldb;
using namespace lldb_private;

SBCompileUnit::SBCompileUnit() { LLDB_INSTRUMENT_VA(this); }

SBCompileUnit::SBCompileUnit(lldb_private::CompileUnit *lldb_object_ptr)
    : m_opaque_ptr(lldb_object_ptr) {}

SBCompileUnit::SBCompileUnit(const SBCompileUnit &rhs)
    : m_opaque_ptr(rhs.m_opaque_ptr) {
  LLDB_INSTRUMENT_VA(this, rhs);
}

const SBCompileUnit &SBCompileUnit::operator=(const SBCompileUnit &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_opaque_ptr = rhs.m_opaque_ptr;
  return *this;
}

SBCompileUnit::~SBCompileUnit() { m_opaque_ptr = nullptr; }

SBFileSpec SBCompileUnit::GetFileSpec() const {
  LLDB_INSTRUMENT_VA(this);

  SBFileSpec file_spec;
  if (m_opaque_ptr)
    file_spec.SetFileSpec(m_opaque_ptr->GetPrimaryFile());
  return file_spec;
}

uint32_t SBCompileUnit::GetNumLineEntries() const {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_ptr) {
    LineTable *line_table = m_opaque_ptr->GetLineTable();
    if (line_table) {
      return line_table->GetSize();
    }
  }
  return 0;
}

SBLineEntry SBCompileUnit::GetLineEntryAtIndex(uint32_t idx) const {
  LLDB_INSTRUMENT_VA(this, idx);

  SBLineEntry sb_line_entry;
  if (m_opaque_ptr) {
    LineTable *line_table = m_opaque_ptr->GetLineTable();
    if (line_table) {
      LineEntry line_entry;
      if (line_table->GetLineEntryAtIndex(idx, line_entry))
        sb_line_entry.SetLineEntry(line_entry);
    }
  }

  return sb_line_entry;
}

uint32_t SBCompileUnit::FindLineEntryIndex(lldb::SBLineEntry &line_entry,
                                           bool exact) const {
  LLDB_INSTRUMENT_VA(this, line_entry, exact);

  if (!m_opaque_ptr || !line_entry.IsValid())
    return UINT32_MAX;

  LineEntry found_line_entry;

  return m_opaque_ptr->FindLineEntry(0, line_entry.GetLine(),
                                     line_entry.GetFileSpec().get(), exact,
                                     &line_entry.ref());
}

uint32_t SBCompileUnit::FindLineEntryIndex(uint32_t start_idx, uint32_t line,
                                           SBFileSpec *inline_file_spec) const {
  LLDB_INSTRUMENT_VA(this, start_idx, line, inline_file_spec);

  const bool exact = true;
  return FindLineEntryIndex(start_idx, line, inline_file_spec, exact);
}

uint32_t SBCompileUnit::FindLineEntryIndex(uint32_t start_idx, uint32_t line,
                                           SBFileSpec *inline_file_spec,
                                           bool exact) const {
  LLDB_INSTRUMENT_VA(this, start_idx, line, inline_file_spec, exact);

  uint32_t index = UINT32_MAX;
  if (m_opaque_ptr) {
    FileSpec file_spec;
    if (inline_file_spec && inline_file_spec->IsValid())
      file_spec = inline_file_spec->ref();
    else
      file_spec = m_opaque_ptr->GetPrimaryFile();

    LineEntry line_entry;
    index = m_opaque_ptr->FindLineEntry(
        start_idx, line, inline_file_spec ? inline_file_spec->get() : nullptr,
        exact, &line_entry);
  }

  return index;
}

uint32_t SBCompileUnit::GetNumSupportFiles() const {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_ptr)
    return m_opaque_ptr->GetSupportFiles().GetSize();

  return 0;
}

lldb::SBTypeList SBCompileUnit::GetTypes(uint32_t type_mask) {
  LLDB_INSTRUMENT_VA(this, type_mask);

  SBTypeList sb_type_list;

  if (!m_opaque_ptr)
    return sb_type_list;

  ModuleSP module_sp(m_opaque_ptr->GetModule());
  if (!module_sp)
    return sb_type_list;

  SymbolFile *symfile = module_sp->GetSymbolFile();
  if (!symfile)
    return sb_type_list;

  TypeClass type_class = static_cast<TypeClass>(type_mask);
  TypeList type_list;
  symfile->GetTypes(m_opaque_ptr, type_class, type_list);
  sb_type_list.m_opaque_up->Append(type_list);
  return sb_type_list;
}

SBFileSpec SBCompileUnit::GetSupportFileAtIndex(uint32_t idx) const {
  LLDB_INSTRUMENT_VA(this, idx);

  SBFileSpec sb_file_spec;
  if (m_opaque_ptr) {
    FileSpec spec = m_opaque_ptr->GetSupportFiles().GetFileSpecAtIndex(idx);
    sb_file_spec.SetFileSpec(spec);
  }

  return sb_file_spec;
}

uint32_t SBCompileUnit::FindSupportFileIndex(uint32_t start_idx,
                                             const SBFileSpec &sb_file,
                                             bool full) {
  LLDB_INSTRUMENT_VA(this, start_idx, sb_file, full);

  if (m_opaque_ptr) {
    const SupportFileList &support_files = m_opaque_ptr->GetSupportFiles();
    return support_files.FindFileIndex(start_idx, sb_file.ref(), full);
  }
  return 0;
}

lldb::LanguageType SBCompileUnit::GetLanguage() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_ptr)
    return m_opaque_ptr->GetLanguage();
  return lldb::eLanguageTypeUnknown;
}

bool SBCompileUnit::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBCompileUnit::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_ptr != nullptr;
}

bool SBCompileUnit::operator==(const SBCompileUnit &rhs) const {
  LLDB_INSTRUMENT_VA(this, rhs);

  return m_opaque_ptr == rhs.m_opaque_ptr;
}

bool SBCompileUnit::operator!=(const SBCompileUnit &rhs) const {
  LLDB_INSTRUMENT_VA(this, rhs);

  return m_opaque_ptr != rhs.m_opaque_ptr;
}

const lldb_private::CompileUnit *SBCompileUnit::operator->() const {
  return m_opaque_ptr;
}

const lldb_private::CompileUnit &SBCompileUnit::operator*() const {
  return *m_opaque_ptr;
}

lldb_private::CompileUnit *SBCompileUnit::get() { return m_opaque_ptr; }

void SBCompileUnit::reset(lldb_private::CompileUnit *lldb_object_ptr) {
  m_opaque_ptr = lldb_object_ptr;
}

bool SBCompileUnit::GetDescription(SBStream &description) {
  LLDB_INSTRUMENT_VA(this, description);

  Stream &strm = description.ref();

  if (m_opaque_ptr) {
    m_opaque_ptr->Dump(&strm, false);
  } else
    strm.PutCString("No value");

  return true;
}

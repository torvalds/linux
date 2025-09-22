//===-- SBSection.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBSection.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBTarget.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/Section.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Utility/DataBuffer.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Instrumentation.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb;
using namespace lldb_private;

SBSection::SBSection() { LLDB_INSTRUMENT_VA(this); }

SBSection::SBSection(const SBSection &rhs) : m_opaque_wp(rhs.m_opaque_wp) {
  LLDB_INSTRUMENT_VA(this, rhs);
}

SBSection::SBSection(const lldb::SectionSP &section_sp) {
  // Don't init with section_sp otherwise this will throw if
  // section_sp doesn't contain a valid Section *
  if (section_sp)
    m_opaque_wp = section_sp;
}

const SBSection &SBSection::operator=(const SBSection &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_opaque_wp = rhs.m_opaque_wp;
  return *this;
}

SBSection::~SBSection() = default;

bool SBSection::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBSection::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  SectionSP section_sp(GetSP());
  return section_sp && section_sp->GetModule().get() != nullptr;
}

const char *SBSection::GetName() {
  LLDB_INSTRUMENT_VA(this);

  SectionSP section_sp(GetSP());
  if (section_sp)
    return section_sp->GetName().GetCString();
  return nullptr;
}

lldb::SBSection SBSection::GetParent() {
  LLDB_INSTRUMENT_VA(this);

  lldb::SBSection sb_section;
  SectionSP section_sp(GetSP());
  if (section_sp) {
    SectionSP parent_section_sp(section_sp->GetParent());
    if (parent_section_sp)
      sb_section.SetSP(parent_section_sp);
  }
  return sb_section;
}

lldb::SBSection SBSection::FindSubSection(const char *sect_name) {
  LLDB_INSTRUMENT_VA(this, sect_name);

  lldb::SBSection sb_section;
  if (sect_name) {
    SectionSP section_sp(GetSP());
    if (section_sp) {
      ConstString const_sect_name(sect_name);
      sb_section.SetSP(
          section_sp->GetChildren().FindSectionByName(const_sect_name));
    }
  }
  return sb_section;
}

size_t SBSection::GetNumSubSections() {
  LLDB_INSTRUMENT_VA(this);

  SectionSP section_sp(GetSP());
  if (section_sp)
    return section_sp->GetChildren().GetSize();
  return 0;
}

lldb::SBSection SBSection::GetSubSectionAtIndex(size_t idx) {
  LLDB_INSTRUMENT_VA(this, idx);

  lldb::SBSection sb_section;
  SectionSP section_sp(GetSP());
  if (section_sp)
    sb_section.SetSP(section_sp->GetChildren().GetSectionAtIndex(idx));
  return sb_section;
}

lldb::SectionSP SBSection::GetSP() const { return m_opaque_wp.lock(); }

void SBSection::SetSP(const lldb::SectionSP &section_sp) {
  m_opaque_wp = section_sp;
}

lldb::addr_t SBSection::GetFileAddress() {
  LLDB_INSTRUMENT_VA(this);

  lldb::addr_t file_addr = LLDB_INVALID_ADDRESS;
  SectionSP section_sp(GetSP());
  if (section_sp)
    return section_sp->GetFileAddress();
  return file_addr;
}

lldb::addr_t SBSection::GetLoadAddress(lldb::SBTarget &sb_target) {
  LLDB_INSTRUMENT_VA(this, sb_target);

  TargetSP target_sp(sb_target.GetSP());
  if (target_sp) {
    SectionSP section_sp(GetSP());
    if (section_sp)
      return section_sp->GetLoadBaseAddress(target_sp.get());
  }
  return LLDB_INVALID_ADDRESS;
}

lldb::addr_t SBSection::GetByteSize() {
  LLDB_INSTRUMENT_VA(this);

  SectionSP section_sp(GetSP());
  if (section_sp)
    return section_sp->GetByteSize();
  return 0;
}

uint64_t SBSection::GetFileOffset() {
  LLDB_INSTRUMENT_VA(this);

  SectionSP section_sp(GetSP());
  if (section_sp) {
    ModuleSP module_sp(section_sp->GetModule());
    if (module_sp) {
      ObjectFile *objfile = module_sp->GetObjectFile();
      if (objfile)
        return objfile->GetFileOffset() + section_sp->GetFileOffset();
    }
  }
  return UINT64_MAX;
}

uint64_t SBSection::GetFileByteSize() {
  LLDB_INSTRUMENT_VA(this);

  SectionSP section_sp(GetSP());
  if (section_sp)
    return section_sp->GetFileSize();
  return 0;
}

SBData SBSection::GetSectionData() {
  LLDB_INSTRUMENT_VA(this);

  return GetSectionData(0, UINT64_MAX);
}

SBData SBSection::GetSectionData(uint64_t offset, uint64_t size) {
  LLDB_INSTRUMENT_VA(this, offset, size);

  SBData sb_data;
  SectionSP section_sp(GetSP());
  if (section_sp) {
    DataExtractor section_data;
    section_sp->GetSectionData(section_data);
    sb_data.SetOpaque(
        std::make_shared<DataExtractor>(section_data, offset, size));
  }
  return sb_data;
}

SectionType SBSection::GetSectionType() {
  LLDB_INSTRUMENT_VA(this);

  SectionSP section_sp(GetSP());
  if (section_sp.get())
    return section_sp->GetType();
  return eSectionTypeInvalid;
}

uint32_t SBSection::GetPermissions() const {
  LLDB_INSTRUMENT_VA(this);

  SectionSP section_sp(GetSP());
  if (section_sp)
    return section_sp->GetPermissions();
  return 0;
}

uint32_t SBSection::GetTargetByteSize() {
  LLDB_INSTRUMENT_VA(this);

  SectionSP section_sp(GetSP());
  if (section_sp.get())
    return section_sp->GetTargetByteSize();
  return 0;
}

uint32_t SBSection::GetAlignment() {
  LLDB_INSTRUMENT_VA(this);

  SectionSP section_sp(GetSP());
  if (section_sp.get())
    return (1 << section_sp->GetLog2Align());
  return 0;
}

bool SBSection::operator==(const SBSection &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  SectionSP lhs_section_sp(GetSP());
  SectionSP rhs_section_sp(rhs.GetSP());
  if (lhs_section_sp && rhs_section_sp)
    return lhs_section_sp == rhs_section_sp;
  return false;
}

bool SBSection::operator!=(const SBSection &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  SectionSP lhs_section_sp(GetSP());
  SectionSP rhs_section_sp(rhs.GetSP());
  return lhs_section_sp != rhs_section_sp;
}

bool SBSection::GetDescription(SBStream &description) {
  LLDB_INSTRUMENT_VA(this, description);

  Stream &strm = description.ref();

  SectionSP section_sp(GetSP());
  if (section_sp) {
    const addr_t file_addr = section_sp->GetFileAddress();
    strm.Printf("[0x%16.16" PRIx64 "-0x%16.16" PRIx64 ") ", file_addr,
                file_addr + section_sp->GetByteSize());
    section_sp->DumpName(strm.AsRawOstream());
  } else {
    strm.PutCString("No value");
  }

  return true;
}

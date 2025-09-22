//===-- SectionLoadList.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/SectionLoadList.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/Section.h"
#include "lldb/Symbol/Block.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

SectionLoadList::SectionLoadList(const SectionLoadList &rhs)
    : m_addr_to_sect(), m_sect_to_addr(), m_mutex() {
  std::lock_guard<std::recursive_mutex> guard(rhs.m_mutex);
  m_addr_to_sect = rhs.m_addr_to_sect;
  m_sect_to_addr = rhs.m_sect_to_addr;
}

void SectionLoadList::operator=(const SectionLoadList &rhs) {
  std::lock(m_mutex, rhs.m_mutex);
  std::lock_guard<std::recursive_mutex> lhs_guard(m_mutex, std::adopt_lock);
  std::lock_guard<std::recursive_mutex> rhs_guard(rhs.m_mutex, std::adopt_lock);
  m_addr_to_sect = rhs.m_addr_to_sect;
  m_sect_to_addr = rhs.m_sect_to_addr;
}

bool SectionLoadList::IsEmpty() const {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  return m_addr_to_sect.empty();
}

void SectionLoadList::Clear() {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  m_addr_to_sect.clear();
  m_sect_to_addr.clear();
}

addr_t
SectionLoadList::GetSectionLoadAddress(const lldb::SectionSP &section) const {
  // TODO: add support for the same section having multiple load addresses
  addr_t section_load_addr = LLDB_INVALID_ADDRESS;
  if (section) {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    sect_to_addr_collection::const_iterator pos =
        m_sect_to_addr.find(section.get());

    if (pos != m_sect_to_addr.end())
      section_load_addr = pos->second;
  }
  return section_load_addr;
}

bool SectionLoadList::SetSectionLoadAddress(const lldb::SectionSP &section,
                                            addr_t load_addr,
                                            bool warn_multiple) {
  Log *log = GetLog(LLDBLog::DynamicLoader);
  ModuleSP module_sp(section->GetModule());

  if (module_sp) {
    LLDB_LOGV(log, "(section = {0} ({1}.{2}), load_addr = {3:x}) module = {4}",
              section.get(), module_sp->GetFileSpec(), section->GetName(),
              load_addr, module_sp.get());

    if (section->GetByteSize() == 0)
      return false; // No change

    // Fill in the section -> load_addr map
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    sect_to_addr_collection::iterator sta_pos =
        m_sect_to_addr.find(section.get());
    if (sta_pos != m_sect_to_addr.end()) {
      if (load_addr == sta_pos->second)
        return false; // No change...
      else
        sta_pos->second = load_addr;
    } else
      m_sect_to_addr[section.get()] = load_addr;

    // Fill in the load_addr -> section map
    addr_to_sect_collection::iterator ats_pos = m_addr_to_sect.find(load_addr);
    if (ats_pos != m_addr_to_sect.end()) {
      // Some sections are ok to overlap, and for others we should warn. When
      // we have multiple load addresses that correspond to a section, we will
      // always attribute the section to the be last section that claims it
      // exists at that address. Sometimes it is ok for more that one section
      // to be loaded at a specific load address, and other times it isn't. The
      // "warn_multiple" parameter tells us if we should warn in this case or
      // not. The DynamicLoader plug-in subclasses should know which sections
      // should warn and which shouldn't (darwin shared cache modules all
      // shared the same "__LINKEDIT" sections, so the dynamic loader can pass
      // false for "warn_multiple").
      if (warn_multiple && section != ats_pos->second) {
        ModuleSP module_sp(section->GetModule());
        if (module_sp) {
          ModuleSP curr_module_sp(ats_pos->second->GetModule());
          if (curr_module_sp) {
            module_sp->ReportWarning(
                "address {0:x16} maps to more than one section: {1}.{2} and "
                "{3}.{4}",
                load_addr, module_sp->GetFileSpec().GetFilename().GetCString(),
                section->GetName().GetCString(),
                curr_module_sp->GetFileSpec().GetFilename().GetCString(),
                ats_pos->second->GetName().GetCString());
          }
        }
      }
      ats_pos->second = section;
    } else {
      // Remove the old address->section entry, if
      // there is one.
      for (const auto &entry : m_addr_to_sect) {
        if (entry.second == section) {
          const auto &it_pos = m_addr_to_sect.find(entry.first);
          m_addr_to_sect.erase(it_pos);
          break;
        }
      }
      m_addr_to_sect[load_addr] = section;
    }
    return true; // Changed

  } else {
    if (log) {
      LLDB_LOGF(
          log,
          "SectionLoadList::%s (section = %p (%s), load_addr = 0x%16.16" PRIx64
          ") error: module has been deleted",
          __FUNCTION__, static_cast<void *>(section.get()),
          section->GetName().AsCString(), load_addr);
    }
  }
  return false;
}

size_t SectionLoadList::SetSectionUnloaded(const lldb::SectionSP &section_sp) {
  size_t unload_count = 0;

  if (section_sp) {
    Log *log = GetLog(LLDBLog::DynamicLoader);

    if (log && log->GetVerbose()) {
      ModuleSP module_sp = section_sp->GetModule();
      std::string module_name("<Unknown>");
      if (module_sp) {
        const FileSpec &module_file_spec(
            section_sp->GetModule()->GetFileSpec());
        module_name = module_file_spec.GetPath();
      }
      LLDB_LOGF(log, "SectionLoadList::%s (section = %p (%s.%s))", __FUNCTION__,
                static_cast<void *>(section_sp.get()), module_name.c_str(),
                section_sp->GetName().AsCString());
    }

    std::lock_guard<std::recursive_mutex> guard(m_mutex);

    sect_to_addr_collection::iterator sta_pos =
        m_sect_to_addr.find(section_sp.get());
    if (sta_pos != m_sect_to_addr.end()) {
      ++unload_count;
      addr_t load_addr = sta_pos->second;
      m_sect_to_addr.erase(sta_pos);

      addr_to_sect_collection::iterator ats_pos =
          m_addr_to_sect.find(load_addr);
      if (ats_pos != m_addr_to_sect.end())
        m_addr_to_sect.erase(ats_pos);
    }
  }
  return unload_count;
}

bool SectionLoadList::SetSectionUnloaded(const lldb::SectionSP &section_sp,
                                         addr_t load_addr) {
  Log *log = GetLog(LLDBLog::DynamicLoader);

  if (log && log->GetVerbose()) {
    ModuleSP module_sp = section_sp->GetModule();
    std::string module_name("<Unknown>");
    if (module_sp) {
      const FileSpec &module_file_spec(section_sp->GetModule()->GetFileSpec());
      module_name = module_file_spec.GetPath();
    }
    LLDB_LOGF(
        log,
        "SectionLoadList::%s (section = %p (%s.%s), load_addr = 0x%16.16" PRIx64
        ")",
        __FUNCTION__, static_cast<void *>(section_sp.get()),
        module_name.c_str(), section_sp->GetName().AsCString(), load_addr);
  }
  bool erased = false;
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  sect_to_addr_collection::iterator sta_pos =
      m_sect_to_addr.find(section_sp.get());
  if (sta_pos != m_sect_to_addr.end()) {
    erased = true;
    m_sect_to_addr.erase(sta_pos);
  }

  addr_to_sect_collection::iterator ats_pos = m_addr_to_sect.find(load_addr);
  if (ats_pos != m_addr_to_sect.end()) {
    erased = true;
    m_addr_to_sect.erase(ats_pos);
  }

  return erased;
}

bool SectionLoadList::ResolveLoadAddress(addr_t load_addr, Address &so_addr,
                                         bool allow_section_end) const {
  // First find the top level section that this load address exists in
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (!m_addr_to_sect.empty()) {
    addr_to_sect_collection::const_iterator pos =
        m_addr_to_sect.lower_bound(load_addr);
    if (pos != m_addr_to_sect.end()) {
      if (load_addr != pos->first && pos != m_addr_to_sect.begin())
        --pos;
      const addr_t pos_load_addr = pos->first;
      if (load_addr >= pos_load_addr) {
        addr_t offset = load_addr - pos_load_addr;
        if (offset < pos->second->GetByteSize() + (allow_section_end ? 1 : 0)) {
          // We have found the top level section, now we need to find the
          // deepest child section.
          return pos->second->ResolveContainedAddress(offset, so_addr,
                                                      allow_section_end);
        }
      }
    } else {
      // There are no entries that have an address that is >= load_addr, so we
      // need to check the last entry on our collection.
      addr_to_sect_collection::const_reverse_iterator rpos =
          m_addr_to_sect.rbegin();
      if (load_addr >= rpos->first) {
        addr_t offset = load_addr - rpos->first;
        if (offset <
            rpos->second->GetByteSize() + (allow_section_end ? 1 : 0)) {
          // We have found the top level section, now we need to find the
          // deepest child section.
          return rpos->second->ResolveContainedAddress(offset, so_addr,
                                                       allow_section_end);
        }
      }
    }
  }
  so_addr.Clear();
  return false;
}

void SectionLoadList::Dump(Stream &s, Target *target) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  addr_to_sect_collection::const_iterator pos, end;
  for (pos = m_addr_to_sect.begin(), end = m_addr_to_sect.end(); pos != end;
       ++pos) {
    s.Printf("addr = 0x%16.16" PRIx64 ", section = %p: ", pos->first,
             static_cast<void *>(pos->second.get()));
    pos->second->Dump(s.AsRawOstream(), s.GetIndentLevel(), target, 0);
  }
}

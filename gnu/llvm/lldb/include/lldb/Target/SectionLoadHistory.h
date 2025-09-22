//===-- SectionLoadHistory.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_SECTIONLOADHISTORY_H
#define LLDB_TARGET_SECTIONLOADHISTORY_H

#include <map>
#include <mutex>

#include "lldb/lldb-public.h"

namespace lldb_private {

class SectionLoadHistory {
public:
  enum : unsigned {
    // Pass eStopIDNow to any function that takes a stop ID to get the current
    // value.
    eStopIDNow = UINT32_MAX
  };
  // Constructors and Destructors
  SectionLoadHistory() = default;

  ~SectionLoadHistory() {
    // Call clear since this takes a lock and clears the section load list in
    // case another thread is currently using this section load list
    Clear();
  }

  SectionLoadList &GetCurrentSectionLoadList();

  bool IsEmpty() const;

  void Clear();

  uint32_t GetLastStopID() const;

  // Get the section load address given a process stop ID
  lldb::addr_t GetSectionLoadAddress(uint32_t stop_id,
                                     const lldb::SectionSP &section_sp);

  bool ResolveLoadAddress(uint32_t stop_id, lldb::addr_t load_addr,
                          Address &so_addr);

  bool SetSectionLoadAddress(uint32_t stop_id,
                             const lldb::SectionSP &section_sp,
                             lldb::addr_t load_addr,
                             bool warn_multiple = false);

  // The old load address should be specified when unloading to ensure we get
  // the correct instance of the section as a shared library could be loaded at
  // more than one location.
  bool SetSectionUnloaded(uint32_t stop_id, const lldb::SectionSP &section_sp,
                          lldb::addr_t load_addr);

  // Unload all instances of a section. This function can be used on systems
  // that don't support multiple copies of the same shared library to be loaded
  // at the same time.
  size_t SetSectionUnloaded(uint32_t stop_id,
                            const lldb::SectionSP &section_sp);

  void Dump(Stream &s, Target *target);

protected:
  SectionLoadList *GetSectionLoadListForStopID(uint32_t stop_id,
                                               bool read_only);

  typedef std::map<uint32_t, lldb::SectionLoadListSP> StopIDToSectionLoadList;
  StopIDToSectionLoadList m_stop_id_to_section_load_list;
  mutable std::recursive_mutex m_mutex;

private:
  SectionLoadHistory(const SectionLoadHistory &) = delete;
  const SectionLoadHistory &operator=(const SectionLoadHistory &) = delete;
};

} // namespace lldb_private

#endif // LLDB_TARGET_SECTIONLOADHISTORY_H

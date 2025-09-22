//===-- FormatCache.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//




#include "lldb/DataFormatters/FormatCache.h"

using namespace lldb;
using namespace lldb_private;

FormatCache::Entry::Entry()
    : m_format_cached(false), m_summary_cached(false),
      m_synthetic_cached(false) {}

bool FormatCache::Entry::IsFormatCached() { return m_format_cached; }

bool FormatCache::Entry::IsSummaryCached() { return m_summary_cached; }

bool FormatCache::Entry::IsSyntheticCached() { return m_synthetic_cached; }

void FormatCache::Entry::Get(lldb::TypeFormatImplSP &retval) {
  retval = m_format_sp;
}

void FormatCache::Entry::Get(lldb::TypeSummaryImplSP &retval) {
  retval = m_summary_sp;
}

void FormatCache::Entry::Get(lldb::SyntheticChildrenSP &retval) {
  retval = m_synthetic_sp;
}

void FormatCache::Entry::Set(lldb::TypeFormatImplSP format_sp) {
  m_format_cached = true;
  m_format_sp = format_sp;
}

void FormatCache::Entry::Set(lldb::TypeSummaryImplSP summary_sp) {
  m_summary_cached = true;
  m_summary_sp = summary_sp;
}

void FormatCache::Entry::Set(lldb::SyntheticChildrenSP synthetic_sp) {
  m_synthetic_cached = true;
  m_synthetic_sp = synthetic_sp;
}

namespace lldb_private {

template<> bool FormatCache::Entry::IsCached<lldb::TypeFormatImplSP>() {
  return IsFormatCached();
}
template<> bool FormatCache::Entry::IsCached<lldb::TypeSummaryImplSP> () {
  return IsSummaryCached();
}
template<> bool FormatCache::Entry::IsCached<lldb::SyntheticChildrenSP>() {
  return IsSyntheticCached();
}

} // namespace lldb_private

template <typename ImplSP>
bool FormatCache::Get(ConstString type, ImplSP &format_impl_sp) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  auto entry = m_entries[type];
  if (entry.IsCached<ImplSP>()) {
    m_cache_hits++;
    entry.Get(format_impl_sp);
    return true;
  }
  m_cache_misses++;
  format_impl_sp.reset();
  return false;
}

/// Explicit instantiations for the three types.
/// \{
template bool
FormatCache::Get<lldb::TypeFormatImplSP>(ConstString, lldb::TypeFormatImplSP &);
template bool
FormatCache::Get<lldb::TypeSummaryImplSP>(ConstString,
                                          lldb::TypeSummaryImplSP &);
template bool
FormatCache::Get<lldb::SyntheticChildrenSP>(ConstString,
                                            lldb::SyntheticChildrenSP &);
/// \}

void FormatCache::Set(ConstString type, lldb::TypeFormatImplSP &format_sp) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  m_entries[type].Set(format_sp);
}

void FormatCache::Set(ConstString type, lldb::TypeSummaryImplSP &summary_sp) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  m_entries[type].Set(summary_sp);
}

void FormatCache::Set(ConstString type,
                      lldb::SyntheticChildrenSP &synthetic_sp) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  m_entries[type].Set(synthetic_sp);
}

void FormatCache::Clear() {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  m_entries.clear();
}

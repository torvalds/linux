//===-- ModuleSpec.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_MODULESPEC_H
#define LLDB_CORE_MODULESPEC_H

#include "lldb/Host/FileSystem.h"
#include "lldb/Target/PathMappingList.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Iterable.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/UUID.h"

#include "llvm/Support/Chrono.h"

#include <mutex>
#include <vector>

namespace lldb_private {

class ModuleSpec {
public:
  ModuleSpec() = default;

  /// If the \c data argument is passed, its contents will be used
  /// as the module contents instead of trying to read them from
  /// \c file_spec .
  ModuleSpec(const FileSpec &file_spec, const UUID &uuid = UUID(),
             lldb::DataBufferSP data = lldb::DataBufferSP())
      : m_file(file_spec), m_uuid(uuid), m_object_offset(0), m_data(data) {
    if (data)
      m_object_size = data->GetByteSize();
    else if (m_file)
      m_object_size = FileSystem::Instance().GetByteSize(file_spec);
  }

  ModuleSpec(const FileSpec &file_spec, const ArchSpec &arch)
      : m_file(file_spec), m_arch(arch), m_object_offset(0),
        m_object_size(FileSystem::Instance().GetByteSize(file_spec)) {}

  FileSpec *GetFileSpecPtr() { return (m_file ? &m_file : nullptr); }

  const FileSpec *GetFileSpecPtr() const {
    return (m_file ? &m_file : nullptr);
  }

  FileSpec &GetFileSpec() { return m_file; }

  const FileSpec &GetFileSpec() const { return m_file; }

  FileSpec *GetPlatformFileSpecPtr() {
    return (m_platform_file ? &m_platform_file : nullptr);
  }

  const FileSpec *GetPlatformFileSpecPtr() const {
    return (m_platform_file ? &m_platform_file : nullptr);
  }

  FileSpec &GetPlatformFileSpec() { return m_platform_file; }

  const FileSpec &GetPlatformFileSpec() const { return m_platform_file; }

  FileSpec *GetSymbolFileSpecPtr() {
    return (m_symbol_file ? &m_symbol_file : nullptr);
  }

  const FileSpec *GetSymbolFileSpecPtr() const {
    return (m_symbol_file ? &m_symbol_file : nullptr);
  }

  FileSpec &GetSymbolFileSpec() { return m_symbol_file; }

  const FileSpec &GetSymbolFileSpec() const { return m_symbol_file; }

  ArchSpec *GetArchitecturePtr() {
    return (m_arch.IsValid() ? &m_arch : nullptr);
  }

  const ArchSpec *GetArchitecturePtr() const {
    return (m_arch.IsValid() ? &m_arch : nullptr);
  }

  ArchSpec &GetArchitecture() { return m_arch; }

  const ArchSpec &GetArchitecture() const { return m_arch; }

  UUID *GetUUIDPtr() { return (m_uuid.IsValid() ? &m_uuid : nullptr); }

  const UUID *GetUUIDPtr() const {
    return (m_uuid.IsValid() ? &m_uuid : nullptr);
  }

  UUID &GetUUID() { return m_uuid; }

  const UUID &GetUUID() const { return m_uuid; }

  ConstString &GetObjectName() { return m_object_name; }

  ConstString GetObjectName() const { return m_object_name; }

  uint64_t GetObjectOffset() const { return m_object_offset; }

  void SetObjectOffset(uint64_t object_offset) {
    m_object_offset = object_offset;
  }

  uint64_t GetObjectSize() const { return m_object_size; }

  void SetObjectSize(uint64_t object_size) { m_object_size = object_size; }

  llvm::sys::TimePoint<> &GetObjectModificationTime() {
    return m_object_mod_time;
  }

  const llvm::sys::TimePoint<> &GetObjectModificationTime() const {
    return m_object_mod_time;
  }

  PathMappingList &GetSourceMappingList() const { return m_source_mappings; }

  lldb::DataBufferSP GetData() const { return m_data; }

  void Clear() {
    m_file.Clear();
    m_platform_file.Clear();
    m_symbol_file.Clear();
    m_arch.Clear();
    m_uuid.Clear();
    m_object_name.Clear();
    m_object_offset = 0;
    m_object_size = 0;
    m_source_mappings.Clear(false);
    m_object_mod_time = llvm::sys::TimePoint<>();
  }

  explicit operator bool() const {
    if (m_file)
      return true;
    if (m_platform_file)
      return true;
    if (m_symbol_file)
      return true;
    if (m_arch.IsValid())
      return true;
    if (m_uuid.IsValid())
      return true;
    if (m_object_name)
      return true;
    if (m_object_size)
      return true;
    if (m_object_mod_time != llvm::sys::TimePoint<>())
      return true;
    return false;
  }

  void Dump(Stream &strm) const {
    bool dumped_something = false;
    if (m_file) {
      strm.PutCString("file = '");
      strm << m_file;
      strm.PutCString("'");
      dumped_something = true;
    }
    if (m_platform_file) {
      if (dumped_something)
        strm.PutCString(", ");
      strm.PutCString("platform_file = '");
      strm << m_platform_file;
      strm.PutCString("'");
      dumped_something = true;
    }
    if (m_symbol_file) {
      if (dumped_something)
        strm.PutCString(", ");
      strm.PutCString("symbol_file = '");
      strm << m_symbol_file;
      strm.PutCString("'");
      dumped_something = true;
    }
    if (m_arch.IsValid()) {
      if (dumped_something)
        strm.PutCString(", ");
      strm.Printf("arch = ");
      m_arch.DumpTriple(strm.AsRawOstream());
      dumped_something = true;
    }
    if (m_uuid.IsValid()) {
      if (dumped_something)
        strm.PutCString(", ");
      strm.PutCString("uuid = ");
      m_uuid.Dump(strm);
      dumped_something = true;
    }
    if (m_object_name) {
      if (dumped_something)
        strm.PutCString(", ");
      strm.Printf("object_name = %s", m_object_name.GetCString());
      dumped_something = true;
    }
    if (m_object_offset > 0) {
      if (dumped_something)
        strm.PutCString(", ");
      strm.Printf("object_offset = %" PRIu64, m_object_offset);
      dumped_something = true;
    }
    if (m_object_size > 0) {
      if (dumped_something)
        strm.PutCString(", ");
      strm.Printf("object size = %" PRIu64, m_object_size);
      dumped_something = true;
    }
    if (m_object_mod_time != llvm::sys::TimePoint<>()) {
      if (dumped_something)
        strm.PutCString(", ");
      strm.Format("object_mod_time = {0:x+}",
                  uint64_t(llvm::sys::toTimeT(m_object_mod_time)));
    }
  }

  bool Matches(const ModuleSpec &match_module_spec,
               bool exact_arch_match) const {
    if (match_module_spec.GetUUIDPtr() &&
        match_module_spec.GetUUID() != GetUUID())
      return false;
    if (match_module_spec.GetObjectName() &&
        match_module_spec.GetObjectName() != GetObjectName())
      return false;
    if (!FileSpec::Match(match_module_spec.GetFileSpec(), GetFileSpec()))
      return false;
    if (GetPlatformFileSpec() &&
        !FileSpec::Match(match_module_spec.GetPlatformFileSpec(),
                         GetPlatformFileSpec())) {
      return false;
    }
    // Only match the symbol file spec if there is one in this ModuleSpec
    if (GetSymbolFileSpec() &&
        !FileSpec::Match(match_module_spec.GetSymbolFileSpec(),
                         GetSymbolFileSpec())) {
      return false;
    }
    if (match_module_spec.GetArchitecturePtr()) {
      if (exact_arch_match) {
        if (!GetArchitecture().IsExactMatch(
                match_module_spec.GetArchitecture()))
          return false;
      } else {
        if (!GetArchitecture().IsCompatibleMatch(
                match_module_spec.GetArchitecture()))
          return false;
      }
    }
    return true;
  }

protected:
  FileSpec m_file;
  FileSpec m_platform_file;
  FileSpec m_symbol_file;
  ArchSpec m_arch;
  UUID m_uuid;
  ConstString m_object_name;
  uint64_t m_object_offset = 0;
  uint64_t m_object_size = 0;
  llvm::sys::TimePoint<> m_object_mod_time;
  mutable PathMappingList m_source_mappings;
  lldb::DataBufferSP m_data = {};
};

class ModuleSpecList {
public:
  ModuleSpecList() = default;

  ModuleSpecList(const ModuleSpecList &rhs) {
    std::lock_guard<std::recursive_mutex> lhs_guard(m_mutex);
    std::lock_guard<std::recursive_mutex> rhs_guard(rhs.m_mutex);
    m_specs = rhs.m_specs;
  }

  ~ModuleSpecList() = default;

  ModuleSpecList &operator=(const ModuleSpecList &rhs) {
    if (this != &rhs) {
      std::lock(m_mutex, rhs.m_mutex);
      std::lock_guard<std::recursive_mutex> lhs_guard(m_mutex, std::adopt_lock);
      std::lock_guard<std::recursive_mutex> rhs_guard(rhs.m_mutex,
                                                      std::adopt_lock);
      m_specs = rhs.m_specs;
    }
    return *this;
  }

  size_t GetSize() const {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    return m_specs.size();
  }

  void Clear() {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    m_specs.clear();
  }

  void Append(const ModuleSpec &spec) {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    m_specs.push_back(spec);
  }

  void Append(const ModuleSpecList &rhs) {
    std::lock_guard<std::recursive_mutex> lhs_guard(m_mutex);
    std::lock_guard<std::recursive_mutex> rhs_guard(rhs.m_mutex);
    m_specs.insert(m_specs.end(), rhs.m_specs.begin(), rhs.m_specs.end());
  }

  // The index "i" must be valid and this can't be used in multi-threaded code
  // as no mutex lock is taken.
  ModuleSpec &GetModuleSpecRefAtIndex(size_t i) { return m_specs[i]; }

  bool GetModuleSpecAtIndex(size_t i, ModuleSpec &module_spec) const {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    if (i < m_specs.size()) {
      module_spec = m_specs[i];
      return true;
    }
    module_spec.Clear();
    return false;
  }

  bool FindMatchingModuleSpec(const ModuleSpec &module_spec,
                              ModuleSpec &match_module_spec) const {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    bool exact_arch_match = true;
    for (auto spec : m_specs) {
      if (spec.Matches(module_spec, exact_arch_match)) {
        match_module_spec = spec;
        return true;
      }
    }

    // If there was an architecture, retry with a compatible arch
    if (module_spec.GetArchitecturePtr()) {
      exact_arch_match = false;
      for (auto spec : m_specs) {
        if (spec.Matches(module_spec, exact_arch_match)) {
          match_module_spec = spec;
          return true;
        }
      }
    }
    match_module_spec.Clear();
    return false;
  }

  void FindMatchingModuleSpecs(const ModuleSpec &module_spec,
                               ModuleSpecList &matching_list) const {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    bool exact_arch_match = true;
    const size_t initial_match_count = matching_list.GetSize();
    for (auto spec : m_specs) {
      if (spec.Matches(module_spec, exact_arch_match))
        matching_list.Append(spec);
    }

    // If there was an architecture, retry with a compatible arch if no matches
    // were found
    if (module_spec.GetArchitecturePtr() &&
        (initial_match_count == matching_list.GetSize())) {
      exact_arch_match = false;
      for (auto spec : m_specs) {
        if (spec.Matches(module_spec, exact_arch_match))
          matching_list.Append(spec);
      }
    }
  }

  void Dump(Stream &strm) {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    uint32_t idx = 0;
    for (auto spec : m_specs) {
      strm.Printf("[%u] ", idx);
      spec.Dump(strm);
      strm.EOL();
      ++idx;
    }
  }

  typedef std::vector<ModuleSpec> collection;
  typedef LockingAdaptedIterable<collection, ModuleSpec, vector_adapter,
                                 std::recursive_mutex>
      ModuleSpecIterable;

  ModuleSpecIterable ModuleSpecs() {
    return ModuleSpecIterable(m_specs, m_mutex);
  }

protected:
  collection m_specs;                         ///< The collection of modules.
  mutable std::recursive_mutex m_mutex;
};

} // namespace lldb_private

#endif // LLDB_CORE_MODULESPEC_H

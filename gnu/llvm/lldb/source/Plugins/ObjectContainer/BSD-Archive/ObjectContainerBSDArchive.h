//===-- ObjectContainerBSDArchive.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_OBJECTCONTAINER_BSD_ARCHIVE_OBJECTCONTAINERBSDARCHIVE_H
#define LLDB_SOURCE_PLUGINS_OBJECTCONTAINER_BSD_ARCHIVE_OBJECTCONTAINERBSDARCHIVE_H

#include "lldb/Core/UniqueCStringMap.h"
#include "lldb/Symbol/ObjectContainer.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/FileSpec.h"

#include "llvm/Object/Archive.h"
#include "llvm/Support/Chrono.h"
#include "llvm/Support/Path.h"

#include <map>
#include <memory>
#include <mutex>

enum class ArchiveType { Invalid, Archive, ThinArchive };

class ObjectContainerBSDArchive : public lldb_private::ObjectContainer {
public:
  ObjectContainerBSDArchive(const lldb::ModuleSP &module_sp,
                            lldb::DataBufferSP &data_sp,
                            lldb::offset_t data_offset,
                            const lldb_private::FileSpec *file,
                            lldb::offset_t offset, lldb::offset_t length,
                            ArchiveType archive_type);

  ~ObjectContainerBSDArchive() override;

  // Static Functions
  static void Initialize();

  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "bsd-archive"; }

  static llvm::StringRef GetPluginDescriptionStatic() {
    return "BSD Archive object container reader.";
  }

  static lldb_private::ObjectContainer *
  CreateInstance(const lldb::ModuleSP &module_sp, lldb::DataBufferSP &data_sp,
                 lldb::offset_t data_offset, const lldb_private::FileSpec *file,
                 lldb::offset_t offset, lldb::offset_t length);

  static size_t GetModuleSpecifications(const lldb_private::FileSpec &file,
                                        lldb::DataBufferSP &data_sp,
                                        lldb::offset_t data_offset,
                                        lldb::offset_t file_offset,
                                        lldb::offset_t length,
                                        lldb_private::ModuleSpecList &specs);

  static ArchiveType MagicBytesMatch(const lldb_private::DataExtractor &data);

  // Member Functions
  bool ParseHeader() override;

  size_t GetNumObjects() const override {
    if (m_archive_sp)
      return m_archive_sp->GetNumObjects();
    return 0;
  }

  lldb::ObjectFileSP GetObjectFile(const lldb_private::FileSpec *file) override;

  // PluginInterface protocol
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

protected:
  struct Object {
    Object();

    void Clear();

    lldb::offset_t ExtractFromThin(const lldb_private::DataExtractor &data,
                                   lldb::offset_t offset,
                                   llvm::StringRef stringTable);

    lldb::offset_t Extract(const lldb_private::DataExtractor &data,
                           lldb::offset_t offset);
    /// Object name in the archive.
    lldb_private::ConstString ar_name;

    /// Object modification time in the archive.
    uint32_t modification_time = 0;

    /// Object size in bytes in the archive.
    uint32_t size = 0;

    /// File offset in bytes from the beginning of the file of the object data.
    lldb::offset_t file_offset = 0;

    /// Length of the object data.
    lldb::offset_t file_size = 0;

    void Dump() const;
  };

  class Archive {
  public:
    typedef std::shared_ptr<Archive> shared_ptr;
    typedef std::multimap<lldb_private::FileSpec, shared_ptr> Map;

    Archive(const lldb_private::ArchSpec &arch,
            const llvm::sys::TimePoint<> &mod_time, lldb::offset_t file_offset,
            lldb_private::DataExtractor &data, ArchiveType archive_type);

    ~Archive();

    static Map &GetArchiveCache();

    static std::recursive_mutex &GetArchiveCacheMutex();

    static Archive::shared_ptr FindCachedArchive(
        const lldb_private::FileSpec &file, const lldb_private::ArchSpec &arch,
        const llvm::sys::TimePoint<> &mod_time, lldb::offset_t file_offset);

    static Archive::shared_ptr ParseAndCacheArchiveForFile(
        const lldb_private::FileSpec &file, const lldb_private::ArchSpec &arch,
        const llvm::sys::TimePoint<> &mod_time, lldb::offset_t file_offset,
        lldb_private::DataExtractor &data, ArchiveType archive_type);

    size_t GetNumObjects() const { return m_objects.size(); }

    const Object *GetObjectAtIndex(size_t idx) {
      if (idx < m_objects.size())
        return &m_objects[idx];
      return nullptr;
    }

    size_t ParseObjects();

    Object *FindObject(lldb_private::ConstString object_name,
                       const llvm::sys::TimePoint<> &object_mod_time);

    lldb::offset_t GetFileOffset() const { return m_file_offset; }

    const llvm::sys::TimePoint<> &GetModificationTime() {
      return m_modification_time;
    }

    const lldb_private::ArchSpec &GetArchitecture() const { return m_arch; }

    void SetArchitecture(const lldb_private::ArchSpec &arch) { m_arch = arch; }

    bool HasNoExternalReferences() const;

    lldb_private::DataExtractor &GetData() { return m_data; }

    ArchiveType GetArchiveType() { return m_archive_type; }

  protected:
    typedef lldb_private::UniqueCStringMap<uint32_t> ObjectNameToIndexMap;
    // Member Variables
    lldb_private::ArchSpec m_arch;
    llvm::sys::TimePoint<> m_modification_time;
    lldb::offset_t m_file_offset;
    std::vector<Object> m_objects;
    ObjectNameToIndexMap m_object_name_to_index_map;
    lldb_private::DataExtractor m_data; ///< The data for this object container
                                        ///so we don't lose data if the .a files
                                        ///gets modified
    ArchiveType m_archive_type;
  };

  void SetArchive(Archive::shared_ptr &archive_sp);

  Archive::shared_ptr m_archive_sp;

  ArchiveType m_archive_type;
};

#endif // LLDB_SOURCE_PLUGINS_OBJECTCONTAINER_BSD_ARCHIVE_OBJECTCONTAINERBSDARCHIVE_H

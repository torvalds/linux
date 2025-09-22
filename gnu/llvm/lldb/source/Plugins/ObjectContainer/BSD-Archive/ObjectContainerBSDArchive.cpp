//===-- ObjectContainerBSDArchive.cpp -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ObjectContainerBSDArchive.h"

#if defined(_WIN32) || defined(__ANDROID__)
// Defines from ar, missing on Windows
#define SARMAG 8
#define ARFMAG "`\n"

typedef struct ar_hdr {
  char ar_name[16];
  char ar_date[12];
  char ar_uid[6], ar_gid[6];
  char ar_mode[8];
  char ar_size[10];
  char ar_fmag[2];
} ar_hdr;
#else
#include <ar.h>
#endif

#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/Timer.h"

#include "llvm/Object/Archive.h"
#include "llvm/Support/MemoryBuffer.h"

using namespace lldb;
using namespace lldb_private;

using namespace llvm::object;

LLDB_PLUGIN_DEFINE(ObjectContainerBSDArchive)

ObjectContainerBSDArchive::Object::Object() : ar_name() {}

void ObjectContainerBSDArchive::Object::Clear() {
  ar_name.Clear();
  modification_time = 0;
  size = 0;
  file_offset = 0;
  file_size = 0;
}

void ObjectContainerBSDArchive::Object::Dump() const {
  printf("name        = \"%s\"\n", ar_name.GetCString());
  printf("mtime       = 0x%8.8" PRIx32 "\n", modification_time);
  printf("size        = 0x%8.8" PRIx32 " (%" PRIu32 ")\n", size, size);
  printf("file_offset = 0x%16.16" PRIx64 " (%" PRIu64 ")\n", file_offset,
         file_offset);
  printf("file_size   = 0x%16.16" PRIx64 " (%" PRIu64 ")\n\n", file_size,
         file_size);
}

ObjectContainerBSDArchive::Archive::Archive(const lldb_private::ArchSpec &arch,
                                            const llvm::sys::TimePoint<> &time,
                                            lldb::offset_t file_offset,
                                            lldb_private::DataExtractor &data,
                                            ArchiveType archive_type)
    : m_arch(arch), m_modification_time(time), m_file_offset(file_offset),
      m_objects(), m_data(data), m_archive_type(archive_type) {}

Log *l = GetLog(LLDBLog::Object);
ObjectContainerBSDArchive::Archive::~Archive() = default;

size_t ObjectContainerBSDArchive::Archive::ParseObjects() {
  DataExtractor &data = m_data;

  std::unique_ptr<llvm::MemoryBuffer> mem_buffer =
      llvm::MemoryBuffer::getMemBuffer(
            llvm::StringRef((const char *)data.GetDataStart(),
                            data.GetByteSize()),
            llvm::StringRef(),
            /*RequiresNullTerminator=*/false);

  auto exp_ar = llvm::object::Archive::create(mem_buffer->getMemBufferRef());
  if (!exp_ar) {
    LLDB_LOG_ERROR(l, exp_ar.takeError(), "failed to create archive: {0}");
    return 0;
  }
  auto llvm_archive = std::move(exp_ar.get());

  llvm::Error iter_err = llvm::Error::success();
  Object obj;
  for (const auto &child: llvm_archive->children(iter_err)) {
    obj.Clear();
    auto exp_name = child.getName();
    if (exp_name) {
      obj.ar_name = ConstString(exp_name.get());
    } else {
      LLDB_LOG_ERROR(l, exp_name.takeError(),
                     "failed to get archive object name: {0}");
      continue;
    }

    auto exp_mtime = child.getLastModified();
    if (exp_mtime) {
      obj.modification_time =
          std::chrono::duration_cast<std::chrono::seconds>(
              std::chrono::time_point_cast<std::chrono::seconds>(
                    exp_mtime.get()).time_since_epoch()).count();
    } else {
      LLDB_LOG_ERROR(l, exp_mtime.takeError(),
                     "failed to get archive object time: {0}");
      continue;
    }

    auto exp_size = child.getRawSize();
    if (exp_size) {
      obj.size = exp_size.get();
    } else {
      LLDB_LOG_ERROR(l, exp_size.takeError(),
                     "failed to get archive object size: {0}");
      continue;
    }

    obj.file_offset = child.getDataOffset();

    auto exp_file_size = child.getSize();
    if (exp_file_size) {
      obj.file_size = exp_file_size.get();
    } else {
      LLDB_LOG_ERROR(l, exp_file_size.takeError(),
                     "failed to get archive object file size: {0}");
      continue;
    }
    m_object_name_to_index_map.Append(obj.ar_name, m_objects.size());
    m_objects.push_back(obj);
  }
  if (iter_err) {
    LLDB_LOG_ERROR(l, std::move(iter_err),
                   "failed to iterate over archive objects: {0}");
  }
  // Now sort all of the object name pointers
  m_object_name_to_index_map.Sort();
  return m_objects.size();
}

ObjectContainerBSDArchive::Object *
ObjectContainerBSDArchive::Archive::FindObject(
    ConstString object_name, const llvm::sys::TimePoint<> &object_mod_time) {
  const ObjectNameToIndexMap::Entry *match =
      m_object_name_to_index_map.FindFirstValueForName(object_name);
  if (!match)
    return nullptr;
  if (object_mod_time == llvm::sys::TimePoint<>())
    return &m_objects[match->value];

  const uint64_t object_modification_date = llvm::sys::toTimeT(object_mod_time);
  if (m_objects[match->value].modification_time == object_modification_date)
    return &m_objects[match->value];

  const ObjectNameToIndexMap::Entry *next_match =
      m_object_name_to_index_map.FindNextValueForName(match);
  while (next_match) {
    if (m_objects[next_match->value].modification_time ==
        object_modification_date)
      return &m_objects[next_match->value];
    next_match = m_object_name_to_index_map.FindNextValueForName(next_match);
  }

  return nullptr;
}

ObjectContainerBSDArchive::Archive::shared_ptr
ObjectContainerBSDArchive::Archive::FindCachedArchive(
    const FileSpec &file, const ArchSpec &arch,
    const llvm::sys::TimePoint<> &time, lldb::offset_t file_offset) {
  std::lock_guard<std::recursive_mutex> guard(Archive::GetArchiveCacheMutex());
  shared_ptr archive_sp;
  Archive::Map &archive_map = Archive::GetArchiveCache();
  Archive::Map::iterator pos = archive_map.find(file);
  // Don't cache a value for "archive_map.end()" below since we might delete an
  // archive entry...
  while (pos != archive_map.end() && pos->first == file) {
    bool match = true;
    if (arch.IsValid() &&
        !pos->second->GetArchitecture().IsCompatibleMatch(arch))
      match = false;
    else if (file_offset != LLDB_INVALID_OFFSET &&
             pos->second->GetFileOffset() != file_offset)
      match = false;
    if (match) {
      if (pos->second->GetModificationTime() == time) {
        return pos->second;
      } else {
        // We have a file at the same path with the same architecture whose
        // modification time doesn't match. It doesn't make sense for us to
        // continue to use this BSD archive since we cache only the object info
        // which consists of file time info and also the file offset and file
        // size of any contained objects. Since this information is now out of
        // date, we won't get the correct information if we go and extract the
        // file data, so we should remove the old and outdated entry.
        archive_map.erase(pos);
        pos = archive_map.find(file);
        continue; // Continue to next iteration so we don't increment pos
                  // below...
      }
    }
    ++pos;
  }
  return archive_sp;
}

ObjectContainerBSDArchive::Archive::shared_ptr
ObjectContainerBSDArchive::Archive::ParseAndCacheArchiveForFile(
    const FileSpec &file, const ArchSpec &arch,
    const llvm::sys::TimePoint<> &time, lldb::offset_t file_offset,
    DataExtractor &data, ArchiveType archive_type) {
  shared_ptr archive_sp(
      new Archive(arch, time, file_offset, data, archive_type));
  if (archive_sp) {
    const size_t num_objects = archive_sp->ParseObjects();
    if (num_objects > 0) {
      std::lock_guard<std::recursive_mutex> guard(
          Archive::GetArchiveCacheMutex());
      Archive::GetArchiveCache().insert(std::make_pair(file, archive_sp));
    } else {
      archive_sp.reset();
    }
  }
  return archive_sp;
}

ObjectContainerBSDArchive::Archive::Map &
ObjectContainerBSDArchive::Archive::GetArchiveCache() {
  static Archive::Map g_archive_map;
  return g_archive_map;
}

std::recursive_mutex &
ObjectContainerBSDArchive::Archive::GetArchiveCacheMutex() {
  static std::recursive_mutex g_archive_map_mutex;
  return g_archive_map_mutex;
}

void ObjectContainerBSDArchive::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance,
                                GetModuleSpecifications);
}

void ObjectContainerBSDArchive::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

ObjectContainer *ObjectContainerBSDArchive::CreateInstance(
    const lldb::ModuleSP &module_sp, DataBufferSP &data_sp,
    lldb::offset_t data_offset, const FileSpec *file,
    lldb::offset_t file_offset, lldb::offset_t length) {
  ConstString object_name(module_sp->GetObjectName());
  if (!object_name)
    return nullptr;

  if (data_sp) {
    // We have data, which means this is the first 512 bytes of the file Check
    // to see if the magic bytes match and if they do, read the entire table of
    // contents for the archive and cache it
    DataExtractor data;
    data.SetData(data_sp, data_offset, length);
    ArchiveType archive_type = ObjectContainerBSDArchive::MagicBytesMatch(data);
    if (file && data_sp && archive_type != ArchiveType::Invalid) {
      LLDB_SCOPED_TIMERF(
          "ObjectContainerBSDArchive::CreateInstance (module = %s, file = "
          "%p, file_offset = 0x%8.8" PRIx64 ", file_size = 0x%8.8" PRIx64 ")",
          module_sp->GetFileSpec().GetPath().c_str(),
          static_cast<const void *>(file), static_cast<uint64_t>(file_offset),
          static_cast<uint64_t>(length));

      // Map the entire .a file to be sure that we don't lose any data if the
      // file gets updated by a new build while this .a file is being used for
      // debugging
      DataBufferSP archive_data_sp =
          FileSystem::Instance().CreateDataBuffer(*file, length, file_offset);
      if (!archive_data_sp)
        return nullptr;

      lldb::offset_t archive_data_offset = 0;

      Archive::shared_ptr archive_sp(Archive::FindCachedArchive(
          *file, module_sp->GetArchitecture(), module_sp->GetModificationTime(),
          file_offset));
      std::unique_ptr<ObjectContainerBSDArchive> container_up(
          new ObjectContainerBSDArchive(module_sp, archive_data_sp,
                                        archive_data_offset, file, file_offset,
                                        length, archive_type));

      if (container_up) {
        if (archive_sp) {
          // We already have this archive in our cache, use it
          container_up->SetArchive(archive_sp);
          return container_up.release();
        } else if (container_up->ParseHeader())
          return container_up.release();
      }
    }
  } else {
    // No data, just check for a cached archive
    Archive::shared_ptr archive_sp(Archive::FindCachedArchive(
        *file, module_sp->GetArchitecture(), module_sp->GetModificationTime(),
        file_offset));
    if (archive_sp) {
      std::unique_ptr<ObjectContainerBSDArchive> container_up(
          new ObjectContainerBSDArchive(module_sp, data_sp, data_offset, file,
                                        file_offset, length,
                                        archive_sp->GetArchiveType()));

      if (container_up) {
        // We already have this archive in our cache, use it
        container_up->SetArchive(archive_sp);
        return container_up.release();
      }
    }
  }
  return nullptr;
}

ArchiveType
ObjectContainerBSDArchive::MagicBytesMatch(const DataExtractor &data) {
  uint32_t offset = 0;
  const char *armag = (const char *)data.PeekData(offset,
                                                  sizeof(ar_hdr) + SARMAG);
  if (armag == nullptr)
    return ArchiveType::Invalid;
  ArchiveType result = ArchiveType::Invalid;
  if (strncmp(armag, ArchiveMagic, SARMAG) == 0)
      result = ArchiveType::Archive;
  else if (strncmp(armag, ThinArchiveMagic, SARMAG) == 0)
      result = ArchiveType::ThinArchive;
  else
      return ArchiveType::Invalid;

  armag += offsetof(struct ar_hdr, ar_fmag) + SARMAG;
  if (strncmp(armag, ARFMAG, 2) == 0)
      return result;
  return ArchiveType::Invalid;
}

ObjectContainerBSDArchive::ObjectContainerBSDArchive(
    const lldb::ModuleSP &module_sp, DataBufferSP &data_sp,
    lldb::offset_t data_offset, const lldb_private::FileSpec *file,
    lldb::offset_t file_offset, lldb::offset_t size, ArchiveType archive_type)
    : ObjectContainer(module_sp, file, file_offset, size, data_sp, data_offset),
      m_archive_sp() {
  m_archive_type = archive_type;
}

void ObjectContainerBSDArchive::SetArchive(Archive::shared_ptr &archive_sp) {
  m_archive_sp = archive_sp;
}

ObjectContainerBSDArchive::~ObjectContainerBSDArchive() = default;

bool ObjectContainerBSDArchive::ParseHeader() {
  if (m_archive_sp.get() == nullptr) {
    if (m_data.GetByteSize() > 0) {
      ModuleSP module_sp(GetModule());
      if (module_sp) {
        m_archive_sp = Archive::ParseAndCacheArchiveForFile(
            m_file, module_sp->GetArchitecture(),
            module_sp->GetModificationTime(), m_offset, m_data, m_archive_type);
      }
      // Clear the m_data that contains the entire archive data and let our
      // m_archive_sp hold onto the data.
      m_data.Clear();
    }
  }
  return m_archive_sp.get() != nullptr;
}

FileSpec GetChildFileSpecificationsFromThin(llvm::StringRef childPath,
                                            const FileSpec &parentFileSpec) {
  llvm::SmallString<128> FullPath;
  if (llvm::sys::path::is_absolute(childPath)) {
    FullPath = childPath;
  } else {
    FullPath = parentFileSpec.GetDirectory().GetStringRef();
    llvm::sys::path::append(FullPath, childPath);
  }
  FileSpec child = FileSpec(FullPath.str(), llvm::sys::path::Style::posix);
  return child;
}

ObjectFileSP ObjectContainerBSDArchive::GetObjectFile(const FileSpec *file) {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    if (module_sp->GetObjectName() && m_archive_sp) {
      Object *object = m_archive_sp->FindObject(
          module_sp->GetObjectName(), module_sp->GetObjectModificationTime());
      if (object) {
        if (m_archive_type == ArchiveType::ThinArchive) {
          // Set file to child object file
          FileSpec child = GetChildFileSpecificationsFromThin(
              object->ar_name.GetStringRef(), m_file);
          lldb::offset_t file_offset = 0;
          lldb::offset_t file_size = object->size;
          std::shared_ptr<DataBuffer> child_data_sp =
              FileSystem::Instance().CreateDataBuffer(child, file_size,
                                                      file_offset);
          if (!child_data_sp ||
              child_data_sp->GetByteSize() != object->file_size)
            return ObjectFileSP();
          lldb::offset_t data_offset = 0;
          return ObjectFile::FindPlugin(
              module_sp, &child, m_offset + object->file_offset,
              object->file_size, child_data_sp, data_offset);
        }
        lldb::offset_t data_offset = object->file_offset;
        return ObjectFile::FindPlugin(
            module_sp, file, m_offset + object->file_offset, object->file_size,
            m_archive_sp->GetData().GetSharedDataBuffer(), data_offset);
      }
    }
  }
  return ObjectFileSP();
}

size_t ObjectContainerBSDArchive::GetModuleSpecifications(
    const lldb_private::FileSpec &file, lldb::DataBufferSP &data_sp,
    lldb::offset_t data_offset, lldb::offset_t file_offset,
    lldb::offset_t file_size, lldb_private::ModuleSpecList &specs) {

  // We have data, which means this is the first 512 bytes of the file Check to
  // see if the magic bytes match and if they do, read the entire table of
  // contents for the archive and cache it
  DataExtractor data;
  data.SetData(data_sp, data_offset, data_sp->GetByteSize());
  ArchiveType archive_type = ObjectContainerBSDArchive::MagicBytesMatch(data);
  if (!file || !data_sp || archive_type == ArchiveType::Invalid)
    return 0;

  const size_t initial_count = specs.GetSize();
  llvm::sys::TimePoint<> file_mod_time = FileSystem::Instance().GetModificationTime(file);
  Archive::shared_ptr archive_sp(
      Archive::FindCachedArchive(file, ArchSpec(), file_mod_time, file_offset));
  bool set_archive_arch = false;
  if (!archive_sp) {
    set_archive_arch = true;
    data_sp =
        FileSystem::Instance().CreateDataBuffer(file, file_size, file_offset);
    if (data_sp) {
      data.SetData(data_sp, 0, data_sp->GetByteSize());
      archive_sp = Archive::ParseAndCacheArchiveForFile(
          file, ArchSpec(), file_mod_time, file_offset, data, archive_type);
    }
  }

  if (archive_sp) {
    const size_t num_objects = archive_sp->GetNumObjects();
    for (size_t idx = 0; idx < num_objects; ++idx) {
      const Object *object = archive_sp->GetObjectAtIndex(idx);
      if (object) {
        if (archive_sp->GetArchiveType() == ArchiveType::ThinArchive) {
          if (object->ar_name.IsEmpty())
            continue;
          FileSpec child = GetChildFileSpecificationsFromThin(
              object->ar_name.GetStringRef(), file);
          if (ObjectFile::GetModuleSpecifications(child, 0, object->file_size,
                                                  specs)) {
            ModuleSpec &spec =
                specs.GetModuleSpecRefAtIndex(specs.GetSize() - 1);
            llvm::sys::TimePoint<> object_mod_time(
                std::chrono::seconds(object->modification_time));
            spec.GetObjectName() = object->ar_name;
            spec.SetObjectOffset(0);
            spec.SetObjectSize(object->file_size);
            spec.GetObjectModificationTime() = object_mod_time;
          }
          continue;
        }
        const lldb::offset_t object_file_offset =
            file_offset + object->file_offset;
        if (object->file_offset < file_size && file_size > object_file_offset) {
          if (ObjectFile::GetModuleSpecifications(
                  file, object_file_offset, file_size - object_file_offset,
                  specs)) {
            ModuleSpec &spec =
                specs.GetModuleSpecRefAtIndex(specs.GetSize() - 1);
            llvm::sys::TimePoint<> object_mod_time(
                std::chrono::seconds(object->modification_time));
            spec.GetObjectName() = object->ar_name;
            spec.SetObjectOffset(object_file_offset);
            spec.SetObjectSize(object->file_size);
            spec.GetObjectModificationTime() = object_mod_time;
          }
        }
      }
    }
  }
  const size_t end_count = specs.GetSize();
  size_t num_specs_added = end_count - initial_count;
  if (set_archive_arch && num_specs_added > 0) {
    // The archive was created but we didn't have an architecture so we need to
    // set it
    for (size_t i = initial_count; i < end_count; ++i) {
      ModuleSpec module_spec;
      if (specs.GetModuleSpecAtIndex(i, module_spec)) {
        if (module_spec.GetArchitecture().IsValid()) {
          archive_sp->SetArchitecture(module_spec.GetArchitecture());
          break;
        }
      }
    }
  }
  return num_specs_added;
}

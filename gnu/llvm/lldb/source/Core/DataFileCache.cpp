//===-- DataFileCache.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/DataFileCache.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Utility/DataEncoder.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "llvm/Support/CachePruning.h"

using namespace lldb_private;


llvm::CachePruningPolicy DataFileCache::GetLLDBIndexCachePolicy() {
  static llvm::CachePruningPolicy policy;
  static llvm::once_flag once_flag;

  llvm::call_once(once_flag, []() {
    // Prune the cache based off of the LLDB settings each time we create a
    // cache object.
    ModuleListProperties &properties =
        ModuleList::GetGlobalModuleListProperties();
    // Only scan once an hour. If we have lots of debug sessions we don't want
    // to scan this directory too often. A timestamp file is written to the
    // directory to ensure different processes don't scan the directory too
    // often. This setting doesn't mean that a thread will continually scan the
    // cache directory within this process.
    policy.Interval = std::chrono::hours(1);
    // Get the user settings for pruning.
    policy.MaxSizeBytes = properties.GetLLDBIndexCacheMaxByteSize();
    policy.MaxSizePercentageOfAvailableSpace =
        properties.GetLLDBIndexCacheMaxPercent();
    policy.Expiration =
        std::chrono::hours(properties.GetLLDBIndexCacheExpirationDays() * 24);
  });
  return policy;
}

DataFileCache::DataFileCache(llvm::StringRef path, llvm::CachePruningPolicy policy) {
  m_cache_dir.SetPath(path);
  pruneCache(path, policy);

  // This lambda will get called when the data is gotten from the cache and
  // also after the data was set for a given key. We only need to take
  // ownership of the data if we are geting the data, so we use the
  // m_take_ownership member variable to indicate if we need to take
  // ownership.

  auto add_buffer = [this](unsigned task, const llvm::Twine &moduleName,
                           std::unique_ptr<llvm::MemoryBuffer> m) {
    if (m_take_ownership)
      m_mem_buff_up = std::move(m);
  };
  llvm::Expected<llvm::FileCache> cache_or_err =
      llvm::localCache("LLDBModuleCache", "lldb-module", path, add_buffer);
  if (cache_or_err)
    m_cache_callback = std::move(*cache_or_err);
  else {
    Log *log = GetLog(LLDBLog::Modules);
    LLDB_LOG_ERROR(log, cache_or_err.takeError(),
                   "failed to create lldb index cache directory: {0}");
  }
}

std::unique_ptr<llvm::MemoryBuffer>
DataFileCache::GetCachedData(llvm::StringRef key) {
  std::lock_guard<std::mutex> guard(m_mutex);

  const unsigned task = 1;
  m_take_ownership = true;
  // If we call the "m_cache_callback" function and the data is cached, it will
  // call the "add_buffer" lambda function from the constructor which will in
  // turn take ownership of the member buffer that is passed to the callback and
  // put it into a member variable.
  llvm::Expected<llvm::AddStreamFn> add_stream_or_err =
      m_cache_callback(task, key, "");
  m_take_ownership = false;
  // At this point we either already called the "add_buffer" lambda with
  // the data or we haven't. We can tell if we got the cached data by checking
  // the add_stream function pointer value below.
  if (add_stream_or_err) {
    llvm::AddStreamFn &add_stream = *add_stream_or_err;
    // If the "add_stream" is nullptr, then the data was cached and we already
    // called the "add_buffer" lambda. If it is valid, then if we were to call
    // the add_stream function it would cause a cache file to get generated
    // and we would be expected to fill in the data. In this function we only
    // want to check if the data was cached, so we don't want to call
    // "add_stream" in this function.
    if (!add_stream)
      return std::move(m_mem_buff_up);
  } else {
    Log *log = GetLog(LLDBLog::Modules);
    LLDB_LOG_ERROR(log, add_stream_or_err.takeError(),
                   "failed to get the cache add stream callback for key: {0}");
  }
  // Data was not cached.
  return std::unique_ptr<llvm::MemoryBuffer>();
}

bool DataFileCache::SetCachedData(llvm::StringRef key,
                                  llvm::ArrayRef<uint8_t> data) {
  std::lock_guard<std::mutex> guard(m_mutex);
  const unsigned task = 2;
  // If we call this function and the data is cached, it will call the
  // add_buffer lambda function from the constructor which will ignore the
  // data.
  llvm::Expected<llvm::AddStreamFn> add_stream_or_err =
      m_cache_callback(task, key, "");
  // If we reach this code then we either already called the callback with
  // the data or we haven't. We can tell if we had the cached data by checking
  // the CacheAddStream function pointer value below.
  if (add_stream_or_err) {
    llvm::AddStreamFn &add_stream = *add_stream_or_err;
    // If the "add_stream" is nullptr, then the data was cached. If it is
    // valid, then if we call the add_stream function with a task it will
    // cause the file to get generated, but we only want to check if the data
    // is cached here, so we don't want to call it here. Note that the
    // add_buffer will also get called in this case after the data has been
    // provided, but we won't take ownership of the memory buffer as we just
    // want to write the data.
    if (add_stream) {
      llvm::Expected<std::unique_ptr<llvm::CachedFileStream>> file_or_err =
          add_stream(task, "");
      if (file_or_err) {
        llvm::CachedFileStream *cfs = file_or_err->get();
        cfs->OS->write((const char *)data.data(), data.size());
        return true;
      } else {
        Log *log = GetLog(LLDBLog::Modules);
        LLDB_LOG_ERROR(log, file_or_err.takeError(),
                       "failed to get the cache file stream for key: {0}");
      }
    }
  } else {
    Log *log = GetLog(LLDBLog::Modules);
    LLDB_LOG_ERROR(log, add_stream_or_err.takeError(),
                   "failed to get the cache add stream callback for key: {0}");
  }
  return false;
}

FileSpec DataFileCache::GetCacheFilePath(llvm::StringRef key) {
  FileSpec cache_file(m_cache_dir);
  std::string filename("llvmcache-");
  filename += key.str();
  cache_file.AppendPathComponent(filename);
  return cache_file;
}

Status DataFileCache::RemoveCacheFile(llvm::StringRef key) {
  FileSpec cache_file = GetCacheFilePath(key);
  FileSystem &fs = FileSystem::Instance();
  if (!fs.Exists(cache_file))
    return Status();
  return fs.RemoveFile(cache_file);
}

CacheSignature::CacheSignature(lldb_private::Module *module) {
  Clear();
  UUID uuid = module->GetUUID();
  if (uuid.IsValid())
    m_uuid = uuid;

  std::time_t mod_time = 0;
  mod_time = llvm::sys::toTimeT(module->GetModificationTime());
  if (mod_time != 0)
    m_mod_time = mod_time;

  mod_time = llvm::sys::toTimeT(module->GetObjectModificationTime());
  if (mod_time != 0)
    m_obj_mod_time = mod_time;
}

CacheSignature::CacheSignature(lldb_private::ObjectFile *objfile) {
  Clear();
  UUID uuid = objfile->GetUUID();
  if (uuid.IsValid())
    m_uuid = uuid;

  std::time_t mod_time = 0;
  // Grab the modification time of the object file's file. It isn't always the
  // same as the module's file when you have a executable file as the main
  // executable, and you have a object file for a symbol file.
  FileSystem &fs = FileSystem::Instance();
  mod_time = llvm::sys::toTimeT(fs.GetModificationTime(objfile->GetFileSpec()));
  if (mod_time != 0)
    m_mod_time = mod_time;

  mod_time =
      llvm::sys::toTimeT(objfile->GetModule()->GetObjectModificationTime());
  if (mod_time != 0)
    m_obj_mod_time = mod_time;
}

enum SignatureEncoding {
  eSignatureUUID = 1u,
  eSignatureModTime = 2u,
  eSignatureObjectModTime = 3u,
  eSignatureEnd = 255u,
};

bool CacheSignature::Encode(DataEncoder &encoder) const {
  if (!IsValid())
    return false; // Invalid signature, return false!

  if (m_uuid) {
    llvm::ArrayRef<uint8_t> uuid_bytes = m_uuid->GetBytes();
    encoder.AppendU8(eSignatureUUID);
    encoder.AppendU8(uuid_bytes.size());
    encoder.AppendData(uuid_bytes);
  }
  if (m_mod_time) {
    encoder.AppendU8(eSignatureModTime);
    encoder.AppendU32(*m_mod_time);
  }
  if (m_obj_mod_time) {
    encoder.AppendU8(eSignatureObjectModTime);
    encoder.AppendU32(*m_obj_mod_time);
  }
  encoder.AppendU8(eSignatureEnd);
  return true;
}

bool CacheSignature::Decode(const lldb_private::DataExtractor &data,
                            lldb::offset_t *offset_ptr) {
  Clear();
  while (uint8_t sig_encoding = data.GetU8(offset_ptr)) {
    switch (sig_encoding) {
    case eSignatureUUID: {
      const uint8_t length = data.GetU8(offset_ptr);
      const uint8_t *bytes = (const uint8_t *)data.GetData(offset_ptr, length);
      if (bytes != nullptr && length > 0)
        m_uuid = UUID(llvm::ArrayRef<uint8_t>(bytes, length));
    } break;
    case eSignatureModTime: {
      uint32_t mod_time = data.GetU32(offset_ptr);
      if (mod_time > 0)
        m_mod_time = mod_time;
    } break;
    case eSignatureObjectModTime: {
      uint32_t mod_time = data.GetU32(offset_ptr);
      if (mod_time > 0)
        m_obj_mod_time = mod_time;
    } break;
    case eSignatureEnd:
      // The definition of is valid changed to only be valid if the UUID is
      // valid so make sure that if we attempt to decode an old cache file
      // that we will fail to decode the cache file if the signature isn't
      // considered valid.
      return IsValid();
    default:
      break;
    }
  }
  return false;
}

uint32_t ConstStringTable::Add(ConstString s) {
  auto pos = m_string_to_offset.find(s);
  if (pos != m_string_to_offset.end())
    return pos->second;
  const uint32_t offset = m_next_offset;
  m_strings.push_back(s);
  m_string_to_offset[s] = offset;
  m_next_offset += s.GetLength() + 1;
  return offset;
}

static const llvm::StringRef kStringTableIdentifier("STAB");

bool ConstStringTable::Encode(DataEncoder &encoder) {
  // Write an 4 character code into the stream. This will help us when decoding
  // to make sure we find this identifier when decoding the string table to make
  // sure we have the rigth data. It also helps to identify the string table
  // when dumping the hex bytes in a cache file.
  encoder.AppendData(kStringTableIdentifier);
  size_t length_offset = encoder.GetByteSize();
  encoder.AppendU32(0); // Total length of all strings which will be fixed up.
  size_t strtab_offset = encoder.GetByteSize();
  encoder.AppendU8(0); // Start the string table with an empty string.
  for (auto s: m_strings) {
    // Make sure all of the offsets match up with what we handed out!
    assert(m_string_to_offset.find(s)->second ==
           encoder.GetByteSize() - strtab_offset);
    // Append the C string into the encoder
    encoder.AppendCString(s.GetStringRef());
  }
  // Fixup the string table length.
  encoder.PutU32(length_offset, encoder.GetByteSize() - strtab_offset);
  return true;
}

bool StringTableReader::Decode(const lldb_private::DataExtractor &data,
                               lldb::offset_t *offset_ptr) {
  llvm::StringRef identifier((const char *)data.GetData(offset_ptr, 4), 4);
  if (identifier != kStringTableIdentifier)
    return false;
  const uint32_t length = data.GetU32(offset_ptr);
  // We always have at least one byte for the empty string at offset zero.
  if (length == 0)
    return false;
  const char *bytes = (const char *)data.GetData(offset_ptr, length);
  if (bytes == nullptr)
    return false;
  m_data = llvm::StringRef(bytes, length);
  return true;
}

llvm::StringRef StringTableReader::Get(uint32_t offset) const {
  if (offset >= m_data.size())
    return llvm::StringRef();
  return llvm::StringRef(m_data.data() + offset);
}


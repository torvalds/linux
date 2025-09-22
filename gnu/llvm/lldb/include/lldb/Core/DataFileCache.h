//===-- DataFileCache.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_DATAFILECACHE_H
#define LLDB_CORE_DATAFILECACHE_H

#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/UUID.h"
#include "lldb/lldb-forward.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/CachePruning.h"
#include "llvm/Support/Caching.h"
#include "llvm/Support/MemoryBuffer.h"

#include <mutex>
#include <optional>

namespace lldb_private {

/// This class enables data to be cached into a directory using the llvm
/// caching code. Data can be stored and accessed using a unique string key.
/// The data will be stored in the directory that is specified in the
/// DataFileCache constructor. The data will be stored in files that start with
/// "llvmcache-<key>" where <key> is the key name specified when getting to
/// setting cached data.
///
/// Sample code for how to use the cache:
///
///   DataFileCache cache("/tmp/lldb-test-cache");
///   StringRef key("Key1");
///   auto mem_buffer_up = cache.GetCachedData(key);
///   if (mem_buffer_up) {
///     printf("cached data:\n%s", mem_buffer_up->getBufferStart());
///   } else {
///     std::vector<uint8_t> data = { 'h', 'e', 'l', 'l', 'o', '\n' };
///     cache.SetCachedData(key, data);
///   }

class DataFileCache {
public:
  /// Create a data file cache in the directory path that is specified, using
  /// the specified policy.
  ///
  /// Data will be cached in files created in this directory when clients call
  /// DataFileCache::SetCacheData.
  DataFileCache(llvm::StringRef path,
                llvm::CachePruningPolicy policy =
                    DataFileCache::GetLLDBIndexCachePolicy());

  /// Gets the default LLDB index cache policy, which is controlled by the
  /// "LLDBIndexCache" family of settings.
  static llvm::CachePruningPolicy GetLLDBIndexCachePolicy();

  /// Get cached data from the cache directory for the specified key.
  ///
  /// Keys must be unique for any given data. This function attempts to see if
  /// the data is available for the specified key and will return a valid memory
  /// buffer is data is available.
  ///
  /// \param key
  ///   The unique string key that identifies data being cached.
  ///
  /// \return
  ///   A valid unique pointer to a memory buffer if the data is available, or
  ///   a unique pointer that contains NULL if the data is not available.
  std::unique_ptr<llvm::MemoryBuffer> GetCachedData(llvm::StringRef key);

  /// Set cached data for the specified key.
  ///
  /// Setting the cached data will save a file in the cache directory to contain
  /// the specified data.
  ///
  /// \param key
  ///   The unique string key that identifies data being cached.
  ///
  /// \return
  ///   True if the data was successfully cached, false otherwise.
  bool SetCachedData(llvm::StringRef key, llvm::ArrayRef<uint8_t> data);

  /// Remove the cache file associated with the key.
  Status RemoveCacheFile(llvm::StringRef key);

private:
  /// Return the cache file that is associated with the key.
  FileSpec GetCacheFilePath(llvm::StringRef key);

  llvm::FileCache m_cache_callback;
  FileSpec m_cache_dir;
  std::mutex m_mutex;
  std::unique_ptr<llvm::MemoryBuffer> m_mem_buff_up;
  bool m_take_ownership = false;
};

/// A signature for a given file on disk.
///
/// Any files that are cached in the LLDB index cached need some data that
/// uniquely identifies a file on disk and this information should be written
/// into each cache file so we can validate if the cache file still matches
/// the file we are trying to load cached data for. Objects can fill out this
/// signature and then encode and decode them to validate the signatures
/// match. If they do not match, the cache file on disk should be removed as
/// it is out of date.
struct CacheSignature {
  /// UUID of object file or module.
  std::optional<UUID> m_uuid;
  /// Modification time of file on disk.
  std::optional<std::time_t> m_mod_time;
  /// If this describes a .o file with a BSD archive, the BSD archive's
  /// modification time will be in m_mod_time, and the .o file's modification
  /// time will be in this m_obj_mod_time.
  std::optional<std::time_t> m_obj_mod_time;

  CacheSignature() = default;

  /// Create a signature from a module.
  CacheSignature(lldb_private::Module *module);

  /// Create a signature from an object file.
  CacheSignature(lldb_private::ObjectFile *objfile);

  void Clear() {
    m_uuid = std::nullopt;
    m_mod_time = std::nullopt;
    m_obj_mod_time = std::nullopt;
  }

  /// Return true only if the CacheSignature is valid.
  ///
  /// Cache signatures are considered valid only if there is a UUID in the file
  /// that can uniquely identify the file. Some build systems play with
  /// modification times of file so we can not trust them without using valid
  /// unique idenifier like the UUID being valid.
  bool IsValid() const { return m_uuid.has_value(); }

  /// Check if two signatures are the same.
  bool operator==(const CacheSignature &rhs) const {
    return m_uuid == rhs.m_uuid && m_mod_time == rhs.m_mod_time &&
           m_obj_mod_time == rhs.m_obj_mod_time;
  }

  /// Check if two signatures differ.
  bool operator!=(const CacheSignature &rhs) const { return !(*this == rhs); }
  /// Encode this object into a data encoder object.
  ///
  /// This allows this object to be serialized to disk. The CacheSignature
  /// object must have at least one member variable that has a value in order to
  /// be serialized so that we can match this data to when the cached file is
  /// loaded at a later time.
  ///
  /// \param encoder
  ///   A data encoder object that serialized bytes will be encoded into.
  ///
  /// \return
  ///   True if a signature was encoded, and false if there were no member
  ///   variables that had value. False indicates this data should not be
  ///   cached to disk because we were unable to encode a valid signature.
  bool Encode(DataEncoder &encoder) const;

  /// Decode a serialized version of this object from data.
  ///
  /// \param data
  ///   The decoder object that references the serialized data.
  ///
  /// \param offset_ptr
  ///   A pointer that contains the offset from which the data will be decoded
  ///   from that gets updated as data gets decoded.
  ///
  /// \return
  ///   True if the signature was successfully decoded, false otherwise.
  bool Decode(const DataExtractor &data, lldb::offset_t *offset_ptr);
};

/// Many cache files require string tables to store data efficiently. This
/// class helps create string tables.
class ConstStringTable {
public:
  ConstStringTable() = default;
  /// Add a string into the string table.
  ///
  /// Add a string to the string table will only add the same string one time
  /// and will return the offset in the string table buffer to that string.
  /// String tables are easy to build with ConstString objects since most LLDB
  /// classes for symbol or debug info use them already and they provide
  /// permanent storage for the string.
  ///
  /// \param s
  ///   The string to insert into the string table.
  ///
  /// \return
  ///   The byte offset from the start of the string table for the inserted
  ///   string. Duplicate strings that get inserted will return the same
  ///   byte offset.
  uint32_t Add(ConstString s);

  bool Encode(DataEncoder &encoder);

private:
  std::vector<ConstString> m_strings;
  llvm::DenseMap<ConstString, uint32_t> m_string_to_offset;
  /// Skip one byte to start the string table off with an empty string.
  uint32_t m_next_offset = 1;
};

/// Many cache files require string tables to store data efficiently. This
/// class helps give out strings from a string table that was read from a
/// cache file.
class StringTableReader {
public:
  StringTableReader() = default;

  llvm::StringRef Get(uint32_t offset) const;

  bool Decode(const DataExtractor &data, lldb::offset_t *offset_ptr);

protected:
  /// All of the strings in the string table are contained in m_data.
  llvm::StringRef m_data;
};

} // namespace lldb_private

#endif // LLDB_CORE_DATAFILECACHE_H

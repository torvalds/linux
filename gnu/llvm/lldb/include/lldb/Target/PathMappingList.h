//===-- PathMappingList.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_PATHMAPPINGLIST_H
#define LLDB_TARGET_PATHMAPPINGLIST_H

#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Status.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/JSON.h"
#include <map>
#include <mutex>
#include <optional>
#include <vector>

namespace lldb_private {

class PathMappingList {
public:
  typedef void (*ChangedCallback)(const PathMappingList &path_list,
                                  void *baton);

  // Constructors and Destructors
  PathMappingList();

  PathMappingList(ChangedCallback callback, void *callback_baton);

  PathMappingList(const PathMappingList &rhs);

  ~PathMappingList();

  const PathMappingList &operator=(const PathMappingList &rhs);

  void Append(llvm::StringRef path, llvm::StringRef replacement, bool notify);

  void Append(const PathMappingList &rhs, bool notify);

  /// Append <path, replacement> pair without duplication.
  /// \return whether appending suceeds without duplication or not.
  bool AppendUnique(llvm::StringRef path, llvm::StringRef replacement,
                    bool notify);

  void Clear(bool notify);

  // By default, dump all pairs.
  void Dump(Stream *s, int pair_index = -1);

  llvm::json::Value ToJSON();

  bool IsEmpty() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_pairs.empty();
  }

  size_t GetSize() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_pairs.size();
  }

  bool GetPathsAtIndex(uint32_t idx, ConstString &path,
                       ConstString &new_path) const;

  void Insert(llvm::StringRef path, llvm::StringRef replacement,
              uint32_t insert_idx, bool notify);

  bool Remove(size_t index, bool notify);

  bool Remove(ConstString path, bool notify);

  bool Replace(llvm::StringRef path, llvm::StringRef replacement, bool notify);

  bool Replace(llvm::StringRef path, llvm::StringRef replacement,
               uint32_t index, bool notify);
  bool RemapPath(ConstString path, ConstString &new_path) const;

  /// Remaps a source file given \a path into \a new_path.
  ///
  /// Remaps \a path if any source remappings match. This function
  /// does NOT stat the file system so it can be used in tight loops
  /// where debug info is being parsed.
  ///
  /// \param[in] path
  ///     The original source file path to try and remap.
  ///
  /// \param[in] only_if_exists
  ///     If \b true, besides matching \p path with the remapping rules, this
  ///     tries to check with the filesystem that the remapped file exists. If
  ///     no valid file is found, \b std::nullopt is returned. This might be
  ///     expensive, specially on a network.
  ///
  ///     If \b false, then the existence of the returned remapping is not
  ///     checked.
  ///
  /// \return
  ///     The remapped filespec that may or may not exist on disk.
  std::optional<FileSpec> RemapPath(llvm::StringRef path,
                                    bool only_if_exists = false) const;
  bool RemapPath(const char *, std::string &) const = delete;

  /// Perform reverse source path remap for input \a file.
  /// Source maps contains a list of <from_original_path, to_new_path> mappings.
  /// Reverse remap means locating a matching entry prefix using "to_new_path"
  /// part and replacing it with "from_original_path" part if found.
  ///
  /// \param[in] file
  ///     The source path to reverse remap.
  /// \param[in] fixed
  ///     The reversed mapped new path.
  ///
  /// \return
  ///     std::nullopt if no remapping happens, otherwise, the matching source
  ///     map entry's ""to_new_pathto"" part (which is the prefix of \a file) is
  ///     returned.
  std::optional<llvm::StringRef> ReverseRemapPath(const FileSpec &file,
                                                  FileSpec &fixed) const;

  /// Finds a source file given a file spec using the path remappings.
  ///
  /// Tries to resolve \a orig_spec by checking the path remappings.
  /// It makes sure the file exists by checking with the file system,
  /// so this call can be expensive if the remappings are on a network
  /// or are even on the local file system, so use this function
  /// sparingly (not in a tight debug info parsing loop).
  ///
  /// \param[in] orig_spec
  ///     The original source file path to try and remap.
  ///
  /// \return
  ///     The newly remapped filespec that is guaranteed to exist.
  std::optional<FileSpec> FindFile(const FileSpec &orig_spec) const;

  uint32_t FindIndexForPath(llvm::StringRef path) const;

  uint32_t GetModificationID() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_mod_id;
  }

protected:
  mutable std::recursive_mutex m_mutex;
  typedef std::pair<ConstString, ConstString> pair;
  typedef std::vector<pair> collection;
  typedef collection::iterator iterator;
  typedef collection::const_iterator const_iterator;

  iterator FindIteratorForPath(ConstString path);

  const_iterator FindIteratorForPath(ConstString path) const;

  collection m_pairs;
  ChangedCallback m_callback = nullptr;
  void *m_callback_baton = nullptr;
  uint32_t m_mod_id = 0; // Incremented anytime anything is added or removed.
};

} // namespace lldb_private

#endif // LLDB_TARGET_PATHMAPPINGLIST_H

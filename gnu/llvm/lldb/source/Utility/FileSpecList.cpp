//===-- FileSpecList.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/FileSpecList.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Stream.h"

#include <cstdint>
#include <utility>

using namespace lldb_private;

FileSpecList::FileSpecList() : m_files() {}

FileSpecList::~FileSpecList() = default;

// Append the "file_spec" to the end of the file spec list.
void FileSpecList::Append(const FileSpec &file_spec) {
  m_files.push_back(file_spec);
}

// Only append the "file_spec" if this list doesn't already contain it.
//
// Returns true if "file_spec" was added, false if this list already contained
// a copy of "file_spec".
bool FileSpecList::AppendIfUnique(const FileSpec &file_spec) {
  collection::iterator end = m_files.end();
  if (find(m_files.begin(), end, file_spec) == end) {
    m_files.push_back(file_spec);
    return true;
  }
  return false;
}

// FIXME: Replace this with a DenseSet at the call site. It is inefficient.
bool SupportFileList::AppendIfUnique(const FileSpec &file_spec) {
  collection::iterator end = m_files.end();
  if (find_if(m_files.begin(), end,
              [&](const std::shared_ptr<SupportFile> &support_file) {
                return support_file->GetSpecOnly() == file_spec;
              }) == end) {
    Append(file_spec);
    return true;
  }
  return false;
}

// Clears the file list.
void FileSpecList::Clear() { m_files.clear(); }

// Dumps the file list to the supplied stream pointer "s".
void FileSpecList::Dump(Stream *s, const char *separator_cstr) const {
  collection::const_iterator pos, end = m_files.end();
  for (pos = m_files.begin(); pos != end; ++pos) {
    pos->Dump(s->AsRawOstream());
    if (separator_cstr && ((pos + 1) != end))
      s->PutCString(separator_cstr);
  }
}

// Find the index of the file in the file spec list that matches "file_spec"
// starting "start_idx" entries into the file spec list.
//
// Returns the valid index of the file that matches "file_spec" if it is found,
// else std::numeric_limits<uint32_t>::max() is returned.
static size_t FindFileIndex(size_t start_idx, const FileSpec &file_spec,
                            bool full, size_t num_files,
                            std::function<const FileSpec &(size_t)> get_ith) {
  // When looking for files, we will compare only the filename if the FILE_SPEC
  // argument is empty
  bool compare_filename_only = file_spec.GetDirectory().IsEmpty();

  for (size_t idx = start_idx; idx < num_files; ++idx) {
    const FileSpec &ith = get_ith(idx);
    if (compare_filename_only) {
      if (ConstString::Equals(ith.GetFilename(), file_spec.GetFilename(),
                              file_spec.IsCaseSensitive() ||
                                  ith.IsCaseSensitive()))
        return idx;
    } else {
      if (FileSpec::Equal(ith, file_spec, full))
        return idx;
    }
  }

  // We didn't find the file, return an invalid index
  return UINT32_MAX;
}

size_t FileSpecList::FindFileIndex(size_t start_idx, const FileSpec &file_spec,
                                   bool full) const {
  return ::FindFileIndex(
      start_idx, file_spec, full, m_files.size(),
      [&](size_t idx) -> const FileSpec & { return m_files[idx]; });
}

size_t SupportFileList::FindFileIndex(size_t start_idx,
                                      const FileSpec &file_spec,
                                      bool full) const {
  return ::FindFileIndex(start_idx, file_spec, full, m_files.size(),
                         [&](size_t idx) -> const FileSpec & {
                           return m_files[idx]->GetSpecOnly();
                         });
}

size_t SupportFileList::FindCompatibleIndex(size_t start_idx,
                                            const FileSpec &file_spec) const {
  const size_t num_files = m_files.size();
  if (start_idx >= num_files)
    return UINT32_MAX;

  const bool file_spec_relative = file_spec.IsRelative();
  const bool file_spec_case_sensitive = file_spec.IsCaseSensitive();
  // When looking for files, we will compare only the filename if the directory
  // argument is empty in file_spec
  const bool full = !file_spec.GetDirectory().IsEmpty();

  for (size_t idx = start_idx; idx < num_files; ++idx) {
    const FileSpec &curr_file = m_files[idx]->GetSpecOnly();

    // Always start by matching the filename first
    if (!curr_file.FileEquals(file_spec))
      continue;

    // Only compare the full name if the we were asked to and if the current
    // file entry has the a directory. If it doesn't have a directory then we
    // only compare the filename.
    if (FileSpec::Equal(curr_file, file_spec, full)) {
      return idx;
    } else if (curr_file.IsRelative() || file_spec_relative) {
      llvm::StringRef curr_file_dir = curr_file.GetDirectory().GetStringRef();
      if (curr_file_dir.empty())
        return idx; // Basename match only for this file in the list

      // Check if we have a relative path in our file list, or if "file_spec" is
      // relative, if so, check if either ends with the other.
      llvm::StringRef file_spec_dir = file_spec.GetDirectory().GetStringRef();
      // We have a relative path in our file list, it matches if the
      // specified path ends with this path, but we must ensure the full
      // component matches (we don't want "foo/bar.cpp" to match "oo/bar.cpp").
      auto is_suffix = [](llvm::StringRef a, llvm::StringRef b,
                          bool case_sensitive) -> bool {
        if (case_sensitive ? a.consume_back(b) : a.consume_back_insensitive(b))
          return a.empty() || a.ends_with("/");
        return false;
      };
      const bool case_sensitive =
          file_spec_case_sensitive || curr_file.IsCaseSensitive();
      if (is_suffix(curr_file_dir, file_spec_dir, case_sensitive) ||
          is_suffix(file_spec_dir, curr_file_dir, case_sensitive))
        return idx;
    }
  }

  // We didn't find the file, return an invalid index
  return UINT32_MAX;
}
// Returns the FileSpec object at index "idx". If "idx" is out of range, then
// an empty FileSpec object will be returned.
const FileSpec &FileSpecList::GetFileSpecAtIndex(size_t idx) const {
  if (idx < m_files.size())
    return m_files[idx];
  static FileSpec g_empty_file_spec;
  return g_empty_file_spec;
}

const FileSpec &SupportFileList::GetFileSpecAtIndex(size_t idx) const {
  if (idx < m_files.size())
    return m_files[idx]->Materialize();
  static FileSpec g_empty_file_spec;
  return g_empty_file_spec;
}

std::shared_ptr<SupportFile>
SupportFileList::GetSupportFileAtIndex(size_t idx) const {
  if (idx < m_files.size())
    return m_files[idx];
  return {};
}

// Return the size in bytes that this object takes in memory. This returns the
// size in bytes of this object's member variables and any FileSpec objects its
// member variables contain, the result doesn't not include the string values
// for the directories any filenames as those are in shared string pools.
size_t FileSpecList::MemorySize() const {
  size_t mem_size = sizeof(FileSpecList);
  collection::const_iterator pos, end = m_files.end();
  for (pos = m_files.begin(); pos != end; ++pos) {
    mem_size += pos->MemorySize();
  }

  return mem_size;
}

// Return the number of files in the file spec list.
size_t FileSpecList::GetSize() const { return m_files.size(); }

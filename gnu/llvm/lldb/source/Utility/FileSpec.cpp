//===-- FileSpec.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/Stream.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"

#include <algorithm>
#include <optional>
#include <system_error>
#include <vector>

#include <cassert>
#include <climits>
#include <cstdio>
#include <cstring>

using namespace lldb;
using namespace lldb_private;

namespace {

static constexpr FileSpec::Style GetNativeStyle() {
#if defined(_WIN32)
  return FileSpec::Style::windows;
#else
  return FileSpec::Style::posix;
#endif
}

bool PathStyleIsPosix(FileSpec::Style style) {
  return llvm::sys::path::is_style_posix(style);
}

const char *GetPathSeparators(FileSpec::Style style) {
  return llvm::sys::path::get_separator(style).data();
}

char GetPreferredPathSeparator(FileSpec::Style style) {
  return GetPathSeparators(style)[0];
}

void Denormalize(llvm::SmallVectorImpl<char> &path, FileSpec::Style style) {
  if (PathStyleIsPosix(style))
    return;

  std::replace(path.begin(), path.end(), '/', '\\');
}

} // end anonymous namespace

FileSpec::FileSpec() : m_style(GetNativeStyle()) {}

// Default constructor that can take an optional full path to a file on disk.
FileSpec::FileSpec(llvm::StringRef path, Style style) : m_style(style) {
  SetFile(path, style);
}

FileSpec::FileSpec(llvm::StringRef path, const llvm::Triple &triple)
    : FileSpec{path, triple.isOSWindows() ? Style::windows : Style::posix} {}

namespace {
/// Safely get a character at the specified index.
///
/// \param[in] path
///     A full, partial, or relative path to a file.
///
/// \param[in] i
///     An index into path which may or may not be valid.
///
/// \return
///   The character at index \a i if the index is valid, or 0 if
///   the index is not valid.
inline char safeCharAtIndex(const llvm::StringRef &path, size_t i) {
  if (i < path.size())
    return path[i];
  return 0;
}

/// Check if a path needs to be normalized.
///
/// Check if a path needs to be normalized. We currently consider a
/// path to need normalization if any of the following are true
///  - path contains "/./"
///  - path contains "/../"
///  - path contains "//"
///  - path ends with "/"
/// Paths that start with "./" or with "../" are not considered to
/// need normalization since we aren't trying to resolve the path,
/// we are just trying to remove redundant things from the path.
///
/// \param[in] path
///     A full, partial, or relative path to a file.
///
/// \return
///   Returns \b true if the path needs to be normalized.
bool needsNormalization(const llvm::StringRef &path) {
  if (path.empty())
    return false;
  // We strip off leading "." values so these paths need to be normalized
  if (path[0] == '.')
    return true;
  for (auto i = path.find_first_of("\\/"); i != llvm::StringRef::npos;
       i = path.find_first_of("\\/", i + 1)) {
    const auto next = safeCharAtIndex(path, i+1);
    switch (next) {
      case 0:
        // path separator char at the end of the string which should be
        // stripped unless it is the one and only character
        return i > 0;
      case '/':
      case '\\':
        // two path separator chars in the middle of a path needs to be
        // normalized
        if (i > 0)
          return true;
        ++i;
        break;

      case '.': {
          const auto next_next = safeCharAtIndex(path, i+2);
          switch (next_next) {
            default: break;
            case 0: return true; // ends with "/."
            case '/':
            case '\\':
              return true; // contains "/./"
            case '.': {
              const auto next_next_next = safeCharAtIndex(path, i+3);
              switch (next_next_next) {
                default: break;
                case 0: return true; // ends with "/.."
                case '/':
                case '\\':
                  return true; // contains "/../"
              }
              break;
            }
          }
        }
        break;

      default:
        break;
    }
  }
  return false;
}


}

void FileSpec::SetFile(llvm::StringRef pathname) { SetFile(pathname, m_style); }

// Update the contents of this object with a new path. The path will be split
// up into a directory and filename and stored as uniqued string values for
// quick comparison and efficient memory usage.
void FileSpec::SetFile(llvm::StringRef pathname, Style style) {
  Clear();
  m_style = (style == Style::native) ? GetNativeStyle() : style;

  if (pathname.empty())
    return;

  llvm::SmallString<128> resolved(pathname);

  // Normalize the path by removing ".", ".." and other redundant components.
  if (needsNormalization(resolved))
    llvm::sys::path::remove_dots(resolved, true, m_style);

  // Normalize back slashes to forward slashes
  if (m_style == Style::windows)
    std::replace(resolved.begin(), resolved.end(), '\\', '/');

  if (resolved.empty()) {
    // If we have no path after normalization set the path to the current
    // directory. This matches what python does and also a few other path
    // utilities.
    m_filename.SetString(".");
    return;
  }

  // Split path into filename and directory. We rely on the underlying char
  // pointer to be nullptr when the components are empty.
  llvm::StringRef filename = llvm::sys::path::filename(resolved, m_style);
  if(!filename.empty())
    m_filename.SetString(filename);

  llvm::StringRef directory = llvm::sys::path::parent_path(resolved, m_style);
  if(!directory.empty())
    m_directory.SetString(directory);
}

void FileSpec::SetFile(llvm::StringRef path, const llvm::Triple &triple) {
  return SetFile(path, triple.isOSWindows() ? Style::windows : Style::posix);
}

// Convert to pointer operator. This allows code to check any FileSpec objects
// to see if they contain anything valid using code such as:
//
//  if (file_spec)
//  {}
FileSpec::operator bool() const { return m_filename || m_directory; }

// Logical NOT operator. This allows code to check any FileSpec objects to see
// if they are invalid using code such as:
//
//  if (!file_spec)
//  {}
bool FileSpec::operator!() const { return !m_directory && !m_filename; }

bool FileSpec::DirectoryEquals(const FileSpec &rhs) const {
  const bool case_sensitive = IsCaseSensitive() || rhs.IsCaseSensitive();
  return ConstString::Equals(m_directory, rhs.m_directory, case_sensitive);
}

bool FileSpec::FileEquals(const FileSpec &rhs) const {
  const bool case_sensitive = IsCaseSensitive() || rhs.IsCaseSensitive();
  return ConstString::Equals(m_filename, rhs.m_filename, case_sensitive);
}

// Equal to operator
bool FileSpec::operator==(const FileSpec &rhs) const {
  return FileEquals(rhs) && DirectoryEquals(rhs);
}

// Not equal to operator
bool FileSpec::operator!=(const FileSpec &rhs) const { return !(*this == rhs); }

// Less than operator
bool FileSpec::operator<(const FileSpec &rhs) const {
  return FileSpec::Compare(*this, rhs, true) < 0;
}

// Dump a FileSpec object to a stream
Stream &lldb_private::operator<<(Stream &s, const FileSpec &f) {
  f.Dump(s.AsRawOstream());
  return s;
}

// Clear this object by releasing both the directory and filename string values
// and making them both the empty string.
void FileSpec::Clear() {
  m_directory.Clear();
  m_filename.Clear();
  PathWasModified();
}

// Compare two FileSpec objects. If "full" is true, then both the directory and
// the filename must match. If "full" is false, then the directory names for
// "a" and "b" are only compared if they are both non-empty. This allows a
// FileSpec object to only contain a filename and it can match FileSpec objects
// that have matching filenames with different paths.
//
// Return -1 if the "a" is less than "b", 0 if "a" is equal to "b" and "1" if
// "a" is greater than "b".
int FileSpec::Compare(const FileSpec &a, const FileSpec &b, bool full) {
  int result = 0;

  // case sensitivity of compare
  const bool case_sensitive = a.IsCaseSensitive() || b.IsCaseSensitive();

  // If full is true, then we must compare both the directory and filename.

  // If full is false, then if either directory is empty, then we match on the
  // basename only, and if both directories have valid values, we still do a
  // full compare. This allows for matching when we just have a filename in one
  // of the FileSpec objects.

  if (full || (a.m_directory && b.m_directory)) {
    result = ConstString::Compare(a.m_directory, b.m_directory, case_sensitive);
    if (result)
      return result;
  }
  return ConstString::Compare(a.m_filename, b.m_filename, case_sensitive);
}

bool FileSpec::Equal(const FileSpec &a, const FileSpec &b, bool full) {
  if (full || (a.GetDirectory() && b.GetDirectory()))
    return a == b;

  return a.FileEquals(b);
}

bool FileSpec::Match(const FileSpec &pattern, const FileSpec &file) {
  if (pattern.GetDirectory())
    return pattern == file;
  if (pattern.GetFilename())
    return pattern.FileEquals(file);
  return true;
}

std::optional<FileSpec::Style>
FileSpec::GuessPathStyle(llvm::StringRef absolute_path) {
  if (absolute_path.starts_with("/"))
    return Style::posix;
  if (absolute_path.starts_with(R"(\\)"))
    return Style::windows;
  if (absolute_path.size() >= 3 && llvm::isAlpha(absolute_path[0]) &&
      (absolute_path.substr(1, 2) == R"(:\)" ||
       absolute_path.substr(1, 2) == R"(:/)"))
    return Style::windows;
  return std::nullopt;
}

// Dump the object to the supplied stream. If the object contains a valid
// directory name, it will be displayed followed by a directory delimiter, and
// the filename.
void FileSpec::Dump(llvm::raw_ostream &s) const {
  std::string path{GetPath(true)};
  s << path;
  char path_separator = GetPreferredPathSeparator(m_style);
  if (!m_filename && !path.empty() && path.back() != path_separator)
    s << path_separator;
}

FileSpec::Style FileSpec::GetPathStyle() const { return m_style; }

void FileSpec::SetDirectory(ConstString directory) {
  m_directory = directory;
  PathWasModified();
}

void FileSpec::SetDirectory(llvm::StringRef directory) {
  m_directory = ConstString(directory);
  PathWasModified();
}

void FileSpec::SetFilename(ConstString filename) {
  m_filename = filename;
  PathWasModified();
}

void FileSpec::SetFilename(llvm::StringRef filename) {
  m_filename = ConstString(filename);
  PathWasModified();
}

void FileSpec::ClearFilename() {
  m_filename.Clear();
  PathWasModified();
}

void FileSpec::ClearDirectory() {
  m_directory.Clear();
  PathWasModified();
}

// Extract the directory and path into a fixed buffer. This is needed as the
// directory and path are stored in separate string values.
size_t FileSpec::GetPath(char *path, size_t path_max_len,
                         bool denormalize) const {
  if (!path)
    return 0;

  std::string result = GetPath(denormalize);
  ::snprintf(path, path_max_len, "%s", result.c_str());
  return std::min(path_max_len - 1, result.length());
}

std::string FileSpec::GetPath(bool denormalize) const {
  llvm::SmallString<64> result;
  GetPath(result, denormalize);
  return static_cast<std::string>(result);
}

ConstString FileSpec::GetPathAsConstString(bool denormalize) const {
  return ConstString{GetPath(denormalize)};
}

void FileSpec::GetPath(llvm::SmallVectorImpl<char> &path,
                       bool denormalize) const {
  path.append(m_directory.GetStringRef().begin(),
              m_directory.GetStringRef().end());
  // Since the path was normalized and all paths use '/' when stored in these
  // objects, we don't need to look for the actual syntax specific path
  // separator, we just look for and insert '/'.
  if (m_directory && m_filename && m_directory.GetStringRef().back() != '/' &&
      m_filename.GetStringRef().back() != '/')
    path.insert(path.end(), '/');
  path.append(m_filename.GetStringRef().begin(),
              m_filename.GetStringRef().end());
  if (denormalize && !path.empty())
    Denormalize(path, m_style);
}

llvm::StringRef FileSpec::GetFileNameExtension() const {
  return llvm::sys::path::extension(m_filename.GetStringRef(), m_style);
}

ConstString FileSpec::GetFileNameStrippingExtension() const {
  return ConstString(llvm::sys::path::stem(m_filename.GetStringRef(), m_style));
}

// Return the size in bytes that this object takes in memory. This returns the
// size in bytes of this object, not any shared string values it may refer to.
size_t FileSpec::MemorySize() const {
  return m_filename.MemorySize() + m_directory.MemorySize();
}

FileSpec
FileSpec::CopyByAppendingPathComponent(llvm::StringRef component) const {
  FileSpec ret = *this;
  ret.AppendPathComponent(component);
  return ret;
}

FileSpec FileSpec::CopyByRemovingLastPathComponent() const {
  llvm::SmallString<64> current_path;
  GetPath(current_path, false);
  if (llvm::sys::path::has_parent_path(current_path, m_style))
    return FileSpec(llvm::sys::path::parent_path(current_path, m_style),
                    m_style);
  return *this;
}

void FileSpec::PrependPathComponent(llvm::StringRef component) {
  llvm::SmallString<64> new_path(component);
  llvm::SmallString<64> current_path;
  GetPath(current_path, false);
  llvm::sys::path::append(new_path,
                          llvm::sys::path::begin(current_path, m_style),
                          llvm::sys::path::end(current_path), m_style);
  SetFile(new_path, m_style);
}

void FileSpec::PrependPathComponent(const FileSpec &new_path) {
  return PrependPathComponent(new_path.GetPath(false));
}

void FileSpec::AppendPathComponent(llvm::StringRef component) {
  llvm::SmallString<64> current_path;
  GetPath(current_path, false);
  llvm::sys::path::append(current_path, m_style, component);
  SetFile(current_path, m_style);
}

void FileSpec::AppendPathComponent(const FileSpec &new_path) {
  return AppendPathComponent(new_path.GetPath(false));
}

bool FileSpec::RemoveLastPathComponent() {
  llvm::SmallString<64> current_path;
  GetPath(current_path, false);
  if (llvm::sys::path::has_parent_path(current_path, m_style)) {
    SetFile(llvm::sys::path::parent_path(current_path, m_style));
    return true;
  }
  return false;
}

std::vector<llvm::StringRef> FileSpec::GetComponents() const {
  std::vector<llvm::StringRef> components;

  auto dir_begin = llvm::sys::path::begin(m_directory.GetStringRef(), m_style);
  auto dir_end = llvm::sys::path::end(m_directory.GetStringRef());

  for (auto iter = dir_begin; iter != dir_end; ++iter) {
    if (*iter == "/" || *iter == ".")
      continue;

    components.push_back(*iter);
  }

  if (!m_filename.IsEmpty() && m_filename != "/" && m_filename != ".")
    components.push_back(m_filename.GetStringRef());

  return components;
}

/// Returns true if the filespec represents an implementation source
/// file (files with a ".c", ".cpp", ".m", ".mm" (many more)
/// extension).
///
/// \return
///     \b true if the filespec represents an implementation source
///     file, \b false otherwise.
bool FileSpec::IsSourceImplementationFile() const {
  llvm::StringRef extension = GetFileNameExtension();
  if (extension.empty())
    return false;

  static RegularExpression g_source_file_regex(llvm::StringRef(
      "^.([cC]|[mM]|[mM][mM]|[cC][pP][pP]|[cC]\\+\\+|[cC][xX][xX]|[cC][cC]|["
      "cC][pP]|[sS]|[aA][sS][mM]|[fF]|[fF]77|[fF]90|[fF]95|[fF]03|[fF][oO]["
      "rR]|[fF][tT][nN]|[fF][pP][pP]|[aA][dD][aA]|[aA][dD][bB]|[aA][dD][sS])"
      "$"));
  return g_source_file_regex.Execute(extension);
}

bool FileSpec::IsRelative() const {
  return !IsAbsolute();
}

bool FileSpec::IsAbsolute() const {
  // Check if we have cached if this path is absolute to avoid recalculating.
  if (m_absolute != Absolute::Calculate)
    return m_absolute == Absolute::Yes;

  m_absolute = Absolute::No;

  llvm::SmallString<64> path;
  GetPath(path, false);

  if (!path.empty()) {
    // We consider paths starting with ~ to be absolute.
    if (path[0] == '~' || llvm::sys::path::is_absolute(path, m_style))
      m_absolute = Absolute::Yes;
  }

  return m_absolute == Absolute::Yes;
}

void FileSpec::MakeAbsolute(const FileSpec &dir) {
  if (IsRelative())
    PrependPathComponent(dir);
}

void llvm::format_provider<FileSpec>::format(const FileSpec &F,
                                             raw_ostream &Stream,
                                             StringRef Style) {
  assert((Style.empty() || Style.equals_insensitive("F") ||
          Style.equals_insensitive("D")) &&
         "Invalid FileSpec style!");

  StringRef dir = F.GetDirectory().GetStringRef();
  StringRef file = F.GetFilename().GetStringRef();

  if (dir.empty() && file.empty()) {
    Stream << "(empty)";
    return;
  }

  if (Style.equals_insensitive("F")) {
    Stream << (file.empty() ? "(empty)" : file);
    return;
  }

  // Style is either D or empty, either way we need to print the directory.
  if (!dir.empty()) {
    // Directory is stored in normalized form, which might be different than
    // preferred form.  In order to handle this, we need to cut off the
    // filename, then denormalize, then write the entire denorm'ed directory.
    llvm::SmallString<64> denormalized_dir = dir;
    Denormalize(denormalized_dir, F.GetPathStyle());
    Stream << denormalized_dir;
    Stream << GetPreferredPathSeparator(F.GetPathStyle());
  }

  if (Style.equals_insensitive("D")) {
    // We only want to print the directory, so now just exit.
    if (dir.empty())
      Stream << "(empty)";
    return;
  }

  if (!file.empty())
    Stream << file;
}

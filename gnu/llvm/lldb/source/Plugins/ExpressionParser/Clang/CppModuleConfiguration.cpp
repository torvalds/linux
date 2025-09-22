//===-- CppModuleConfiguration.cpp ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CppModuleConfiguration.h"

#include "ClangHost.h"
#include "lldb/Host/FileSystem.h"
#include "llvm/TargetParser/Triple.h"
#include <optional>

using namespace lldb_private;

bool CppModuleConfiguration::SetOncePath::TrySet(llvm::StringRef path) {
  // Setting for the first time always works.
  if (m_first) {
    m_path = path.str();
    m_valid = true;
    m_first = false;
    return true;
  }
  // Changing the path to the same value is fine.
  if (m_path == path)
    return true;

  // Changing the path after it was already set is not allowed.
  m_valid = false;
  return false;
}

static llvm::SmallVector<std::string, 2>
getTargetIncludePaths(const llvm::Triple &triple) {
  llvm::SmallVector<std::string, 2> paths;
  if (!triple.str().empty()) {
    paths.push_back("/usr/include/" + triple.str());
    if (!triple.getArchName().empty() ||
        triple.getOSAndEnvironmentName().empty())
      paths.push_back(("/usr/include/" + triple.getArchName() + "-" +
                       triple.getOSAndEnvironmentName())
                          .str());
  }
  return paths;
}

/// Returns the include path matching the given pattern for the given file
/// path (or std::nullopt if the path doesn't match the pattern).
static std::optional<llvm::StringRef>
guessIncludePath(llvm::StringRef path_to_file, llvm::StringRef pattern) {
  if (pattern.empty())
    return std::nullopt;
  size_t pos = path_to_file.find(pattern);
  if (pos == llvm::StringRef::npos)
    return std::nullopt;

  return path_to_file.substr(0, pos + pattern.size());
}

bool CppModuleConfiguration::analyzeFile(const FileSpec &f,
                                         const llvm::Triple &triple) {
  using namespace llvm::sys::path;
  // Convert to slashes to make following operations simpler.
  std::string dir_buffer = convert_to_slash(f.GetDirectory().GetStringRef());
  llvm::StringRef posix_dir(dir_buffer);

  // Check for /c++/vX/ that is used by libc++.
  static llvm::Regex libcpp_regex(R"regex(/c[+][+]/v[0-9]/)regex");
  // If the path is in the libc++ include directory use it as the found libc++
  // path. Ignore subdirectories such as /c++/v1/experimental as those don't
  // need to be specified in the header search.
  if (libcpp_regex.match(convert_to_slash(f.GetPath())) &&
      parent_path(posix_dir, Style::posix).ends_with("c++")) {
    if (!m_std_inc.TrySet(posix_dir))
      return false;
    if (triple.str().empty())
      return true;

    posix_dir.consume_back("c++/v1");
    // Check if this is a target-specific libc++ include directory.
    return m_std_target_inc.TrySet(
        (posix_dir + triple.str() + "/c++/v1").str());
  }

  std::optional<llvm::StringRef> inc_path;
  // Target specific paths contains /usr/include, so we check them first
  for (auto &path : getTargetIncludePaths(triple)) {
    if ((inc_path = guessIncludePath(posix_dir, path)))
      return m_c_target_inc.TrySet(*inc_path);
  }
  if ((inc_path = guessIncludePath(posix_dir, "/usr/include")))
    return m_c_inc.TrySet(*inc_path);

  // File wasn't interesting, continue analyzing.
  return true;
}

/// Utility function for just appending two paths.
static std::string MakePath(llvm::StringRef lhs, llvm::StringRef rhs) {
  llvm::SmallString<256> result(lhs);
  llvm::sys::path::append(result, rhs);
  return std::string(result);
}

bool CppModuleConfiguration::hasValidConfig() {
  // We need to have a C and C++ include dir for a valid configuration.
  if (!m_c_inc.Valid() || !m_std_inc.Valid())
    return false;

  // Do some basic sanity checks on the directories that we don't activate
  // the module when it's clear that it's not usable.
  const std::vector<std::string> files_to_check = {
      // * Check that the C library contains at least one random C standard
      //   library header.
      MakePath(m_c_inc.Get(), "stdio.h"),
      // * Without a libc++ modulemap file we can't have a 'std' module that
      //   could be imported.
      MakePath(m_std_inc.Get(), "module.modulemap"),
      // * Check for a random libc++ header (vector in this case) that has to
      //   exist in a working libc++ setup.
      MakePath(m_std_inc.Get(), "vector"),
  };

  for (llvm::StringRef file_to_check : files_to_check) {
    if (!FileSystem::Instance().Exists(file_to_check))
      return false;
  }

  return true;
}

CppModuleConfiguration::CppModuleConfiguration(
    const FileSpecList &support_files, const llvm::Triple &triple) {
  // Analyze all files we were given to build the configuration.
  bool error = !llvm::all_of(support_files, [&](auto &file) {
    return CppModuleConfiguration::analyzeFile(file, triple);
  });
  // If we have a valid configuration at this point, set the
  // include directories and module list that should be used.
  if (!error && hasValidConfig()) {
    // Calculate the resource directory for LLDB.
    llvm::SmallString<256> resource_dir;
    llvm::sys::path::append(resource_dir, GetClangResourceDir().GetPath(),
                            "include");
    m_resource_inc = std::string(resource_dir.str());

    // This order matches the way Clang orders these directories.
    m_include_dirs = {m_std_inc.Get().str(), m_resource_inc,
                      m_c_inc.Get().str()};
    if (m_c_target_inc.Valid())
      m_include_dirs.push_back(m_c_target_inc.Get().str());
    if (m_std_target_inc.Valid())
      m_include_dirs.push_back(m_std_target_inc.Get().str());
    m_imported_modules = {"std"};
  }
}

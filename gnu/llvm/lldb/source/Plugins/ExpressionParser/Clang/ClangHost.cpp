//===-- ClangHost.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ClangHost.h"

#include "clang/Basic/Version.h"
#include "clang/Config/config.h"
#include "clang/Driver/Driver.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Threading.h"

#include "lldb/Host/Config.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

#include <string>

using namespace lldb_private;

static bool VerifyClangPath(const llvm::Twine &clang_path) {
  if (FileSystem::Instance().IsDirectory(clang_path))
    return true;
  Log *log = GetLog(LLDBLog::Host);
  LLDB_LOGF(log,
            "VerifyClangPath(): "
            "failed to stat clang resource directory at \"%s\"",
            clang_path.str().c_str());
  return false;
}

///
/// This will compute the clang resource directory assuming that clang was
/// installed with the same prefix as lldb.
///
/// If verify is true, the first candidate resource directory will be returned.
/// This mode is only used for testing.
///
static bool DefaultComputeClangResourceDirectory(FileSpec &lldb_shlib_spec,
                                                 FileSpec &file_spec,
                                                 bool verify) {
  Log *log = GetLog(LLDBLog::Host);
  std::string raw_path = lldb_shlib_spec.GetPath();
  llvm::StringRef parent_dir = llvm::sys::path::parent_path(raw_path);
  static const std::string clang_resource_path =
      clang::driver::Driver::GetResourcesPath("bin/lldb", CLANG_RESOURCE_DIR);

  static const llvm::StringRef kResourceDirSuffixes[] = {
      // LLVM.org's build of LLDB uses the clang resource directory placed
      // in $install_dir/lib{,64}/clang/$clang_version or
      // $install_dir/bin/$CLANG_RESOURCE_DIR
      clang_resource_path,
      // swift-lldb uses the clang resource directory copied from swift, which
      // by default is placed in $install_dir/lib{,64}/lldb/clang. LLDB places
      // it there, so we use LLDB_INSTALL_LIBDIR_BASENAME.
      LLDB_INSTALL_LIBDIR_BASENAME "/lldb/clang",
  };

  for (const auto &Suffix : kResourceDirSuffixes) {
    llvm::SmallString<256> clang_dir(parent_dir);
    llvm::SmallString<32> relative_path(Suffix);
    llvm::sys::path::native(relative_path);
    llvm::sys::path::append(clang_dir, relative_path);
    if (!verify || VerifyClangPath(clang_dir)) {
      LLDB_LOG(log,
               "DefaultComputeClangResourceDir: Setting ClangResourceDir "
               "to \"{0}\", verify = {1}",
               clang_dir.str(), verify ? "true" : "false");
      file_spec.SetDirectory(clang_dir);
      FileSystem::Instance().Resolve(file_spec);
      return true;
    }
  }

  return false;
}

bool lldb_private::ComputeClangResourceDirectory(FileSpec &lldb_shlib_spec,
                                                 FileSpec &file_spec,
                                                 bool verify) {
#if !defined(__APPLE__)
  return DefaultComputeClangResourceDirectory(lldb_shlib_spec, file_spec,
                                              verify);
#else
  std::string raw_path = lldb_shlib_spec.GetPath();

  auto rev_it = llvm::sys::path::rbegin(raw_path);
  auto r_end = llvm::sys::path::rend(raw_path);

  // Check for a Posix-style build of LLDB.
  while (rev_it != r_end) {
    if (*rev_it == "LLDB.framework")
      break;
    ++rev_it;
  }

  // We found a non-framework build of LLDB
  if (rev_it == r_end)
    return DefaultComputeClangResourceDirectory(lldb_shlib_spec, file_spec,
                                                verify);

  // Inside Xcode and in Xcode toolchains LLDB is always in lockstep
  // with the Swift compiler, so it can reuse its Clang resource
  // directory. This allows LLDB and the Swift compiler to share the
  // same Clang module cache.
  llvm::SmallString<256> clang_path;
  const char *swift_clang_resource_dir = "usr/lib/swift/clang";
  auto parent = std::next(rev_it);
  if (parent != r_end && *parent == "SharedFrameworks") {
    // This is the top-level LLDB in the Xcode.app bundle.
    // E.g., "Xcode.app/Contents/SharedFrameworks/LLDB.framework/Versions/A"
    raw_path.resize(parent - r_end);
    llvm::sys::path::append(clang_path, raw_path,
                            "Developer/Toolchains/XcodeDefault.xctoolchain",
                            swift_clang_resource_dir);
    if (!verify || VerifyClangPath(clang_path)) {
      file_spec.SetDirectory(clang_path);
      FileSystem::Instance().Resolve(file_spec);
      return true;
    }
  } else if (parent != r_end && *parent == "PrivateFrameworks" &&
             std::distance(parent, r_end) > 2) {
    ++parent;
    ++parent;
    if (*parent == "System") {
      // This is LLDB inside an Xcode toolchain.
      // E.g., "Xcode.app/Contents/Developer/Toolchains/"               \
      //       "My.xctoolchain/System/Library/PrivateFrameworks/LLDB.framework"
      raw_path.resize(parent - r_end);
      llvm::sys::path::append(clang_path, raw_path, swift_clang_resource_dir);
      if (!verify || VerifyClangPath(clang_path)) {
        file_spec.SetDirectory(clang_path);
        FileSystem::Instance().Resolve(file_spec);
        return true;
      }
    }
  }

  // Fall back to the Clang resource directory inside the framework.
  raw_path = lldb_shlib_spec.GetPath();
  raw_path.resize(rev_it - r_end);
  raw_path.append("LLDB.framework/Resources/Clang");
  file_spec.SetDirectory(raw_path);
  FileSystem::Instance().Resolve(file_spec);
  return true;
#endif // __APPLE__
}

FileSpec lldb_private::GetClangResourceDir() {
  static FileSpec g_cached_resource_dir;
  static llvm::once_flag g_once_flag;
  llvm::call_once(g_once_flag, []() {
    if (FileSpec lldb_file_spec = HostInfo::GetShlibDir())
      ComputeClangResourceDirectory(lldb_file_spec, g_cached_resource_dir,
                                    true);
    Log *log = GetLog(LLDBLog::Host);
    LLDB_LOGF(log, "GetClangResourceDir() => '%s'",
              g_cached_resource_dir.GetPath().c_str());
  });
  return g_cached_resource_dir;
}

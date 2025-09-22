//===-- HostInfoPosix.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/posix/HostInfoPosix.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/UserIDResolver.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <climits>
#include <cstdlib>
#include <grp.h>
#include <mutex>
#include <optional>
#include <pwd.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

using namespace lldb_private;

size_t HostInfoPosix::GetPageSize() { return ::getpagesize(); }

bool HostInfoPosix::GetHostname(std::string &s) {
  char hostname[PATH_MAX];
  hostname[sizeof(hostname) - 1] = '\0';
  if (::gethostname(hostname, sizeof(hostname) - 1) == 0) {
    s.assign(hostname);
    return true;
  }
  return false;
}

std::optional<std::string> HostInfoPosix::GetOSKernelDescription() {
  struct utsname un;
  if (uname(&un) < 0)
    return std::nullopt;

  return std::string(un.version);
}

#ifdef __ANDROID__
#include <android/api-level.h>
#endif
#if defined(__ANDROID_API__) && __ANDROID_API__ < 21
#define USE_GETPWUID
#endif

namespace {
class PosixUserIDResolver : public UserIDResolver {
protected:
  std::optional<std::string> DoGetUserName(id_t uid) override;
  std::optional<std::string> DoGetGroupName(id_t gid) override;
};
} // namespace

struct PasswdEntry {
  std::string username;
  std::string shell;
};

static std::optional<PasswdEntry> GetPassword(id_t uid) {
#ifdef USE_GETPWUID
  // getpwuid_r is missing from android-9
  // The caller should provide some thread safety by making sure no one calls
  // this function concurrently, because using getpwuid is ultimately not
  // thread-safe as we don't know who else might be calling it.
  if (auto *user_info_ptr = ::getpwuid(uid))
    return PasswdEntry{user_info_ptr->pw_name, user_info_ptr->pw_shell};
#else
  struct passwd user_info;
  struct passwd *user_info_ptr = &user_info;
  char user_buffer[PATH_MAX];
  size_t user_buffer_size = sizeof(user_buffer);
  if (::getpwuid_r(uid, &user_info, user_buffer, user_buffer_size,
                   &user_info_ptr) == 0 &&
      user_info_ptr) {
    return PasswdEntry{user_info_ptr->pw_name, user_info_ptr->pw_shell};
  }
#endif
  return std::nullopt;
}

std::optional<std::string> PosixUserIDResolver::DoGetUserName(id_t uid) {
  if (std::optional<PasswdEntry> password = GetPassword(uid))
    return password->username;
  return std::nullopt;
}

std::optional<std::string> PosixUserIDResolver::DoGetGroupName(id_t gid) {
#ifndef __ANDROID__
  char group_buffer[PATH_MAX];
  size_t group_buffer_size = sizeof(group_buffer);
  struct group group_info;
  struct group *group_info_ptr = &group_info;
  // Try the threadsafe version first
  if (::getgrgid_r(gid, &group_info, group_buffer, group_buffer_size,
                   &group_info_ptr) == 0) {
    if (group_info_ptr)
      return std::string(group_info_ptr->gr_name);
  } else {
    // The threadsafe version isn't currently working for me on darwin, but the
    // non-threadsafe version is, so I am calling it below.
    group_info_ptr = ::getgrgid(gid);
    if (group_info_ptr)
      return std::string(group_info_ptr->gr_name);
  }
#endif
  return std::nullopt;
}

static llvm::ManagedStatic<PosixUserIDResolver> g_user_id_resolver;

UserIDResolver &HostInfoPosix::GetUserIDResolver() {
  return *g_user_id_resolver;
}

uint32_t HostInfoPosix::GetUserID() { return getuid(); }

uint32_t HostInfoPosix::GetGroupID() { return getgid(); }

uint32_t HostInfoPosix::GetEffectiveUserID() { return geteuid(); }

uint32_t HostInfoPosix::GetEffectiveGroupID() { return getegid(); }

FileSpec HostInfoPosix::GetDefaultShell() {
  if (const char *v = ::getenv("SHELL"))
    return FileSpec(v);
  if (std::optional<PasswdEntry> password = GetPassword(::geteuid()))
    return FileSpec(password->shell);
  return FileSpec("/bin/sh");
}

bool HostInfoPosix::ComputeSupportExeDirectory(FileSpec &file_spec) {
  return ComputePathRelativeToLibrary(file_spec, "/bin");
}

bool HostInfoPosix::ComputeHeaderDirectory(FileSpec &file_spec) {
  FileSpec temp_file("/opt/local/include/lldb");
  file_spec.SetDirectory(temp_file.GetPath());
  return true;
}

bool HostInfoPosix::GetEnvironmentVar(const std::string &var_name,
                                      std::string &var) {
  if (const char *pvar = ::getenv(var_name.c_str())) {
    var = std::string(pvar);
    return true;
  }
  return false;
}

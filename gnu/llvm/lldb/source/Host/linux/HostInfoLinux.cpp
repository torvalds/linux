//===-- HostInfoLinux.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/linux/HostInfoLinux.h"
#include "lldb/Host/Config.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

#include "llvm/Support/Threading.h"

#include <climits>
#include <cstdio>
#include <cstring>
#include <sys/utsname.h>
#include <unistd.h>

#include <algorithm>
#include <mutex>
#include <optional>

using namespace lldb_private;

namespace {
struct HostInfoLinuxFields {
  llvm::once_flag m_distribution_once_flag;
  std::string m_distribution_id;
  llvm::once_flag m_os_version_once_flag;
  llvm::VersionTuple m_os_version;
};
} // namespace

static HostInfoLinuxFields *g_fields = nullptr;

void HostInfoLinux::Initialize(SharedLibraryDirectoryHelper *helper) {
  HostInfoPosix::Initialize(helper);

  g_fields = new HostInfoLinuxFields();
}

void HostInfoLinux::Terminate() {
  assert(g_fields && "Missing call to Initialize?");
  delete g_fields;
  g_fields = nullptr;
  HostInfoBase::Terminate();
}

llvm::VersionTuple HostInfoLinux::GetOSVersion() {
  assert(g_fields && "Missing call to Initialize?");
  llvm::call_once(g_fields->m_os_version_once_flag, []() {
    struct utsname un;
    if (uname(&un) != 0)
      return;

    llvm::StringRef release = un.release;
    // The kernel release string can include a lot of stuff (e.g.
    // 4.9.0-6-amd64). We're only interested in the numbered prefix.
    release = release.substr(0, release.find_first_not_of("0123456789."));
    g_fields->m_os_version.tryParse(release);
  });

  return g_fields->m_os_version;
}

std::optional<std::string> HostInfoLinux::GetOSBuildString() {
  struct utsname un;
  ::memset(&un, 0, sizeof(utsname));

  if (uname(&un) < 0)
    return std::nullopt;

  return std::string(un.release);
}

llvm::StringRef HostInfoLinux::GetDistributionId() {
  assert(g_fields && "Missing call to Initialize?");
  // Try to run 'lbs_release -i', and use that response for the distribution
  // id.
  llvm::call_once(g_fields->m_distribution_once_flag, []() {
    Log *log = GetLog(LLDBLog::Host);
    LLDB_LOGF(log, "attempting to determine Linux distribution...");

    // check if the lsb_release command exists at one of the following paths
    const char *const exe_paths[] = {"/bin/lsb_release",
                                     "/usr/bin/lsb_release"};

    for (size_t exe_index = 0;
         exe_index < sizeof(exe_paths) / sizeof(exe_paths[0]); ++exe_index) {
      const char *const get_distribution_info_exe = exe_paths[exe_index];
      if (access(get_distribution_info_exe, F_OK)) {
        // this exe doesn't exist, move on to next exe
        LLDB_LOGF(log, "executable doesn't exist: %s",
                  get_distribution_info_exe);
        continue;
      }

      // execute the distribution-retrieval command, read output
      std::string get_distribution_id_command(get_distribution_info_exe);
      get_distribution_id_command += " -i";

      FILE *file = popen(get_distribution_id_command.c_str(), "r");
      if (!file) {
        LLDB_LOGF(log,
                  "failed to run command: \"%s\", cannot retrieve "
                  "platform information",
                  get_distribution_id_command.c_str());
        break;
      }

      // retrieve the distribution id string.
      char distribution_id[256] = {'\0'};
      if (fgets(distribution_id, sizeof(distribution_id) - 1, file) !=
          nullptr) {
        LLDB_LOGF(log, "distribution id command returned \"%s\"",
                  distribution_id);

        const char *const distributor_id_key = "Distributor ID:\t";
        if (strstr(distribution_id, distributor_id_key)) {
          // strip newlines
          std::string id_string(distribution_id + strlen(distributor_id_key));
          llvm::erase(id_string, '\n');

          // lower case it and convert whitespace to underscores
          std::transform(
              id_string.begin(), id_string.end(), id_string.begin(),
              [](char ch) { return tolower(isspace(ch) ? '_' : ch); });

          g_fields->m_distribution_id = id_string;
          LLDB_LOGF(log, "distribution id set to \"%s\"",
                    g_fields->m_distribution_id.c_str());
        } else {
          LLDB_LOGF(log, "failed to find \"%s\" field in \"%s\"",
                    distributor_id_key, distribution_id);
        }
      } else {
        LLDB_LOGF(log,
                  "failed to retrieve distribution id, \"%s\" returned no"
                  " lines",
                  get_distribution_id_command.c_str());
      }

      // clean up the file
      pclose(file);
    }
  });

  return g_fields->m_distribution_id;
}

FileSpec HostInfoLinux::GetProgramFileSpec() {
  static FileSpec g_program_filespec;

  if (!g_program_filespec) {
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len > 0) {
      exe_path[len] = 0;
      g_program_filespec.SetFile(exe_path, FileSpec::Style::native);
    }
  }

  return g_program_filespec;
}

bool HostInfoLinux::ComputeSupportExeDirectory(FileSpec &file_spec) {
  if (HostInfoPosix::ComputeSupportExeDirectory(file_spec) &&
      file_spec.IsAbsolute() && FileSystem::Instance().Exists(file_spec))
    return true;
  file_spec.SetDirectory(GetProgramFileSpec().GetDirectory());
  return !file_spec.GetDirectory().IsEmpty();
}

bool HostInfoLinux::ComputeSystemPluginsDirectory(FileSpec &file_spec) {
  FileSpec temp_file("/usr/" LLDB_INSTALL_LIBDIR_BASENAME "/lldb/plugins");
  FileSystem::Instance().Resolve(temp_file);
  file_spec.SetDirectory(temp_file.GetPath());
  return true;
}

bool HostInfoLinux::ComputeUserPluginsDirectory(FileSpec &file_spec) {
  // XDG Base Directory Specification
  // http://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html If
  // XDG_DATA_HOME exists, use that, otherwise use ~/.local/share/lldb.
  const char *xdg_data_home = getenv("XDG_DATA_HOME");
  if (xdg_data_home && xdg_data_home[0]) {
    std::string user_plugin_dir(xdg_data_home);
    user_plugin_dir += "/lldb";
    file_spec.SetDirectory(user_plugin_dir.c_str());
  } else
    file_spec.SetDirectory("~/.local/share/lldb");
  return true;
}

void HostInfoLinux::ComputeHostArchitectureSupport(ArchSpec &arch_32,
                                                   ArchSpec &arch_64) {
  HostInfoPosix::ComputeHostArchitectureSupport(arch_32, arch_64);

  // On Linux, "unknown" in the vendor slot isn't what we want for the default
  // triple.  It's probably an artifact of config.guess.
  if (arch_32.IsValid()) {
    if (arch_32.GetTriple().getVendor() == llvm::Triple::UnknownVendor)
      arch_32.GetTriple().setVendorName(llvm::StringRef());
  }
  if (arch_64.IsValid()) {
    if (arch_64.GetTriple().getVendor() == llvm::Triple::UnknownVendor)
      arch_64.GetTriple().setVendorName(llvm::StringRef());
  }
}

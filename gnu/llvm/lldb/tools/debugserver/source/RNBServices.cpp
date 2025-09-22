//===-- RNBServices.cpp -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Created by Christopher Friesen on 3/21/08.
//
//===----------------------------------------------------------------------===//

#include "RNBServices.h"

#include "DNB.h"
#include "CFString.h"
#include "DNBLog.h"
#include "MacOSX/CFUtils.h"
#include <CoreFoundation/CoreFoundation.h>
#include <libproc.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <vector>

// For now only SpringBoard has a notion of "Applications" that it can list for
// us.
// So we have to use the SpringBoard API's here.
#if defined(WITH_SPRINGBOARD) || defined(WITH_BKS)
#include <SpringBoardServices/SpringBoardServices.h>
#endif

int GetProcesses(CFMutableArrayRef plistMutableArray, bool all_users) {
  if (plistMutableArray == NULL)
    return -1;

  // Running as root, get all processes
  std::vector<struct kinfo_proc> proc_infos;
  const size_t num_proc_infos = DNBGetAllInfos(proc_infos);
  if (num_proc_infos > 0) {
    const pid_t our_pid = getpid();
    const uid_t our_uid = getuid();
    uint32_t i;
    CFAllocatorRef alloc = kCFAllocatorDefault;

    for (i = 0; i < num_proc_infos; i++) {
      struct kinfo_proc &proc_info = proc_infos[i];

      bool kinfo_user_matches;
      // Special case, if lldb is being run as root we can attach to anything.
      if (all_users)
        kinfo_user_matches = true;
      else
        kinfo_user_matches = proc_info.kp_eproc.e_pcred.p_ruid == our_uid;

      const pid_t pid = proc_info.kp_proc.p_pid;
      // Skip zombie processes and processes with unset status
      if (!kinfo_user_matches || // User is acceptable
          pid == our_pid ||      // Skip this process
          pid == 0 ||            // Skip kernel (kernel pid is zero)
          proc_info.kp_proc.p_stat ==
              SZOMB || // Zombies are bad, they like brains...
          proc_info.kp_proc.p_flag & P_TRACED || // Being debugged?
          proc_info.kp_proc.p_flag & P_WEXIT     // Working on exiting?
      )
        continue;

      // Create a new mutable dictionary for each application
      CFReleaser<CFMutableDictionaryRef> appInfoDict(
          ::CFDictionaryCreateMutable(alloc, 0, &kCFTypeDictionaryKeyCallBacks,
                                      &kCFTypeDictionaryValueCallBacks));

      // Get the process id for the app (if there is one)
      const int32_t pid_int32 = pid;
      CFReleaser<CFNumberRef> pidCFNumber(
          ::CFNumberCreate(alloc, kCFNumberSInt32Type, &pid_int32));
      ::CFDictionarySetValue(appInfoDict.get(), DTSERVICES_APP_PID_KEY,
                             pidCFNumber.get());

      // Set a boolean to indicate if this is the front most
      ::CFDictionarySetValue(appInfoDict.get(), DTSERVICES_APP_FRONTMOST_KEY,
                             kCFBooleanFalse);

      const char *pid_basename = proc_info.kp_proc.p_comm;
      char proc_path_buf[PATH_MAX];

      int return_val = proc_pidpath(pid, proc_path_buf, PATH_MAX);
      if (return_val > 0) {
        // Okay, now search backwards from that to see if there is a
        // slash in the name.  Note, even though we got all the args we don't
        // care
        // because the list data is just a bunch of concatenated null terminated
        // strings
        // so strrchr will start from the end of argv0.

        pid_basename = strrchr(proc_path_buf, '/');
        if (pid_basename) {
          // Skip the '/'
          ++pid_basename;
        } else {
          // We didn't find a directory delimiter in the process argv[0], just
          // use what was in there
          pid_basename = proc_path_buf;
        }
        CFString cf_pid_path(proc_path_buf);
        if (cf_pid_path.get())
          ::CFDictionarySetValue(appInfoDict.get(), DTSERVICES_APP_PATH_KEY,
                                 cf_pid_path.get());
      }

      if (pid_basename && pid_basename[0]) {
        CFString pid_name(pid_basename);
        ::CFDictionarySetValue(appInfoDict.get(),
                               DTSERVICES_APP_DISPLAY_NAME_KEY, pid_name.get());
      }

      // Append the application info to the plist array
      ::CFArrayAppendValue(plistMutableArray, appInfoDict.get());
    }
  }
  return 0;
}
int ListApplications(std::string &plist, bool opt_runningApps,
                     bool opt_debuggable) {
  int result = -1;

  CFAllocatorRef alloc = kCFAllocatorDefault;

  // Create a mutable array that we can populate. Specify zero so it can be of
  // any size.
  CFReleaser<CFMutableArrayRef> plistMutableArray(
      ::CFArrayCreateMutable(alloc, 0, &kCFTypeArrayCallBacks));

  const uid_t our_uid = getuid();

#if defined(WITH_SPRINGBOARD) || defined(WITH_BKS)

  if (our_uid == 0) {
    bool all_users = true;
    result = GetProcesses(plistMutableArray.get(), all_users);
  } else {
    CFReleaser<CFStringRef> sbsFrontAppID(
        ::SBSCopyFrontmostApplicationDisplayIdentifier());
    CFReleaser<CFArrayRef> sbsAppIDs(::SBSCopyApplicationDisplayIdentifiers(
        opt_runningApps, opt_debuggable));

    // Need to check the return value from SBSCopyApplicationDisplayIdentifiers.
    CFIndex count = sbsAppIDs.get() ? ::CFArrayGetCount(sbsAppIDs.get()) : 0;
    CFIndex i = 0;
    for (i = 0; i < count; i++) {
      CFStringRef displayIdentifier =
          (CFStringRef)::CFArrayGetValueAtIndex(sbsAppIDs.get(), i);

      // Create a new mutable dictionary for each application
      CFReleaser<CFMutableDictionaryRef> appInfoDict(
          ::CFDictionaryCreateMutable(alloc, 0, &kCFTypeDictionaryKeyCallBacks,
                                      &kCFTypeDictionaryValueCallBacks));

      // Get the process id for the app (if there is one)
      pid_t pid = INVALID_NUB_PROCESS;
      if (::SBSProcessIDForDisplayIdentifier((CFStringRef)displayIdentifier,
                                             &pid) == true) {
        CFReleaser<CFNumberRef> pidCFNumber(
            ::CFNumberCreate(alloc, kCFNumberSInt32Type, &pid));
        ::CFDictionarySetValue(appInfoDict.get(), DTSERVICES_APP_PID_KEY,
                               pidCFNumber.get());
      }

      // Set a boolean to indicate if this is the front most
      if (sbsFrontAppID.get() && displayIdentifier &&
          (::CFStringCompare(sbsFrontAppID.get(), displayIdentifier, 0) ==
           kCFCompareEqualTo))
        ::CFDictionarySetValue(appInfoDict.get(), DTSERVICES_APP_FRONTMOST_KEY,
                               kCFBooleanTrue);
      else
        ::CFDictionarySetValue(appInfoDict.get(), DTSERVICES_APP_FRONTMOST_KEY,
                               kCFBooleanFalse);

      CFReleaser<CFStringRef> executablePath(
          ::SBSCopyExecutablePathForDisplayIdentifier(displayIdentifier));
      if (executablePath.get() != NULL) {
        ::CFDictionarySetValue(appInfoDict.get(), DTSERVICES_APP_PATH_KEY,
                               executablePath.get());
      }

      CFReleaser<CFStringRef> iconImagePath(
          ::SBSCopyIconImagePathForDisplayIdentifier(displayIdentifier));
      if (iconImagePath.get() != NULL) {
        ::CFDictionarySetValue(appInfoDict.get(), DTSERVICES_APP_ICON_PATH_KEY,
                               iconImagePath.get());
      }

      CFReleaser<CFStringRef> localizedDisplayName(
          ::SBSCopyLocalizedApplicationNameForDisplayIdentifier(
              displayIdentifier));
      if (localizedDisplayName.get() != NULL) {
        ::CFDictionarySetValue(appInfoDict.get(),
                               DTSERVICES_APP_DISPLAY_NAME_KEY,
                               localizedDisplayName.get());
      }

      // Append the application info to the plist array
      ::CFArrayAppendValue(plistMutableArray.get(), appInfoDict.get());
    }
  }
#else // #if defined (WITH_SPRINGBOARD) || defined (WITH_BKS)
  // When root, show all processes
  bool all_users = (our_uid == 0);
  GetProcesses(plistMutableArray.get(), all_users);
#endif

  CFReleaser<CFDataRef> plistData(
      ::CFPropertyListCreateXMLData(alloc, plistMutableArray.get()));

  // write plist to service port
  if (plistData.get() != NULL) {
    CFIndex size = ::CFDataGetLength(plistData.get());
    const UInt8 *bytes = ::CFDataGetBytePtr(plistData.get());
    if (bytes != NULL && size > 0) {
      plist.assign((const char *)bytes, size);
      return 0; // Success
    } else {
      DNBLogError("empty application property list.");
      result = -2;
    }
  } else {
    DNBLogError("serializing task list.");
    result = -3;
  }

  return result;
}

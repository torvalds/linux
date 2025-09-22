//===-- sanitizer_mac.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is shared between various sanitizers' runtime libraries and
// provides definitions for OSX-specific functions.
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_APPLE_H
#define SANITIZER_APPLE_H

#include "sanitizer_common.h"
#include "sanitizer_platform.h"
#if SANITIZER_APPLE
#include "sanitizer_posix.h"

namespace __sanitizer {

struct MemoryMappingLayoutData {
  int current_image;
  u32 current_magic;
  u32 current_filetype;
  ModuleArch current_arch;
  u8 current_uuid[kModuleUUIDSize];
  int current_load_cmd_count;
  const char *current_load_cmd_addr;
  bool current_instrumented;
};

template <typename VersionType>
struct VersionBase {
  u16 major;
  u16 minor;

  VersionBase(u16 major, u16 minor) : major(major), minor(minor) {}

  bool operator==(const VersionType &other) const {
    return major == other.major && minor == other.minor;
  }
  bool operator>=(const VersionType &other) const {
    return major > other.major ||
           (major == other.major && minor >= other.minor);
  }
  bool operator<(const VersionType &other) const { return !(*this >= other); }
};

struct MacosVersion : VersionBase<MacosVersion> {
  MacosVersion(u16 major, u16 minor) : VersionBase(major, minor) {}
};

struct DarwinKernelVersion : VersionBase<DarwinKernelVersion> {
  DarwinKernelVersion(u16 major, u16 minor) : VersionBase(major, minor) {}
};

MacosVersion GetMacosAlignedVersion();
DarwinKernelVersion GetDarwinKernelVersion();

char **GetEnviron();

void RestrictMemoryToMaxAddress(uptr max_address);

using ThreadEventCallback = void (*)(uptr thread);
using ThreadCreateEventCallback = void (*)(uptr thread, bool gcd_worker);
struct ThreadEventCallbacks {
  ThreadCreateEventCallback create;
  ThreadEventCallback start;
  ThreadEventCallback terminate;
  ThreadEventCallback destroy;
};

void InstallPthreadIntrospectionHook(const ThreadEventCallbacks &callbacks);

}  // namespace __sanitizer

#endif  // SANITIZER_APPLE
#endif  // SANITIZER_APPLE_H

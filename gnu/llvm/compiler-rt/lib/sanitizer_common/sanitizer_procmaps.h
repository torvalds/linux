//===-- sanitizer_procmaps.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is shared between AddressSanitizer and ThreadSanitizer.
//
// Information about the process mappings.
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_PROCMAPS_H
#define SANITIZER_PROCMAPS_H

#include "sanitizer_platform.h"

#if SANITIZER_LINUX || SANITIZER_FREEBSD || SANITIZER_NETBSD || \
    SANITIZER_APPLE || SANITIZER_SOLARIS ||  \
    SANITIZER_FUCHSIA

#include "sanitizer_common.h"
#include "sanitizer_internal_defs.h"
#include "sanitizer_fuchsia.h"
#include "sanitizer_linux.h"
#include "sanitizer_mac.h"
#include "sanitizer_mutex.h"

namespace __sanitizer {

// Memory protection masks.
static const uptr kProtectionRead = 1;
static const uptr kProtectionWrite = 2;
static const uptr kProtectionExecute = 4;
static const uptr kProtectionShared = 8;

struct MemoryMappedSegmentData;

class MemoryMappedSegment {
 public:
  explicit MemoryMappedSegment(char *buff = nullptr, uptr size = 0)
      : filename(buff), filename_size(size), data_(nullptr) {}
  ~MemoryMappedSegment() {}

  bool IsReadable() const { return protection & kProtectionRead; }
  bool IsWritable() const { return protection & kProtectionWrite; }
  bool IsExecutable() const { return protection & kProtectionExecute; }
  bool IsShared() const { return protection & kProtectionShared; }

  void AddAddressRanges(LoadedModule *module);

  uptr start;
  uptr end;
  uptr offset;
  char *filename;  // owned by caller
  uptr filename_size;
  uptr protection;
  ModuleArch arch;
  u8 uuid[kModuleUUIDSize];

 private:
  friend class MemoryMappingLayout;

  // This field is assigned and owned by MemoryMappingLayout if needed
  MemoryMappedSegmentData *data_;
};

struct ImageHeader;

class MemoryMappingLayoutBase {
 public:
  virtual bool Next(MemoryMappedSegment *segment) { UNIMPLEMENTED(); }
  virtual bool Error() const { UNIMPLEMENTED(); };
  virtual void Reset() { UNIMPLEMENTED(); }

 protected:
  ~MemoryMappingLayoutBase() {}
};

class MemoryMappingLayout : public MemoryMappingLayoutBase {
 public:
  explicit MemoryMappingLayout(bool cache_enabled);

// This destructor cannot be virtual, as it would cause an operator new() linking
// failures in hwasan test cases. However non-virtual destructors emit warnings
// in macOS build, hence disabling those
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnon-virtual-dtor"
#endif
  ~MemoryMappingLayout();
#ifdef __clang__
#pragma clang diagnostic pop
#endif

  virtual bool Next(MemoryMappedSegment *segment) override;
  virtual bool Error() const override;
  virtual void Reset() override;
  // In some cases, e.g. when running under a sandbox on Linux, ASan is unable
  // to obtain the memory mappings. It should fall back to pre-cached data
  // instead of aborting.
  static void CacheMemoryMappings();

  // Adds all mapped objects into a vector.
  void DumpListOfModules(InternalMmapVectorNoCtor<LoadedModule> *modules);

 protected:
#if SANITIZER_APPLE
  virtual const ImageHeader *CurrentImageHeader();
#endif
  MemoryMappingLayoutData data_;

 private:
  void LoadFromCache();
};

// Returns code range for the specified module.
bool GetCodeRangeForFile(const char *module, uptr *start, uptr *end);

bool IsDecimal(char c);
uptr ParseDecimal(const char **p);
bool IsHex(char c);
uptr ParseHex(const char **p);

}  // namespace __sanitizer

#endif
#endif  // SANITIZER_PROCMAPS_H

//===-- sanitizer_procmaps_mac.cpp ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Information about the process mappings (Mac-specific parts).
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"
#if SANITIZER_APPLE
#include "sanitizer_common.h"
#include "sanitizer_placement_new.h"
#include "sanitizer_procmaps.h"

#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach/mach.h>

// These are not available in older macOS SDKs.
#ifndef CPU_SUBTYPE_X86_64_H
#define CPU_SUBTYPE_X86_64_H  ((cpu_subtype_t)8)   /* Haswell */
#endif
#ifndef CPU_SUBTYPE_ARM_V7S
#define CPU_SUBTYPE_ARM_V7S   ((cpu_subtype_t)11)  /* Swift */
#endif
#ifndef CPU_SUBTYPE_ARM_V7K
#define CPU_SUBTYPE_ARM_V7K   ((cpu_subtype_t)12)
#endif
#ifndef CPU_TYPE_ARM64
#define CPU_TYPE_ARM64        (CPU_TYPE_ARM | CPU_ARCH_ABI64)
#endif

namespace __sanitizer {

// Contains information used to iterate through sections.
struct MemoryMappedSegmentData {
  char name[kMaxSegName];
  uptr nsects;
  const char *current_load_cmd_addr;
  u32 lc_type;
  uptr base_virt_addr;
  uptr addr_mask;
};

template <typename Section>
static void NextSectionLoad(LoadedModule *module, MemoryMappedSegmentData *data,
                            bool isWritable) {
  const Section *sc = (const Section *)data->current_load_cmd_addr;
  data->current_load_cmd_addr += sizeof(Section);

  uptr sec_start = (sc->addr & data->addr_mask) + data->base_virt_addr;
  uptr sec_end = sec_start + sc->size;
  module->addAddressRange(sec_start, sec_end, /*executable=*/false, isWritable,
                          sc->sectname);
}

void MemoryMappedSegment::AddAddressRanges(LoadedModule *module) {
  // Don't iterate over sections when the caller hasn't set up the
  // data pointer, when there are no sections, or when the segment
  // is executable. Avoid iterating over executable sections because
  // it will confuse libignore, and because the extra granularity
  // of information is not needed by any sanitizers.
  if (!data_ || !data_->nsects || IsExecutable()) {
    module->addAddressRange(start, end, IsExecutable(), IsWritable(),
                            data_ ? data_->name : nullptr);
    return;
  }

  do {
    if (data_->lc_type == LC_SEGMENT) {
      NextSectionLoad<struct section>(module, data_, IsWritable());
#ifdef MH_MAGIC_64
    } else if (data_->lc_type == LC_SEGMENT_64) {
      NextSectionLoad<struct section_64>(module, data_, IsWritable());
#endif
    }
  } while (--data_->nsects);
}

MemoryMappingLayout::MemoryMappingLayout(bool cache_enabled) {
  Reset();
}

MemoryMappingLayout::~MemoryMappingLayout() {
}

bool MemoryMappingLayout::Error() const {
  return false;
}

// More information about Mach-O headers can be found in mach-o/loader.h
// Each Mach-O image has a header (mach_header or mach_header_64) starting with
// a magic number, and a list of linker load commands directly following the
// header.
// A load command is at least two 32-bit words: the command type and the
// command size in bytes. We're interested only in segment load commands
// (LC_SEGMENT and LC_SEGMENT_64), which tell that a part of the file is mapped
// into the task's address space.
// The |vmaddr|, |vmsize| and |fileoff| fields of segment_command or
// segment_command_64 correspond to the memory address, memory size and the
// file offset of the current memory segment.
// Because these fields are taken from the images as is, one needs to add
// _dyld_get_image_vmaddr_slide() to get the actual addresses at runtime.

void MemoryMappingLayout::Reset() {
  // Count down from the top.
  // TODO(glider): as per man 3 dyld, iterating over the headers with
  // _dyld_image_count is thread-unsafe. We need to register callbacks for
  // adding and removing images which will invalidate the MemoryMappingLayout
  // state.
  data_.current_image = _dyld_image_count();
  data_.current_load_cmd_count = -1;
  data_.current_load_cmd_addr = 0;
  data_.current_magic = 0;
  data_.current_filetype = 0;
  data_.current_arch = kModuleArchUnknown;
  internal_memset(data_.current_uuid, 0, kModuleUUIDSize);
}

// The dyld load address should be unchanged throughout process execution,
// and it is expensive to compute once many libraries have been loaded,
// so cache it here and do not reset.
static mach_header *dyld_hdr = 0;
static const char kDyldPath[] = "/usr/lib/dyld";
static const int kDyldImageIdx = -1;

// static
void MemoryMappingLayout::CacheMemoryMappings() {
  // No-op on Mac for now.
}

void MemoryMappingLayout::LoadFromCache() {
  // No-op on Mac for now.
}

static bool IsDyldHdr(const mach_header *hdr) {
  return (hdr->magic == MH_MAGIC || hdr->magic == MH_MAGIC_64) &&
         hdr->filetype == MH_DYLINKER;
}

// _dyld_get_image_header() and related APIs don't report dyld itself.
// We work around this by manually recursing through the memory map
// until we hit a Mach header matching dyld instead. These recurse
// calls are expensive, but the first memory map generation occurs
// early in the process, when dyld is one of the only images loaded,
// so it will be hit after only a few iterations.  These assumptions don't hold
// on macOS 13+ anymore (dyld itself has moved into the shared cache).
static mach_header *GetDyldImageHeaderViaVMRegion() {
  vm_address_t address = 0;

  while (true) {
    vm_size_t size = 0;
    unsigned depth = 1;
    struct vm_region_submap_info_64 info;
    mach_msg_type_number_t count = VM_REGION_SUBMAP_INFO_COUNT_64;
    kern_return_t err =
        vm_region_recurse_64(mach_task_self(), &address, &size, &depth,
                             (vm_region_info_t)&info, &count);
    if (err != KERN_SUCCESS) return nullptr;

    if (size >= sizeof(mach_header) && info.protection & kProtectionRead) {
      mach_header *hdr = (mach_header *)address;
      if (IsDyldHdr(hdr)) {
        return hdr;
      }
    }
    address += size;
  }
}

extern "C" {
struct dyld_shared_cache_dylib_text_info {
  uint64_t version;  // current version 2
  // following fields all exist in version 1
  uint64_t loadAddressUnslid;
  uint64_t textSegmentSize;
  uuid_t dylibUuid;
  const char *path;  // pointer invalid at end of iterations
  // following fields all exist in version 2
  uint64_t textSegmentOffset;  // offset from start of cache
};
typedef struct dyld_shared_cache_dylib_text_info
    dyld_shared_cache_dylib_text_info;

extern bool _dyld_get_shared_cache_uuid(uuid_t uuid);
extern const void *_dyld_get_shared_cache_range(size_t *length);
extern int dyld_shared_cache_iterate_text(
    const uuid_t cacheUuid,
    void (^callback)(const dyld_shared_cache_dylib_text_info *info));
}  // extern "C"

static mach_header *GetDyldImageHeaderViaSharedCache() {
  uuid_t uuid;
  bool hasCache = _dyld_get_shared_cache_uuid(uuid);
  if (!hasCache)
    return nullptr;

  size_t cacheLength;
  __block uptr cacheStart = (uptr)_dyld_get_shared_cache_range(&cacheLength);
  CHECK(cacheStart && cacheLength);

  __block mach_header *dyldHdr = nullptr;
  int res = dyld_shared_cache_iterate_text(
      uuid, ^(const dyld_shared_cache_dylib_text_info *info) {
        CHECK_GE(info->version, 2);
        mach_header *hdr =
            (mach_header *)(cacheStart + info->textSegmentOffset);
        if (IsDyldHdr(hdr))
          dyldHdr = hdr;
      });
  CHECK_EQ(res, 0);

  return dyldHdr;
}

const mach_header *get_dyld_hdr() {
  if (!dyld_hdr) {
    // On macOS 13+, dyld itself has moved into the shared cache.  Looking it up
    // via vm_region_recurse_64() causes spins/hangs/crashes.
    if (GetMacosAlignedVersion() >= MacosVersion(13, 0)) {
      dyld_hdr = GetDyldImageHeaderViaSharedCache();
      if (!dyld_hdr) {
        VReport(1,
                "Failed to lookup the dyld image header in the shared cache on "
                "macOS 13+ (or no shared cache in use).  Falling back to "
                "lookup via vm_region_recurse_64().\n");
        dyld_hdr = GetDyldImageHeaderViaVMRegion();
      }
    } else {
      dyld_hdr = GetDyldImageHeaderViaVMRegion();
    }
    CHECK(dyld_hdr);
  }

  return dyld_hdr;
}

// Next and NextSegmentLoad were inspired by base/sysinfo.cc in
// Google Perftools, https://github.com/gperftools/gperftools.

// NextSegmentLoad scans the current image for the next segment load command
// and returns the start and end addresses and file offset of the corresponding
// segment.
// Note that the segment addresses are not necessarily sorted.
template <u32 kLCSegment, typename SegmentCommand>
static bool NextSegmentLoad(MemoryMappedSegment *segment,
                            MemoryMappedSegmentData *seg_data,
                            MemoryMappingLayoutData *layout_data) {
  const char *lc = layout_data->current_load_cmd_addr;

  layout_data->current_load_cmd_addr += ((const load_command *)lc)->cmdsize;
  layout_data->current_load_cmd_count--;
  if (((const load_command *)lc)->cmd == kLCSegment) {
    const SegmentCommand* sc = (const SegmentCommand *)lc;
    uptr base_virt_addr, addr_mask;
    if (layout_data->current_image == kDyldImageIdx) {
      base_virt_addr = (uptr)get_dyld_hdr();
      // vmaddr is masked with 0xfffff because on macOS versions < 10.12,
      // it contains an absolute address rather than an offset for dyld.
      // To make matters even more complicated, this absolute address
      // isn't actually the absolute segment address, but the offset portion
      // of the address is accurate when combined with the dyld base address,
      // and the mask will give just this offset.
      addr_mask = 0xfffff;
    } else {
      base_virt_addr =
          (uptr)_dyld_get_image_vmaddr_slide(layout_data->current_image);
      addr_mask = ~0;
    }

    segment->start = (sc->vmaddr & addr_mask) + base_virt_addr;
    segment->end = segment->start + sc->vmsize;
    // Most callers don't need section information, so only fill this struct
    // when required.
    if (seg_data) {
      seg_data->nsects = sc->nsects;
      seg_data->current_load_cmd_addr =
          (const char *)lc + sizeof(SegmentCommand);
      seg_data->lc_type = kLCSegment;
      seg_data->base_virt_addr = base_virt_addr;
      seg_data->addr_mask = addr_mask;
      internal_strncpy(seg_data->name, sc->segname,
                       ARRAY_SIZE(seg_data->name));
    }

    // Return the initial protection.
    segment->protection = sc->initprot;
    segment->offset = (layout_data->current_filetype ==
                       /*MH_EXECUTE*/ 0x2)
                          ? sc->vmaddr
                          : sc->fileoff;
    if (segment->filename) {
      const char *src = (layout_data->current_image == kDyldImageIdx)
                            ? kDyldPath
                            : _dyld_get_image_name(layout_data->current_image);
      internal_strncpy(segment->filename, src, segment->filename_size);
    }
    segment->arch = layout_data->current_arch;
    internal_memcpy(segment->uuid, layout_data->current_uuid, kModuleUUIDSize);
    return true;
  }
  return false;
}

ModuleArch ModuleArchFromCpuType(cpu_type_t cputype, cpu_subtype_t cpusubtype) {
  cpusubtype = cpusubtype & ~CPU_SUBTYPE_MASK;
  switch (cputype) {
    case CPU_TYPE_I386:
      return kModuleArchI386;
    case CPU_TYPE_X86_64:
      if (cpusubtype == CPU_SUBTYPE_X86_64_ALL) return kModuleArchX86_64;
      if (cpusubtype == CPU_SUBTYPE_X86_64_H) return kModuleArchX86_64H;
      CHECK(0 && "Invalid subtype of x86_64");
      return kModuleArchUnknown;
    case CPU_TYPE_ARM:
      if (cpusubtype == CPU_SUBTYPE_ARM_V6) return kModuleArchARMV6;
      if (cpusubtype == CPU_SUBTYPE_ARM_V7) return kModuleArchARMV7;
      if (cpusubtype == CPU_SUBTYPE_ARM_V7S) return kModuleArchARMV7S;
      if (cpusubtype == CPU_SUBTYPE_ARM_V7K) return kModuleArchARMV7K;
      CHECK(0 && "Invalid subtype of ARM");
      return kModuleArchUnknown;
    case CPU_TYPE_ARM64:
      return kModuleArchARM64;
    default:
      CHECK(0 && "Invalid CPU type");
      return kModuleArchUnknown;
  }
}

static const load_command *NextCommand(const load_command *lc) {
  return (const load_command *)((const char *)lc + lc->cmdsize);
}

static void FindUUID(const load_command *first_lc, u8 *uuid_output) {
  for (const load_command *lc = first_lc; lc->cmd != 0; lc = NextCommand(lc)) {
    if (lc->cmd != LC_UUID) continue;

    const uuid_command *uuid_lc = (const uuid_command *)lc;
    const uint8_t *uuid = &uuid_lc->uuid[0];
    internal_memcpy(uuid_output, uuid, kModuleUUIDSize);
    return;
  }
}

static bool IsModuleInstrumented(const load_command *first_lc) {
  for (const load_command *lc = first_lc; lc->cmd != 0; lc = NextCommand(lc)) {
    if (lc->cmd != LC_LOAD_DYLIB) continue;

    const dylib_command *dylib_lc = (const dylib_command *)lc;
    uint32_t dylib_name_offset = dylib_lc->dylib.name.offset;
    const char *dylib_name = ((const char *)dylib_lc) + dylib_name_offset;
    dylib_name = StripModuleName(dylib_name);
    if (dylib_name != 0 && (internal_strstr(dylib_name, "libclang_rt."))) {
      return true;
    }
  }
  return false;
}

const ImageHeader *MemoryMappingLayout::CurrentImageHeader() {
  const mach_header *hdr = (data_.current_image == kDyldImageIdx)
                                ? get_dyld_hdr()
                                : _dyld_get_image_header(data_.current_image);
  return (const ImageHeader *)hdr;
}

bool MemoryMappingLayout::Next(MemoryMappedSegment *segment) {
  for (; data_.current_image >= kDyldImageIdx; data_.current_image--) {
    const mach_header *hdr = (const mach_header *)CurrentImageHeader();
    if (!hdr) continue;
    if (data_.current_load_cmd_count < 0) {
      // Set up for this image;
      data_.current_load_cmd_count = hdr->ncmds;
      data_.current_magic = hdr->magic;
      data_.current_filetype = hdr->filetype;
      data_.current_arch = ModuleArchFromCpuType(hdr->cputype, hdr->cpusubtype);
      switch (data_.current_magic) {
#ifdef MH_MAGIC_64
        case MH_MAGIC_64: {
          data_.current_load_cmd_addr =
              (const char *)hdr + sizeof(mach_header_64);
          break;
        }
#endif
        case MH_MAGIC: {
          data_.current_load_cmd_addr = (const char *)hdr + sizeof(mach_header);
          break;
        }
        default: {
          continue;
        }
      }
      FindUUID((const load_command *)data_.current_load_cmd_addr,
               data_.current_uuid);
      data_.current_instrumented = IsModuleInstrumented(
          (const load_command *)data_.current_load_cmd_addr);
    }

    while (data_.current_load_cmd_count > 0) {
      switch (data_.current_magic) {
        // data_.current_magic may be only one of MH_MAGIC, MH_MAGIC_64.
#ifdef MH_MAGIC_64
        case MH_MAGIC_64: {
          if (NextSegmentLoad<LC_SEGMENT_64, struct segment_command_64>(
                  segment, segment->data_, &data_))
            return true;
          break;
        }
#endif
        case MH_MAGIC: {
          if (NextSegmentLoad<LC_SEGMENT, struct segment_command>(
                  segment, segment->data_, &data_))
            return true;
          break;
        }
      }
    }
    // If we get here, no more load_cmd's in this image talk about
    // segments.  Go on to the next image.
    data_.current_load_cmd_count = -1; // This will trigger loading next image
  }
  return false;
}

void MemoryMappingLayout::DumpListOfModules(
    InternalMmapVectorNoCtor<LoadedModule> *modules) {
  Reset();
  InternalMmapVector<char> module_name(kMaxPathLength);
  MemoryMappedSegment segment(module_name.data(), module_name.size());
  MemoryMappedSegmentData data;
  segment.data_ = &data;
  while (Next(&segment)) {
    if (segment.filename[0] == '\0') continue;
    LoadedModule *cur_module = nullptr;
    if (!modules->empty() &&
        0 == internal_strcmp(segment.filename, modules->back().full_name())) {
      cur_module = &modules->back();
    } else {
      modules->push_back(LoadedModule());
      cur_module = &modules->back();
      cur_module->set(segment.filename, segment.start, segment.arch,
                      segment.uuid, data_.current_instrumented);
    }
    segment.AddAddressRanges(cur_module);
  }
}

}  // namespace __sanitizer

#endif  // SANITIZER_APPLE

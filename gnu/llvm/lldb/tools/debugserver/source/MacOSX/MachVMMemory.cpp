//===-- MachVMMemory.cpp ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 6/26/07.
//
//===----------------------------------------------------------------------===//

#include "MachVMMemory.h"
#include "DNBLog.h"
#include "MachVMRegion.h"
#include <dlfcn.h>
#include <mach/mach_vm.h>
#include <mach/shared_region.h>
#include <sys/sysctl.h>

#if defined(WITH_FBS) || defined(WITH_BKS)
extern "C" {
#import <System/sys/kern_memorystatus.h>
}
#endif

static const vm_size_t kInvalidPageSize = ~0;

MachVMMemory::MachVMMemory() : m_page_size(kInvalidPageSize), m_err(0) {}

MachVMMemory::~MachVMMemory() = default;

nub_size_t MachVMMemory::PageSize(task_t task) {
  if (m_page_size == kInvalidPageSize) {
#if defined(TASK_VM_INFO) && TASK_VM_INFO >= 22
    if (task != TASK_NULL) {
      kern_return_t kr;
      mach_msg_type_number_t info_count = TASK_VM_INFO_COUNT;
      task_vm_info_data_t vm_info;
      kr = task_info(task, TASK_VM_INFO, (task_info_t)&vm_info, &info_count);
      if (kr == KERN_SUCCESS) {
        DNBLogThreadedIf(
            LOG_TASK,
            "MachVMMemory::PageSize task_info returned page size of 0x%x",
            (int)vm_info.page_size);
        m_page_size = vm_info.page_size;
        return m_page_size;
      } else {
        DNBLogThreadedIf(LOG_TASK, "MachVMMemory::PageSize task_info call "
                                   "failed to get page size, TASK_VM_INFO %d, "
                                   "TASK_VM_INFO_COUNT %d, kern return %d",
                         TASK_VM_INFO, TASK_VM_INFO_COUNT, kr);
      }
    }
#endif
    m_err = ::host_page_size(::mach_host_self(), &m_page_size);
    if (m_err.Fail())
      m_page_size = 0;
  }
  return m_page_size;
}

nub_size_t MachVMMemory::MaxBytesLeftInPage(task_t task, nub_addr_t addr,
                                            nub_size_t count) {
  const nub_size_t page_size = PageSize(task);
  if (page_size > 0) {
    nub_size_t page_offset = (addr % page_size);
    nub_size_t bytes_left_in_page = page_size - page_offset;
    if (count > bytes_left_in_page)
      count = bytes_left_in_page;
  }
  return count;
}

#define MAX_STACK_ALLOC_DISPOSITIONS                                           \
  (16 * 1024 / sizeof(int)) // 16K of allocations

std::vector<nub_addr_t> get_dirty_pages(task_t task, mach_vm_address_t addr,
                                        mach_vm_size_t size) {
  std::vector<nub_addr_t> dirty_pages;

  int pages_to_query = size / vm_page_size;
  // Don't try to fetch too many pages' dispositions in a single call or we
  // could blow our stack out.
  mach_vm_size_t dispositions_size =
      std::min(pages_to_query, (int)MAX_STACK_ALLOC_DISPOSITIONS);
  int dispositions[dispositions_size];

  mach_vm_size_t chunk_count =
      ((pages_to_query + MAX_STACK_ALLOC_DISPOSITIONS - 1) /
       MAX_STACK_ALLOC_DISPOSITIONS);

  for (mach_vm_size_t cur_disposition_chunk = 0;
       cur_disposition_chunk < chunk_count; cur_disposition_chunk++) {
    mach_vm_size_t dispositions_already_queried =
        cur_disposition_chunk * MAX_STACK_ALLOC_DISPOSITIONS;

    mach_vm_size_t chunk_pages_to_query = std::min(
        pages_to_query - dispositions_already_queried, dispositions_size);
    mach_vm_address_t chunk_page_aligned_start_addr =
        addr + (dispositions_already_queried * vm_page_size);

    kern_return_t kr = mach_vm_page_range_query(
        task, chunk_page_aligned_start_addr,
        chunk_pages_to_query * vm_page_size, (mach_vm_address_t)dispositions,
        &chunk_pages_to_query);
    if (kr != KERN_SUCCESS)
      return dirty_pages;
    for (mach_vm_size_t i = 0; i < chunk_pages_to_query; i++) {
      uint64_t dirty_addr = chunk_page_aligned_start_addr + (i * vm_page_size);
      if (dispositions[i] & VM_PAGE_QUERY_PAGE_DIRTY)
        dirty_pages.push_back(dirty_addr);
    }
  }
  return dirty_pages;
}

nub_bool_t MachVMMemory::GetMemoryRegionInfo(task_t task, nub_addr_t address,
                                             DNBRegionInfo *region_info) {
  MachVMRegion vmRegion(task);

  if (vmRegion.GetRegionForAddress(address)) {
    region_info->addr = vmRegion.StartAddress();
    region_info->size = vmRegion.GetByteSize();
    region_info->permissions = vmRegion.GetDNBPermissions();
    region_info->dirty_pages =
        get_dirty_pages(task, vmRegion.StartAddress(), vmRegion.GetByteSize());
    region_info->vm_types = vmRegion.GetMemoryTypes();
  } else {
    region_info->addr = address;
    region_info->size = 0;
    if (vmRegion.GetError().Success()) {
      // vmRegion.GetRegionForAddress() return false, indicating that "address"
      // wasn't in a valid region, but the "vmRegion" info was successfully
      // read from the task which means the info describes the next valid
      // region from which we can infer the size of this invalid region
      mach_vm_address_t start_addr = vmRegion.StartAddress();
      if (address < start_addr)
        region_info->size = start_addr - address;
    }
    // If we can't get any info about the size from the next region it means
    // we asked about an address that was past all mappings, so the size
    // of this region will take up all remaining address space.
    if (region_info->size == 0)
      region_info->size = INVALID_NUB_ADDRESS - region_info->addr;

    // Not readable, writeable or executable
    region_info->permissions = 0;
  }
  return true;
}

static uint64_t GetPhysicalMemory() {
  // This doesn't change often at all. No need to poll each time.
  static uint64_t physical_memory = 0;
  static bool calculated = false;
  if (calculated)
    return physical_memory;

  size_t len = sizeof(physical_memory);
  sysctlbyname("hw.memsize", &physical_memory, &len, NULL, 0);

  calculated = true;
  return physical_memory;
}

nub_bool_t MachVMMemory::GetMemoryProfile(
    DNBProfileDataScanType scanType, task_t task, struct task_basic_info ti,
    cpu_type_t cputype, nub_process_t pid, vm_statistics64_data_t &vminfo,
    uint64_t &physical_memory, uint64_t &anonymous,
    uint64_t &phys_footprint, uint64_t &memory_cap)
{
  if (scanType & eProfileHostMemory)
    physical_memory = GetPhysicalMemory();

  if (scanType & eProfileMemory) {
    static mach_port_t localHost = mach_host_self();
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    host_statistics64(localHost, HOST_VM_INFO64, (host_info64_t)&vminfo,
                      &count);
    
    kern_return_t kr;
    mach_msg_type_number_t info_count;
    task_vm_info_data_t vm_info;

    info_count = TASK_VM_INFO_COUNT;
    kr = task_info(task, TASK_VM_INFO_PURGEABLE, (task_info_t)&vm_info, &info_count);
    if (kr == KERN_SUCCESS) {
      if (scanType & eProfileMemoryAnonymous) {
        anonymous = vm_info.internal + vm_info.compressed - vm_info.purgeable_volatile_pmap;
      }

      phys_footprint = vm_info.phys_footprint;
    }
  }

#if defined(WITH_FBS) || defined(WITH_BKS)
  if (scanType & eProfileMemoryCap) {
    memorystatus_memlimit_properties_t memlimit_properties;
    memset(&memlimit_properties, 0, sizeof(memlimit_properties));
    if (memorystatus_control(MEMORYSTATUS_CMD_GET_MEMLIMIT_PROPERTIES, pid, 0, &memlimit_properties, sizeof(memlimit_properties)) == 0) {
        memory_cap = memlimit_properties.memlimit_active;
    }
  }
#endif

  return true;
}

nub_size_t MachVMMemory::Read(task_t task, nub_addr_t address, void *data,
                              nub_size_t data_count) {
  if (data == NULL || data_count == 0)
    return 0;

  nub_size_t total_bytes_read = 0;
  nub_addr_t curr_addr = address;
  uint8_t *curr_data = (uint8_t *)data;
  while (total_bytes_read < data_count) {
    mach_vm_size_t curr_size =
        MaxBytesLeftInPage(task, curr_addr, data_count - total_bytes_read);
    mach_msg_type_number_t curr_bytes_read = 0;
    vm_offset_t vm_memory = 0;
    m_err = ::mach_vm_read(task, curr_addr, curr_size, &vm_memory,
                           &curr_bytes_read);

    if (DNBLogCheckLogBit(LOG_MEMORY))
      m_err.LogThreaded("::mach_vm_read ( task = 0x%4.4x, addr = 0x%8.8llx, "
                        "size = %llu, data => %8.8p, dataCnt => %i )",
                        task, (uint64_t)curr_addr, (uint64_t)curr_size,
                        vm_memory, curr_bytes_read);

    if (m_err.Success()) {
      if (curr_bytes_read != curr_size) {
        if (DNBLogCheckLogBit(LOG_MEMORY))
          m_err.LogThreaded(
              "::mach_vm_read ( task = 0x%4.4x, addr = 0x%8.8llx, size = %llu, "
              "data => %8.8p, dataCnt=>%i ) only read %u of %llu bytes",
              task, (uint64_t)curr_addr, (uint64_t)curr_size, vm_memory,
              curr_bytes_read, curr_bytes_read, (uint64_t)curr_size);
      }
      ::memcpy(curr_data, (void *)vm_memory, curr_bytes_read);
      ::vm_deallocate(mach_task_self(), vm_memory, curr_bytes_read);
      total_bytes_read += curr_bytes_read;
      curr_addr += curr_bytes_read;
      curr_data += curr_bytes_read;
    } else {
      break;
    }
  }
  return total_bytes_read;
}

nub_size_t MachVMMemory::Write(task_t task, nub_addr_t address,
                               const void *data, nub_size_t data_count) {
  MachVMRegion vmRegion(task);

  nub_size_t total_bytes_written = 0;
  nub_addr_t curr_addr = address;
  const uint8_t *curr_data = (const uint8_t *)data;

  while (total_bytes_written < data_count) {
    if (vmRegion.GetRegionForAddress(curr_addr)) {
      mach_vm_size_t curr_data_count = data_count - total_bytes_written;
      mach_vm_size_t region_bytes_left = vmRegion.BytesRemaining(curr_addr);
      if (region_bytes_left == 0) {
        break;
      }
      if (curr_data_count > region_bytes_left)
        curr_data_count = region_bytes_left;

      if (vmRegion.SetProtections(curr_addr, curr_data_count,
                                  VM_PROT_READ | VM_PROT_WRITE)) {
        nub_size_t bytes_written =
            WriteRegion(task, curr_addr, curr_data, curr_data_count);
        if (bytes_written <= 0) {
          // Status should have already be posted by WriteRegion...
          break;
        } else {
          total_bytes_written += bytes_written;
          curr_addr += bytes_written;
          curr_data += bytes_written;
        }
      } else {
        DNBLogThreadedIf(
            LOG_MEMORY_PROTECTIONS, "Failed to set read/write protections on "
                                    "region for address: [0x%8.8llx-0x%8.8llx)",
            (uint64_t)curr_addr, (uint64_t)(curr_addr + curr_data_count));
        break;
      }
    } else {
      DNBLogThreadedIf(LOG_MEMORY_PROTECTIONS,
                       "Failed to get region for address: 0x%8.8llx",
                       (uint64_t)address);
      break;
    }
  }

  return total_bytes_written;
}

nub_size_t MachVMMemory::WriteRegion(task_t task, const nub_addr_t address,
                                     const void *data,
                                     const nub_size_t data_count) {
  if (data == NULL || data_count == 0)
    return 0;

  nub_size_t total_bytes_written = 0;
  nub_addr_t curr_addr = address;
  const uint8_t *curr_data = (const uint8_t *)data;
  while (total_bytes_written < data_count) {
    mach_msg_type_number_t curr_data_count =
        static_cast<mach_msg_type_number_t>(MaxBytesLeftInPage(
            task, curr_addr, data_count - total_bytes_written));
    m_err =
        ::mach_vm_write(task, curr_addr, (pointer_t)curr_data, curr_data_count);
    if (DNBLogCheckLogBit(LOG_MEMORY) || m_err.Fail())
      m_err.LogThreaded("::mach_vm_write ( task = 0x%4.4x, addr = 0x%8.8llx, "
                        "data = %8.8p, dataCnt = %u )",
                        task, (uint64_t)curr_addr, curr_data, curr_data_count);

#if !defined(__i386__) && !defined(__x86_64__)
    vm_machine_attribute_val_t mattr_value = MATTR_VAL_CACHE_FLUSH;

    m_err = ::vm_machine_attribute(task, curr_addr, curr_data_count,
                                   MATTR_CACHE, &mattr_value);
    if (DNBLogCheckLogBit(LOG_MEMORY) || m_err.Fail())
      m_err.LogThreaded("::vm_machine_attribute ( task = 0x%4.4x, addr = "
                        "0x%8.8llx, size = %u, attr = MATTR_CACHE, mattr_value "
                        "=> MATTR_VAL_CACHE_FLUSH )",
                        task, (uint64_t)curr_addr, curr_data_count);
#endif

    if (m_err.Success()) {
      total_bytes_written += curr_data_count;
      curr_addr += curr_data_count;
      curr_data += curr_data_count;
    } else {
      break;
    }
  }
  return total_bytes_written;
}

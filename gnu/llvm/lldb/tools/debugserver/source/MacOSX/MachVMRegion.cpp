//===-- MachVMRegion.cpp ----------------------------------------*- C++ -*-===//
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

#include "MachVMRegion.h"
#include "DNBLog.h"
#include <cassert>
#include <mach/mach_vm.h>

MachVMRegion::MachVMRegion(task_t task)
    : m_task(task), m_addr(INVALID_NUB_ADDRESS), m_err(),
      m_start(INVALID_NUB_ADDRESS), m_size(0), m_depth(-1),
      m_curr_protection(0), m_protection_addr(INVALID_NUB_ADDRESS),
      m_protection_size(0) {
  memset(&m_data, 0, sizeof(m_data));
}

MachVMRegion::~MachVMRegion() {
  // Restore any original protections and clear our vars
  Clear();
}

void MachVMRegion::Clear() {
  RestoreProtections();
  m_addr = INVALID_NUB_ADDRESS;
  m_err.Clear();
  m_start = INVALID_NUB_ADDRESS;
  m_size = 0;
  m_depth = -1;
  memset(&m_data, 0, sizeof(m_data));
  m_curr_protection = 0;
  m_protection_addr = INVALID_NUB_ADDRESS;
  m_protection_size = 0;
}

bool MachVMRegion::SetProtections(mach_vm_address_t addr, mach_vm_size_t size,
                                  vm_prot_t prot) {
  if (ContainsAddress(addr)) {
    mach_vm_size_t prot_size = size;
    mach_vm_address_t end_addr = EndAddress();
    if (prot_size > (end_addr - addr))
      prot_size = end_addr - addr;

    if (prot_size > 0) {
      if (prot == (m_curr_protection & VM_PROT_ALL)) {
        DNBLogThreadedIf(LOG_MEMORY_PROTECTIONS | LOG_VERBOSE,
                         "MachVMRegion::%s: protections (%u) already "
                         "sufficient for task 0x%4.4x at address 0x%8.8llx) ",
                         __FUNCTION__, prot, m_task, (uint64_t)addr);
        // Protections are already set as requested...
        return true;
      } else {
        m_err = ::mach_vm_protect(m_task, addr, prot_size, 0, prot);
        if (DNBLogCheckLogBit(LOG_MEMORY_PROTECTIONS))
          m_err.LogThreaded("::mach_vm_protect ( task = 0x%4.4x, addr = "
                            "0x%8.8llx, size = %llu, set_max = %i, prot = %u )",
                            m_task, (uint64_t)addr, (uint64_t)prot_size, 0,
                            prot);
        if (m_err.Fail()) {
          // Try again with the ability to create a copy on write region
          m_err = ::mach_vm_protect(m_task, addr, prot_size, 0,
                                    prot | VM_PROT_COPY);
          if (DNBLogCheckLogBit(LOG_MEMORY_PROTECTIONS) || m_err.Fail())
            m_err.LogThreaded("::mach_vm_protect ( task = 0x%4.4x, addr = "
                              "0x%8.8llx, size = %llu, set_max = %i, prot = %u "
                              ")",
                              m_task, (uint64_t)addr, (uint64_t)prot_size, 0,
                              prot | VM_PROT_COPY);
        }
        if (m_err.Success()) {
          m_curr_protection = prot;
          m_protection_addr = addr;
          m_protection_size = prot_size;
          return true;
        }
      }
    } else {
      DNBLogThreadedIf(LOG_MEMORY_PROTECTIONS | LOG_VERBOSE,
                       "%s: Zero size for task 0x%4.4x at address 0x%8.8llx) ",
                       __FUNCTION__, m_task, (uint64_t)addr);
    }
  }
  return false;
}

bool MachVMRegion::RestoreProtections() {
  if (m_curr_protection != m_data.protection && m_protection_size > 0) {
    m_err = ::mach_vm_protect(m_task, m_protection_addr, m_protection_size, 0,
                              m_data.protection);
    if (DNBLogCheckLogBit(LOG_MEMORY_PROTECTIONS) || m_err.Fail())
      m_err.LogThreaded("::mach_vm_protect ( task = 0x%4.4x, addr = 0x%8.8llx, "
                        "size = %llu, set_max = %i, prot = %u )",
                        m_task, (uint64_t)m_protection_addr,
                        (uint64_t)m_protection_size, 0, m_data.protection);
    if (m_err.Success()) {
      m_protection_size = 0;
      m_protection_addr = INVALID_NUB_ADDRESS;
      m_curr_protection = m_data.protection;
      return true;
    }
  } else {
    m_err.Clear();
    return true;
  }

  return false;
}

bool MachVMRegion::GetRegionForAddress(nub_addr_t addr) {
  // Restore any original protections and clear our vars
  Clear();
  m_err.Clear();
  m_addr = addr;
  m_start = addr;
  m_depth = 1024;
  mach_msg_type_number_t info_size = kRegionInfoSize;
  static_assert(sizeof(info_size) == 4);
  m_err =
      ::mach_vm_region_recurse(m_task, &m_start, &m_size, &m_depth,
                               (vm_region_recurse_info_t)&m_data, &info_size);

  const bool failed = m_err.Fail();
  const bool log_protections = DNBLogCheckLogBit(LOG_MEMORY_PROTECTIONS);

  if (log_protections || failed)
    m_err.LogThreaded("::mach_vm_region_recurse ( task = 0x%4.4x, address => "
                      "0x%8.8llx, size => %llu, nesting_depth => %d, info => "
                      "%p, infoCnt => %d) addr = 0x%8.8llx ",
                      m_task, (uint64_t)m_start, (uint64_t)m_size, m_depth,
                      &m_data, info_size, (uint64_t)addr);

  if (failed)
    return false;
  if (log_protections) {
    DNBLogThreaded("info = { prot = %u, "
                   "max_prot = %u, "
                   "inheritance = 0x%8.8x, "
                   "offset = 0x%8.8llx, "
                   "user_tag = 0x%8.8x, "
                   "ref_count = %u, "
                   "shadow_depth = %u, "
                   "ext_pager = %u, "
                   "share_mode = %u, "
                   "is_submap = %d, "
                   "behavior = %d, "
                   "object_id = 0x%8.8x, "
                   "user_wired_count = 0x%4.4x }",
                   m_data.protection, m_data.max_protection, m_data.inheritance,
                   (uint64_t)m_data.offset, m_data.user_tag, m_data.ref_count,
                   m_data.shadow_depth, m_data.external_pager,
                   m_data.share_mode, m_data.is_submap, m_data.behavior,
                   m_data.object_id, m_data.user_wired_count);
  }
  m_curr_protection = m_data.protection;

  // We make a request for an address and got no error back, but this
  // doesn't mean that "addr" is in the range. The data in this object will
  // be valid though, so you could see where the next region begins. So we
  // return false, yet leave "m_err" with a successfull return code.
  return !((addr < m_start) || (addr >= (m_start + m_size)));
}

uint32_t MachVMRegion::GetDNBPermissions() const {
  if (m_addr == INVALID_NUB_ADDRESS || m_start == INVALID_NUB_ADDRESS ||
      m_size == 0)
    return 0;
  uint32_t dnb_permissions = 0;

  if ((m_data.protection & VM_PROT_READ) == VM_PROT_READ)
    dnb_permissions |= eMemoryPermissionsReadable;
  if ((m_data.protection & VM_PROT_WRITE) == VM_PROT_WRITE)
    dnb_permissions |= eMemoryPermissionsWritable;
  if ((m_data.protection & VM_PROT_EXECUTE) == VM_PROT_EXECUTE)
    dnb_permissions |= eMemoryPermissionsExecutable;
  return dnb_permissions;
}

std::vector<std::string> MachVMRegion::GetMemoryTypes() const {
  std::vector<std::string> types;
  if (m_data.user_tag == VM_MEMORY_STACK) {
    if (m_data.protection == VM_PROT_NONE) {
      types.push_back("stack-guard");
    } else {
      types.push_back("stack");
    }
  }
  if (m_data.user_tag == VM_MEMORY_MALLOC) {
    if (m_data.protection == VM_PROT_NONE)
      types.push_back("malloc-guard");
    else if (m_data.share_mode == SM_EMPTY)
      types.push_back("malloc-reserved");
    else
      types.push_back("malloc-metadata");
  }
  if (m_data.user_tag == VM_MEMORY_MALLOC_NANO ||
      m_data.user_tag == VM_MEMORY_MALLOC_TINY ||
      m_data.user_tag == VM_MEMORY_MALLOC_SMALL ||
      m_data.user_tag == VM_MEMORY_MALLOC_LARGE ||
      m_data.user_tag == VM_MEMORY_MALLOC_LARGE_REUSED ||
      m_data.user_tag == VM_MEMORY_MALLOC_LARGE_REUSABLE ||
      m_data.user_tag == VM_MEMORY_MALLOC_HUGE ||
      m_data.user_tag == VM_MEMORY_REALLOC ||
      m_data.user_tag == VM_MEMORY_SBRK) {
    types.push_back("heap");
    if (m_data.user_tag == VM_MEMORY_MALLOC_TINY) {
      types.push_back("malloc-tiny");
    }
    if (m_data.user_tag == VM_MEMORY_MALLOC_LARGE) {
      types.push_back("malloc-large");
    }
    if (m_data.user_tag == VM_MEMORY_MALLOC_SMALL) {
      types.push_back("malloc-small");
    }
  }
  return types;
}

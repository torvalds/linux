//===-- IRMemoryMap.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Expression/IRMemoryMap.h"
#include "lldb/Target/MemoryRegionInfo.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/Status.h"

using namespace lldb_private;

IRMemoryMap::IRMemoryMap(lldb::TargetSP target_sp) : m_target_wp(target_sp) {
  if (target_sp)
    m_process_wp = target_sp->GetProcessSP();
}

IRMemoryMap::~IRMemoryMap() {
  lldb::ProcessSP process_sp = m_process_wp.lock();

  if (process_sp) {
    AllocationMap::iterator iter;

    Status err;

    while ((iter = m_allocations.begin()) != m_allocations.end()) {
      err.Clear();
      if (iter->second.m_leak)
        m_allocations.erase(iter);
      else
        Free(iter->first, err);
    }
  }
}

lldb::addr_t IRMemoryMap::FindSpace(size_t size) {
  // The FindSpace algorithm's job is to find a region of memory that the
  // underlying process is unlikely to be using.
  //
  // The memory returned by this function will never be written to.  The only
  // point is that it should not shadow process memory if possible, so that
  // expressions processing real values from the process do not use the wrong
  // data.
  //
  // If the process can in fact allocate memory (CanJIT() lets us know this)
  // then this can be accomplished just be allocating memory in the inferior.
  // Then no guessing is required.

  lldb::TargetSP target_sp = m_target_wp.lock();
  lldb::ProcessSP process_sp = m_process_wp.lock();

  const bool process_is_alive = process_sp && process_sp->IsAlive();

  lldb::addr_t ret = LLDB_INVALID_ADDRESS;
  if (size == 0)
    return ret;

  if (process_is_alive && process_sp->CanJIT()) {
    Status alloc_error;

    ret = process_sp->AllocateMemory(size, lldb::ePermissionsReadable |
                                               lldb::ePermissionsWritable,
                                     alloc_error);

    if (!alloc_error.Success())
      return LLDB_INVALID_ADDRESS;
    else
      return ret;
  }

  // At this point we know that we need to hunt.
  //
  // First, go to the end of the existing allocations we've made if there are
  // any allocations.  Otherwise start at the beginning of memory.

  if (m_allocations.empty()) {
    ret = 0x0;
  } else {
    auto back = m_allocations.rbegin();
    lldb::addr_t addr = back->first;
    size_t alloc_size = back->second.m_size;
    ret = llvm::alignTo(addr + alloc_size, 4096);
  }

  uint64_t end_of_memory;
  switch (GetAddressByteSize()) {
  case 2:
    end_of_memory = 0xffffull;
    break;
  case 4:
    end_of_memory = 0xffffffffull;
    break;
  case 8:
    end_of_memory = 0xffffffffffffffffull;
    break;
  default:
    lldbassert(false && "Invalid address size.");
    return LLDB_INVALID_ADDRESS;
  }

  // Now, if it's possible to use the GetMemoryRegionInfo API to detect mapped
  // regions, walk forward through memory until a region is found that has
  // adequate space for our allocation.
  if (process_is_alive) {
    MemoryRegionInfo region_info;
    Status err = process_sp->GetMemoryRegionInfo(ret, region_info);
    if (err.Success()) {
      while (true) {
        if (region_info.GetReadable() != MemoryRegionInfo::OptionalBool::eNo ||
            region_info.GetWritable() != MemoryRegionInfo::OptionalBool::eNo ||
            region_info.GetExecutable() !=
                MemoryRegionInfo::OptionalBool::eNo) {
          if (region_info.GetRange().GetRangeEnd() - 1 >= end_of_memory) {
            ret = LLDB_INVALID_ADDRESS;
            break;
          } else {
            ret = region_info.GetRange().GetRangeEnd();
          }
        } else if (ret + size < region_info.GetRange().GetRangeEnd()) {
          return ret;
        } else {
          // ret stays the same.  We just need to walk a bit further.
        }

        err = process_sp->GetMemoryRegionInfo(
            region_info.GetRange().GetRangeEnd(), region_info);
        if (err.Fail()) {
          lldbassert(0 && "GetMemoryRegionInfo() succeeded, then failed");
          ret = LLDB_INVALID_ADDRESS;
          break;
        }
      }
    }
  }

  // We've tried our algorithm, and it didn't work.  Now we have to reset back
  // to the end of the allocations we've already reported, or use a 'sensible'
  // default if this is our first allocation.
  if (m_allocations.empty()) {
    uint64_t alloc_address = target_sp->GetExprAllocAddress();
    if (alloc_address > 0) {
      if (alloc_address >= end_of_memory) {
        lldbassert(0 && "The allocation address for expression evaluation must "
                        "be within process address space");
        return LLDB_INVALID_ADDRESS;
      }
      ret = alloc_address;
    } else {
      uint32_t address_byte_size = GetAddressByteSize();
      if (address_byte_size != UINT32_MAX) {
        switch (address_byte_size) {
        case 2:
          ret = 0x8000ull;
          break;
        case 4:
          ret = 0xee000000ull;
          break;
        case 8:
          ret = 0xdead0fff00000000ull;
          break;
        default:
          lldbassert(false && "Invalid address size.");
          return LLDB_INVALID_ADDRESS;
        }
      }
    }
  } else {
    auto back = m_allocations.rbegin();
    lldb::addr_t addr = back->first;
    size_t alloc_size = back->second.m_size;
    uint64_t align = target_sp->GetExprAllocAlign();
    if (align == 0)
      align = 4096;
    ret = llvm::alignTo(addr + alloc_size, align);
  }

  return ret;
}

IRMemoryMap::AllocationMap::iterator
IRMemoryMap::FindAllocation(lldb::addr_t addr, size_t size) {
  if (addr == LLDB_INVALID_ADDRESS)
    return m_allocations.end();

  AllocationMap::iterator iter = m_allocations.lower_bound(addr);

  if (iter == m_allocations.end() || iter->first > addr) {
    if (iter == m_allocations.begin())
      return m_allocations.end();
    iter--;
  }

  if (iter->first <= addr && iter->first + iter->second.m_size >= addr + size)
    return iter;

  return m_allocations.end();
}

bool IRMemoryMap::IntersectsAllocation(lldb::addr_t addr, size_t size) const {
  if (addr == LLDB_INVALID_ADDRESS)
    return false;

  AllocationMap::const_iterator iter = m_allocations.lower_bound(addr);

  // Since we only know that the returned interval begins at a location greater
  // than or equal to where the given interval begins, it's possible that the
  // given interval intersects either the returned interval or the previous
  // interval.  Thus, we need to check both. Note that we only need to check
  // these two intervals.  Since all intervals are disjoint it is not possible
  // that an adjacent interval does not intersect, but a non-adjacent interval
  // does intersect.
  if (iter != m_allocations.end()) {
    if (AllocationsIntersect(addr, size, iter->second.m_process_start,
                             iter->second.m_size))
      return true;
  }

  if (iter != m_allocations.begin()) {
    --iter;
    if (AllocationsIntersect(addr, size, iter->second.m_process_start,
                             iter->second.m_size))
      return true;
  }

  return false;
}

bool IRMemoryMap::AllocationsIntersect(lldb::addr_t addr1, size_t size1,
                                       lldb::addr_t addr2, size_t size2) {
  // Given two half open intervals [A, B) and [X, Y), the only 6 permutations
  // that satisfy A<B and X<Y are the following:
  // A B X Y
  // A X B Y  (intersects)
  // A X Y B  (intersects)
  // X A B Y  (intersects)
  // X A Y B  (intersects)
  // X Y A B
  // The first is B <= X, and the last is Y <= A. So the condition is !(B <= X
  // || Y <= A)), or (X < B && A < Y)
  return (addr2 < (addr1 + size1)) && (addr1 < (addr2 + size2));
}

lldb::ByteOrder IRMemoryMap::GetByteOrder() {
  lldb::ProcessSP process_sp = m_process_wp.lock();

  if (process_sp)
    return process_sp->GetByteOrder();

  lldb::TargetSP target_sp = m_target_wp.lock();

  if (target_sp)
    return target_sp->GetArchitecture().GetByteOrder();

  return lldb::eByteOrderInvalid;
}

uint32_t IRMemoryMap::GetAddressByteSize() {
  lldb::ProcessSP process_sp = m_process_wp.lock();

  if (process_sp)
    return process_sp->GetAddressByteSize();

  lldb::TargetSP target_sp = m_target_wp.lock();

  if (target_sp)
    return target_sp->GetArchitecture().GetAddressByteSize();

  return UINT32_MAX;
}

ExecutionContextScope *IRMemoryMap::GetBestExecutionContextScope() const {
  lldb::ProcessSP process_sp = m_process_wp.lock();

  if (process_sp)
    return process_sp.get();

  lldb::TargetSP target_sp = m_target_wp.lock();

  if (target_sp)
    return target_sp.get();

  return nullptr;
}

IRMemoryMap::Allocation::Allocation(lldb::addr_t process_alloc,
                                    lldb::addr_t process_start, size_t size,
                                    uint32_t permissions, uint8_t alignment,
                                    AllocationPolicy policy)
    : m_process_alloc(process_alloc), m_process_start(process_start),
      m_size(size), m_policy(policy), m_leak(false), m_permissions(permissions),
      m_alignment(alignment) {
  switch (policy) {
  default:
    llvm_unreachable("Invalid AllocationPolicy");
  case eAllocationPolicyHostOnly:
  case eAllocationPolicyMirror:
    m_data.SetByteSize(size);
    break;
  case eAllocationPolicyProcessOnly:
    break;
  }
}

lldb::addr_t IRMemoryMap::Malloc(size_t size, uint8_t alignment,
                                 uint32_t permissions, AllocationPolicy policy,
                                 bool zero_memory, Status &error) {
  lldb_private::Log *log(GetLog(LLDBLog::Expressions));
  error.Clear();

  lldb::ProcessSP process_sp;
  lldb::addr_t allocation_address = LLDB_INVALID_ADDRESS;
  lldb::addr_t aligned_address = LLDB_INVALID_ADDRESS;

  size_t allocation_size;

  if (size == 0) {
    // FIXME: Malloc(0) should either return an invalid address or assert, in
    // order to cut down on unnecessary allocations.
    allocation_size = alignment;
  } else {
    // Round up the requested size to an aligned value.
    allocation_size = llvm::alignTo(size, alignment);

    // The process page cache does not see the requested alignment. We can't
    // assume its result will be any more than 1-byte aligned. To work around
    // this, request `alignment - 1` additional bytes.
    allocation_size += alignment - 1;
  }

  switch (policy) {
  default:
    error.SetErrorToGenericError();
    error.SetErrorString("Couldn't malloc: invalid allocation policy");
    return LLDB_INVALID_ADDRESS;
  case eAllocationPolicyHostOnly:
    allocation_address = FindSpace(allocation_size);
    if (allocation_address == LLDB_INVALID_ADDRESS) {
      error.SetErrorToGenericError();
      error.SetErrorString("Couldn't malloc: address space is full");
      return LLDB_INVALID_ADDRESS;
    }
    break;
  case eAllocationPolicyMirror:
    process_sp = m_process_wp.lock();
    LLDB_LOGF(log,
              "IRMemoryMap::%s process_sp=0x%" PRIxPTR
              ", process_sp->CanJIT()=%s, process_sp->IsAlive()=%s",
              __FUNCTION__, reinterpret_cast<uintptr_t>(process_sp.get()),
              process_sp && process_sp->CanJIT() ? "true" : "false",
              process_sp && process_sp->IsAlive() ? "true" : "false");
    if (process_sp && process_sp->CanJIT() && process_sp->IsAlive()) {
      if (!zero_memory)
        allocation_address =
            process_sp->AllocateMemory(allocation_size, permissions, error);
      else
        allocation_address =
            process_sp->CallocateMemory(allocation_size, permissions, error);

      if (!error.Success())
        return LLDB_INVALID_ADDRESS;
    } else {
      LLDB_LOGF(log,
                "IRMemoryMap::%s switching to eAllocationPolicyHostOnly "
                "due to failed condition (see previous expr log message)",
                __FUNCTION__);
      policy = eAllocationPolicyHostOnly;
      allocation_address = FindSpace(allocation_size);
      if (allocation_address == LLDB_INVALID_ADDRESS) {
        error.SetErrorToGenericError();
        error.SetErrorString("Couldn't malloc: address space is full");
        return LLDB_INVALID_ADDRESS;
      }
    }
    break;
  case eAllocationPolicyProcessOnly:
    process_sp = m_process_wp.lock();
    if (process_sp) {
      if (process_sp->CanJIT() && process_sp->IsAlive()) {
        if (!zero_memory)
          allocation_address =
              process_sp->AllocateMemory(allocation_size, permissions, error);
        else
          allocation_address =
              process_sp->CallocateMemory(allocation_size, permissions, error);

        if (!error.Success())
          return LLDB_INVALID_ADDRESS;
      } else {
        error.SetErrorToGenericError();
        error.SetErrorString(
            "Couldn't malloc: process doesn't support allocating memory");
        return LLDB_INVALID_ADDRESS;
      }
    } else {
      error.SetErrorToGenericError();
      error.SetErrorString("Couldn't malloc: process doesn't exist, and this "
                           "memory must be in the process");
      return LLDB_INVALID_ADDRESS;
    }
    break;
  }

  lldb::addr_t mask = alignment - 1;
  aligned_address = (allocation_address + mask) & (~mask);

  m_allocations.emplace(
      std::piecewise_construct, std::forward_as_tuple(aligned_address),
      std::forward_as_tuple(allocation_address, aligned_address,
                            allocation_size, permissions, alignment, policy));

  if (zero_memory) {
    Status write_error;
    std::vector<uint8_t> zero_buf(size, 0);
    WriteMemory(aligned_address, zero_buf.data(), size, write_error);
  }

  if (log) {
    const char *policy_string;

    switch (policy) {
    default:
      policy_string = "<invalid policy>";
      break;
    case eAllocationPolicyHostOnly:
      policy_string = "eAllocationPolicyHostOnly";
      break;
    case eAllocationPolicyProcessOnly:
      policy_string = "eAllocationPolicyProcessOnly";
      break;
    case eAllocationPolicyMirror:
      policy_string = "eAllocationPolicyMirror";
      break;
    }

    LLDB_LOGF(log,
              "IRMemoryMap::Malloc (%" PRIu64 ", 0x%" PRIx64 ", 0x%" PRIx64
              ", %s) -> 0x%" PRIx64,
              (uint64_t)allocation_size, (uint64_t)alignment,
              (uint64_t)permissions, policy_string, aligned_address);
  }

  return aligned_address;
}

void IRMemoryMap::Leak(lldb::addr_t process_address, Status &error) {
  error.Clear();

  AllocationMap::iterator iter = m_allocations.find(process_address);

  if (iter == m_allocations.end()) {
    error.SetErrorToGenericError();
    error.SetErrorString("Couldn't leak: allocation doesn't exist");
    return;
  }

  Allocation &allocation = iter->second;

  allocation.m_leak = true;
}

void IRMemoryMap::Free(lldb::addr_t process_address, Status &error) {
  error.Clear();

  AllocationMap::iterator iter = m_allocations.find(process_address);

  if (iter == m_allocations.end()) {
    error.SetErrorToGenericError();
    error.SetErrorString("Couldn't free: allocation doesn't exist");
    return;
  }

  Allocation &allocation = iter->second;

  switch (allocation.m_policy) {
  default:
  case eAllocationPolicyHostOnly: {
    lldb::ProcessSP process_sp = m_process_wp.lock();
    if (process_sp) {
      if (process_sp->CanJIT() && process_sp->IsAlive())
        process_sp->DeallocateMemory(
            allocation.m_process_alloc); // FindSpace allocated this for real
    }

    break;
  }
  case eAllocationPolicyMirror:
  case eAllocationPolicyProcessOnly: {
    lldb::ProcessSP process_sp = m_process_wp.lock();
    if (process_sp)
      process_sp->DeallocateMemory(allocation.m_process_alloc);
  }
  }

  if (lldb_private::Log *log = GetLog(LLDBLog::Expressions)) {
    LLDB_LOGF(log,
              "IRMemoryMap::Free (0x%" PRIx64 ") freed [0x%" PRIx64
              "..0x%" PRIx64 ")",
              (uint64_t)process_address, iter->second.m_process_start,
              iter->second.m_process_start + iter->second.m_size);
  }

  m_allocations.erase(iter);
}

bool IRMemoryMap::GetAllocSize(lldb::addr_t address, size_t &size) {
  AllocationMap::iterator iter = FindAllocation(address, size);
  if (iter == m_allocations.end())
    return false;

  Allocation &al = iter->second;

  if (address > (al.m_process_start + al.m_size)) {
    size = 0;
    return false;
  }

  if (address > al.m_process_start) {
    int dif = address - al.m_process_start;
    size = al.m_size - dif;
    return true;
  }

  size = al.m_size;
  return true;
}

void IRMemoryMap::WriteMemory(lldb::addr_t process_address,
                              const uint8_t *bytes, size_t size,
                              Status &error) {
  error.Clear();

  AllocationMap::iterator iter = FindAllocation(process_address, size);

  if (iter == m_allocations.end()) {
    lldb::ProcessSP process_sp = m_process_wp.lock();

    if (process_sp) {
      process_sp->WriteMemory(process_address, bytes, size, error);
      return;
    }

    error.SetErrorToGenericError();
    error.SetErrorString("Couldn't write: no allocation contains the target "
                         "range and the process doesn't exist");
    return;
  }

  Allocation &allocation = iter->second;

  uint64_t offset = process_address - allocation.m_process_start;

  lldb::ProcessSP process_sp;

  switch (allocation.m_policy) {
  default:
    error.SetErrorToGenericError();
    error.SetErrorString("Couldn't write: invalid allocation policy");
    return;
  case eAllocationPolicyHostOnly:
    if (!allocation.m_data.GetByteSize()) {
      error.SetErrorToGenericError();
      error.SetErrorString("Couldn't write: data buffer is empty");
      return;
    }
    ::memcpy(allocation.m_data.GetBytes() + offset, bytes, size);
    break;
  case eAllocationPolicyMirror:
    if (!allocation.m_data.GetByteSize()) {
      error.SetErrorToGenericError();
      error.SetErrorString("Couldn't write: data buffer is empty");
      return;
    }
    ::memcpy(allocation.m_data.GetBytes() + offset, bytes, size);
    process_sp = m_process_wp.lock();
    if (process_sp) {
      process_sp->WriteMemory(process_address, bytes, size, error);
      if (!error.Success())
        return;
    }
    break;
  case eAllocationPolicyProcessOnly:
    process_sp = m_process_wp.lock();
    if (process_sp) {
      process_sp->WriteMemory(process_address, bytes, size, error);
      if (!error.Success())
        return;
    }
    break;
  }

  if (lldb_private::Log *log = GetLog(LLDBLog::Expressions)) {
    LLDB_LOGF(log,
              "IRMemoryMap::WriteMemory (0x%" PRIx64 ", 0x%" PRIxPTR
              ", 0x%" PRId64 ") went to [0x%" PRIx64 "..0x%" PRIx64 ")",
              (uint64_t)process_address, reinterpret_cast<uintptr_t>(bytes), (uint64_t)size,
              (uint64_t)allocation.m_process_start,
              (uint64_t)allocation.m_process_start +
                  (uint64_t)allocation.m_size);
  }
}

void IRMemoryMap::WriteScalarToMemory(lldb::addr_t process_address,
                                      Scalar &scalar, size_t size,
                                      Status &error) {
  error.Clear();

  if (size == UINT32_MAX)
    size = scalar.GetByteSize();

  if (size > 0) {
    uint8_t buf[32];
    const size_t mem_size =
        scalar.GetAsMemoryData(buf, size, GetByteOrder(), error);
    if (mem_size > 0) {
      return WriteMemory(process_address, buf, mem_size, error);
    } else {
      error.SetErrorToGenericError();
      error.SetErrorString(
          "Couldn't write scalar: failed to get scalar as memory data");
    }
  } else {
    error.SetErrorToGenericError();
    error.SetErrorString("Couldn't write scalar: its size was zero");
  }
}

void IRMemoryMap::WritePointerToMemory(lldb::addr_t process_address,
                                       lldb::addr_t address, Status &error) {
  error.Clear();

  Scalar scalar(address);

  WriteScalarToMemory(process_address, scalar, GetAddressByteSize(), error);
}

void IRMemoryMap::ReadMemory(uint8_t *bytes, lldb::addr_t process_address,
                             size_t size, Status &error) {
  error.Clear();

  AllocationMap::iterator iter = FindAllocation(process_address, size);

  if (iter == m_allocations.end()) {
    lldb::ProcessSP process_sp = m_process_wp.lock();

    if (process_sp) {
      process_sp->ReadMemory(process_address, bytes, size, error);
      return;
    }

    lldb::TargetSP target_sp = m_target_wp.lock();

    if (target_sp) {
      Address absolute_address(process_address);
      target_sp->ReadMemory(absolute_address, bytes, size, error, true);
      return;
    }

    error.SetErrorToGenericError();
    error.SetErrorString("Couldn't read: no allocation contains the target "
                         "range, and neither the process nor the target exist");
    return;
  }

  Allocation &allocation = iter->second;

  uint64_t offset = process_address - allocation.m_process_start;

  if (offset > allocation.m_size) {
    error.SetErrorToGenericError();
    error.SetErrorString("Couldn't read: data is not in the allocation");
    return;
  }

  lldb::ProcessSP process_sp;

  switch (allocation.m_policy) {
  default:
    error.SetErrorToGenericError();
    error.SetErrorString("Couldn't read: invalid allocation policy");
    return;
  case eAllocationPolicyHostOnly:
    if (!allocation.m_data.GetByteSize()) {
      error.SetErrorToGenericError();
      error.SetErrorString("Couldn't read: data buffer is empty");
      return;
    }
    if (allocation.m_data.GetByteSize() < offset + size) {
      error.SetErrorToGenericError();
      error.SetErrorString("Couldn't read: not enough underlying data");
      return;
    }

    ::memcpy(bytes, allocation.m_data.GetBytes() + offset, size);
    break;
  case eAllocationPolicyMirror:
    process_sp = m_process_wp.lock();
    if (process_sp) {
      process_sp->ReadMemory(process_address, bytes, size, error);
      if (!error.Success())
        return;
    } else {
      if (!allocation.m_data.GetByteSize()) {
        error.SetErrorToGenericError();
        error.SetErrorString("Couldn't read: data buffer is empty");
        return;
      }
      ::memcpy(bytes, allocation.m_data.GetBytes() + offset, size);
    }
    break;
  case eAllocationPolicyProcessOnly:
    process_sp = m_process_wp.lock();
    if (process_sp) {
      process_sp->ReadMemory(process_address, bytes, size, error);
      if (!error.Success())
        return;
    }
    break;
  }

  if (lldb_private::Log *log = GetLog(LLDBLog::Expressions)) {
    LLDB_LOGF(log,
              "IRMemoryMap::ReadMemory (0x%" PRIx64 ", 0x%" PRIxPTR
              ", 0x%" PRId64 ") came from [0x%" PRIx64 "..0x%" PRIx64 ")",
              (uint64_t)process_address, reinterpret_cast<uintptr_t>(bytes), (uint64_t)size,
              (uint64_t)allocation.m_process_start,
              (uint64_t)allocation.m_process_start +
                  (uint64_t)allocation.m_size);
  }
}

void IRMemoryMap::ReadScalarFromMemory(Scalar &scalar,
                                       lldb::addr_t process_address,
                                       size_t size, Status &error) {
  error.Clear();

  if (size > 0) {
    DataBufferHeap buf(size, 0);
    ReadMemory(buf.GetBytes(), process_address, size, error);

    if (!error.Success())
      return;

    DataExtractor extractor(buf.GetBytes(), buf.GetByteSize(), GetByteOrder(),
                            GetAddressByteSize());

    lldb::offset_t offset = 0;

    switch (size) {
    default:
      error.SetErrorToGenericError();
      error.SetErrorStringWithFormat(
          "Couldn't read scalar: unsupported size %" PRIu64, (uint64_t)size);
      return;
    case 1:
      scalar = extractor.GetU8(&offset);
      break;
    case 2:
      scalar = extractor.GetU16(&offset);
      break;
    case 4:
      scalar = extractor.GetU32(&offset);
      break;
    case 8:
      scalar = extractor.GetU64(&offset);
      break;
    }
  } else {
    error.SetErrorToGenericError();
    error.SetErrorString("Couldn't read scalar: its size was zero");
  }
}

void IRMemoryMap::ReadPointerFromMemory(lldb::addr_t *address,
                                        lldb::addr_t process_address,
                                        Status &error) {
  error.Clear();

  Scalar pointer_scalar;
  ReadScalarFromMemory(pointer_scalar, process_address, GetAddressByteSize(),
                       error);

  if (!error.Success())
    return;

  *address = pointer_scalar.ULongLong();
}

void IRMemoryMap::GetMemoryData(DataExtractor &extractor,
                                lldb::addr_t process_address, size_t size,
                                Status &error) {
  error.Clear();

  if (size > 0) {
    AllocationMap::iterator iter = FindAllocation(process_address, size);

    if (iter == m_allocations.end()) {
      error.SetErrorToGenericError();
      error.SetErrorStringWithFormat(
          "Couldn't find an allocation containing [0x%" PRIx64 "..0x%" PRIx64
          ")",
          process_address, process_address + size);
      return;
    }

    Allocation &allocation = iter->second;

    switch (allocation.m_policy) {
    default:
      error.SetErrorToGenericError();
      error.SetErrorString(
          "Couldn't get memory data: invalid allocation policy");
      return;
    case eAllocationPolicyProcessOnly:
      error.SetErrorToGenericError();
      error.SetErrorString(
          "Couldn't get memory data: memory is only in the target");
      return;
    case eAllocationPolicyMirror: {
      lldb::ProcessSP process_sp = m_process_wp.lock();

      if (!allocation.m_data.GetByteSize()) {
        error.SetErrorToGenericError();
        error.SetErrorString("Couldn't get memory data: data buffer is empty");
        return;
      }
      if (process_sp) {
        process_sp->ReadMemory(allocation.m_process_start,
                               allocation.m_data.GetBytes(),
                               allocation.m_data.GetByteSize(), error);
        if (!error.Success())
          return;
        uint64_t offset = process_address - allocation.m_process_start;
        extractor = DataExtractor(allocation.m_data.GetBytes() + offset, size,
                                  GetByteOrder(), GetAddressByteSize());
        return;
      }
    } break;
    case eAllocationPolicyHostOnly:
      if (!allocation.m_data.GetByteSize()) {
        error.SetErrorToGenericError();
        error.SetErrorString("Couldn't get memory data: data buffer is empty");
        return;
      }
      uint64_t offset = process_address - allocation.m_process_start;
      extractor = DataExtractor(allocation.m_data.GetBytes() + offset, size,
                                GetByteOrder(), GetAddressByteSize());
      return;
    }
  } else {
    error.SetErrorToGenericError();
    error.SetErrorString("Couldn't get memory data: its size was zero");
    return;
  }
}

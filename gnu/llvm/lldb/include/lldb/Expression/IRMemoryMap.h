//===-- IRMemoryMap.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_EXPRESSION_IRMEMORYMAP_H
#define LLDB_EXPRESSION_IRMEMORYMAP_H

#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/UserID.h"
#include "lldb/lldb-public.h"

#include <map>

namespace lldb_private {

/// \class IRMemoryMap IRMemoryMap.h "lldb/Expression/IRMemoryMap.h"
/// Encapsulates memory that may exist in the process but must
///     also be available in the host process.
///
/// This class encapsulates a group of memory objects that must be readable or
/// writable from the host process regardless of whether the process exists.
/// This allows the IR interpreter as well as JITted code to access the same
/// memory.  All allocations made by this class are represented as disjoint
/// intervals.
///
/// Point queries against this group of memory objects can be made by the
/// address in the tar at which they reside.  If the inferior does not exist,
/// allocations still get made-up addresses.  If an inferior appears at some
/// point, then those addresses need to be re-mapped.
class IRMemoryMap {
public:
  IRMemoryMap(lldb::TargetSP target_sp);
  ~IRMemoryMap();

  enum AllocationPolicy : uint8_t {
    eAllocationPolicyInvalid =
        0, ///< It is an error for an allocation to have this policy.
    eAllocationPolicyHostOnly, ///< This allocation was created in the host and
                               ///will never make it into the process.
    ///< It is an error to create other types of allocations while such
    ///allocations exist.
    eAllocationPolicyMirror, ///< The intent is that this allocation exist both
                             ///in the host and the process and have
                             ///< the same content in both.
    eAllocationPolicyProcessOnly ///< The intent is that this allocation exist
                                 ///only in the process.
  };

  lldb::addr_t Malloc(size_t size, uint8_t alignment, uint32_t permissions,
                      AllocationPolicy policy, bool zero_memory, Status &error);
  void Leak(lldb::addr_t process_address, Status &error);
  void Free(lldb::addr_t process_address, Status &error);

  void WriteMemory(lldb::addr_t process_address, const uint8_t *bytes,
                   size_t size, Status &error);
  void WriteScalarToMemory(lldb::addr_t process_address, Scalar &scalar,
                           size_t size, Status &error);
  void WritePointerToMemory(lldb::addr_t process_address, lldb::addr_t address,
                            Status &error);
  void ReadMemory(uint8_t *bytes, lldb::addr_t process_address, size_t size,
                  Status &error);
  void ReadScalarFromMemory(Scalar &scalar, lldb::addr_t process_address,
                            size_t size, Status &error);
  void ReadPointerFromMemory(lldb::addr_t *address,
                             lldb::addr_t process_address, Status &error);
  bool GetAllocSize(lldb::addr_t address, size_t &size);
  void GetMemoryData(DataExtractor &extractor, lldb::addr_t process_address,
                     size_t size, Status &error);

  lldb::ByteOrder GetByteOrder();
  uint32_t GetAddressByteSize();

  // This function can return NULL.
  ExecutionContextScope *GetBestExecutionContextScope() const;

  lldb::TargetSP GetTarget() { return m_target_wp.lock(); }

protected:
  // This function should only be used if you know you are using the JIT. Any
  // other cases should use GetBestExecutionContextScope().

  lldb::ProcessWP &GetProcessWP() { return m_process_wp; }

private:
  struct Allocation {
    lldb::addr_t
        m_process_alloc; ///< The (unaligned) base for the remote allocation.
    lldb::addr_t
        m_process_start; ///< The base address of the allocation in the process.
    size_t m_size;       ///< The size of the requested allocation.
    DataBufferHeap m_data;

    /// Flags. Keep these grouped together to avoid structure padding.
    AllocationPolicy m_policy;
    bool m_leak;
    uint8_t m_permissions; ///< The access permissions on the memory in the
                           /// process. In the host, the memory is always
                           /// read/write.
    uint8_t m_alignment;   ///< The alignment of the requested allocation.

  public:
    Allocation(lldb::addr_t process_alloc, lldb::addr_t process_start,
               size_t size, uint32_t permissions, uint8_t alignment,
               AllocationPolicy m_policy);

    Allocation(const Allocation &) = delete;
    const Allocation &operator=(const Allocation &) = delete;
  };

  static_assert(sizeof(Allocation) <=
                    (4 * sizeof(lldb::addr_t)) + sizeof(DataBufferHeap),
                "IRMemoryMap::Allocation is larger than expected");

  lldb::ProcessWP m_process_wp;
  lldb::TargetWP m_target_wp;
  typedef std::map<lldb::addr_t, Allocation> AllocationMap;
  AllocationMap m_allocations;

  lldb::addr_t FindSpace(size_t size);
  bool ContainsHostOnlyAllocations();
  AllocationMap::iterator FindAllocation(lldb::addr_t addr, size_t size);

  // Returns true if the given allocation intersects any allocation in the
  // memory map.
  bool IntersectsAllocation(lldb::addr_t addr, size_t size) const;

  // Returns true if the two given allocations intersect each other.
  static bool AllocationsIntersect(lldb::addr_t addr1, size_t size1,
                                   lldb::addr_t addr2, size_t size2);
};
}

#endif

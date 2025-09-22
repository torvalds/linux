//===- SectionMemoryManager.cpp - Memory manager for MCJIT/RtDyld *- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the section-based memory manager used by the MCJIT
// execution engine and RuntimeDyld
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/Config/config.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Process.h"

namespace llvm {

uint8_t *SectionMemoryManager::allocateDataSection(uintptr_t Size,
                                                   unsigned Alignment,
                                                   unsigned SectionID,
                                                   StringRef SectionName,
                                                   bool IsReadOnly) {
  if (IsReadOnly)
    return allocateSection(SectionMemoryManager::AllocationPurpose::ROData,
                           Size, Alignment);
  return allocateSection(SectionMemoryManager::AllocationPurpose::RWData, Size,
                         Alignment);
}

uint8_t *SectionMemoryManager::allocateCodeSection(uintptr_t Size,
                                                   unsigned Alignment,
                                                   unsigned SectionID,
                                                   StringRef SectionName) {
  return allocateSection(SectionMemoryManager::AllocationPurpose::Code, Size,
                         Alignment);
}

uint8_t *SectionMemoryManager::allocateSection(
    SectionMemoryManager::AllocationPurpose Purpose, uintptr_t Size,
    unsigned Alignment) {
  if (!Alignment)
    Alignment = 16;

  assert(!(Alignment & (Alignment - 1)) && "Alignment must be a power of two.");

  uintptr_t RequiredSize = Alignment * ((Size + Alignment - 1) / Alignment + 1);
  uintptr_t Addr = 0;

  MemoryGroup &MemGroup = [&]() -> MemoryGroup & {
    switch (Purpose) {
    case AllocationPurpose::Code:
      return CodeMem;
    case AllocationPurpose::ROData:
      return RODataMem;
    case AllocationPurpose::RWData:
      return RWDataMem;
    }
    llvm_unreachable("Unknown SectionMemoryManager::AllocationPurpose");
  }();

  // Look in the list of free memory regions and use a block there if one
  // is available.
  for (FreeMemBlock &FreeMB : MemGroup.FreeMem) {
    if (FreeMB.Free.allocatedSize() >= RequiredSize) {
      Addr = (uintptr_t)FreeMB.Free.base();
      uintptr_t EndOfBlock = Addr + FreeMB.Free.allocatedSize();
      // Align the address.
      Addr = (Addr + Alignment - 1) & ~(uintptr_t)(Alignment - 1);

      if (FreeMB.PendingPrefixIndex == (unsigned)-1) {
        // The part of the block we're giving out to the user is now pending
        MemGroup.PendingMem.push_back(sys::MemoryBlock((void *)Addr, Size));

        // Remember this pending block, such that future allocations can just
        // modify it rather than creating a new one
        FreeMB.PendingPrefixIndex = MemGroup.PendingMem.size() - 1;
      } else {
        sys::MemoryBlock &PendingMB =
            MemGroup.PendingMem[FreeMB.PendingPrefixIndex];
        PendingMB = sys::MemoryBlock(PendingMB.base(),
                                     Addr + Size - (uintptr_t)PendingMB.base());
      }

      // Remember how much free space is now left in this block
      FreeMB.Free =
          sys::MemoryBlock((void *)(Addr + Size), EndOfBlock - Addr - Size);
      return (uint8_t *)Addr;
    }
  }

  // No pre-allocated free block was large enough. Allocate a new memory region.
  // Note that all sections get allocated as read-write.  The permissions will
  // be updated later based on memory group.
  //
  // FIXME: It would be useful to define a default allocation size (or add
  // it as a constructor parameter) to minimize the number of allocations.
  //
  // FIXME: Initialize the Near member for each memory group to avoid
  // interleaving.
  std::error_code ec;
  sys::MemoryBlock MB = MMapper->allocateMappedMemory(
      Purpose, RequiredSize, &MemGroup.Near,
      sys::Memory::MF_READ | sys::Memory::MF_WRITE, ec);
  if (ec) {
    // FIXME: Add error propagation to the interface.
    return nullptr;
  }

  // Save this address as the basis for our next request
  MemGroup.Near = MB;

  // Copy the address to all the other groups, if they have not
  // been initialized.
  if (CodeMem.Near.base() == nullptr)
    CodeMem.Near = MB;
  if (RODataMem.Near.base() == nullptr)
    RODataMem.Near = MB;
  if (RWDataMem.Near.base() == nullptr)
    RWDataMem.Near = MB;

  // Remember that we allocated this memory
  MemGroup.AllocatedMem.push_back(MB);
  Addr = (uintptr_t)MB.base();
  uintptr_t EndOfBlock = Addr + MB.allocatedSize();

  // Align the address.
  Addr = (Addr + Alignment - 1) & ~(uintptr_t)(Alignment - 1);

  // The part of the block we're giving out to the user is now pending
  MemGroup.PendingMem.push_back(sys::MemoryBlock((void *)Addr, Size));

  // The allocateMappedMemory may allocate much more memory than we need. In
  // this case, we store the unused memory as a free memory block.
  unsigned FreeSize = EndOfBlock - Addr - Size;
  if (FreeSize > 16) {
    FreeMemBlock FreeMB;
    FreeMB.Free = sys::MemoryBlock((void *)(Addr + Size), FreeSize);
    FreeMB.PendingPrefixIndex = (unsigned)-1;
    MemGroup.FreeMem.push_back(FreeMB);
  }

  // Return aligned address
  return (uint8_t *)Addr;
}

bool SectionMemoryManager::finalizeMemory(std::string *ErrMsg) {
  // FIXME: Should in-progress permissions be reverted if an error occurs?
  std::error_code ec;

  // Make code memory executable.
  ec = applyMemoryGroupPermissions(CodeMem,
                                   sys::Memory::MF_READ | sys::Memory::MF_EXEC);
  if (ec) {
    if (ErrMsg) {
      *ErrMsg = ec.message();
    }
    return true;
  }

  // Make read-only data memory read-only.
  ec = applyMemoryGroupPermissions(RODataMem, sys::Memory::MF_READ);
  if (ec) {
    if (ErrMsg) {
      *ErrMsg = ec.message();
    }
    return true;
  }

  // Read-write data memory already has the correct permissions

  // Some platforms with separate data cache and instruction cache require
  // explicit cache flush, otherwise JIT code manipulations (like resolved
  // relocations) will get to the data cache but not to the instruction cache.
  invalidateInstructionCache();

  return false;
}

static sys::MemoryBlock trimBlockToPageSize(sys::MemoryBlock M) {
  static const size_t PageSize = sys::Process::getPageSizeEstimate();

  size_t StartOverlap =
      (PageSize - ((uintptr_t)M.base() % PageSize)) % PageSize;

  size_t TrimmedSize = M.allocatedSize();
  TrimmedSize -= StartOverlap;
  TrimmedSize -= TrimmedSize % PageSize;

  sys::MemoryBlock Trimmed((void *)((uintptr_t)M.base() + StartOverlap),
                           TrimmedSize);

  assert(((uintptr_t)Trimmed.base() % PageSize) == 0);
  assert((Trimmed.allocatedSize() % PageSize) == 0);
  assert(M.base() <= Trimmed.base() &&
         Trimmed.allocatedSize() <= M.allocatedSize());

  return Trimmed;
}

std::error_code
SectionMemoryManager::applyMemoryGroupPermissions(MemoryGroup &MemGroup,
                                                  unsigned Permissions) {
  for (sys::MemoryBlock &MB : MemGroup.PendingMem)
    if (std::error_code EC = MMapper->protectMappedMemory(MB, Permissions))
      return EC;

  MemGroup.PendingMem.clear();

  // Now go through free blocks and trim any of them that don't span the entire
  // page because one of the pending blocks may have overlapped it.
  for (FreeMemBlock &FreeMB : MemGroup.FreeMem) {
    FreeMB.Free = trimBlockToPageSize(FreeMB.Free);
    // We cleared the PendingMem list, so all these pointers are now invalid
    FreeMB.PendingPrefixIndex = (unsigned)-1;
  }

  // Remove all blocks which are now empty
  erase_if(MemGroup.FreeMem, [](FreeMemBlock &FreeMB) {
    return FreeMB.Free.allocatedSize() == 0;
  });

  return std::error_code();
}

void SectionMemoryManager::invalidateInstructionCache() {
  for (sys::MemoryBlock &Block : CodeMem.PendingMem)
    sys::Memory::InvalidateInstructionCache(Block.base(),
                                            Block.allocatedSize());
}

SectionMemoryManager::~SectionMemoryManager() {
  for (MemoryGroup *Group : {&CodeMem, &RWDataMem, &RODataMem}) {
    for (sys::MemoryBlock &Block : Group->AllocatedMem)
      MMapper->releaseMappedMemory(Block);
  }
}

SectionMemoryManager::MemoryMapper::~MemoryMapper() = default;

void SectionMemoryManager::anchor() {}

namespace {
// Trivial implementation of SectionMemoryManager::MemoryMapper that just calls
// into sys::Memory.
class DefaultMMapper final : public SectionMemoryManager::MemoryMapper {
public:
  sys::MemoryBlock
  allocateMappedMemory(SectionMemoryManager::AllocationPurpose Purpose,
                       size_t NumBytes, const sys::MemoryBlock *const NearBlock,
                       unsigned Flags, std::error_code &EC) override {
    return sys::Memory::allocateMappedMemory(NumBytes, NearBlock, Flags, EC);
  }

  std::error_code protectMappedMemory(const sys::MemoryBlock &Block,
                                      unsigned Flags) override {
    return sys::Memory::protectMappedMemory(Block, Flags);
  }

  std::error_code releaseMappedMemory(sys::MemoryBlock &M) override {
    return sys::Memory::releaseMappedMemory(M);
  }
};
} // namespace

SectionMemoryManager::SectionMemoryManager(MemoryMapper *UnownedMM)
    : MMapper(UnownedMM), OwnedMMapper(nullptr) {
  if (!MMapper) {
    OwnedMMapper = std::make_unique<DefaultMMapper>();
    MMapper = OwnedMMapper.get();
  }
}

} // namespace llvm

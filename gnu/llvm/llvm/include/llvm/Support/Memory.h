//===- llvm/Support/Memory.h - Memory Support -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the llvm::sys::Memory class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_MEMORY_H
#define LLVM_SUPPORT_MEMORY_H

#include "llvm/Support/DataTypes.h"
#include <system_error>

namespace llvm {

// Forward declare raw_ostream: it is used for debug dumping below.
class raw_ostream;

namespace sys {

  /// This class encapsulates the notion of a memory block which has an address
  /// and a size. It is used by the Memory class (a friend) as the result of
  /// various memory allocation operations.
  /// @see Memory
  /// Memory block abstraction.
  class MemoryBlock {
  public:
    MemoryBlock() : Address(nullptr), AllocatedSize(0) {}
    MemoryBlock(void *addr, size_t allocatedSize)
        : Address(addr), AllocatedSize(allocatedSize) {}
    void *base() const { return Address; }
    /// The size as it was allocated. This is always greater or equal to the
    /// size that was originally requested.
    size_t allocatedSize() const { return AllocatedSize; }

  private:
    void *Address;    ///< Address of first byte of memory area
    size_t AllocatedSize; ///< Size, in bytes of the memory area
    unsigned Flags = 0;
    friend class Memory;
  };

  /// This class provides various memory handling functions that manipulate
  /// MemoryBlock instances.
  /// @since 1.4
  /// An abstraction for memory operations.
  class Memory {
  public:
    enum ProtectionFlags {
      MF_READ = 0x1000000,
      MF_WRITE = 0x2000000,
      MF_EXEC = 0x4000000,
      MF_RWE_MASK = 0x7000000,

      /// The \p MF_HUGE_HINT flag is used to indicate that the request for
      /// a memory block should be satisfied with large pages if possible.
      /// This is only a hint and small pages will be used as fallback.
      ///
      /// The presence or absence of this flag in the returned memory block
      /// is (at least currently) *not* a reliable indicator that the memory
      /// block will use or will not use large pages. On some systems a request
      /// without this flag can be backed by large pages without this flag being
      /// set, and on some other systems a request with this flag can fallback
      /// to small pages without this flag being cleared.
      MF_HUGE_HINT = 0x0000001
    };

    /// This method allocates a block of memory that is suitable for loading
    /// dynamically generated code (e.g. JIT). An attempt to allocate
    /// \p NumBytes bytes of virtual memory is made.
    /// \p NearBlock may point to an existing allocation in which case
    /// an attempt is made to allocate more memory near the existing block.
    /// The actual allocated address is not guaranteed to be near the requested
    /// address.
    /// \p Flags is used to set the initial protection flags for the block
    /// of the memory.
    /// \p EC [out] returns an object describing any error that occurs.
    ///
    /// This method may allocate more than the number of bytes requested.  The
    /// actual number of bytes allocated is indicated in the returned
    /// MemoryBlock.
    ///
    /// The start of the allocated block must be aligned with the
    /// system allocation granularity (64K on Windows, page size on Linux).
    /// If the address following \p NearBlock is not so aligned, it will be
    /// rounded up to the next allocation granularity boundary.
    ///
    /// \r a non-null MemoryBlock if the function was successful,
    /// otherwise a null MemoryBlock is with \p EC describing the error.
    ///
    /// Allocate mapped memory.
    static MemoryBlock allocateMappedMemory(size_t NumBytes,
                                            const MemoryBlock *const NearBlock,
                                            unsigned Flags,
                                            std::error_code &EC);

    /// This method releases a block of memory that was allocated with the
    /// allocateMappedMemory method. It should not be used to release any
    /// memory block allocated any other way.
    /// \p Block describes the memory to be released.
    ///
    /// \r error_success if the function was successful, or an error_code
    /// describing the failure if an error occurred.
    ///
    /// Release mapped memory.
    static std::error_code releaseMappedMemory(MemoryBlock &Block);

    /// This method sets the protection flags for a block of memory to the
    /// state specified by /p Flags.  The behavior is not specified if the
    /// memory was not allocated using the allocateMappedMemory method.
    /// \p Block describes the memory block to be protected.
    /// \p Flags specifies the new protection state to be assigned to the block.
    ///
    /// If \p Flags is MF_WRITE, the actual behavior varies
    /// with the operating system (i.e. MF_READ | MF_WRITE on Windows) and the
    /// target architecture (i.e. MF_WRITE -> MF_READ | MF_WRITE on i386).
    ///
    /// \r error_success if the function was successful, or an error_code
    /// describing the failure if an error occurred.
    ///
    /// Set memory protection state.
    static std::error_code protectMappedMemory(const MemoryBlock &Block,
                                               unsigned Flags);

    /// InvalidateInstructionCache - Before the JIT can run a block of code
    /// that has been emitted it must invalidate the instruction cache on some
    /// platforms.
    static void InvalidateInstructionCache(const void *Addr, size_t Len);
  };

  /// Owning version of MemoryBlock.
  class OwningMemoryBlock {
  public:
    OwningMemoryBlock() = default;
    explicit OwningMemoryBlock(MemoryBlock M) : M(M) {}
    OwningMemoryBlock(OwningMemoryBlock &&Other) {
      M = Other.M;
      Other.M = MemoryBlock();
    }
    OwningMemoryBlock& operator=(OwningMemoryBlock &&Other) {
      M = Other.M;
      Other.M = MemoryBlock();
      return *this;
    }
    ~OwningMemoryBlock() {
      if (M.base())
        Memory::releaseMappedMemory(M);
    }
    void *base() const { return M.base(); }
    /// The size as it was allocated. This is always greater or equal to the
    /// size that was originally requested.
    size_t allocatedSize() const { return M.allocatedSize(); }
    MemoryBlock getMemoryBlock() const { return M; }
    std::error_code release() {
      std::error_code EC;
      if (M.base()) {
        EC = Memory::releaseMappedMemory(M);
        M = MemoryBlock();
      }
      return EC;
    }
  private:
    MemoryBlock M;
  };

#ifndef NDEBUG
  /// Debugging output for Memory::ProtectionFlags.
  raw_ostream &operator<<(raw_ostream &OS, const Memory::ProtectionFlags &PF);

  /// Debugging output for MemoryBlock.
  raw_ostream &operator<<(raw_ostream &OS, const MemoryBlock &MB);
#endif // ifndef NDEBUG
  }    // end namespace sys
  }    // end namespace llvm

#endif

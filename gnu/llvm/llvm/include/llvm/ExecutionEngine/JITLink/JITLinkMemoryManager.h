//===-- JITLinkMemoryManager.h - JITLink mem manager interface --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Contains the JITLinkMemoryManager interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_JITLINK_JITLINKMEMORYMANAGER_H
#define LLVM_EXECUTIONENGINE_JITLINK_JITLINKMEMORYMANAGER_H

#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ExecutionEngine/JITLink/JITLinkDylib.h"
#include "llvm/ExecutionEngine/Orc/Shared/AllocationActions.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/ExecutionEngine/Orc/Shared/MemoryFlags.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MSVCErrorWorkarounds.h"
#include "llvm/Support/Memory.h"
#include "llvm/Support/RecyclingAllocator.h"

#include <cstdint>
#include <future>
#include <mutex>

namespace llvm {
namespace jitlink {

class Block;
class LinkGraph;
class Section;

/// Manages allocations of JIT memory.
///
/// Instances of this class may be accessed concurrently from multiple threads
/// and their implemetations should include any necessary synchronization.
class JITLinkMemoryManager {
public:

  /// Represents a finalized allocation.
  ///
  /// Finalized allocations must be passed to the
  /// JITLinkMemoryManager:deallocate method prior to being destroyed.
  ///
  /// The interpretation of the Address associated with the finalized allocation
  /// is up to the memory manager implementation. Common options are using the
  /// base address of the allocation, or the address of a memory management
  /// object that tracks the allocation.
  class FinalizedAlloc {
    friend class JITLinkMemoryManager;

    static constexpr auto InvalidAddr = ~uint64_t(0);

  public:
    FinalizedAlloc() = default;
    explicit FinalizedAlloc(orc::ExecutorAddr A) : A(A) {
      assert(A.getValue() != InvalidAddr &&
             "Explicitly creating an invalid allocation?");
    }
    FinalizedAlloc(const FinalizedAlloc &) = delete;
    FinalizedAlloc(FinalizedAlloc &&Other) : A(Other.A) {
      Other.A.setValue(InvalidAddr);
    }
    FinalizedAlloc &operator=(const FinalizedAlloc &) = delete;
    FinalizedAlloc &operator=(FinalizedAlloc &&Other) {
      assert(A.getValue() == InvalidAddr &&
             "Cannot overwrite active finalized allocation");
      std::swap(A, Other.A);
      return *this;
    }
    ~FinalizedAlloc() {
      assert(A.getValue() == InvalidAddr &&
             "Finalized allocation was not deallocated");
    }

    /// FinalizedAllocs convert to false for default-constructed, and
    /// true otherwise. Default-constructed allocs need not be deallocated.
    explicit operator bool() const { return A.getValue() != InvalidAddr; }

    /// Returns the address associated with this finalized allocation.
    /// The allocation is unmodified.
    orc::ExecutorAddr getAddress() const { return A; }

    /// Returns the address associated with this finalized allocation and
    /// resets this object to the default state.
    /// This should only be used by allocators when deallocating memory.
    orc::ExecutorAddr release() {
      orc::ExecutorAddr Tmp = A;
      A.setValue(InvalidAddr);
      return Tmp;
    }

  private:
    orc::ExecutorAddr A{InvalidAddr};
  };

  /// Represents an allocation which has not been finalized yet.
  ///
  /// InFlightAllocs manage both executor memory allocations and working
  /// memory allocations.
  ///
  /// On finalization, the InFlightAlloc should transfer the content of
  /// working memory into executor memory, apply memory protections, and
  /// run any finalization functions.
  ///
  /// Working memory should be kept alive at least until one of the following
  /// happens: (1) the InFlightAlloc instance is destroyed, (2) the
  /// InFlightAlloc is abandoned, (3) finalized target memory is destroyed.
  ///
  /// If abandon is called then working memory and executor memory should both
  /// be freed.
  class InFlightAlloc {
  public:
    using OnFinalizedFunction = unique_function<void(Expected<FinalizedAlloc>)>;
    using OnAbandonedFunction = unique_function<void(Error)>;

    virtual ~InFlightAlloc();

    /// Called prior to finalization if the allocation should be abandoned.
    virtual void abandon(OnAbandonedFunction OnAbandoned) = 0;

    /// Called to transfer working memory to the target and apply finalization.
    virtual void finalize(OnFinalizedFunction OnFinalized) = 0;

    /// Synchronous convenience version of finalize.
    Expected<FinalizedAlloc> finalize() {
      std::promise<MSVCPExpected<FinalizedAlloc>> FinalizeResultP;
      auto FinalizeResultF = FinalizeResultP.get_future();
      finalize([&](Expected<FinalizedAlloc> Result) {
        FinalizeResultP.set_value(std::move(Result));
      });
      return FinalizeResultF.get();
    }
  };

  /// Typedef for the argument to be passed to OnAllocatedFunction.
  using AllocResult = Expected<std::unique_ptr<InFlightAlloc>>;

  /// Called when allocation has been completed.
  using OnAllocatedFunction = unique_function<void(AllocResult)>;

  /// Called when deallocation has completed.
  using OnDeallocatedFunction = unique_function<void(Error)>;

  virtual ~JITLinkMemoryManager();

  /// Start the allocation process.
  ///
  /// If the initial allocation is successful then the OnAllocated function will
  /// be called with a std::unique_ptr<InFlightAlloc> value. If the assocation
  /// is unsuccessful then the OnAllocated function will be called with an
  /// Error.
  virtual void allocate(const JITLinkDylib *JD, LinkGraph &G,
                        OnAllocatedFunction OnAllocated) = 0;

  /// Convenience function for blocking allocation.
  AllocResult allocate(const JITLinkDylib *JD, LinkGraph &G) {
    std::promise<MSVCPExpected<std::unique_ptr<InFlightAlloc>>> AllocResultP;
    auto AllocResultF = AllocResultP.get_future();
    allocate(JD, G, [&](AllocResult Alloc) {
      AllocResultP.set_value(std::move(Alloc));
    });
    return AllocResultF.get();
  }

  /// Deallocate a list of allocation objects.
  ///
  /// Dealloc actions will be run in reverse order (from the end of the vector
  /// to the start).
  virtual void deallocate(std::vector<FinalizedAlloc> Allocs,
                          OnDeallocatedFunction OnDeallocated) = 0;

  /// Convenience function for deallocation of a single alloc.
  void deallocate(FinalizedAlloc Alloc, OnDeallocatedFunction OnDeallocated) {
    std::vector<FinalizedAlloc> Allocs;
    Allocs.push_back(std::move(Alloc));
    deallocate(std::move(Allocs), std::move(OnDeallocated));
  }

  /// Convenience function for blocking deallocation.
  Error deallocate(std::vector<FinalizedAlloc> Allocs) {
    std::promise<MSVCPError> DeallocResultP;
    auto DeallocResultF = DeallocResultP.get_future();
    deallocate(std::move(Allocs),
               [&](Error Err) { DeallocResultP.set_value(std::move(Err)); });
    return DeallocResultF.get();
  }

  /// Convenience function for blocking deallocation of a single alloc.
  Error deallocate(FinalizedAlloc Alloc) {
    std::vector<FinalizedAlloc> Allocs;
    Allocs.push_back(std::move(Alloc));
    return deallocate(std::move(Allocs));
  }
};

/// BasicLayout simplifies the implementation of JITLinkMemoryManagers.
///
/// BasicLayout groups Sections into Segments based on their memory protection
/// and deallocation policies. JITLinkMemoryManagers can construct a BasicLayout
/// from a Graph, and then assign working memory and addresses to each of the
/// Segments. These addreses will be mapped back onto the Graph blocks in
/// the apply method.
class BasicLayout {
public:
  /// The Alignment, ContentSize and ZeroFillSize of each segment will be
  /// pre-filled from the Graph. Clients must set the Addr and WorkingMem fields
  /// prior to calling apply.
  //
  // FIXME: The C++98 initializer is an attempt to work around compile failures
  // due to http://www.open-std.org/jtc1/sc22/wg21/docs/cwg_defects.html#1397.
  // We should be able to switch this back to member initialization once that
  // issue is fixed.
  class Segment {
    friend class BasicLayout;

  public:
    Segment()
        : ContentSize(0), ZeroFillSize(0), Addr(0), WorkingMem(nullptr),
          NextWorkingMemOffset(0) {}
    Align Alignment;
    size_t ContentSize;
    uint64_t ZeroFillSize;
    orc::ExecutorAddr Addr;
    char *WorkingMem = nullptr;

  private:
    size_t NextWorkingMemOffset;
    std::vector<Block *> ContentBlocks, ZeroFillBlocks;
  };

  /// A convenience class that further groups segments based on memory
  /// deallocation policy. This allows clients to make two slab allocations:
  /// one for all standard segments, and one for all finalize segments.
  struct ContiguousPageBasedLayoutSizes {
    uint64_t StandardSegs = 0;
    uint64_t FinalizeSegs = 0;

    uint64_t total() const { return StandardSegs + FinalizeSegs; }
  };

private:
  using SegmentMap = orc::AllocGroupSmallMap<Segment>;

public:
  BasicLayout(LinkGraph &G);

  /// Return a reference to the graph this allocation was created from.
  LinkGraph &getGraph() { return G; }

  /// Returns the total number of required to allocate all segments (with each
  /// segment padded out to page size) for all standard segments, and all
  /// finalize segments.
  ///
  /// This is a convenience function for the common case where the segments will
  /// be allocated contiguously.
  ///
  /// This function will return an error if any segment has an alignment that
  /// is higher than a page.
  Expected<ContiguousPageBasedLayoutSizes>
  getContiguousPageBasedLayoutSizes(uint64_t PageSize);

  /// Returns an iterator over the segments of the layout.
  iterator_range<SegmentMap::iterator> segments() {
    return {Segments.begin(), Segments.end()};
  }

  /// Apply the layout to the graph.
  Error apply();

  /// Returns a reference to the AllocActions in the graph.
  /// This convenience function saves callers from having to #include
  /// LinkGraph.h if all they need are allocation actions.
  orc::shared::AllocActions &graphAllocActions();

private:
  LinkGraph &G;
  SegmentMap Segments;
};

/// A utility class for making simple allocations using JITLinkMemoryManager.
///
/// SimpleSegementAlloc takes a mapping of AllocGroups to Segments and uses
/// this to create a LinkGraph with one Section (containing one Block) per
/// Segment. Clients can obtain a pointer to the working memory and executor
/// address of that block using the Segment's AllocGroup. Once memory has been
/// populated, clients can call finalize to finalize the memory.
///
/// Note: Segments with MemLifetime::NoAlloc are not permitted, since they would
/// not be useful, and their presence is likely to indicate a bug.
class SimpleSegmentAlloc {
public:
  /// Describes a segment to be allocated.
  struct Segment {
    Segment() = default;
    Segment(size_t ContentSize, Align ContentAlign)
        : ContentSize(ContentSize), ContentAlign(ContentAlign) {}

    size_t ContentSize = 0;
    Align ContentAlign;
  };

  /// Describes the segment working memory and executor address.
  struct SegmentInfo {
    orc::ExecutorAddr Addr;
    MutableArrayRef<char> WorkingMem;
  };

  using SegmentMap = orc::AllocGroupSmallMap<Segment>;

  using OnCreatedFunction = unique_function<void(Expected<SimpleSegmentAlloc>)>;

  using OnFinalizedFunction =
      JITLinkMemoryManager::InFlightAlloc::OnFinalizedFunction;

  static void Create(JITLinkMemoryManager &MemMgr, const JITLinkDylib *JD,
                     SegmentMap Segments, OnCreatedFunction OnCreated);

  static Expected<SimpleSegmentAlloc> Create(JITLinkMemoryManager &MemMgr,
                                             const JITLinkDylib *JD,
                                             SegmentMap Segments);

  SimpleSegmentAlloc(SimpleSegmentAlloc &&);
  SimpleSegmentAlloc &operator=(SimpleSegmentAlloc &&);
  ~SimpleSegmentAlloc();

  /// Returns the SegmentInfo for the given group.
  SegmentInfo getSegInfo(orc::AllocGroup AG);

  /// Finalize all groups (async version).
  void finalize(OnFinalizedFunction OnFinalized) {
    Alloc->finalize(std::move(OnFinalized));
  }

  /// Finalize all groups.
  Expected<JITLinkMemoryManager::FinalizedAlloc> finalize() {
    return Alloc->finalize();
  }

private:
  SimpleSegmentAlloc(
      std::unique_ptr<LinkGraph> G,
      orc::AllocGroupSmallMap<Block *> ContentBlocks,
      std::unique_ptr<JITLinkMemoryManager::InFlightAlloc> Alloc);

  std::unique_ptr<LinkGraph> G;
  orc::AllocGroupSmallMap<Block *> ContentBlocks;
  std::unique_ptr<JITLinkMemoryManager::InFlightAlloc> Alloc;
};

/// A JITLinkMemoryManager that allocates in-process memory.
class InProcessMemoryManager : public JITLinkMemoryManager {
public:
  class IPInFlightAlloc;

  /// Attempts to auto-detect the host page size.
  static Expected<std::unique_ptr<InProcessMemoryManager>> Create();

  /// Create an instance using the given page size.
  InProcessMemoryManager(uint64_t PageSize) : PageSize(PageSize) {}

  void allocate(const JITLinkDylib *JD, LinkGraph &G,
                OnAllocatedFunction OnAllocated) override;

  // Use overloads from base class.
  using JITLinkMemoryManager::allocate;

  void deallocate(std::vector<FinalizedAlloc> Alloc,
                  OnDeallocatedFunction OnDeallocated) override;

  // Use overloads from base class.
  using JITLinkMemoryManager::deallocate;

private:
  // FIXME: Use an in-place array instead of a vector for DeallocActions.
  //        There shouldn't need to be a heap alloc for this.
  struct FinalizedAllocInfo {
    sys::MemoryBlock StandardSegments;
    std::vector<orc::shared::WrapperFunctionCall> DeallocActions;
  };

  FinalizedAlloc createFinalizedAlloc(
      sys::MemoryBlock StandardSegments,
      std::vector<orc::shared::WrapperFunctionCall> DeallocActions);

  uint64_t PageSize;
  std::mutex FinalizedAllocsMutex;
  RecyclingAllocator<BumpPtrAllocator, FinalizedAllocInfo> FinalizedAllocInfos;
};

} // end namespace jitlink
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_JITLINK_JITLINKMEMORYMANAGER_H

//===--------------------- ResourceManager.cpp ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// The classes here represent processor resource units and their management
/// strategy.  These classes are managed by the Scheduler.
///
//===----------------------------------------------------------------------===//

#include "llvm/MCA/HardwareUnits/ResourceManager.h"
#include "llvm/MCA/Support.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
namespace mca {

#define DEBUG_TYPE "llvm-mca"
ResourceStrategy::~ResourceStrategy() = default;

static uint64_t selectImpl(uint64_t CandidateMask,
                           uint64_t &NextInSequenceMask) {
  // The upper bit set in CandidateMask identifies our next candidate resource.
  CandidateMask = 1ULL << getResourceStateIndex(CandidateMask);
  NextInSequenceMask &= (CandidateMask | (CandidateMask - 1));
  return CandidateMask;
}

uint64_t DefaultResourceStrategy::select(uint64_t ReadyMask) {
  // This method assumes that ReadyMask cannot be zero.
  uint64_t CandidateMask = ReadyMask & NextInSequenceMask;
  if (CandidateMask)
    return selectImpl(CandidateMask, NextInSequenceMask);

  NextInSequenceMask = ResourceUnitMask ^ RemovedFromNextInSequence;
  RemovedFromNextInSequence = 0;
  CandidateMask = ReadyMask & NextInSequenceMask;
  if (CandidateMask)
    return selectImpl(CandidateMask, NextInSequenceMask);

  NextInSequenceMask = ResourceUnitMask;
  CandidateMask = ReadyMask & NextInSequenceMask;
  return selectImpl(CandidateMask, NextInSequenceMask);
}

void DefaultResourceStrategy::used(uint64_t Mask) {
  if (Mask > NextInSequenceMask) {
    RemovedFromNextInSequence |= Mask;
    return;
  }

  NextInSequenceMask &= (~Mask);
  if (NextInSequenceMask)
    return;

  NextInSequenceMask = ResourceUnitMask ^ RemovedFromNextInSequence;
  RemovedFromNextInSequence = 0;
}

ResourceState::ResourceState(const MCProcResourceDesc &Desc, unsigned Index,
                             uint64_t Mask)
    : ProcResourceDescIndex(Index), ResourceMask(Mask),
      BufferSize(Desc.BufferSize), IsAGroup(llvm::popcount(ResourceMask) > 1) {
  if (IsAGroup) {
    ResourceSizeMask =
        ResourceMask ^ 1ULL << getResourceStateIndex(ResourceMask);
  } else {
    ResourceSizeMask = (1ULL << Desc.NumUnits) - 1;
  }
  ReadyMask = ResourceSizeMask;
  AvailableSlots = BufferSize == -1 ? 0U : static_cast<unsigned>(BufferSize);
  Unavailable = false;
}

bool ResourceState::isReady(unsigned NumUnits) const {
  return (!isReserved() || isADispatchHazard()) &&
         (unsigned)llvm::popcount(ReadyMask) >= NumUnits;
}

ResourceStateEvent ResourceState::isBufferAvailable() const {
  if (isADispatchHazard() && isReserved())
    return RS_RESERVED;
  if (!isBuffered() || AvailableSlots)
    return RS_BUFFER_AVAILABLE;
  return RS_BUFFER_UNAVAILABLE;
}

#ifndef NDEBUG
void ResourceState::dump() const {
  dbgs() << "MASK=" << format_hex(ResourceMask, 16)
         << ", SZMASK=" << format_hex(ResourceSizeMask, 16)
         << ", RDYMASK=" << format_hex(ReadyMask, 16)
         << ", BufferSize=" << BufferSize
         << ", AvailableSlots=" << AvailableSlots
         << ", Reserved=" << Unavailable << '\n';
}
#endif

static std::unique_ptr<ResourceStrategy>
getStrategyFor(const ResourceState &RS) {
  if (RS.isAResourceGroup() || RS.getNumUnits() > 1)
    return std::make_unique<DefaultResourceStrategy>(RS.getReadyMask());
  return std::unique_ptr<ResourceStrategy>(nullptr);
}

ResourceManager::ResourceManager(const MCSchedModel &SM)
    : Resources(SM.getNumProcResourceKinds() - 1),
      Strategies(SM.getNumProcResourceKinds() - 1),
      Resource2Groups(SM.getNumProcResourceKinds() - 1, 0),
      ProcResID2Mask(SM.getNumProcResourceKinds(), 0),
      ResIndex2ProcResID(SM.getNumProcResourceKinds() - 1, 0),
      ProcResUnitMask(0), ReservedResourceGroups(0), AvailableBuffers(~0ULL),
      ReservedBuffers(0) {
  computeProcResourceMasks(SM, ProcResID2Mask);

  // initialize vector ResIndex2ProcResID.
  for (unsigned I = 1, E = SM.getNumProcResourceKinds(); I < E; ++I) {
    unsigned Index = getResourceStateIndex(ProcResID2Mask[I]);
    ResIndex2ProcResID[Index] = I;
  }

  for (unsigned I = 1, E = SM.getNumProcResourceKinds(); I < E; ++I) {
    uint64_t Mask = ProcResID2Mask[I];
    unsigned Index = getResourceStateIndex(Mask);
    Resources[Index] =
        std::make_unique<ResourceState>(*SM.getProcResource(I), I, Mask);
    Strategies[Index] = getStrategyFor(*Resources[Index]);
  }

  for (unsigned I = 1, E = SM.getNumProcResourceKinds(); I < E; ++I) {
    uint64_t Mask = ProcResID2Mask[I];
    unsigned Index = getResourceStateIndex(Mask);
    const ResourceState &RS = *Resources[Index];
    if (!RS.isAResourceGroup()) {
      ProcResUnitMask |= Mask;
      continue;
    }

    uint64_t GroupMaskIdx = 1ULL << Index;
    Mask -= GroupMaskIdx;
    while (Mask) {
      // Extract lowest set isolated bit.
      uint64_t Unit = Mask & (-Mask);
      unsigned IndexUnit = getResourceStateIndex(Unit);
      Resource2Groups[IndexUnit] |= GroupMaskIdx;
      Mask ^= Unit;
    }
  }

  AvailableProcResUnits = ProcResUnitMask;
}

void ResourceManager::setCustomStrategyImpl(std::unique_ptr<ResourceStrategy> S,
                                            uint64_t ResourceMask) {
  unsigned Index = getResourceStateIndex(ResourceMask);
  assert(Index < Resources.size() && "Invalid processor resource index!");
  assert(S && "Unexpected null strategy in input!");
  Strategies[Index] = std::move(S);
}

unsigned ResourceManager::resolveResourceMask(uint64_t Mask) const {
  return ResIndex2ProcResID[getResourceStateIndex(Mask)];
}

unsigned ResourceManager::getNumUnits(uint64_t ResourceID) const {
  return Resources[getResourceStateIndex(ResourceID)]->getNumUnits();
}

// Returns the actual resource consumed by this Use.
// First, is the primary resource ID.
// Second, is the specific sub-resource ID.
ResourceRef ResourceManager::selectPipe(uint64_t ResourceID) {
  unsigned Index = getResourceStateIndex(ResourceID);
  assert(Index < Resources.size() && "Invalid resource use!");
  ResourceState &RS = *Resources[Index];
  assert(RS.isReady() && "No available units to select!");

  // Special case where RS is not a group, and it only declares a single
  // resource unit.
  if (!RS.isAResourceGroup() && RS.getNumUnits() == 1)
    return std::make_pair(ResourceID, RS.getReadyMask());

  uint64_t SubResourceID = Strategies[Index]->select(RS.getReadyMask());
  if (RS.isAResourceGroup())
    return selectPipe(SubResourceID);
  return std::make_pair(ResourceID, SubResourceID);
}

void ResourceManager::use(const ResourceRef &RR) {
  // Mark the sub-resource referenced by RR as used.
  unsigned RSID = getResourceStateIndex(RR.first);
  ResourceState &RS = *Resources[RSID];
  RS.markSubResourceAsUsed(RR.second);
  // Remember to update the resource strategy for non-group resources with
  // multiple units.
  if (RS.getNumUnits() > 1)
    Strategies[RSID]->used(RR.second);

  // If there are still available units in RR.first,
  // then we are done.
  if (RS.isReady())
    return;

  AvailableProcResUnits ^= RR.first;

  // Notify groups that RR.first is no longer available.
  uint64_t Users = Resource2Groups[RSID];
  while (Users) {
    // Extract lowest set isolated bit.
    unsigned GroupIndex = getResourceStateIndex(Users & (-Users));
    ResourceState &CurrentUser = *Resources[GroupIndex];
    CurrentUser.markSubResourceAsUsed(RR.first);
    Strategies[GroupIndex]->used(RR.first);
    // Reset lowest set bit.
    Users &= Users - 1;
  }
}

void ResourceManager::release(const ResourceRef &RR) {
  unsigned RSID = getResourceStateIndex(RR.first);
  ResourceState &RS = *Resources[RSID];
  bool WasFullyUsed = !RS.isReady();
  RS.releaseSubResource(RR.second);
  if (!WasFullyUsed)
    return;

  AvailableProcResUnits ^= RR.first;

  // Notify groups that RR.first is now available again.
  uint64_t Users = Resource2Groups[RSID];
  while (Users) {
    unsigned GroupIndex = getResourceStateIndex(Users & (-Users));
    ResourceState &CurrentUser = *Resources[GroupIndex];
    CurrentUser.releaseSubResource(RR.first);
    Users &= Users - 1;
  }
}

ResourceStateEvent
ResourceManager::canBeDispatched(uint64_t ConsumedBuffers) const {
  if (ConsumedBuffers & ReservedBuffers)
    return ResourceStateEvent::RS_RESERVED;
  if (ConsumedBuffers & (~AvailableBuffers))
    return ResourceStateEvent::RS_BUFFER_UNAVAILABLE;
  return ResourceStateEvent::RS_BUFFER_AVAILABLE;
}

void ResourceManager::reserveBuffers(uint64_t ConsumedBuffers) {
  while (ConsumedBuffers) {
    uint64_t CurrentBuffer = ConsumedBuffers & (-ConsumedBuffers);
    ResourceState &RS = *Resources[getResourceStateIndex(CurrentBuffer)];
    ConsumedBuffers ^= CurrentBuffer;
    assert(RS.isBufferAvailable() == ResourceStateEvent::RS_BUFFER_AVAILABLE);
    if (!RS.reserveBuffer())
      AvailableBuffers ^= CurrentBuffer;
    if (RS.isADispatchHazard()) {
      // Reserve this buffer now, and release it once pipeline resources
      // consumed by the instruction become available again.
      // We do this to simulate an in-order dispatch/issue of instructions.
      ReservedBuffers ^= CurrentBuffer;
    }
  }
}

void ResourceManager::releaseBuffers(uint64_t ConsumedBuffers) {
  AvailableBuffers |= ConsumedBuffers;
  while (ConsumedBuffers) {
    uint64_t CurrentBuffer = ConsumedBuffers & (-ConsumedBuffers);
    ResourceState &RS = *Resources[getResourceStateIndex(CurrentBuffer)];
    ConsumedBuffers ^= CurrentBuffer;
    RS.releaseBuffer();
    // Do not unreserve dispatch hazard resource buffers. Wait until all
    // pipeline resources have been freed too.
  }
}

uint64_t ResourceManager::checkAvailability(const InstrDesc &Desc) const {
  uint64_t BusyResourceMask = 0;
  uint64_t ConsumedResourceMask = 0;
  DenseMap<uint64_t, unsigned> AvailableUnits;

  for (const std::pair<uint64_t, ResourceUsage> &E : Desc.Resources) {
    unsigned NumUnits = E.second.isReserved() ? 0U : E.second.NumUnits;
    const ResourceState &RS = *Resources[getResourceStateIndex(E.first)];
    if (!RS.isReady(NumUnits)) {
      BusyResourceMask |= E.first;
      continue;
    }

    if (Desc.HasPartiallyOverlappingGroups && !RS.isAResourceGroup()) {
      unsigned NumAvailableUnits = llvm::popcount(RS.getReadyMask());
      NumAvailableUnits -= NumUnits;
      AvailableUnits[E.first] = NumAvailableUnits;
      if (!NumAvailableUnits)
        ConsumedResourceMask |= E.first;
    }
  }

  BusyResourceMask &= ProcResUnitMask;
  if (BusyResourceMask)
    return BusyResourceMask;

  BusyResourceMask = Desc.UsedProcResGroups & ReservedResourceGroups;
  if (!Desc.HasPartiallyOverlappingGroups || BusyResourceMask)
    return BusyResourceMask;

  // If this instruction has overlapping groups, make sure that we can
  // select at least one unit per group.
  for (const std::pair<uint64_t, ResourceUsage> &E : Desc.Resources) {
    const ResourceState &RS = *Resources[getResourceStateIndex(E.first)];
    if (!E.second.isReserved() && RS.isAResourceGroup()) {
      uint64_t ReadyMask = RS.getReadyMask() & ~ConsumedResourceMask;
      if (!ReadyMask) {
        BusyResourceMask |= RS.getReadyMask();
        continue;
      }

      uint64_t ResourceMask = llvm::bit_floor(ReadyMask);

      auto it = AvailableUnits.find(ResourceMask);
      if (it == AvailableUnits.end()) {
        unsigned Index = getResourceStateIndex(ResourceMask);
        unsigned NumUnits = llvm::popcount(Resources[Index]->getReadyMask());
        it =
            AvailableUnits.insert(std::make_pair(ResourceMask, NumUnits)).first;
      }

      if (!it->second) {
        BusyResourceMask |= it->first;
        continue;
      }

      it->second--;
      if (!it->second)
        ConsumedResourceMask |= it->first;
    }
  }

  return BusyResourceMask;
}

void ResourceManager::issueInstruction(
    const InstrDesc &Desc,
    SmallVectorImpl<std::pair<ResourceRef, ReleaseAtCycles>> &Pipes) {
  for (const std::pair<uint64_t, ResourceUsage> &R : Desc.Resources) {
    const CycleSegment &CS = R.second.CS;
    if (!CS.size()) {
      releaseResource(R.first);
      continue;
    }

    assert(CS.begin() == 0 && "Invalid {Start, End} cycles!");
    if (!R.second.isReserved()) {
      ResourceRef Pipe = selectPipe(R.first);
      use(Pipe);
      BusyResources[Pipe] += CS.size();
      Pipes.emplace_back(std::pair<ResourceRef, ReleaseAtCycles>(
          Pipe, ReleaseAtCycles(CS.size())));
    } else {
      assert((llvm::popcount(R.first) > 1) && "Expected a group!");
      // Mark this group as reserved.
      assert(R.second.isReserved());
      reserveResource(R.first);
      BusyResources[ResourceRef(R.first, R.first)] += CS.size();
    }
  }
}

void ResourceManager::cycleEvent(SmallVectorImpl<ResourceRef> &ResourcesFreed) {
  for (std::pair<ResourceRef, unsigned> &BR : BusyResources) {
    if (BR.second)
      BR.second--;
    if (!BR.second) {
      // Release this resource.
      const ResourceRef &RR = BR.first;

      if (llvm::popcount(RR.first) == 1)
        release(RR);
      releaseResource(RR.first);
      ResourcesFreed.push_back(RR);
    }
  }

  for (const ResourceRef &RF : ResourcesFreed)
    BusyResources.erase(RF);
}

void ResourceManager::reserveResource(uint64_t ResourceID) {
  const unsigned Index = getResourceStateIndex(ResourceID);
  ResourceState &Resource = *Resources[Index];
  assert(Resource.isAResourceGroup() && !Resource.isReserved() &&
         "Unexpected resource state found!");
  Resource.setReserved();
  ReservedResourceGroups ^= 1ULL << Index;
}

void ResourceManager::releaseResource(uint64_t ResourceID) {
  const unsigned Index = getResourceStateIndex(ResourceID);
  ResourceState &Resource = *Resources[Index];
  Resource.clearReserved();
  if (Resource.isAResourceGroup())
    ReservedResourceGroups ^= 1ULL << Index;
  // Now it is safe to release dispatch/issue resources.
  if (Resource.isADispatchHazard())
    ReservedBuffers ^= 1ULL << Index;
}

} // namespace mca
} // namespace llvm

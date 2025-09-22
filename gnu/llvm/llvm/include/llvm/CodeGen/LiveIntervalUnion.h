//===- LiveIntervalUnion.h - Live interval union data struct ---*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// LiveIntervalUnion is a union of live segments across multiple live virtual
// registers. This may be used during coalescing to represent a congruence
// class, or during register allocation to model liveness of a physical
// register.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_LIVEINTERVALUNION_H
#define LLVM_CODEGEN_LIVEINTERVALUNION_H

#include "llvm/ADT/IntervalMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include <cassert>
#include <limits>

namespace llvm {

class raw_ostream;
class TargetRegisterInfo;

#ifndef NDEBUG
// forward declaration
template <unsigned Element> class SparseBitVector;

using LiveVirtRegBitSet = SparseBitVector<128>;
#endif

/// Union of live intervals that are strong candidates for coalescing into a
/// single register (either physical or virtual depending on the context).  We
/// expect the constituent live intervals to be disjoint, although we may
/// eventually make exceptions to handle value-based interference.
class LiveIntervalUnion {
  // A set of live virtual register segments that supports fast insertion,
  // intersection, and removal.
  // Mapping SlotIndex intervals to virtual register numbers.
  using LiveSegments = IntervalMap<SlotIndex, const LiveInterval *>;

public:
  // SegmentIter can advance to the next segment ordered by starting position
  // which may belong to a different live virtual register. We also must be able
  // to reach the current segment's containing virtual register.
  using SegmentIter = LiveSegments::iterator;

  /// Const version of SegmentIter.
  using ConstSegmentIter = LiveSegments::const_iterator;

  // LiveIntervalUnions share an external allocator.
  using Allocator = LiveSegments::Allocator;

private:
  unsigned Tag = 0;       // unique tag for current contents.
  LiveSegments Segments;  // union of virtual reg segments

public:
  explicit LiveIntervalUnion(Allocator &a) : Segments(a) {}

  // Iterate over all segments in the union of live virtual registers ordered
  // by their starting position.
  SegmentIter begin() { return Segments.begin(); }
  SegmentIter end() { return Segments.end(); }
  SegmentIter find(SlotIndex x) { return Segments.find(x); }
  ConstSegmentIter begin() const { return Segments.begin(); }
  ConstSegmentIter end() const { return Segments.end(); }
  ConstSegmentIter find(SlotIndex x) const { return Segments.find(x); }

  bool empty() const { return Segments.empty(); }
  SlotIndex startIndex() const { return Segments.start(); }
  SlotIndex endIndex() const { return Segments.stop(); }

  // Provide public access to the underlying map to allow overlap iteration.
  using Map = LiveSegments;
  const Map &getMap() const { return Segments; }

  /// getTag - Return an opaque tag representing the current state of the union.
  unsigned getTag() const { return Tag; }

  /// changedSince - Return true if the union change since getTag returned tag.
  bool changedSince(unsigned tag) const { return tag != Tag; }

  // Add a live virtual register to this union and merge its segments.
  void unify(const LiveInterval &VirtReg, const LiveRange &Range);

  // Remove a live virtual register's segments from this union.
  void extract(const LiveInterval &VirtReg, const LiveRange &Range);

  // Remove all inserted virtual registers.
  void clear() { Segments.clear(); ++Tag; }

  // Print union, using TRI to translate register names
  void print(raw_ostream &OS, const TargetRegisterInfo *TRI) const;

#ifndef NDEBUG
  // Verify the live intervals in this union and add them to the visited set.
  void verify(LiveVirtRegBitSet& VisitedVRegs);
#endif

  // Get any virtual register that is assign to this physical unit
  const LiveInterval *getOneVReg() const;

  /// Query interferences between a single live virtual register and a live
  /// interval union.
  class Query {
    const LiveIntervalUnion *LiveUnion = nullptr;
    const LiveRange *LR = nullptr;
    LiveRange::const_iterator LRI;  ///< current position in LR
    ConstSegmentIter LiveUnionI;    ///< current position in LiveUnion
    SmallVector<const LiveInterval *, 4> InterferingVRegs;
    bool CheckedFirstInterference = false;
    bool SeenAllInterferences = false;
    unsigned Tag = 0;
    unsigned UserTag = 0;

    // Count the virtual registers in this union that interfere with this
    // query's live virtual register, up to maxInterferingRegs.
    unsigned collectInterferingVRegs(unsigned MaxInterferingRegs);

    // Was this virtual register visited during collectInterferingVRegs?
    bool isSeenInterference(const LiveInterval *VirtReg) const;

  public:
    Query() = default;
    Query(const LiveRange &LR, const LiveIntervalUnion &LIU)
        : LiveUnion(&LIU), LR(&LR) {}
    Query(const Query &) = delete;
    Query &operator=(const Query &) = delete;

    void reset(unsigned NewUserTag, const LiveRange &NewLR,
               const LiveIntervalUnion &NewLiveUnion) {
      LiveUnion = &NewLiveUnion;
      LR = &NewLR;
      InterferingVRegs.clear();
      CheckedFirstInterference = false;
      SeenAllInterferences = false;
      Tag = NewLiveUnion.getTag();
      UserTag = NewUserTag;
    }

    void init(unsigned NewUserTag, const LiveRange &NewLR,
              const LiveIntervalUnion &NewLiveUnion) {
      if (UserTag == NewUserTag && LR == &NewLR && LiveUnion == &NewLiveUnion &&
          !NewLiveUnion.changedSince(Tag)) {
        // Retain cached results, e.g. firstInterference.
        return;
      }
      reset(NewUserTag, NewLR, NewLiveUnion);
    }

    // Does this live virtual register interfere with the union?
    bool checkInterference() { return collectInterferingVRegs(1); }

    // Vector generated by collectInterferingVRegs.
    const SmallVectorImpl<const LiveInterval *> &interferingVRegs(
        unsigned MaxInterferingRegs = std::numeric_limits<unsigned>::max()) {
      if (!SeenAllInterferences || MaxInterferingRegs < InterferingVRegs.size())
        collectInterferingVRegs(MaxInterferingRegs);
      return InterferingVRegs;
    }
  };

  // Array of LiveIntervalUnions.
  class Array {
    unsigned Size = 0;
    LiveIntervalUnion *LIUs = nullptr;

  public:
    Array() = default;
    ~Array() { clear(); }

    // Initialize the array to have Size entries.
    // Reuse an existing allocation if the size matches.
    void init(LiveIntervalUnion::Allocator&, unsigned Size);

    unsigned size() const { return Size; }

    void clear();

    LiveIntervalUnion& operator[](unsigned idx) {
      assert(idx <  Size && "idx out of bounds");
      return LIUs[idx];
    }

    const LiveIntervalUnion& operator[](unsigned Idx) const {
      assert(Idx < Size && "Idx out of bounds");
      return LIUs[Idx];
    }
  };
};

} // end namespace llvm

#endif // LLVM_CODEGEN_LIVEINTERVALUNION_H

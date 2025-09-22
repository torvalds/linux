//===- StackColoring.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass implements the stack-coloring optimization that looks for
// lifetime markers machine instructions (LIFETIME_START and LIFETIME_END),
// which represent the possible lifetime of stack slots. It attempts to
// merge disjoint stack slots and reduce the used stack space.
// NOTE: This pass is not StackSlotColoring, which optimizes spill slots.
//
// TODO: In the future we plan to improve stack coloring in the following ways:
// 1. Allow merging multiple small slots into a single larger slot at different
//    offsets.
// 2. Merge this pass with StackSlotColoring and allow merging of allocas with
//    spill slots.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/PseudoSourceValueManager.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/WinEHFuncInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/Value.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <limits>
#include <memory>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "stack-coloring"

static cl::opt<bool>
DisableColoring("no-stack-coloring",
        cl::init(false), cl::Hidden,
        cl::desc("Disable stack coloring"));

/// The user may write code that uses allocas outside of the declared lifetime
/// zone. This can happen when the user returns a reference to a local
/// data-structure. We can detect these cases and decide not to optimize the
/// code. If this flag is enabled, we try to save the user. This option
/// is treated as overriding LifetimeStartOnFirstUse below.
static cl::opt<bool>
ProtectFromEscapedAllocas("protect-from-escaped-allocas",
                          cl::init(false), cl::Hidden,
                          cl::desc("Do not optimize lifetime zones that "
                                   "are broken"));

/// Enable enhanced dataflow scheme for lifetime analysis (treat first
/// use of stack slot as start of slot lifetime, as opposed to looking
/// for LIFETIME_START marker). See "Implementation notes" below for
/// more info.
static cl::opt<bool>
LifetimeStartOnFirstUse("stackcoloring-lifetime-start-on-first-use",
        cl::init(true), cl::Hidden,
        cl::desc("Treat stack lifetimes as starting on first use, not on START marker."));


STATISTIC(NumMarkerSeen,  "Number of lifetime markers found.");
STATISTIC(StackSpaceSaved, "Number of bytes saved due to merging slots.");
STATISTIC(StackSlotMerged, "Number of stack slot merged.");
STATISTIC(EscapedAllocas, "Number of allocas that escaped the lifetime region");

//===----------------------------------------------------------------------===//
//                           StackColoring Pass
//===----------------------------------------------------------------------===//
//
// Stack Coloring reduces stack usage by merging stack slots when they
// can't be used together. For example, consider the following C program:
//
//     void bar(char *, int);
//     void foo(bool var) {
//         A: {
//             char z[4096];
//             bar(z, 0);
//         }
//
//         char *p;
//         char x[4096];
//         char y[4096];
//         if (var) {
//             p = x;
//         } else {
//             bar(y, 1);
//             p = y + 1024;
//         }
//     B:
//         bar(p, 2);
//     }
//
// Naively-compiled, this program would use 12k of stack space. However, the
// stack slot corresponding to `z` is always destroyed before either of the
// stack slots for `x` or `y` are used, and then `x` is only used if `var`
// is true, while `y` is only used if `var` is false. So in no time are 2
// of the stack slots used together, and therefore we can merge them,
// compiling the function using only a single 4k alloca:
//
//     void foo(bool var) { // equivalent
//         char x[4096];
//         char *p;
//         bar(x, 0);
//         if (var) {
//             p = x;
//         } else {
//             bar(x, 1);
//             p = x + 1024;
//         }
//         bar(p, 2);
//     }
//
// This is an important optimization if we want stack space to be under
// control in large functions, both open-coded ones and ones created by
// inlining.
//
// Implementation Notes:
// ---------------------
//
// An important part of the above reasoning is that `z` can't be accessed
// while the latter 2 calls to `bar` are running. This is justified because
// `z`'s lifetime is over after we exit from block `A:`, so any further
// accesses to it would be UB. The way we represent this information
// in LLVM is by having frontends delimit blocks with `lifetime.start`
// and `lifetime.end` intrinsics.
//
// The effect of these intrinsics seems to be as follows (maybe I should
// specify this in the reference?):
//
//   L1) at start, each stack-slot is marked as *out-of-scope*, unless no
//   lifetime intrinsic refers to that stack slot, in which case
//   it is marked as *in-scope*.
//   L2) on a `lifetime.start`, a stack slot is marked as *in-scope* and
//   the stack slot is overwritten with `undef`.
//   L3) on a `lifetime.end`, a stack slot is marked as *out-of-scope*.
//   L4) on function exit, all stack slots are marked as *out-of-scope*.
//   L5) `lifetime.end` is a no-op when called on a slot that is already
//   *out-of-scope*.
//   L6) memory accesses to *out-of-scope* stack slots are UB.
//   L7) when a stack-slot is marked as *out-of-scope*, all pointers to it
//   are invalidated, unless the slot is "degenerate". This is used to
//   justify not marking slots as in-use until the pointer to them is
//   used, but feels a bit hacky in the presence of things like LICM. See
//   the "Degenerate Slots" section for more details.
//
// Now, let's ground stack coloring on these rules. We'll define a slot
// as *in-use* at a (dynamic) point in execution if it either can be
// written to at that point, or if it has a live and non-undef content
// at that point.
//
// Obviously, slots that are never *in-use* together can be merged, and
// in our example `foo`, the slots for `x`, `y` and `z` are never
// in-use together (of course, sometimes slots that *are* in-use together
// might still be mergable, but we don't care about that here).
//
// In this implementation, we successively merge pairs of slots that are
// not *in-use* together. We could be smarter - for example, we could merge
// a single large slot with 2 small slots, or we could construct the
// interference graph and run a "smart" graph coloring algorithm, but with
// that aside, how do we find out whether a pair of slots might be *in-use*
// together?
//
// From our rules, we see that *out-of-scope* slots are never *in-use*,
// and from (L7) we see that "non-degenerate" slots remain non-*in-use*
// until their address is taken. Therefore, we can approximate slot activity
// using dataflow.
//
// A subtle point: naively, we might try to figure out which pairs of
// stack-slots interfere by propagating `S in-use` through the CFG for every
// stack-slot `S`, and having `S` and `T` interfere if there is a CFG point in
// which they are both *in-use*.
//
// That is sound, but overly conservative in some cases: in our (artificial)
// example `foo`, either `x` or `y` might be in use at the label `B:`, but
// as `x` is only in use if we came in from the `var` edge and `y` only
// if we came from the `!var` edge, they still can't be in use together.
// See PR32488 for an important real-life case.
//
// If we wanted to find all points of interference precisely, we could
// propagate `S in-use` and `S&T in-use` predicates through the CFG. That
// would be precise, but requires propagating `O(n^2)` dataflow facts.
//
// However, we aren't interested in the *set* of points of interference
// between 2 stack slots, only *whether* there *is* such a point. So we
// can rely on a little trick: for `S` and `T` to be in-use together,
// one of them needs to become in-use while the other is in-use (or
// they might both become in use simultaneously). We can check this
// by also keeping track of the points at which a stack slot might *start*
// being in-use.
//
// Exact first use:
// ----------------
//
// Consider the following motivating example:
//
//     int foo() {
//       char b1[1024], b2[1024];
//       if (...) {
//         char b3[1024];
//         <uses of b1, b3>;
//         return x;
//       } else {
//         char b4[1024], b5[1024];
//         <uses of b2, b4, b5>;
//         return y;
//       }
//     }
//
// In the code above, "b3" and "b4" are declared in distinct lexical
// scopes, meaning that it is easy to prove that they can share the
// same stack slot. Variables "b1" and "b2" are declared in the same
// scope, meaning that from a lexical point of view, their lifetimes
// overlap. From a control flow pointer of view, however, the two
// variables are accessed in disjoint regions of the CFG, thus it
// should be possible for them to share the same stack slot. An ideal
// stack allocation for the function above would look like:
//
//     slot 0: b1, b2
//     slot 1: b3, b4
//     slot 2: b5
//
// Achieving this allocation is tricky, however, due to the way
// lifetime markers are inserted. Here is a simplified view of the
// control flow graph for the code above:
//
//                +------  block 0 -------+
//               0| LIFETIME_START b1, b2 |
//               1| <test 'if' condition> |
//                +-----------------------+
//                   ./              \.
//   +------  block 1 -------+   +------  block 2 -------+
//  2| LIFETIME_START b3     |  5| LIFETIME_START b4, b5 |
//  3| <uses of b1, b3>      |  6| <uses of b2, b4, b5>  |
//  4| LIFETIME_END b3       |  7| LIFETIME_END b4, b5   |
//   +-----------------------+   +-----------------------+
//                   \.              /.
//                +------  block 3 -------+
//               8| <cleanupcode>         |
//               9| LIFETIME_END b1, b2   |
//              10| return                |
//                +-----------------------+
//
// If we create live intervals for the variables above strictly based
// on the lifetime markers, we'll get the set of intervals on the
// left. If we ignore the lifetime start markers and instead treat a
// variable's lifetime as beginning with the first reference to the
// var, then we get the intervals on the right.
//
//            LIFETIME_START      First Use
//     b1:    [0,9]               [3,4] [8,9]
//     b2:    [0,9]               [6,9]
//     b3:    [2,4]               [3,4]
//     b4:    [5,7]               [6,7]
//     b5:    [5,7]               [6,7]
//
// For the intervals on the left, the best we can do is overlap two
// variables (b3 and b4, for example); this gives us a stack size of
// 4*1024 bytes, not ideal. When treating first-use as the start of a
// lifetime, we can additionally overlap b1 and b5, giving us a 3*1024
// byte stack (better).
//
// Degenerate Slots:
// -----------------
//
// Relying entirely on first-use of stack slots is problematic,
// however, due to the fact that optimizations can sometimes migrate
// uses of a variable outside of its lifetime start/end region. Here
// is an example:
//
//     int bar() {
//       char b1[1024], b2[1024];
//       if (...) {
//         <uses of b2>
//         return y;
//       } else {
//         <uses of b1>
//         while (...) {
//           char b3[1024];
//           <uses of b3>
//         }
//       }
//     }
//
// Before optimization, the control flow graph for the code above
// might look like the following:
//
//                +------  block 0 -------+
//               0| LIFETIME_START b1, b2 |
//               1| <test 'if' condition> |
//                +-----------------------+
//                   ./              \.
//   +------  block 1 -------+    +------- block 2 -------+
//  2| <uses of b2>          |   3| <uses of b1>          |
//   +-----------------------+    +-----------------------+
//              |                            |
//              |                 +------- block 3 -------+ <-\.
//              |                4| <while condition>     |    |
//              |                 +-----------------------+    |
//              |               /          |                   |
//              |              /  +------- block 4 -------+
//              \             /  5| LIFETIME_START b3     |    |
//               \           /   6| <uses of b3>          |    |
//                \         /    7| LIFETIME_END b3       |    |
//                 \        |    +------------------------+    |
//                  \       |                 \                /
//                +------  block 5 -----+      \---------------
//               8| <cleanupcode>       |
//               9| LIFETIME_END b1, b2 |
//              10| return              |
//                +---------------------+
//
// During optimization, however, it can happen that an instruction
// computing an address in "b3" (for example, a loop-invariant GEP) is
// hoisted up out of the loop from block 4 to block 2.  [Note that
// this is not an actual load from the stack, only an instruction that
// computes the address to be loaded]. If this happens, there is now a
// path leading from the first use of b3 to the return instruction
// that does not encounter the b3 LIFETIME_END, hence b3's lifetime is
// now larger than if we were computing live intervals strictly based
// on lifetime markers. In the example above, this lengthened lifetime
// would mean that it would appear illegal to overlap b3 with b2.
//
// To deal with this such cases, the code in ::collectMarkers() below
// tries to identify "degenerate" slots -- those slots where on a single
// forward pass through the CFG we encounter a first reference to slot
// K before we hit the slot K lifetime start marker. For such slots,
// we fall back on using the lifetime start marker as the beginning of
// the variable's lifetime.  NB: with this implementation, slots can
// appear degenerate in cases where there is unstructured control flow:
//
//    if (q) goto mid;
//    if (x > 9) {
//         int b[100];
//         memcpy(&b[0], ...);
//    mid: b[k] = ...;
//         abc(&b);
//    }
//
// If in RPO ordering chosen to walk the CFG  we happen to visit the b[k]
// before visiting the memcpy block (which will contain the lifetime start
// for "b" then it will appear that 'b' has a degenerate lifetime.

namespace {

/// StackColoring - A machine pass for merging disjoint stack allocations,
/// marked by the LIFETIME_START and LIFETIME_END pseudo instructions.
class StackColoring : public MachineFunctionPass {
  MachineFrameInfo *MFI = nullptr;
  MachineFunction *MF = nullptr;

  /// A class representing liveness information for a single basic block.
  /// Each bit in the BitVector represents the liveness property
  /// for a different stack slot.
  struct BlockLifetimeInfo {
    /// Which slots BEGINs in each basic block.
    BitVector Begin;

    /// Which slots ENDs in each basic block.
    BitVector End;

    /// Which slots are marked as LIVE_IN, coming into each basic block.
    BitVector LiveIn;

    /// Which slots are marked as LIVE_OUT, coming out of each basic block.
    BitVector LiveOut;
  };

  /// Maps active slots (per bit) for each basic block.
  using LivenessMap = DenseMap<const MachineBasicBlock *, BlockLifetimeInfo>;
  LivenessMap BlockLiveness;

  /// Maps serial numbers to basic blocks.
  DenseMap<const MachineBasicBlock *, int> BasicBlocks;

  /// Maps basic blocks to a serial number.
  SmallVector<const MachineBasicBlock *, 8> BasicBlockNumbering;

  /// Maps slots to their use interval. Outside of this interval, slots
  /// values are either dead or `undef` and they will not be written to.
  SmallVector<std::unique_ptr<LiveInterval>, 16> Intervals;

  /// Maps slots to the points where they can become in-use.
  SmallVector<SmallVector<SlotIndex, 4>, 16> LiveStarts;

  /// VNInfo is used for the construction of LiveIntervals.
  VNInfo::Allocator VNInfoAllocator;

  /// SlotIndex analysis object.
  SlotIndexes *Indexes = nullptr;

  /// The list of lifetime markers found. These markers are to be removed
  /// once the coloring is done.
  SmallVector<MachineInstr*, 8> Markers;

  /// Record the FI slots for which we have seen some sort of
  /// lifetime marker (either start or end).
  BitVector InterestingSlots;

  /// FI slots that need to be handled conservatively (for these
  /// slots lifetime-start-on-first-use is disabled).
  BitVector ConservativeSlots;

  /// Number of iterations taken during data flow analysis.
  unsigned NumIterations;

public:
  static char ID;

  StackColoring() : MachineFunctionPass(ID) {
    initializeStackColoringPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnMachineFunction(MachineFunction &Func) override;

private:
  /// Used in collectMarkers
  using BlockBitVecMap = DenseMap<const MachineBasicBlock *, BitVector>;

  /// Debug.
  void dump() const;
  void dumpIntervals() const;
  void dumpBB(MachineBasicBlock *MBB) const;
  void dumpBV(const char *tag, const BitVector &BV) const;

  /// Removes all of the lifetime marker instructions from the function.
  /// \returns true if any markers were removed.
  bool removeAllMarkers();

  /// Scan the machine function and find all of the lifetime markers.
  /// Record the findings in the BEGIN and END vectors.
  /// \returns the number of markers found.
  unsigned collectMarkers(unsigned NumSlot);

  /// Perform the dataflow calculation and calculate the lifetime for each of
  /// the slots, based on the BEGIN/END vectors. Set the LifetimeLIVE_IN and
  /// LifetimeLIVE_OUT maps that represent which stack slots are live coming
  /// in and out blocks.
  void calculateLocalLiveness();

  /// Returns TRUE if we're using the first-use-begins-lifetime method for
  /// this slot (if FALSE, then the start marker is treated as start of lifetime).
  bool applyFirstUse(int Slot) {
    if (!LifetimeStartOnFirstUse || ProtectFromEscapedAllocas)
      return false;
    if (ConservativeSlots.test(Slot))
      return false;
    return true;
  }

  /// Examines the specified instruction and returns TRUE if the instruction
  /// represents the start or end of an interesting lifetime. The slot or slots
  /// starting or ending are added to the vector "slots" and "isStart" is set
  /// accordingly.
  /// \returns True if inst contains a lifetime start or end
  bool isLifetimeStartOrEnd(const MachineInstr &MI,
                            SmallVector<int, 4> &slots,
                            bool &isStart);

  /// Construct the LiveIntervals for the slots.
  void calculateLiveIntervals(unsigned NumSlots);

  /// Go over the machine function and change instructions which use stack
  /// slots to use the joint slots.
  void remapInstructions(DenseMap<int, int> &SlotRemap);

  /// The input program may contain instructions which are not inside lifetime
  /// markers. This can happen due to a bug in the compiler or due to a bug in
  /// user code (for example, returning a reference to a local variable).
  /// This procedure checks all of the instructions in the function and
  /// invalidates lifetime ranges which do not contain all of the instructions
  /// which access that frame slot.
  void removeInvalidSlotRanges();

  /// Map entries which point to other entries to their destination.
  ///   A->B->C becomes A->C.
  void expungeSlotMap(DenseMap<int, int> &SlotRemap, unsigned NumSlots);
};

} // end anonymous namespace

char StackColoring::ID = 0;

char &llvm::StackColoringID = StackColoring::ID;

INITIALIZE_PASS_BEGIN(StackColoring, DEBUG_TYPE,
                      "Merge disjoint stack slots", false, false)
INITIALIZE_PASS_DEPENDENCY(SlotIndexesWrapperPass)
INITIALIZE_PASS_END(StackColoring, DEBUG_TYPE,
                    "Merge disjoint stack slots", false, false)

void StackColoring::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<SlotIndexesWrapperPass>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void StackColoring::dumpBV(const char *tag,
                                            const BitVector &BV) const {
  dbgs() << tag << " : { ";
  for (unsigned I = 0, E = BV.size(); I != E; ++I)
    dbgs() << BV.test(I) << " ";
  dbgs() << "}\n";
}

LLVM_DUMP_METHOD void StackColoring::dumpBB(MachineBasicBlock *MBB) const {
  LivenessMap::const_iterator BI = BlockLiveness.find(MBB);
  assert(BI != BlockLiveness.end() && "Block not found");
  const BlockLifetimeInfo &BlockInfo = BI->second;

  dumpBV("BEGIN", BlockInfo.Begin);
  dumpBV("END", BlockInfo.End);
  dumpBV("LIVE_IN", BlockInfo.LiveIn);
  dumpBV("LIVE_OUT", BlockInfo.LiveOut);
}

LLVM_DUMP_METHOD void StackColoring::dump() const {
  for (MachineBasicBlock *MBB : depth_first(MF)) {
    dbgs() << "Inspecting block #" << MBB->getNumber() << " ["
           << MBB->getName() << "]\n";
    dumpBB(MBB);
  }
}

LLVM_DUMP_METHOD void StackColoring::dumpIntervals() const {
  for (unsigned I = 0, E = Intervals.size(); I != E; ++I) {
    dbgs() << "Interval[" << I << "]:\n";
    Intervals[I]->dump();
  }
}
#endif

static inline int getStartOrEndSlot(const MachineInstr &MI)
{
  assert((MI.getOpcode() == TargetOpcode::LIFETIME_START ||
          MI.getOpcode() == TargetOpcode::LIFETIME_END) &&
         "Expected LIFETIME_START or LIFETIME_END op");
  const MachineOperand &MO = MI.getOperand(0);
  int Slot = MO.getIndex();
  if (Slot >= 0)
    return Slot;
  return -1;
}

// At the moment the only way to end a variable lifetime is with
// a VARIABLE_LIFETIME op (which can't contain a start). If things
// change and the IR allows for a single inst that both begins
// and ends lifetime(s), this interface will need to be reworked.
bool StackColoring::isLifetimeStartOrEnd(const MachineInstr &MI,
                                         SmallVector<int, 4> &slots,
                                         bool &isStart) {
  if (MI.getOpcode() == TargetOpcode::LIFETIME_START ||
      MI.getOpcode() == TargetOpcode::LIFETIME_END) {
    int Slot = getStartOrEndSlot(MI);
    if (Slot < 0)
      return false;
    if (!InterestingSlots.test(Slot))
      return false;
    slots.push_back(Slot);
    if (MI.getOpcode() == TargetOpcode::LIFETIME_END) {
      isStart = false;
      return true;
    }
    if (!applyFirstUse(Slot)) {
      isStart = true;
      return true;
    }
  } else if (LifetimeStartOnFirstUse && !ProtectFromEscapedAllocas) {
    if (!MI.isDebugInstr()) {
      bool found = false;
      for (const MachineOperand &MO : MI.operands()) {
        if (!MO.isFI())
          continue;
        int Slot = MO.getIndex();
        if (Slot<0)
          continue;
        if (InterestingSlots.test(Slot) && applyFirstUse(Slot)) {
          slots.push_back(Slot);
          found = true;
        }
      }
      if (found) {
        isStart = true;
        return true;
      }
    }
  }
  return false;
}

unsigned StackColoring::collectMarkers(unsigned NumSlot) {
  unsigned MarkersFound = 0;
  BlockBitVecMap SeenStartMap;
  InterestingSlots.clear();
  InterestingSlots.resize(NumSlot);
  ConservativeSlots.clear();
  ConservativeSlots.resize(NumSlot);

  // number of start and end lifetime ops for each slot
  SmallVector<int, 8> NumStartLifetimes(NumSlot, 0);
  SmallVector<int, 8> NumEndLifetimes(NumSlot, 0);

  // Step 1: collect markers and populate the "InterestingSlots"
  // and "ConservativeSlots" sets.
  for (MachineBasicBlock *MBB : depth_first(MF)) {
    // Compute the set of slots for which we've seen a START marker but have
    // not yet seen an END marker at this point in the walk (e.g. on entry
    // to this bb).
    BitVector BetweenStartEnd;
    BetweenStartEnd.resize(NumSlot);
    for (const MachineBasicBlock *Pred : MBB->predecessors()) {
      BlockBitVecMap::const_iterator I = SeenStartMap.find(Pred);
      if (I != SeenStartMap.end()) {
        BetweenStartEnd |= I->second;
      }
    }

    // Walk the instructions in the block to look for start/end ops.
    for (MachineInstr &MI : *MBB) {
      if (MI.isDebugInstr())
        continue;
      if (MI.getOpcode() == TargetOpcode::LIFETIME_START ||
          MI.getOpcode() == TargetOpcode::LIFETIME_END) {
        int Slot = getStartOrEndSlot(MI);
        if (Slot < 0)
          continue;
        InterestingSlots.set(Slot);
        if (MI.getOpcode() == TargetOpcode::LIFETIME_START) {
          BetweenStartEnd.set(Slot);
          NumStartLifetimes[Slot] += 1;
        } else {
          BetweenStartEnd.reset(Slot);
          NumEndLifetimes[Slot] += 1;
        }
        const AllocaInst *Allocation = MFI->getObjectAllocation(Slot);
        if (Allocation) {
          LLVM_DEBUG(dbgs() << "Found a lifetime ");
          LLVM_DEBUG(dbgs() << (MI.getOpcode() == TargetOpcode::LIFETIME_START
                                    ? "start"
                                    : "end"));
          LLVM_DEBUG(dbgs() << " marker for slot #" << Slot);
          LLVM_DEBUG(dbgs()
                     << " with allocation: " << Allocation->getName() << "\n");
        }
        Markers.push_back(&MI);
        MarkersFound += 1;
      } else {
        for (const MachineOperand &MO : MI.operands()) {
          if (!MO.isFI())
            continue;
          int Slot = MO.getIndex();
          if (Slot < 0)
            continue;
          if (! BetweenStartEnd.test(Slot)) {
            ConservativeSlots.set(Slot);
          }
        }
      }
    }
    BitVector &SeenStart = SeenStartMap[MBB];
    SeenStart |= BetweenStartEnd;
  }
  if (!MarkersFound) {
    return 0;
  }

  // PR27903: slots with multiple start or end lifetime ops are not
  // safe to enable for "lifetime-start-on-first-use".
  for (unsigned slot = 0; slot < NumSlot; ++slot) {
    if (NumStartLifetimes[slot] > 1 || NumEndLifetimes[slot] > 1)
      ConservativeSlots.set(slot);
  }

  // The write to the catch object by the personality function is not propely
  // modeled in IR: It happens before any cleanuppads are executed, even if the
  // first mention of the catch object is in a catchpad. As such, mark catch
  // object slots as conservative, so they are excluded from first-use analysis.
  if (WinEHFuncInfo *EHInfo = MF->getWinEHFuncInfo())
    for (WinEHTryBlockMapEntry &TBME : EHInfo->TryBlockMap)
      for (WinEHHandlerType &H : TBME.HandlerArray)
        if (H.CatchObj.FrameIndex != std::numeric_limits<int>::max() &&
            H.CatchObj.FrameIndex >= 0)
          ConservativeSlots.set(H.CatchObj.FrameIndex);

  LLVM_DEBUG(dumpBV("Conservative slots", ConservativeSlots));

  // Step 2: compute begin/end sets for each block

  // NOTE: We use a depth-first iteration to ensure that we obtain a
  // deterministic numbering.
  for (MachineBasicBlock *MBB : depth_first(MF)) {
    // Assign a serial number to this basic block.
    BasicBlocks[MBB] = BasicBlockNumbering.size();
    BasicBlockNumbering.push_back(MBB);

    // Keep a reference to avoid repeated lookups.
    BlockLifetimeInfo &BlockInfo = BlockLiveness[MBB];

    BlockInfo.Begin.resize(NumSlot);
    BlockInfo.End.resize(NumSlot);

    SmallVector<int, 4> slots;
    for (MachineInstr &MI : *MBB) {
      bool isStart = false;
      slots.clear();
      if (isLifetimeStartOrEnd(MI, slots, isStart)) {
        if (!isStart) {
          assert(slots.size() == 1 && "unexpected: MI ends multiple slots");
          int Slot = slots[0];
          if (BlockInfo.Begin.test(Slot)) {
            BlockInfo.Begin.reset(Slot);
          }
          BlockInfo.End.set(Slot);
        } else {
          for (auto Slot : slots) {
            LLVM_DEBUG(dbgs() << "Found a use of slot #" << Slot);
            LLVM_DEBUG(dbgs()
                       << " at " << printMBBReference(*MBB) << " index ");
            LLVM_DEBUG(Indexes->getInstructionIndex(MI).print(dbgs()));
            const AllocaInst *Allocation = MFI->getObjectAllocation(Slot);
            if (Allocation) {
              LLVM_DEBUG(dbgs()
                         << " with allocation: " << Allocation->getName());
            }
            LLVM_DEBUG(dbgs() << "\n");
            if (BlockInfo.End.test(Slot)) {
              BlockInfo.End.reset(Slot);
            }
            BlockInfo.Begin.set(Slot);
          }
        }
      }
    }
  }

  // Update statistics.
  NumMarkerSeen += MarkersFound;
  return MarkersFound;
}

void StackColoring::calculateLocalLiveness() {
  unsigned NumIters = 0;
  bool changed = true;
  // Create BitVector outside the loop and reuse them to avoid repeated heap
  // allocations.
  BitVector LocalLiveIn;
  BitVector LocalLiveOut;
  while (changed) {
    changed = false;
    ++NumIters;

    for (const MachineBasicBlock *BB : BasicBlockNumbering) {
      // Use an iterator to avoid repeated lookups.
      LivenessMap::iterator BI = BlockLiveness.find(BB);
      assert(BI != BlockLiveness.end() && "Block not found");
      BlockLifetimeInfo &BlockInfo = BI->second;

      // Compute LiveIn by unioning together the LiveOut sets of all preds.
      LocalLiveIn.clear();
      for (MachineBasicBlock *Pred : BB->predecessors()) {
        LivenessMap::const_iterator I = BlockLiveness.find(Pred);
        // PR37130: transformations prior to stack coloring can
        // sometimes leave behind statically unreachable blocks; these
        // can be safely skipped here.
        if (I != BlockLiveness.end())
          LocalLiveIn |= I->second.LiveOut;
      }

      // Compute LiveOut by subtracting out lifetimes that end in this
      // block, then adding in lifetimes that begin in this block.  If
      // we have both BEGIN and END markers in the same basic block
      // then we know that the BEGIN marker comes after the END,
      // because we already handle the case where the BEGIN comes
      // before the END when collecting the markers (and building the
      // BEGIN/END vectors).
      LocalLiveOut = LocalLiveIn;
      LocalLiveOut.reset(BlockInfo.End);
      LocalLiveOut |= BlockInfo.Begin;

      // Update block LiveIn set, noting whether it has changed.
      if (LocalLiveIn.test(BlockInfo.LiveIn)) {
        changed = true;
        BlockInfo.LiveIn |= LocalLiveIn;
      }

      // Update block LiveOut set, noting whether it has changed.
      if (LocalLiveOut.test(BlockInfo.LiveOut)) {
        changed = true;
        BlockInfo.LiveOut |= LocalLiveOut;
      }
    }
  } // while changed.

  NumIterations = NumIters;
}

void StackColoring::calculateLiveIntervals(unsigned NumSlots) {
  SmallVector<SlotIndex, 16> Starts;
  SmallVector<bool, 16> DefinitelyInUse;

  // For each block, find which slots are active within this block
  // and update the live intervals.
  for (const MachineBasicBlock &MBB : *MF) {
    Starts.clear();
    Starts.resize(NumSlots);
    DefinitelyInUse.clear();
    DefinitelyInUse.resize(NumSlots);

    // Start the interval of the slots that we previously found to be 'in-use'.
    BlockLifetimeInfo &MBBLiveness = BlockLiveness[&MBB];
    for (int pos = MBBLiveness.LiveIn.find_first(); pos != -1;
         pos = MBBLiveness.LiveIn.find_next(pos)) {
      Starts[pos] = Indexes->getMBBStartIdx(&MBB);
    }

    // Create the interval for the basic blocks containing lifetime begin/end.
    for (const MachineInstr &MI : MBB) {
      SmallVector<int, 4> slots;
      bool IsStart = false;
      if (!isLifetimeStartOrEnd(MI, slots, IsStart))
        continue;
      SlotIndex ThisIndex = Indexes->getInstructionIndex(MI);
      for (auto Slot : slots) {
        if (IsStart) {
          // If a slot is already definitely in use, we don't have to emit
          // a new start marker because there is already a pre-existing
          // one.
          if (!DefinitelyInUse[Slot]) {
            LiveStarts[Slot].push_back(ThisIndex);
            DefinitelyInUse[Slot] = true;
          }
          if (!Starts[Slot].isValid())
            Starts[Slot] = ThisIndex;
        } else {
          if (Starts[Slot].isValid()) {
            VNInfo *VNI = Intervals[Slot]->getValNumInfo(0);
            Intervals[Slot]->addSegment(
                LiveInterval::Segment(Starts[Slot], ThisIndex, VNI));
            Starts[Slot] = SlotIndex(); // Invalidate the start index
            DefinitelyInUse[Slot] = false;
          }
        }
      }
    }

    // Finish up started segments
    for (unsigned i = 0; i < NumSlots; ++i) {
      if (!Starts[i].isValid())
        continue;

      SlotIndex EndIdx = Indexes->getMBBEndIdx(&MBB);
      VNInfo *VNI = Intervals[i]->getValNumInfo(0);
      Intervals[i]->addSegment(LiveInterval::Segment(Starts[i], EndIdx, VNI));
    }
  }
}

bool StackColoring::removeAllMarkers() {
  unsigned Count = 0;
  for (MachineInstr *MI : Markers) {
    MI->eraseFromParent();
    Count++;
  }
  Markers.clear();

  LLVM_DEBUG(dbgs() << "Removed " << Count << " markers.\n");
  return Count;
}

void StackColoring::remapInstructions(DenseMap<int, int> &SlotRemap) {
  unsigned FixedInstr = 0;
  unsigned FixedMemOp = 0;
  unsigned FixedDbg = 0;

  // Remap debug information that refers to stack slots.
  for (auto &VI : MF->getVariableDbgInfo()) {
    if (!VI.Var || !VI.inStackSlot())
      continue;
    int Slot = VI.getStackSlot();
    if (SlotRemap.count(Slot)) {
      LLVM_DEBUG(dbgs() << "Remapping debug info for ["
                        << cast<DILocalVariable>(VI.Var)->getName() << "].\n");
      VI.updateStackSlot(SlotRemap[Slot]);
      FixedDbg++;
    }
  }

  // Keep a list of *allocas* which need to be remapped.
  DenseMap<const AllocaInst*, const AllocaInst*> Allocas;

  // Keep a list of allocas which has been affected by the remap.
  SmallPtrSet<const AllocaInst*, 32> MergedAllocas;

  for (const std::pair<int, int> &SI : SlotRemap) {
    const AllocaInst *From = MFI->getObjectAllocation(SI.first);
    const AllocaInst *To = MFI->getObjectAllocation(SI.second);
    assert(To && From && "Invalid allocation object");
    Allocas[From] = To;

    // If From is before wo, its possible that there is a use of From between
    // them.
    if (From->comesBefore(To))
      const_cast<AllocaInst*>(To)->moveBefore(const_cast<AllocaInst*>(From));

    // AA might be used later for instruction scheduling, and we need it to be
    // able to deduce the correct aliasing releationships between pointers
    // derived from the alloca being remapped and the target of that remapping.
    // The only safe way, without directly informing AA about the remapping
    // somehow, is to directly update the IR to reflect the change being made
    // here.
    Instruction *Inst = const_cast<AllocaInst *>(To);
    if (From->getType() != To->getType()) {
      BitCastInst *Cast = new BitCastInst(Inst, From->getType());
      Cast->insertAfter(Inst);
      Inst = Cast;
    }

    // We keep both slots to maintain AliasAnalysis metadata later.
    MergedAllocas.insert(From);
    MergedAllocas.insert(To);

    // Transfer the stack protector layout tag, but make sure that SSPLK_AddrOf
    // does not overwrite SSPLK_SmallArray or SSPLK_LargeArray, and make sure
    // that SSPLK_SmallArray does not overwrite SSPLK_LargeArray.
    MachineFrameInfo::SSPLayoutKind FromKind
        = MFI->getObjectSSPLayout(SI.first);
    MachineFrameInfo::SSPLayoutKind ToKind = MFI->getObjectSSPLayout(SI.second);
    if (FromKind != MachineFrameInfo::SSPLK_None &&
        (ToKind == MachineFrameInfo::SSPLK_None ||
         (ToKind != MachineFrameInfo::SSPLK_LargeArray &&
          FromKind != MachineFrameInfo::SSPLK_AddrOf)))
      MFI->setObjectSSPLayout(SI.second, FromKind);

    // The new alloca might not be valid in a llvm.dbg.declare for this
    // variable, so poison out the use to make the verifier happy.
    AllocaInst *FromAI = const_cast<AllocaInst *>(From);
    if (FromAI->isUsedByMetadata())
      ValueAsMetadata::handleRAUW(FromAI, PoisonValue::get(FromAI->getType()));
    for (auto &Use : FromAI->uses()) {
      if (BitCastInst *BCI = dyn_cast<BitCastInst>(Use.get()))
        if (BCI->isUsedByMetadata())
          ValueAsMetadata::handleRAUW(BCI, PoisonValue::get(BCI->getType()));
    }

    // Note that this will not replace uses in MMOs (which we'll update below),
    // or anywhere else (which is why we won't delete the original
    // instruction).
    FromAI->replaceAllUsesWith(Inst);
  }

  // Remap all instructions to the new stack slots.
  std::vector<std::vector<MachineMemOperand *>> SSRefs(
      MFI->getObjectIndexEnd());
  for (MachineBasicBlock &BB : *MF)
    for (MachineInstr &I : BB) {
      // Skip lifetime markers. We'll remove them soon.
      if (I.getOpcode() == TargetOpcode::LIFETIME_START ||
          I.getOpcode() == TargetOpcode::LIFETIME_END)
        continue;

      // Update the MachineMemOperand to use the new alloca.
      for (MachineMemOperand *MMO : I.memoperands()) {
        // We've replaced IR-level uses of the remapped allocas, so we only
        // need to replace direct uses here.
        const AllocaInst *AI = dyn_cast_or_null<AllocaInst>(MMO->getValue());
        if (!AI)
          continue;

        if (!Allocas.count(AI))
          continue;

        MMO->setValue(Allocas[AI]);
        FixedMemOp++;
      }

      // Update all of the machine instruction operands.
      for (MachineOperand &MO : I.operands()) {
        if (!MO.isFI())
          continue;
        int FromSlot = MO.getIndex();

        // Don't touch arguments.
        if (FromSlot<0)
          continue;

        // Only look at mapped slots.
        if (!SlotRemap.count(FromSlot))
          continue;

        // In a debug build, check that the instruction that we are modifying is
        // inside the expected live range. If the instruction is not inside
        // the calculated range then it means that the alloca usage moved
        // outside of the lifetime markers, or that the user has a bug.
        // NOTE: Alloca address calculations which happen outside the lifetime
        // zone are okay, despite the fact that we don't have a good way
        // for validating all of the usages of the calculation.
#ifndef NDEBUG
        bool TouchesMemory = I.mayLoadOrStore();
        // If we *don't* protect the user from escaped allocas, don't bother
        // validating the instructions.
        if (!I.isDebugInstr() && TouchesMemory && ProtectFromEscapedAllocas) {
          SlotIndex Index = Indexes->getInstructionIndex(I);
          const LiveInterval *Interval = &*Intervals[FromSlot];
          assert(Interval->find(Index) != Interval->end() &&
                 "Found instruction usage outside of live range.");
        }
#endif

        // Fix the machine instructions.
        int ToSlot = SlotRemap[FromSlot];
        MO.setIndex(ToSlot);
        FixedInstr++;
      }

      // We adjust AliasAnalysis information for merged stack slots.
      SmallVector<MachineMemOperand *, 2> NewMMOs;
      bool ReplaceMemOps = false;
      for (MachineMemOperand *MMO : I.memoperands()) {
        // Collect MachineMemOperands which reference
        // FixedStackPseudoSourceValues with old frame indices.
        if (const auto *FSV = dyn_cast_or_null<FixedStackPseudoSourceValue>(
                MMO->getPseudoValue())) {
          int FI = FSV->getFrameIndex();
          auto To = SlotRemap.find(FI);
          if (To != SlotRemap.end())
            SSRefs[FI].push_back(MMO);
        }

        // If this memory location can be a slot remapped here,
        // we remove AA information.
        bool MayHaveConflictingAAMD = false;
        if (MMO->getAAInfo()) {
          if (const Value *MMOV = MMO->getValue()) {
            SmallVector<Value *, 4> Objs;
            getUnderlyingObjectsForCodeGen(MMOV, Objs);

            if (Objs.empty())
              MayHaveConflictingAAMD = true;
            else
              for (Value *V : Objs) {
                // If this memory location comes from a known stack slot
                // that is not remapped, we continue checking.
                // Otherwise, we need to invalidate AA infomation.
                const AllocaInst *AI = dyn_cast_or_null<AllocaInst>(V);
                if (AI && MergedAllocas.count(AI)) {
                  MayHaveConflictingAAMD = true;
                  break;
                }
              }
          }
        }
        if (MayHaveConflictingAAMD) {
          NewMMOs.push_back(MF->getMachineMemOperand(MMO, AAMDNodes()));
          ReplaceMemOps = true;
        } else {
          NewMMOs.push_back(MMO);
        }
      }

      // If any memory operand is updated, set memory references of
      // this instruction.
      if (ReplaceMemOps)
        I.setMemRefs(*MF, NewMMOs);
    }

  // Rewrite MachineMemOperands that reference old frame indices.
  for (auto E : enumerate(SSRefs))
    if (!E.value().empty()) {
      const PseudoSourceValue *NewSV =
          MF->getPSVManager().getFixedStack(SlotRemap.find(E.index())->second);
      for (MachineMemOperand *Ref : E.value())
        Ref->setValue(NewSV);
    }

  // Update the location of C++ catch objects for the MSVC personality routine.
  if (WinEHFuncInfo *EHInfo = MF->getWinEHFuncInfo())
    for (WinEHTryBlockMapEntry &TBME : EHInfo->TryBlockMap)
      for (WinEHHandlerType &H : TBME.HandlerArray)
        if (H.CatchObj.FrameIndex != std::numeric_limits<int>::max() &&
            SlotRemap.count(H.CatchObj.FrameIndex))
          H.CatchObj.FrameIndex = SlotRemap[H.CatchObj.FrameIndex];

  LLVM_DEBUG(dbgs() << "Fixed " << FixedMemOp << " machine memory operands.\n");
  LLVM_DEBUG(dbgs() << "Fixed " << FixedDbg << " debug locations.\n");
  LLVM_DEBUG(dbgs() << "Fixed " << FixedInstr << " machine instructions.\n");
  (void) FixedMemOp;
  (void) FixedDbg;
  (void) FixedInstr;
}

void StackColoring::removeInvalidSlotRanges() {
  for (MachineBasicBlock &BB : *MF)
    for (MachineInstr &I : BB) {
      if (I.getOpcode() == TargetOpcode::LIFETIME_START ||
          I.getOpcode() == TargetOpcode::LIFETIME_END || I.isDebugInstr())
        continue;

      // Some intervals are suspicious! In some cases we find address
      // calculations outside of the lifetime zone, but not actual memory
      // read or write. Memory accesses outside of the lifetime zone are a clear
      // violation, but address calculations are okay. This can happen when
      // GEPs are hoisted outside of the lifetime zone.
      // So, in here we only check instructions which can read or write memory.
      if (!I.mayLoad() && !I.mayStore())
        continue;

      // Check all of the machine operands.
      for (const MachineOperand &MO : I.operands()) {
        if (!MO.isFI())
          continue;

        int Slot = MO.getIndex();

        if (Slot<0)
          continue;

        if (Intervals[Slot]->empty())
          continue;

        // Check that the used slot is inside the calculated lifetime range.
        // If it is not, warn about it and invalidate the range.
        LiveInterval *Interval = &*Intervals[Slot];
        SlotIndex Index = Indexes->getInstructionIndex(I);
        if (Interval->find(Index) == Interval->end()) {
          Interval->clear();
          LLVM_DEBUG(dbgs() << "Invalidating range #" << Slot << "\n");
          EscapedAllocas++;
        }
      }
    }
}

void StackColoring::expungeSlotMap(DenseMap<int, int> &SlotRemap,
                                   unsigned NumSlots) {
  // Expunge slot remap map.
  for (unsigned i=0; i < NumSlots; ++i) {
    // If we are remapping i
    if (SlotRemap.count(i)) {
      int Target = SlotRemap[i];
      // As long as our target is mapped to something else, follow it.
      while (SlotRemap.count(Target)) {
        Target = SlotRemap[Target];
        SlotRemap[i] = Target;
      }
    }
  }
}

bool StackColoring::runOnMachineFunction(MachineFunction &Func) {
  LLVM_DEBUG(dbgs() << "********** Stack Coloring **********\n"
                    << "********** Function: " << Func.getName() << '\n');
  MF = &Func;
  MFI = &MF->getFrameInfo();
  Indexes = &getAnalysis<SlotIndexesWrapperPass>().getSI();
  BlockLiveness.clear();
  BasicBlocks.clear();
  BasicBlockNumbering.clear();
  Markers.clear();
  Intervals.clear();
  LiveStarts.clear();
  VNInfoAllocator.Reset();

  unsigned NumSlots = MFI->getObjectIndexEnd();

  // If there are no stack slots then there are no markers to remove.
  if (!NumSlots)
    return false;

  SmallVector<int, 8> SortedSlots;
  SortedSlots.reserve(NumSlots);
  Intervals.reserve(NumSlots);
  LiveStarts.resize(NumSlots);

  unsigned NumMarkers = collectMarkers(NumSlots);

  unsigned TotalSize = 0;
  LLVM_DEBUG(dbgs() << "Found " << NumMarkers << " markers and " << NumSlots
                    << " slots\n");
  LLVM_DEBUG(dbgs() << "Slot structure:\n");

  for (int i=0; i < MFI->getObjectIndexEnd(); ++i) {
    LLVM_DEBUG(dbgs() << "Slot #" << i << " - " << MFI->getObjectSize(i)
                      << " bytes.\n");
    TotalSize += MFI->getObjectSize(i);
  }

  LLVM_DEBUG(dbgs() << "Total Stack size: " << TotalSize << " bytes\n\n");

  // Don't continue because there are not enough lifetime markers, or the
  // stack is too small, or we are told not to optimize the slots.
  if (NumMarkers < 2 || TotalSize < 16 || DisableColoring ||
      skipFunction(Func.getFunction())) {
    LLVM_DEBUG(dbgs() << "Will not try to merge slots.\n");
    return removeAllMarkers();
  }

  for (unsigned i=0; i < NumSlots; ++i) {
    std::unique_ptr<LiveInterval> LI(new LiveInterval(i, 0));
    LI->getNextValue(Indexes->getZeroIndex(), VNInfoAllocator);
    Intervals.push_back(std::move(LI));
    SortedSlots.push_back(i);
  }

  // Calculate the liveness of each block.
  calculateLocalLiveness();
  LLVM_DEBUG(dbgs() << "Dataflow iterations: " << NumIterations << "\n");
  LLVM_DEBUG(dump());

  // Propagate the liveness information.
  calculateLiveIntervals(NumSlots);
  LLVM_DEBUG(dumpIntervals());

  // Search for allocas which are used outside of the declared lifetime
  // markers.
  if (ProtectFromEscapedAllocas)
    removeInvalidSlotRanges();

  // Maps old slots to new slots.
  DenseMap<int, int> SlotRemap;
  unsigned RemovedSlots = 0;
  unsigned ReducedSize = 0;

  // Do not bother looking at empty intervals.
  for (unsigned I = 0; I < NumSlots; ++I) {
    if (Intervals[SortedSlots[I]]->empty())
      SortedSlots[I] = -1;
  }

  // This is a simple greedy algorithm for merging allocas. First, sort the
  // slots, placing the largest slots first. Next, perform an n^2 scan and look
  // for disjoint slots. When you find disjoint slots, merge the smaller one
  // into the bigger one and update the live interval. Remove the small alloca
  // and continue.

  // Sort the slots according to their size. Place unused slots at the end.
  // Use stable sort to guarantee deterministic code generation.
  llvm::stable_sort(SortedSlots, [this](int LHS, int RHS) {
    // We use -1 to denote a uninteresting slot. Place these slots at the end.
    if (LHS == -1)
      return false;
    if (RHS == -1)
      return true;
    // Sort according to size.
    return MFI->getObjectSize(LHS) > MFI->getObjectSize(RHS);
  });

  for (auto &s : LiveStarts)
    llvm::sort(s);

  bool Changed = true;
  while (Changed) {
    Changed = false;
    for (unsigned I = 0; I < NumSlots; ++I) {
      if (SortedSlots[I] == -1)
        continue;

      for (unsigned J=I+1; J < NumSlots; ++J) {
        if (SortedSlots[J] == -1)
          continue;

        int FirstSlot = SortedSlots[I];
        int SecondSlot = SortedSlots[J];

        // Objects with different stack IDs cannot be merged.
        if (MFI->getStackID(FirstSlot) != MFI->getStackID(SecondSlot))
          continue;

        LiveInterval *First = &*Intervals[FirstSlot];
        LiveInterval *Second = &*Intervals[SecondSlot];
        auto &FirstS = LiveStarts[FirstSlot];
        auto &SecondS = LiveStarts[SecondSlot];
        assert(!First->empty() && !Second->empty() && "Found an empty range");

        // Merge disjoint slots. This is a little bit tricky - see the
        // Implementation Notes section for an explanation.
        if (!First->isLiveAtIndexes(SecondS) &&
            !Second->isLiveAtIndexes(FirstS)) {
          Changed = true;
          First->MergeSegmentsInAsValue(*Second, First->getValNumInfo(0));

          int OldSize = FirstS.size();
          FirstS.append(SecondS.begin(), SecondS.end());
          auto Mid = FirstS.begin() + OldSize;
          std::inplace_merge(FirstS.begin(), Mid, FirstS.end());

          SlotRemap[SecondSlot] = FirstSlot;
          SortedSlots[J] = -1;
          LLVM_DEBUG(dbgs() << "Merging #" << FirstSlot << " and slots #"
                            << SecondSlot << " together.\n");
          Align MaxAlignment = std::max(MFI->getObjectAlign(FirstSlot),
                                        MFI->getObjectAlign(SecondSlot));

          assert(MFI->getObjectSize(FirstSlot) >=
                 MFI->getObjectSize(SecondSlot) &&
                 "Merging a small object into a larger one");

          RemovedSlots+=1;
          ReducedSize += MFI->getObjectSize(SecondSlot);
          MFI->setObjectAlignment(FirstSlot, MaxAlignment);
          MFI->RemoveStackObject(SecondSlot);
        }
      }
    }
  }// While changed.

  // Record statistics.
  StackSpaceSaved += ReducedSize;
  StackSlotMerged += RemovedSlots;
  LLVM_DEBUG(dbgs() << "Merge " << RemovedSlots << " slots. Saved "
                    << ReducedSize << " bytes\n");

  // Scan the entire function and update all machine operands that use frame
  // indices to use the remapped frame index.
  if (!SlotRemap.empty()) {
    expungeSlotMap(SlotRemap, NumSlots);
    remapInstructions(SlotRemap);
  }

  return removeAllMarkers();
}

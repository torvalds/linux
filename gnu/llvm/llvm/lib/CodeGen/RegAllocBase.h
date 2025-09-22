//===- RegAllocBase.h - basic regalloc interface and driver -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the RegAllocBase class, which is the skeleton of a basic
// register allocation algorithm and interface for extending it. It provides the
// building blocks on which to construct other experimental allocators and test
// the validity of two principles:
//
// - If virtual and physical register liveness is modeled using intervals, then
// on-the-fly interference checking is cheap. Furthermore, interferences can be
// lazily cached and reused.
//
// - Register allocation complexity, and generated code performance is
// determined by the effectiveness of live range splitting rather than optimal
// coloring.
//
// Following the first principle, interfering checking revolves around the
// LiveIntervalUnion data structure.
//
// To fulfill the second principle, the basic allocator provides a driver for
// incremental splitting. It essentially punts on the problem of register
// coloring, instead driving the assignment of virtual to physical registers by
// the cost of splitting. The basic allocator allows for heuristic reassignment
// of registers, if a more sophisticated allocator chooses to do that.
//
// This framework provides a way to engineer the compile time vs. code
// quality trade-off without relying on a particular theoretical solver.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_REGALLOCBASE_H
#define LLVM_LIB_CODEGEN_REGALLOCBASE_H

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegAllocCommon.h"
#include "llvm/CodeGen/RegisterClassInfo.h"

namespace llvm {

class LiveInterval;
class LiveIntervals;
class LiveRegMatrix;
class MachineInstr;
class MachineRegisterInfo;
template<typename T> class SmallVectorImpl;
class Spiller;
class TargetRegisterInfo;
class VirtRegMap;

/// RegAllocBase provides the register allocation driver and interface that can
/// be extended to add interesting heuristics.
///
/// Register allocators must override the selectOrSplit() method to implement
/// live range splitting. They must also override enqueue/dequeue to provide an
/// assignment order.
class RegAllocBase {
  virtual void anchor();

protected:
  const TargetRegisterInfo *TRI = nullptr;
  MachineRegisterInfo *MRI = nullptr;
  VirtRegMap *VRM = nullptr;
  LiveIntervals *LIS = nullptr;
  LiveRegMatrix *Matrix = nullptr;
  RegisterClassInfo RegClassInfo;

private:
  /// Private, callees should go through shouldAllocateRegister
  const RegAllocFilterFunc shouldAllocateRegisterImpl;

protected:
  /// Inst which is a def of an original reg and whose defs are already all
  /// dead after remat is saved in DeadRemats. The deletion of such inst is
  /// postponed till all the allocations are done, so its remat expr is
  /// always available for the remat of all the siblings of the original reg.
  SmallPtrSet<MachineInstr *, 32> DeadRemats;

  RegAllocBase(const RegAllocFilterFunc F = nullptr)
      : shouldAllocateRegisterImpl(F) {}

  virtual ~RegAllocBase() = default;

  // A RegAlloc pass should call this before allocatePhysRegs.
  void init(VirtRegMap &vrm, LiveIntervals &lis, LiveRegMatrix &mat);

  /// Get whether a given register should be allocated
  bool shouldAllocateRegister(Register Reg) {
    if (!shouldAllocateRegisterImpl)
      return true;
    return shouldAllocateRegisterImpl(*TRI, *MRI, Reg);
  }

  // The top-level driver. The output is a VirtRegMap that us updated with
  // physical register assignments.
  void allocatePhysRegs();

  // Include spiller post optimization and removing dead defs left because of
  // rematerialization.
  virtual void postOptimization();

  // Get a temporary reference to a Spiller instance.
  virtual Spiller &spiller() = 0;

  /// enqueue - Add VirtReg to the priority queue of unassigned registers.
  virtual void enqueueImpl(const LiveInterval *LI) = 0;

  /// enqueue - Add VirtReg to the priority queue of unassigned registers.
  void enqueue(const LiveInterval *LI);

  /// dequeue - Return the next unassigned register, or NULL.
  virtual const LiveInterval *dequeue() = 0;

  // A RegAlloc pass should override this to provide the allocation heuristics.
  // Each call must guarantee forward progess by returning an available PhysReg
  // or new set of split live virtual registers. It is up to the splitter to
  // converge quickly toward fully spilled live ranges.
  virtual MCRegister selectOrSplit(const LiveInterval &VirtReg,
                                   SmallVectorImpl<Register> &splitLVRs) = 0;

  // Use this group name for NamedRegionTimer.
  static const char TimerGroupName[];
  static const char TimerGroupDescription[];

  /// Method called when the allocator is about to remove a LiveInterval.
  virtual void aboutToRemoveInterval(const LiveInterval &LI) {}

public:
  /// VerifyEnabled - True when -verify-regalloc is given.
  static bool VerifyEnabled;

private:
  void seedLiveRegs();
};

} // end namespace llvm

#endif // LLVM_LIB_CODEGEN_REGALLOCBASE_H

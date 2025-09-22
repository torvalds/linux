//===-- WebAssemblySortRegion.h - WebAssembly Sort SortRegion ----*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief This file implements regions used in CFGSort and CFGStackify.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYSORTREGION_H
#define LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYSORTREGION_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/iterator_range.h"

namespace llvm {

class MachineBasicBlock;
class MachineLoop;
class MachineLoopInfo;
class WebAssemblyException;
class WebAssemblyExceptionInfo;

namespace WebAssembly {

// Wrapper for loops and exceptions
class SortRegion {
public:
  virtual ~SortRegion() = default;
  virtual MachineBasicBlock *getHeader() const = 0;
  virtual bool contains(const MachineBasicBlock *MBB) const = 0;
  virtual unsigned getNumBlocks() const = 0;
  using block_iterator = typename ArrayRef<MachineBasicBlock *>::const_iterator;
  virtual iterator_range<block_iterator> blocks() const = 0;
  virtual bool isLoop() const = 0;
};

template <typename T> class ConcreteSortRegion : public SortRegion {
  const T *Unit;

public:
  ConcreteSortRegion(const T *Unit) : Unit(Unit) {}
  MachineBasicBlock *getHeader() const override { return Unit->getHeader(); }
  bool contains(const MachineBasicBlock *MBB) const override {
    return Unit->contains(MBB);
  }
  unsigned getNumBlocks() const override { return Unit->getNumBlocks(); }
  iterator_range<block_iterator> blocks() const override {
    return Unit->blocks();
  }
  bool isLoop() const override { return false; }
};

// This class has information of nested SortRegions; this is analogous to what
// LoopInfo is for loops.
class SortRegionInfo {
  friend class ConcreteSortRegion<MachineLoopInfo>;
  friend class ConcreteSortRegion<WebAssemblyException>;

  const MachineLoopInfo &MLI;
  const WebAssemblyExceptionInfo &WEI;
  DenseMap<const MachineLoop *, std::unique_ptr<SortRegion>> LoopMap;
  DenseMap<const WebAssemblyException *, std::unique_ptr<SortRegion>>
      ExceptionMap;

public:
  SortRegionInfo(const MachineLoopInfo &MLI,
                 const WebAssemblyExceptionInfo &WEI)
      : MLI(MLI), WEI(WEI) {}

  // Returns a smallest loop or exception that contains MBB
  const SortRegion *getRegionFor(const MachineBasicBlock *MBB);

  // Return the "bottom" block among all blocks dominated by the region
  // (MachineLoop or WebAssemblyException) header. This works when the entity is
  // discontiguous.
  MachineBasicBlock *getBottom(const SortRegion *R);
  MachineBasicBlock *getBottom(const MachineLoop *ML);
  MachineBasicBlock *getBottom(const WebAssemblyException *WE);
};

} // end namespace WebAssembly

} // end namespace llvm

#endif

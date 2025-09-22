//===-- NVPTXMachineFunctionInfo.h - NVPTX-specific Function Info  --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class is attached to a MachineFunction instance and tracks target-
// dependent information
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_NVPTX_NVPTXMACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_NVPTX_NVPTXMACHINEFUNCTIONINFO_H

#include "llvm/CodeGen/MachineFunction.h"

namespace llvm {
class NVPTXMachineFunctionInfo : public MachineFunctionInfo {
private:
  /// Stores a mapping from index to symbol name for removing image handles
  /// on Fermi.
  SmallVector<std::string, 8> ImageHandleList;

public:
  NVPTXMachineFunctionInfo(const Function &F, const TargetSubtargetInfo *STI) {}

  MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const override {
    return DestMF.cloneInfo<NVPTXMachineFunctionInfo>(*this);
  }

  /// Returns the index for the symbol \p Symbol. If the symbol was previously,
  /// added, the same index is returned. Otherwise, the symbol is added and the
  /// new index is returned.
  unsigned getImageHandleSymbolIndex(const char *Symbol) {
    // Is the symbol already present?
    for (unsigned i = 0, e = ImageHandleList.size(); i != e; ++i)
      if (ImageHandleList[i] == std::string(Symbol))
        return i;
    // Nope, insert it
    ImageHandleList.push_back(Symbol);
    return ImageHandleList.size()-1;
  }

  /// Returns the symbol name at the given index.
  const char *getImageHandleSymbol(unsigned Idx) const {
    assert(ImageHandleList.size() > Idx && "Bad index");
    return ImageHandleList[Idx].c_str();
  }
};
}

#endif

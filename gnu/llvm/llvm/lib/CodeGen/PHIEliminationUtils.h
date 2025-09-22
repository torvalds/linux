//=- PHIEliminationUtils.h - Helper functions for PHI elimination -*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_PHIELIMINATIONUTILS_H
#define LLVM_LIB_CODEGEN_PHIELIMINATIONUTILS_H

#include "llvm/CodeGen/MachineBasicBlock.h"

namespace llvm {
    /// findPHICopyInsertPoint - Find a safe place in MBB to insert a copy from
    /// SrcReg when following the CFG edge to SuccMBB. This needs to be after
    /// any def of SrcReg, but before any subsequent point where control flow
    /// might jump out of the basic block.
    MachineBasicBlock::iterator
    findPHICopyInsertPoint(MachineBasicBlock* MBB, MachineBasicBlock* SuccMBB,
                           unsigned SrcReg);
}

#endif

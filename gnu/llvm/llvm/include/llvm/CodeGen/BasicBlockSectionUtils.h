//===- BasicBlockSectionUtils.h - Utilities for basic block sections     --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_BASICBLOCKSECTIONUTILS_H
#define LLVM_CODEGEN_BASICBLOCKSECTIONUTILS_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/CommandLine.h"

namespace llvm {

extern cl::opt<std::string> BBSectionsColdTextPrefix;

class MachineFunction;
class MachineBasicBlock;

using MachineBasicBlockComparator =
    function_ref<bool(const MachineBasicBlock &, const MachineBasicBlock &)>;

void sortBasicBlocksAndUpdateBranches(MachineFunction &MF,
                                      MachineBasicBlockComparator MBBCmp);

void avoidZeroOffsetLandingPad(MachineFunction &MF);

/// This checks if the source of this function has drifted since this binary was
/// profiled previously.
/// For now, we are piggy backing on what PGO does to
/// detect this with instrumented profiles.  PGO emits an hash of the IR and
/// checks if the hash has changed.  Advanced basic block layout is usually done
/// on top of PGO optimized binaries and hence this check works well in
/// practice.
bool hasInstrProfHashMismatch(MachineFunction &MF);

} // end namespace llvm

#endif // LLVM_CODEGEN_BASICBLOCKSECTIONUTILS_H

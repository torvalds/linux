//===- IPO/OpenMPOpt.h - Collection of OpenMP optimizations -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_OPENMPOPT_H
#define LLVM_TRANSFORMS_IPO_OPENMPOPT_H

#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

namespace omp {

/// Summary of a kernel (=entry point for target offloading).
using Kernel = Function *;

/// Set of kernels in the module
using KernelSet = SetVector<Kernel>;

/// Helper to determine if \p M contains OpenMP.
bool containsOpenMP(Module &M);

/// Helper to determine if \p M is a OpenMP target offloading device module.
bool isOpenMPDevice(Module &M);

/// Return true iff \p Fn is an OpenMP GPU kernel; \p Fn has the "kernel"
/// attribute.
bool isOpenMPKernel(Function &Fn);

/// Get OpenMP device kernels in \p M.
KernelSet getDeviceKernels(Module &M);

} // namespace omp

/// OpenMP optimizations pass.
class OpenMPOptPass : public PassInfoMixin<OpenMPOptPass> {
public:
  OpenMPOptPass() = default;
  OpenMPOptPass(ThinOrFullLTOPhase LTOPhase) : LTOPhase(LTOPhase) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

private:
  const ThinOrFullLTOPhase LTOPhase = ThinOrFullLTOPhase::None;
};

class OpenMPOptCGSCCPass : public PassInfoMixin<OpenMPOptCGSCCPass> {
public:
  OpenMPOptCGSCCPass() = default;
  OpenMPOptCGSCCPass(ThinOrFullLTOPhase LTOPhase) : LTOPhase(LTOPhase) {}

  PreservedAnalyses run(LazyCallGraph::SCC &C, CGSCCAnalysisManager &AM,
                        LazyCallGraph &CG, CGSCCUpdateResult &UR);

private:
  const ThinOrFullLTOPhase LTOPhase = ThinOrFullLTOPhase::None;
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_OPENMPOPT_H

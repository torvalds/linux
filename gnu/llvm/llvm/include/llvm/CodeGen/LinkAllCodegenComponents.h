//===- llvm/Codegen/LinkAllCodegenComponents.h ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header file pulls in all codegen related passes for tools like lli and
// llc that need this functionality.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_LINKALLCODEGENCOMPONENTS_H
#define LLVM_CODEGEN_LINKALLCODEGENCOMPONENTS_H

#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/SchedulerRegistry.h"
#include "llvm/Target/TargetMachine.h"
#include <cstdlib>

namespace {
  struct ForceCodegenLinking {
    ForceCodegenLinking() {
      // We must reference the passes in such a way that compilers will not
      // delete it all as dead code, even with whole program optimization,
      // yet is effectively a NO-OP. As the compiler isn't smart enough
      // to know that getenv() never returns -1, this will do the job.
      // This is so that globals in the translation units where these functions
      // are defined are forced to be initialized, populating various
      // registries.
      if (std::getenv("bar") != (char*) -1)
        return;

      (void) llvm::createFastRegisterAllocator();
      (void) llvm::createBasicRegisterAllocator();
      (void) llvm::createGreedyRegisterAllocator();
      (void) llvm::createDefaultPBQPRegisterAllocator();

      (void)llvm::createBURRListDAGScheduler(nullptr,
                                             llvm::CodeGenOptLevel::Default);
      (void)llvm::createSourceListDAGScheduler(nullptr,
                                               llvm::CodeGenOptLevel::Default);
      (void)llvm::createHybridListDAGScheduler(nullptr,
                                               llvm::CodeGenOptLevel::Default);
      (void)llvm::createFastDAGScheduler(nullptr,
                                         llvm::CodeGenOptLevel::Default);
      (void)llvm::createDefaultScheduler(nullptr,
                                         llvm::CodeGenOptLevel::Default);
      (void)llvm::createVLIWDAGScheduler(nullptr,
                                         llvm::CodeGenOptLevel::Default);
    }
  } ForceCodegenLinking; // Force link by creating a global definition.
}

#endif

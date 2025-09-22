//===----- LinkAllIR.h - Reference All VMCore Code --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header file pulls in all the object modules of the VMCore library so
// that tools like llc, opt, and lli can ensure they are linked with all symbols
// from libVMCore.a It should only be used from a tool's main program.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LINKALLIR_H
#define LLVM_LINKALLIR_H

#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Memory.h"
#include "llvm/Support/Mutex.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/Signals.h"
#include <cstdlib>

namespace {
  struct ForceVMCoreLinking {
    ForceVMCoreLinking() {
      // We must reference VMCore in such a way that compilers will not
      // delete it all as dead code, even with whole program optimization,
      // yet is effectively a NO-OP. As the compiler isn't smart enough
      // to know that getenv() never returns -1, this will do the job.
      // This is so that globals in the translation units where these functions
      // are defined are forced to be initialized, populating various
      // registries.
      if (std::getenv("bar") != (char*) -1)
        return;
      llvm::LLVMContext Context;
      (void)new llvm::Module("", Context);
      (void)new llvm::UnreachableInst(Context);
      (void)    llvm::createVerifierPass();
    }
  } ForceVMCoreLinking;
}

#endif

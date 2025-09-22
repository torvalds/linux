//===- llvm/Codegen/LinkAllAsmWriterComponents.h ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header file pulls in all assembler writer related passes for tools like
// llc that need this functionality.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_LINKALLASMWRITERCOMPONENTS_H
#define LLVM_CODEGEN_LINKALLASMWRITERCOMPONENTS_H

#include "llvm/IR/BuiltinGCs.h"
#include <cstdlib>

namespace {
  struct ForceAsmWriterLinking {
    ForceAsmWriterLinking() {
      // We must reference the plug-ins in such a way that compilers will not
      // delete it all as dead code, even with whole program optimization,
      // yet is effectively a NO-OP. As the compiler isn't smart enough
      // to know that getenv() never returns -1, this will do the job.
      // This is so that globals in the translation units where these functions
      // are defined are forced to be initialized, populating various
      // registries.
      if (std::getenv("bar") != (char*) -1)
        return;

      llvm::linkOcamlGCPrinter();
      llvm::linkErlangGCPrinter();

    }
  } ForceAsmWriterLinking; // Force link by creating a global definition.
}

#endif // LLVM_CODEGEN_LINKALLASMWRITERCOMPONENTS_H

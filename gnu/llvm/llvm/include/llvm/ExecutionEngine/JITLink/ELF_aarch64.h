//===--- ELF_aarch64.h - JIT link functions for ELF/aarch64 --*- C++ -*----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//
//
// jit-link functions for ELF/aarch64.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_JITLINK_ELF_AARCH64_H
#define LLVM_EXECUTIONENGINE_JITLINK_ELF_AARCH64_H

#include "llvm/ExecutionEngine/JITLink/JITLink.h"

namespace llvm {
namespace jitlink {

/// Create a LinkGraph from an ELF/aarch64 relocatable object
///
/// Note: The graph does not take ownership of the underlying buffer, nor copy
/// its contents. The caller is responsible for ensuring that the object buffer
/// outlives the graph.
Expected<std::unique_ptr<LinkGraph>>
createLinkGraphFromELFObject_aarch64(MemoryBufferRef ObjectBuffer);

/// jit-link the given object buffer, which must be a ELF aarch64 relocatable
/// object file.
void link_ELF_aarch64(std::unique_ptr<LinkGraph> G,
                      std::unique_ptr<JITLinkContext> Ctx);

} // end namespace jitlink
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_JITLINK_ELF_AARCH64_H

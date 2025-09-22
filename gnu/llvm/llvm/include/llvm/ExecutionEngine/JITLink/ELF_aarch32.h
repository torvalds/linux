//===---- ELF_aarch32.h - JIT link functions for arm/thumb -----*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// jit-link functions for ELF/aarch32.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_JITLINK_ELF_AARCH32
#define LLVM_EXECUTIONENGINE_JITLINK_ELF_AARCH32

#include "llvm/ExecutionEngine/JITLink/JITLink.h"
#include "llvm/ExecutionEngine/JITLink/aarch32.h"

namespace llvm {
namespace jitlink {

/// Create a LinkGraph from an ELF/arm relocatable object
///
/// Note: The graph does not take ownership of the underlying buffer, nor copy
/// its contents. The caller is responsible for ensuring that the object buffer
/// outlives the graph.
Expected<std::unique_ptr<LinkGraph>>
createLinkGraphFromELFObject_aarch32(MemoryBufferRef ObjectBuffer);

/// jit-link the given object buffer, which must be an ELF arm/thumb object
/// file.
void link_ELF_aarch32(std::unique_ptr<LinkGraph> G,
                      std::unique_ptr<JITLinkContext> Ctx);

} // end namespace jitlink
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_JITLINK_ELF_AARCH32

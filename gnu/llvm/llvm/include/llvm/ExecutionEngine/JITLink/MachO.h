//===------- MachO.h - Generic JIT link function for MachO ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic jit-link functions for MachO.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_JITLINK_MACHO_H
#define LLVM_EXECUTIONENGINE_JITLINK_MACHO_H

#include "llvm/ExecutionEngine/JITLink/JITLink.h"

namespace llvm {
namespace jitlink {

/// Create a LinkGraph from a MachO relocatable object.
///
/// Note: The graph does not take ownership of the underlying buffer, nor copy
/// its contents. The caller is responsible for ensuring that the object buffer
/// outlives the graph.
Expected<std::unique_ptr<LinkGraph>>
createLinkGraphFromMachOObject(MemoryBufferRef ObjectBuffer);

/// jit-link the given ObjBuffer, which must be a MachO object file.
///
/// Uses conservative defaults for GOT and stub handling based on the target
/// platform.
void link_MachO(std::unique_ptr<LinkGraph> G,
                std::unique_ptr<JITLinkContext> Ctx);

} // end namespace jitlink
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_JITLINK_MACHO_H

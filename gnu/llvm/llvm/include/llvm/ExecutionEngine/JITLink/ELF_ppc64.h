//===------ ELF_ppc64.h - JIT link functions for ELF/ppc64 ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// jit-link functions for ELF/ppc64{le}.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_JITLINK_ELF_PPC64_H
#define LLVM_EXECUTIONENGINE_JITLINK_ELF_PPC64_H

#include "llvm/ExecutionEngine/JITLink/JITLink.h"

namespace llvm::jitlink {

/// Create a LinkGraph from an ELF/ppc64 relocatable object.
///
/// Note: The graph does not take ownership of the underlying buffer, nor copy
/// its contents. The caller is responsible for ensuring that the object buffer
/// outlives the graph.
///
/// WARNING: The big-endian backend has not been tested yet.
Expected<std::unique_ptr<LinkGraph>>
createLinkGraphFromELFObject_ppc64(MemoryBufferRef ObjectBuffer);

/// Create a LinkGraph from an ELF/ppc64le relocatable object.
///
/// Note: The graph does not take ownership of the underlying buffer, nor copy
/// its contents. The caller is responsible for ensuring that the object buffer
/// outlives the graph.
Expected<std::unique_ptr<LinkGraph>>
createLinkGraphFromELFObject_ppc64le(MemoryBufferRef ObjectBuffer);

/// jit-link the given object buffer, which must be a ELF ppc64le object file.
///
/// WARNING: The big-endian backend has not been tested yet.
void link_ELF_ppc64(std::unique_ptr<LinkGraph> G,
                    std::unique_ptr<JITLinkContext> Ctx);

/// jit-link the given object buffer, which must be a ELF ppc64le object file.
void link_ELF_ppc64le(std::unique_ptr<LinkGraph> G,
                      std::unique_ptr<JITLinkContext> Ctx);

} // end namespace llvm::jitlink

#endif // LLVM_EXECUTIONENGINE_JITLINK_ELF_PPC64_H

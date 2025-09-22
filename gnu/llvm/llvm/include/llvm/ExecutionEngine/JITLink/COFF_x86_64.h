//===--- COFF_x86_64.h - JIT link functions for COFF/x86-64 ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// jit-link functions for COFF/x86-64.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_JITLINK_COFF_X86_64_H
#define LLVM_EXECUTIONENGINE_JITLINK_COFF_X86_64_H

#include "llvm/ExecutionEngine/JITLink/JITLink.h"

namespace llvm {
namespace jitlink {

/// Create a LinkGraph from an COFF/x86-64 relocatable object.
///
/// Note: The graph does not take ownership of the underlying buffer, nor copy
/// its contents. The caller is responsible for ensuring that the object buffer
/// outlives the graph.
Expected<std::unique_ptr<LinkGraph>>
createLinkGraphFromCOFFObject_x86_64(MemoryBufferRef ObjectBuffer);

/// jit-link the given object buffer, which must be a COFF x86-64 object file.
void link_COFF_x86_64(std::unique_ptr<LinkGraph> G,
                      std::unique_ptr<JITLinkContext> Ctx);

/// Return the string name of the given COFF x86-64 edge kind.
const char *getCOFFX86RelocationKindName(Edge::Kind R);
} // end namespace jitlink
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_JITLINK_COFF_X86_64_H

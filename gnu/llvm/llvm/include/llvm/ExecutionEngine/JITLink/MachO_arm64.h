//===---- MachO_arm64.h - JIT link functions for MachO/arm64 ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// jit-link functions for MachO/arm64.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_JITLINK_MACHO_ARM64_H
#define LLVM_EXECUTIONENGINE_JITLINK_MACHO_ARM64_H

#include "llvm/ExecutionEngine/JITLink/JITLink.h"

namespace llvm {
namespace jitlink {

/// Create a LinkGraph from a MachO/arm64 relocatable object.
///
/// Note: The graph does not take ownership of the underlying buffer, nor copy
/// its contents. The caller is responsible for ensuring that the object buffer
/// outlives the graph.
Expected<std::unique_ptr<LinkGraph>>
createLinkGraphFromMachOObject_arm64(MemoryBufferRef ObjectBuffer);

/// jit-link the given object buffer, which must be a MachO arm64 object file.
///
/// If PrePrunePasses is empty then a default mark-live pass will be inserted
/// that will mark all exported atoms live. If PrePrunePasses is not empty, the
/// caller is responsible for including a pass to mark atoms as live.
///
/// If PostPrunePasses is empty then a default GOT-and-stubs insertion pass will
/// be inserted. If PostPrunePasses is not empty then the caller is responsible
/// for including a pass to insert GOT and stub edges.
void link_MachO_arm64(std::unique_ptr<LinkGraph> G,
                      std::unique_ptr<JITLinkContext> Ctx);

/// Returns a pass suitable for splitting __eh_frame sections in MachO/x86-64
/// objects.
LinkGraphPassFunction createEHFrameSplitterPass_MachO_arm64();

/// Returns a pass suitable for fixing missing edges in an __eh_frame section
/// in a MachO/x86-64 object.
LinkGraphPassFunction createEHFrameEdgeFixerPass_MachO_arm64();

} // end namespace jitlink
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_JITLINK_MACHO_ARM64_H

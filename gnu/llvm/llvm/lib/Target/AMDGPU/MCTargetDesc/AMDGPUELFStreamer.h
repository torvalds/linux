//===-------- AMDGPUELFStreamer.h - ELF Object Output -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a custom MCELFStreamer which allows us to insert some hooks before
// emitting data into an actual object file.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_MCTARGETDESC_AMDGPUELFSTREAMER_H
#define LLVM_LIB_TARGET_AMDGPU_MCTARGETDESC_AMDGPUELFSTREAMER_H

#include <memory>
namespace llvm {
class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCELFStreamer;
class MCObjectWriter;
class Triple;

MCELFStreamer *createAMDGPUELFStreamer(const Triple &T, MCContext &Context,
                                       std::unique_ptr<MCAsmBackend> MAB,
                                       std::unique_ptr<MCObjectWriter> OW,
                                       std::unique_ptr<MCCodeEmitter> Emitter);
} // namespace llvm.

#endif

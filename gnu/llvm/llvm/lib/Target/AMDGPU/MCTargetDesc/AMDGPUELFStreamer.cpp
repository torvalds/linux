//===-------- AMDGPUELFStreamer.cpp - ELF Object Output -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AMDGPUELFStreamer.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCObjectWriter.h"

using namespace llvm;

namespace {

class AMDGPUELFStreamer : public MCELFStreamer {
public:
  AMDGPUELFStreamer(const Triple &T, MCContext &Context,
                    std::unique_ptr<MCAsmBackend> MAB,
                    std::unique_ptr<MCObjectWriter> OW,
                    std::unique_ptr<MCCodeEmitter> Emitter)
      : MCELFStreamer(Context, std::move(MAB), std::move(OW),
                      std::move(Emitter)) {}
};

} // anonymous namespace

MCELFStreamer *
llvm::createAMDGPUELFStreamer(const Triple &T, MCContext &Context,
                              std::unique_ptr<MCAsmBackend> MAB,
                              std::unique_ptr<MCObjectWriter> OW,
                              std::unique_ptr<MCCodeEmitter> Emitter) {
  return new AMDGPUELFStreamer(T, Context, std::move(MAB), std::move(OW),
                               std::move(Emitter));
}

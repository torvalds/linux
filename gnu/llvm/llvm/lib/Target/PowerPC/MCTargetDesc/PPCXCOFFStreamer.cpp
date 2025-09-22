//===-------- PPCXCOFFStreamer.cpp - XCOFF Object Output ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a custom MCXCOFFStreamer for PowerPC.
//
// The purpose of the custom XCOFF streamer is to allow us to intercept
// instructions as they are being emitted and align all 8 byte instructions
// to a 64 byte boundary if required (by adding a 4 byte nop). This is important
// because 8 byte instructions are not allowed to cross 64 byte boundaries
// and by aligning anything that is within 4 bytes of the boundary we can
// guarantee that the 8 byte instructions do not cross that boundary.
//
//===----------------------------------------------------------------------===//

#include "PPCXCOFFStreamer.h"
#include "PPCMCCodeEmitter.h"
#include "llvm/BinaryFormat/XCOFF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSectionXCOFF.h"
#include "llvm/MC/MCSymbolXCOFF.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

PPCXCOFFStreamer::PPCXCOFFStreamer(MCContext &Context,
                                   std::unique_ptr<MCAsmBackend> MAB,
                                   std::unique_ptr<MCObjectWriter> OW,
                                   std::unique_ptr<MCCodeEmitter> Emitter)
    : MCXCOFFStreamer(Context, std::move(MAB), std::move(OW),
                      std::move(Emitter)) {}

void PPCXCOFFStreamer::emitPrefixedInstruction(const MCInst &Inst,
                                               const MCSubtargetInfo &STI) {
  // Prefixed instructions must not cross a 64-byte boundary (i.e. prefix is
  // before the boundary and the remaining 4-bytes are after the boundary). In
  // order to achieve this, a nop is added prior to any such boundary-crossing
  // prefixed instruction. Align to 64 bytes if possible but add a maximum of 4
  // bytes when trying to do that. If alignment requires adding more than 4
  // bytes then the instruction won't be aligned.
  emitCodeAlignment(Align(64), &STI, 4);

  // Emit the instruction.
  // Since the previous emit created a new fragment then adding this instruction
  // also forces the addition of a new fragment. Inst is now the first
  // instruction in that new fragment.
  MCXCOFFStreamer::emitInstruction(Inst, STI);
}

void PPCXCOFFStreamer::emitInstruction(const MCInst &Inst,
                                       const MCSubtargetInfo &STI) {
  PPCMCCodeEmitter *Emitter =
      static_cast<PPCMCCodeEmitter *>(getAssembler().getEmitterPtr());

  // Special handling is only for prefixed instructions.
  if (!Emitter->isPrefixedInstruction(Inst)) {
    MCXCOFFStreamer::emitInstruction(Inst, STI);
    return;
  }
  emitPrefixedInstruction(Inst, STI);
}

MCXCOFFStreamer *
llvm::createPPCXCOFFStreamer(MCContext &Context,
                             std::unique_ptr<MCAsmBackend> MAB,
                             std::unique_ptr<MCObjectWriter> OW,
                             std::unique_ptr<MCCodeEmitter> Emitter) {
  return new PPCXCOFFStreamer(Context, std::move(MAB), std::move(OW),
                              std::move(Emitter));
}

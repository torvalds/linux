//===-- MSP430ELFStreamer.cpp - MSP430 ELF Target Streamer Methods --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides MSP430 specific target streamer methods.
//
//===----------------------------------------------------------------------===//

#include "MSP430MCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/MSP430Attributes.h"

using namespace llvm;
using namespace llvm::MSP430Attrs;

namespace llvm {

class MSP430TargetELFStreamer : public MCTargetStreamer {
public:
  MCELFStreamer &getStreamer();
  MSP430TargetELFStreamer(MCStreamer &S, const MCSubtargetInfo &STI);
};

// This part is for ELF object output.
MSP430TargetELFStreamer::MSP430TargetELFStreamer(MCStreamer &S,
                                                 const MCSubtargetInfo &STI)
    : MCTargetStreamer(S) {
  // Emit build attributes section according to
  // MSP430 EABI (slaa534.pdf, part 13).
  MCSection *AttributeSection = getStreamer().getContext().getELFSection(
      ".MSP430.attributes", ELF::SHT_MSP430_ATTRIBUTES, 0);
  Streamer.switchSection(AttributeSection);

  // Format version.
  Streamer.emitInt8(0x41);
  // Subsection length.
  Streamer.emitInt32(22);
  // Vendor name string, zero-terminated.
  Streamer.emitBytes("mspabi");
  Streamer.emitInt8(0);

  // Attribute vector scope tag. 1 stands for the entire file.
  Streamer.emitInt8(1);
  // Attribute vector length.
  Streamer.emitInt32(11);

  Streamer.emitInt8(TagISA);
  Streamer.emitInt8(STI.hasFeature(MSP430::FeatureX) ? ISAMSP430X : ISAMSP430);
  Streamer.emitInt8(TagCodeModel);
  Streamer.emitInt8(CMSmall);
  Streamer.emitInt8(TagDataModel);
  Streamer.emitInt8(DMSmall);
  // Don't emit TagEnumSize, for full GCC compatibility.
}

MCELFStreamer &MSP430TargetELFStreamer::getStreamer() {
  return static_cast<MCELFStreamer &>(Streamer);
}

MCTargetStreamer *
createMSP430ObjectTargetStreamer(MCStreamer &S, const MCSubtargetInfo &STI) {
  const Triple &TT = STI.getTargetTriple();
  if (TT.isOSBinFormatELF())
    return new MSP430TargetELFStreamer(S, STI);
  return nullptr;
}

} // namespace llvm

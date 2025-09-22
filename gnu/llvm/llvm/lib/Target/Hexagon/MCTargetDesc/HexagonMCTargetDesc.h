//===-- HexagonMCTargetDesc.h - Hexagon Target Descriptions -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides Hexagon specific target descriptions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_MCTARGETDESC_HEXAGONMCTARGETDESC_H
#define LLVM_LIB_TARGET_HEXAGON_MCTARGETDESC_HEXAGONMCTARGETDESC_H

#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/CommandLine.h"
#include <cstdint>

#define Hexagon_POINTER_SIZE 4

#define Hexagon_PointerSize (Hexagon_POINTER_SIZE)
#define Hexagon_PointerSize_Bits (Hexagon_POINTER_SIZE * 8)
#define Hexagon_WordSize Hexagon_PointerSize
#define Hexagon_WordSize_Bits Hexagon_PointerSize_Bits

// allocframe saves LR and FP on stack before allocating
// a new stack frame. This takes 8 bytes.
#define HEXAGON_LRFP_SIZE 8

// Normal instruction size (in bytes).
#define HEXAGON_INSTR_SIZE 4

// Maximum number of words and instructions in a packet.
#define HEXAGON_PACKET_SIZE 4
#define HEXAGON_MAX_PACKET_SIZE (HEXAGON_PACKET_SIZE * HEXAGON_INSTR_SIZE)
// Minimum number of instructions in an end-loop packet.
#define HEXAGON_PACKET_INNER_SIZE 2
#define HEXAGON_PACKET_OUTER_SIZE 3
// Maximum number of instructions in a packet before shuffling,
// including a compound one or a duplex or an extender.
#define HEXAGON_PRESHUFFLE_PACKET_SIZE (HEXAGON_PACKET_SIZE + 3)

// Name of the global offset table as defined by the Hexagon ABI
#define HEXAGON_GOT_SYM_NAME "_GLOBAL_OFFSET_TABLE_"

namespace llvm {

struct InstrStage;
class FeatureBitset;
class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCInstrInfo;
class MCObjectTargetWriter;
class MCRegisterInfo;
class MCSubtargetInfo;
class MCTargetOptions;
class Target;
class Triple;
class StringRef;

extern cl::opt<bool> HexagonDisableCompound;
extern cl::opt<bool> HexagonDisableDuplex;
extern const InstrStage HexagonStages[];

MCInstrInfo *createHexagonMCInstrInfo();
MCRegisterInfo *createHexagonMCRegisterInfo(StringRef TT);

namespace Hexagon_MC {
  StringRef selectHexagonCPU(StringRef CPU);

  FeatureBitset completeHVXFeatures(const FeatureBitset &FB);
  /// Create a Hexagon MCSubtargetInfo instance. This is exposed so Asm parser,
  /// etc. do not need to go through TargetRegistry.
  MCSubtargetInfo *createHexagonMCSubtargetInfo(const Triple &TT, StringRef CPU,
                                                StringRef FS);
  MCSubtargetInfo const *getArchSubtarget(MCSubtargetInfo const *STI);
  void addArchSubtarget(MCSubtargetInfo const *STI,
                        StringRef FS);
  unsigned GetELFFlags(const MCSubtargetInfo &STI);

  llvm::ArrayRef<MCPhysReg> GetVectRegRev();

  std::optional<unsigned> getHVXVersion(const FeatureBitset &Features);

  unsigned getArchVersion(const FeatureBitset &Features);
  } // namespace Hexagon_MC

MCCodeEmitter *createHexagonMCCodeEmitter(const MCInstrInfo &MCII,
                                          MCContext &MCT);

MCAsmBackend *createHexagonAsmBackend(const Target &T,
                                      const MCSubtargetInfo &STI,
                                      const MCRegisterInfo &MRI,
                                      const MCTargetOptions &Options);

std::unique_ptr<MCObjectTargetWriter>
createHexagonELFObjectWriter(uint8_t OSABI, StringRef CPU);

unsigned HexagonGetLastSlot();
unsigned HexagonConvertUnits(unsigned ItinUnits, unsigned *Lanes);

} // End llvm namespace

// Define symbolic names for Hexagon registers.  This defines a mapping from
// register name to register number.
//
#define GET_REGINFO_ENUM
#include "HexagonGenRegisterInfo.inc"

// Defines symbolic names for the Hexagon instructions.
//
#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_SCHED_ENUM
#define GET_INSTRINFO_MC_HELPER_DECLS
#include "HexagonGenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "HexagonGenSubtargetInfo.inc"

#endif // LLVM_LIB_TARGET_HEXAGON_MCTARGETDESC_HEXAGONMCTARGETDESC_H

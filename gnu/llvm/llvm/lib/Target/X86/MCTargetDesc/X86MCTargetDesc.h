//===-- X86MCTargetDesc.h - X86 Target Descriptions -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides X86 specific target descriptions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_MCTARGETDESC_X86MCTARGETDESC_H
#define LLVM_LIB_TARGET_X86_MCTARGETDESC_X86MCTARGETDESC_H

#include "llvm/ADT/SmallVector.h"
#include <memory>
#include <string>

namespace llvm {
class formatted_raw_ostream;
class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCInst;
class MCInstPrinter;
class MCInstrInfo;
class MCObjectStreamer;
class MCObjectTargetWriter;
class MCObjectWriter;
class MCRegister;
class MCRegisterInfo;
class MCStreamer;
class MCSubtargetInfo;
class MCTargetOptions;
class MCTargetStreamer;
class Target;
class Triple;
class StringRef;

/// Flavour of dwarf regnumbers
///
namespace DWARFFlavour {
  enum {
    X86_64 = 0, X86_32_DarwinEH = 1, X86_32_Generic = 2
  };
}

///  Native X86 register numbers
///
namespace N86 {
  enum {
    EAX = 0, ECX = 1, EDX = 2, EBX = 3, ESP = 4, EBP = 5, ESI = 6, EDI = 7
  };
}

namespace X86_MC {
std::string ParseX86Triple(const Triple &TT);

unsigned getDwarfRegFlavour(const Triple &TT, bool isEH);

void initLLVMToSEHAndCVRegMapping(MCRegisterInfo *MRI);


/// Returns true if this instruction has a LOCK prefix.
bool hasLockPrefix(const MCInst &MI);

/// \param Op operand # of the memory operand.
///
/// \returns true if the specified instruction has a 16-bit memory operand.
bool is16BitMemOperand(const MCInst &MI, unsigned Op,
                       const MCSubtargetInfo &STI);

/// \param Op operand # of the memory operand.
///
/// \returns true if the specified instruction has a 32-bit memory operand.
bool is32BitMemOperand(const MCInst &MI, unsigned Op);

/// \param Op operand # of the memory operand.
///
/// \returns true if the specified instruction has a 64-bit memory operand.
#ifndef NDEBUG
bool is64BitMemOperand(const MCInst &MI, unsigned Op);
#endif

/// Returns true if this instruction needs an Address-Size override prefix.
bool needsAddressSizeOverride(const MCInst &MI, const MCSubtargetInfo &STI,
                              int MemoryOperand, uint64_t TSFlags);

/// Create a X86 MCSubtargetInfo instance. This is exposed so Asm parser, etc.
/// do not need to go through TargetRegistry.
MCSubtargetInfo *createX86MCSubtargetInfo(const Triple &TT, StringRef CPU,
                                          StringRef FS);

void emitInstruction(MCObjectStreamer &, const MCInst &Inst,
                     const MCSubtargetInfo &STI);

void emitPrefix(MCCodeEmitter &MCE, const MCInst &MI, SmallVectorImpl<char> &CB,
                const MCSubtargetInfo &STI);
}

MCCodeEmitter *createX86MCCodeEmitter(const MCInstrInfo &MCII,
                                      MCContext &Ctx);

MCAsmBackend *createX86_32AsmBackend(const Target &T,
                                     const MCSubtargetInfo &STI,
                                     const MCRegisterInfo &MRI,
                                     const MCTargetOptions &Options);
MCAsmBackend *createX86_64AsmBackend(const Target &T,
                                     const MCSubtargetInfo &STI,
                                     const MCRegisterInfo &MRI,
                                     const MCTargetOptions &Options);

/// Implements X86-only directives for assembly emission.
MCTargetStreamer *createX86AsmTargetStreamer(MCStreamer &S,
                                             formatted_raw_ostream &OS,
                                             MCInstPrinter *InstPrinter);

/// Implements X86-only directives for object files.
MCTargetStreamer *createX86ObjectTargetStreamer(MCStreamer &S,
                                                const MCSubtargetInfo &STI);

/// Construct an X86 Windows COFF machine code streamer which will generate
/// PE/COFF format object files.
///
/// Takes ownership of \p AB and \p CE.
MCStreamer *createX86WinCOFFStreamer(MCContext &C,
                                     std::unique_ptr<MCAsmBackend> &&AB,
                                     std::unique_ptr<MCObjectWriter> &&OW,
                                     std::unique_ptr<MCCodeEmitter> &&CE);

MCStreamer *createX86ELFStreamer(const Triple &T, MCContext &Context,
                                 std::unique_ptr<MCAsmBackend> &&MAB,
                                 std::unique_ptr<MCObjectWriter> &&MOW,
                                 std::unique_ptr<MCCodeEmitter> &&MCE);

/// Construct an X86 Mach-O object writer.
std::unique_ptr<MCObjectTargetWriter>
createX86MachObjectWriter(bool Is64Bit, uint32_t CPUType, uint32_t CPUSubtype);

/// Construct an X86 ELF object writer.
std::unique_ptr<MCObjectTargetWriter>
createX86ELFObjectWriter(bool IsELF64, uint8_t OSABI, uint16_t EMachine);
/// Construct an X86 Win COFF object writer.
std::unique_ptr<MCObjectTargetWriter>
createX86WinCOFFObjectWriter(bool Is64Bit);

/// \param Reg speicifed register.
/// \param Size the bit size of returned register.
/// \param High requires the high register.
///
/// \returns the sub or super register of a specific X86 register.
MCRegister getX86SubSuperRegister(MCRegister Reg, unsigned Size,
                                  bool High = false);
} // End llvm namespace


// Defines symbolic names for X86 registers.  This defines a mapping from
// register name to register number.
//
#define GET_REGINFO_ENUM
#include "X86GenRegisterInfo.inc"

// Defines symbolic names for the X86 instructions.
//
#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_MC_HELPER_DECLS
#include "X86GenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "X86GenSubtargetInfo.inc"

#define GET_X86_MNEMONIC_TABLES_H
#include "X86GenMnemonicTables.inc"

#endif

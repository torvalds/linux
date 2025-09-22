//===- DirectXMCTargetDesc.cpp - DirectX Target Implementation --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains DirectX target initializer.
///
//===----------------------------------------------------------------------===//

#include "DirectXMCTargetDesc.h"
#include "DirectXContainerObjectWriter.h"
#include "TargetInfo/DirectXTargetInfo.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCDXContainerWriter.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSchedule.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"
#include <memory>

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#define GET_INSTRINFO_MC_HELPERS
#include "DirectXGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "DirectXGenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "DirectXGenRegisterInfo.inc"

namespace {

// DXILInstPrinter is a null stub because DXIL instructions aren't printed.
class DXILInstPrinter : public MCInstPrinter {
public:
  DXILInstPrinter(const MCAsmInfo &MAI, const MCInstrInfo &MII,
                  const MCRegisterInfo &MRI)
      : MCInstPrinter(MAI, MII, MRI) {}

  void printInst(const MCInst *MI, uint64_t Address, StringRef Annot,
                 const MCSubtargetInfo &STI, raw_ostream &O) override {}

  std::pair<const char *, uint64_t> getMnemonic(const MCInst *MI) override {
    return std::make_pair<const char *, uint64_t>("", 0ull);
  }

private:
};

class DXILMCCodeEmitter : public MCCodeEmitter {
public:
  DXILMCCodeEmitter() {}

  void encodeInstruction(const MCInst &Inst, SmallVectorImpl<char> &CB,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override {}
};

class DXILAsmBackend : public MCAsmBackend {

public:
  DXILAsmBackend(const MCSubtargetInfo &STI)
      : MCAsmBackend(llvm::endianness::little) {}
  ~DXILAsmBackend() override = default;

  void applyFixup(const MCAssembler &Asm, const MCFixup &Fixup,
                  const MCValue &Target, MutableArrayRef<char> Data,
                  uint64_t Value, bool IsResolved,
                  const MCSubtargetInfo *STI) const override {}

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createDXContainerTargetObjectWriter();
  }

  unsigned getNumFixupKinds() const override { return 0; }

  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *STI) const override {
    return true;
  }
};

class DirectXMCAsmInfo : public MCAsmInfo {
public:
  explicit DirectXMCAsmInfo(const Triple &TT, const MCTargetOptions &Options)
      : MCAsmInfo() {}
};

} // namespace

static MCInstPrinter *createDXILMCInstPrinter(const Triple &T,
                                              unsigned SyntaxVariant,
                                              const MCAsmInfo &MAI,
                                              const MCInstrInfo &MII,
                                              const MCRegisterInfo &MRI) {
  if (SyntaxVariant == 0)
    return new DXILInstPrinter(MAI, MII, MRI);
  return nullptr;
}

MCCodeEmitter *createDXILMCCodeEmitter(const MCInstrInfo &MCII,
                                       MCContext &Ctx) {
  return new DXILMCCodeEmitter();
}

MCAsmBackend *createDXILMCAsmBackend(const Target &T,
                                     const MCSubtargetInfo &STI,
                                     const MCRegisterInfo &MRI,
                                     const MCTargetOptions &Options) {
  return new DXILAsmBackend(STI);
}

static MCSubtargetInfo *
createDirectXMCSubtargetInfo(const Triple &TT, StringRef CPU, StringRef FS) {
  return createDirectXMCSubtargetInfoImpl(TT, CPU, /*TuneCPU*/ CPU, FS);
}

static MCRegisterInfo *createDirectXMCRegisterInfo(const Triple &Triple) {
  return new MCRegisterInfo();
}

static MCInstrInfo *createDirectXMCInstrInfo() { return new MCInstrInfo(); }

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeDirectXTargetMC() {
  Target &T = getTheDirectXTarget();
  RegisterMCAsmInfo<DirectXMCAsmInfo> X(T);
  TargetRegistry::RegisterMCInstrInfo(T, createDirectXMCInstrInfo);
  TargetRegistry::RegisterMCInstPrinter(T, createDXILMCInstPrinter);
  TargetRegistry::RegisterMCRegInfo(T, createDirectXMCRegisterInfo);
  TargetRegistry::RegisterMCSubtargetInfo(T, createDirectXMCSubtargetInfo);
  TargetRegistry::RegisterMCCodeEmitter(T, createDXILMCCodeEmitter);
  TargetRegistry::RegisterMCAsmBackend(T, createDXILMCAsmBackend);
}

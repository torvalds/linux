//===-- LlvmState.cpp -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LlvmState.h"
#include "Target.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"

namespace llvm {
namespace exegesis {

Expected<LLVMState> LLVMState::Create(std::string TripleName,
                                      std::string CpuName,
                                      const StringRef Features,
                                      bool UseDummyPerfCounters) {
  if (TripleName.empty())
    TripleName = Triple::normalize(sys::getDefaultTargetTriple());

  Triple TheTriple(TripleName);

  // Get the target specific parser.
  std::string Error;
  const Target *TheTarget =
      TargetRegistry::lookupTarget(/*MArch=*/"", TheTriple, Error);
  if (!TheTarget) {
    return make_error<StringError>("no LLVM target for triple " + TripleName,
                                   inconvertibleErrorCode());
  }

  // Update Triple with the updated triple from the target lookup.
  TripleName = TheTriple.str();

  if (CpuName == "native")
    CpuName = std::string(sys::getHostCPUName());

  std::unique_ptr<MCSubtargetInfo> STI(
      TheTarget->createMCSubtargetInfo(TripleName, CpuName, ""));
  assert(STI && "Unable to create subtarget info!");
  if (!STI->isCPUStringValid(CpuName)) {
    return make_error<StringError>(Twine("invalid CPU name (")
                                       .concat(CpuName)
                                       .concat(") for triple ")
                                       .concat(TripleName),
                                   inconvertibleErrorCode());
  }
  const TargetOptions Options;
  std::unique_ptr<const TargetMachine> TM(
      static_cast<LLVMTargetMachine *>(TheTarget->createTargetMachine(
          TripleName, CpuName, Features, Options, Reloc::Model::Static)));
  if (!TM) {
    return make_error<StringError>("unable to create target machine",
                                   inconvertibleErrorCode());
  }

  const ExegesisTarget *ET =
      TripleName.empty() ? &ExegesisTarget::getDefault()
                         : ExegesisTarget::lookup(TM->getTargetTriple());
  if (!ET) {
    return make_error<StringError>("no Exegesis target for triple " +
                                       TripleName,
                                   inconvertibleErrorCode());
  }
  const PfmCountersInfo &PCI = UseDummyPerfCounters
                                   ? ET->getDummyPfmCounters()
                                   : ET->getPfmCounters(CpuName);
  return LLVMState(std::move(TM), ET, &PCI);
}

LLVMState::LLVMState(std::unique_ptr<const TargetMachine> TM,
                     const ExegesisTarget *ET, const PfmCountersInfo *PCI)
    : TheExegesisTarget(ET), TheTargetMachine(std::move(TM)), PfmCounters(PCI),
      OpcodeNameToOpcodeIdxMapping(createOpcodeNameToOpcodeIdxMapping()),
      RegNameToRegNoMapping(createRegNameToRegNoMapping()) {
  BitVector ReservedRegs = getFunctionReservedRegs(getTargetMachine());
  for (const unsigned Reg : TheExegesisTarget->getUnavailableRegisters())
    ReservedRegs.set(Reg);
  RATC.reset(
      new RegisterAliasingTrackerCache(getRegInfo(), std::move(ReservedRegs)));
  IC.reset(new InstructionsCache(getInstrInfo(), getRATC()));
}

std::unique_ptr<LLVMTargetMachine> LLVMState::createTargetMachine() const {
  return std::unique_ptr<LLVMTargetMachine>(static_cast<LLVMTargetMachine *>(
      TheTargetMachine->getTarget().createTargetMachine(
          TheTargetMachine->getTargetTriple().normalize(),
          TheTargetMachine->getTargetCPU(),
          TheTargetMachine->getTargetFeatureString(), TheTargetMachine->Options,
          Reloc::Model::Static)));
}

std::unique_ptr<const DenseMap<StringRef, unsigned>>
LLVMState::createOpcodeNameToOpcodeIdxMapping() const {
  const MCInstrInfo &InstrInfo = getInstrInfo();
  auto Map = std::make_unique<DenseMap<StringRef, unsigned>>(
      InstrInfo.getNumOpcodes());
  for (unsigned I = 0, E = InstrInfo.getNumOpcodes(); I < E; ++I)
    (*Map)[InstrInfo.getName(I)] = I;
  assert(Map->size() == InstrInfo.getNumOpcodes() && "Size prediction failed");
  return std::move(Map);
}

std::unique_ptr<const DenseMap<StringRef, unsigned>>
LLVMState::createRegNameToRegNoMapping() const {
  const MCRegisterInfo &RegInfo = getRegInfo();
  auto Map =
      std::make_unique<DenseMap<StringRef, unsigned>>(RegInfo.getNumRegs());
  // Special-case RegNo 0, which would otherwise be spelled as ''.
  (*Map)[kNoRegister] = 0;
  for (unsigned I = 1, E = RegInfo.getNumRegs(); I < E; ++I)
    (*Map)[RegInfo.getName(I)] = I;
  assert(Map->size() == RegInfo.getNumRegs() && "Size prediction failed");
  return std::move(Map);
}

bool LLVMState::canAssemble(const MCInst &Inst) const {
  MCContext Context(TheTargetMachine->getTargetTriple(),
                    TheTargetMachine->getMCAsmInfo(),
                    TheTargetMachine->getMCRegisterInfo(),
                    TheTargetMachine->getMCSubtargetInfo());
  std::unique_ptr<const MCCodeEmitter> CodeEmitter(
      TheTargetMachine->getTarget().createMCCodeEmitter(
          *TheTargetMachine->getMCInstrInfo(), Context));
  assert(CodeEmitter && "unable to create code emitter");
  SmallVector<char, 16> Tmp;
  SmallVector<MCFixup, 4> Fixups;
  CodeEmitter->encodeInstruction(Inst, Tmp, Fixups,
                                 *TheTargetMachine->getMCSubtargetInfo());
  return Tmp.size() > 0;
}

} // namespace exegesis
} // namespace llvm

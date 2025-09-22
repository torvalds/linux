//===- AMDGPUMIRFormatter.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Implementation of AMDGPU overrides of MIRFormatter.
//
//===----------------------------------------------------------------------===//

#include "AMDGPUMIRFormatter.h"
#include "GCNSubtarget.h"
#include "SIMachineFunctionInfo.h"

using namespace llvm;

void AMDGPUMIRFormatter::printImm(raw_ostream &OS, const MachineInstr &MI,
                      std::optional<unsigned int> OpIdx, int64_t Imm) const {

  switch (MI.getOpcode()) {
  case AMDGPU::S_DELAY_ALU:
    assert(OpIdx == 0);
    printSDelayAluImm(Imm, OS);
    break;
  default:
    MIRFormatter::printImm(OS, MI, OpIdx, Imm);
    break;
  }
}

/// Implement target specific parsing of immediate mnemonics. The mnemonic is
/// a string with a leading dot.
bool AMDGPUMIRFormatter::parseImmMnemonic(const unsigned OpCode,
                              const unsigned OpIdx,
                              StringRef Src, int64_t &Imm,
                              ErrorCallbackType ErrorCallback) const
{

  switch (OpCode) {
  case AMDGPU::S_DELAY_ALU:
    return parseSDelayAluImmMnemonic(OpIdx, Imm, Src, ErrorCallback);
  default:
    break;
  }
  return true; // Don't know what this is
}

void AMDGPUMIRFormatter::printSDelayAluImm(int64_t Imm,
                                           llvm::raw_ostream &OS) const {
  // Construct an immediate string to represent the information encoded in the
  // s_delay_alu immediate.
  // .id0_<dep>[_skip_<count>_id1<dep>]
  constexpr int64_t None = 0;
  constexpr int64_t Same = 0;

  uint64_t Id0 = (Imm & 0xF);
  uint64_t Skip = ((Imm >> 4) & 0x7);
  uint64_t Id1 = ((Imm >> 7) & 0xF);
  auto Outdep = [&](uint64_t Id) {
    if (Id == None)
      OS << "NONE";
    else if (Id < 5)
      OS << "VALU_DEP_" << Id;
    else if (Id < 8)
      OS << "TRANS32_DEP_" << Id - 4;
    else
      OS << "SALU_CYCLE_" << Id - 8;
  };

  OS << ".id0_";
  Outdep(Id0);

  // If the second inst is "same" and "none", no need to print the rest of the
  // string.
  if (Skip == Same && Id1 == None)
    return;

  // Encode the second delay specification.
  OS << "_skip_";
  if (Skip == 0)
    OS << "SAME";
  else if (Skip == 1)
    OS << "NEXT";
  else
    OS << "SKIP_" << Skip - 1;

  OS << "_id1_";
  Outdep(Id1);
}

bool AMDGPUMIRFormatter::parseSDelayAluImmMnemonic(
    const unsigned int OpIdx, int64_t &Imm, llvm::StringRef &Src,
    llvm::MIRFormatter::ErrorCallbackType &ErrorCallback) const
{
  assert(OpIdx == 0);

  Imm = 0;
  bool Expected = Src.consume_front(".id0_");
  if (!Expected)
    return ErrorCallback(Src.begin(), "Expected .id0_");

  auto ExpectInt = [&](StringRef &Src, int64_t Offset) -> int64_t {
    int64_t Dep;
    if (!Src.consumeInteger(10, Dep))
      return Dep + Offset;

    return -1;
  };

  auto DecodeDelay = [&](StringRef &Src) -> int64_t {
    if (Src.consume_front("NONE"))
      return 0;
    if (Src.consume_front("VALU_DEP_"))
      return ExpectInt(Src, 0);
    if (Src.consume_front("TRANS32_DEP_"))
      return ExpectInt(Src, 4);
    if (Src.consume_front("SALU_CYCLE_"))
      return ExpectInt(Src, 8);

    return -1;
  };

  int64_t Delay0 = DecodeDelay(Src);
  int64_t Skip = 0;
  int64_t Delay1 = 0;
  if (Delay0 == -1)
    return ErrorCallback(Src.begin(), "Could not decode delay0");


  // Set the Imm so far, to that early return has the correct value.
  Imm = Delay0;

  // If that was the end of the string, the second instruction is "same" and
  // "none"
  if (Src.begin() == Src.end())
    return false;

  Expected = Src.consume_front("_skip_");
  if (!Expected)
    return ErrorCallback(Src.begin(), "Expected _skip_");


  if (Src.consume_front("SAME")) {
    Skip = 0;
  } else if (Src.consume_front("NEXT")) {
    Skip = 1;
  } else if (Src.consume_front("SKIP_")) {
    if (Src.consumeInteger(10, Skip)) {
      return ErrorCallback(Src.begin(), "Expected integer Skip value");
    }
    Skip += 1;
  } else {
    ErrorCallback(Src.begin(), "Unexpected Skip Value");
  }

  Expected = Src.consume_front("_id1_");
  if (!Expected)
    return ErrorCallback(Src.begin(), "Expected _id1_");

  Delay1 = DecodeDelay(Src);
  if (Delay1 == -1)
    return ErrorCallback(Src.begin(), "Could not decode delay1");

  Imm = Imm | (Skip << 4) | (Delay1 << 7);
  return false;
}

bool AMDGPUMIRFormatter::parseCustomPseudoSourceValue(
    StringRef Src, MachineFunction &MF, PerFunctionMIParsingState &PFS,
    const PseudoSourceValue *&PSV, ErrorCallbackType ErrorCallback) const {
  SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
  const AMDGPUTargetMachine &TM =
      static_cast<const AMDGPUTargetMachine &>(MF.getTarget());
  if (Src == "GWSResource") {
    PSV = MFI->getGWSPSV(TM);
    return false;
  }
  llvm_unreachable("unknown MIR custom pseudo source value");
}

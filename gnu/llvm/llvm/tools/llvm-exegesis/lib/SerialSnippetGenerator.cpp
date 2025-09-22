//===-- SerialSnippetGenerator.cpp ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SerialSnippetGenerator.h"

#include "CodeTemplate.h"
#include "MCInstrDescView.h"
#include "Target.h"
#include <algorithm>
#include <numeric>
#include <vector>

namespace llvm {
namespace exegesis {

struct ExecutionClass {
  ExecutionMode Mask;
  const char *Description;
} static const kExecutionClasses[] = {
    {ExecutionMode::ALWAYS_SERIAL_IMPLICIT_REGS_ALIAS |
         ExecutionMode::ALWAYS_SERIAL_TIED_REGS_ALIAS,
     "Repeating a single implicitly serial instruction"},
    {ExecutionMode::SERIAL_VIA_EXPLICIT_REGS,
     "Repeating a single explicitly serial instruction"},
    {ExecutionMode::SERIAL_VIA_MEMORY_INSTR |
         ExecutionMode::SERIAL_VIA_NON_MEMORY_INSTR,
     "Repeating two instructions"},
};

static constexpr size_t kMaxAliasingInstructions = 10;

static std::vector<const Instruction *>
computeAliasingInstructions(const LLVMState &State, const Instruction *Instr,
                            size_t MaxAliasingInstructions,
                            const BitVector &ForbiddenRegisters) {
  const auto &ET = State.getExegesisTarget();
  const auto AvailableFeatures = State.getSubtargetInfo().getFeatureBits();
  // Randomly iterate the set of instructions.
  std::vector<unsigned> Opcodes;
  Opcodes.resize(State.getInstrInfo().getNumOpcodes());
  std::iota(Opcodes.begin(), Opcodes.end(), 0U);
  llvm::shuffle(Opcodes.begin(), Opcodes.end(), randomGenerator());

  std::vector<const Instruction *> AliasingInstructions;
  for (const unsigned OtherOpcode : Opcodes) {
    if (!ET.isOpcodeAvailable(OtherOpcode, AvailableFeatures))
      continue;
    if (OtherOpcode == Instr->Description.getOpcode())
      continue;
    const Instruction &OtherInstr = State.getIC().getInstr(OtherOpcode);
    const MCInstrDesc &OtherInstrDesc = OtherInstr.Description;
    // Ignore instructions that we cannot run.
    if (OtherInstrDesc.isPseudo() || OtherInstrDesc.usesCustomInsertionHook() ||
        OtherInstrDesc.isBranch() || OtherInstrDesc.isIndirectBranch() ||
        OtherInstrDesc.isCall() || OtherInstrDesc.isReturn()) {
          continue;
    }
    if (OtherInstr.hasMemoryOperands())
      continue;
    if (!ET.allowAsBackToBack(OtherInstr))
      continue;
    if (Instr->hasAliasingRegistersThrough(OtherInstr, ForbiddenRegisters))
      AliasingInstructions.push_back(&OtherInstr);
    if (AliasingInstructions.size() >= MaxAliasingInstructions)
      break;
  }
  return AliasingInstructions;
}

static ExecutionMode getExecutionModes(const Instruction &Instr,
                                       const BitVector &ForbiddenRegisters) {
  ExecutionMode EM = ExecutionMode::UNKNOWN;
  if (Instr.hasAliasingImplicitRegisters())
    EM |= ExecutionMode::ALWAYS_SERIAL_IMPLICIT_REGS_ALIAS;
  if (Instr.hasTiedRegisters())
    EM |= ExecutionMode::ALWAYS_SERIAL_TIED_REGS_ALIAS;
  if (Instr.hasMemoryOperands())
    EM |= ExecutionMode::SERIAL_VIA_MEMORY_INSTR;
  else {
    if (Instr.hasAliasingRegisters(ForbiddenRegisters))
      EM |= ExecutionMode::SERIAL_VIA_EXPLICIT_REGS;
    if (Instr.hasOneUseOrOneDef())
      EM |= ExecutionMode::SERIAL_VIA_NON_MEMORY_INSTR;
  }
  return EM;
}

static void appendCodeTemplates(const LLVMState &State,
                                InstructionTemplate Variant,
                                const BitVector &ForbiddenRegisters,
                                ExecutionMode ExecutionModeBit,
                                StringRef ExecutionClassDescription,
                                std::vector<CodeTemplate> &CodeTemplates) {
  assert(isEnumValue(ExecutionModeBit) && "Bit must be a power of two");
  switch (ExecutionModeBit) {
  case ExecutionMode::ALWAYS_SERIAL_IMPLICIT_REGS_ALIAS:
    // Nothing to do, the instruction is always serial.
    [[fallthrough]];
  case ExecutionMode::ALWAYS_SERIAL_TIED_REGS_ALIAS: {
    // Picking whatever value for the tied variable will make the instruction
    // serial.
    CodeTemplate CT;
    CT.Execution = ExecutionModeBit;
    CT.Info = std::string(ExecutionClassDescription);
    CT.Instructions.push_back(std::move(Variant));
    CodeTemplates.push_back(std::move(CT));
    return;
  }
  case ExecutionMode::SERIAL_VIA_MEMORY_INSTR: {
    // Select back-to-back memory instruction.
    // TODO: Implement me.
    return;
  }
  case ExecutionMode::SERIAL_VIA_EXPLICIT_REGS: {
    // Making the execution of this instruction serial by selecting one def
    // register to alias with one use register.
    const AliasingConfigurations SelfAliasing(
        Variant.getInstr(), Variant.getInstr(), ForbiddenRegisters);
    assert(!SelfAliasing.empty() && !SelfAliasing.hasImplicitAliasing() &&
           "Instr must alias itself explicitly");
    // This is a self aliasing instruction so defs and uses are from the same
    // instance, hence twice Variant in the following call.
    setRandomAliasing(SelfAliasing, Variant, Variant);
    CodeTemplate CT;
    CT.Execution = ExecutionModeBit;
    CT.Info = std::string(ExecutionClassDescription);
    CT.Instructions.push_back(std::move(Variant));
    CodeTemplates.push_back(std::move(CT));
    return;
  }
  case ExecutionMode::SERIAL_VIA_NON_MEMORY_INSTR: {
    const Instruction &Instr = Variant.getInstr();
    // Select back-to-back non-memory instruction.
    for (const auto *OtherInstr : computeAliasingInstructions(
             State, &Instr, kMaxAliasingInstructions, ForbiddenRegisters)) {
      const AliasingConfigurations Forward(Instr, *OtherInstr,
                                           ForbiddenRegisters);
      const AliasingConfigurations Back(*OtherInstr, Instr, ForbiddenRegisters);
      InstructionTemplate ThisIT(Variant);
      InstructionTemplate OtherIT(OtherInstr);
      if (!Forward.hasImplicitAliasing())
        setRandomAliasing(Forward, ThisIT, OtherIT);
      else if (!Back.hasImplicitAliasing())
        setRandomAliasing(Back, OtherIT, ThisIT);
      CodeTemplate CT;
      CT.Execution = ExecutionModeBit;
      CT.Info = std::string(ExecutionClassDescription);
      CT.Instructions.push_back(std::move(ThisIT));
      CT.Instructions.push_back(std::move(OtherIT));
      CodeTemplates.push_back(std::move(CT));
    }
    return;
  }
  default:
    llvm_unreachable("Unhandled enum value");
  }
}

SerialSnippetGenerator::~SerialSnippetGenerator() = default;

Expected<std::vector<CodeTemplate>>
SerialSnippetGenerator::generateCodeTemplates(
    InstructionTemplate Variant, const BitVector &ForbiddenRegisters) const {
  std::vector<CodeTemplate> Results;
  const ExecutionMode EM =
      getExecutionModes(Variant.getInstr(), ForbiddenRegisters);
  for (const auto EC : kExecutionClasses) {
    for (const auto ExecutionModeBit : getExecutionModeBits(EM & EC.Mask))
      appendCodeTemplates(State, Variant, ForbiddenRegisters, ExecutionModeBit,
                          EC.Description, Results);
    if (!Results.empty())
      break;
  }
  if (Results.empty())
    return make_error<Failure>(
        "No strategy found to make the execution serial");
  return std::move(Results);
}

} // namespace exegesis
} // namespace llvm

//===-- SnippetGenerator.cpp ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <string>

#include "Assembler.h"
#include "Error.h"
#include "MCInstrDescView.h"
#include "SnippetGenerator.h"
#include "Target.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/Program.h"

namespace llvm {
namespace exegesis {

std::vector<CodeTemplate> getSingleton(CodeTemplate &&CT) {
  std::vector<CodeTemplate> Result;
  Result.push_back(std::move(CT));
  return Result;
}

SnippetGeneratorFailure::SnippetGeneratorFailure(const Twine &S)
    : StringError(S, inconvertibleErrorCode()) {}

SnippetGenerator::SnippetGenerator(const LLVMState &State, const Options &Opts)
    : State(State), Opts(Opts) {}

SnippetGenerator::~SnippetGenerator() = default;

Error SnippetGenerator::generateConfigurations(
    const InstructionTemplate &Variant, std::vector<BenchmarkCode> &Benchmarks,
    const BitVector &ExtraForbiddenRegs) const {
  BitVector ForbiddenRegs = State.getRATC().reservedRegisters();
  ForbiddenRegs |= ExtraForbiddenRegs;
  // If the instruction has memory registers, prevent the generator from
  // using the scratch register and its aliasing registers.
  if (Variant.getInstr().hasMemoryOperands()) {
    const auto &ET = State.getExegesisTarget();
    unsigned ScratchSpacePointerInReg =
        ET.getScratchMemoryRegister(State.getTargetMachine().getTargetTriple());
    if (ScratchSpacePointerInReg == 0)
      return make_error<Failure>(
          "Infeasible : target does not support memory instructions");
    const auto &ScratchRegAliases =
        State.getRATC().getRegister(ScratchSpacePointerInReg).aliasedBits();
    // If the instruction implicitly writes to ScratchSpacePointerInReg , abort.
    // FIXME: We could make a copy of the scratch register.
    for (const auto &Op : Variant.getInstr().Operands) {
      if (Op.isDef() && Op.isImplicitReg() &&
          ScratchRegAliases.test(Op.getImplicitReg()))
        return make_error<Failure>(
            "Infeasible : memory instruction uses scratch memory register");
    }
    ForbiddenRegs |= ScratchRegAliases;
  }

  if (auto E = generateCodeTemplates(Variant, ForbiddenRegs)) {
    MutableArrayRef<CodeTemplate> Templates = E.get();

    // Avoid reallocations in the loop.
    Benchmarks.reserve(Benchmarks.size() + Templates.size());
    for (CodeTemplate &CT : Templates) {
      // TODO: Generate as many BenchmarkCode as needed.
      {
        BenchmarkCode BC;
        BC.Info = CT.Info;
        BC.Key.Instructions.reserve(CT.Instructions.size());
        for (InstructionTemplate &IT : CT.Instructions) {
          if (auto Error = randomizeUnsetVariables(State, ForbiddenRegs, IT))
            return Error;
          MCInst Inst = IT.build();
          if (auto Error = validateGeneratedInstruction(State, Inst))
            return Error;
          BC.Key.Instructions.push_back(Inst);
        }
        if (CT.ScratchSpacePointerInReg)
          BC.LiveIns.push_back(CT.ScratchSpacePointerInReg);
        BC.Key.RegisterInitialValues =
            computeRegisterInitialValues(CT.Instructions);
        BC.Key.Config = CT.Config;
        Benchmarks.emplace_back(std::move(BC));
        if (Benchmarks.size() >= Opts.MaxConfigsPerOpcode) {
          // We reached the number of  allowed configs and return early.
          return Error::success();
        }
      }
    }
    return Error::success();
  } else
    return E.takeError();
}

std::vector<RegisterValue> SnippetGenerator::computeRegisterInitialValues(
    const std::vector<InstructionTemplate> &Instructions) const {
  // Collect all register uses and create an assignment for each of them.
  // Ignore memory operands which are handled separately.
  // Loop invariant: DefinedRegs[i] is true iif it has been set at least once
  // before the current instruction.
  BitVector DefinedRegs = State.getRATC().emptyRegisters();
  std::vector<RegisterValue> RIV;
  for (const InstructionTemplate &IT : Instructions) {
    // Returns the register that this Operand sets or uses, or 0 if this is not
    // a register.
    const auto GetOpReg = [&IT](const Operand &Op) -> unsigned {
      if (Op.isMemory())
        return 0;
      if (Op.isImplicitReg())
        return Op.getImplicitReg();
      if (Op.isExplicit() && IT.getValueFor(Op).isReg())
        return IT.getValueFor(Op).getReg();
      return 0;
    };
    // Collect used registers that have never been def'ed.
    for (const Operand &Op : IT.getInstr().Operands) {
      if (Op.isUse()) {
        const unsigned Reg = GetOpReg(Op);
        if (Reg > 0 && !DefinedRegs.test(Reg)) {
          RIV.push_back(RegisterValue::zero(Reg));
          DefinedRegs.set(Reg);
        }
      }
    }
    // Mark defs as having been def'ed.
    for (const Operand &Op : IT.getInstr().Operands) {
      if (Op.isDef()) {
        const unsigned Reg = GetOpReg(Op);
        if (Reg > 0)
          DefinedRegs.set(Reg);
      }
    }
  }
  return RIV;
}

Expected<std::vector<CodeTemplate>>
generateSelfAliasingCodeTemplates(InstructionTemplate Variant,
                                  const BitVector &ForbiddenRegisters) {
  const AliasingConfigurations SelfAliasing(
      Variant.getInstr(), Variant.getInstr(), ForbiddenRegisters);
  if (SelfAliasing.empty())
    return make_error<SnippetGeneratorFailure>("empty self aliasing");
  std::vector<CodeTemplate> Result;
  Result.emplace_back();
  CodeTemplate &CT = Result.back();
  if (SelfAliasing.hasImplicitAliasing()) {
    CT.Info = "implicit Self cycles, picking random values.";
  } else {
    CT.Info = "explicit self cycles, selecting one aliasing Conf.";
    // This is a self aliasing instruction so defs and uses are from the same
    // instance, hence twice Variant in the following call.
    setRandomAliasing(SelfAliasing, Variant, Variant);
  }
  CT.Instructions.push_back(std::move(Variant));
  return std::move(Result);
}

Expected<std::vector<CodeTemplate>>
generateUnconstrainedCodeTemplates(const InstructionTemplate &Variant,
                                   StringRef Msg) {
  std::vector<CodeTemplate> Result;
  Result.emplace_back();
  CodeTemplate &CT = Result.back();
  CT.Info =
      std::string(formatv("{0}, repeating an unconstrained assignment", Msg));
  CT.Instructions.push_back(std::move(Variant));
  return std::move(Result);
}

std::mt19937 &randomGenerator() {
  static std::random_device RandomDevice;
  static std::mt19937 RandomGenerator(RandomDevice());
  return RandomGenerator;
}

size_t randomIndex(size_t Max) {
  std::uniform_int_distribution<> Distribution(0, Max);
  return Distribution(randomGenerator());
}

template <typename C> static decltype(auto) randomElement(const C &Container) {
  assert(!Container.empty() &&
         "Can't pick a random element from an empty container)");
  return Container[randomIndex(Container.size() - 1)];
}

static void setRegisterOperandValue(const RegisterOperandAssignment &ROV,
                                    InstructionTemplate &IB) {
  assert(ROV.Op);
  if (ROV.Op->isExplicit()) {
    auto &AssignedValue = IB.getValueFor(*ROV.Op);
    if (AssignedValue.isValid()) {
      assert(AssignedValue.isReg() && AssignedValue.getReg() == ROV.Reg);
      return;
    }
    AssignedValue = MCOperand::createReg(ROV.Reg);
  } else {
    assert(ROV.Op->isImplicitReg());
    assert(ROV.Reg == ROV.Op->getImplicitReg());
  }
}

size_t randomBit(const BitVector &Vector) {
  assert(Vector.any());
  auto Itr = Vector.set_bits_begin();
  for (size_t I = randomIndex(Vector.count() - 1); I != 0; --I)
    ++Itr;
  return *Itr;
}

std::optional<int> getFirstCommonBit(const BitVector &A, const BitVector &B) {
  BitVector Intersect = A;
  Intersect &= B;
  int idx = Intersect.find_first();
  if (idx != -1)
    return idx;
  return {};
}

void setRandomAliasing(const AliasingConfigurations &AliasingConfigurations,
                       InstructionTemplate &DefIB, InstructionTemplate &UseIB) {
  assert(!AliasingConfigurations.empty());
  assert(!AliasingConfigurations.hasImplicitAliasing());
  const auto &RandomConf = randomElement(AliasingConfigurations.Configurations);
  setRegisterOperandValue(randomElement(RandomConf.Defs), DefIB);
  setRegisterOperandValue(randomElement(RandomConf.Uses), UseIB);
}

static Error randomizeMCOperand(const LLVMState &State,
                                const Instruction &Instr, const Variable &Var,
                                MCOperand &AssignedValue,
                                const BitVector &ForbiddenRegs) {
  const Operand &Op = Instr.getPrimaryOperand(Var);
  if (Op.getExplicitOperandInfo().OperandType >=
      MCOI::OperandType::OPERAND_FIRST_TARGET)
    return State.getExegesisTarget().randomizeTargetMCOperand(
        Instr, Var, AssignedValue, ForbiddenRegs);
  switch (Op.getExplicitOperandInfo().OperandType) {
  case MCOI::OperandType::OPERAND_IMMEDIATE:
    // FIXME: explore immediate values too.
    AssignedValue = MCOperand::createImm(1);
    break;
  case MCOI::OperandType::OPERAND_REGISTER: {
    assert(Op.isReg());
    auto AllowedRegs = Op.getRegisterAliasing().sourceBits();
    assert(AllowedRegs.size() == ForbiddenRegs.size());
    for (auto I : ForbiddenRegs.set_bits())
      AllowedRegs.reset(I);
    if (!AllowedRegs.any())
      return make_error<Failure>(
          Twine("no available registers:\ncandidates:\n")
              .concat(debugString(State.getRegInfo(),
                                  Op.getRegisterAliasing().sourceBits()))
              .concat("\nforbidden:\n")
              .concat(debugString(State.getRegInfo(), ForbiddenRegs)));
    AssignedValue = MCOperand::createReg(randomBit(AllowedRegs));
    break;
  }
  default:
    break;
  }
  return Error::success();
}

Error randomizeUnsetVariables(const LLVMState &State,
                              const BitVector &ForbiddenRegs,
                              InstructionTemplate &IT) {
  for (const Variable &Var : IT.getInstr().Variables) {
    MCOperand &AssignedValue = IT.getValueFor(Var);
    if (!AssignedValue.isValid())
      if (auto Err = randomizeMCOperand(State, IT.getInstr(), Var,
                                        AssignedValue, ForbiddenRegs))
        return Err;
  }
  return Error::success();
}

Error validateGeneratedInstruction(const LLVMState &State, const MCInst &Inst) {
  for (const auto &Operand : Inst) {
    if (!Operand.isValid()) {
      // Mention the particular opcode - it is not necessarily the "main"
      // opcode being benchmarked by this snippet. For example, serial snippet
      // generator uses one more opcode when in SERIAL_VIA_NON_MEMORY_INSTR
      // execution mode.
      const auto OpcodeName = State.getInstrInfo().getName(Inst.getOpcode());
      return make_error<Failure>("Not all operands were initialized by the "
                                 "snippet generator for " +
                                 OpcodeName + " opcode.");
    }
  }
  return Error::success();
}

} // namespace exegesis
} // namespace llvm

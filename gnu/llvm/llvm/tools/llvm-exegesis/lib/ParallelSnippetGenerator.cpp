//===-- ParallelSnippetGenerator.cpp ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ParallelSnippetGenerator.h"

#include "BenchmarkRunner.h"
#include "MCInstrDescView.h"
#include "Target.h"

// FIXME: Load constants into registers (e.g. with fld1) to not break
// instructions like x87.

// Ideally we would like the only limitation on executing instructions to be the
// availability of the CPU resources (e.g. execution ports) needed to execute
// them, instead of the availability of their data dependencies.

// To achieve that, one approach is to generate instructions that do not have
// data dependencies between them.
//
// For some instructions, this is trivial:
//    mov rax, qword ptr [rsi]
//    mov rax, qword ptr [rsi]
//    mov rax, qword ptr [rsi]
//    mov rax, qword ptr [rsi]
// For the above snippet, haswell just renames rax four times and executes the
// four instructions two at a time on P23 and P0126.
//
// For some instructions, we just need to make sure that the source is
// different from the destination. For example, IDIV8r reads from GPR and
// writes to AX. We just need to ensure that the Var is assigned a
// register which is different from AX:
//    idiv bx
//    idiv bx
//    idiv bx
//    idiv bx
// The above snippet will be able to fully saturate the ports, while the same
// with ax would issue one uop every `latency(IDIV8r)` cycles.
//
// Some instructions make this harder because they both read and write from
// the same register:
//    inc rax
//    inc rax
//    inc rax
//    inc rax
// This has a data dependency from each instruction to the next, limit the
// number of instructions that can be issued in parallel.
// It turns out that this is not a big issue on recent Intel CPUs because they
// have heuristics to balance port pressure. In the snippet above, subsequent
// instructions will end up evenly distributed on {P0,P1,P5,P6}, but some CPUs
// might end up executing them all on P0 (just because they can), or try
// avoiding P5 because it's usually under high pressure from vector
// instructions.
// This issue is even more important for high-latency instructions because
// they increase the idle time of the CPU, e.g. :
//    imul rax, rbx
//    imul rax, rbx
//    imul rax, rbx
//    imul rax, rbx
//
// To avoid that, we do the renaming statically by generating as many
// independent exclusive assignments as possible (until all possible registers
// are exhausted) e.g.:
//    imul rax, rbx
//    imul rcx, rbx
//    imul rdx, rbx
//    imul r8,  rbx
//
// Some instruction even make the above static renaming impossible because
// they implicitly read and write from the same operand, e.g. ADC16rr reads
// and writes from EFLAGS.
// In that case we just use a greedy register assignment and hope for the
// best.

namespace llvm {
namespace exegesis {

static bool hasVariablesWithTiedOperands(const Instruction &Instr) {
  SmallVector<const Variable *, 8> Result;
  for (const auto &Var : Instr.Variables)
    if (Var.hasTiedOperands())
      return true;
  return false;
}

ParallelSnippetGenerator::~ParallelSnippetGenerator() = default;

void ParallelSnippetGenerator::instantiateMemoryOperands(
    const unsigned ScratchSpacePointerInReg,
    std::vector<InstructionTemplate> &Instructions) const {
  if (ScratchSpacePointerInReg == 0)
    return; // no memory operands.
  const auto &ET = State.getExegesisTarget();
  const unsigned MemStep = ET.getMaxMemoryAccessSize();
  const size_t OriginalInstructionsSize = Instructions.size();
  size_t I = 0;
  for (InstructionTemplate &IT : Instructions) {
    ET.fillMemoryOperands(IT, ScratchSpacePointerInReg, I * MemStep);
    ++I;
  }

  while (Instructions.size() < kMinNumDifferentAddresses) {
    InstructionTemplate IT = Instructions[I % OriginalInstructionsSize];
    ET.fillMemoryOperands(IT, ScratchSpacePointerInReg, I * MemStep);
    ++I;
    Instructions.push_back(std::move(IT));
  }
  assert(I * MemStep < BenchmarkRunner::ScratchSpace::kSize &&
         "not enough scratch space");
}

enum class RegRandomizationStrategy : uint8_t {
  PickRandomRegs,
  SingleStaticRegPerOperand,
  SingleStaticReg,

  FIRST = PickRandomRegs,
  LAST = SingleStaticReg,
};

} // namespace exegesis

template <> struct enum_iteration_traits<exegesis::RegRandomizationStrategy> {
  static constexpr bool is_iterable = true;
};

namespace exegesis {

const char *getDescription(RegRandomizationStrategy S) {
  switch (S) {
  case RegRandomizationStrategy::PickRandomRegs:
    return "randomizing registers";
  case RegRandomizationStrategy::SingleStaticRegPerOperand:
    return "one unique register for each position";
  case RegRandomizationStrategy::SingleStaticReg:
    return "reusing the same register for all positions";
  }
  llvm_unreachable("Unknown UseRegRandomizationStrategy enum");
}

static std::variant<std::nullopt_t, MCOperand, Register>
generateSingleRegisterForInstrAvoidingDefUseOverlap(
    const LLVMState &State, const BitVector &ForbiddenRegisters,
    const BitVector &ImplicitUseAliases, const BitVector &ImplicitDefAliases,
    const BitVector &Uses, const BitVector &Defs, const InstructionTemplate &IT,
    const Operand &Op, const ArrayRef<InstructionTemplate> Instructions,
    RegRandomizationStrategy S) {
  const Instruction &Instr = IT.getInstr();
  assert(Op.isReg() && Op.isExplicit() && !Op.isMemory() &&
         !IT.getValueFor(Op).isValid());
  assert((!Op.isUse() || !Op.isTied()) &&
         "Not expecting to see a tied use reg");

  if (Op.isUse()) {
    switch (S) {
    case RegRandomizationStrategy::PickRandomRegs:
      break;
    case RegRandomizationStrategy::SingleStaticReg:
    case RegRandomizationStrategy::SingleStaticRegPerOperand: {
      if (!Instructions.empty())
        return Instructions.front().getValueFor(Op);
      if (S != RegRandomizationStrategy::SingleStaticReg)
        break;
      BitVector PossibleRegisters = Op.getRegisterAliasing().sourceBits();
      const BitVector UseAliases = getAliasedBits(State.getRegInfo(), Uses);
      if (std::optional<int> CommonBit =
              getFirstCommonBit(PossibleRegisters, UseAliases))
        return *CommonBit;
      break;
    }
    }
  }

  BitVector PossibleRegisters = Op.getRegisterAliasing().sourceBits();
  remove(PossibleRegisters, ForbiddenRegisters);

  if (Op.isDef()) {
    remove(PossibleRegisters, ImplicitUseAliases);
    const BitVector UseAliases = getAliasedBits(State.getRegInfo(), Uses);
    remove(PossibleRegisters, UseAliases);
  }

  if (Op.isUse()) {
    remove(PossibleRegisters, ImplicitDefAliases);
    // NOTE: in general, using same reg for multiple Use's is fine.
    if (S == RegRandomizationStrategy::SingleStaticRegPerOperand) {
      const BitVector UseAliases = getAliasedBits(State.getRegInfo(), Uses);
      remove(PossibleRegisters, UseAliases);
    }
  }

  bool IsDefWithTiedUse =
      Instr.Variables[Op.getVariableIndex()].hasTiedOperands();
  if (Op.isUse() || IsDefWithTiedUse) {
    // Now, important bit: if we have used some register for def,
    // then we can not use that same register for *any* use,
    // be it either an untied use, or an use tied to a def.
    // But def-ing same regs is fine, as long as there are no uses!
    const BitVector DefsAliases = getAliasedBits(State.getRegInfo(), Defs);
    remove(PossibleRegisters, DefsAliases);
  }

  if (!PossibleRegisters.any())
    return std::nullopt;

  return randomBit(PossibleRegisters);
}

static std::optional<InstructionTemplate>
generateSingleSnippetForInstrAvoidingDefUseOverlap(
    const LLVMState &State, const BitVector &ForbiddenRegisters,
    const BitVector &ImplicitUseAliases, const BitVector &ImplicitDefAliases,
    BitVector &Uses, BitVector &Defs, InstructionTemplate IT,
    const ArrayRef<InstructionTemplate> Instructions,
    RegRandomizationStrategy S) {
  const Instruction &Instr = IT.getInstr();
  for (const Operand &Op : Instr.Operands) {
    if (!Op.isReg() || !Op.isExplicit() || Op.isMemory() ||
        IT.getValueFor(Op).isValid())
      continue;
    assert((!Op.isUse() || !Op.isTied()) && "Will not get tied uses.");

    std::variant<std::nullopt_t, MCOperand, Register> R =
        generateSingleRegisterForInstrAvoidingDefUseOverlap(
            State, ForbiddenRegisters, ImplicitUseAliases, ImplicitDefAliases,
            Uses, Defs, IT, Op, Instructions, S);

    if (std::holds_alternative<std::nullopt_t>(R))
      return {};

    MCOperand MCOp;
    if (std::holds_alternative<MCOperand>(R))
      MCOp = std::get<MCOperand>(R);
    else {
      Register RandomReg = std::get<Register>(R);
      if (Op.isDef())
        Defs.set(RandomReg);
      if (Op.isUse())
        Uses.set(RandomReg);
      MCOp = MCOperand::createReg(RandomReg);
    }
    IT.getValueFor(Op) = MCOp;
  }
  return IT;
}

static std::vector<InstructionTemplate>
generateSnippetForInstrAvoidingDefUseOverlap(
    const LLVMState &State, const InstructionTemplate &IT,
    RegRandomizationStrategy S, const BitVector &ForbiddenRegisters) {
  // We don't want to accidentally serialize the instruction,
  // so we must be sure that we don't pick a def that is an implicit use,
  // or a use that is an implicit def, so record implicit regs now.
  BitVector ImplicitUses(State.getRegInfo().getNumRegs());
  BitVector ImplicitDefs(State.getRegInfo().getNumRegs());
  for (const auto &Op : IT.getInstr().Operands) {
    if (Op.isReg() && Op.isImplicit() && !Op.isMemory()) {
      assert(Op.isImplicitReg() && "Not an implicit register operand?");
      if (Op.isUse())
        ImplicitUses.set(Op.getImplicitReg());
      else {
        assert(Op.isDef() && "Not a use and not a def?");
        ImplicitDefs.set(Op.getImplicitReg());
      }
    }
  }
  const BitVector ImplicitUseAliases =
      getAliasedBits(State.getRegInfo(), ImplicitUses);
  const BitVector ImplicitDefAliases =
      getAliasedBits(State.getRegInfo(), ImplicitDefs);

  BitVector Defs(State.getRegInfo().getNumRegs());
  BitVector Uses(State.getRegInfo().getNumRegs());
  std::vector<InstructionTemplate> Instructions;

  while (true) {
    std::optional<InstructionTemplate> TmpIT =
        generateSingleSnippetForInstrAvoidingDefUseOverlap(
            State, ForbiddenRegisters, ImplicitUseAliases, ImplicitDefAliases,
            Uses, Defs, IT, Instructions, S);
    if (!TmpIT)
      return Instructions;
    Instructions.push_back(std::move(*TmpIT));
    if (!hasVariablesWithTiedOperands(IT.getInstr()))
      return Instructions;
    assert(Instructions.size() <= 128 && "Stuck in endless loop?");
  }
}

Expected<std::vector<CodeTemplate>>
ParallelSnippetGenerator::generateCodeTemplates(
    InstructionTemplate Variant, const BitVector &ForbiddenRegisters) const {
  const Instruction &Instr = Variant.getInstr();
  CodeTemplate CT;
  CT.ScratchSpacePointerInReg =
      Instr.hasMemoryOperands()
          ? State.getExegesisTarget().getScratchMemoryRegister(
                State.getTargetMachine().getTargetTriple())
          : 0;
  const AliasingConfigurations SelfAliasing(Instr, Instr, ForbiddenRegisters);
  if (SelfAliasing.empty()) {
    CT.Info = "instruction is parallel, repeating a random one.";
    CT.Instructions.push_back(std::move(Variant));
    instantiateMemoryOperands(CT.ScratchSpacePointerInReg, CT.Instructions);
    return getSingleton(std::move(CT));
  }
  if (SelfAliasing.hasImplicitAliasing()) {
    CT.Info = "instruction is serial, repeating a random one.";
    CT.Instructions.push_back(std::move(Variant));
    instantiateMemoryOperands(CT.ScratchSpacePointerInReg, CT.Instructions);
    return getSingleton(std::move(CT));
  }
  std::vector<CodeTemplate> Result;
  bool HasTiedOperands = hasVariablesWithTiedOperands(Instr);
  // If there are no tied operands, then we don't want to "saturate backedge",
  // and the template we will produce will have only a single instruction.
  unsigned NumUntiedUseRegs = count_if(Instr.Operands, [](const Operand &Op) {
    return Op.isReg() && Op.isExplicit() && !Op.isMemory() && Op.isUse() &&
           !Op.isTied();
  });
  SmallVector<RegRandomizationStrategy, 3> Strategies;
  if (HasTiedOperands || NumUntiedUseRegs >= 3)
    Strategies.push_back(RegRandomizationStrategy::PickRandomRegs);
  if (NumUntiedUseRegs >= 2)
    Strategies.push_back(RegRandomizationStrategy::SingleStaticRegPerOperand);
  Strategies.push_back(RegRandomizationStrategy::SingleStaticReg);
  for (RegRandomizationStrategy S : Strategies) {
    CodeTemplate CurrCT = CT.clone();
    CurrCT.Info =
        Twine("instruction has ")
            .concat(HasTiedOperands ? "" : "no ")
            .concat("tied variables, avoiding "
                    "Read-After-Write issue, picking random def and use "
                    "registers not aliasing each other, for uses, ")
            .concat(getDescription(S))
            .str();
    CurrCT.Instructions = generateSnippetForInstrAvoidingDefUseOverlap(
        State, Variant, S, ForbiddenRegisters);
    if (CurrCT.Instructions.empty())
      return make_error<StringError>(
          Twine("Failed to produce any snippet via: ").concat(CurrCT.Info),
          inconvertibleErrorCode());
    instantiateMemoryOperands(CurrCT.ScratchSpacePointerInReg,
                              CurrCT.Instructions);
    Result.push_back(std::move(CurrCT));
  }
  return Result;
}

constexpr const size_t ParallelSnippetGenerator::kMinNumDifferentAddresses;

} // namespace exegesis
} // namespace llvm

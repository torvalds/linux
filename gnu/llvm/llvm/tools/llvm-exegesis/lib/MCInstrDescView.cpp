//===-- MCInstrDescView.cpp -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCInstrDescView.h"

#include <iterator>
#include <tuple>

#include "llvm/ADT/STLExtras.h"

namespace llvm {
namespace exegesis {

unsigned Variable::getIndex() const { return *Index; }

unsigned Variable::getPrimaryOperandIndex() const {
  assert(!TiedOperands.empty());
  return TiedOperands[0];
}

bool Variable::hasTiedOperands() const {
  assert(TiedOperands.size() <= 2 &&
         "No more than two operands can be tied together");
  // By definition only Use and Def operands can be tied together.
  // TiedOperands[0] is the Def operand (LLVM stores defs first).
  // TiedOperands[1] is the Use operand.
  return TiedOperands.size() > 1;
}

unsigned Operand::getIndex() const { return *Index; }

bool Operand::isExplicit() const { return Info; }

bool Operand::isImplicit() const { return !Info; }

bool Operand::isImplicitReg() const { return ImplicitReg; }

bool Operand::isDef() const { return IsDef; }

bool Operand::isUse() const { return !IsDef; }

bool Operand::isReg() const { return Tracker; }

bool Operand::isTied() const { return TiedToIndex.has_value(); }

bool Operand::isVariable() const { return VariableIndex.has_value(); }

bool Operand::isMemory() const {
  return isExplicit() &&
         getExplicitOperandInfo().OperandType == MCOI::OPERAND_MEMORY;
}

bool Operand::isImmediate() const {
  return isExplicit() &&
         getExplicitOperandInfo().OperandType == MCOI::OPERAND_IMMEDIATE;
}

unsigned Operand::getTiedToIndex() const { return *TiedToIndex; }

unsigned Operand::getVariableIndex() const { return *VariableIndex; }

unsigned Operand::getImplicitReg() const {
  assert(ImplicitReg);
  return ImplicitReg;
}

const RegisterAliasingTracker &Operand::getRegisterAliasing() const {
  assert(Tracker);
  return *Tracker;
}

const MCOperandInfo &Operand::getExplicitOperandInfo() const {
  assert(Info);
  return *Info;
}

const BitVector *BitVectorCache::getUnique(BitVector &&BV) const {
  for (const auto &Entry : Cache)
    if (*Entry == BV)
      return Entry.get();
  Cache.push_back(std::make_unique<BitVector>());
  auto &Entry = Cache.back();
  Entry->swap(BV);
  return Entry.get();
}

Instruction::Instruction(const MCInstrDesc *Description, StringRef Name,
                         SmallVector<Operand, 8> Operands,
                         SmallVector<Variable, 4> Variables,
                         const BitVector *ImplDefRegs,
                         const BitVector *ImplUseRegs,
                         const BitVector *AllDefRegs,
                         const BitVector *AllUseRegs)
    : Description(*Description), Name(Name), Operands(std::move(Operands)),
      Variables(std::move(Variables)), ImplDefRegs(*ImplDefRegs),
      ImplUseRegs(*ImplUseRegs), AllDefRegs(*AllDefRegs),
      AllUseRegs(*AllUseRegs) {}

std::unique_ptr<Instruction>
Instruction::create(const MCInstrInfo &InstrInfo,
                    const RegisterAliasingTrackerCache &RATC,
                    const BitVectorCache &BVC, unsigned Opcode) {
  const MCInstrDesc *const Description = &InstrInfo.get(Opcode);
  unsigned OpIndex = 0;
  SmallVector<Operand, 8> Operands;
  SmallVector<Variable, 4> Variables;
  for (; OpIndex < Description->getNumOperands(); ++OpIndex) {
    const auto &OpInfo = Description->operands()[OpIndex];
    Operand Operand;
    Operand.Index = OpIndex;
    Operand.IsDef = (OpIndex < Description->getNumDefs());
    // TODO(gchatelet): Handle isLookupPtrRegClass.
    if (OpInfo.RegClass >= 0)
      Operand.Tracker = &RATC.getRegisterClass(OpInfo.RegClass);
    int TiedToIndex = Description->getOperandConstraint(OpIndex, MCOI::TIED_TO);
    assert((TiedToIndex == -1 ||
            (0 <= TiedToIndex &&
             TiedToIndex < std::numeric_limits<uint8_t>::max())) &&
           "Unknown Operand Constraint");
    if (TiedToIndex >= 0)
      Operand.TiedToIndex = TiedToIndex;
    Operand.Info = &OpInfo;
    Operands.push_back(Operand);
  }
  for (MCPhysReg MCPhysReg : Description->implicit_defs()) {
    Operand Operand;
    Operand.Index = OpIndex++;
    Operand.IsDef = true;
    Operand.Tracker = &RATC.getRegister(MCPhysReg);
    Operand.ImplicitReg = MCPhysReg;
    Operands.push_back(Operand);
  }
  for (MCPhysReg MCPhysReg : Description->implicit_uses()) {
    Operand Operand;
    Operand.Index = OpIndex++;
    Operand.IsDef = false;
    Operand.Tracker = &RATC.getRegister(MCPhysReg);
    Operand.ImplicitReg = MCPhysReg;
    Operands.push_back(Operand);
  }
  Variables.reserve(Operands.size()); // Variables.size() <= Operands.size()
  // Assigning Variables to non tied explicit operands.
  for (auto &Op : Operands)
    if (Op.isExplicit() && !Op.isTied()) {
      const size_t VariableIndex = Variables.size();
      assert(VariableIndex < std::numeric_limits<uint8_t>::max());
      Op.VariableIndex = VariableIndex;
      Variables.emplace_back();
      Variables.back().Index = VariableIndex;
    }
  // Assigning Variables to tied operands.
  for (auto &Op : Operands)
    if (Op.isExplicit() && Op.isTied())
      Op.VariableIndex = Operands[Op.getTiedToIndex()].getVariableIndex();
  // Assigning Operands to Variables.
  for (auto &Op : Operands)
    if (Op.isVariable())
      Variables[Op.getVariableIndex()].TiedOperands.push_back(Op.getIndex());
  // Processing Aliasing.
  BitVector ImplDefRegs = RATC.emptyRegisters();
  BitVector ImplUseRegs = RATC.emptyRegisters();
  BitVector AllDefRegs = RATC.emptyRegisters();
  BitVector AllUseRegs = RATC.emptyRegisters();
  for (const auto &Op : Operands) {
    if (Op.isReg()) {
      const auto &AliasingBits = Op.getRegisterAliasing().aliasedBits();
      if (Op.isDef())
        AllDefRegs |= AliasingBits;
      if (Op.isUse())
        AllUseRegs |= AliasingBits;
      if (Op.isDef() && Op.isImplicit())
        ImplDefRegs |= AliasingBits;
      if (Op.isUse() && Op.isImplicit())
        ImplUseRegs |= AliasingBits;
    }
  }
  // Can't use make_unique because constructor is private.
  return std::unique_ptr<Instruction>(new Instruction(
      Description, InstrInfo.getName(Opcode), std::move(Operands),
      std::move(Variables), BVC.getUnique(std::move(ImplDefRegs)),
      BVC.getUnique(std::move(ImplUseRegs)),
      BVC.getUnique(std::move(AllDefRegs)),
      BVC.getUnique(std::move(AllUseRegs))));
}

const Operand &Instruction::getPrimaryOperand(const Variable &Var) const {
  const auto PrimaryOperandIndex = Var.getPrimaryOperandIndex();
  assert(PrimaryOperandIndex < Operands.size());
  return Operands[PrimaryOperandIndex];
}

bool Instruction::hasMemoryOperands() const {
  return any_of(Operands, [](const Operand &Op) {
    return Op.isReg() && Op.isExplicit() && Op.isMemory();
  });
}

bool Instruction::hasAliasingImplicitRegisters() const {
  return ImplDefRegs.anyCommon(ImplUseRegs);
}

// Returns true if there are registers that are both in `A` and `B` but not in
// `Forbidden`.
static bool anyCommonExcludingForbidden(const BitVector &A, const BitVector &B,
                                        const BitVector &Forbidden) {
  assert(A.size() == B.size() && B.size() == Forbidden.size());
  const auto Size = A.size();
  for (int AIndex = A.find_first(); AIndex != -1;) {
    const int BIndex = B.find_first_in(AIndex, Size);
    if (BIndex == -1)
      return false;
    if (AIndex == BIndex && !Forbidden.test(AIndex))
      return true;
    AIndex = A.find_first_in(BIndex + 1, Size);
  }
  return false;
}

bool Instruction::hasAliasingRegistersThrough(
    const Instruction &OtherInstr, const BitVector &ForbiddenRegisters) const {
  return anyCommonExcludingForbidden(AllDefRegs, OtherInstr.AllUseRegs,
                                     ForbiddenRegisters) &&
         anyCommonExcludingForbidden(OtherInstr.AllDefRegs, AllUseRegs,
                                     ForbiddenRegisters);
}

bool Instruction::hasTiedRegisters() const {
  return any_of(Variables,
                [](const Variable &Var) { return Var.hasTiedOperands(); });
}

bool Instruction::hasAliasingRegisters(
    const BitVector &ForbiddenRegisters) const {
  return anyCommonExcludingForbidden(AllDefRegs, AllUseRegs,
                                     ForbiddenRegisters);
}

bool Instruction::hasOneUseOrOneDef() const {
  return AllDefRegs.count() || AllUseRegs.count();
}

void Instruction::dump(const MCRegisterInfo &RegInfo,
                       const RegisterAliasingTrackerCache &RATC,
                       raw_ostream &Stream) const {
  Stream << "- " << Name << "\n";
  for (const auto &Op : Operands) {
    Stream << "- Op" << Op.getIndex();
    if (Op.isExplicit())
      Stream << " Explicit";
    if (Op.isImplicit())
      Stream << " Implicit";
    if (Op.isUse())
      Stream << " Use";
    if (Op.isDef())
      Stream << " Def";
    if (Op.isImmediate())
      Stream << " Immediate";
    if (Op.isMemory())
      Stream << " Memory";
    if (Op.isReg()) {
      if (Op.isImplicitReg())
        Stream << " Reg(" << RegInfo.getName(Op.getImplicitReg()) << ")";
      else
        Stream << " RegClass("
               << RegInfo.getRegClassName(
                      &RegInfo.getRegClass(Op.Info->RegClass))
               << ")";
    }
    if (Op.isTied())
      Stream << " TiedToOp" << Op.getTiedToIndex();
    Stream << "\n";
  }
  for (const auto &Var : Variables) {
    Stream << "- Var" << Var.getIndex();
    Stream << " [";
    bool IsFirst = true;
    for (auto OperandIndex : Var.TiedOperands) {
      if (!IsFirst)
        Stream << ",";
      Stream << "Op" << OperandIndex;
      IsFirst = false;
    }
    Stream << "]";
    Stream << "\n";
  }
  if (hasMemoryOperands())
    Stream << "- hasMemoryOperands\n";
  if (hasAliasingImplicitRegisters())
    Stream << "- hasAliasingImplicitRegisters (execution is always serial)\n";
  if (hasTiedRegisters())
    Stream << "- hasTiedRegisters (execution is always serial)\n";
  if (hasAliasingRegisters(RATC.emptyRegisters()))
    Stream << "- hasAliasingRegisters\n";
}

InstructionsCache::InstructionsCache(const MCInstrInfo &InstrInfo,
                                     const RegisterAliasingTrackerCache &RATC)
    : InstrInfo(InstrInfo), RATC(RATC), BVC() {}

const Instruction &InstructionsCache::getInstr(unsigned Opcode) const {
  auto &Found = Instructions[Opcode];
  if (!Found)
    Found = Instruction::create(InstrInfo, RATC, BVC, Opcode);
  return *Found;
}

bool RegisterOperandAssignment::
operator==(const RegisterOperandAssignment &Other) const {
  return std::tie(Op, Reg) == std::tie(Other.Op, Other.Reg);
}

bool AliasingRegisterOperands::
operator==(const AliasingRegisterOperands &Other) const {
  return std::tie(Defs, Uses) == std::tie(Other.Defs, Other.Uses);
}

static void
addOperandIfAlias(const MCPhysReg Reg, bool SelectDef,
                  ArrayRef<Operand> Operands,
                  SmallVectorImpl<RegisterOperandAssignment> &OperandValues) {
  for (const auto &Op : Operands) {
    if (Op.isReg() && Op.isDef() == SelectDef) {
      const int SourceReg = Op.getRegisterAliasing().getOrigin(Reg);
      if (SourceReg >= 0)
        OperandValues.emplace_back(&Op, SourceReg);
    }
  }
}

bool AliasingRegisterOperands::hasImplicitAliasing() const {
  const auto HasImplicit = [](const RegisterOperandAssignment &ROV) {
    return ROV.Op->isImplicit();
  };
  return any_of(Defs, HasImplicit) && any_of(Uses, HasImplicit);
}

bool AliasingConfigurations::empty() const { return Configurations.empty(); }

bool AliasingConfigurations::hasImplicitAliasing() const {
  return any_of(Configurations, [](const AliasingRegisterOperands &ARO) {
    return ARO.hasImplicitAliasing();
  });
}

AliasingConfigurations::AliasingConfigurations(
    const Instruction &DefInstruction, const Instruction &UseInstruction,
    const BitVector &ForbiddenRegisters) {
  auto CommonRegisters = UseInstruction.AllUseRegs;
  CommonRegisters &= DefInstruction.AllDefRegs;
  CommonRegisters.reset(ForbiddenRegisters);
  if (!CommonRegisters.empty()) {
    for (const MCPhysReg Reg : CommonRegisters.set_bits()) {
      AliasingRegisterOperands ARO;
      addOperandIfAlias(Reg, true, DefInstruction.Operands, ARO.Defs);
      addOperandIfAlias(Reg, false, UseInstruction.Operands, ARO.Uses);
      if (!ARO.Defs.empty() && !ARO.Uses.empty() &&
          !is_contained(Configurations, ARO))
        Configurations.push_back(std::move(ARO));
    }
  }
}

void DumpMCOperand(const MCRegisterInfo &MCRegisterInfo, const MCOperand &Op,
                   raw_ostream &OS) {
  if (!Op.isValid())
    OS << "Invalid";
  else if (Op.isReg())
    OS << MCRegisterInfo.getName(Op.getReg());
  else if (Op.isImm())
    OS << Op.getImm();
  else if (Op.isDFPImm())
    OS << bit_cast<double>(Op.getDFPImm());
  else if (Op.isSFPImm())
    OS << bit_cast<float>(Op.getSFPImm());
  else if (Op.isExpr())
    OS << "Expr";
  else if (Op.isInst())
    OS << "SubInst";
}

void DumpMCInst(const MCRegisterInfo &MCRegisterInfo,
                const MCInstrInfo &MCInstrInfo, const MCInst &MCInst,
                raw_ostream &OS) {
  OS << MCInstrInfo.getName(MCInst.getOpcode());
  for (unsigned I = 0, E = MCInst.getNumOperands(); I < E; ++I) {
    if (I > 0)
      OS << ',';
    OS << ' ';
    DumpMCOperand(MCRegisterInfo, MCInst.getOperand(I), OS);
  }
}

} // namespace exegesis
} // namespace llvm

//===- utils/TableGen/X86FoldTablesEmitter.cpp - X86 backend-*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This tablegen backend is responsible for emitting the memory fold tables of
// the X86 backend instructions.
//
//===----------------------------------------------------------------------===//

#include "Common/CodeGenInstruction.h"
#include "Common/CodeGenTarget.h"
#include "X86RecognizableInstr.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/X86FoldTablesUtils.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <set>

using namespace llvm;
using namespace X86Disassembler;

namespace {
// Represents an entry in the manual mapped instructions set.
struct ManualMapEntry {
  const char *RegInstStr;
  const char *MemInstStr;
  uint16_t Strategy;
};

// List of instructions requiring explicitly aligned memory.
const char *ExplicitAlign[] = {"MOVDQA",  "MOVAPS",  "MOVAPD",  "MOVNTPS",
                               "MOVNTPD", "MOVNTDQ", "MOVNTDQA"};

// List of instructions NOT requiring explicit memory alignment.
const char *ExplicitUnalign[] = {"MOVDQU",    "MOVUPS",    "MOVUPD",
                                 "PCMPESTRM", "PCMPESTRI", "PCMPISTRM",
                                 "PCMPISTRI"};

const ManualMapEntry ManualMapSet[] = {
#define ENTRY(REG, MEM, FLAGS) {#REG, #MEM, FLAGS},
#include "X86ManualFoldTables.def"
};

const std::set<StringRef> NoFoldSet = {
#define NOFOLD(INSN) #INSN,
#include "X86ManualFoldTables.def"
};

static bool isExplicitAlign(const CodeGenInstruction *Inst) {
  return any_of(ExplicitAlign, [Inst](const char *InstStr) {
    return Inst->TheDef->getName().contains(InstStr);
  });
}

static bool isExplicitUnalign(const CodeGenInstruction *Inst) {
  return any_of(ExplicitUnalign, [Inst](const char *InstStr) {
    return Inst->TheDef->getName().contains(InstStr);
  });
}

class X86FoldTablesEmitter {
  RecordKeeper &Records;
  CodeGenTarget Target;

  // Represents an entry in the folding table
  class X86FoldTableEntry {
    const CodeGenInstruction *RegInst;
    const CodeGenInstruction *MemInst;

  public:
    bool NoReverse = false;
    bool NoForward = false;
    bool FoldLoad = false;
    bool FoldStore = false;
    enum BcastType {
      BCAST_NONE,
      BCAST_W,
      BCAST_D,
      BCAST_Q,
      BCAST_SS,
      BCAST_SD,
      BCAST_SH,
    };
    BcastType BroadcastKind = BCAST_NONE;

    Align Alignment;

    X86FoldTableEntry() = default;
    X86FoldTableEntry(const CodeGenInstruction *RegInst,
                      const CodeGenInstruction *MemInst)
        : RegInst(RegInst), MemInst(MemInst) {}

    void print(raw_ostream &OS) const {
      OS.indent(2);
      OS << "{X86::" << RegInst->TheDef->getName() << ", ";
      OS << "X86::" << MemInst->TheDef->getName() << ", ";

      std::string Attrs;
      if (FoldLoad)
        Attrs += "TB_FOLDED_LOAD|";
      if (FoldStore)
        Attrs += "TB_FOLDED_STORE|";
      if (NoReverse)
        Attrs += "TB_NO_REVERSE|";
      if (NoForward)
        Attrs += "TB_NO_FORWARD|";
      if (Alignment != Align(1))
        Attrs += "TB_ALIGN_" + std::to_string(Alignment.value()) + "|";
      switch (BroadcastKind) {
      case BCAST_NONE:
        break;
      case BCAST_W:
        Attrs += "TB_BCAST_W|";
        break;
      case BCAST_D:
        Attrs += "TB_BCAST_D|";
        break;
      case BCAST_Q:
        Attrs += "TB_BCAST_Q|";
        break;
      case BCAST_SS:
        Attrs += "TB_BCAST_SS|";
        break;
      case BCAST_SD:
        Attrs += "TB_BCAST_SD|";
        break;
      case BCAST_SH:
        Attrs += "TB_BCAST_SH|";
        break;
      }

      StringRef SimplifiedAttrs = StringRef(Attrs).rtrim("|");
      if (SimplifiedAttrs.empty())
        SimplifiedAttrs = "0";

      OS << SimplifiedAttrs << "},\n";
    }

#ifndef NDEBUG
    // Check that Uses and Defs are same after memory fold.
    void checkCorrectness() const {
      auto &RegInstRec = *RegInst->TheDef;
      auto &MemInstRec = *MemInst->TheDef;
      auto ListOfUsesReg = RegInstRec.getValueAsListOfDefs("Uses");
      auto ListOfUsesMem = MemInstRec.getValueAsListOfDefs("Uses");
      auto ListOfDefsReg = RegInstRec.getValueAsListOfDefs("Defs");
      auto ListOfDefsMem = MemInstRec.getValueAsListOfDefs("Defs");
      if (ListOfUsesReg != ListOfUsesMem || ListOfDefsReg != ListOfDefsMem)
        report_fatal_error("Uses/Defs couldn't be changed after folding " +
                           RegInstRec.getName() + " to " +
                           MemInstRec.getName());
    }
#endif
  };

  // NOTE: We check the fold tables are sorted in X86InstrFoldTables.cpp by the
  // enum of the instruction, which is computed in
  // CodeGenTarget::ComputeInstrsByEnum. So we should use the same comparator
  // here.
  // FIXME: Could we share the code with CodeGenTarget::ComputeInstrsByEnum?
  struct CompareInstrsByEnum {
    bool operator()(const CodeGenInstruction *LHS,
                    const CodeGenInstruction *RHS) const {
      assert(LHS && RHS && "LHS and RHS shouldn't be nullptr");
      const auto &D1 = *LHS->TheDef;
      const auto &D2 = *RHS->TheDef;
      return std::tuple(!D1.getValueAsBit("isPseudo"), D1.getName()) <
             std::tuple(!D2.getValueAsBit("isPseudo"), D2.getName());
    }
  };

  typedef std::map<const CodeGenInstruction *, X86FoldTableEntry,
                   CompareInstrsByEnum>
      FoldTable;
  // Table2Addr - Holds instructions which their memory form performs
  //              load+store.
  //
  // Table#i - Holds instructions which the their memory form
  //           performs a load OR a store, and their #i'th operand is folded.
  //
  // BroadcastTable#i - Holds instructions which the their memory form performs
  //                    a broadcast load and their #i'th operand is folded.
  FoldTable Table2Addr;
  FoldTable Table0;
  FoldTable Table1;
  FoldTable Table2;
  FoldTable Table3;
  FoldTable Table4;
  FoldTable BroadcastTable1;
  FoldTable BroadcastTable2;
  FoldTable BroadcastTable3;
  FoldTable BroadcastTable4;

public:
  X86FoldTablesEmitter(RecordKeeper &R) : Records(R), Target(R) {}

  // run - Generate the 6 X86 memory fold tables.
  void run(raw_ostream &OS);

private:
  // Decides to which table to add the entry with the given instructions.
  // S sets the strategy of adding the TB_NO_REVERSE flag.
  void updateTables(const CodeGenInstruction *RegInst,
                    const CodeGenInstruction *MemInst, uint16_t S = 0,
                    bool IsManual = false, bool IsBroadcast = false);

  // Generates X86FoldTableEntry with the given instructions and fill it with
  // the appropriate flags, then adds it to a memory fold table.
  void addEntryWithFlags(FoldTable &Table, const CodeGenInstruction *RegInst,
                         const CodeGenInstruction *MemInst, uint16_t S,
                         unsigned FoldedIdx, bool IsManual);
  // Generates X86FoldTableEntry with the given instructions and adds it to a
  // broadcast table.
  void addBroadcastEntry(FoldTable &Table, const CodeGenInstruction *RegInst,
                         const CodeGenInstruction *MemInst);

  // Print the given table as a static const C++ array of type
  // X86FoldTableEntry.
  void printTable(const FoldTable &Table, StringRef TableName,
                  raw_ostream &OS) {
    OS << "static const X86FoldTableEntry " << TableName << "[] = {\n";

    for (auto &E : Table)
      E.second.print(OS);

    OS << "};\n\n";
  }
};

// Return true if one of the instruction's operands is a RST register class
static bool hasRSTRegClass(const CodeGenInstruction *Inst) {
  return any_of(Inst->Operands, [](const CGIOperandList::OperandInfo &OpIn) {
    return OpIn.Rec->getName() == "RST" || OpIn.Rec->getName() == "RSTi";
  });
}

// Return true if one of the instruction's operands is a ptr_rc_tailcall
static bool hasPtrTailcallRegClass(const CodeGenInstruction *Inst) {
  return any_of(Inst->Operands, [](const CGIOperandList::OperandInfo &OpIn) {
    return OpIn.Rec->getName() == "ptr_rc_tailcall";
  });
}

static uint8_t byteFromBitsInit(const BitsInit *B) {
  unsigned N = B->getNumBits();
  assert(N <= 8 && "Field is too large for uint8_t!");

  uint8_t Value = 0;
  for (unsigned I = 0; I != N; ++I) {
    BitInit *Bit = cast<BitInit>(B->getBit(I));
    Value |= Bit->getValue() << I;
  }
  return Value;
}

static bool mayFoldFromForm(uint8_t Form) {
  switch (Form) {
  default:
    return Form >= X86Local::MRM0r && Form <= X86Local::MRM7r;
  case X86Local::MRMXr:
  case X86Local::MRMXrCC:
  case X86Local::MRMDestReg:
  case X86Local::MRMSrcReg:
  case X86Local::MRMSrcReg4VOp3:
  case X86Local::MRMSrcRegOp4:
  case X86Local::MRMSrcRegCC:
    return true;
  }
}

static bool mayFoldToForm(uint8_t Form) {
  switch (Form) {
  default:
    return Form >= X86Local::MRM0m && Form <= X86Local::MRM7m;
  case X86Local::MRMXm:
  case X86Local::MRMXmCC:
  case X86Local::MRMDestMem:
  case X86Local::MRMSrcMem:
  case X86Local::MRMSrcMem4VOp3:
  case X86Local::MRMSrcMemOp4:
  case X86Local::MRMSrcMemCC:
    return true;
  }
}

static bool mayFoldFromLeftToRight(uint8_t LHS, uint8_t RHS) {
  switch (LHS) {
  default:
    llvm_unreachable("Unexpected Form!");
  case X86Local::MRM0r:
    return RHS == X86Local::MRM0m;
  case X86Local::MRM1r:
    return RHS == X86Local::MRM1m;
  case X86Local::MRM2r:
    return RHS == X86Local::MRM2m;
  case X86Local::MRM3r:
    return RHS == X86Local::MRM3m;
  case X86Local::MRM4r:
    return RHS == X86Local::MRM4m;
  case X86Local::MRM5r:
    return RHS == X86Local::MRM5m;
  case X86Local::MRM6r:
    return RHS == X86Local::MRM6m;
  case X86Local::MRM7r:
    return RHS == X86Local::MRM7m;
  case X86Local::MRMXr:
    return RHS == X86Local::MRMXm;
  case X86Local::MRMXrCC:
    return RHS == X86Local::MRMXmCC;
  case X86Local::MRMDestReg:
    return RHS == X86Local::MRMDestMem;
  case X86Local::MRMSrcReg:
    return RHS == X86Local::MRMSrcMem;
  case X86Local::MRMSrcReg4VOp3:
    return RHS == X86Local::MRMSrcMem4VOp3;
  case X86Local::MRMSrcRegOp4:
    return RHS == X86Local::MRMSrcMemOp4;
  case X86Local::MRMSrcRegCC:
    return RHS == X86Local::MRMSrcMemCC;
  }
}

static bool isNOREXRegClass(const Record *Op) {
  return Op->getName().contains("_NOREX");
}

// Function object - Operator() returns true if the given Reg instruction
// matches the Mem instruction of this object.
class IsMatch {
  const CodeGenInstruction *MemInst;
  const X86Disassembler::RecognizableInstrBase MemRI;
  bool IsBroadcast;
  const unsigned Variant;

public:
  IsMatch(const CodeGenInstruction *Inst, bool IsBroadcast, unsigned V)
      : MemInst(Inst), MemRI(*MemInst), IsBroadcast(IsBroadcast), Variant(V) {}

  bool operator()(const CodeGenInstruction *RegInst) {
    X86Disassembler::RecognizableInstrBase RegRI(*RegInst);
    const Record *RegRec = RegInst->TheDef;
    const Record *MemRec = MemInst->TheDef;

    // EVEX_B means different things for memory and register forms.
    // register form: rounding control or SAE
    // memory form: broadcast
    if (IsBroadcast && (RegRI.HasEVEX_B || !MemRI.HasEVEX_B))
      return false;
    // EVEX_B indicates NDD for MAP4 instructions
    if (!IsBroadcast && (RegRI.HasEVEX_B || MemRI.HasEVEX_B) &&
        RegRI.OpMap != X86Local::T_MAP4)
      return false;

    if (!mayFoldFromLeftToRight(RegRI.Form, MemRI.Form))
      return false;

    // X86 encoding is crazy, e.g
    //
    // f3 0f c7 30       vmxon   (%rax)
    // f3 0f c7 f0       senduipi        %rax
    //
    // This two instruction have similiar encoding fields but are unrelated
    if (X86Disassembler::getMnemonic(MemInst, Variant) !=
        X86Disassembler::getMnemonic(RegInst, Variant))
      return false;

    // Return false if any of the following fields of does not match.
    if (std::tuple(RegRI.Encoding, RegRI.Opcode, RegRI.OpPrefix, RegRI.OpMap,
                   RegRI.OpSize, RegRI.AdSize, RegRI.HasREX_W, RegRI.HasVEX_4V,
                   RegRI.HasVEX_L, RegRI.IgnoresVEX_L, RegRI.IgnoresW,
                   RegRI.HasEVEX_K, RegRI.HasEVEX_KZ, RegRI.HasEVEX_L2,
                   RegRI.HasEVEX_NF, RegRec->getValueAsBit("hasEVEX_RC"),
                   RegRec->getValueAsBit("hasLockPrefix"),
                   RegRec->getValueAsBit("hasNoTrackPrefix")) !=
        std::tuple(MemRI.Encoding, MemRI.Opcode, MemRI.OpPrefix, MemRI.OpMap,
                   MemRI.OpSize, MemRI.AdSize, MemRI.HasREX_W, MemRI.HasVEX_4V,
                   MemRI.HasVEX_L, MemRI.IgnoresVEX_L, MemRI.IgnoresW,
                   MemRI.HasEVEX_K, MemRI.HasEVEX_KZ, MemRI.HasEVEX_L2,
                   MemRI.HasEVEX_NF, MemRec->getValueAsBit("hasEVEX_RC"),
                   MemRec->getValueAsBit("hasLockPrefix"),
                   MemRec->getValueAsBit("hasNoTrackPrefix")))
      return false;

    // Make sure the sizes of the operands of both instructions suit each other.
    // This is needed for instructions with intrinsic version (_Int).
    // Where the only difference is the size of the operands.
    // For example: VUCOMISDZrm and VUCOMISDrm_Int
    // Also for instructions that their EVEX version was upgraded to work with
    // k-registers. For example VPCMPEQBrm (xmm output register) and
    // VPCMPEQBZ128rm (k register output register).
    unsigned MemOutSize = MemRec->getValueAsDag("OutOperandList")->getNumArgs();
    unsigned RegOutSize = RegRec->getValueAsDag("OutOperandList")->getNumArgs();
    unsigned MemInSize = MemRec->getValueAsDag("InOperandList")->getNumArgs();
    unsigned RegInSize = RegRec->getValueAsDag("InOperandList")->getNumArgs();

    // Instructions with one output in their memory form use the memory folded
    // operand as source and destination (Read-Modify-Write).
    unsigned RegStartIdx =
        (MemOutSize + 1 == RegOutSize) && (MemInSize == RegInSize) ? 1 : 0;

    bool FoundFoldedOp = false;
    for (unsigned I = 0, E = MemInst->Operands.size(); I != E; I++) {
      Record *MemOpRec = MemInst->Operands[I].Rec;
      Record *RegOpRec = RegInst->Operands[I + RegStartIdx].Rec;

      if (MemOpRec == RegOpRec)
        continue;

      if (isRegisterOperand(MemOpRec) && isRegisterOperand(RegOpRec) &&
          ((getRegOperandSize(MemOpRec) != getRegOperandSize(RegOpRec)) ||
           (isNOREXRegClass(MemOpRec) != isNOREXRegClass(RegOpRec))))
        return false;

      if (isMemoryOperand(MemOpRec) && isMemoryOperand(RegOpRec) &&
          (getMemOperandSize(MemOpRec) != getMemOperandSize(RegOpRec)))
        return false;

      if (isImmediateOperand(MemOpRec) && isImmediateOperand(RegOpRec) &&
          (MemOpRec->getValueAsDef("Type") != RegOpRec->getValueAsDef("Type")))
        return false;

      // Only one operand can be folded.
      if (FoundFoldedOp)
        return false;

      assert(isRegisterOperand(RegOpRec) && isMemoryOperand(MemOpRec));
      FoundFoldedOp = true;
    }

    return FoundFoldedOp;
  }
};

} // end anonymous namespace

void X86FoldTablesEmitter::addEntryWithFlags(FoldTable &Table,
                                             const CodeGenInstruction *RegInst,
                                             const CodeGenInstruction *MemInst,
                                             uint16_t S, unsigned FoldedIdx,
                                             bool IsManual) {

  assert((IsManual || Table.find(RegInst) == Table.end()) &&
         "Override entry unexpectedly");
  X86FoldTableEntry Result = X86FoldTableEntry(RegInst, MemInst);
  Record *RegRec = RegInst->TheDef;
  Result.NoReverse = S & TB_NO_REVERSE;
  Result.NoForward = S & TB_NO_FORWARD;
  Result.FoldLoad = S & TB_FOLDED_LOAD;
  Result.FoldStore = S & TB_FOLDED_STORE;
  Result.Alignment = Align(1ULL << ((S & TB_ALIGN_MASK) >> TB_ALIGN_SHIFT));
  if (IsManual) {
    Table[RegInst] = Result;
    return;
  }

  Record *RegOpRec = RegInst->Operands[FoldedIdx].Rec;
  Record *MemOpRec = MemInst->Operands[FoldedIdx].Rec;

  // Unfolding code generates a load/store instruction according to the size of
  // the register in the register form instruction.
  // If the register's size is greater than the memory's operand size, do not
  // allow unfolding.

  // the unfolded load size will be based on the register size. If that’s bigger
  // than the memory operand size, the unfolded load will load more memory and
  // potentially cause a memory fault.
  if (getRegOperandSize(RegOpRec) > getMemOperandSize(MemOpRec))
    Result.NoReverse = true;

  // Check no-kz version's isMoveReg
  StringRef RegInstName = RegRec->getName();
  unsigned DropLen =
      RegInstName.ends_with("rkz") ? 2 : (RegInstName.ends_with("rk") ? 1 : 0);
  Record *BaseDef =
      DropLen ? Records.getDef(RegInstName.drop_back(DropLen)) : nullptr;
  bool IsMoveReg =
      BaseDef ? Target.getInstruction(BaseDef).isMoveReg : RegInst->isMoveReg;
  // A masked load can not be unfolded to a full load, otherwise it would access
  // unexpected memory. A simple store can not be unfolded.
  if (IsMoveReg && (BaseDef || Result.FoldStore))
    Result.NoReverse = true;

  uint8_t Enc = byteFromBitsInit(RegRec->getValueAsBitsInit("OpEncBits"));
  if (isExplicitAlign(RegInst)) {
    // The instruction require explicitly aligned memory.
    BitsInit *VectSize = RegRec->getValueAsBitsInit("VectSize");
    Result.Alignment = Align(byteFromBitsInit(VectSize));
  } else if (!Enc && !isExplicitUnalign(RegInst) &&
             getMemOperandSize(MemOpRec) > 64) {
    // Instructions with XOP/VEX/EVEX encoding do not require alignment while
    // SSE packed vector instructions require a 16 byte alignment.
    Result.Alignment = Align(16);
  }
  // Expand is only ever created as a masked instruction. It is not safe to
  // unfold a masked expand because we don't know if it came from an expand load
  // intrinsic or folding a plain load. If it is from a expand load intrinsic,
  // Unfolding to plain load would read more elements and could trigger a fault.
  if (RegRec->getName().contains("EXPAND"))
    Result.NoReverse = true;

  Table[RegInst] = Result;
}

void X86FoldTablesEmitter::addBroadcastEntry(
    FoldTable &Table, const CodeGenInstruction *RegInst,
    const CodeGenInstruction *MemInst) {

  assert(Table.find(RegInst) == Table.end() && "Override entry unexpectedly");
  X86FoldTableEntry Result = X86FoldTableEntry(RegInst, MemInst);

  DagInit *In = MemInst->TheDef->getValueAsDag("InOperandList");
  for (unsigned I = 0, E = In->getNumArgs(); I != E; ++I) {
    Result.BroadcastKind =
        StringSwitch<X86FoldTableEntry::BcastType>(In->getArg(I)->getAsString())
            .Case("i16mem", X86FoldTableEntry::BCAST_W)
            .Case("i32mem", X86FoldTableEntry::BCAST_D)
            .Case("i64mem", X86FoldTableEntry::BCAST_Q)
            .Case("f16mem", X86FoldTableEntry::BCAST_SH)
            .Case("f32mem", X86FoldTableEntry::BCAST_SS)
            .Case("f64mem", X86FoldTableEntry::BCAST_SD)
            .Default(X86FoldTableEntry::BCAST_NONE);
    if (Result.BroadcastKind != X86FoldTableEntry::BCAST_NONE)
      break;
  }
  assert(Result.BroadcastKind != X86FoldTableEntry::BCAST_NONE &&
         "Unknown memory operand for broadcast");

  Table[RegInst] = Result;
}

void X86FoldTablesEmitter::updateTables(const CodeGenInstruction *RegInst,
                                        const CodeGenInstruction *MemInst,
                                        uint16_t S, bool IsManual,
                                        bool IsBroadcast) {

  Record *RegRec = RegInst->TheDef;
  Record *MemRec = MemInst->TheDef;
  unsigned MemOutSize = MemRec->getValueAsDag("OutOperandList")->getNumArgs();
  unsigned RegOutSize = RegRec->getValueAsDag("OutOperandList")->getNumArgs();
  unsigned MemInSize = MemRec->getValueAsDag("InOperandList")->getNumArgs();
  unsigned RegInSize = RegRec->getValueAsDag("InOperandList")->getNumArgs();

  // Instructions which Read-Modify-Write should be added to Table2Addr.
  if (!MemOutSize && RegOutSize == 1 && MemInSize == RegInSize) {
    assert(!IsBroadcast && "Read-Modify-Write can not be broadcast");
    // X86 would not unfold Read-Modify-Write instructions so add TB_NO_REVERSE.
    addEntryWithFlags(Table2Addr, RegInst, MemInst, S | TB_NO_REVERSE, 0,
                      IsManual);
    return;
  }

  // Only table0 entries should explicitly specify a load or store flag.
  // If the instruction writes to the folded operand, it will appear as
  // an output in the register form instruction and as an input in the
  // memory form instruction. If the instruction reads from the folded
  // operand, it will appear as in input in both forms.
  if (MemInSize == RegInSize && MemOutSize == RegOutSize) {
    // Load-Folding cases.
    // If the i'th register form operand is a register and the i'th memory form
    // operand is a memory operand, add instructions to Table#i.
    for (unsigned I = RegOutSize, E = RegInst->Operands.size(); I < E; I++) {
      Record *RegOpRec = RegInst->Operands[I].Rec;
      Record *MemOpRec = MemInst->Operands[I].Rec;
      // PointerLikeRegClass: For instructions like TAILJMPr, TAILJMPr64,
      // TAILJMPr64_REX
      if ((isRegisterOperand(RegOpRec) ||
           RegOpRec->isSubClassOf("PointerLikeRegClass")) &&
          isMemoryOperand(MemOpRec)) {
        switch (I) {
        case 0:
          assert(!IsBroadcast && "BroadcastTable0 needs to be added");
          addEntryWithFlags(Table0, RegInst, MemInst, S | TB_FOLDED_LOAD, 0,
                            IsManual);
          return;
        case 1:
          IsBroadcast
              ? addBroadcastEntry(BroadcastTable1, RegInst, MemInst)
              : addEntryWithFlags(Table1, RegInst, MemInst, S, 1, IsManual);
          return;
        case 2:
          IsBroadcast
              ? addBroadcastEntry(BroadcastTable2, RegInst, MemInst)
              : addEntryWithFlags(Table2, RegInst, MemInst, S, 2, IsManual);
          return;
        case 3:
          IsBroadcast
              ? addBroadcastEntry(BroadcastTable3, RegInst, MemInst)
              : addEntryWithFlags(Table3, RegInst, MemInst, S, 3, IsManual);
          return;
        case 4:
          IsBroadcast
              ? addBroadcastEntry(BroadcastTable4, RegInst, MemInst)
              : addEntryWithFlags(Table4, RegInst, MemInst, S, 4, IsManual);
          return;
        }
      }
    }
  } else if (MemInSize == RegInSize + 1 && MemOutSize + 1 == RegOutSize) {
    // Store-Folding cases.
    // If the memory form instruction performs a store, the *output*
    // register of the register form instructions disappear and instead a
    // memory *input* operand appears in the memory form instruction.
    // For example:
    //   MOVAPSrr => (outs VR128:$dst), (ins VR128:$src)
    //   MOVAPSmr => (outs), (ins f128mem:$dst, VR128:$src)
    Record *RegOpRec = RegInst->Operands[RegOutSize - 1].Rec;
    Record *MemOpRec = MemInst->Operands[RegOutSize - 1].Rec;
    if (isRegisterOperand(RegOpRec) && isMemoryOperand(MemOpRec) &&
        getRegOperandSize(RegOpRec) == getMemOperandSize(MemOpRec)) {
      assert(!IsBroadcast && "Store can not be broadcast");
      addEntryWithFlags(Table0, RegInst, MemInst, S | TB_FOLDED_STORE, 0,
                        IsManual);
    }
  }
}

void X86FoldTablesEmitter::run(raw_ostream &OS) {
  // Holds all memory instructions
  std::vector<const CodeGenInstruction *> MemInsts;
  // Holds all register instructions - divided according to opcode.
  std::map<uint8_t, std::vector<const CodeGenInstruction *>> RegInsts;

  ArrayRef<const CodeGenInstruction *> NumberedInstructions =
      Target.getInstructionsByEnumValue();

  for (const CodeGenInstruction *Inst : NumberedInstructions) {
    const Record *Rec = Inst->TheDef;
    if (!Rec->isSubClassOf("X86Inst") || Rec->getValueAsBit("isAsmParserOnly"))
      continue;

    if (NoFoldSet.find(Rec->getName()) != NoFoldSet.end())
      continue;

    // Promoted legacy instruction is in EVEX space, and has REX2-encoding
    // alternative. It's added due to HW design and never emitted by compiler.
    if (byteFromBitsInit(Rec->getValueAsBitsInit("OpMapBits")) ==
            X86Local::T_MAP4 &&
        byteFromBitsInit(Rec->getValueAsBitsInit("explicitOpPrefixBits")) ==
            X86Local::ExplicitEVEX)
      continue;

    // - Instructions including RST register class operands are not relevant
    //   for memory folding (for further details check the explanation in
    //   lib/Target/X86/X86InstrFPStack.td file).
    // - Some instructions (listed in the manual map above) use the register
    //   class ptr_rc_tailcall, which can be of a size 32 or 64, to ensure
    //   safe mapping of these instruction we manually map them and exclude
    //   them from the automation.
    if (hasRSTRegClass(Inst) || hasPtrTailcallRegClass(Inst))
      continue;

    // Add all the memory form instructions to MemInsts, and all the register
    // form instructions to RegInsts[Opc], where Opc is the opcode of each
    // instructions. this helps reducing the runtime of the backend.
    const BitsInit *FormBits = Rec->getValueAsBitsInit("FormBits");
    uint8_t Form = byteFromBitsInit(FormBits);
    if (mayFoldToForm(Form))
      MemInsts.push_back(Inst);
    else if (mayFoldFromForm(Form)) {
      uint8_t Opc = byteFromBitsInit(Rec->getValueAsBitsInit("Opcode"));
      RegInsts[Opc].push_back(Inst);
    }
  }

  // Create a copy b/c the register instruction will removed when a new entry is
  // added into memory fold tables.
  auto RegInstsForBroadcast = RegInsts;

  Record *AsmWriter = Target.getAsmWriter();
  unsigned Variant = AsmWriter->getValueAsInt("Variant");
  auto FixUp = [&](const CodeGenInstruction *RegInst) {
    StringRef RegInstName = RegInst->TheDef->getName();
    if (RegInstName.ends_with("_REV") || RegInstName.ends_with("_alt"))
      if (auto *RegAltRec = Records.getDef(RegInstName.drop_back(4)))
        RegInst = &Target.getInstruction(RegAltRec);
    return RegInst;
  };
  // For each memory form instruction, try to find its register form
  // instruction.
  for (const CodeGenInstruction *MemInst : MemInsts) {
    uint8_t Opc =
        byteFromBitsInit(MemInst->TheDef->getValueAsBitsInit("Opcode"));

    auto RegInstsIt = RegInsts.find(Opc);
    if (RegInstsIt == RegInsts.end())
      continue;

    // Two forms (memory & register) of the same instruction must have the same
    // opcode.
    std::vector<const CodeGenInstruction *> &OpcRegInsts = RegInstsIt->second;

    // Memory fold tables
    auto Match =
        find_if(OpcRegInsts, IsMatch(MemInst, /*IsBroadcast=*/false, Variant));
    if (Match != OpcRegInsts.end()) {
      updateTables(FixUp(*Match), MemInst);
      OpcRegInsts.erase(Match);
    }

    // Broadcast tables
    StringRef MemInstName = MemInst->TheDef->getName();
    if (!MemInstName.contains("mb") && !MemInstName.contains("mib"))
      continue;
    RegInstsIt = RegInstsForBroadcast.find(Opc);
    assert(RegInstsIt != RegInstsForBroadcast.end() &&
           "Unexpected control flow");
    std::vector<const CodeGenInstruction *> &OpcRegInstsForBroadcast =
        RegInstsIt->second;
    Match = find_if(OpcRegInstsForBroadcast,
                    IsMatch(MemInst, /*IsBroadcast=*/true, Variant));
    if (Match != OpcRegInstsForBroadcast.end()) {
      updateTables(FixUp(*Match), MemInst, 0, /*IsManual=*/false,
                   /*IsBroadcast=*/true);
      OpcRegInstsForBroadcast.erase(Match);
    }
  }

  // Add the manually mapped instructions listed above.
  for (const ManualMapEntry &Entry : ManualMapSet) {
    Record *RegInstIter = Records.getDef(Entry.RegInstStr);
    Record *MemInstIter = Records.getDef(Entry.MemInstStr);

    updateTables(&(Target.getInstruction(RegInstIter)),
                 &(Target.getInstruction(MemInstIter)), Entry.Strategy, true);
  }

#ifndef NDEBUG
  auto CheckMemFoldTable = [](const FoldTable &Table) -> void {
    for (const auto &Record : Table) {
      auto &FoldEntry = Record.second;
      FoldEntry.checkCorrectness();
    }
  };
  CheckMemFoldTable(Table2Addr);
  CheckMemFoldTable(Table0);
  CheckMemFoldTable(Table1);
  CheckMemFoldTable(Table2);
  CheckMemFoldTable(Table3);
  CheckMemFoldTable(Table4);
  CheckMemFoldTable(BroadcastTable1);
  CheckMemFoldTable(BroadcastTable2);
  CheckMemFoldTable(BroadcastTable3);
  CheckMemFoldTable(BroadcastTable4);
#endif
#define PRINT_TABLE(TABLE) printTable(TABLE, #TABLE, OS);
  // Print all tables.
  PRINT_TABLE(Table2Addr)
  PRINT_TABLE(Table0)
  PRINT_TABLE(Table1)
  PRINT_TABLE(Table2)
  PRINT_TABLE(Table3)
  PRINT_TABLE(Table4)
  PRINT_TABLE(BroadcastTable1)
  PRINT_TABLE(BroadcastTable2)
  PRINT_TABLE(BroadcastTable3)
  PRINT_TABLE(BroadcastTable4)
}

static TableGen::Emitter::OptClass<X86FoldTablesEmitter>
    X("gen-x86-fold-tables", "Generate X86 fold tables");

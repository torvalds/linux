//===-- SystemZAsmPrinter.h - SystemZ LLVM assembly printer ----*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZASMPRINTER_H
#define LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZASMPRINTER_H

#include "SystemZMCInstLower.h"
#include "SystemZTargetMachine.h"
#include "SystemZTargetStreamer.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/StackMaps.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class MCStreamer;
class MachineInstr;
class Module;
class raw_ostream;

class LLVM_LIBRARY_VISIBILITY SystemZAsmPrinter : public AsmPrinter {
private:
  MCSymbol *CurrentFnPPA1Sym;     // PPA1 Symbol.
  MCSymbol *CurrentFnEPMarkerSym; // Entry Point Marker.
  MCSymbol *PPA2Sym;

  SystemZTargetStreamer *getTargetStreamer() {
    MCTargetStreamer *TS = OutStreamer->getTargetStreamer();
    assert(TS && "do not have a target streamer");
    return static_cast<SystemZTargetStreamer *>(TS);
  }

  /// Call type information for XPLINK.
  enum class CallType {
    BASR76 = 0,   // b'x000' == BASR  r7,r6
    BRAS7 = 1,    // b'x001' == BRAS  r7,ep
    RESVD_2 = 2,  // b'x010'
    BRASL7 = 3,   // b'x011' == BRASL r7,ep
    RESVD_4 = 4,  // b'x100'
    RESVD_5 = 5,  // b'x101'
    BALR1415 = 6, // b'x110' == BALR  r14,r15
    BASR33 = 7,   // b'x111' == BASR  r3,r3
  };

  // The Associated Data Area (ADA) contains descriptors which help locating
  // external symbols. For each symbol and type, the displacement into the ADA
  // is stored.
  class AssociatedDataAreaTable {
  public:
    using DisplacementTable =
        MapVector<std::pair<const MCSymbol *, unsigned>, uint32_t>;

  private:
    const uint64_t PointerSize;

    /// The mapping of name/slot type pairs to displacements.
    DisplacementTable Displacements;

    /// The next available displacement value. Incremented when new entries into
    /// the ADA are created.
    uint32_t NextDisplacement = 0;

  public:
    AssociatedDataAreaTable(uint64_t PointerSize) : PointerSize(PointerSize) {}

    /// @brief Add a function descriptor to the ADA.
    /// @param MI Pointer to an ADA_ENTRY instruction.
    /// @return The displacement of the descriptor into the ADA.
    uint32_t insert(const MachineOperand MO);

    /// @brief Get the displacement into associated data area (ADA) for a name.
    /// If no  displacement is already associated with the name, assign one and
    /// return it.
    /// @param Sym The symbol for which the displacement should be returned.
    /// @param SlotKind The ADA type.
    /// @return The displacement of the descriptor into the ADA.
    uint32_t insert(const MCSymbol *Sym, unsigned SlotKind);

    /// Get the table of GOFF displacements.  This is 'const' since it should
    /// never be modified by anything except the APIs on this class.
    const DisplacementTable &getTable() const { return Displacements; }

    uint32_t getNextDisplacement() const { return NextDisplacement; }
  };

  AssociatedDataAreaTable ADATable;

  void emitPPA1(MCSymbol *FnEndSym);
  void emitPPA2(Module &M);
  void emitADASection();
  void emitIDRLSection(Module &M);

public:
  SystemZAsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer)), CurrentFnPPA1Sym(nullptr),
        CurrentFnEPMarkerSym(nullptr), PPA2Sym(nullptr),
        ADATable(TM.getPointerSize(0)) {}

  // Override AsmPrinter.
  StringRef getPassName() const override { return "SystemZ Assembly Printer"; }
  void emitInstruction(const MachineInstr *MI) override;
  void emitMachineConstantPoolValue(MachineConstantPoolValue *MCPV) override;
  void emitEndOfAsmFile(Module &M) override;
  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                       const char *ExtraCode, raw_ostream &OS) override;
  bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                             const char *ExtraCode, raw_ostream &OS) override;

  bool doInitialization(Module &M) override {
    SM.reset();
    return AsmPrinter::doInitialization(M);
  }
  void emitFunctionEntryLabel() override;
  void emitFunctionBodyEnd() override;
  void emitStartOfAsmFile(Module &M) override;

private:
  void emitCallInformation(CallType CT);
  void LowerFENTRY_CALL(const MachineInstr &MI, SystemZMCInstLower &MCIL);
  void LowerSTACKMAP(const MachineInstr &MI);
  void LowerPATCHPOINT(const MachineInstr &MI, SystemZMCInstLower &Lower);
  void emitAttributes(Module &M);
};
} // end namespace llvm

#endif

//===- HexagonVectorPrint.cpp - Generate vector printing instructions -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass adds the capability to generate pseudo vector/predicate register
// printing instructions. These pseudo instructions should be used with the
// simulator, NEVER on hardware.
//
//===----------------------------------------------------------------------===//

#include "HexagonInstrInfo.h"
#include "HexagonSubtarget.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <string>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "hexagon-vector-print"

static cl::opt<bool>
    TraceHexVectorStoresOnly("trace-hex-vector-stores-only", cl::Hidden,
                             cl::desc("Enables tracing of vector stores"));

namespace llvm {

FunctionPass *createHexagonVectorPrint();
void initializeHexagonVectorPrintPass(PassRegistry&);

} // end namespace llvm

namespace {

class HexagonVectorPrint : public MachineFunctionPass {
  const HexagonSubtarget *QST = nullptr;
  const HexagonInstrInfo *QII = nullptr;
  const HexagonRegisterInfo *QRI = nullptr;

public:
  static char ID;

  HexagonVectorPrint() : MachineFunctionPass(ID) {
    initializeHexagonVectorPrintPass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override { return "Hexagon VectorPrint pass"; }

  bool runOnMachineFunction(MachineFunction &Fn) override;
};

} // end anonymous namespace

char HexagonVectorPrint::ID = 0;

static bool isVecReg(unsigned Reg) {
  return (Reg >= Hexagon::V0 && Reg <= Hexagon::V31) ||
         (Reg >= Hexagon::W0 && Reg <= Hexagon::W15) ||
         (Reg >= Hexagon::WR0 && Reg <= Hexagon::WR15) ||
         (Reg >= Hexagon::Q0 && Reg <= Hexagon::Q3);
}

static std::string getStringReg(unsigned R) {
  if (R >= Hexagon::V0 && R <= Hexagon::V31) {
    static const char* S[] = { "20", "21", "22", "23", "24", "25", "26", "27",
                        "28", "29", "2a", "2b", "2c", "2d", "2e", "2f",
                        "30", "31", "32", "33", "34", "35", "36", "37",
                        "38", "39", "3a", "3b", "3c", "3d", "3e", "3f"};
    return S[R-Hexagon::V0];
  }
  if (R >= Hexagon::Q0 && R <= Hexagon::Q3) {
    static const char* S[] = { "00", "01", "02", "03"};
    return S[R-Hexagon::Q0];

  }
  llvm_unreachable("valid vreg");
}

static void addAsmInstr(MachineBasicBlock *MBB, unsigned Reg,
                        MachineBasicBlock::instr_iterator I,
                        const DebugLoc &DL, const HexagonInstrInfo *QII,
                        MachineFunction &Fn) {
  std::string VDescStr = ".long 0x1dffe0" + getStringReg(Reg);
  const char *cstr = Fn.createExternalSymbolName(VDescStr);
  unsigned ExtraInfo = InlineAsm::Extra_HasSideEffects;
  BuildMI(*MBB, I, DL, QII->get(TargetOpcode::INLINEASM))
    .addExternalSymbol(cstr)
    .addImm(ExtraInfo);
}

static bool getInstrVecReg(const MachineInstr &MI, unsigned &Reg) {
  if (MI.getNumOperands() < 1) return false;
  // Vec load or compute.
  if (MI.getOperand(0).isReg() && MI.getOperand(0).isDef()) {
    Reg = MI.getOperand(0).getReg();
    if (isVecReg(Reg))
      return !TraceHexVectorStoresOnly;
  }
  // Vec store.
  if (MI.mayStore() && MI.getNumOperands() >= 3 && MI.getOperand(2).isReg()) {
    Reg = MI.getOperand(2).getReg();
    if (isVecReg(Reg))
      return true;
  }
  // Vec store post increment.
  if (MI.mayStore() && MI.getNumOperands() >= 4 && MI.getOperand(3).isReg()) {
    Reg = MI.getOperand(3).getReg();
    if (isVecReg(Reg))
      return true;
  }
  return false;
}

bool HexagonVectorPrint::runOnMachineFunction(MachineFunction &Fn) {
  bool Changed = false;
  QST = &Fn.getSubtarget<HexagonSubtarget>();
  QRI = QST->getRegisterInfo();
  QII = QST->getInstrInfo();
  std::vector<MachineInstr *> VecPrintList;
  for (auto &MBB : Fn)
    for (auto &MI : MBB) {
      if (MI.isBundle()) {
        MachineBasicBlock::instr_iterator MII = MI.getIterator();
        for (++MII; MII != MBB.instr_end() && MII->isInsideBundle(); ++MII) {
          if (MII->getNumOperands() < 1)
            continue;
          unsigned Reg = 0;
          if (getInstrVecReg(*MII, Reg)) {
            VecPrintList.push_back((&*MII));
            LLVM_DEBUG(dbgs() << "Found vector reg inside bundle \n";
                       MII->dump());
          }
        }
      } else {
        unsigned Reg = 0;
        if (getInstrVecReg(MI, Reg)) {
          VecPrintList.push_back(&MI);
          LLVM_DEBUG(dbgs() << "Found vector reg \n"; MI.dump());
        }
      }
    }

  Changed = !VecPrintList.empty();
  if (!Changed)
    return Changed;

  for (auto *I : VecPrintList) {
    DebugLoc DL = I->getDebugLoc();
    MachineBasicBlock *MBB = I->getParent();
    LLVM_DEBUG(dbgs() << "Evaluating V MI\n"; I->dump());
    unsigned Reg = 0;
    if (!getInstrVecReg(*I, Reg))
      llvm_unreachable("Need a vector reg");
    MachineBasicBlock::instr_iterator MII = I->getIterator();
    if (I->isInsideBundle()) {
      LLVM_DEBUG(dbgs() << "add to end of bundle\n"; I->dump());
      while (MBB->instr_end() != MII && MII->isInsideBundle())
        MII++;
    } else {
      LLVM_DEBUG(dbgs() << "add after instruction\n"; I->dump());
      MII++;
    }
    if (MBB->instr_end() == MII)
      continue;

    if (Reg >= Hexagon::V0 && Reg <= Hexagon::V31) {
      LLVM_DEBUG(dbgs() << "adding dump for V" << Reg - Hexagon::V0 << '\n');
      addAsmInstr(MBB, Reg, MII, DL, QII, Fn);
    } else if (Reg >= Hexagon::W0 && Reg <= Hexagon::W15) {
      LLVM_DEBUG(dbgs() << "adding dump for W" << Reg - Hexagon::W0 << '\n');
      addAsmInstr(MBB, Hexagon::V0 + (Reg - Hexagon::W0) * 2 + 1,
                  MII, DL, QII, Fn);
      addAsmInstr(MBB, Hexagon::V0 + (Reg - Hexagon::W0) * 2,
                   MII, DL, QII, Fn);
    } else if (Reg >= Hexagon::Q0 && Reg <= Hexagon::Q3) {
      LLVM_DEBUG(dbgs() << "adding dump for Q" << Reg - Hexagon::Q0 << '\n');
      addAsmInstr(MBB, Reg, MII, DL, QII, Fn);
    } else
      llvm_unreachable("Bad Vector reg");
  }
  return Changed;
}

//===----------------------------------------------------------------------===//
//                         Public Constructor Functions
//===----------------------------------------------------------------------===//
INITIALIZE_PASS(HexagonVectorPrint, "hexagon-vector-print",
  "Hexagon VectorPrint pass", false, false)

FunctionPass *llvm::createHexagonVectorPrint() {
  return new HexagonVectorPrint();
}

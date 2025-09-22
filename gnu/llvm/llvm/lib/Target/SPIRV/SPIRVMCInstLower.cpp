//=- SPIRVMCInstLower.cpp - Convert SPIR-V MachineInstr to MCInst -*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains code to lower SPIR-V MachineInstrs to their corresponding
// MCInst records.
//
//===----------------------------------------------------------------------===//

#include "SPIRVMCInstLower.h"
#include "SPIRV.h"
#include "SPIRVModuleAnalysis.h"
#include "SPIRVUtils.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/IR/Constants.h"

using namespace llvm;

void SPIRVMCInstLower::lower(const MachineInstr *MI, MCInst &OutMI,
                             SPIRV::ModuleAnalysisInfo *MAI) const {
  OutMI.setOpcode(MI->getOpcode());
  // Propagate previously set flags
  OutMI.setFlags(MI->getAsmPrinterFlags());
  const MachineFunction *MF = MI->getMF();
  for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI->getOperand(i);
    MCOperand MCOp;
    switch (MO.getType()) {
    default:
      llvm_unreachable("unknown operand type");
    case MachineOperand::MO_GlobalAddress: {
      Register FuncReg = MAI->getFuncReg(dyn_cast<Function>(MO.getGlobal()));
      if (!FuncReg.isValid()) {
        std::string DiagMsg;
        raw_string_ostream OS(DiagMsg);
        MI->print(OS);
        DiagMsg = "Unknown function in:" + DiagMsg;
        report_fatal_error(DiagMsg.c_str());
      }
      MCOp = MCOperand::createReg(FuncReg);
      break;
    }
    case MachineOperand::MO_MachineBasicBlock:
      MCOp = MCOperand::createReg(MAI->getOrCreateMBBRegister(*MO.getMBB()));
      break;
    case MachineOperand::MO_Register: {
      Register NewReg = MAI->getRegisterAlias(MF, MO.getReg());
      MCOp = MCOperand::createReg(NewReg.isValid() ? NewReg : MO.getReg());
      break;
    }
    case MachineOperand::MO_Immediate:
      if (MI->getOpcode() == SPIRV::OpExtInst && i == 2) {
        Register Reg = MAI->getExtInstSetReg(MO.getImm());
        MCOp = MCOperand::createReg(Reg);
      } else {
        MCOp = MCOperand::createImm(MO.getImm());
      }
      break;
    case MachineOperand::MO_FPImmediate:
      MCOp = MCOperand::createDFPImm(
          MO.getFPImm()->getValueAPF().convertToFloat());
      break;
    }

    OutMI.addOperand(MCOp);
  }
}

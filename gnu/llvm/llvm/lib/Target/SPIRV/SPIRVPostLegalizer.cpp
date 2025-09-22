//===-- SPIRVPostLegalizer.cpp - ammend info after legalization -*- C++ -*-===//
//
// which may appear after the legalizer pass
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The pass partially apply pre-legalization logic to new instructions inserted
// as a result of legalization:
// - assigns SPIR-V types to registers for new instructions.
//
//===----------------------------------------------------------------------===//

#include "SPIRV.h"
#include "SPIRVSubtarget.h"
#include "SPIRVUtils.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/IntrinsicsSPIRV.h"
#include "llvm/Target/TargetIntrinsicInfo.h"

#define DEBUG_TYPE "spirv-postlegalizer"

using namespace llvm;

namespace {
class SPIRVPostLegalizer : public MachineFunctionPass {
public:
  static char ID;
  SPIRVPostLegalizer() : MachineFunctionPass(ID) {
    initializeSPIRVPostLegalizerPass(*PassRegistry::getPassRegistry());
  }
  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // namespace

// Defined in SPIRVLegalizerInfo.cpp.
extern bool isTypeFoldingSupported(unsigned Opcode);

namespace llvm {
//  Defined in SPIRVPreLegalizer.cpp.
extern Register insertAssignInstr(Register Reg, Type *Ty, SPIRVType *SpirvTy,
                                  SPIRVGlobalRegistry *GR,
                                  MachineIRBuilder &MIB,
                                  MachineRegisterInfo &MRI);
extern void processInstr(MachineInstr &MI, MachineIRBuilder &MIB,
                         MachineRegisterInfo &MRI, SPIRVGlobalRegistry *GR);
} // namespace llvm

static bool isMetaInstrGET(unsigned Opcode) {
  return Opcode == SPIRV::GET_ID || Opcode == SPIRV::GET_ID64 ||
         Opcode == SPIRV::GET_fID || Opcode == SPIRV::GET_fID64 ||
         Opcode == SPIRV::GET_pID32 || Opcode == SPIRV::GET_pID64 ||
         Opcode == SPIRV::GET_vID || Opcode == SPIRV::GET_vfID ||
         Opcode == SPIRV::GET_vpID32 || Opcode == SPIRV::GET_vpID64;
}

static bool mayBeInserted(unsigned Opcode) {
  switch (Opcode) {
  case TargetOpcode::G_SMAX:
  case TargetOpcode::G_UMAX:
  case TargetOpcode::G_SMIN:
  case TargetOpcode::G_UMIN:
  case TargetOpcode::G_FMINNUM:
  case TargetOpcode::G_FMINIMUM:
  case TargetOpcode::G_FMAXNUM:
  case TargetOpcode::G_FMAXIMUM:
    return true;
  default:
    return isTypeFoldingSupported(Opcode);
  }
}

static void processNewInstrs(MachineFunction &MF, SPIRVGlobalRegistry *GR,
                             MachineIRBuilder MIB) {
  MachineRegisterInfo &MRI = MF.getRegInfo();

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &I : MBB) {
      const unsigned Opcode = I.getOpcode();
      if (Opcode == TargetOpcode::G_UNMERGE_VALUES) {
        unsigned ArgI = I.getNumOperands() - 1;
        Register SrcReg = I.getOperand(ArgI).isReg()
                              ? I.getOperand(ArgI).getReg()
                              : Register(0);
        SPIRVType *DefType =
            SrcReg.isValid() ? GR->getSPIRVTypeForVReg(SrcReg) : nullptr;
        if (!DefType || DefType->getOpcode() != SPIRV::OpTypeVector)
          report_fatal_error(
              "cannot select G_UNMERGE_VALUES with a non-vector argument");
        SPIRVType *ScalarType =
            GR->getSPIRVTypeForVReg(DefType->getOperand(1).getReg());
        for (unsigned i = 0; i < I.getNumDefs(); ++i) {
          Register ResVReg = I.getOperand(i).getReg();
          SPIRVType *ResType = GR->getSPIRVTypeForVReg(ResVReg);
          if (!ResType) {
            // There was no "assign type" actions, let's fix this now
            ResType = ScalarType;
            MRI.setRegClass(ResVReg, &SPIRV::IDRegClass);
            MRI.setType(ResVReg,
                        LLT::scalar(GR->getScalarOrVectorBitWidth(ResType)));
            GR->assignSPIRVTypeToVReg(ResType, ResVReg, *GR->CurMF);
          }
        }
      } else if (mayBeInserted(Opcode) && I.getNumDefs() == 1 &&
                 I.getNumOperands() > 1 && I.getOperand(1).isReg()) {
        // Legalizer may have added a new instructions and introduced new
        // registers, we must decorate them as if they were introduced in a
        // non-automatic way
        Register ResVReg = I.getOperand(0).getReg();
        SPIRVType *ResVType = GR->getSPIRVTypeForVReg(ResVReg);
        // Check if the register defined by the instruction is newly generated
        // or already processed
        if (!ResVType) {
          // Set type of the defined register
          ResVType = GR->getSPIRVTypeForVReg(I.getOperand(1).getReg());
          // Check if we have type defined for operands of the new instruction
          if (!ResVType)
            continue;
          // Set type & class
          MRI.setRegClass(ResVReg, &SPIRV::IDRegClass);
          MRI.setType(ResVReg,
                      LLT::scalar(GR->getScalarOrVectorBitWidth(ResVType)));
          GR->assignSPIRVTypeToVReg(ResVType, ResVReg, *GR->CurMF);
        }
        // If this is a simple operation that is to be reduced by TableGen
        // definition we must apply some of pre-legalizer rules here
        if (isTypeFoldingSupported(Opcode)) {
          // Check if the instruction newly generated or already processed
          MachineInstr *NextMI = I.getNextNode();
          if (NextMI && isMetaInstrGET(NextMI->getOpcode()))
            continue;
          // Restore usual instructions pattern for the newly inserted
          // instruction
          MRI.setRegClass(ResVReg, MRI.getType(ResVReg).isVector()
                                       ? &SPIRV::IDRegClass
                                       : &SPIRV::ANYIDRegClass);
          MRI.setType(ResVReg, LLT::scalar(32));
          insertAssignInstr(ResVReg, nullptr, ResVType, GR, MIB, MRI);
          processInstr(I, MIB, MRI, GR);
        }
      }
    }
  }
}

bool SPIRVPostLegalizer::runOnMachineFunction(MachineFunction &MF) {
  // Initialize the type registry.
  const SPIRVSubtarget &ST = MF.getSubtarget<SPIRVSubtarget>();
  SPIRVGlobalRegistry *GR = ST.getSPIRVGlobalRegistry();
  GR->setCurrentFunc(MF);
  MachineIRBuilder MIB(MF);

  processNewInstrs(MF, GR, MIB);

  return true;
}

INITIALIZE_PASS(SPIRVPostLegalizer, DEBUG_TYPE, "SPIRV post legalizer", false,
                false)

char SPIRVPostLegalizer::ID = 0;

FunctionPass *llvm::createSPIRVPostLegalizerPass() {
  return new SPIRVPostLegalizer();
}

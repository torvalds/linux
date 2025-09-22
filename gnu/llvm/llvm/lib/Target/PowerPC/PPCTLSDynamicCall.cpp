//===---------- PPCTLSDynamicCall.cpp - TLS Dynamic Call Fixup ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass expands ADDItls{ld,gd}LADDR[32] machine instructions into
// separate ADDItls[gd]L[32] and GETtlsADDR[32] instructions, both of
// which define GPR3.  A copy is added from GPR3 to the target virtual
// register of the original instruction.  The GETtlsADDR[32] is really
// a call instruction, so its target register is constrained to be GPR3.
// This is not true of ADDItls[gd]L[32], but there is a legacy linker
// optimization bug that requires the target register of the addi of
// a local- or general-dynamic TLS access sequence to be GPR3.
//
// This is done in a late pass so that TLS variable accesses can be
// fully commoned by MachineCSE.
//
//===----------------------------------------------------------------------===//

#include "PPC.h"
#include "PPCInstrBuilder.h"
#include "PPCInstrInfo.h"
#include "PPCTargetMachine.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "ppc-tls-dynamic-call"

namespace {
  struct PPCTLSDynamicCall : public MachineFunctionPass {
    static char ID;
    PPCTLSDynamicCall() : MachineFunctionPass(ID) {
      initializePPCTLSDynamicCallPass(*PassRegistry::getPassRegistry());
    }

    const PPCInstrInfo *TII;

protected:
    bool processBlock(MachineBasicBlock &MBB) {
      bool Changed = false;
      bool NeedFence = true;
      const PPCSubtarget &Subtarget =
          MBB.getParent()->getSubtarget<PPCSubtarget>();
      bool Is64Bit = Subtarget.isPPC64();
      bool IsAIX = Subtarget.isAIXABI();
      bool IsLargeModel =
          Subtarget.getTargetMachine().getCodeModel() == CodeModel::Large;
      bool IsPCREL = false;
      MachineFunction *MF = MBB.getParent();
      MachineRegisterInfo &RegInfo = MF->getRegInfo();

      for (MachineBasicBlock::iterator I = MBB.begin(), IE = MBB.end();
           I != IE;) {
        MachineInstr &MI = *I;
        IsPCREL = isPCREL(MI);
        // There are a number of slight differences in code generation
        // when we call .__get_tpointer (32-bit AIX TLS).
        bool IsTLSTPRelMI = MI.getOpcode() == PPC::GETtlsTpointer32AIX;
        bool IsTLSLDAIXMI = (MI.getOpcode() == PPC::TLSLDAIX8 ||
                             MI.getOpcode() == PPC::TLSLDAIX);

        if (MI.getOpcode() != PPC::ADDItlsgdLADDR &&
            MI.getOpcode() != PPC::ADDItlsldLADDR &&
            MI.getOpcode() != PPC::ADDItlsgdLADDR32 &&
            MI.getOpcode() != PPC::ADDItlsldLADDR32 &&
            MI.getOpcode() != PPC::TLSGDAIX &&
            MI.getOpcode() != PPC::TLSGDAIX8 && !IsTLSTPRelMI && !IsPCREL &&
            !IsTLSLDAIXMI) {
          // Although we create ADJCALLSTACKDOWN and ADJCALLSTACKUP
          // as scheduling fences, we skip creating fences if we already
          // have existing ADJCALLSTACKDOWN/UP to avoid nesting,
          // which causes verification error with -verify-machineinstrs.
          if (MI.getOpcode() == PPC::ADJCALLSTACKDOWN)
            NeedFence = false;
          else if (MI.getOpcode() == PPC::ADJCALLSTACKUP)
            NeedFence = true;

          ++I;
          continue;
        }

        LLVM_DEBUG(dbgs() << "TLS Dynamic Call Fixup:\n    " << MI);

        Register OutReg = MI.getOperand(0).getReg();
        Register InReg = PPC::NoRegister;
        Register GPR3 = Is64Bit ? PPC::X3 : PPC::R3;
        Register GPR4 = Is64Bit ? PPC::X4 : PPC::R4;
        if (!IsPCREL && !IsTLSTPRelMI)
          InReg = MI.getOperand(1).getReg();
        DebugLoc DL = MI.getDebugLoc();

        unsigned Opc1, Opc2;
        switch (MI.getOpcode()) {
        default:
          llvm_unreachable("Opcode inconsistency error");
        case PPC::ADDItlsgdLADDR:
          Opc1 = PPC::ADDItlsgdL;
          Opc2 = PPC::GETtlsADDR;
          break;
        case PPC::ADDItlsldLADDR:
          Opc1 = PPC::ADDItlsldL;
          Opc2 = PPC::GETtlsldADDR;
          break;
        case PPC::ADDItlsgdLADDR32:
          Opc1 = PPC::ADDItlsgdL32;
          Opc2 = PPC::GETtlsADDR32;
          break;
        case PPC::ADDItlsldLADDR32:
          Opc1 = PPC::ADDItlsldL32;
          Opc2 = PPC::GETtlsldADDR32;
          break;
        case PPC::TLSLDAIX:
          // TLSLDAIX is expanded to one copy and GET_TLS_MOD, so we only set
          // Opc2 here.
          Opc2 = PPC::GETtlsMOD32AIX;
          break;
        case PPC::TLSLDAIX8:
          // TLSLDAIX8 is expanded to one copy and GET_TLS_MOD, so we only set
          // Opc2 here.
          Opc2 = PPC::GETtlsMOD64AIX;
          break;
        case PPC::TLSGDAIX8:
          // TLSGDAIX8 is expanded to two copies and GET_TLS_ADDR, so we only
          // set Opc2 here.
          Opc2 = PPC::GETtlsADDR64AIX;
          break;
        case PPC::TLSGDAIX:
          // TLSGDAIX is expanded to two copies and GET_TLS_ADDR, so we only
          // set Opc2 here.
          Opc2 = PPC::GETtlsADDR32AIX;
          break;
        case PPC::GETtlsTpointer32AIX:
          // GETtlsTpointer32AIX is expanded to a call to GET_TPOINTER on AIX
          // 32-bit mode within PPCAsmPrinter. This instruction does not need
          // to change, so Opc2 is set to the same instruction opcode.
          Opc2 = PPC::GETtlsTpointer32AIX;
          break;
        case PPC::PADDI8pc:
          assert(IsPCREL && "Expecting General/Local Dynamic PCRel");
          Opc1 = PPC::PADDI8pc;
          Opc2 = MI.getOperand(2).getTargetFlags() ==
                         PPCII::MO_GOT_TLSGD_PCREL_FLAG
                     ? PPC::GETtlsADDRPCREL
                     : PPC::GETtlsldADDRPCREL;
        }

        // We create ADJCALLSTACKUP and ADJCALLSTACKDOWN around _tls_get_addr
        // as scheduling fence to avoid it is scheduled before
        // mflr in the prologue and the address in LR is clobbered (PR25839).
        // We don't really need to save data to the stack - the clobbered
        // registers are already saved when the SDNode (e.g. PPCaddiTlsgdLAddr)
        // gets translated to the pseudo instruction (e.g. ADDItlsgdLADDR).
        if (NeedFence) {
          MBB.getParent()->getFrameInfo().setAdjustsStack(true);
          BuildMI(MBB, I, DL, TII->get(PPC::ADJCALLSTACKDOWN)).addImm(0)
                                                              .addImm(0);
        }

        if (IsAIX) {
          if (IsTLSLDAIXMI) {
            // The relative order between the node that loads the variable
            // offset from the TOC, and the .__tls_get_mod node is being tuned
            // here. It is better to put the variable offset TOC load after the
            // call, since this node can use clobbers r4/r5.
            // Search for the pattern of the two nodes that load from the TOC
            // (either for the variable offset or for the module handle), and
            // then move the variable offset TOC load right before the node that
            // uses the OutReg of the .__tls_get_mod node.
            unsigned LDTocOp =
                Is64Bit ? (IsLargeModel ? PPC::LDtocL : PPC::LDtoc)
                        : (IsLargeModel ? PPC::LWZtocL : PPC::LWZtoc);
            if (!RegInfo.use_empty(OutReg)) {
              std::set<MachineInstr *> Uses;
              // Collect all instructions that use the OutReg.
              for (MachineOperand &MO : RegInfo.use_operands(OutReg))
                Uses.insert(MO.getParent());
              // Find the first user (e.g.: lwax/stfdx) of the OutReg within the
              // current BB.
              MachineBasicBlock::iterator UseIter = MBB.begin();
              for (MachineBasicBlock::iterator IE = MBB.end(); UseIter != IE;
                   ++UseIter)
                if (Uses.count(&*UseIter))
                  break;

              // Additional handling is required when UserIter (the first user
              // of OutReg) is pointing to a valid node that loads from the TOC.
              // Check the pattern and do the movement if the pattern matches.
              if (UseIter != MBB.end()) {
                // Collect all associated nodes that load from the TOC. Use
                // hasOneDef() to guard against unexpected scenarios.
                std::set<MachineInstr *> LoadFromTocs;
                for (MachineOperand &MO : UseIter->operands())
                  if (MO.isReg() && MO.isUse()) {
                    Register MOReg = MO.getReg();
                    if (RegInfo.hasOneDef(MOReg)) {
                      MachineInstr *Temp =
                          RegInfo.getOneDef(MOReg)->getParent();
                      // For the current TLSLDAIX node, get the corresponding
                      // node that loads from the TOC for the InReg. Otherwise,
                      // Temp probably pointed to the variable offset TOC load
                      // we would like to move.
                      if (Temp == &MI && RegInfo.hasOneDef(InReg))
                        Temp = RegInfo.getOneDef(InReg)->getParent();
                      if (Temp->getOpcode() == LDTocOp)
                        LoadFromTocs.insert(Temp);
                    } else {
                      // FIXME: analyze this scenario if there is one.
                      LoadFromTocs.clear();
                      break;
                    }
                  }

                // Check the two nodes that loaded from the TOC: one should be
                // "_$TLSML", and the other will be moved before the node that
                // uses the OutReg of the .__tls_get_mod node.
                if (LoadFromTocs.size() == 2) {
                  MachineBasicBlock::iterator TLSMLIter = MBB.end();
                  MachineBasicBlock::iterator OffsetIter = MBB.end();
                  // Make sure the two nodes that loaded from the TOC are within
                  // the current BB, and that one of them is from the "_$TLSML"
                  // pseudo symbol, while the other is from the variable.
                  for (MachineBasicBlock::iterator I = MBB.begin(),
                                                   IE = MBB.end();
                       I != IE; ++I)
                    if (LoadFromTocs.count(&*I)) {
                      MachineOperand MO = I->getOperand(1);
                      if (MO.isGlobal() && MO.getGlobal()->hasName() &&
                          MO.getGlobal()->getName() == "_$TLSML")
                        TLSMLIter = I;
                      else
                        OffsetIter = I;
                    }
                  // Perform the movement when the desired scenario has been
                  // identified, which should be when both of the iterators are
                  // valid.
                  if (TLSMLIter != MBB.end() && OffsetIter != MBB.end())
                    OffsetIter->moveBefore(&*UseIter);
                }
              }
            }
            // The module-handle is copied into r3. The copy is followed by
            // GETtlsMOD32AIX/GETtlsMOD64AIX.
            BuildMI(MBB, I, DL, TII->get(TargetOpcode::COPY), GPR3)
                .addReg(InReg);
            // The call to .__tls_get_mod.
            BuildMI(MBB, I, DL, TII->get(Opc2), GPR3).addReg(GPR3);
          } else if (!IsTLSTPRelMI) {
            // The variable offset and region handle (for TLSGD) are copied in
            // r4 and r3. The copies are followed by
            // GETtlsADDR32AIX/GETtlsADDR64AIX.
            BuildMI(MBB, I, DL, TII->get(TargetOpcode::COPY), GPR4)
                .addReg(MI.getOperand(1).getReg());
            BuildMI(MBB, I, DL, TII->get(TargetOpcode::COPY), GPR3)
                .addReg(MI.getOperand(2).getReg());
            BuildMI(MBB, I, DL, TII->get(Opc2), GPR3).addReg(GPR3).addReg(GPR4);
          } else
            // The opcode of GETtlsTpointer32AIX does not change, because later
            // this instruction will be expanded into a call to .__get_tpointer,
            // which will return the thread pointer into r3.
            BuildMI(MBB, I, DL, TII->get(Opc2), GPR3);
        } else {
          MachineInstr *Addi;
          if (IsPCREL) {
            Addi = BuildMI(MBB, I, DL, TII->get(Opc1), GPR3).addImm(0);
          } else {
            // Expand into two ops built prior to the existing instruction.
            assert(InReg != PPC::NoRegister && "Operand must be a register");
            Addi = BuildMI(MBB, I, DL, TII->get(Opc1), GPR3).addReg(InReg);
          }

          Addi->addOperand(MI.getOperand(2));

          MachineInstr *Call =
              (BuildMI(MBB, I, DL, TII->get(Opc2), GPR3).addReg(GPR3));
          if (IsPCREL)
            Call->addOperand(MI.getOperand(2));
          else
            Call->addOperand(MI.getOperand(3));
        }
        if (NeedFence)
          BuildMI(MBB, I, DL, TII->get(PPC::ADJCALLSTACKUP)).addImm(0).addImm(0);

        BuildMI(MBB, I, DL, TII->get(TargetOpcode::COPY), OutReg)
          .addReg(GPR3);

        // Move past the original instruction and remove it.
        ++I;
        MI.removeFromParent();

        Changed = true;
      }

      return Changed;
    }

public:
  bool isPCREL(const MachineInstr &MI) {
    return (MI.getOpcode() == PPC::PADDI8pc) &&
           (MI.getOperand(2).getTargetFlags() ==
                PPCII::MO_GOT_TLSGD_PCREL_FLAG ||
            MI.getOperand(2).getTargetFlags() ==
                PPCII::MO_GOT_TLSLD_PCREL_FLAG);
  }

    bool runOnMachineFunction(MachineFunction &MF) override {
      TII = MF.getSubtarget<PPCSubtarget>().getInstrInfo();

      bool Changed = false;

      for (MachineBasicBlock &B : llvm::make_early_inc_range(MF))
        if (processBlock(B))
          Changed = true;

      return Changed;
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<LiveIntervalsWrapperPass>();
      AU.addRequired<SlotIndexesWrapperPass>();
      MachineFunctionPass::getAnalysisUsage(AU);
    }
  };
}

INITIALIZE_PASS_BEGIN(PPCTLSDynamicCall, DEBUG_TYPE,
                      "PowerPC TLS Dynamic Call Fixup", false, false)
INITIALIZE_PASS_DEPENDENCY(LiveIntervalsWrapperPass)
INITIALIZE_PASS_DEPENDENCY(SlotIndexesWrapperPass)
INITIALIZE_PASS_END(PPCTLSDynamicCall, DEBUG_TYPE,
                    "PowerPC TLS Dynamic Call Fixup", false, false)

char PPCTLSDynamicCall::ID = 0;
FunctionPass*
llvm::createPPCTLSDynamicCallPass() { return new PPCTLSDynamicCall(); }

//===-- ARMExpandPseudoInsts.cpp - Expand pseudo instructions -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a pass that expands pseudo instructions into target
// instructions to allow proper scheduling, if-conversion, and other late
// optimizations. This pass should be run after register allocation but before
// the post-regalloc scheduling pass.
//
//===----------------------------------------------------------------------===//

#include "ARM.h"
#include "ARMBaseInstrInfo.h"
#include "ARMBaseRegisterInfo.h"
#include "ARMConstantPoolValue.h"
#include "ARMMachineFunctionInfo.h"
#include "ARMSubtarget.h"
#include "MCTargetDesc/ARMAddressingModes.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "arm-pseudo"

static cl::opt<bool>
VerifyARMPseudo("verify-arm-pseudo-expand", cl::Hidden,
                cl::desc("Verify machine code after expanding ARM pseudos"));

#define ARM_EXPAND_PSEUDO_NAME "ARM pseudo instruction expansion pass"

namespace {
  class ARMExpandPseudo : public MachineFunctionPass {
  public:
    static char ID;
    ARMExpandPseudo() : MachineFunctionPass(ID) {}

    const ARMBaseInstrInfo *TII;
    const TargetRegisterInfo *TRI;
    const ARMSubtarget *STI;
    ARMFunctionInfo *AFI;

    bool runOnMachineFunction(MachineFunction &Fn) override;

    MachineFunctionProperties getRequiredProperties() const override {
      return MachineFunctionProperties().set(
          MachineFunctionProperties::Property::NoVRegs);
    }

    StringRef getPassName() const override {
      return ARM_EXPAND_PSEUDO_NAME;
    }

  private:
    bool ExpandMI(MachineBasicBlock &MBB,
                  MachineBasicBlock::iterator MBBI,
                  MachineBasicBlock::iterator &NextMBBI);
    bool ExpandMBB(MachineBasicBlock &MBB);
    void ExpandVLD(MachineBasicBlock::iterator &MBBI);
    void ExpandVST(MachineBasicBlock::iterator &MBBI);
    void ExpandLaneOp(MachineBasicBlock::iterator &MBBI);
    void ExpandVTBL(MachineBasicBlock::iterator &MBBI,
                    unsigned Opc, bool IsExt);
    void ExpandMQQPRLoadStore(MachineBasicBlock::iterator &MBBI);
    void ExpandTMOV32BitImm(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator &MBBI);
    void ExpandMOV32BitImm(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator &MBBI);
    void CMSEClearGPRegs(MachineBasicBlock &MBB,
                         MachineBasicBlock::iterator MBBI, const DebugLoc &DL,
                         const SmallVectorImpl<unsigned> &ClearRegs,
                         unsigned ClobberReg);
    MachineBasicBlock &CMSEClearFPRegs(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator MBBI);
    MachineBasicBlock &CMSEClearFPRegsV8(MachineBasicBlock &MBB,
                                         MachineBasicBlock::iterator MBBI,
                                         const BitVector &ClearRegs);
    MachineBasicBlock &CMSEClearFPRegsV81(MachineBasicBlock &MBB,
                                          MachineBasicBlock::iterator MBBI,
                                          const BitVector &ClearRegs);
    void CMSESaveClearFPRegs(MachineBasicBlock &MBB,
                             MachineBasicBlock::iterator MBBI, DebugLoc &DL,
                             const LivePhysRegs &LiveRegs,
                             SmallVectorImpl<unsigned> &AvailableRegs);
    void CMSESaveClearFPRegsV8(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator MBBI, DebugLoc &DL,
                               const LivePhysRegs &LiveRegs,
                               SmallVectorImpl<unsigned> &ScratchRegs);
    void CMSESaveClearFPRegsV81(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MBBI, DebugLoc &DL,
                                const LivePhysRegs &LiveRegs);
    void CMSERestoreFPRegs(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MBBI, DebugLoc &DL,
                           SmallVectorImpl<unsigned> &AvailableRegs);
    void CMSERestoreFPRegsV8(MachineBasicBlock &MBB,
                             MachineBasicBlock::iterator MBBI, DebugLoc &DL,
                             SmallVectorImpl<unsigned> &AvailableRegs);
    void CMSERestoreFPRegsV81(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MBBI, DebugLoc &DL,
                              SmallVectorImpl<unsigned> &AvailableRegs);
    bool ExpandCMP_SWAP(MachineBasicBlock &MBB,
                        MachineBasicBlock::iterator MBBI, unsigned LdrexOp,
                        unsigned StrexOp, unsigned UxtOp,
                        MachineBasicBlock::iterator &NextMBBI);

    bool ExpandCMP_SWAP_64(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MBBI,
                           MachineBasicBlock::iterator &NextMBBI);
  };
  char ARMExpandPseudo::ID = 0;
}

INITIALIZE_PASS(ARMExpandPseudo, DEBUG_TYPE, ARM_EXPAND_PSEUDO_NAME, false,
                false)

namespace {
  // Constants for register spacing in NEON load/store instructions.
  // For quad-register load-lane and store-lane pseudo instructors, the
  // spacing is initially assumed to be EvenDblSpc, and that is changed to
  // OddDblSpc depending on the lane number operand.
  enum NEONRegSpacing {
    SingleSpc,
    SingleLowSpc ,  // Single spacing, low registers, three and four vectors.
    SingleHighQSpc, // Single spacing, high registers, four vectors.
    SingleHighTSpc, // Single spacing, high registers, three vectors.
    EvenDblSpc,
    OddDblSpc
  };

  // Entries for NEON load/store information table.  The table is sorted by
  // PseudoOpc for fast binary-search lookups.
  struct NEONLdStTableEntry {
    uint16_t PseudoOpc;
    uint16_t RealOpc;
    bool IsLoad;
    bool isUpdating;
    bool hasWritebackOperand;
    uint8_t RegSpacing; // One of type NEONRegSpacing
    uint8_t NumRegs; // D registers loaded or stored
    uint8_t RegElts; // elements per D register; used for lane ops
    // FIXME: Temporary flag to denote whether the real instruction takes
    // a single register (like the encoding) or all of the registers in
    // the list (like the asm syntax and the isel DAG). When all definitions
    // are converted to take only the single encoded register, this will
    // go away.
    bool copyAllListRegs;

    // Comparison methods for binary search of the table.
    bool operator<(const NEONLdStTableEntry &TE) const {
      return PseudoOpc < TE.PseudoOpc;
    }
    friend bool operator<(const NEONLdStTableEntry &TE, unsigned PseudoOpc) {
      return TE.PseudoOpc < PseudoOpc;
    }
    friend bool LLVM_ATTRIBUTE_UNUSED operator<(unsigned PseudoOpc,
                                                const NEONLdStTableEntry &TE) {
      return PseudoOpc < TE.PseudoOpc;
    }
  };
}

static const NEONLdStTableEntry NEONLdStTable[] = {
{ ARM::VLD1LNq16Pseudo,     ARM::VLD1LNd16,     true, false, false, EvenDblSpc, 1, 4 ,true},
{ ARM::VLD1LNq16Pseudo_UPD, ARM::VLD1LNd16_UPD, true, true, true,  EvenDblSpc, 1, 4 ,true},
{ ARM::VLD1LNq32Pseudo,     ARM::VLD1LNd32,     true, false, false, EvenDblSpc, 1, 2 ,true},
{ ARM::VLD1LNq32Pseudo_UPD, ARM::VLD1LNd32_UPD, true, true, true,  EvenDblSpc, 1, 2 ,true},
{ ARM::VLD1LNq8Pseudo,      ARM::VLD1LNd8,      true, false, false, EvenDblSpc, 1, 8 ,true},
{ ARM::VLD1LNq8Pseudo_UPD,  ARM::VLD1LNd8_UPD, true, true, true,  EvenDblSpc, 1, 8 ,true},

{ ARM::VLD1d16QPseudo,      ARM::VLD1d16Q,     true,  false, false, SingleSpc,  4, 4 ,false},
{ ARM::VLD1d16QPseudoWB_fixed,  ARM::VLD1d16Qwb_fixed,   true, true, false, SingleSpc,  4, 4 ,false},
{ ARM::VLD1d16QPseudoWB_register,  ARM::VLD1d16Qwb_register, true, true, true, SingleSpc,  4, 4 ,false},
{ ARM::VLD1d16TPseudo,      ARM::VLD1d16T,     true,  false, false, SingleSpc,  3, 4 ,false},
{ ARM::VLD1d16TPseudoWB_fixed,  ARM::VLD1d16Twb_fixed,   true, true, false, SingleSpc,  3, 4 ,false},
{ ARM::VLD1d16TPseudoWB_register,  ARM::VLD1d16Twb_register, true, true, true, SingleSpc,  3, 4 ,false},

{ ARM::VLD1d32QPseudo,      ARM::VLD1d32Q,     true,  false, false, SingleSpc,  4, 2 ,false},
{ ARM::VLD1d32QPseudoWB_fixed,  ARM::VLD1d32Qwb_fixed,   true, true, false, SingleSpc,  4, 2 ,false},
{ ARM::VLD1d32QPseudoWB_register,  ARM::VLD1d32Qwb_register, true, true, true, SingleSpc,  4, 2 ,false},
{ ARM::VLD1d32TPseudo,      ARM::VLD1d32T,     true,  false, false, SingleSpc,  3, 2 ,false},
{ ARM::VLD1d32TPseudoWB_fixed,  ARM::VLD1d32Twb_fixed,   true, true, false, SingleSpc,  3, 2 ,false},
{ ARM::VLD1d32TPseudoWB_register,  ARM::VLD1d32Twb_register, true, true, true, SingleSpc,  3, 2 ,false},

{ ARM::VLD1d64QPseudo,      ARM::VLD1d64Q,     true,  false, false, SingleSpc,  4, 1 ,false},
{ ARM::VLD1d64QPseudoWB_fixed,  ARM::VLD1d64Qwb_fixed,   true,  true, false, SingleSpc,  4, 1 ,false},
{ ARM::VLD1d64QPseudoWB_register,  ARM::VLD1d64Qwb_register,   true,  true, true, SingleSpc,  4, 1 ,false},
{ ARM::VLD1d64TPseudo,      ARM::VLD1d64T,     true,  false, false, SingleSpc,  3, 1 ,false},
{ ARM::VLD1d64TPseudoWB_fixed,  ARM::VLD1d64Twb_fixed,   true,  true, false, SingleSpc,  3, 1 ,false},
{ ARM::VLD1d64TPseudoWB_register,  ARM::VLD1d64Twb_register, true, true, true,  SingleSpc,  3, 1 ,false},

{ ARM::VLD1d8QPseudo,       ARM::VLD1d8Q,      true,  false, false, SingleSpc,  4, 8 ,false},
{ ARM::VLD1d8QPseudoWB_fixed,   ARM::VLD1d8Qwb_fixed,    true,  true, false, SingleSpc,  4, 8 ,false},
{ ARM::VLD1d8QPseudoWB_register,   ARM::VLD1d8Qwb_register,  true, true, true, SingleSpc,  4, 8 ,false},
{ ARM::VLD1d8TPseudo,       ARM::VLD1d8T,      true,  false, false, SingleSpc,  3, 8 ,false},
{ ARM::VLD1d8TPseudoWB_fixed,   ARM::VLD1d8Twb_fixed,    true,  true, false, SingleSpc,  3, 8 ,false},
{ ARM::VLD1d8TPseudoWB_register,   ARM::VLD1d8Twb_register,  true,  true, true, SingleSpc,  3, 8 ,false},

{ ARM::VLD1q16HighQPseudo,  ARM::VLD1d16Q,     true,  false, false, SingleHighQSpc,  4, 4 ,false},
{ ARM::VLD1q16HighQPseudo_UPD, ARM::VLD1d16Qwb_fixed,   true,  true, true, SingleHighQSpc,  4, 4 ,false},
{ ARM::VLD1q16HighTPseudo,  ARM::VLD1d16T,     true,  false, false, SingleHighTSpc,  3, 4 ,false},
{ ARM::VLD1q16HighTPseudo_UPD, ARM::VLD1d16Twb_fixed,   true,  true, true, SingleHighTSpc,  3, 4 ,false},
{ ARM::VLD1q16LowQPseudo_UPD,  ARM::VLD1d16Qwb_fixed,   true,  true, true, SingleLowSpc,  4, 4 ,false},
{ ARM::VLD1q16LowTPseudo_UPD,  ARM::VLD1d16Twb_fixed,   true,  true, true, SingleLowSpc,  3, 4 ,false},

{ ARM::VLD1q32HighQPseudo,  ARM::VLD1d32Q,     true,  false, false, SingleHighQSpc,  4, 2 ,false},
{ ARM::VLD1q32HighQPseudo_UPD, ARM::VLD1d32Qwb_fixed,   true,  true, true, SingleHighQSpc,  4, 2 ,false},
{ ARM::VLD1q32HighTPseudo,  ARM::VLD1d32T,     true,  false, false, SingleHighTSpc,  3, 2 ,false},
{ ARM::VLD1q32HighTPseudo_UPD, ARM::VLD1d32Twb_fixed,   true,  true, true, SingleHighTSpc,  3, 2 ,false},
{ ARM::VLD1q32LowQPseudo_UPD,  ARM::VLD1d32Qwb_fixed,   true,  true, true, SingleLowSpc,  4, 2 ,false},
{ ARM::VLD1q32LowTPseudo_UPD,  ARM::VLD1d32Twb_fixed,   true,  true, true, SingleLowSpc,  3, 2 ,false},

{ ARM::VLD1q64HighQPseudo,  ARM::VLD1d64Q,     true,  false, false, SingleHighQSpc,  4, 1 ,false},
{ ARM::VLD1q64HighQPseudo_UPD, ARM::VLD1d64Qwb_fixed,   true,  true, true, SingleHighQSpc,  4, 1 ,false},
{ ARM::VLD1q64HighTPseudo,  ARM::VLD1d64T,     true,  false, false, SingleHighTSpc,  3, 1 ,false},
{ ARM::VLD1q64HighTPseudo_UPD, ARM::VLD1d64Twb_fixed,   true,  true, true, SingleHighTSpc,  3, 1 ,false},
{ ARM::VLD1q64LowQPseudo_UPD,  ARM::VLD1d64Qwb_fixed,   true,  true, true, SingleLowSpc,  4, 1 ,false},
{ ARM::VLD1q64LowTPseudo_UPD,  ARM::VLD1d64Twb_fixed,   true,  true, true, SingleLowSpc,  3, 1 ,false},

{ ARM::VLD1q8HighQPseudo,   ARM::VLD1d8Q,     true,  false, false, SingleHighQSpc,  4, 8 ,false},
{ ARM::VLD1q8HighQPseudo_UPD, ARM::VLD1d8Qwb_fixed,   true,  true, true, SingleHighQSpc,  4, 8 ,false},
{ ARM::VLD1q8HighTPseudo,   ARM::VLD1d8T,     true,  false, false, SingleHighTSpc,  3, 8 ,false},
{ ARM::VLD1q8HighTPseudo_UPD, ARM::VLD1d8Twb_fixed,   true,  true, true, SingleHighTSpc,  3, 8 ,false},
{ ARM::VLD1q8LowQPseudo_UPD,  ARM::VLD1d8Qwb_fixed,   true,  true, true, SingleLowSpc,  4, 8 ,false},
{ ARM::VLD1q8LowTPseudo_UPD,  ARM::VLD1d8Twb_fixed,   true,  true, true, SingleLowSpc,  3, 8 ,false},

{ ARM::VLD2DUPq16EvenPseudo,  ARM::VLD2DUPd16x2,  true, false, false, EvenDblSpc, 2, 4 ,false},
{ ARM::VLD2DUPq16OddPseudo,   ARM::VLD2DUPd16x2,  true, false, false, OddDblSpc,  2, 4 ,false},
{ ARM::VLD2DUPq16OddPseudoWB_fixed,   ARM::VLD2DUPd16x2wb_fixed, true, true, false, OddDblSpc,  2, 4 ,false},
{ ARM::VLD2DUPq16OddPseudoWB_register,   ARM::VLD2DUPd16x2wb_register, true, true, true, OddDblSpc,  2, 4 ,false},
{ ARM::VLD2DUPq32EvenPseudo,  ARM::VLD2DUPd32x2,  true, false, false, EvenDblSpc, 2, 2 ,false},
{ ARM::VLD2DUPq32OddPseudo,   ARM::VLD2DUPd32x2,  true, false, false, OddDblSpc,  2, 2 ,false},
{ ARM::VLD2DUPq32OddPseudoWB_fixed,   ARM::VLD2DUPd32x2wb_fixed, true, true, false, OddDblSpc,  2, 2 ,false},
{ ARM::VLD2DUPq32OddPseudoWB_register,   ARM::VLD2DUPd32x2wb_register, true, true, true, OddDblSpc,  2, 2 ,false},
{ ARM::VLD2DUPq8EvenPseudo,   ARM::VLD2DUPd8x2,   true, false, false, EvenDblSpc, 2, 8 ,false},
{ ARM::VLD2DUPq8OddPseudo,    ARM::VLD2DUPd8x2,   true, false, false, OddDblSpc,  2, 8 ,false},
{ ARM::VLD2DUPq8OddPseudoWB_fixed,    ARM::VLD2DUPd8x2wb_fixed, true, true, false, OddDblSpc,  2, 8 ,false},
{ ARM::VLD2DUPq8OddPseudoWB_register,    ARM::VLD2DUPd8x2wb_register, true, true, true, OddDblSpc,  2, 8 ,false},

{ ARM::VLD2LNd16Pseudo,     ARM::VLD2LNd16,     true, false, false, SingleSpc,  2, 4 ,true},
{ ARM::VLD2LNd16Pseudo_UPD, ARM::VLD2LNd16_UPD, true, true, true,  SingleSpc,  2, 4 ,true},
{ ARM::VLD2LNd32Pseudo,     ARM::VLD2LNd32,     true, false, false, SingleSpc,  2, 2 ,true},
{ ARM::VLD2LNd32Pseudo_UPD, ARM::VLD2LNd32_UPD, true, true, true,  SingleSpc,  2, 2 ,true},
{ ARM::VLD2LNd8Pseudo,      ARM::VLD2LNd8,      true, false, false, SingleSpc,  2, 8 ,true},
{ ARM::VLD2LNd8Pseudo_UPD,  ARM::VLD2LNd8_UPD, true, true, true,  SingleSpc,  2, 8 ,true},
{ ARM::VLD2LNq16Pseudo,     ARM::VLD2LNq16,     true, false, false, EvenDblSpc, 2, 4 ,true},
{ ARM::VLD2LNq16Pseudo_UPD, ARM::VLD2LNq16_UPD, true, true, true,  EvenDblSpc, 2, 4 ,true},
{ ARM::VLD2LNq32Pseudo,     ARM::VLD2LNq32,     true, false, false, EvenDblSpc, 2, 2 ,true},
{ ARM::VLD2LNq32Pseudo_UPD, ARM::VLD2LNq32_UPD, true, true, true,  EvenDblSpc, 2, 2 ,true},

{ ARM::VLD2q16Pseudo,       ARM::VLD2q16,      true,  false, false, SingleSpc,  4, 4 ,false},
{ ARM::VLD2q16PseudoWB_fixed,   ARM::VLD2q16wb_fixed, true, true, false,  SingleSpc,  4, 4 ,false},
{ ARM::VLD2q16PseudoWB_register,   ARM::VLD2q16wb_register, true, true, true,  SingleSpc,  4, 4 ,false},
{ ARM::VLD2q32Pseudo,       ARM::VLD2q32,      true,  false, false, SingleSpc,  4, 2 ,false},
{ ARM::VLD2q32PseudoWB_fixed,   ARM::VLD2q32wb_fixed, true, true, false,  SingleSpc,  4, 2 ,false},
{ ARM::VLD2q32PseudoWB_register,   ARM::VLD2q32wb_register, true, true, true,  SingleSpc,  4, 2 ,false},
{ ARM::VLD2q8Pseudo,        ARM::VLD2q8,       true,  false, false, SingleSpc,  4, 8 ,false},
{ ARM::VLD2q8PseudoWB_fixed,    ARM::VLD2q8wb_fixed, true, true, false,  SingleSpc,  4, 8 ,false},
{ ARM::VLD2q8PseudoWB_register,    ARM::VLD2q8wb_register, true, true, true,  SingleSpc,  4, 8 ,false},

{ ARM::VLD3DUPd16Pseudo,     ARM::VLD3DUPd16,     true, false, false, SingleSpc, 3, 4,true},
{ ARM::VLD3DUPd16Pseudo_UPD, ARM::VLD3DUPd16_UPD, true, true, true,  SingleSpc, 3, 4,true},
{ ARM::VLD3DUPd32Pseudo,     ARM::VLD3DUPd32,     true, false, false, SingleSpc, 3, 2,true},
{ ARM::VLD3DUPd32Pseudo_UPD, ARM::VLD3DUPd32_UPD, true, true, true,  SingleSpc, 3, 2,true},
{ ARM::VLD3DUPd8Pseudo,      ARM::VLD3DUPd8,      true, false, false, SingleSpc, 3, 8,true},
{ ARM::VLD3DUPd8Pseudo_UPD,  ARM::VLD3DUPd8_UPD, true, true, true,  SingleSpc, 3, 8,true},
{ ARM::VLD3DUPq16EvenPseudo, ARM::VLD3DUPq16,     true, false, false, EvenDblSpc, 3, 4 ,true},
{ ARM::VLD3DUPq16OddPseudo,  ARM::VLD3DUPq16,     true, false, false, OddDblSpc,  3, 4 ,true},
{ ARM::VLD3DUPq16OddPseudo_UPD,  ARM::VLD3DUPq16_UPD, true, true, true, OddDblSpc,  3, 4 ,true},
{ ARM::VLD3DUPq32EvenPseudo, ARM::VLD3DUPq32,     true, false, false, EvenDblSpc, 3, 2 ,true},
{ ARM::VLD3DUPq32OddPseudo,  ARM::VLD3DUPq32,     true, false, false, OddDblSpc,  3, 2 ,true},
{ ARM::VLD3DUPq32OddPseudo_UPD,  ARM::VLD3DUPq32_UPD, true, true, true, OddDblSpc,  3, 2 ,true},
{ ARM::VLD3DUPq8EvenPseudo,  ARM::VLD3DUPq8,      true, false, false, EvenDblSpc, 3, 8 ,true},
{ ARM::VLD3DUPq8OddPseudo,   ARM::VLD3DUPq8,      true, false, false, OddDblSpc,  3, 8 ,true},
{ ARM::VLD3DUPq8OddPseudo_UPD,   ARM::VLD3DUPq8_UPD, true, true, true, OddDblSpc,  3, 8 ,true},

{ ARM::VLD3LNd16Pseudo,     ARM::VLD3LNd16,     true, false, false, SingleSpc,  3, 4 ,true},
{ ARM::VLD3LNd16Pseudo_UPD, ARM::VLD3LNd16_UPD, true, true, true,  SingleSpc,  3, 4 ,true},
{ ARM::VLD3LNd32Pseudo,     ARM::VLD3LNd32,     true, false, false, SingleSpc,  3, 2 ,true},
{ ARM::VLD3LNd32Pseudo_UPD, ARM::VLD3LNd32_UPD, true, true, true,  SingleSpc,  3, 2 ,true},
{ ARM::VLD3LNd8Pseudo,      ARM::VLD3LNd8,      true, false, false, SingleSpc,  3, 8 ,true},
{ ARM::VLD3LNd8Pseudo_UPD,  ARM::VLD3LNd8_UPD, true, true, true,  SingleSpc,  3, 8 ,true},
{ ARM::VLD3LNq16Pseudo,     ARM::VLD3LNq16,     true, false, false, EvenDblSpc, 3, 4 ,true},
{ ARM::VLD3LNq16Pseudo_UPD, ARM::VLD3LNq16_UPD, true, true, true,  EvenDblSpc, 3, 4 ,true},
{ ARM::VLD3LNq32Pseudo,     ARM::VLD3LNq32,     true, false, false, EvenDblSpc, 3, 2 ,true},
{ ARM::VLD3LNq32Pseudo_UPD, ARM::VLD3LNq32_UPD, true, true, true,  EvenDblSpc, 3, 2 ,true},

{ ARM::VLD3d16Pseudo,       ARM::VLD3d16,      true,  false, false, SingleSpc,  3, 4 ,true},
{ ARM::VLD3d16Pseudo_UPD,   ARM::VLD3d16_UPD, true, true, true,  SingleSpc,  3, 4 ,true},
{ ARM::VLD3d32Pseudo,       ARM::VLD3d32,      true,  false, false, SingleSpc,  3, 2 ,true},
{ ARM::VLD3d32Pseudo_UPD,   ARM::VLD3d32_UPD, true, true, true,  SingleSpc,  3, 2 ,true},
{ ARM::VLD3d8Pseudo,        ARM::VLD3d8,       true,  false, false, SingleSpc,  3, 8 ,true},
{ ARM::VLD3d8Pseudo_UPD,    ARM::VLD3d8_UPD, true, true, true,  SingleSpc,  3, 8 ,true},

{ ARM::VLD3q16Pseudo_UPD,    ARM::VLD3q16_UPD, true, true, true,  EvenDblSpc, 3, 4 ,true},
{ ARM::VLD3q16oddPseudo,     ARM::VLD3q16,     true,  false, false, OddDblSpc,  3, 4 ,true},
{ ARM::VLD3q16oddPseudo_UPD, ARM::VLD3q16_UPD, true, true, true,  OddDblSpc,  3, 4 ,true},
{ ARM::VLD3q32Pseudo_UPD,    ARM::VLD3q32_UPD, true, true, true,  EvenDblSpc, 3, 2 ,true},
{ ARM::VLD3q32oddPseudo,     ARM::VLD3q32,     true,  false, false, OddDblSpc,  3, 2 ,true},
{ ARM::VLD3q32oddPseudo_UPD, ARM::VLD3q32_UPD, true, true, true,  OddDblSpc,  3, 2 ,true},
{ ARM::VLD3q8Pseudo_UPD,     ARM::VLD3q8_UPD, true, true, true,  EvenDblSpc, 3, 8 ,true},
{ ARM::VLD3q8oddPseudo,      ARM::VLD3q8,      true,  false, false, OddDblSpc,  3, 8 ,true},
{ ARM::VLD3q8oddPseudo_UPD,  ARM::VLD3q8_UPD, true, true, true,  OddDblSpc,  3, 8 ,true},

{ ARM::VLD4DUPd16Pseudo,     ARM::VLD4DUPd16,     true, false, false, SingleSpc, 4, 4,true},
{ ARM::VLD4DUPd16Pseudo_UPD, ARM::VLD4DUPd16_UPD, true, true, true,  SingleSpc, 4, 4,true},
{ ARM::VLD4DUPd32Pseudo,     ARM::VLD4DUPd32,     true, false, false, SingleSpc, 4, 2,true},
{ ARM::VLD4DUPd32Pseudo_UPD, ARM::VLD4DUPd32_UPD, true, true, true,  SingleSpc, 4, 2,true},
{ ARM::VLD4DUPd8Pseudo,      ARM::VLD4DUPd8,      true, false, false, SingleSpc, 4, 8,true},
{ ARM::VLD4DUPd8Pseudo_UPD,  ARM::VLD4DUPd8_UPD, true, true, true,  SingleSpc, 4, 8,true},
{ ARM::VLD4DUPq16EvenPseudo, ARM::VLD4DUPq16,     true, false, false, EvenDblSpc, 4, 4 ,true},
{ ARM::VLD4DUPq16OddPseudo,  ARM::VLD4DUPq16,     true, false, false, OddDblSpc,  4, 4 ,true},
{ ARM::VLD4DUPq16OddPseudo_UPD,  ARM::VLD4DUPq16_UPD, true, true, true, OddDblSpc,  4, 4 ,true},
{ ARM::VLD4DUPq32EvenPseudo, ARM::VLD4DUPq32,     true, false, false, EvenDblSpc, 4, 2 ,true},
{ ARM::VLD4DUPq32OddPseudo,  ARM::VLD4DUPq32,     true, false, false, OddDblSpc,  4, 2 ,true},
{ ARM::VLD4DUPq32OddPseudo_UPD,  ARM::VLD4DUPq32_UPD, true, true, true, OddDblSpc,  4, 2 ,true},
{ ARM::VLD4DUPq8EvenPseudo,  ARM::VLD4DUPq8,      true, false, false, EvenDblSpc, 4, 8 ,true},
{ ARM::VLD4DUPq8OddPseudo,   ARM::VLD4DUPq8,      true, false, false, OddDblSpc,  4, 8 ,true},
{ ARM::VLD4DUPq8OddPseudo_UPD,   ARM::VLD4DUPq8_UPD, true, true, true, OddDblSpc,  4, 8 ,true},

{ ARM::VLD4LNd16Pseudo,     ARM::VLD4LNd16,     true, false, false, SingleSpc,  4, 4 ,true},
{ ARM::VLD4LNd16Pseudo_UPD, ARM::VLD4LNd16_UPD, true, true, true,  SingleSpc,  4, 4 ,true},
{ ARM::VLD4LNd32Pseudo,     ARM::VLD4LNd32,     true, false, false, SingleSpc,  4, 2 ,true},
{ ARM::VLD4LNd32Pseudo_UPD, ARM::VLD4LNd32_UPD, true, true, true,  SingleSpc,  4, 2 ,true},
{ ARM::VLD4LNd8Pseudo,      ARM::VLD4LNd8,      true, false, false, SingleSpc,  4, 8 ,true},
{ ARM::VLD4LNd8Pseudo_UPD,  ARM::VLD4LNd8_UPD, true, true, true,  SingleSpc,  4, 8 ,true},
{ ARM::VLD4LNq16Pseudo,     ARM::VLD4LNq16,     true, false, false, EvenDblSpc, 4, 4 ,true},
{ ARM::VLD4LNq16Pseudo_UPD, ARM::VLD4LNq16_UPD, true, true, true,  EvenDblSpc, 4, 4 ,true},
{ ARM::VLD4LNq32Pseudo,     ARM::VLD4LNq32,     true, false, false, EvenDblSpc, 4, 2 ,true},
{ ARM::VLD4LNq32Pseudo_UPD, ARM::VLD4LNq32_UPD, true, true, true,  EvenDblSpc, 4, 2 ,true},

{ ARM::VLD4d16Pseudo,       ARM::VLD4d16,      true,  false, false, SingleSpc,  4, 4 ,true},
{ ARM::VLD4d16Pseudo_UPD,   ARM::VLD4d16_UPD, true, true, true,  SingleSpc,  4, 4 ,true},
{ ARM::VLD4d32Pseudo,       ARM::VLD4d32,      true,  false, false, SingleSpc,  4, 2 ,true},
{ ARM::VLD4d32Pseudo_UPD,   ARM::VLD4d32_UPD, true, true, true,  SingleSpc,  4, 2 ,true},
{ ARM::VLD4d8Pseudo,        ARM::VLD4d8,       true,  false, false, SingleSpc,  4, 8 ,true},
{ ARM::VLD4d8Pseudo_UPD,    ARM::VLD4d8_UPD, true, true, true,  SingleSpc,  4, 8 ,true},

{ ARM::VLD4q16Pseudo_UPD,    ARM::VLD4q16_UPD, true, true, true,  EvenDblSpc, 4, 4 ,true},
{ ARM::VLD4q16oddPseudo,     ARM::VLD4q16,     true,  false, false, OddDblSpc,  4, 4 ,true},
{ ARM::VLD4q16oddPseudo_UPD, ARM::VLD4q16_UPD, true, true, true,  OddDblSpc,  4, 4 ,true},
{ ARM::VLD4q32Pseudo_UPD,    ARM::VLD4q32_UPD, true, true, true,  EvenDblSpc, 4, 2 ,true},
{ ARM::VLD4q32oddPseudo,     ARM::VLD4q32,     true,  false, false, OddDblSpc,  4, 2 ,true},
{ ARM::VLD4q32oddPseudo_UPD, ARM::VLD4q32_UPD, true, true, true,  OddDblSpc,  4, 2 ,true},
{ ARM::VLD4q8Pseudo_UPD,     ARM::VLD4q8_UPD, true, true, true,  EvenDblSpc, 4, 8 ,true},
{ ARM::VLD4q8oddPseudo,      ARM::VLD4q8,      true,  false, false, OddDblSpc,  4, 8 ,true},
{ ARM::VLD4q8oddPseudo_UPD,  ARM::VLD4q8_UPD, true, true, true,  OddDblSpc,  4, 8 ,true},

{ ARM::VST1LNq16Pseudo,     ARM::VST1LNd16,    false, false, false, EvenDblSpc, 1, 4 ,true},
{ ARM::VST1LNq16Pseudo_UPD, ARM::VST1LNd16_UPD, false, true, true,  EvenDblSpc, 1, 4 ,true},
{ ARM::VST1LNq32Pseudo,     ARM::VST1LNd32,    false, false, false, EvenDblSpc, 1, 2 ,true},
{ ARM::VST1LNq32Pseudo_UPD, ARM::VST1LNd32_UPD, false, true, true,  EvenDblSpc, 1, 2 ,true},
{ ARM::VST1LNq8Pseudo,      ARM::VST1LNd8,     false, false, false, EvenDblSpc, 1, 8 ,true},
{ ARM::VST1LNq8Pseudo_UPD,  ARM::VST1LNd8_UPD, false, true, true,  EvenDblSpc, 1, 8 ,true},

{ ARM::VST1d16QPseudo,      ARM::VST1d16Q,     false, false, false, SingleSpc,  4, 4 ,false},
{ ARM::VST1d16QPseudoWB_fixed,  ARM::VST1d16Qwb_fixed, false, true, false, SingleSpc,  4, 4 ,false},
{ ARM::VST1d16QPseudoWB_register, ARM::VST1d16Qwb_register, false, true, true, SingleSpc,  4, 4 ,false},
{ ARM::VST1d16TPseudo,      ARM::VST1d16T,     false, false, false, SingleSpc,  3, 4 ,false},
{ ARM::VST1d16TPseudoWB_fixed,  ARM::VST1d16Twb_fixed, false, true, false, SingleSpc,  3, 4 ,false},
{ ARM::VST1d16TPseudoWB_register, ARM::VST1d16Twb_register, false, true, true, SingleSpc,  3, 4 ,false},

{ ARM::VST1d32QPseudo,      ARM::VST1d32Q,     false, false, false, SingleSpc,  4, 2 ,false},
{ ARM::VST1d32QPseudoWB_fixed,  ARM::VST1d32Qwb_fixed, false, true, false, SingleSpc,  4, 2 ,false},
{ ARM::VST1d32QPseudoWB_register, ARM::VST1d32Qwb_register, false, true, true, SingleSpc,  4, 2 ,false},
{ ARM::VST1d32TPseudo,      ARM::VST1d32T,     false, false, false, SingleSpc,  3, 2 ,false},
{ ARM::VST1d32TPseudoWB_fixed,  ARM::VST1d32Twb_fixed, false, true, false, SingleSpc,  3, 2 ,false},
{ ARM::VST1d32TPseudoWB_register, ARM::VST1d32Twb_register, false, true, true, SingleSpc,  3, 2 ,false},

{ ARM::VST1d64QPseudo,      ARM::VST1d64Q,     false, false, false, SingleSpc,  4, 1 ,false},
{ ARM::VST1d64QPseudoWB_fixed,  ARM::VST1d64Qwb_fixed, false, true, false,  SingleSpc,  4, 1 ,false},
{ ARM::VST1d64QPseudoWB_register, ARM::VST1d64Qwb_register, false, true, true,  SingleSpc,  4, 1 ,false},
{ ARM::VST1d64TPseudo,      ARM::VST1d64T,     false, false, false, SingleSpc,  3, 1 ,false},
{ ARM::VST1d64TPseudoWB_fixed,  ARM::VST1d64Twb_fixed, false, true, false,  SingleSpc,  3, 1 ,false},
{ ARM::VST1d64TPseudoWB_register, ARM::VST1d64Twb_register, false, true, true,  SingleSpc,  3, 1 ,false},

{ ARM::VST1d8QPseudo,       ARM::VST1d8Q,      false, false, false, SingleSpc,  4, 8 ,false},
{ ARM::VST1d8QPseudoWB_fixed,   ARM::VST1d8Qwb_fixed, false, true, false, SingleSpc,  4, 8 ,false},
{ ARM::VST1d8QPseudoWB_register,  ARM::VST1d8Qwb_register, false, true, true, SingleSpc,  4, 8 ,false},
{ ARM::VST1d8TPseudo,       ARM::VST1d8T,      false, false, false, SingleSpc,  3, 8 ,false},
{ ARM::VST1d8TPseudoWB_fixed,   ARM::VST1d8Twb_fixed, false, true, false, SingleSpc,  3, 8 ,false},
{ ARM::VST1d8TPseudoWB_register,  ARM::VST1d8Twb_register, false, true, true, SingleSpc,  3, 8 ,false},

{ ARM::VST1q16HighQPseudo,  ARM::VST1d16Q,     false, false, false, SingleHighQSpc,   4, 4 ,false},
{ ARM::VST1q16HighQPseudo_UPD,  ARM::VST1d16Qwb_fixed,  false, true, true, SingleHighQSpc,   4, 8 ,false},
{ ARM::VST1q16HighTPseudo,  ARM::VST1d16T,     false, false, false, SingleHighTSpc,   3, 4 ,false},
{ ARM::VST1q16HighTPseudo_UPD,  ARM::VST1d16Twb_fixed,  false, true, true, SingleHighTSpc,   3, 4 ,false},
{ ARM::VST1q16LowQPseudo_UPD,   ARM::VST1d16Qwb_fixed,  false, true, true, SingleLowSpc,   4, 4 ,false},
{ ARM::VST1q16LowTPseudo_UPD,   ARM::VST1d16Twb_fixed,  false, true, true, SingleLowSpc,   3, 4 ,false},

{ ARM::VST1q32HighQPseudo,  ARM::VST1d32Q,     false, false, false, SingleHighQSpc,   4, 2 ,false},
{ ARM::VST1q32HighQPseudo_UPD,  ARM::VST1d32Qwb_fixed,  false, true, true, SingleHighQSpc,   4, 8 ,false},
{ ARM::VST1q32HighTPseudo,  ARM::VST1d32T,     false, false, false, SingleHighTSpc,   3, 2 ,false},
{ ARM::VST1q32HighTPseudo_UPD,  ARM::VST1d32Twb_fixed,  false, true, true, SingleHighTSpc,   3, 2 ,false},
{ ARM::VST1q32LowQPseudo_UPD,   ARM::VST1d32Qwb_fixed,  false, true, true, SingleLowSpc,   4, 2 ,false},
{ ARM::VST1q32LowTPseudo_UPD,   ARM::VST1d32Twb_fixed,  false, true, true, SingleLowSpc,   3, 2 ,false},

{ ARM::VST1q64HighQPseudo,  ARM::VST1d64Q,     false, false, false, SingleHighQSpc,   4, 1 ,false},
{ ARM::VST1q64HighQPseudo_UPD,  ARM::VST1d64Qwb_fixed,  false, true, true, SingleHighQSpc,   4, 8 ,false},
{ ARM::VST1q64HighTPseudo,  ARM::VST1d64T,     false, false, false, SingleHighTSpc,   3, 1 ,false},
{ ARM::VST1q64HighTPseudo_UPD,  ARM::VST1d64Twb_fixed,  false, true, true, SingleHighTSpc,   3, 1 ,false},
{ ARM::VST1q64LowQPseudo_UPD,   ARM::VST1d64Qwb_fixed,  false, true, true, SingleLowSpc,   4, 1 ,false},
{ ARM::VST1q64LowTPseudo_UPD,   ARM::VST1d64Twb_fixed,  false, true, true, SingleLowSpc,   3, 1 ,false},

{ ARM::VST1q8HighQPseudo,   ARM::VST1d8Q,      false, false, false, SingleHighQSpc,   4, 8 ,false},
{ ARM::VST1q8HighQPseudo_UPD,  ARM::VST1d8Qwb_fixed,  false, true, true, SingleHighQSpc,   4, 8 ,false},
{ ARM::VST1q8HighTPseudo,   ARM::VST1d8T,      false, false, false, SingleHighTSpc,   3, 8 ,false},
{ ARM::VST1q8HighTPseudo_UPD,  ARM::VST1d8Twb_fixed,  false, true, true, SingleHighTSpc,   3, 8 ,false},
{ ARM::VST1q8LowQPseudo_UPD,   ARM::VST1d8Qwb_fixed,  false, true, true, SingleLowSpc,   4, 8 ,false},
{ ARM::VST1q8LowTPseudo_UPD,   ARM::VST1d8Twb_fixed,  false, true, true, SingleLowSpc,   3, 8 ,false},

{ ARM::VST2LNd16Pseudo,     ARM::VST2LNd16,     false, false, false, SingleSpc, 2, 4 ,true},
{ ARM::VST2LNd16Pseudo_UPD, ARM::VST2LNd16_UPD, false, true, true,  SingleSpc, 2, 4 ,true},
{ ARM::VST2LNd32Pseudo,     ARM::VST2LNd32,     false, false, false, SingleSpc, 2, 2 ,true},
{ ARM::VST2LNd32Pseudo_UPD, ARM::VST2LNd32_UPD, false, true, true,  SingleSpc, 2, 2 ,true},
{ ARM::VST2LNd8Pseudo,      ARM::VST2LNd8,      false, false, false, SingleSpc, 2, 8 ,true},
{ ARM::VST2LNd8Pseudo_UPD,  ARM::VST2LNd8_UPD, false, true, true,  SingleSpc, 2, 8 ,true},
{ ARM::VST2LNq16Pseudo,     ARM::VST2LNq16,     false, false, false, EvenDblSpc, 2, 4,true},
{ ARM::VST2LNq16Pseudo_UPD, ARM::VST2LNq16_UPD, false, true, true,  EvenDblSpc, 2, 4,true},
{ ARM::VST2LNq32Pseudo,     ARM::VST2LNq32,     false, false, false, EvenDblSpc, 2, 2,true},
{ ARM::VST2LNq32Pseudo_UPD, ARM::VST2LNq32_UPD, false, true, true,  EvenDblSpc, 2, 2,true},

{ ARM::VST2q16Pseudo,       ARM::VST2q16,      false, false, false, SingleSpc,  4, 4 ,false},
{ ARM::VST2q16PseudoWB_fixed,   ARM::VST2q16wb_fixed, false, true, false,  SingleSpc,  4, 4 ,false},
{ ARM::VST2q16PseudoWB_register,   ARM::VST2q16wb_register, false, true, true,  SingleSpc,  4, 4 ,false},
{ ARM::VST2q32Pseudo,       ARM::VST2q32,      false, false, false, SingleSpc,  4, 2 ,false},
{ ARM::VST2q32PseudoWB_fixed,   ARM::VST2q32wb_fixed, false, true, false,  SingleSpc,  4, 2 ,false},
{ ARM::VST2q32PseudoWB_register,   ARM::VST2q32wb_register, false, true, true,  SingleSpc,  4, 2 ,false},
{ ARM::VST2q8Pseudo,        ARM::VST2q8,       false, false, false, SingleSpc,  4, 8 ,false},
{ ARM::VST2q8PseudoWB_fixed,    ARM::VST2q8wb_fixed, false, true, false,  SingleSpc,  4, 8 ,false},
{ ARM::VST2q8PseudoWB_register,    ARM::VST2q8wb_register, false, true, true,  SingleSpc,  4, 8 ,false},

{ ARM::VST3LNd16Pseudo,     ARM::VST3LNd16,     false, false, false, SingleSpc, 3, 4 ,true},
{ ARM::VST3LNd16Pseudo_UPD, ARM::VST3LNd16_UPD, false, true, true,  SingleSpc, 3, 4 ,true},
{ ARM::VST3LNd32Pseudo,     ARM::VST3LNd32,     false, false, false, SingleSpc, 3, 2 ,true},
{ ARM::VST3LNd32Pseudo_UPD, ARM::VST3LNd32_UPD, false, true, true,  SingleSpc, 3, 2 ,true},
{ ARM::VST3LNd8Pseudo,      ARM::VST3LNd8,      false, false, false, SingleSpc, 3, 8 ,true},
{ ARM::VST3LNd8Pseudo_UPD,  ARM::VST3LNd8_UPD, false, true, true,  SingleSpc, 3, 8 ,true},
{ ARM::VST3LNq16Pseudo,     ARM::VST3LNq16,     false, false, false, EvenDblSpc, 3, 4,true},
{ ARM::VST3LNq16Pseudo_UPD, ARM::VST3LNq16_UPD, false, true, true,  EvenDblSpc, 3, 4,true},
{ ARM::VST3LNq32Pseudo,     ARM::VST3LNq32,     false, false, false, EvenDblSpc, 3, 2,true},
{ ARM::VST3LNq32Pseudo_UPD, ARM::VST3LNq32_UPD, false, true, true,  EvenDblSpc, 3, 2,true},

{ ARM::VST3d16Pseudo,       ARM::VST3d16,      false, false, false, SingleSpc,  3, 4 ,true},
{ ARM::VST3d16Pseudo_UPD,   ARM::VST3d16_UPD, false, true, true,  SingleSpc,  3, 4 ,true},
{ ARM::VST3d32Pseudo,       ARM::VST3d32,      false, false, false, SingleSpc,  3, 2 ,true},
{ ARM::VST3d32Pseudo_UPD,   ARM::VST3d32_UPD, false, true, true,  SingleSpc,  3, 2 ,true},
{ ARM::VST3d8Pseudo,        ARM::VST3d8,       false, false, false, SingleSpc,  3, 8 ,true},
{ ARM::VST3d8Pseudo_UPD,    ARM::VST3d8_UPD, false, true, true,  SingleSpc,  3, 8 ,true},

{ ARM::VST3q16Pseudo_UPD,    ARM::VST3q16_UPD, false, true, true,  EvenDblSpc, 3, 4 ,true},
{ ARM::VST3q16oddPseudo,     ARM::VST3q16,     false, false, false, OddDblSpc,  3, 4 ,true},
{ ARM::VST3q16oddPseudo_UPD, ARM::VST3q16_UPD, false, true, true,  OddDblSpc,  3, 4 ,true},
{ ARM::VST3q32Pseudo_UPD,    ARM::VST3q32_UPD, false, true, true,  EvenDblSpc, 3, 2 ,true},
{ ARM::VST3q32oddPseudo,     ARM::VST3q32,     false, false, false, OddDblSpc,  3, 2 ,true},
{ ARM::VST3q32oddPseudo_UPD, ARM::VST3q32_UPD, false, true, true,  OddDblSpc,  3, 2 ,true},
{ ARM::VST3q8Pseudo_UPD,     ARM::VST3q8_UPD, false, true, true,  EvenDblSpc, 3, 8 ,true},
{ ARM::VST3q8oddPseudo,      ARM::VST3q8,      false, false, false, OddDblSpc,  3, 8 ,true},
{ ARM::VST3q8oddPseudo_UPD,  ARM::VST3q8_UPD, false, true, true,  OddDblSpc,  3, 8 ,true},

{ ARM::VST4LNd16Pseudo,     ARM::VST4LNd16,     false, false, false, SingleSpc, 4, 4 ,true},
{ ARM::VST4LNd16Pseudo_UPD, ARM::VST4LNd16_UPD, false, true, true,  SingleSpc, 4, 4 ,true},
{ ARM::VST4LNd32Pseudo,     ARM::VST4LNd32,     false, false, false, SingleSpc, 4, 2 ,true},
{ ARM::VST4LNd32Pseudo_UPD, ARM::VST4LNd32_UPD, false, true, true,  SingleSpc, 4, 2 ,true},
{ ARM::VST4LNd8Pseudo,      ARM::VST4LNd8,      false, false, false, SingleSpc, 4, 8 ,true},
{ ARM::VST4LNd8Pseudo_UPD,  ARM::VST4LNd8_UPD, false, true, true,  SingleSpc, 4, 8 ,true},
{ ARM::VST4LNq16Pseudo,     ARM::VST4LNq16,     false, false, false, EvenDblSpc, 4, 4,true},
{ ARM::VST4LNq16Pseudo_UPD, ARM::VST4LNq16_UPD, false, true, true,  EvenDblSpc, 4, 4,true},
{ ARM::VST4LNq32Pseudo,     ARM::VST4LNq32,     false, false, false, EvenDblSpc, 4, 2,true},
{ ARM::VST4LNq32Pseudo_UPD, ARM::VST4LNq32_UPD, false, true, true,  EvenDblSpc, 4, 2,true},

{ ARM::VST4d16Pseudo,       ARM::VST4d16,      false, false, false, SingleSpc,  4, 4 ,true},
{ ARM::VST4d16Pseudo_UPD,   ARM::VST4d16_UPD, false, true, true,  SingleSpc,  4, 4 ,true},
{ ARM::VST4d32Pseudo,       ARM::VST4d32,      false, false, false, SingleSpc,  4, 2 ,true},
{ ARM::VST4d32Pseudo_UPD,   ARM::VST4d32_UPD, false, true, true,  SingleSpc,  4, 2 ,true},
{ ARM::VST4d8Pseudo,        ARM::VST4d8,       false, false, false, SingleSpc,  4, 8 ,true},
{ ARM::VST4d8Pseudo_UPD,    ARM::VST4d8_UPD, false, true, true,  SingleSpc,  4, 8 ,true},

{ ARM::VST4q16Pseudo_UPD,    ARM::VST4q16_UPD, false, true, true,  EvenDblSpc, 4, 4 ,true},
{ ARM::VST4q16oddPseudo,     ARM::VST4q16,     false, false, false, OddDblSpc,  4, 4 ,true},
{ ARM::VST4q16oddPseudo_UPD, ARM::VST4q16_UPD, false, true, true,  OddDblSpc,  4, 4 ,true},
{ ARM::VST4q32Pseudo_UPD,    ARM::VST4q32_UPD, false, true, true,  EvenDblSpc, 4, 2 ,true},
{ ARM::VST4q32oddPseudo,     ARM::VST4q32,     false, false, false, OddDblSpc,  4, 2 ,true},
{ ARM::VST4q32oddPseudo_UPD, ARM::VST4q32_UPD, false, true, true,  OddDblSpc,  4, 2 ,true},
{ ARM::VST4q8Pseudo_UPD,     ARM::VST4q8_UPD, false, true, true,  EvenDblSpc, 4, 8 ,true},
{ ARM::VST4q8oddPseudo,      ARM::VST4q8,      false, false, false, OddDblSpc,  4, 8 ,true},
{ ARM::VST4q8oddPseudo_UPD,  ARM::VST4q8_UPD, false, true, true,  OddDblSpc,  4, 8 ,true}
};

/// LookupNEONLdSt - Search the NEONLdStTable for information about a NEON
/// load or store pseudo instruction.
static const NEONLdStTableEntry *LookupNEONLdSt(unsigned Opcode) {
#ifndef NDEBUG
  // Make sure the table is sorted.
  static std::atomic<bool> TableChecked(false);
  if (!TableChecked.load(std::memory_order_relaxed)) {
    assert(llvm::is_sorted(NEONLdStTable) && "NEONLdStTable is not sorted!");
    TableChecked.store(true, std::memory_order_relaxed);
  }
#endif

  auto I = llvm::lower_bound(NEONLdStTable, Opcode);
  if (I != std::end(NEONLdStTable) && I->PseudoOpc == Opcode)
    return I;
  return nullptr;
}

/// GetDSubRegs - Get 4 D subregisters of a Q, QQ, or QQQQ register,
/// corresponding to the specified register spacing.  Not all of the results
/// are necessarily valid, e.g., a Q register only has 2 D subregisters.
static void GetDSubRegs(unsigned Reg, NEONRegSpacing RegSpc,
                        const TargetRegisterInfo *TRI, unsigned &D0,
                        unsigned &D1, unsigned &D2, unsigned &D3) {
  if (RegSpc == SingleSpc || RegSpc == SingleLowSpc) {
    D0 = TRI->getSubReg(Reg, ARM::dsub_0);
    D1 = TRI->getSubReg(Reg, ARM::dsub_1);
    D2 = TRI->getSubReg(Reg, ARM::dsub_2);
    D3 = TRI->getSubReg(Reg, ARM::dsub_3);
  } else if (RegSpc == SingleHighQSpc) {
    D0 = TRI->getSubReg(Reg, ARM::dsub_4);
    D1 = TRI->getSubReg(Reg, ARM::dsub_5);
    D2 = TRI->getSubReg(Reg, ARM::dsub_6);
    D3 = TRI->getSubReg(Reg, ARM::dsub_7);
  } else if (RegSpc == SingleHighTSpc) {
    D0 = TRI->getSubReg(Reg, ARM::dsub_3);
    D1 = TRI->getSubReg(Reg, ARM::dsub_4);
    D2 = TRI->getSubReg(Reg, ARM::dsub_5);
    D3 = TRI->getSubReg(Reg, ARM::dsub_6);
  } else if (RegSpc == EvenDblSpc) {
    D0 = TRI->getSubReg(Reg, ARM::dsub_0);
    D1 = TRI->getSubReg(Reg, ARM::dsub_2);
    D2 = TRI->getSubReg(Reg, ARM::dsub_4);
    D3 = TRI->getSubReg(Reg, ARM::dsub_6);
  } else {
    assert(RegSpc == OddDblSpc && "unknown register spacing");
    D0 = TRI->getSubReg(Reg, ARM::dsub_1);
    D1 = TRI->getSubReg(Reg, ARM::dsub_3);
    D2 = TRI->getSubReg(Reg, ARM::dsub_5);
    D3 = TRI->getSubReg(Reg, ARM::dsub_7);
  }
}

/// ExpandVLD - Translate VLD pseudo instructions with Q, QQ or QQQQ register
/// operands to real VLD instructions with D register operands.
void ARMExpandPseudo::ExpandVLD(MachineBasicBlock::iterator &MBBI) {
  MachineInstr &MI = *MBBI;
  MachineBasicBlock &MBB = *MI.getParent();
  LLVM_DEBUG(dbgs() << "Expanding: "; MI.dump());

  const NEONLdStTableEntry *TableEntry = LookupNEONLdSt(MI.getOpcode());
  assert(TableEntry && TableEntry->IsLoad && "NEONLdStTable lookup failed");
  NEONRegSpacing RegSpc = (NEONRegSpacing)TableEntry->RegSpacing;
  unsigned NumRegs = TableEntry->NumRegs;

  MachineInstrBuilder MIB = BuildMI(MBB, MBBI, MI.getDebugLoc(),
                                    TII->get(TableEntry->RealOpc));
  unsigned OpIdx = 0;

  bool DstIsDead = MI.getOperand(OpIdx).isDead();
  Register DstReg = MI.getOperand(OpIdx++).getReg();

  bool IsVLD2DUP = TableEntry->RealOpc == ARM::VLD2DUPd8x2 ||
                   TableEntry->RealOpc == ARM::VLD2DUPd16x2 ||
                   TableEntry->RealOpc == ARM::VLD2DUPd32x2 ||
                   TableEntry->RealOpc == ARM::VLD2DUPd8x2wb_fixed ||
                   TableEntry->RealOpc == ARM::VLD2DUPd16x2wb_fixed ||
                   TableEntry->RealOpc == ARM::VLD2DUPd32x2wb_fixed ||
                   TableEntry->RealOpc == ARM::VLD2DUPd8x2wb_register ||
                   TableEntry->RealOpc == ARM::VLD2DUPd16x2wb_register ||
                   TableEntry->RealOpc == ARM::VLD2DUPd32x2wb_register;

  if (IsVLD2DUP) {
    unsigned SubRegIndex;
    if (RegSpc == EvenDblSpc) {
      SubRegIndex = ARM::dsub_0;
    } else {
      assert(RegSpc == OddDblSpc && "Unexpected spacing!");
      SubRegIndex = ARM::dsub_1;
    }
    Register SubReg = TRI->getSubReg(DstReg, SubRegIndex);
    unsigned DstRegPair = TRI->getMatchingSuperReg(SubReg, ARM::dsub_0,
                                                   &ARM::DPairSpcRegClass);
    MIB.addReg(DstRegPair, RegState::Define | getDeadRegState(DstIsDead));
  } else {
    unsigned D0, D1, D2, D3;
    GetDSubRegs(DstReg, RegSpc, TRI, D0, D1, D2, D3);
    MIB.addReg(D0, RegState::Define | getDeadRegState(DstIsDead));
    if (NumRegs > 1 && TableEntry->copyAllListRegs)
      MIB.addReg(D1, RegState::Define | getDeadRegState(DstIsDead));
    if (NumRegs > 2 && TableEntry->copyAllListRegs)
      MIB.addReg(D2, RegState::Define | getDeadRegState(DstIsDead));
    if (NumRegs > 3 && TableEntry->copyAllListRegs)
      MIB.addReg(D3, RegState::Define | getDeadRegState(DstIsDead));
  }

  if (TableEntry->isUpdating)
    MIB.add(MI.getOperand(OpIdx++));

  // Copy the addrmode6 operands.
  MIB.add(MI.getOperand(OpIdx++));
  MIB.add(MI.getOperand(OpIdx++));

  // Copy the am6offset operand.
  if (TableEntry->hasWritebackOperand) {
    // TODO: The writing-back pseudo instructions we translate here are all
    // defined to take am6offset nodes that are capable to represent both fixed
    // and register forms. Some real instructions, however, do not rely on
    // am6offset and have separate definitions for such forms. When this is the
    // case, fixed forms do not take any offset nodes, so here we skip them for
    // such instructions. Once all real and pseudo writing-back instructions are
    // rewritten without use of am6offset nodes, this code will go away.
    const MachineOperand &AM6Offset = MI.getOperand(OpIdx++);
    if (TableEntry->RealOpc == ARM::VLD1d8Qwb_fixed ||
        TableEntry->RealOpc == ARM::VLD1d16Qwb_fixed ||
        TableEntry->RealOpc == ARM::VLD1d32Qwb_fixed ||
        TableEntry->RealOpc == ARM::VLD1d64Qwb_fixed ||
        TableEntry->RealOpc == ARM::VLD1d8Twb_fixed ||
        TableEntry->RealOpc == ARM::VLD1d16Twb_fixed ||
        TableEntry->RealOpc == ARM::VLD1d32Twb_fixed ||
        TableEntry->RealOpc == ARM::VLD1d64Twb_fixed ||
        TableEntry->RealOpc == ARM::VLD2DUPd8x2wb_fixed ||
        TableEntry->RealOpc == ARM::VLD2DUPd16x2wb_fixed ||
        TableEntry->RealOpc == ARM::VLD2DUPd32x2wb_fixed) {
      assert(AM6Offset.getReg() == 0 &&
             "A fixed writing-back pseudo instruction provides an offset "
             "register!");
    } else {
      MIB.add(AM6Offset);
    }
  }

  // For an instruction writing double-spaced subregs, the pseudo instruction
  // has an extra operand that is a use of the super-register.  Record the
  // operand index and skip over it.
  unsigned SrcOpIdx = 0;
  if (RegSpc == EvenDblSpc || RegSpc == OddDblSpc || RegSpc == SingleLowSpc ||
      RegSpc == SingleHighQSpc || RegSpc == SingleHighTSpc)
    SrcOpIdx = OpIdx++;

  // Copy the predicate operands.
  MIB.add(MI.getOperand(OpIdx++));
  MIB.add(MI.getOperand(OpIdx++));

  // Copy the super-register source operand used for double-spaced subregs over
  // to the new instruction as an implicit operand.
  if (SrcOpIdx != 0) {
    MachineOperand MO = MI.getOperand(SrcOpIdx);
    MO.setImplicit(true);
    MIB.add(MO);
  }
  // Add an implicit def for the super-register.
  MIB.addReg(DstReg, RegState::ImplicitDefine | getDeadRegState(DstIsDead));
  MIB.copyImplicitOps(MI);

  // Transfer memoperands.
  MIB.cloneMemRefs(MI);
  MI.eraseFromParent();
  LLVM_DEBUG(dbgs() << "To:        "; MIB.getInstr()->dump(););
}

/// ExpandVST - Translate VST pseudo instructions with Q, QQ or QQQQ register
/// operands to real VST instructions with D register operands.
void ARMExpandPseudo::ExpandVST(MachineBasicBlock::iterator &MBBI) {
  MachineInstr &MI = *MBBI;
  MachineBasicBlock &MBB = *MI.getParent();
  LLVM_DEBUG(dbgs() << "Expanding: "; MI.dump());

  const NEONLdStTableEntry *TableEntry = LookupNEONLdSt(MI.getOpcode());
  assert(TableEntry && !TableEntry->IsLoad && "NEONLdStTable lookup failed");
  NEONRegSpacing RegSpc = (NEONRegSpacing)TableEntry->RegSpacing;
  unsigned NumRegs = TableEntry->NumRegs;

  MachineInstrBuilder MIB = BuildMI(MBB, MBBI, MI.getDebugLoc(),
                                    TII->get(TableEntry->RealOpc));
  unsigned OpIdx = 0;
  if (TableEntry->isUpdating)
    MIB.add(MI.getOperand(OpIdx++));

  // Copy the addrmode6 operands.
  MIB.add(MI.getOperand(OpIdx++));
  MIB.add(MI.getOperand(OpIdx++));

  if (TableEntry->hasWritebackOperand) {
    // TODO: The writing-back pseudo instructions we translate here are all
    // defined to take am6offset nodes that are capable to represent both fixed
    // and register forms. Some real instructions, however, do not rely on
    // am6offset and have separate definitions for such forms. When this is the
    // case, fixed forms do not take any offset nodes, so here we skip them for
    // such instructions. Once all real and pseudo writing-back instructions are
    // rewritten without use of am6offset nodes, this code will go away.
    const MachineOperand &AM6Offset = MI.getOperand(OpIdx++);
    if (TableEntry->RealOpc == ARM::VST1d8Qwb_fixed ||
        TableEntry->RealOpc == ARM::VST1d16Qwb_fixed ||
        TableEntry->RealOpc == ARM::VST1d32Qwb_fixed ||
        TableEntry->RealOpc == ARM::VST1d64Qwb_fixed ||
        TableEntry->RealOpc == ARM::VST1d8Twb_fixed ||
        TableEntry->RealOpc == ARM::VST1d16Twb_fixed ||
        TableEntry->RealOpc == ARM::VST1d32Twb_fixed ||
        TableEntry->RealOpc == ARM::VST1d64Twb_fixed) {
      assert(AM6Offset.getReg() == 0 &&
             "A fixed writing-back pseudo instruction provides an offset "
             "register!");
    } else {
      MIB.add(AM6Offset);
    }
  }

  bool SrcIsKill = MI.getOperand(OpIdx).isKill();
  bool SrcIsUndef = MI.getOperand(OpIdx).isUndef();
  Register SrcReg = MI.getOperand(OpIdx++).getReg();
  unsigned D0, D1, D2, D3;
  GetDSubRegs(SrcReg, RegSpc, TRI, D0, D1, D2, D3);
  MIB.addReg(D0, getUndefRegState(SrcIsUndef));
  if (NumRegs > 1 && TableEntry->copyAllListRegs)
    MIB.addReg(D1, getUndefRegState(SrcIsUndef));
  if (NumRegs > 2 && TableEntry->copyAllListRegs)
    MIB.addReg(D2, getUndefRegState(SrcIsUndef));
  if (NumRegs > 3 && TableEntry->copyAllListRegs)
    MIB.addReg(D3, getUndefRegState(SrcIsUndef));

  // Copy the predicate operands.
  MIB.add(MI.getOperand(OpIdx++));
  MIB.add(MI.getOperand(OpIdx++));

  if (SrcIsKill && !SrcIsUndef) // Add an implicit kill for the super-reg.
    MIB->addRegisterKilled(SrcReg, TRI, true);
  else if (!SrcIsUndef)
    MIB.addReg(SrcReg, RegState::Implicit); // Add implicit uses for src reg.
  MIB.copyImplicitOps(MI);

  // Transfer memoperands.
  MIB.cloneMemRefs(MI);
  MI.eraseFromParent();
  LLVM_DEBUG(dbgs() << "To:        "; MIB.getInstr()->dump(););
}

/// ExpandLaneOp - Translate VLD*LN and VST*LN instructions with Q, QQ or QQQQ
/// register operands to real instructions with D register operands.
void ARMExpandPseudo::ExpandLaneOp(MachineBasicBlock::iterator &MBBI) {
  MachineInstr &MI = *MBBI;
  MachineBasicBlock &MBB = *MI.getParent();
  LLVM_DEBUG(dbgs() << "Expanding: "; MI.dump());

  const NEONLdStTableEntry *TableEntry = LookupNEONLdSt(MI.getOpcode());
  assert(TableEntry && "NEONLdStTable lookup failed");
  NEONRegSpacing RegSpc = (NEONRegSpacing)TableEntry->RegSpacing;
  unsigned NumRegs = TableEntry->NumRegs;
  unsigned RegElts = TableEntry->RegElts;

  MachineInstrBuilder MIB = BuildMI(MBB, MBBI, MI.getDebugLoc(),
                                    TII->get(TableEntry->RealOpc));
  unsigned OpIdx = 0;
  // The lane operand is always the 3rd from last operand, before the 2
  // predicate operands.
  unsigned Lane = MI.getOperand(MI.getDesc().getNumOperands() - 3).getImm();

  // Adjust the lane and spacing as needed for Q registers.
  assert(RegSpc != OddDblSpc && "unexpected register spacing for VLD/VST-lane");
  if (RegSpc == EvenDblSpc && Lane >= RegElts) {
    RegSpc = OddDblSpc;
    Lane -= RegElts;
  }
  assert(Lane < RegElts && "out of range lane for VLD/VST-lane");

  unsigned D0 = 0, D1 = 0, D2 = 0, D3 = 0;
  unsigned DstReg = 0;
  bool DstIsDead = false;
  if (TableEntry->IsLoad) {
    DstIsDead = MI.getOperand(OpIdx).isDead();
    DstReg = MI.getOperand(OpIdx++).getReg();
    GetDSubRegs(DstReg, RegSpc, TRI, D0, D1, D2, D3);
    MIB.addReg(D0, RegState::Define | getDeadRegState(DstIsDead));
    if (NumRegs > 1)
      MIB.addReg(D1, RegState::Define | getDeadRegState(DstIsDead));
    if (NumRegs > 2)
      MIB.addReg(D2, RegState::Define | getDeadRegState(DstIsDead));
    if (NumRegs > 3)
      MIB.addReg(D3, RegState::Define | getDeadRegState(DstIsDead));
  }

  if (TableEntry->isUpdating)
    MIB.add(MI.getOperand(OpIdx++));

  // Copy the addrmode6 operands.
  MIB.add(MI.getOperand(OpIdx++));
  MIB.add(MI.getOperand(OpIdx++));
  // Copy the am6offset operand.
  if (TableEntry->hasWritebackOperand)
    MIB.add(MI.getOperand(OpIdx++));

  // Grab the super-register source.
  MachineOperand MO = MI.getOperand(OpIdx++);
  if (!TableEntry->IsLoad)
    GetDSubRegs(MO.getReg(), RegSpc, TRI, D0, D1, D2, D3);

  // Add the subregs as sources of the new instruction.
  unsigned SrcFlags = (getUndefRegState(MO.isUndef()) |
                       getKillRegState(MO.isKill()));
  MIB.addReg(D0, SrcFlags);
  if (NumRegs > 1)
    MIB.addReg(D1, SrcFlags);
  if (NumRegs > 2)
    MIB.addReg(D2, SrcFlags);
  if (NumRegs > 3)
    MIB.addReg(D3, SrcFlags);

  // Add the lane number operand.
  MIB.addImm(Lane);
  OpIdx += 1;

  // Copy the predicate operands.
  MIB.add(MI.getOperand(OpIdx++));
  MIB.add(MI.getOperand(OpIdx++));

  // Copy the super-register source to be an implicit source.
  MO.setImplicit(true);
  MIB.add(MO);
  if (TableEntry->IsLoad)
    // Add an implicit def for the super-register.
    MIB.addReg(DstReg, RegState::ImplicitDefine | getDeadRegState(DstIsDead));
  MIB.copyImplicitOps(MI);
  // Transfer memoperands.
  MIB.cloneMemRefs(MI);
  MI.eraseFromParent();
}

/// ExpandVTBL - Translate VTBL and VTBX pseudo instructions with Q or QQ
/// register operands to real instructions with D register operands.
void ARMExpandPseudo::ExpandVTBL(MachineBasicBlock::iterator &MBBI,
                                 unsigned Opc, bool IsExt) {
  MachineInstr &MI = *MBBI;
  MachineBasicBlock &MBB = *MI.getParent();
  LLVM_DEBUG(dbgs() << "Expanding: "; MI.dump());

  MachineInstrBuilder MIB = BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(Opc));
  unsigned OpIdx = 0;

  // Transfer the destination register operand.
  MIB.add(MI.getOperand(OpIdx++));
  if (IsExt) {
    MachineOperand VdSrc(MI.getOperand(OpIdx++));
    MIB.add(VdSrc);
  }

  bool SrcIsKill = MI.getOperand(OpIdx).isKill();
  Register SrcReg = MI.getOperand(OpIdx++).getReg();
  unsigned D0, D1, D2, D3;
  GetDSubRegs(SrcReg, SingleSpc, TRI, D0, D1, D2, D3);
  MIB.addReg(D0);

  // Copy the other source register operand.
  MachineOperand VmSrc(MI.getOperand(OpIdx++));
  MIB.add(VmSrc);

  // Copy the predicate operands.
  MIB.add(MI.getOperand(OpIdx++));
  MIB.add(MI.getOperand(OpIdx++));

  // Add an implicit kill and use for the super-reg.
  MIB.addReg(SrcReg, RegState::Implicit | getKillRegState(SrcIsKill));
  MIB.copyImplicitOps(MI);
  MI.eraseFromParent();
  LLVM_DEBUG(dbgs() << "To:        "; MIB.getInstr()->dump(););
}

void ARMExpandPseudo::ExpandMQQPRLoadStore(MachineBasicBlock::iterator &MBBI) {
  MachineInstr &MI = *MBBI;
  MachineBasicBlock &MBB = *MI.getParent();
  unsigned NewOpc =
      MI.getOpcode() == ARM::MQQPRStore || MI.getOpcode() == ARM::MQQQQPRStore
          ? ARM::VSTMDIA
          : ARM::VLDMDIA;
  MachineInstrBuilder MIB =
      BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(NewOpc));

  unsigned Flags = getKillRegState(MI.getOperand(0).isKill()) |
                   getDefRegState(MI.getOperand(0).isDef());
  Register SrcReg = MI.getOperand(0).getReg();

  // Copy the destination register.
  MIB.add(MI.getOperand(1));
  MIB.add(predOps(ARMCC::AL));
  MIB.addReg(TRI->getSubReg(SrcReg, ARM::dsub_0), Flags);
  MIB.addReg(TRI->getSubReg(SrcReg, ARM::dsub_1), Flags);
  MIB.addReg(TRI->getSubReg(SrcReg, ARM::dsub_2), Flags);
  MIB.addReg(TRI->getSubReg(SrcReg, ARM::dsub_3), Flags);
  if (MI.getOpcode() == ARM::MQQQQPRStore ||
      MI.getOpcode() == ARM::MQQQQPRLoad) {
    MIB.addReg(TRI->getSubReg(SrcReg, ARM::dsub_4), Flags);
    MIB.addReg(TRI->getSubReg(SrcReg, ARM::dsub_5), Flags);
    MIB.addReg(TRI->getSubReg(SrcReg, ARM::dsub_6), Flags);
    MIB.addReg(TRI->getSubReg(SrcReg, ARM::dsub_7), Flags);
  }

  if (NewOpc == ARM::VSTMDIA)
    MIB.addReg(SrcReg, RegState::Implicit);

  MIB.copyImplicitOps(MI);
  MIB.cloneMemRefs(MI);
  MI.eraseFromParent();
}

static bool IsAnAddressOperand(const MachineOperand &MO) {
  // This check is overly conservative.  Unless we are certain that the machine
  // operand is not a symbol reference, we return that it is a symbol reference.
  // This is important as the load pair may not be split up Windows.
  switch (MO.getType()) {
  case MachineOperand::MO_Register:
  case MachineOperand::MO_Immediate:
  case MachineOperand::MO_CImmediate:
  case MachineOperand::MO_FPImmediate:
  case MachineOperand::MO_ShuffleMask:
    return false;
  case MachineOperand::MO_MachineBasicBlock:
    return true;
  case MachineOperand::MO_FrameIndex:
    return false;
  case MachineOperand::MO_ConstantPoolIndex:
  case MachineOperand::MO_TargetIndex:
  case MachineOperand::MO_JumpTableIndex:
  case MachineOperand::MO_ExternalSymbol:
  case MachineOperand::MO_GlobalAddress:
  case MachineOperand::MO_BlockAddress:
    return true;
  case MachineOperand::MO_RegisterMask:
  case MachineOperand::MO_RegisterLiveOut:
    return false;
  case MachineOperand::MO_Metadata:
  case MachineOperand::MO_MCSymbol:
    return true;
  case MachineOperand::MO_DbgInstrRef:
  case MachineOperand::MO_CFIIndex:
    return false;
  case MachineOperand::MO_IntrinsicID:
  case MachineOperand::MO_Predicate:
    llvm_unreachable("should not exist post-isel");
  }
  llvm_unreachable("unhandled machine operand type");
}

static MachineOperand makeImplicit(const MachineOperand &MO) {
  MachineOperand NewMO = MO;
  NewMO.setImplicit();
  return NewMO;
}

static MachineOperand getMovOperand(const MachineOperand &MO,
                                    unsigned TargetFlag) {
  unsigned TF = MO.getTargetFlags() | TargetFlag;
  switch (MO.getType()) {
  case MachineOperand::MO_Immediate: {
    unsigned Imm = MO.getImm();
    switch (TargetFlag) {
    case ARMII::MO_HI_8_15:
      Imm = (Imm >> 24) & 0xff;
      break;
    case ARMII::MO_HI_0_7:
      Imm = (Imm >> 16) & 0xff;
      break;
    case ARMII::MO_LO_8_15:
      Imm = (Imm >> 8) & 0xff;
      break;
    case ARMII::MO_LO_0_7:
      Imm = Imm & 0xff;
      break;
    case ARMII::MO_HI16:
      Imm = (Imm >> 16) & 0xffff;
      break;
    case ARMII::MO_LO16:
      Imm = Imm & 0xffff;
      break;
    default:
      llvm_unreachable("Only HI/LO target flags are expected");
    }
    return MachineOperand::CreateImm(Imm);
  }
  case MachineOperand::MO_ExternalSymbol:
    return MachineOperand::CreateES(MO.getSymbolName(), TF);
  case MachineOperand::MO_JumpTableIndex:
    return MachineOperand::CreateJTI(MO.getIndex(), TF);
  default:
    return MachineOperand::CreateGA(MO.getGlobal(), MO.getOffset(), TF);
  }
}

void ARMExpandPseudo::ExpandTMOV32BitImm(MachineBasicBlock &MBB,
                                         MachineBasicBlock::iterator &MBBI) {
  MachineInstr &MI = *MBBI;
  Register DstReg = MI.getOperand(0).getReg();
  bool DstIsDead = MI.getOperand(0).isDead();
  const MachineOperand &MO = MI.getOperand(1);
  unsigned MIFlags = MI.getFlags();

  LLVM_DEBUG(dbgs() << "Expanding: "; MI.dump());

  // Expand the mov into a sequence of mov/add+lsl of the individual bytes. We
  // want to avoid emitting any zero bytes, as they won't change the result, and
  // also don't want any pointless shifts, so instead of immediately emitting
  // the shift for a byte we keep track of how much we will need to shift and do
  // it before the next nonzero byte.
  unsigned PendingShift = 0;
  for (unsigned Byte = 0; Byte < 4; ++Byte) {
    unsigned Flag = Byte == 0   ? ARMII::MO_HI_8_15
                    : Byte == 1 ? ARMII::MO_HI_0_7
                    : Byte == 2 ? ARMII::MO_LO_8_15
                                : ARMII::MO_LO_0_7;
    MachineOperand Operand = getMovOperand(MO, Flag);
    bool ZeroImm = Operand.isImm() && Operand.getImm() == 0;
    unsigned Op = PendingShift ? ARM::tADDi8 : ARM::tMOVi8;

    // Emit the pending shift if we're going to emit this byte or if we've
    // reached the end.
    if (PendingShift && (!ZeroImm || Byte == 3)) {
      MachineInstr *Lsl =
          BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(ARM::tLSLri), DstReg)
              .add(t1CondCodeOp(true))
              .addReg(DstReg)
              .addImm(PendingShift)
              .add(predOps(ARMCC::AL))
              .setMIFlags(MIFlags);
      (void)Lsl;
      LLVM_DEBUG(dbgs() << "And:       "; Lsl->dump(););
      PendingShift = 0;
    }

    // Emit this byte if it's nonzero.
    if (!ZeroImm) {
      MachineInstrBuilder MIB =
          BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(Op), DstReg)
              .add(t1CondCodeOp(true));
      if (Op == ARM::tADDi8)
        MIB.addReg(DstReg);
      MIB.add(Operand);
      MIB.add(predOps(ARMCC::AL));
      MIB.setMIFlags(MIFlags);
      LLVM_DEBUG(dbgs() << (Op == ARM::tMOVi8 ? "To: " : "And:") << "       ";
                 MIB.getInstr()->dump(););
    }

    // Don't accumulate the shift value if we've not yet seen a nonzero byte.
    if (PendingShift || !ZeroImm)
      PendingShift += 8;
  }

  // The dest is dead on the last instruction we emitted if it was dead on the
  // original instruction.
  (--MBBI)->getOperand(0).setIsDead(DstIsDead);

  MI.eraseFromParent();
}

void ARMExpandPseudo::ExpandMOV32BitImm(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator &MBBI) {
  MachineInstr &MI = *MBBI;
  unsigned Opcode = MI.getOpcode();
  Register PredReg;
  ARMCC::CondCodes Pred = getInstrPredicate(MI, PredReg);
  Register DstReg = MI.getOperand(0).getReg();
  bool DstIsDead = MI.getOperand(0).isDead();
  bool isCC = Opcode == ARM::MOVCCi32imm || Opcode == ARM::t2MOVCCi32imm;
  const MachineOperand &MO = MI.getOperand(isCC ? 2 : 1);
  bool RequiresBundling = STI->isTargetWindows() && IsAnAddressOperand(MO);
  MachineInstrBuilder LO16, HI16;
  LLVM_DEBUG(dbgs() << "Expanding: "; MI.dump());

  if (!STI->hasV6T2Ops() &&
      (Opcode == ARM::MOVi32imm || Opcode == ARM::MOVCCi32imm)) {
    // FIXME Windows CE supports older ARM CPUs
    assert(!STI->isTargetWindows() && "Windows on ARM requires ARMv7+");

    assert (MO.isImm() && "MOVi32imm w/ non-immediate source operand!");
    unsigned ImmVal = (unsigned)MO.getImm();
    unsigned SOImmValV1 = 0, SOImmValV2 = 0;

    if (ARM_AM::isSOImmTwoPartVal(ImmVal)) { // Expand into a movi + orr.
      LO16 = BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(ARM::MOVi), DstReg);
      HI16 = BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(ARM::ORRri))
          .addReg(DstReg, RegState::Define | getDeadRegState(DstIsDead))
          .addReg(DstReg);
      SOImmValV1 = ARM_AM::getSOImmTwoPartFirst(ImmVal);
      SOImmValV2 = ARM_AM::getSOImmTwoPartSecond(ImmVal);
    } else { // Expand into a mvn + sub.
      LO16 = BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(ARM::MVNi), DstReg);
      HI16 = BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(ARM::SUBri))
          .addReg(DstReg, RegState::Define | getDeadRegState(DstIsDead))
          .addReg(DstReg);
      SOImmValV1 = ARM_AM::getSOImmTwoPartFirst(-ImmVal);
      SOImmValV2 = ARM_AM::getSOImmTwoPartSecond(-ImmVal);
      SOImmValV1 = ~(-SOImmValV1);
    }

    unsigned MIFlags = MI.getFlags();
    LO16 = LO16.addImm(SOImmValV1);
    HI16 = HI16.addImm(SOImmValV2);
    LO16.cloneMemRefs(MI);
    HI16.cloneMemRefs(MI);
    LO16.setMIFlags(MIFlags);
    HI16.setMIFlags(MIFlags);
    LO16.addImm(Pred).addReg(PredReg).add(condCodeOp());
    HI16.addImm(Pred).addReg(PredReg).add(condCodeOp());
    if (isCC)
      LO16.add(makeImplicit(MI.getOperand(1)));
    LO16.copyImplicitOps(MI);
    HI16.copyImplicitOps(MI);
    MI.eraseFromParent();
    return;
  }

  unsigned LO16Opc = 0;
  unsigned HI16Opc = 0;
  unsigned MIFlags = MI.getFlags();
  if (Opcode == ARM::t2MOVi32imm || Opcode == ARM::t2MOVCCi32imm) {
    LO16Opc = ARM::t2MOVi16;
    HI16Opc = ARM::t2MOVTi16;
  } else {
    LO16Opc = ARM::MOVi16;
    HI16Opc = ARM::MOVTi16;
  }

  LO16 = BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(LO16Opc), DstReg);
  LO16.setMIFlags(MIFlags);
  LO16.add(getMovOperand(MO, ARMII::MO_LO16));
  LO16.cloneMemRefs(MI);
  LO16.addImm(Pred).addReg(PredReg);
  if (isCC)
    LO16.add(makeImplicit(MI.getOperand(1)));
  LO16.copyImplicitOps(MI);
  LLVM_DEBUG(dbgs() << "To:        "; LO16.getInstr()->dump(););

  MachineOperand HIOperand = getMovOperand(MO, ARMII::MO_HI16);
  if (!(HIOperand.isImm() && HIOperand.getImm() == 0)) {
    HI16 = BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(HI16Opc))
               .addReg(DstReg, RegState::Define | getDeadRegState(DstIsDead))
               .addReg(DstReg);
    HI16.setMIFlags(MIFlags);
    HI16.add(HIOperand);
    HI16.cloneMemRefs(MI);
    HI16.addImm(Pred).addReg(PredReg);
    HI16.copyImplicitOps(MI);
    LLVM_DEBUG(dbgs() << "And:       "; HI16.getInstr()->dump(););
  } else {
    LO16->getOperand(0).setIsDead(DstIsDead);
  }

  if (RequiresBundling)
    finalizeBundle(MBB, LO16->getIterator(), MBBI->getIterator());

  MI.eraseFromParent();
}

// The size of the area, accessed by that VLSTM/VLLDM
// S0-S31 + FPSCR + 8 more bytes (VPR + pad, or just pad)
static const int CMSE_FP_SAVE_SIZE = 136;

static void determineGPRegsToClear(const MachineInstr &MI,
                                   const std::initializer_list<unsigned> &Regs,
                                   SmallVectorImpl<unsigned> &ClearRegs) {
  SmallVector<unsigned, 4> OpRegs;
  for (const MachineOperand &Op : MI.operands()) {
    if (!Op.isReg() || !Op.isUse())
      continue;
    OpRegs.push_back(Op.getReg());
  }
  llvm::sort(OpRegs);

  std::set_difference(Regs.begin(), Regs.end(), OpRegs.begin(), OpRegs.end(),
                      std::back_inserter(ClearRegs));
}

void ARMExpandPseudo::CMSEClearGPRegs(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    const DebugLoc &DL, const SmallVectorImpl<unsigned> &ClearRegs,
    unsigned ClobberReg) {

  if (STI->hasV8_1MMainlineOps()) {
    // Clear the registers using the CLRM instruction.
    MachineInstrBuilder CLRM =
        BuildMI(MBB, MBBI, DL, TII->get(ARM::t2CLRM)).add(predOps(ARMCC::AL));
    for (unsigned R : ClearRegs)
      CLRM.addReg(R, RegState::Define);
    CLRM.addReg(ARM::APSR, RegState::Define);
    CLRM.addReg(ARM::CPSR, RegState::Define | RegState::Implicit);
  } else {
    // Clear the registers and flags by copying ClobberReg into them.
    // (Baseline can't do a high register clear in one instruction).
    for (unsigned Reg : ClearRegs) {
      if (Reg == ClobberReg)
        continue;
      BuildMI(MBB, MBBI, DL, TII->get(ARM::tMOVr), Reg)
          .addReg(ClobberReg)
          .add(predOps(ARMCC::AL));
    }

    BuildMI(MBB, MBBI, DL, TII->get(ARM::t2MSR_M))
        .addImm(STI->hasDSP() ? 0xc00 : 0x800)
        .addReg(ClobberReg)
        .add(predOps(ARMCC::AL));
  }
}

// Find which FP registers need to be cleared.  The parameter `ClearRegs` is
// initialised with all elements set to true, and this function resets all the
// bits, which correspond to register uses. Returns true if any floating point
// register is defined, false otherwise.
static bool determineFPRegsToClear(const MachineInstr &MI,
                                   BitVector &ClearRegs) {
  bool DefFP = false;
  for (const MachineOperand &Op : MI.operands()) {
    if (!Op.isReg())
      continue;

    Register Reg = Op.getReg();
    if (Op.isDef()) {
      if ((Reg >= ARM::Q0 && Reg <= ARM::Q7) ||
          (Reg >= ARM::D0 && Reg <= ARM::D15) ||
          (Reg >= ARM::S0 && Reg <= ARM::S31))
        DefFP = true;
      continue;
    }

    if (Reg >= ARM::Q0 && Reg <= ARM::Q7) {
      int R = Reg - ARM::Q0;
      ClearRegs.reset(R * 4, (R + 1) * 4);
    } else if (Reg >= ARM::D0 && Reg <= ARM::D15) {
      int R = Reg - ARM::D0;
      ClearRegs.reset(R * 2, (R + 1) * 2);
    } else if (Reg >= ARM::S0 && Reg <= ARM::S31) {
      ClearRegs[Reg - ARM::S0] = false;
    }
  }
  return DefFP;
}

MachineBasicBlock &
ARMExpandPseudo::CMSEClearFPRegs(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MBBI) {
  BitVector ClearRegs(16, true);
  (void)determineFPRegsToClear(*MBBI, ClearRegs);

  if (STI->hasV8_1MMainlineOps())
    return CMSEClearFPRegsV81(MBB, MBBI, ClearRegs);
  else
    return CMSEClearFPRegsV8(MBB, MBBI, ClearRegs);
}

// Clear the FP registers for v8.0-M, by copying over the content
// of LR. Uses R12 as a scratch register.
MachineBasicBlock &
ARMExpandPseudo::CMSEClearFPRegsV8(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator MBBI,
                                   const BitVector &ClearRegs) {
  if (!STI->hasFPRegs())
    return MBB;

  auto &RetI = *MBBI;
  const DebugLoc &DL = RetI.getDebugLoc();

  // If optimising for minimum size, clear FP registers unconditionally.
  // Otherwise, check the CONTROL.SFPA (Secure Floating-Point Active) bit and
  // don't clear them if they belong to the non-secure state.
  MachineBasicBlock *ClearBB, *DoneBB;
  if (STI->hasMinSize()) {
    ClearBB = DoneBB = &MBB;
  } else {
    MachineFunction *MF = MBB.getParent();
    ClearBB = MF->CreateMachineBasicBlock(MBB.getBasicBlock());
    DoneBB = MF->CreateMachineBasicBlock(MBB.getBasicBlock());

    MF->insert(++MBB.getIterator(), ClearBB);
    MF->insert(++ClearBB->getIterator(), DoneBB);

    DoneBB->splice(DoneBB->end(), &MBB, MBBI, MBB.end());
    DoneBB->transferSuccessors(&MBB);
    MBB.addSuccessor(ClearBB);
    MBB.addSuccessor(DoneBB);
    ClearBB->addSuccessor(DoneBB);

    // At the new basic blocks we need to have live-in the registers, used
    // for the return value as well as LR, used to clear registers.
    for (const MachineOperand &Op : RetI.operands()) {
      if (!Op.isReg())
        continue;
      Register Reg = Op.getReg();
      if (Reg == ARM::NoRegister || Reg == ARM::LR)
        continue;
      assert(Reg.isPhysical() && "Unallocated register");
      ClearBB->addLiveIn(Reg);
      DoneBB->addLiveIn(Reg);
    }
    ClearBB->addLiveIn(ARM::LR);
    DoneBB->addLiveIn(ARM::LR);

    // Read the CONTROL register.
    BuildMI(MBB, MBB.end(), DL, TII->get(ARM::t2MRS_M), ARM::R12)
        .addImm(20)
        .add(predOps(ARMCC::AL));
    // Check bit 3 (SFPA).
    BuildMI(MBB, MBB.end(), DL, TII->get(ARM::t2TSTri))
        .addReg(ARM::R12)
        .addImm(8)
        .add(predOps(ARMCC::AL));
    // If SFPA is clear, jump over ClearBB to DoneBB.
    BuildMI(MBB, MBB.end(), DL, TII->get(ARM::tBcc))
        .addMBB(DoneBB)
        .addImm(ARMCC::EQ)
        .addReg(ARM::CPSR, RegState::Kill);
  }

  // Emit the clearing sequence
  for (unsigned D = 0; D < 8; D++) {
    // Attempt to clear as double
    if (ClearRegs[D * 2 + 0] && ClearRegs[D * 2 + 1]) {
      unsigned Reg = ARM::D0 + D;
      BuildMI(ClearBB, DL, TII->get(ARM::VMOVDRR), Reg)
          .addReg(ARM::LR)
          .addReg(ARM::LR)
          .add(predOps(ARMCC::AL));
    } else {
      // Clear first part as single
      if (ClearRegs[D * 2 + 0]) {
        unsigned Reg = ARM::S0 + D * 2;
        BuildMI(ClearBB, DL, TII->get(ARM::VMOVSR), Reg)
            .addReg(ARM::LR)
            .add(predOps(ARMCC::AL));
      }
      // Clear second part as single
      if (ClearRegs[D * 2 + 1]) {
        unsigned Reg = ARM::S0 + D * 2 + 1;
        BuildMI(ClearBB, DL, TII->get(ARM::VMOVSR), Reg)
            .addReg(ARM::LR)
            .add(predOps(ARMCC::AL));
      }
    }
  }

  // Clear FPSCR bits 0-4, 7, 28-31
  // The other bits are program global according to the AAPCS
  BuildMI(ClearBB, DL, TII->get(ARM::VMRS), ARM::R12)
      .add(predOps(ARMCC::AL));
  BuildMI(ClearBB, DL, TII->get(ARM::t2BICri), ARM::R12)
      .addReg(ARM::R12)
      .addImm(0x0000009F)
      .add(predOps(ARMCC::AL))
      .add(condCodeOp());
  BuildMI(ClearBB, DL, TII->get(ARM::t2BICri), ARM::R12)
      .addReg(ARM::R12)
      .addImm(0xF0000000)
      .add(predOps(ARMCC::AL))
      .add(condCodeOp());
  BuildMI(ClearBB, DL, TII->get(ARM::VMSR))
      .addReg(ARM::R12)
      .add(predOps(ARMCC::AL));

  return *DoneBB;
}

MachineBasicBlock &
ARMExpandPseudo::CMSEClearFPRegsV81(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator MBBI,
                                    const BitVector &ClearRegs) {
  auto &RetI = *MBBI;

  // Emit a sequence of VSCCLRM <sreglist> instructions, one instruction for
  // each contiguous sequence of S-registers.
  int Start = -1, End = -1;
  for (int S = 0, E = ClearRegs.size(); S != E; ++S) {
    if (ClearRegs[S] && S == End + 1) {
      End = S; // extend range
      continue;
    }
    // Emit current range.
    if (Start < End) {
      MachineInstrBuilder VSCCLRM =
          BuildMI(MBB, MBBI, RetI.getDebugLoc(), TII->get(ARM::VSCCLRMS))
              .add(predOps(ARMCC::AL));
      while (++Start <= End)
        VSCCLRM.addReg(ARM::S0 + Start, RegState::Define);
      VSCCLRM.addReg(ARM::VPR, RegState::Define);
    }
    Start = End = S;
  }
  // Emit last range.
  if (Start < End) {
    MachineInstrBuilder VSCCLRM =
        BuildMI(MBB, MBBI, RetI.getDebugLoc(), TII->get(ARM::VSCCLRMS))
            .add(predOps(ARMCC::AL));
    while (++Start <= End)
      VSCCLRM.addReg(ARM::S0 + Start, RegState::Define);
    VSCCLRM.addReg(ARM::VPR, RegState::Define);
  }

  return MBB;
}

void ARMExpandPseudo::CMSESaveClearFPRegs(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI, DebugLoc &DL,
    const LivePhysRegs &LiveRegs, SmallVectorImpl<unsigned> &ScratchRegs) {
  if (STI->hasV8_1MMainlineOps())
    CMSESaveClearFPRegsV81(MBB, MBBI, DL, LiveRegs);
  else if (STI->hasV8MMainlineOps())
    CMSESaveClearFPRegsV8(MBB, MBBI, DL, LiveRegs, ScratchRegs);
}

// Save and clear FP registers if present
void ARMExpandPseudo::CMSESaveClearFPRegsV8(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI, DebugLoc &DL,
    const LivePhysRegs &LiveRegs, SmallVectorImpl<unsigned> &ScratchRegs) {

  // Store an available register for FPSCR clearing
  assert(!ScratchRegs.empty());
  unsigned SpareReg = ScratchRegs.front();

  // save space on stack for VLSTM
  BuildMI(MBB, MBBI, DL, TII->get(ARM::tSUBspi), ARM::SP)
      .addReg(ARM::SP)
      .addImm(CMSE_FP_SAVE_SIZE >> 2)
      .add(predOps(ARMCC::AL));

  // Use ScratchRegs to store the fp regs
  std::vector<std::tuple<unsigned, unsigned, unsigned>> ClearedFPRegs;
  std::vector<unsigned> NonclearedFPRegs;
  for (const MachineOperand &Op : MBBI->operands()) {
    if (Op.isReg() && Op.isUse()) {
      Register Reg = Op.getReg();
      assert(!ARM::DPRRegClass.contains(Reg) ||
             ARM::DPR_VFP2RegClass.contains(Reg));
      assert(!ARM::QPRRegClass.contains(Reg));
      if (ARM::DPR_VFP2RegClass.contains(Reg)) {
        if (ScratchRegs.size() >= 2) {
          unsigned SaveReg2 = ScratchRegs.pop_back_val();
          unsigned SaveReg1 = ScratchRegs.pop_back_val();
          ClearedFPRegs.emplace_back(Reg, SaveReg1, SaveReg2);

          // Save the fp register to the normal registers
          BuildMI(MBB, MBBI, DL, TII->get(ARM::VMOVRRD))
              .addReg(SaveReg1, RegState::Define)
              .addReg(SaveReg2, RegState::Define)
              .addReg(Reg)
              .add(predOps(ARMCC::AL));
        } else {
          NonclearedFPRegs.push_back(Reg);
        }
      } else if (ARM::SPRRegClass.contains(Reg)) {
        if (ScratchRegs.size() >= 1) {
          unsigned SaveReg = ScratchRegs.pop_back_val();
          ClearedFPRegs.emplace_back(Reg, SaveReg, 0);

          // Save the fp register to the normal registers
          BuildMI(MBB, MBBI, DL, TII->get(ARM::VMOVRS), SaveReg)
              .addReg(Reg)
              .add(predOps(ARMCC::AL));
        } else {
          NonclearedFPRegs.push_back(Reg);
        }
      }
    }
  }

  bool passesFPReg = (!NonclearedFPRegs.empty() || !ClearedFPRegs.empty());

  if (passesFPReg)
    assert(STI->hasFPRegs() && "Subtarget needs fpregs");

  // Lazy store all fp registers to the stack.
  // This executes as NOP in the absence of floating-point support.
  MachineInstrBuilder VLSTM =
      BuildMI(MBB, MBBI, DL, TII->get(ARM::VLSTM))
          .addReg(ARM::SP)
          .add(predOps(ARMCC::AL))
          .addImm(0); // Represents a pseoudo register list, has no effect on
                      // the encoding.
  // Mark non-live registers as undef
  for (MachineOperand &MO : VLSTM->implicit_operands()) {
    if (MO.isReg() && !MO.isDef()) {
      Register Reg = MO.getReg();
      MO.setIsUndef(!LiveRegs.contains(Reg));
    }
  }

  // Restore all arguments
  for (const auto &Regs : ClearedFPRegs) {
    unsigned Reg, SaveReg1, SaveReg2;
    std::tie(Reg, SaveReg1, SaveReg2) = Regs;
    if (ARM::DPR_VFP2RegClass.contains(Reg))
      BuildMI(MBB, MBBI, DL, TII->get(ARM::VMOVDRR), Reg)
          .addReg(SaveReg1)
          .addReg(SaveReg2)
          .add(predOps(ARMCC::AL));
    else if (ARM::SPRRegClass.contains(Reg))
      BuildMI(MBB, MBBI, DL, TII->get(ARM::VMOVSR), Reg)
          .addReg(SaveReg1)
          .add(predOps(ARMCC::AL));
  }

  for (unsigned Reg : NonclearedFPRegs) {
    if (ARM::DPR_VFP2RegClass.contains(Reg)) {
      if (STI->isLittle()) {
        BuildMI(MBB, MBBI, DL, TII->get(ARM::VLDRD), Reg)
            .addReg(ARM::SP)
            .addImm((Reg - ARM::D0) * 2)
            .add(predOps(ARMCC::AL));
      } else {
        // For big-endian targets we need to load the two subregisters of Reg
        // manually because VLDRD would load them in wrong order
        unsigned SReg0 = TRI->getSubReg(Reg, ARM::ssub_0);
        BuildMI(MBB, MBBI, DL, TII->get(ARM::VLDRS), SReg0)
            .addReg(ARM::SP)
            .addImm((Reg - ARM::D0) * 2)
            .add(predOps(ARMCC::AL));
        BuildMI(MBB, MBBI, DL, TII->get(ARM::VLDRS), SReg0 + 1)
            .addReg(ARM::SP)
            .addImm((Reg - ARM::D0) * 2 + 1)
            .add(predOps(ARMCC::AL));
      }
    } else if (ARM::SPRRegClass.contains(Reg)) {
      BuildMI(MBB, MBBI, DL, TII->get(ARM::VLDRS), Reg)
          .addReg(ARM::SP)
          .addImm(Reg - ARM::S0)
          .add(predOps(ARMCC::AL));
    }
  }
  // restore FPSCR from stack and clear bits 0-4, 7, 28-31
  // The other bits are program global according to the AAPCS
  if (passesFPReg) {
    BuildMI(MBB, MBBI, DL, TII->get(ARM::tLDRspi), SpareReg)
        .addReg(ARM::SP)
        .addImm(0x10)
        .add(predOps(ARMCC::AL));
    BuildMI(MBB, MBBI, DL, TII->get(ARM::t2BICri), SpareReg)
        .addReg(SpareReg)
        .addImm(0x0000009F)
        .add(predOps(ARMCC::AL))
        .add(condCodeOp());
    BuildMI(MBB, MBBI, DL, TII->get(ARM::t2BICri), SpareReg)
        .addReg(SpareReg)
        .addImm(0xF0000000)
        .add(predOps(ARMCC::AL))
        .add(condCodeOp());
    BuildMI(MBB, MBBI, DL, TII->get(ARM::VMSR))
        .addReg(SpareReg)
        .add(predOps(ARMCC::AL));
    // The ldr must happen after a floating point instruction. To prevent the
    // post-ra scheduler to mess with the order, we create a bundle.
    finalizeBundle(MBB, VLSTM->getIterator(), MBBI->getIterator());
  }
}

void ARMExpandPseudo::CMSESaveClearFPRegsV81(MachineBasicBlock &MBB,
                                             MachineBasicBlock::iterator MBBI,
                                             DebugLoc &DL,
                                             const LivePhysRegs &LiveRegs) {
  BitVector ClearRegs(32, true);
  bool DefFP = determineFPRegsToClear(*MBBI, ClearRegs);

  // If the instruction does not write to a FP register and no elements were
  // removed from the set, then no FP registers were used to pass
  // arguments/returns.
  if (!DefFP && ClearRegs.count() == ClearRegs.size()) {
    // save space on stack for VLSTM
    BuildMI(MBB, MBBI, DL, TII->get(ARM::tSUBspi), ARM::SP)
        .addReg(ARM::SP)
        .addImm(CMSE_FP_SAVE_SIZE >> 2)
        .add(predOps(ARMCC::AL));

    // Lazy store all FP registers to the stack
    MachineInstrBuilder VLSTM =
        BuildMI(MBB, MBBI, DL, TII->get(ARM::VLSTM))
            .addReg(ARM::SP)
            .add(predOps(ARMCC::AL))
            .addImm(0); // Represents a pseoudo register list, has no effect on
                        // the encoding.
    // Mark non-live registers as undef
    for (MachineOperand &MO : VLSTM->implicit_operands()) {
      if (MO.isReg() && MO.isImplicit() && !MO.isDef()) {
        Register Reg = MO.getReg();
        MO.setIsUndef(!LiveRegs.contains(Reg));
      }
    }
  } else {
    // Push all the callee-saved registers (s16-s31).
    MachineInstrBuilder VPUSH =
        BuildMI(MBB, MBBI, DL, TII->get(ARM::VSTMSDB_UPD), ARM::SP)
            .addReg(ARM::SP)
            .add(predOps(ARMCC::AL));
    for (int Reg = ARM::S16; Reg <= ARM::S31; ++Reg)
      VPUSH.addReg(Reg);

    // Clear FP registers with a VSCCLRM.
    (void)CMSEClearFPRegsV81(MBB, MBBI, ClearRegs);

    // Save floating-point context.
    BuildMI(MBB, MBBI, DL, TII->get(ARM::VSTR_FPCXTS_pre), ARM::SP)
        .addReg(ARM::SP)
        .addImm(-8)
        .add(predOps(ARMCC::AL));
  }
}

// Restore FP registers if present
void ARMExpandPseudo::CMSERestoreFPRegs(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI, DebugLoc &DL,
    SmallVectorImpl<unsigned> &AvailableRegs) {
  if (STI->hasV8_1MMainlineOps())
    CMSERestoreFPRegsV81(MBB, MBBI, DL, AvailableRegs);
  else if (STI->hasV8MMainlineOps())
    CMSERestoreFPRegsV8(MBB, MBBI, DL, AvailableRegs);
}

void ARMExpandPseudo::CMSERestoreFPRegsV8(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI, DebugLoc &DL,
    SmallVectorImpl<unsigned> &AvailableRegs) {

  // Keep a scratch register for the mitigation sequence.
  unsigned ScratchReg = ARM::NoRegister;
  if (STI->fixCMSE_CVE_2021_35465())
    ScratchReg = AvailableRegs.pop_back_val();

  // Use AvailableRegs to store the fp regs
  std::vector<std::tuple<unsigned, unsigned, unsigned>> ClearedFPRegs;
  std::vector<unsigned> NonclearedFPRegs;
  for (const MachineOperand &Op : MBBI->operands()) {
    if (Op.isReg() && Op.isDef()) {
      Register Reg = Op.getReg();
      assert(!ARM::DPRRegClass.contains(Reg) ||
             ARM::DPR_VFP2RegClass.contains(Reg));
      assert(!ARM::QPRRegClass.contains(Reg));
      if (ARM::DPR_VFP2RegClass.contains(Reg)) {
        if (AvailableRegs.size() >= 2) {
          unsigned SaveReg2 = AvailableRegs.pop_back_val();
          unsigned SaveReg1 = AvailableRegs.pop_back_val();
          ClearedFPRegs.emplace_back(Reg, SaveReg1, SaveReg2);

          // Save the fp register to the normal registers
          BuildMI(MBB, MBBI, DL, TII->get(ARM::VMOVRRD))
              .addReg(SaveReg1, RegState::Define)
              .addReg(SaveReg2, RegState::Define)
              .addReg(Reg)
              .add(predOps(ARMCC::AL));
        } else {
          NonclearedFPRegs.push_back(Reg);
        }
      } else if (ARM::SPRRegClass.contains(Reg)) {
        if (AvailableRegs.size() >= 1) {
          unsigned SaveReg = AvailableRegs.pop_back_val();
          ClearedFPRegs.emplace_back(Reg, SaveReg, 0);

          // Save the fp register to the normal registers
          BuildMI(MBB, MBBI, DL, TII->get(ARM::VMOVRS), SaveReg)
              .addReg(Reg)
              .add(predOps(ARMCC::AL));
        } else {
          NonclearedFPRegs.push_back(Reg);
        }
      }
    }
  }

  bool returnsFPReg = (!NonclearedFPRegs.empty() || !ClearedFPRegs.empty());

  if (returnsFPReg)
    assert(STI->hasFPRegs() && "Subtarget needs fpregs");

  // Push FP regs that cannot be restored via normal registers on the stack
  for (unsigned Reg : NonclearedFPRegs) {
    if (ARM::DPR_VFP2RegClass.contains(Reg))
      BuildMI(MBB, MBBI, DL, TII->get(ARM::VSTRD))
          .addReg(Reg)
          .addReg(ARM::SP)
          .addImm((Reg - ARM::D0) * 2)
          .add(predOps(ARMCC::AL));
    else if (ARM::SPRRegClass.contains(Reg))
      BuildMI(MBB, MBBI, DL, TII->get(ARM::VSTRS))
          .addReg(Reg)
          .addReg(ARM::SP)
          .addImm(Reg - ARM::S0)
          .add(predOps(ARMCC::AL));
  }

  // Lazy load fp regs from stack.
  // This executes as NOP in the absence of floating-point support.
  MachineInstrBuilder VLLDM =
      BuildMI(MBB, MBBI, DL, TII->get(ARM::VLLDM))
          .addReg(ARM::SP)
          .add(predOps(ARMCC::AL))
          .addImm(0); // Represents a pseoudo register list, has no effect on
                      // the encoding.

  if (STI->fixCMSE_CVE_2021_35465()) {
    auto Bundler = MIBundleBuilder(MBB, VLLDM);
    // Read the CONTROL register.
    Bundler.append(BuildMI(*MBB.getParent(), DL, TII->get(ARM::t2MRS_M))
                       .addReg(ScratchReg, RegState::Define)
                       .addImm(20)
                       .add(predOps(ARMCC::AL)));
    // Check bit 3 (SFPA).
    Bundler.append(BuildMI(*MBB.getParent(), DL, TII->get(ARM::t2TSTri))
                       .addReg(ScratchReg)
                       .addImm(8)
                       .add(predOps(ARMCC::AL)));
    // Emit the IT block.
    Bundler.append(BuildMI(*MBB.getParent(), DL, TII->get(ARM::t2IT))
                       .addImm(ARMCC::NE)
                       .addImm(8));
    // If SFPA is clear jump over to VLLDM, otherwise execute an instruction
    // which has no functional effect apart from causing context creation:
    // vmovne s0, s0. In the absence of FPU we emit .inst.w 0xeeb00a40,
    // which is defined as NOP if not executed.
    if (STI->hasFPRegs())
      Bundler.append(BuildMI(*MBB.getParent(), DL, TII->get(ARM::VMOVS))
                         .addReg(ARM::S0, RegState::Define)
                         .addReg(ARM::S0, RegState::Undef)
                         .add(predOps(ARMCC::NE)));
    else
      Bundler.append(BuildMI(*MBB.getParent(), DL, TII->get(ARM::INLINEASM))
                         .addExternalSymbol(".inst.w 0xeeb00a40")
                         .addImm(InlineAsm::Extra_HasSideEffects));
    finalizeBundle(MBB, Bundler.begin(), Bundler.end());
  }

  // Restore all FP registers via normal registers
  for (const auto &Regs : ClearedFPRegs) {
    unsigned Reg, SaveReg1, SaveReg2;
    std::tie(Reg, SaveReg1, SaveReg2) = Regs;
    if (ARM::DPR_VFP2RegClass.contains(Reg))
      BuildMI(MBB, MBBI, DL, TII->get(ARM::VMOVDRR), Reg)
          .addReg(SaveReg1)
          .addReg(SaveReg2)
          .add(predOps(ARMCC::AL));
    else if (ARM::SPRRegClass.contains(Reg))
      BuildMI(MBB, MBBI, DL, TII->get(ARM::VMOVSR), Reg)
          .addReg(SaveReg1)
          .add(predOps(ARMCC::AL));
  }

  // Pop the stack space
  BuildMI(MBB, MBBI, DL, TII->get(ARM::tADDspi), ARM::SP)
      .addReg(ARM::SP)
      .addImm(CMSE_FP_SAVE_SIZE >> 2)
      .add(predOps(ARMCC::AL));
}

static bool definesOrUsesFPReg(const MachineInstr &MI) {
  for (const MachineOperand &Op : MI.operands()) {
    if (!Op.isReg())
      continue;
    Register Reg = Op.getReg();
    if ((Reg >= ARM::Q0 && Reg <= ARM::Q7) ||
        (Reg >= ARM::D0 && Reg <= ARM::D15) ||
        (Reg >= ARM::S0 && Reg <= ARM::S31))
      return true;
  }
  return false;
}

void ARMExpandPseudo::CMSERestoreFPRegsV81(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI, DebugLoc &DL,
    SmallVectorImpl<unsigned> &AvailableRegs) {
  if (!definesOrUsesFPReg(*MBBI)) {
    if (STI->fixCMSE_CVE_2021_35465()) {
      BuildMI(MBB, MBBI, DL, TII->get(ARM::VSCCLRMS))
          .add(predOps(ARMCC::AL))
          .addReg(ARM::VPR, RegState::Define);
    }

    // Load FP registers from stack.
    BuildMI(MBB, MBBI, DL, TII->get(ARM::VLLDM))
        .addReg(ARM::SP)
        .add(predOps(ARMCC::AL))
        .addImm(0); // Represents a pseoudo register list, has no effect on the
                    // encoding.

    // Pop the stack space
    BuildMI(MBB, MBBI, DL, TII->get(ARM::tADDspi), ARM::SP)
        .addReg(ARM::SP)
        .addImm(CMSE_FP_SAVE_SIZE >> 2)
        .add(predOps(ARMCC::AL));
  } else {
    // Restore the floating point context.
    BuildMI(MBB, MBBI, MBBI->getDebugLoc(), TII->get(ARM::VLDR_FPCXTS_post),
            ARM::SP)
        .addReg(ARM::SP)
        .addImm(8)
        .add(predOps(ARMCC::AL));

    // Pop all the callee-saved registers (s16-s31).
    MachineInstrBuilder VPOP =
        BuildMI(MBB, MBBI, DL, TII->get(ARM::VLDMSIA_UPD), ARM::SP)
            .addReg(ARM::SP)
            .add(predOps(ARMCC::AL));
    for (int Reg = ARM::S16; Reg <= ARM::S31; ++Reg)
      VPOP.addReg(Reg, RegState::Define);
  }
}

/// Expand a CMP_SWAP pseudo-inst to an ldrex/strex loop as simply as
/// possible. This only gets used at -O0 so we don't care about efficiency of
/// the generated code.
bool ARMExpandPseudo::ExpandCMP_SWAP(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator MBBI,
                                     unsigned LdrexOp, unsigned StrexOp,
                                     unsigned UxtOp,
                                     MachineBasicBlock::iterator &NextMBBI) {
  bool IsThumb = STI->isThumb();
  bool IsThumb1Only = STI->isThumb1Only();
  MachineInstr &MI = *MBBI;
  DebugLoc DL = MI.getDebugLoc();
  const MachineOperand &Dest = MI.getOperand(0);
  Register TempReg = MI.getOperand(1).getReg();
  // Duplicating undef operands into 2 instructions does not guarantee the same
  // value on both; However undef should be replaced by xzr anyway.
  assert(!MI.getOperand(2).isUndef() && "cannot handle undef");
  Register AddrReg = MI.getOperand(2).getReg();
  Register DesiredReg = MI.getOperand(3).getReg();
  Register NewReg = MI.getOperand(4).getReg();

  if (IsThumb) {
    assert(STI->hasV8MBaselineOps() &&
           "CMP_SWAP not expected to be custom expanded for Thumb1");
    assert((UxtOp == 0 || UxtOp == ARM::tUXTB || UxtOp == ARM::tUXTH) &&
           "ARMv8-M.baseline does not have t2UXTB/t2UXTH");
    assert((UxtOp == 0 || ARM::tGPRRegClass.contains(DesiredReg)) &&
           "DesiredReg used for UXT op must be tGPR");
  }

  MachineFunction *MF = MBB.getParent();
  auto LoadCmpBB = MF->CreateMachineBasicBlock(MBB.getBasicBlock());
  auto StoreBB = MF->CreateMachineBasicBlock(MBB.getBasicBlock());
  auto DoneBB = MF->CreateMachineBasicBlock(MBB.getBasicBlock());

  MF->insert(++MBB.getIterator(), LoadCmpBB);
  MF->insert(++LoadCmpBB->getIterator(), StoreBB);
  MF->insert(++StoreBB->getIterator(), DoneBB);

  if (UxtOp) {
    MachineInstrBuilder MIB =
        BuildMI(MBB, MBBI, DL, TII->get(UxtOp), DesiredReg)
            .addReg(DesiredReg, RegState::Kill);
    if (!IsThumb)
      MIB.addImm(0);
    MIB.add(predOps(ARMCC::AL));
  }

  // .Lloadcmp:
  //     ldrex rDest, [rAddr]
  //     cmp rDest, rDesired
  //     bne .Ldone

  MachineInstrBuilder MIB;
  MIB = BuildMI(LoadCmpBB, DL, TII->get(LdrexOp), Dest.getReg());
  MIB.addReg(AddrReg);
  if (LdrexOp == ARM::t2LDREX)
    MIB.addImm(0); // a 32-bit Thumb ldrex (only) allows an offset.
  MIB.add(predOps(ARMCC::AL));

  unsigned CMPrr = IsThumb ? ARM::tCMPhir : ARM::CMPrr;
  BuildMI(LoadCmpBB, DL, TII->get(CMPrr))
      .addReg(Dest.getReg(), getKillRegState(Dest.isDead()))
      .addReg(DesiredReg)
      .add(predOps(ARMCC::AL));
  unsigned Bcc = IsThumb ? ARM::tBcc : ARM::Bcc;
  BuildMI(LoadCmpBB, DL, TII->get(Bcc))
      .addMBB(DoneBB)
      .addImm(ARMCC::NE)
      .addReg(ARM::CPSR, RegState::Kill);
  LoadCmpBB->addSuccessor(DoneBB);
  LoadCmpBB->addSuccessor(StoreBB);

  // .Lstore:
  //     strex rTempReg, rNew, [rAddr]
  //     cmp rTempReg, #0
  //     bne .Lloadcmp
  MIB = BuildMI(StoreBB, DL, TII->get(StrexOp), TempReg)
    .addReg(NewReg)
    .addReg(AddrReg);
  if (StrexOp == ARM::t2STREX)
    MIB.addImm(0); // a 32-bit Thumb strex (only) allows an offset.
  MIB.add(predOps(ARMCC::AL));

  unsigned CMPri =
      IsThumb ? (IsThumb1Only ? ARM::tCMPi8 : ARM::t2CMPri) : ARM::CMPri;
  BuildMI(StoreBB, DL, TII->get(CMPri))
      .addReg(TempReg, RegState::Kill)
      .addImm(0)
      .add(predOps(ARMCC::AL));
  BuildMI(StoreBB, DL, TII->get(Bcc))
      .addMBB(LoadCmpBB)
      .addImm(ARMCC::NE)
      .addReg(ARM::CPSR, RegState::Kill);
  StoreBB->addSuccessor(LoadCmpBB);
  StoreBB->addSuccessor(DoneBB);

  DoneBB->splice(DoneBB->end(), &MBB, MI, MBB.end());
  DoneBB->transferSuccessors(&MBB);

  MBB.addSuccessor(LoadCmpBB);

  NextMBBI = MBB.end();
  MI.eraseFromParent();

  // Recompute livein lists.
  LivePhysRegs LiveRegs;
  computeAndAddLiveIns(LiveRegs, *DoneBB);
  computeAndAddLiveIns(LiveRegs, *StoreBB);
  computeAndAddLiveIns(LiveRegs, *LoadCmpBB);
  // Do an extra pass around the loop to get loop carried registers right.
  StoreBB->clearLiveIns();
  computeAndAddLiveIns(LiveRegs, *StoreBB);
  LoadCmpBB->clearLiveIns();
  computeAndAddLiveIns(LiveRegs, *LoadCmpBB);

  return true;
}

/// ARM's ldrexd/strexd take a consecutive register pair (represented as a
/// single GPRPair register), Thumb's take two separate registers so we need to
/// extract the subregs from the pair.
static void addExclusiveRegPair(MachineInstrBuilder &MIB, MachineOperand &Reg,
                                unsigned Flags, bool IsThumb,
                                const TargetRegisterInfo *TRI) {
  if (IsThumb) {
    Register RegLo = TRI->getSubReg(Reg.getReg(), ARM::gsub_0);
    Register RegHi = TRI->getSubReg(Reg.getReg(), ARM::gsub_1);
    MIB.addReg(RegLo, Flags);
    MIB.addReg(RegHi, Flags);
  } else
    MIB.addReg(Reg.getReg(), Flags);
}

/// Expand a 64-bit CMP_SWAP to an ldrexd/strexd loop.
bool ARMExpandPseudo::ExpandCMP_SWAP_64(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator MBBI,
                                        MachineBasicBlock::iterator &NextMBBI) {
  bool IsThumb = STI->isThumb();
  assert(!STI->isThumb1Only() && "CMP_SWAP_64 unsupported under Thumb1!");
  MachineInstr &MI = *MBBI;
  DebugLoc DL = MI.getDebugLoc();
  MachineOperand &Dest = MI.getOperand(0);
  Register TempReg = MI.getOperand(1).getReg();
  // Duplicating undef operands into 2 instructions does not guarantee the same
  // value on both; However undef should be replaced by xzr anyway.
  assert(!MI.getOperand(2).isUndef() && "cannot handle undef");
  Register AddrReg = MI.getOperand(2).getReg();
  Register DesiredReg = MI.getOperand(3).getReg();
  MachineOperand New = MI.getOperand(4);
  New.setIsKill(false);

  Register DestLo = TRI->getSubReg(Dest.getReg(), ARM::gsub_0);
  Register DestHi = TRI->getSubReg(Dest.getReg(), ARM::gsub_1);
  Register DesiredLo = TRI->getSubReg(DesiredReg, ARM::gsub_0);
  Register DesiredHi = TRI->getSubReg(DesiredReg, ARM::gsub_1);

  MachineFunction *MF = MBB.getParent();
  auto LoadCmpBB = MF->CreateMachineBasicBlock(MBB.getBasicBlock());
  auto StoreBB = MF->CreateMachineBasicBlock(MBB.getBasicBlock());
  auto DoneBB = MF->CreateMachineBasicBlock(MBB.getBasicBlock());

  MF->insert(++MBB.getIterator(), LoadCmpBB);
  MF->insert(++LoadCmpBB->getIterator(), StoreBB);
  MF->insert(++StoreBB->getIterator(), DoneBB);

  // .Lloadcmp:
  //     ldrexd rDestLo, rDestHi, [rAddr]
  //     cmp rDestLo, rDesiredLo
  //     sbcs dead rTempReg, rDestHi, rDesiredHi
  //     bne .Ldone
  unsigned LDREXD = IsThumb ? ARM::t2LDREXD : ARM::LDREXD;
  MachineInstrBuilder MIB;
  MIB = BuildMI(LoadCmpBB, DL, TII->get(LDREXD));
  addExclusiveRegPair(MIB, Dest, RegState::Define, IsThumb, TRI);
  MIB.addReg(AddrReg).add(predOps(ARMCC::AL));

  unsigned CMPrr = IsThumb ? ARM::tCMPhir : ARM::CMPrr;
  BuildMI(LoadCmpBB, DL, TII->get(CMPrr))
      .addReg(DestLo, getKillRegState(Dest.isDead()))
      .addReg(DesiredLo)
      .add(predOps(ARMCC::AL));

  BuildMI(LoadCmpBB, DL, TII->get(CMPrr))
      .addReg(DestHi, getKillRegState(Dest.isDead()))
      .addReg(DesiredHi)
      .addImm(ARMCC::EQ).addReg(ARM::CPSR, RegState::Kill);

  unsigned Bcc = IsThumb ? ARM::tBcc : ARM::Bcc;
  BuildMI(LoadCmpBB, DL, TII->get(Bcc))
      .addMBB(DoneBB)
      .addImm(ARMCC::NE)
      .addReg(ARM::CPSR, RegState::Kill);
  LoadCmpBB->addSuccessor(DoneBB);
  LoadCmpBB->addSuccessor(StoreBB);

  // .Lstore:
  //     strexd rTempReg, rNewLo, rNewHi, [rAddr]
  //     cmp rTempReg, #0
  //     bne .Lloadcmp
  unsigned STREXD = IsThumb ? ARM::t2STREXD : ARM::STREXD;
  MIB = BuildMI(StoreBB, DL, TII->get(STREXD), TempReg);
  unsigned Flags = getKillRegState(New.isDead());
  addExclusiveRegPair(MIB, New, Flags, IsThumb, TRI);
  MIB.addReg(AddrReg).add(predOps(ARMCC::AL));

  unsigned CMPri = IsThumb ? ARM::t2CMPri : ARM::CMPri;
  BuildMI(StoreBB, DL, TII->get(CMPri))
      .addReg(TempReg, RegState::Kill)
      .addImm(0)
      .add(predOps(ARMCC::AL));
  BuildMI(StoreBB, DL, TII->get(Bcc))
      .addMBB(LoadCmpBB)
      .addImm(ARMCC::NE)
      .addReg(ARM::CPSR, RegState::Kill);
  StoreBB->addSuccessor(LoadCmpBB);
  StoreBB->addSuccessor(DoneBB);

  DoneBB->splice(DoneBB->end(), &MBB, MI, MBB.end());
  DoneBB->transferSuccessors(&MBB);

  MBB.addSuccessor(LoadCmpBB);

  NextMBBI = MBB.end();
  MI.eraseFromParent();

  // Recompute livein lists.
  LivePhysRegs LiveRegs;
  computeAndAddLiveIns(LiveRegs, *DoneBB);
  computeAndAddLiveIns(LiveRegs, *StoreBB);
  computeAndAddLiveIns(LiveRegs, *LoadCmpBB);
  // Do an extra pass around the loop to get loop carried registers right.
  StoreBB->clearLiveIns();
  computeAndAddLiveIns(LiveRegs, *StoreBB);
  LoadCmpBB->clearLiveIns();
  computeAndAddLiveIns(LiveRegs, *LoadCmpBB);

  return true;
}

static void CMSEPushCalleeSaves(const TargetInstrInfo &TII,
                                MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MBBI, int JumpReg,
                                const LivePhysRegs &LiveRegs, bool Thumb1Only) {
  const DebugLoc &DL = MBBI->getDebugLoc();
  if (Thumb1Only) { // push Lo and Hi regs separately
    MachineInstrBuilder PushMIB =
        BuildMI(MBB, MBBI, DL, TII.get(ARM::tPUSH)).add(predOps(ARMCC::AL));
    for (int Reg = ARM::R4; Reg < ARM::R8; ++Reg) {
      PushMIB.addReg(
          Reg, Reg == JumpReg || LiveRegs.contains(Reg) ? 0 : RegState::Undef);
    }

    // Thumb1 can only tPUSH low regs, so we copy the high regs to the low
    // regs that we just saved and push the low regs again, taking care to
    // not clobber JumpReg. If JumpReg is one of the low registers, push first
    // the values of r9-r11, and then r8. That would leave them ordered in
    // memory, and allow us to later pop them with a single instructions.
    // FIXME: Could also use any of r0-r3 that are free (including in the
    // first PUSH above).
    for (int LoReg = ARM::R7, HiReg = ARM::R11; LoReg >= ARM::R4; --LoReg) {
      if (JumpReg == LoReg)
        continue;
      BuildMI(MBB, MBBI, DL, TII.get(ARM::tMOVr), LoReg)
          .addReg(HiReg, LiveRegs.contains(HiReg) ? 0 : RegState::Undef)
          .add(predOps(ARMCC::AL));
      --HiReg;
    }
    MachineInstrBuilder PushMIB2 =
        BuildMI(MBB, MBBI, DL, TII.get(ARM::tPUSH)).add(predOps(ARMCC::AL));
    for (int Reg = ARM::R4; Reg < ARM::R8; ++Reg) {
      if (Reg == JumpReg)
        continue;
      PushMIB2.addReg(Reg, RegState::Kill);
    }

    // If we couldn't use a low register for temporary storage (because it was
    // the JumpReg), use r4 or r5, whichever is not JumpReg. It has already been
    // saved.
    if (JumpReg >= ARM::R4 && JumpReg <= ARM::R7) {
      int LoReg = JumpReg == ARM::R4 ? ARM::R5 : ARM::R4;
      BuildMI(MBB, MBBI, DL, TII.get(ARM::tMOVr), LoReg)
          .addReg(ARM::R8, LiveRegs.contains(ARM::R8) ? 0 : RegState::Undef)
          .add(predOps(ARMCC::AL));
      BuildMI(MBB, MBBI, DL, TII.get(ARM::tPUSH))
          .add(predOps(ARMCC::AL))
          .addReg(LoReg, RegState::Kill);
    }
  } else { // push Lo and Hi registers with a single instruction
    MachineInstrBuilder PushMIB =
        BuildMI(MBB, MBBI, DL, TII.get(ARM::t2STMDB_UPD), ARM::SP)
            .addReg(ARM::SP)
            .add(predOps(ARMCC::AL));
    for (int Reg = ARM::R4; Reg < ARM::R12; ++Reg) {
      PushMIB.addReg(
          Reg, Reg == JumpReg || LiveRegs.contains(Reg) ? 0 : RegState::Undef);
    }
  }
}

static void CMSEPopCalleeSaves(const TargetInstrInfo &TII,
                               MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator MBBI, int JumpReg,
                               bool Thumb1Only) {
  const DebugLoc &DL = MBBI->getDebugLoc();
  if (Thumb1Only) {
    MachineInstrBuilder PopMIB =
        BuildMI(MBB, MBBI, DL, TII.get(ARM::tPOP)).add(predOps(ARMCC::AL));
    for (int R = 0; R < 4; ++R) {
      PopMIB.addReg(ARM::R4 + R, RegState::Define);
      BuildMI(MBB, MBBI, DL, TII.get(ARM::tMOVr), ARM::R8 + R)
          .addReg(ARM::R4 + R, RegState::Kill)
          .add(predOps(ARMCC::AL));
    }
    MachineInstrBuilder PopMIB2 =
        BuildMI(MBB, MBBI, DL, TII.get(ARM::tPOP)).add(predOps(ARMCC::AL));
    for (int R = 0; R < 4; ++R)
      PopMIB2.addReg(ARM::R4 + R, RegState::Define);
  } else { // pop Lo and Hi registers with a single instruction
    MachineInstrBuilder PopMIB =
        BuildMI(MBB, MBBI, DL, TII.get(ARM::t2LDMIA_UPD), ARM::SP)
            .addReg(ARM::SP)
            .add(predOps(ARMCC::AL));
    for (int Reg = ARM::R4; Reg < ARM::R12; ++Reg)
      PopMIB.addReg(Reg, RegState::Define);
  }
}

bool ARMExpandPseudo::ExpandMI(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator MBBI,
                               MachineBasicBlock::iterator &NextMBBI) {
  MachineInstr &MI = *MBBI;
  unsigned Opcode = MI.getOpcode();
  switch (Opcode) {
    default:
      return false;

    case ARM::VBSPd:
    case ARM::VBSPq: {
      Register DstReg = MI.getOperand(0).getReg();
      if (DstReg == MI.getOperand(3).getReg()) {
        // Expand to VBIT
        unsigned NewOpc = Opcode == ARM::VBSPd ? ARM::VBITd : ARM::VBITq;
        BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(NewOpc))
            .add(MI.getOperand(0))
            .add(MI.getOperand(3))
            .add(MI.getOperand(2))
            .add(MI.getOperand(1))
            .addImm(MI.getOperand(4).getImm())
            .add(MI.getOperand(5));
      } else if (DstReg == MI.getOperand(2).getReg()) {
        // Expand to VBIF
        unsigned NewOpc = Opcode == ARM::VBSPd ? ARM::VBIFd : ARM::VBIFq;
        BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(NewOpc))
            .add(MI.getOperand(0))
            .add(MI.getOperand(2))
            .add(MI.getOperand(3))
            .add(MI.getOperand(1))
            .addImm(MI.getOperand(4).getImm())
            .add(MI.getOperand(5));
      } else {
        // Expand to VBSL
        unsigned NewOpc = Opcode == ARM::VBSPd ? ARM::VBSLd : ARM::VBSLq;
        if (DstReg == MI.getOperand(1).getReg()) {
          BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(NewOpc))
              .add(MI.getOperand(0))
              .add(MI.getOperand(1))
              .add(MI.getOperand(2))
              .add(MI.getOperand(3))
              .addImm(MI.getOperand(4).getImm())
              .add(MI.getOperand(5));
        } else {
          // Use move to satisfy constraints
          unsigned MoveOpc = Opcode == ARM::VBSPd ? ARM::VORRd : ARM::VORRq;
          BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(MoveOpc))
              .addReg(DstReg,
                      RegState::Define |
                          getRenamableRegState(MI.getOperand(0).isRenamable()))
              .add(MI.getOperand(1))
              .add(MI.getOperand(1))
              .addImm(MI.getOperand(4).getImm())
              .add(MI.getOperand(5));
          BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(NewOpc))
              .add(MI.getOperand(0))
              .addReg(DstReg,
                      RegState::Kill |
                          getRenamableRegState(MI.getOperand(0).isRenamable()))
              .add(MI.getOperand(2))
              .add(MI.getOperand(3))
              .addImm(MI.getOperand(4).getImm())
              .add(MI.getOperand(5));
        }
      }
      MI.eraseFromParent();
      return true;
    }

    case ARM::TCRETURNdi:
    case ARM::TCRETURNri:
    case ARM::TCRETURNrinotr12: {
      MachineBasicBlock::iterator MBBI = MBB.getLastNonDebugInstr();
      if (MBBI->getOpcode() == ARM::SEH_EpilogEnd)
        MBBI--;
      if (MBBI->getOpcode() == ARM::SEH_Nop_Ret)
        MBBI--;
      assert(MBBI->isReturn() &&
             "Can only insert epilog into returning blocks");
      unsigned RetOpcode = MBBI->getOpcode();
      DebugLoc dl = MBBI->getDebugLoc();
      const ARMBaseInstrInfo &TII = *static_cast<const ARMBaseInstrInfo *>(
          MBB.getParent()->getSubtarget().getInstrInfo());

      // Tail call return: adjust the stack pointer and jump to callee.
      MBBI = MBB.getLastNonDebugInstr();
      if (MBBI->getOpcode() == ARM::SEH_EpilogEnd)
        MBBI--;
      if (MBBI->getOpcode() == ARM::SEH_Nop_Ret)
        MBBI--;
      MachineOperand &JumpTarget = MBBI->getOperand(0);

      // Jump to label or value in register.
      if (RetOpcode == ARM::TCRETURNdi) {
        MachineFunction *MF = MBB.getParent();
        bool NeedsWinCFI = MF->getTarget().getMCAsmInfo()->usesWindowsCFI() &&
                           MF->getFunction().needsUnwindTableEntry();
        unsigned TCOpcode =
            STI->isThumb()
                ? ((STI->isTargetMachO() || NeedsWinCFI) ? ARM::tTAILJMPd
                                                         : ARM::tTAILJMPdND)
                : ARM::TAILJMPd;
        MachineInstrBuilder MIB = BuildMI(MBB, MBBI, dl, TII.get(TCOpcode));
        if (JumpTarget.isGlobal())
          MIB.addGlobalAddress(JumpTarget.getGlobal(), JumpTarget.getOffset(),
                               JumpTarget.getTargetFlags());
        else {
          assert(JumpTarget.isSymbol());
          MIB.addExternalSymbol(JumpTarget.getSymbolName(),
                                JumpTarget.getTargetFlags());
        }

        // Add the default predicate in Thumb mode.
        if (STI->isThumb())
          MIB.add(predOps(ARMCC::AL));
      } else if (RetOpcode == ARM::TCRETURNri ||
                 RetOpcode == ARM::TCRETURNrinotr12) {
        unsigned Opcode =
          STI->isThumb() ? ARM::tTAILJMPr
                         : (STI->hasV4TOps() ? ARM::TAILJMPr : ARM::TAILJMPr4);
        BuildMI(MBB, MBBI, dl,
                TII.get(Opcode))
            .addReg(JumpTarget.getReg(), RegState::Kill);
      }

      auto NewMI = std::prev(MBBI);
      for (unsigned i = 2, e = MBBI->getNumOperands(); i != e; ++i)
        NewMI->addOperand(MBBI->getOperand(i));


      // Update call site info and delete the pseudo instruction TCRETURN.
      if (MI.isCandidateForCallSiteEntry())
        MI.getMF()->moveCallSiteInfo(&MI, &*NewMI);
      // Copy nomerge flag over to new instruction.
      if (MI.getFlag(MachineInstr::NoMerge))
        NewMI->setFlag(MachineInstr::NoMerge);
      MBB.erase(MBBI);

      MBBI = NewMI;
      return true;
    }
    case ARM::tBXNS_RET: {
      // For v8.0-M.Main we need to authenticate LR before clearing FPRs, which
      // uses R12 as a scratch register.
      if (!STI->hasV8_1MMainlineOps() && AFI->shouldSignReturnAddress())
        BuildMI(MBB, MBBI, DebugLoc(), TII->get(ARM::t2AUT));

      MachineBasicBlock &AfterBB = CMSEClearFPRegs(MBB, MBBI);

      if (STI->hasV8_1MMainlineOps()) {
        // Restore the non-secure floating point context.
        BuildMI(MBB, MBBI, MBBI->getDebugLoc(),
                TII->get(ARM::VLDR_FPCXTNS_post), ARM::SP)
            .addReg(ARM::SP)
            .addImm(4)
            .add(predOps(ARMCC::AL));

        if (AFI->shouldSignReturnAddress())
          BuildMI(AfterBB, AfterBB.end(), DebugLoc(), TII->get(ARM::t2AUT));
      }

      // Clear all GPR that are not a use of the return instruction.
      assert(llvm::all_of(MBBI->operands(), [](const MachineOperand &Op) {
        return !Op.isReg() || Op.getReg() != ARM::R12;
      }));
      SmallVector<unsigned, 5> ClearRegs;
      determineGPRegsToClear(
          *MBBI, {ARM::R0, ARM::R1, ARM::R2, ARM::R3, ARM::R12}, ClearRegs);
      CMSEClearGPRegs(AfterBB, AfterBB.end(), MBBI->getDebugLoc(), ClearRegs,
                      ARM::LR);

      MachineInstrBuilder NewMI =
          BuildMI(AfterBB, AfterBB.end(), MBBI->getDebugLoc(),
                  TII->get(ARM::tBXNS))
              .addReg(ARM::LR)
              .add(predOps(ARMCC::AL));
      for (const MachineOperand &Op : MI.operands())
        NewMI->addOperand(Op);
      MI.eraseFromParent();
      return true;
    }
    case ARM::tBLXNS_CALL: {
      DebugLoc DL = MBBI->getDebugLoc();
      Register JumpReg = MBBI->getOperand(0).getReg();

      // Figure out which registers are live at the point immediately before the
      // call. When we indiscriminately push a set of registers, the live
      // registers are added as ordinary use operands, whereas dead registers
      // are "undef".
      LivePhysRegs LiveRegs(*TRI);
      LiveRegs.addLiveOuts(MBB);
      for (const MachineInstr &MI : make_range(MBB.rbegin(), MBBI.getReverse()))
        LiveRegs.stepBackward(MI);
      LiveRegs.stepBackward(*MBBI);

      CMSEPushCalleeSaves(*TII, MBB, MBBI, JumpReg, LiveRegs,
                          AFI->isThumb1OnlyFunction());

      SmallVector<unsigned, 16> ClearRegs;
      determineGPRegsToClear(*MBBI,
                             {ARM::R0, ARM::R1, ARM::R2, ARM::R3, ARM::R4,
                              ARM::R5, ARM::R6, ARM::R7, ARM::R8, ARM::R9,
                              ARM::R10, ARM::R11, ARM::R12},
                             ClearRegs);
      auto OriginalClearRegs = ClearRegs;

      // Get the first cleared register as a scratch (to use later with tBIC).
      // We need to use the first so we can ensure it is a low register.
      unsigned ScratchReg = ClearRegs.front();

      // Clear LSB of JumpReg
      if (AFI->isThumb2Function()) {
        BuildMI(MBB, MBBI, DL, TII->get(ARM::t2BICri), JumpReg)
            .addReg(JumpReg)
            .addImm(1)
            .add(predOps(ARMCC::AL))
            .add(condCodeOp());
      } else {
        // We need to use an extra register to cope with 8M Baseline,
        // since we have saved all of the registers we are ok to trash a non
        // argument register here.
        BuildMI(MBB, MBBI, DL, TII->get(ARM::tMOVi8), ScratchReg)
            .add(condCodeOp())
            .addImm(1)
            .add(predOps(ARMCC::AL));
        BuildMI(MBB, MBBI, DL, TII->get(ARM::tBIC), JumpReg)
            .addReg(ARM::CPSR, RegState::Define)
            .addReg(JumpReg)
            .addReg(ScratchReg)
            .add(predOps(ARMCC::AL));
      }

      CMSESaveClearFPRegs(MBB, MBBI, DL, LiveRegs,
                          ClearRegs); // save+clear FP regs with ClearRegs
      CMSEClearGPRegs(MBB, MBBI, DL, ClearRegs, JumpReg);

      const MachineInstrBuilder NewCall =
          BuildMI(MBB, MBBI, DL, TII->get(ARM::tBLXNSr))
              .add(predOps(ARMCC::AL))
              .addReg(JumpReg, RegState::Kill);

      for (const MachineOperand &MO : llvm::drop_begin(MI.operands()))
        NewCall->addOperand(MO);
      if (MI.isCandidateForCallSiteEntry())
        MI.getMF()->moveCallSiteInfo(&MI, NewCall.getInstr());

      CMSERestoreFPRegs(MBB, MBBI, DL, OriginalClearRegs); // restore FP registers

      CMSEPopCalleeSaves(*TII, MBB, MBBI, JumpReg, AFI->isThumb1OnlyFunction());

      MI.eraseFromParent();
      return true;
    }
    case ARM::VMOVHcc:
    case ARM::VMOVScc:
    case ARM::VMOVDcc: {
      unsigned newOpc = Opcode != ARM::VMOVDcc ? ARM::VMOVS : ARM::VMOVD;
      BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(newOpc),
              MI.getOperand(1).getReg())
          .add(MI.getOperand(2))
          .addImm(MI.getOperand(3).getImm()) // 'pred'
          .add(MI.getOperand(4))
          .add(makeImplicit(MI.getOperand(1)));

      MI.eraseFromParent();
      return true;
    }
    case ARM::t2MOVCCr:
    case ARM::MOVCCr: {
      unsigned Opc = AFI->isThumbFunction() ? ARM::t2MOVr : ARM::MOVr;
      BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(Opc),
              MI.getOperand(1).getReg())
          .add(MI.getOperand(2))
          .addImm(MI.getOperand(3).getImm()) // 'pred'
          .add(MI.getOperand(4))
          .add(condCodeOp()) // 's' bit
          .add(makeImplicit(MI.getOperand(1)));

      MI.eraseFromParent();
      return true;
    }
    case ARM::MOVCCsi: {
      BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(ARM::MOVsi),
              (MI.getOperand(1).getReg()))
          .add(MI.getOperand(2))
          .addImm(MI.getOperand(3).getImm())
          .addImm(MI.getOperand(4).getImm()) // 'pred'
          .add(MI.getOperand(5))
          .add(condCodeOp()) // 's' bit
          .add(makeImplicit(MI.getOperand(1)));

      MI.eraseFromParent();
      return true;
    }
    case ARM::MOVCCsr: {
      BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(ARM::MOVsr),
              (MI.getOperand(1).getReg()))
          .add(MI.getOperand(2))
          .add(MI.getOperand(3))
          .addImm(MI.getOperand(4).getImm())
          .addImm(MI.getOperand(5).getImm()) // 'pred'
          .add(MI.getOperand(6))
          .add(condCodeOp()) // 's' bit
          .add(makeImplicit(MI.getOperand(1)));

      MI.eraseFromParent();
      return true;
    }
    case ARM::t2MOVCCi16:
    case ARM::MOVCCi16: {
      unsigned NewOpc = AFI->isThumbFunction() ? ARM::t2MOVi16 : ARM::MOVi16;
      BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(NewOpc),
              MI.getOperand(1).getReg())
          .addImm(MI.getOperand(2).getImm())
          .addImm(MI.getOperand(3).getImm()) // 'pred'
          .add(MI.getOperand(4))
          .add(makeImplicit(MI.getOperand(1)));
      MI.eraseFromParent();
      return true;
    }
    case ARM::t2MOVCCi:
    case ARM::MOVCCi: {
      unsigned Opc = AFI->isThumbFunction() ? ARM::t2MOVi : ARM::MOVi;
      BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(Opc),
              MI.getOperand(1).getReg())
          .addImm(MI.getOperand(2).getImm())
          .addImm(MI.getOperand(3).getImm()) // 'pred'
          .add(MI.getOperand(4))
          .add(condCodeOp()) // 's' bit
          .add(makeImplicit(MI.getOperand(1)));

      MI.eraseFromParent();
      return true;
    }
    case ARM::t2MVNCCi:
    case ARM::MVNCCi: {
      unsigned Opc = AFI->isThumbFunction() ? ARM::t2MVNi : ARM::MVNi;
      BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(Opc),
              MI.getOperand(1).getReg())
          .addImm(MI.getOperand(2).getImm())
          .addImm(MI.getOperand(3).getImm()) // 'pred'
          .add(MI.getOperand(4))
          .add(condCodeOp()) // 's' bit
          .add(makeImplicit(MI.getOperand(1)));

      MI.eraseFromParent();
      return true;
    }
    case ARM::t2MOVCClsl:
    case ARM::t2MOVCClsr:
    case ARM::t2MOVCCasr:
    case ARM::t2MOVCCror: {
      unsigned NewOpc;
      switch (Opcode) {
      case ARM::t2MOVCClsl: NewOpc = ARM::t2LSLri; break;
      case ARM::t2MOVCClsr: NewOpc = ARM::t2LSRri; break;
      case ARM::t2MOVCCasr: NewOpc = ARM::t2ASRri; break;
      case ARM::t2MOVCCror: NewOpc = ARM::t2RORri; break;
      default: llvm_unreachable("unexpeced conditional move");
      }
      BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(NewOpc),
              MI.getOperand(1).getReg())
          .add(MI.getOperand(2))
          .addImm(MI.getOperand(3).getImm())
          .addImm(MI.getOperand(4).getImm()) // 'pred'
          .add(MI.getOperand(5))
          .add(condCodeOp()) // 's' bit
          .add(makeImplicit(MI.getOperand(1)));
      MI.eraseFromParent();
      return true;
    }
    case ARM::Int_eh_sjlj_dispatchsetup: {
      MachineFunction &MF = *MI.getParent()->getParent();
      const ARMBaseInstrInfo *AII =
        static_cast<const ARMBaseInstrInfo*>(TII);
      const ARMBaseRegisterInfo &RI = AII->getRegisterInfo();
      // For functions using a base pointer, we rematerialize it (via the frame
      // pointer) here since eh.sjlj.setjmp and eh.sjlj.longjmp don't do it
      // for us. Otherwise, expand to nothing.
      if (RI.hasBasePointer(MF)) {
        int32_t NumBytes = AFI->getFramePtrSpillOffset();
        Register FramePtr = RI.getFrameRegister(MF);
        assert(MF.getSubtarget().getFrameLowering()->hasFP(MF) &&
               "base pointer without frame pointer?");

        if (AFI->isThumb2Function()) {
          emitT2RegPlusImmediate(MBB, MBBI, MI.getDebugLoc(), ARM::R6,
                                 FramePtr, -NumBytes, ARMCC::AL, 0, *TII);
        } else if (AFI->isThumbFunction()) {
          emitThumbRegPlusImmediate(MBB, MBBI, MI.getDebugLoc(), ARM::R6,
                                    FramePtr, -NumBytes, *TII, RI);
        } else {
          emitARMRegPlusImmediate(MBB, MBBI, MI.getDebugLoc(), ARM::R6,
                                  FramePtr, -NumBytes, ARMCC::AL, 0,
                                  *TII);
        }
        // If there's dynamic realignment, adjust for it.
        if (RI.hasStackRealignment(MF)) {
          MachineFrameInfo &MFI = MF.getFrameInfo();
          Align MaxAlign = MFI.getMaxAlign();
          assert (!AFI->isThumb1OnlyFunction());
          // Emit bic r6, r6, MaxAlign
          assert(MaxAlign <= Align(256) &&
                 "The BIC instruction cannot encode "
                 "immediates larger than 256 with all lower "
                 "bits set.");
          unsigned bicOpc = AFI->isThumbFunction() ?
            ARM::t2BICri : ARM::BICri;
          BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(bicOpc), ARM::R6)
              .addReg(ARM::R6, RegState::Kill)
              .addImm(MaxAlign.value() - 1)
              .add(predOps(ARMCC::AL))
              .add(condCodeOp());
        }
      }
      MI.eraseFromParent();
      return true;
    }

    case ARM::MOVsrl_glue:
    case ARM::MOVsra_glue: {
      // These are just fancy MOVs instructions.
      BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(ARM::MOVsi),
              MI.getOperand(0).getReg())
          .add(MI.getOperand(1))
          .addImm(ARM_AM::getSORegOpc(
              (Opcode == ARM::MOVsrl_glue ? ARM_AM::lsr : ARM_AM::asr), 1))
          .add(predOps(ARMCC::AL))
          .addReg(ARM::CPSR, RegState::Define);
      MI.eraseFromParent();
      return true;
    }
    case ARM::RRX: {
      // This encodes as "MOVs Rd, Rm, rrx
      BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(ARM::MOVsi),
              MI.getOperand(0).getReg())
          .add(MI.getOperand(1))
          .addImm(ARM_AM::getSORegOpc(ARM_AM::rrx, 0))
          .add(predOps(ARMCC::AL))
          .add(condCodeOp())
          .copyImplicitOps(MI);
      MI.eraseFromParent();
      return true;
    }
    case ARM::tTPsoft:
    case ARM::TPsoft: {
      const bool Thumb = Opcode == ARM::tTPsoft;

      MachineInstrBuilder MIB;
      MachineFunction *MF = MBB.getParent();
      if (STI->genLongCalls()) {
        MachineConstantPool *MCP = MF->getConstantPool();
        unsigned PCLabelID = AFI->createPICLabelUId();
        MachineConstantPoolValue *CPV =
            ARMConstantPoolSymbol::Create(MF->getFunction().getContext(),
                                          "__aeabi_read_tp", PCLabelID, 0);
        Register Reg = MI.getOperand(0).getReg();
        MIB =
            BuildMI(MBB, MBBI, MI.getDebugLoc(),
                    TII->get(Thumb ? ARM::tLDRpci : ARM::LDRi12), Reg)
                .addConstantPoolIndex(MCP->getConstantPoolIndex(CPV, Align(4)));
        if (!Thumb)
          MIB.addImm(0);
        MIB.add(predOps(ARMCC::AL));

        MIB =
            BuildMI(MBB, MBBI, MI.getDebugLoc(),
                    TII->get(Thumb ? gettBLXrOpcode(*MF) : getBLXOpcode(*MF)));
        if (Thumb)
          MIB.add(predOps(ARMCC::AL));
        MIB.addReg(Reg, RegState::Kill);
      } else {
        MIB = BuildMI(MBB, MBBI, MI.getDebugLoc(),
                      TII->get(Thumb ? ARM::tBL : ARM::BL));
        if (Thumb)
          MIB.add(predOps(ARMCC::AL));
        MIB.addExternalSymbol("__aeabi_read_tp", 0);
      }

      MIB.cloneMemRefs(MI);
      MIB.copyImplicitOps(MI);
      // Update the call site info.
      if (MI.isCandidateForCallSiteEntry())
        MF->moveCallSiteInfo(&MI, &*MIB);
      MI.eraseFromParent();
      return true;
    }
    case ARM::tLDRpci_pic:
    case ARM::t2LDRpci_pic: {
      unsigned NewLdOpc = (Opcode == ARM::tLDRpci_pic)
        ? ARM::tLDRpci : ARM::t2LDRpci;
      Register DstReg = MI.getOperand(0).getReg();
      bool DstIsDead = MI.getOperand(0).isDead();
      BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(NewLdOpc), DstReg)
          .add(MI.getOperand(1))
          .add(predOps(ARMCC::AL))
          .cloneMemRefs(MI)
          .copyImplicitOps(MI);
      BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(ARM::tPICADD))
          .addReg(DstReg, RegState::Define | getDeadRegState(DstIsDead))
          .addReg(DstReg)
          .add(MI.getOperand(2))
          .copyImplicitOps(MI);
      MI.eraseFromParent();
      return true;
    }

    case ARM::LDRLIT_ga_abs:
    case ARM::LDRLIT_ga_pcrel:
    case ARM::LDRLIT_ga_pcrel_ldr:
    case ARM::tLDRLIT_ga_abs:
    case ARM::t2LDRLIT_ga_pcrel:
    case ARM::tLDRLIT_ga_pcrel: {
      Register DstReg = MI.getOperand(0).getReg();
      bool DstIsDead = MI.getOperand(0).isDead();
      const MachineOperand &MO1 = MI.getOperand(1);
      auto Flags = MO1.getTargetFlags();
      const GlobalValue *GV = MO1.getGlobal();
      bool IsARM = Opcode != ARM::tLDRLIT_ga_pcrel &&
                   Opcode != ARM::tLDRLIT_ga_abs &&
                   Opcode != ARM::t2LDRLIT_ga_pcrel;
      bool IsPIC =
          Opcode != ARM::LDRLIT_ga_abs && Opcode != ARM::tLDRLIT_ga_abs;
      unsigned LDRLITOpc = IsARM ? ARM::LDRi12 : ARM::tLDRpci;
      if (Opcode == ARM::t2LDRLIT_ga_pcrel)
        LDRLITOpc = ARM::t2LDRpci;
      unsigned PICAddOpc =
          IsARM
              ? (Opcode == ARM::LDRLIT_ga_pcrel_ldr ? ARM::PICLDR : ARM::PICADD)
              : ARM::tPICADD;

      // We need a new const-pool entry to load from.
      MachineConstantPool *MCP = MBB.getParent()->getConstantPool();
      unsigned ARMPCLabelIndex = 0;
      MachineConstantPoolValue *CPV;

      if (IsPIC) {
        unsigned PCAdj = IsARM ? 8 : 4;
        auto Modifier = (Flags & ARMII::MO_GOT)
                            ? ARMCP::GOT_PREL
                            : ARMCP::no_modifier;
        ARMPCLabelIndex = AFI->createPICLabelUId();
        CPV = ARMConstantPoolConstant::Create(
            GV, ARMPCLabelIndex, ARMCP::CPValue, PCAdj, Modifier,
            /*AddCurrentAddr*/ Modifier == ARMCP::GOT_PREL);
      } else
        CPV = ARMConstantPoolConstant::Create(GV, ARMCP::no_modifier);

      MachineInstrBuilder MIB =
          BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(LDRLITOpc), DstReg)
              .addConstantPoolIndex(MCP->getConstantPoolIndex(CPV, Align(4)));
      if (IsARM)
        MIB.addImm(0);
      MIB.add(predOps(ARMCC::AL));

      if (IsPIC) {
        MachineInstrBuilder MIB =
          BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(PICAddOpc))
            .addReg(DstReg, RegState::Define | getDeadRegState(DstIsDead))
            .addReg(DstReg)
            .addImm(ARMPCLabelIndex);

        if (IsARM)
          MIB.add(predOps(ARMCC::AL));
      }

      MI.eraseFromParent();
      return true;
    }
    case ARM::MOV_ga_pcrel:
    case ARM::MOV_ga_pcrel_ldr:
    case ARM::t2MOV_ga_pcrel: {
      // Expand into movw + movw. Also "add pc" / ldr [pc] in PIC mode.
      unsigned LabelId = AFI->createPICLabelUId();
      Register DstReg = MI.getOperand(0).getReg();
      bool DstIsDead = MI.getOperand(0).isDead();
      const MachineOperand &MO1 = MI.getOperand(1);
      const GlobalValue *GV = MO1.getGlobal();
      unsigned TF = MO1.getTargetFlags();
      bool isARM = Opcode != ARM::t2MOV_ga_pcrel;
      unsigned LO16Opc = isARM ? ARM::MOVi16_ga_pcrel : ARM::t2MOVi16_ga_pcrel;
      unsigned HI16Opc = isARM ? ARM::MOVTi16_ga_pcrel :ARM::t2MOVTi16_ga_pcrel;
      unsigned LO16TF = TF | ARMII::MO_LO16;
      unsigned HI16TF = TF | ARMII::MO_HI16;
      unsigned PICAddOpc = isARM
        ? (Opcode == ARM::MOV_ga_pcrel_ldr ? ARM::PICLDR : ARM::PICADD)
        : ARM::tPICADD;
      BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(LO16Opc), DstReg)
          .addGlobalAddress(GV, MO1.getOffset(), TF | LO16TF)
          .addImm(LabelId)
          .copyImplicitOps(MI);

      BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(HI16Opc), DstReg)
          .addReg(DstReg)
          .addGlobalAddress(GV, MO1.getOffset(), TF | HI16TF)
          .addImm(LabelId)
          .copyImplicitOps(MI);

      MachineInstrBuilder MIB3 = BuildMI(MBB, MBBI, MI.getDebugLoc(),
                                         TII->get(PICAddOpc))
        .addReg(DstReg, RegState::Define | getDeadRegState(DstIsDead))
        .addReg(DstReg).addImm(LabelId);
      if (isARM) {
        MIB3.add(predOps(ARMCC::AL));
        if (Opcode == ARM::MOV_ga_pcrel_ldr)
          MIB3.cloneMemRefs(MI);
      }
      MIB3.copyImplicitOps(MI);
      MI.eraseFromParent();
      return true;
    }

    case ARM::MOVi32imm:
    case ARM::MOVCCi32imm:
    case ARM::t2MOVi32imm:
    case ARM::t2MOVCCi32imm:
      ExpandMOV32BitImm(MBB, MBBI);
      return true;

    case ARM::tMOVi32imm:
      ExpandTMOV32BitImm(MBB, MBBI);
      return true;

    case ARM::tLEApcrelJT:
      // Inline jump tables are handled in ARMAsmPrinter.
      if (MI.getMF()->getJumpTableInfo()->getEntryKind() ==
          MachineJumpTableInfo::EK_Inline)
        return false;

      // Use a 32-bit immediate move to generate the address of the jump table.
      assert(STI->isThumb() && "Non-inline jump tables expected only in thumb");
      ExpandTMOV32BitImm(MBB, MBBI);
      return true;

    case ARM::SUBS_PC_LR: {
      BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(ARM::SUBri), ARM::PC)
          .addReg(ARM::LR)
          .add(MI.getOperand(0))
          .add(MI.getOperand(1))
          .add(MI.getOperand(2))
          .addReg(ARM::CPSR, RegState::Undef)
          .copyImplicitOps(MI);
      MI.eraseFromParent();
      return true;
    }
    case ARM::VLDMQIA: {
      unsigned NewOpc = ARM::VLDMDIA;
      MachineInstrBuilder MIB =
        BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(NewOpc));
      unsigned OpIdx = 0;

      // Grab the Q register destination.
      bool DstIsDead = MI.getOperand(OpIdx).isDead();
      Register DstReg = MI.getOperand(OpIdx++).getReg();

      // Copy the source register.
      MIB.add(MI.getOperand(OpIdx++));

      // Copy the predicate operands.
      MIB.add(MI.getOperand(OpIdx++));
      MIB.add(MI.getOperand(OpIdx++));

      // Add the destination operands (D subregs).
      Register D0 = TRI->getSubReg(DstReg, ARM::dsub_0);
      Register D1 = TRI->getSubReg(DstReg, ARM::dsub_1);
      MIB.addReg(D0, RegState::Define | getDeadRegState(DstIsDead))
        .addReg(D1, RegState::Define | getDeadRegState(DstIsDead));

      // Add an implicit def for the super-register.
      MIB.addReg(DstReg, RegState::ImplicitDefine | getDeadRegState(DstIsDead));
      MIB.copyImplicitOps(MI);
      MIB.cloneMemRefs(MI);
      MI.eraseFromParent();
      return true;
    }

    case ARM::VSTMQIA: {
      unsigned NewOpc = ARM::VSTMDIA;
      MachineInstrBuilder MIB =
        BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(NewOpc));
      unsigned OpIdx = 0;

      // Grab the Q register source.
      bool SrcIsKill = MI.getOperand(OpIdx).isKill();
      Register SrcReg = MI.getOperand(OpIdx++).getReg();

      // Copy the destination register.
      MachineOperand Dst(MI.getOperand(OpIdx++));
      MIB.add(Dst);

      // Copy the predicate operands.
      MIB.add(MI.getOperand(OpIdx++));
      MIB.add(MI.getOperand(OpIdx++));

      // Add the source operands (D subregs).
      Register D0 = TRI->getSubReg(SrcReg, ARM::dsub_0);
      Register D1 = TRI->getSubReg(SrcReg, ARM::dsub_1);
      MIB.addReg(D0, SrcIsKill ? RegState::Kill : 0)
         .addReg(D1, SrcIsKill ? RegState::Kill : 0);

      if (SrcIsKill)      // Add an implicit kill for the Q register.
        MIB->addRegisterKilled(SrcReg, TRI, true);

      MIB.copyImplicitOps(MI);
      MIB.cloneMemRefs(MI);
      MI.eraseFromParent();
      return true;
    }

    case ARM::VLD2q8Pseudo:
    case ARM::VLD2q16Pseudo:
    case ARM::VLD2q32Pseudo:
    case ARM::VLD2q8PseudoWB_fixed:
    case ARM::VLD2q16PseudoWB_fixed:
    case ARM::VLD2q32PseudoWB_fixed:
    case ARM::VLD2q8PseudoWB_register:
    case ARM::VLD2q16PseudoWB_register:
    case ARM::VLD2q32PseudoWB_register:
    case ARM::VLD3d8Pseudo:
    case ARM::VLD3d16Pseudo:
    case ARM::VLD3d32Pseudo:
    case ARM::VLD1d8TPseudo:
    case ARM::VLD1d8TPseudoWB_fixed:
    case ARM::VLD1d8TPseudoWB_register:
    case ARM::VLD1d16TPseudo:
    case ARM::VLD1d16TPseudoWB_fixed:
    case ARM::VLD1d16TPseudoWB_register:
    case ARM::VLD1d32TPseudo:
    case ARM::VLD1d32TPseudoWB_fixed:
    case ARM::VLD1d32TPseudoWB_register:
    case ARM::VLD1d64TPseudo:
    case ARM::VLD1d64TPseudoWB_fixed:
    case ARM::VLD1d64TPseudoWB_register:
    case ARM::VLD3d8Pseudo_UPD:
    case ARM::VLD3d16Pseudo_UPD:
    case ARM::VLD3d32Pseudo_UPD:
    case ARM::VLD3q8Pseudo_UPD:
    case ARM::VLD3q16Pseudo_UPD:
    case ARM::VLD3q32Pseudo_UPD:
    case ARM::VLD3q8oddPseudo:
    case ARM::VLD3q16oddPseudo:
    case ARM::VLD3q32oddPseudo:
    case ARM::VLD3q8oddPseudo_UPD:
    case ARM::VLD3q16oddPseudo_UPD:
    case ARM::VLD3q32oddPseudo_UPD:
    case ARM::VLD4d8Pseudo:
    case ARM::VLD4d16Pseudo:
    case ARM::VLD4d32Pseudo:
    case ARM::VLD1d8QPseudo:
    case ARM::VLD1d8QPseudoWB_fixed:
    case ARM::VLD1d8QPseudoWB_register:
    case ARM::VLD1d16QPseudo:
    case ARM::VLD1d16QPseudoWB_fixed:
    case ARM::VLD1d16QPseudoWB_register:
    case ARM::VLD1d32QPseudo:
    case ARM::VLD1d32QPseudoWB_fixed:
    case ARM::VLD1d32QPseudoWB_register:
    case ARM::VLD1d64QPseudo:
    case ARM::VLD1d64QPseudoWB_fixed:
    case ARM::VLD1d64QPseudoWB_register:
    case ARM::VLD1q8HighQPseudo:
    case ARM::VLD1q8HighQPseudo_UPD:
    case ARM::VLD1q8LowQPseudo_UPD:
    case ARM::VLD1q8HighTPseudo:
    case ARM::VLD1q8HighTPseudo_UPD:
    case ARM::VLD1q8LowTPseudo_UPD:
    case ARM::VLD1q16HighQPseudo:
    case ARM::VLD1q16HighQPseudo_UPD:
    case ARM::VLD1q16LowQPseudo_UPD:
    case ARM::VLD1q16HighTPseudo:
    case ARM::VLD1q16HighTPseudo_UPD:
    case ARM::VLD1q16LowTPseudo_UPD:
    case ARM::VLD1q32HighQPseudo:
    case ARM::VLD1q32HighQPseudo_UPD:
    case ARM::VLD1q32LowQPseudo_UPD:
    case ARM::VLD1q32HighTPseudo:
    case ARM::VLD1q32HighTPseudo_UPD:
    case ARM::VLD1q32LowTPseudo_UPD:
    case ARM::VLD1q64HighQPseudo:
    case ARM::VLD1q64HighQPseudo_UPD:
    case ARM::VLD1q64LowQPseudo_UPD:
    case ARM::VLD1q64HighTPseudo:
    case ARM::VLD1q64HighTPseudo_UPD:
    case ARM::VLD1q64LowTPseudo_UPD:
    case ARM::VLD4d8Pseudo_UPD:
    case ARM::VLD4d16Pseudo_UPD:
    case ARM::VLD4d32Pseudo_UPD:
    case ARM::VLD4q8Pseudo_UPD:
    case ARM::VLD4q16Pseudo_UPD:
    case ARM::VLD4q32Pseudo_UPD:
    case ARM::VLD4q8oddPseudo:
    case ARM::VLD4q16oddPseudo:
    case ARM::VLD4q32oddPseudo:
    case ARM::VLD4q8oddPseudo_UPD:
    case ARM::VLD4q16oddPseudo_UPD:
    case ARM::VLD4q32oddPseudo_UPD:
    case ARM::VLD3DUPd8Pseudo:
    case ARM::VLD3DUPd16Pseudo:
    case ARM::VLD3DUPd32Pseudo:
    case ARM::VLD3DUPd8Pseudo_UPD:
    case ARM::VLD3DUPd16Pseudo_UPD:
    case ARM::VLD3DUPd32Pseudo_UPD:
    case ARM::VLD4DUPd8Pseudo:
    case ARM::VLD4DUPd16Pseudo:
    case ARM::VLD4DUPd32Pseudo:
    case ARM::VLD4DUPd8Pseudo_UPD:
    case ARM::VLD4DUPd16Pseudo_UPD:
    case ARM::VLD4DUPd32Pseudo_UPD:
    case ARM::VLD2DUPq8EvenPseudo:
    case ARM::VLD2DUPq8OddPseudo:
    case ARM::VLD2DUPq16EvenPseudo:
    case ARM::VLD2DUPq16OddPseudo:
    case ARM::VLD2DUPq32EvenPseudo:
    case ARM::VLD2DUPq32OddPseudo:
    case ARM::VLD2DUPq8OddPseudoWB_fixed:
    case ARM::VLD2DUPq8OddPseudoWB_register:
    case ARM::VLD2DUPq16OddPseudoWB_fixed:
    case ARM::VLD2DUPq16OddPseudoWB_register:
    case ARM::VLD2DUPq32OddPseudoWB_fixed:
    case ARM::VLD2DUPq32OddPseudoWB_register:
    case ARM::VLD3DUPq8EvenPseudo:
    case ARM::VLD3DUPq8OddPseudo:
    case ARM::VLD3DUPq16EvenPseudo:
    case ARM::VLD3DUPq16OddPseudo:
    case ARM::VLD3DUPq32EvenPseudo:
    case ARM::VLD3DUPq32OddPseudo:
    case ARM::VLD3DUPq8OddPseudo_UPD:
    case ARM::VLD3DUPq16OddPseudo_UPD:
    case ARM::VLD3DUPq32OddPseudo_UPD:
    case ARM::VLD4DUPq8EvenPseudo:
    case ARM::VLD4DUPq8OddPseudo:
    case ARM::VLD4DUPq16EvenPseudo:
    case ARM::VLD4DUPq16OddPseudo:
    case ARM::VLD4DUPq32EvenPseudo:
    case ARM::VLD4DUPq32OddPseudo:
    case ARM::VLD4DUPq8OddPseudo_UPD:
    case ARM::VLD4DUPq16OddPseudo_UPD:
    case ARM::VLD4DUPq32OddPseudo_UPD:
      ExpandVLD(MBBI);
      return true;

    case ARM::VST2q8Pseudo:
    case ARM::VST2q16Pseudo:
    case ARM::VST2q32Pseudo:
    case ARM::VST2q8PseudoWB_fixed:
    case ARM::VST2q16PseudoWB_fixed:
    case ARM::VST2q32PseudoWB_fixed:
    case ARM::VST2q8PseudoWB_register:
    case ARM::VST2q16PseudoWB_register:
    case ARM::VST2q32PseudoWB_register:
    case ARM::VST3d8Pseudo:
    case ARM::VST3d16Pseudo:
    case ARM::VST3d32Pseudo:
    case ARM::VST1d8TPseudo:
    case ARM::VST1d8TPseudoWB_fixed:
    case ARM::VST1d8TPseudoWB_register:
    case ARM::VST1d16TPseudo:
    case ARM::VST1d16TPseudoWB_fixed:
    case ARM::VST1d16TPseudoWB_register:
    case ARM::VST1d32TPseudo:
    case ARM::VST1d32TPseudoWB_fixed:
    case ARM::VST1d32TPseudoWB_register:
    case ARM::VST1d64TPseudo:
    case ARM::VST1d64TPseudoWB_fixed:
    case ARM::VST1d64TPseudoWB_register:
    case ARM::VST3d8Pseudo_UPD:
    case ARM::VST3d16Pseudo_UPD:
    case ARM::VST3d32Pseudo_UPD:
    case ARM::VST3q8Pseudo_UPD:
    case ARM::VST3q16Pseudo_UPD:
    case ARM::VST3q32Pseudo_UPD:
    case ARM::VST3q8oddPseudo:
    case ARM::VST3q16oddPseudo:
    case ARM::VST3q32oddPseudo:
    case ARM::VST3q8oddPseudo_UPD:
    case ARM::VST3q16oddPseudo_UPD:
    case ARM::VST3q32oddPseudo_UPD:
    case ARM::VST4d8Pseudo:
    case ARM::VST4d16Pseudo:
    case ARM::VST4d32Pseudo:
    case ARM::VST1d8QPseudo:
    case ARM::VST1d8QPseudoWB_fixed:
    case ARM::VST1d8QPseudoWB_register:
    case ARM::VST1d16QPseudo:
    case ARM::VST1d16QPseudoWB_fixed:
    case ARM::VST1d16QPseudoWB_register:
    case ARM::VST1d32QPseudo:
    case ARM::VST1d32QPseudoWB_fixed:
    case ARM::VST1d32QPseudoWB_register:
    case ARM::VST1d64QPseudo:
    case ARM::VST1d64QPseudoWB_fixed:
    case ARM::VST1d64QPseudoWB_register:
    case ARM::VST4d8Pseudo_UPD:
    case ARM::VST4d16Pseudo_UPD:
    case ARM::VST4d32Pseudo_UPD:
    case ARM::VST1q8HighQPseudo:
    case ARM::VST1q8LowQPseudo_UPD:
    case ARM::VST1q8HighTPseudo:
    case ARM::VST1q8LowTPseudo_UPD:
    case ARM::VST1q16HighQPseudo:
    case ARM::VST1q16LowQPseudo_UPD:
    case ARM::VST1q16HighTPseudo:
    case ARM::VST1q16LowTPseudo_UPD:
    case ARM::VST1q32HighQPseudo:
    case ARM::VST1q32LowQPseudo_UPD:
    case ARM::VST1q32HighTPseudo:
    case ARM::VST1q32LowTPseudo_UPD:
    case ARM::VST1q64HighQPseudo:
    case ARM::VST1q64LowQPseudo_UPD:
    case ARM::VST1q64HighTPseudo:
    case ARM::VST1q64LowTPseudo_UPD:
    case ARM::VST1q8HighTPseudo_UPD:
    case ARM::VST1q16HighTPseudo_UPD:
    case ARM::VST1q32HighTPseudo_UPD:
    case ARM::VST1q64HighTPseudo_UPD:
    case ARM::VST1q8HighQPseudo_UPD:
    case ARM::VST1q16HighQPseudo_UPD:
    case ARM::VST1q32HighQPseudo_UPD:
    case ARM::VST1q64HighQPseudo_UPD:
    case ARM::VST4q8Pseudo_UPD:
    case ARM::VST4q16Pseudo_UPD:
    case ARM::VST4q32Pseudo_UPD:
    case ARM::VST4q8oddPseudo:
    case ARM::VST4q16oddPseudo:
    case ARM::VST4q32oddPseudo:
    case ARM::VST4q8oddPseudo_UPD:
    case ARM::VST4q16oddPseudo_UPD:
    case ARM::VST4q32oddPseudo_UPD:
      ExpandVST(MBBI);
      return true;

    case ARM::VLD1LNq8Pseudo:
    case ARM::VLD1LNq16Pseudo:
    case ARM::VLD1LNq32Pseudo:
    case ARM::VLD1LNq8Pseudo_UPD:
    case ARM::VLD1LNq16Pseudo_UPD:
    case ARM::VLD1LNq32Pseudo_UPD:
    case ARM::VLD2LNd8Pseudo:
    case ARM::VLD2LNd16Pseudo:
    case ARM::VLD2LNd32Pseudo:
    case ARM::VLD2LNq16Pseudo:
    case ARM::VLD2LNq32Pseudo:
    case ARM::VLD2LNd8Pseudo_UPD:
    case ARM::VLD2LNd16Pseudo_UPD:
    case ARM::VLD2LNd32Pseudo_UPD:
    case ARM::VLD2LNq16Pseudo_UPD:
    case ARM::VLD2LNq32Pseudo_UPD:
    case ARM::VLD3LNd8Pseudo:
    case ARM::VLD3LNd16Pseudo:
    case ARM::VLD3LNd32Pseudo:
    case ARM::VLD3LNq16Pseudo:
    case ARM::VLD3LNq32Pseudo:
    case ARM::VLD3LNd8Pseudo_UPD:
    case ARM::VLD3LNd16Pseudo_UPD:
    case ARM::VLD3LNd32Pseudo_UPD:
    case ARM::VLD3LNq16Pseudo_UPD:
    case ARM::VLD3LNq32Pseudo_UPD:
    case ARM::VLD4LNd8Pseudo:
    case ARM::VLD4LNd16Pseudo:
    case ARM::VLD4LNd32Pseudo:
    case ARM::VLD4LNq16Pseudo:
    case ARM::VLD4LNq32Pseudo:
    case ARM::VLD4LNd8Pseudo_UPD:
    case ARM::VLD4LNd16Pseudo_UPD:
    case ARM::VLD4LNd32Pseudo_UPD:
    case ARM::VLD4LNq16Pseudo_UPD:
    case ARM::VLD4LNq32Pseudo_UPD:
    case ARM::VST1LNq8Pseudo:
    case ARM::VST1LNq16Pseudo:
    case ARM::VST1LNq32Pseudo:
    case ARM::VST1LNq8Pseudo_UPD:
    case ARM::VST1LNq16Pseudo_UPD:
    case ARM::VST1LNq32Pseudo_UPD:
    case ARM::VST2LNd8Pseudo:
    case ARM::VST2LNd16Pseudo:
    case ARM::VST2LNd32Pseudo:
    case ARM::VST2LNq16Pseudo:
    case ARM::VST2LNq32Pseudo:
    case ARM::VST2LNd8Pseudo_UPD:
    case ARM::VST2LNd16Pseudo_UPD:
    case ARM::VST2LNd32Pseudo_UPD:
    case ARM::VST2LNq16Pseudo_UPD:
    case ARM::VST2LNq32Pseudo_UPD:
    case ARM::VST3LNd8Pseudo:
    case ARM::VST3LNd16Pseudo:
    case ARM::VST3LNd32Pseudo:
    case ARM::VST3LNq16Pseudo:
    case ARM::VST3LNq32Pseudo:
    case ARM::VST3LNd8Pseudo_UPD:
    case ARM::VST3LNd16Pseudo_UPD:
    case ARM::VST3LNd32Pseudo_UPD:
    case ARM::VST3LNq16Pseudo_UPD:
    case ARM::VST3LNq32Pseudo_UPD:
    case ARM::VST4LNd8Pseudo:
    case ARM::VST4LNd16Pseudo:
    case ARM::VST4LNd32Pseudo:
    case ARM::VST4LNq16Pseudo:
    case ARM::VST4LNq32Pseudo:
    case ARM::VST4LNd8Pseudo_UPD:
    case ARM::VST4LNd16Pseudo_UPD:
    case ARM::VST4LNd32Pseudo_UPD:
    case ARM::VST4LNq16Pseudo_UPD:
    case ARM::VST4LNq32Pseudo_UPD:
      ExpandLaneOp(MBBI);
      return true;

    case ARM::VTBL3Pseudo: ExpandVTBL(MBBI, ARM::VTBL3, false); return true;
    case ARM::VTBL4Pseudo: ExpandVTBL(MBBI, ARM::VTBL4, false); return true;
    case ARM::VTBX3Pseudo: ExpandVTBL(MBBI, ARM::VTBX3, true); return true;
    case ARM::VTBX4Pseudo: ExpandVTBL(MBBI, ARM::VTBX4, true); return true;

    case ARM::MQQPRLoad:
    case ARM::MQQPRStore:
    case ARM::MQQQQPRLoad:
    case ARM::MQQQQPRStore:
      ExpandMQQPRLoadStore(MBBI);
      return true;

    case ARM::tCMP_SWAP_8:
      assert(STI->isThumb());
      return ExpandCMP_SWAP(MBB, MBBI, ARM::t2LDREXB, ARM::t2STREXB, ARM::tUXTB,
                            NextMBBI);
    case ARM::tCMP_SWAP_16:
      assert(STI->isThumb());
      return ExpandCMP_SWAP(MBB, MBBI, ARM::t2LDREXH, ARM::t2STREXH, ARM::tUXTH,
                            NextMBBI);
    case ARM::tCMP_SWAP_32:
      assert(STI->isThumb());
      return ExpandCMP_SWAP(MBB, MBBI, ARM::t2LDREX, ARM::t2STREX, 0, NextMBBI);

    case ARM::CMP_SWAP_8:
      assert(!STI->isThumb());
      return ExpandCMP_SWAP(MBB, MBBI, ARM::LDREXB, ARM::STREXB, ARM::UXTB,
                            NextMBBI);
    case ARM::CMP_SWAP_16:
      assert(!STI->isThumb());
      return ExpandCMP_SWAP(MBB, MBBI, ARM::LDREXH, ARM::STREXH, ARM::UXTH,
                            NextMBBI);
    case ARM::CMP_SWAP_32:
      assert(!STI->isThumb());
      return ExpandCMP_SWAP(MBB, MBBI, ARM::LDREX, ARM::STREX, 0, NextMBBI);

    case ARM::CMP_SWAP_64:
      return ExpandCMP_SWAP_64(MBB, MBBI, NextMBBI);

    case ARM::tBL_PUSHLR:
    case ARM::BL_PUSHLR: {
      const bool Thumb = Opcode == ARM::tBL_PUSHLR;
      Register Reg = MI.getOperand(0).getReg();
      assert(Reg == ARM::LR && "expect LR register!");
      MachineInstrBuilder MIB;
      if (Thumb) {
        // push {lr}
        BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(ARM::tPUSH))
            .add(predOps(ARMCC::AL))
            .addReg(Reg);

        // bl __gnu_mcount_nc
        MIB = BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(ARM::tBL));
      } else {
        // stmdb   sp!, {lr}
        BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(ARM::STMDB_UPD))
            .addReg(ARM::SP, RegState::Define)
            .addReg(ARM::SP)
            .add(predOps(ARMCC::AL))
            .addReg(Reg);

        // bl __gnu_mcount_nc
        MIB = BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(ARM::BL));
      }
      MIB.cloneMemRefs(MI);
      for (const MachineOperand &MO : llvm::drop_begin(MI.operands()))
        MIB.add(MO);
      MI.eraseFromParent();
      return true;
    }
    case ARM::t2CALL_BTI: {
      MachineFunction &MF = *MI.getMF();
      MachineInstrBuilder MIB =
          BuildMI(MF, MI.getDebugLoc(), TII->get(ARM::tBL));
      MIB.cloneMemRefs(MI);
      for (unsigned i = 0; i < MI.getNumOperands(); ++i)
        MIB.add(MI.getOperand(i));
      if (MI.isCandidateForCallSiteEntry())
        MF.moveCallSiteInfo(&MI, MIB.getInstr());
      MIBundleBuilder Bundler(MBB, MI);
      Bundler.append(MIB);
      Bundler.append(BuildMI(MF, MI.getDebugLoc(), TII->get(ARM::t2BTI)));
      finalizeBundle(MBB, Bundler.begin(), Bundler.end());
      MI.eraseFromParent();
      return true;
    }
    case ARM::LOADDUAL:
    case ARM::STOREDUAL: {
      Register PairReg = MI.getOperand(0).getReg();

      MachineInstrBuilder MIB =
          BuildMI(MBB, MBBI, MI.getDebugLoc(),
                  TII->get(Opcode == ARM::LOADDUAL ? ARM::LDRD : ARM::STRD))
              .addReg(TRI->getSubReg(PairReg, ARM::gsub_0),
                      Opcode == ARM::LOADDUAL ? RegState::Define : 0)
              .addReg(TRI->getSubReg(PairReg, ARM::gsub_1),
                      Opcode == ARM::LOADDUAL ? RegState::Define : 0);
      for (const MachineOperand &MO : llvm::drop_begin(MI.operands()))
        MIB.add(MO);
      MIB.add(predOps(ARMCC::AL));
      MIB.cloneMemRefs(MI);
      MI.eraseFromParent();
      return true;
    }
  }
}

bool ARMExpandPseudo::ExpandMBB(MachineBasicBlock &MBB) {
  bool Modified = false;

  MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
  while (MBBI != E) {
    MachineBasicBlock::iterator NMBBI = std::next(MBBI);
    Modified |= ExpandMI(MBB, MBBI, NMBBI);
    MBBI = NMBBI;
  }

  return Modified;
}

bool ARMExpandPseudo::runOnMachineFunction(MachineFunction &MF) {
  STI = &MF.getSubtarget<ARMSubtarget>();
  TII = STI->getInstrInfo();
  TRI = STI->getRegisterInfo();
  AFI = MF.getInfo<ARMFunctionInfo>();

  LLVM_DEBUG(dbgs() << "********** ARM EXPAND PSEUDO INSTRUCTIONS **********\n"
                    << "********** Function: " << MF.getName() << '\n');

  bool Modified = false;
  for (MachineBasicBlock &MBB : MF)
    Modified |= ExpandMBB(MBB);
  if (VerifyARMPseudo)
    MF.verify(this, "After expanding ARM pseudo instructions.");

  LLVM_DEBUG(dbgs() << "***************************************************\n");
  return Modified;
}

/// createARMExpandPseudoPass - returns an instance of the pseudo instruction
/// expansion pass.
FunctionPass *llvm::createARMExpandPseudoPass() {
  return new ARMExpandPseudo();
}

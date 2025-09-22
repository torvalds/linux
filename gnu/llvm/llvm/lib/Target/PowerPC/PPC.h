//===-- PPC.h - Top-level interface for PowerPC Target ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in the LLVM
// PowerPC back-end.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_POWERPC_PPC_H
#define LLVM_LIB_TARGET_POWERPC_PPC_H

#include "llvm/Support/CodeGen.h"

// GCC #defines PPC on Linux but we use it as our namespace name
#undef PPC

namespace llvm {
class PPCRegisterBankInfo;
class PPCSubtarget;
class PPCTargetMachine;
class PassRegistry;
class FunctionPass;
class InstructionSelector;
class MachineInstr;
class MachineOperand;
class AsmPrinter;
class MCInst;
class MCOperand;
class ModulePass;

#ifndef NDEBUG
  FunctionPass *createPPCCTRLoopsVerify();
#endif
  FunctionPass *createPPCLoopInstrFormPrepPass(PPCTargetMachine &TM);
  FunctionPass *createPPCTOCRegDepsPass();
  FunctionPass *createPPCEarlyReturnPass();
  FunctionPass *createPPCVSXCopyPass();
  FunctionPass *createPPCVSXFMAMutatePass();
  FunctionPass *createPPCVSXSwapRemovalPass();
  FunctionPass *createPPCReduceCRLogicalsPass();
  FunctionPass *createPPCMIPeepholePass();
  FunctionPass *createPPCBranchSelectionPass();
  FunctionPass *createPPCBranchCoalescingPass();
  FunctionPass *createPPCISelDag(PPCTargetMachine &TM, CodeGenOptLevel OL);
  FunctionPass *createPPCTLSDynamicCallPass();
  FunctionPass *createPPCBoolRetToIntPass();
  FunctionPass *createPPCExpandISELPass();
  FunctionPass *createPPCPreEmitPeepholePass();
  FunctionPass *createPPCExpandAtomicPseudoPass();
  FunctionPass *createPPCCTRLoopsPass();
  ModulePass *createPPCMergeStringPoolPass();
  void LowerPPCMachineInstrToMCInst(const MachineInstr *MI, MCInst &OutMI,
                                    AsmPrinter &AP);
  bool LowerPPCMachineOperandToMCOperand(const MachineOperand &MO,
                                         MCOperand &OutMO, AsmPrinter &AP);

#ifndef NDEBUG
  void initializePPCCTRLoopsVerifyPass(PassRegistry&);
#endif
  void initializePPCLoopInstrFormPrepPass(PassRegistry&);
  void initializePPCTOCRegDepsPass(PassRegistry&);
  void initializePPCEarlyReturnPass(PassRegistry&);
  void initializePPCVSXCopyPass(PassRegistry&);
  void initializePPCVSXFMAMutatePass(PassRegistry&);
  void initializePPCVSXSwapRemovalPass(PassRegistry&);
  void initializePPCReduceCRLogicalsPass(PassRegistry&);
  void initializePPCBSelPass(PassRegistry&);
  void initializePPCBranchCoalescingPass(PassRegistry&);
  void initializePPCBoolRetToIntPass(PassRegistry&);
  void initializePPCExpandISELPass(PassRegistry &);
  void initializePPCPreEmitPeepholePass(PassRegistry &);
  void initializePPCTLSDynamicCallPass(PassRegistry &);
  void initializePPCMIPeepholePass(PassRegistry&);
  void initializePPCExpandAtomicPseudoPass(PassRegistry &);
  void initializePPCCTRLoopsPass(PassRegistry &);
  void initializePPCDAGToDAGISelLegacyPass(PassRegistry &);
  void initializePPCMergeStringPoolPass(PassRegistry &);

  extern char &PPCVSXFMAMutateID;

  ModulePass *createPPCLowerMASSVEntriesPass();
  void initializePPCLowerMASSVEntriesPass(PassRegistry &);
  extern char &PPCLowerMASSVEntriesID;

  ModulePass *createPPCGenScalarMASSEntriesPass();
  void initializePPCGenScalarMASSEntriesPass(PassRegistry &);
  extern char &PPCGenScalarMASSEntriesID;

  InstructionSelector *
  createPPCInstructionSelector(const PPCTargetMachine &, const PPCSubtarget &,
                               const PPCRegisterBankInfo &);
  namespace PPCII {

  /// Target Operand Flag enum.
  enum TOF {
    //===------------------------------------------------------------------===//
    // PPC Specific MachineOperand flags.
    MO_NO_FLAG,

    /// On PPC, the 12 bits are not enough for all target operand flags.
    /// Treat all PPC target flags as direct flags. To define new flag that is
    /// combination of other flags, add new enum entry instead of combining
    /// existing flags. See example MO_GOT_TPREL_PCREL_FLAG.

    /// On a symbol operand "FOO", this indicates that the reference is actually
    /// to "FOO@plt".  This is used for calls and jumps to external functions
    /// and for PIC calls on 32-bit ELF systems.
    MO_PLT,

    /// MO_PIC_FLAG - If this bit is set, the symbol reference is relative to
    /// the function's picbase, e.g. lo16(symbol-picbase).
    MO_PIC_FLAG,

    /// MO_PCREL_FLAG - If this bit is set, the symbol reference is relative to
    /// the current instruction address(pc), e.g., var@pcrel. Fixup is VK_PCREL.
    MO_PCREL_FLAG,

    /// MO_GOT_FLAG - If this bit is set the symbol reference is to be computed
    /// via the GOT. For example when combined with the MO_PCREL_FLAG it should
    /// produce the relocation @got@pcrel. Fixup is VK_PPC_GOT_PCREL.
    MO_GOT_FLAG,

    /// MO_PCREL_OPT_FLAG - If this bit is set the operand is part of a
    /// PC Relative linker optimization.
    MO_PCREL_OPT_FLAG,

    /// MO_TLSGD_FLAG - If this bit is set the symbol reference is relative to
    /// TLS General Dynamic model for Linux and the variable offset of TLS
    /// General Dynamic model for AIX.
    MO_TLSGD_FLAG,

    /// MO_TPREL_FLAG - If this bit is set, the symbol reference is relative to
    /// the thread pointer and the symbol can be used for the TLS Initial Exec
    /// and Local Exec models.
    MO_TPREL_FLAG,

    /// MO_TLSLDM_FLAG - on AIX the ML relocation type is only valid for a
    /// reference to a TOC symbol from the symbol itself, and right now its only
    /// user is the symbol "_$TLSML". The symbol name is used to decide that
    /// the R_TLSML relocation is expected.
    MO_TLSLDM_FLAG,

    /// MO_TLSLD_FLAG - If this bit is set the symbol reference is relative to
    /// TLS Local Dynamic model.
    MO_TLSLD_FLAG,

    /// MO_TLSGDM_FLAG - If this bit is set the symbol reference is relative
    /// to the region handle of TLS General Dynamic model for AIX.
    MO_TLSGDM_FLAG,

    /// MO_GOT_TLSGD_PCREL_FLAG - A combintaion of flags, if these bits are set
    /// they should produce the relocation @got@tlsgd@pcrel.
    /// Fix up is VK_PPC_GOT_TLSGD_PCREL
    /// MO_GOT_TLSGD_PCREL_FLAG = MO_PCREL_FLAG | MO_GOT_FLAG | MO_TLSGD_FLAG,
    MO_GOT_TLSGD_PCREL_FLAG,

    /// MO_GOT_TLSLD_PCREL_FLAG - A combintaion of flags, if these bits are set
    /// they should produce the relocation @got@tlsld@pcrel.
    /// Fix up is VK_PPC_GOT_TLSLD_PCREL
    /// MO_GOT_TLSLD_PCREL_FLAG = MO_PCREL_FLAG | MO_GOT_FLAG | MO_TLSLD_FLAG,
    MO_GOT_TLSLD_PCREL_FLAG,

    /// MO_GOT_TPREL_PCREL_FLAG - A combintaion of flags, if these bits are set
    /// they should produce the relocation @got@tprel@pcrel.
    /// Fix up is VK_PPC_GOT_TPREL_PCREL
    /// MO_GOT_TPREL_PCREL_FLAG = MO_GOT_FLAG | MO_TPREL_FLAG | MO_PCREL_FLAG,
    MO_GOT_TPREL_PCREL_FLAG,

    /// MO_LO, MO_HA - lo16(symbol) and ha16(symbol)
    MO_LO,
    MO_HA,

    MO_TPREL_LO,
    MO_TPREL_HA,

    /// These values identify relocations on immediates folded
    /// into memory operations.
    MO_DTPREL_LO,
    MO_TLSLD_LO,
    MO_TOC_LO,

    /// Symbol for VK_PPC_TLS fixup attached to an ADD instruction
    MO_TLS,

    /// MO_PIC_HA_FLAG = MO_PIC_FLAG | MO_HA
    MO_PIC_HA_FLAG,

    /// MO_PIC_LO_FLAG = MO_PIC_FLAG | MO_LO
    MO_PIC_LO_FLAG,

    /// MO_TPREL_PCREL_FLAG = MO_PCREL_FLAG | MO_TPREL_FLAG
    MO_TPREL_PCREL_FLAG,

    /// MO_TPREL_PCREL_FLAG = MO_PCREL_FLAG | MO_TLS
    MO_TLS_PCREL_FLAG,

    /// MO_GOT_PCREL_FLAG = MO_PCREL_FLAG | MO_GOT_FLAG
    MO_GOT_PCREL_FLAG,
  };
  } // end namespace PPCII

} // end namespace llvm;

#endif

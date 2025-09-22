//===- ARMDisassembler.cpp - Disassembler for ARM/Thumb ISA ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ARMBaseInstrInfo.h"
#include "MCTargetDesc/ARMAddressingModes.h"
#include "MCTargetDesc/ARMBaseInfo.h"
#include "MCTargetDesc/ARMMCTargetDesc.h"
#include "TargetInfo/ARMTargetInfo.h"
#include "Utils/ARMBaseInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDecoderOps.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "arm-disassembler"

using DecodeStatus = MCDisassembler::DecodeStatus;

namespace {

  // Handles the condition code status of instructions in IT blocks
  class ITStatus
  {
    public:
      // Returns the condition code for instruction in IT block
      unsigned getITCC() {
        unsigned CC = ARMCC::AL;
        if (instrInITBlock())
          CC = ITStates.back();
        return CC;
      }

      // Advances the IT block state to the next T or E
      void advanceITState() {
        ITStates.pop_back();
      }

      // Returns true if the current instruction is in an IT block
      bool instrInITBlock() {
        return !ITStates.empty();
      }

      // Returns true if current instruction is the last instruction in an IT block
      bool instrLastInITBlock() {
        return ITStates.size() == 1;
      }

      // Called when decoding an IT instruction. Sets the IT state for
      // the following instructions that for the IT block. Firstcond
      // corresponds to the field in the IT instruction encoding; Mask
      // is in the MCOperand format in which 1 means 'else' and 0 'then'.
      void setITState(char Firstcond, char Mask) {
        // (3 - the number of trailing zeros) is the number of then / else.
        unsigned NumTZ = llvm::countr_zero<uint8_t>(Mask);
        unsigned char CCBits = static_cast<unsigned char>(Firstcond & 0xf);
        assert(NumTZ <= 3 && "Invalid IT mask!");
        // push condition codes onto the stack the correct order for the pops
        for (unsigned Pos = NumTZ+1; Pos <= 3; ++Pos) {
          unsigned Else = (Mask >> Pos) & 1;
          ITStates.push_back(CCBits ^ Else);
        }
        ITStates.push_back(CCBits);
      }

    private:
      std::vector<unsigned char> ITStates;
  };

  class VPTStatus
  {
    public:
      unsigned getVPTPred() {
        unsigned Pred = ARMVCC::None;
        if (instrInVPTBlock())
          Pred = VPTStates.back();
        return Pred;
      }

      void advanceVPTState() {
        VPTStates.pop_back();
      }

      bool instrInVPTBlock() {
        return !VPTStates.empty();
      }

      bool instrLastInVPTBlock() {
        return VPTStates.size() == 1;
      }

      void setVPTState(char Mask) {
        // (3 - the number of trailing zeros) is the number of then / else.
        unsigned NumTZ = llvm::countr_zero<uint8_t>(Mask);
        assert(NumTZ <= 3 && "Invalid VPT mask!");
        // push predicates onto the stack the correct order for the pops
        for (unsigned Pos = NumTZ+1; Pos <= 3; ++Pos) {
          bool T = ((Mask >> Pos) & 1) == 0;
          if (T)
            VPTStates.push_back(ARMVCC::Then);
          else
            VPTStates.push_back(ARMVCC::Else);
        }
        VPTStates.push_back(ARMVCC::Then);
      }

    private:
      SmallVector<unsigned char, 4> VPTStates;
  };

/// ARM disassembler for all ARM platforms.
class ARMDisassembler : public MCDisassembler {
public:
  std::unique_ptr<const MCInstrInfo> MCII;

  ARMDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx,
                  const MCInstrInfo *MCII)
      : MCDisassembler(STI, Ctx), MCII(MCII) {
        InstructionEndianness = STI.hasFeature(ARM::ModeBigEndianInstructions)
                                    ? llvm::endianness::big
                                    : llvm::endianness::little;
  }

  ~ARMDisassembler() override = default;

  DecodeStatus getInstruction(MCInst &Instr, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &CStream) const override;

  uint64_t suggestBytesToSkip(ArrayRef<uint8_t> Bytes,
                              uint64_t Address) const override;

private:
  DecodeStatus getARMInstruction(MCInst &Instr, uint64_t &Size,
                                 ArrayRef<uint8_t> Bytes, uint64_t Address,
                                 raw_ostream &CStream) const;

  DecodeStatus getThumbInstruction(MCInst &Instr, uint64_t &Size,
                                   ArrayRef<uint8_t> Bytes, uint64_t Address,
                                   raw_ostream &CStream) const;

  mutable ITStatus ITBlock;
  mutable VPTStatus VPTBlock;

  void AddThumb1SBit(MCInst &MI, bool InITBlock) const;
  bool isVectorPredicable(const MCInst &MI) const;
  DecodeStatus AddThumbPredicate(MCInst&) const;
  void UpdateThumbVFPPredicate(DecodeStatus &, MCInst&) const;

  llvm::endianness InstructionEndianness;
};

} // end anonymous namespace

// Forward declare these because the autogenerated code will reference them.
// Definitions are further down.
static DecodeStatus DecodeGPRRegisterClass(MCInst &Inst, unsigned RegNo,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder);
static DecodeStatus DecodeCLRMGPRRegisterClass(MCInst &Inst, unsigned RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder);
static DecodeStatus DecodetGPROddRegisterClass(MCInst &Inst, unsigned RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder);
static DecodeStatus DecodetGPREvenRegisterClass(MCInst &Inst, unsigned RegNo,
                                                uint64_t Address,
                                                const MCDisassembler *Decoder);
static DecodeStatus
DecodeGPRwithAPSR_NZCVnospRegisterClass(MCInst &Inst, unsigned RegNo,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder);
static DecodeStatus DecodeGPRnopcRegisterClass(MCInst &Inst, unsigned RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder);
static DecodeStatus DecodeGPRnospRegisterClass(MCInst &Inst, unsigned RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder);
static DecodeStatus
DecodeGPRwithAPSRRegisterClass(MCInst &Inst, unsigned RegNo, uint64_t Address,
                               const MCDisassembler *Decoder);
static DecodeStatus DecodeGPRwithZRRegisterClass(MCInst &Inst, unsigned RegNo,
                                                 uint64_t Address,
                                                 const MCDisassembler *Decoder);
static DecodeStatus
DecodeGPRwithZRnospRegisterClass(MCInst &Inst, unsigned RegNo, uint64_t Address,
                                 const MCDisassembler *Decoder);
static DecodeStatus DecodetGPRRegisterClass(MCInst &Inst, unsigned RegNo,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder);
static DecodeStatus DecodetcGPRRegisterClass(MCInst &Inst, unsigned RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder);
static DecodeStatus DecoderGPRRegisterClass(MCInst &Inst, unsigned RegNo,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder);
static DecodeStatus DecodeGPRPairRegisterClass(MCInst &Inst, unsigned RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder);
static DecodeStatus
DecodeGPRPairnospRegisterClass(MCInst &Inst, unsigned RegNo, uint64_t Address,
                               const MCDisassembler *Decoder);
static DecodeStatus DecodeGPRspRegisterClass(MCInst &Inst, unsigned RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder);
static DecodeStatus DecodeHPRRegisterClass(MCInst &Inst, unsigned RegNo,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder);
static DecodeStatus DecodeSPRRegisterClass(MCInst &Inst, unsigned RegNo,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder);
static DecodeStatus DecodeDPRRegisterClass(MCInst &Inst, unsigned RegNo,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder);
static DecodeStatus DecodeDPR_8RegisterClass(MCInst &Inst, unsigned RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder);
static DecodeStatus DecodeSPR_8RegisterClass(MCInst &Inst, unsigned RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder);
static DecodeStatus DecodeDPR_VFP2RegisterClass(MCInst &Inst, unsigned RegNo,
                                                uint64_t Address,
                                                const MCDisassembler *Decoder);
static DecodeStatus DecodeQPRRegisterClass(MCInst &Inst, unsigned RegNo,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder);
static DecodeStatus DecodeMQPRRegisterClass(MCInst &Inst, unsigned RegNo,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder);
static DecodeStatus DecodeMQQPRRegisterClass(MCInst &Inst, unsigned RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder);
static DecodeStatus DecodeMQQQQPRRegisterClass(MCInst &Inst, unsigned RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder);
static DecodeStatus DecodeDPairRegisterClass(MCInst &Inst, unsigned RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder);
static DecodeStatus
DecodeDPairSpacedRegisterClass(MCInst &Inst, unsigned RegNo, uint64_t Address,
                               const MCDisassembler *Decoder);

static DecodeStatus DecodePredicateOperand(MCInst &Inst, unsigned Val,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder);
static DecodeStatus DecodeCCOutOperand(MCInst &Inst, unsigned Val,
                                       uint64_t Address,
                                       const MCDisassembler *Decoder);
static DecodeStatus DecodeRegListOperand(MCInst &Inst, unsigned Val,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder);
static DecodeStatus DecodeSPRRegListOperand(MCInst &Inst, unsigned Val,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder);
static DecodeStatus DecodeDPRRegListOperand(MCInst &Inst, unsigned Val,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder);

static DecodeStatus DecodeBitfieldMaskOperand(MCInst &Inst, unsigned Insn,
                                              uint64_t Address,
                                              const MCDisassembler *Decoder);
static DecodeStatus DecodeCopMemInstruction(MCInst &Inst, unsigned Insn,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder);
static DecodeStatus
DecodeAddrMode2IdxInstruction(MCInst &Inst, unsigned Insn, uint64_t Address,
                              const MCDisassembler *Decoder);
static DecodeStatus DecodeSORegMemOperand(MCInst &Inst, unsigned Insn,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder);
static DecodeStatus DecodeAddrMode3Instruction(MCInst &Inst, unsigned Insn,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder);
static DecodeStatus DecodeTSBInstruction(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder);
static DecodeStatus DecodeSORegImmOperand(MCInst &Inst, unsigned Insn,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder);
static DecodeStatus DecodeSORegRegOperand(MCInst &Inst, unsigned Insn,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder);

static DecodeStatus
DecodeMemMultipleWritebackInstruction(MCInst &Inst, unsigned Insn,
                                      uint64_t Adddress,
                                      const MCDisassembler *Decoder);
static DecodeStatus DecodeT2MOVTWInstruction(MCInst &Inst, unsigned Insn,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder);
static DecodeStatus DecodeArmMOVTWInstruction(MCInst &Inst, unsigned Insn,
                                              uint64_t Address,
                                              const MCDisassembler *Decoder);
static DecodeStatus DecodeSMLAInstruction(MCInst &Inst, unsigned Insn,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder);
static DecodeStatus DecodeHINTInstruction(MCInst &Inst, unsigned Insn,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder);
static DecodeStatus DecodeCPSInstruction(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder);
static DecodeStatus DecodeTSTInstruction(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder);
static DecodeStatus DecodeSETPANInstruction(MCInst &Inst, unsigned Insn,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder);
static DecodeStatus DecodeT2CPSInstruction(MCInst &Inst, unsigned Insn,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder);
static DecodeStatus DecodeT2HintSpaceInstruction(MCInst &Inst, unsigned Insn,
                                                 uint64_t Address,
                                                 const MCDisassembler *Decoder);
static DecodeStatus DecodeAddrModeImm12Operand(MCInst &Inst, unsigned Val,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder);
static DecodeStatus DecodeAddrMode5Operand(MCInst &Inst, unsigned Val,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder);
static DecodeStatus DecodeAddrMode5FP16Operand(MCInst &Inst, unsigned Val,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder);
static DecodeStatus DecodeAddrMode7Operand(MCInst &Inst, unsigned Val,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder);
static DecodeStatus DecodeT2BInstruction(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder);
static DecodeStatus DecodeBranchImmInstruction(MCInst &Inst, unsigned Insn,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder);
static DecodeStatus DecodeAddrMode6Operand(MCInst &Inst, unsigned Val,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder);
static DecodeStatus DecodeVLDST1Instruction(MCInst &Inst, unsigned Val,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder);
static DecodeStatus DecodeVLDST2Instruction(MCInst &Inst, unsigned Val,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder);
static DecodeStatus DecodeVLDST3Instruction(MCInst &Inst, unsigned Val,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder);
static DecodeStatus DecodeVLDST4Instruction(MCInst &Inst, unsigned Val,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder);
static DecodeStatus DecodeVLDInstruction(MCInst &Inst, unsigned Val,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder);
static DecodeStatus DecodeVSTInstruction(MCInst &Inst, unsigned Val,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder);
static DecodeStatus DecodeVLD1DupInstruction(MCInst &Inst, unsigned Val,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder);
static DecodeStatus DecodeVLD2DupInstruction(MCInst &Inst, unsigned Val,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder);
static DecodeStatus DecodeVLD3DupInstruction(MCInst &Inst, unsigned Val,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder);
static DecodeStatus DecodeVLD4DupInstruction(MCInst &Inst, unsigned Val,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder);
static DecodeStatus DecodeVMOVModImmInstruction(MCInst &Inst, unsigned Val,
                                                uint64_t Address,
                                                const MCDisassembler *Decoder);
static DecodeStatus DecodeMVEModImmInstruction(MCInst &Inst, unsigned Val,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder);
static DecodeStatus DecodeMVEVADCInstruction(MCInst &Inst, unsigned Insn,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder);
static DecodeStatus DecodeVSHLMaxInstruction(MCInst &Inst, unsigned Val,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder);
static DecodeStatus DecodeShiftRight8Imm(MCInst &Inst, unsigned Val,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder);
static DecodeStatus DecodeShiftRight16Imm(MCInst &Inst, unsigned Val,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder);
static DecodeStatus DecodeShiftRight32Imm(MCInst &Inst, unsigned Val,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder);
static DecodeStatus DecodeShiftRight64Imm(MCInst &Inst, unsigned Val,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder);
static DecodeStatus DecodeTBLInstruction(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder);
static DecodeStatus DecodePostIdxReg(MCInst &Inst, unsigned Insn,
                                     uint64_t Address,
                                     const MCDisassembler *Decoder);
static DecodeStatus DecodeMveAddrModeRQ(MCInst &Inst, unsigned Insn,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder);
template <int shift>
static DecodeStatus DecodeMveAddrModeQ(MCInst &Inst, unsigned Insn,
                                       uint64_t Address,
                                       const MCDisassembler *Decoder);
static DecodeStatus DecodeCoprocessor(MCInst &Inst, unsigned Insn,
                                      uint64_t Address,
                                      const MCDisassembler *Decoder);
static DecodeStatus DecodeMemBarrierOption(MCInst &Inst, unsigned Insn,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder);
static DecodeStatus DecodeInstSyncBarrierOption(MCInst &Inst, unsigned Insn,
                                                uint64_t Address,
                                                const MCDisassembler *Decoder);
static DecodeStatus DecodeMSRMask(MCInst &Inst, unsigned Insn, uint64_t Address,
                                  const MCDisassembler *Decoder);
static DecodeStatus DecodeBankedReg(MCInst &Inst, unsigned Insn,
                                    uint64_t Address,
                                    const MCDisassembler *Decoder);
static DecodeStatus DecodeDoubleRegLoad(MCInst &Inst, unsigned Insn,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder);
static DecodeStatus DecodeDoubleRegStore(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder);
static DecodeStatus DecodeLDRPreImm(MCInst &Inst, unsigned Insn,
                                    uint64_t Address,
                                    const MCDisassembler *Decoder);
static DecodeStatus DecodeLDRPreReg(MCInst &Inst, unsigned Insn,
                                    uint64_t Address,
                                    const MCDisassembler *Decoder);
static DecodeStatus DecodeSTRPreImm(MCInst &Inst, unsigned Insn,
                                    uint64_t Address,
                                    const MCDisassembler *Decoder);
static DecodeStatus DecodeSTRPreReg(MCInst &Inst, unsigned Insn,
                                    uint64_t Address,
                                    const MCDisassembler *Decoder);
static DecodeStatus DecodeVLD1LN(MCInst &Inst, unsigned Insn, uint64_t Address,
                                 const MCDisassembler *Decoder);
static DecodeStatus DecodeVLD2LN(MCInst &Inst, unsigned Insn, uint64_t Address,
                                 const MCDisassembler *Decoder);
static DecodeStatus DecodeVLD3LN(MCInst &Inst, unsigned Insn, uint64_t Address,
                                 const MCDisassembler *Decoder);
static DecodeStatus DecodeVLD4LN(MCInst &Inst, unsigned Insn, uint64_t Address,
                                 const MCDisassembler *Decoder);
static DecodeStatus DecodeVST1LN(MCInst &Inst, unsigned Insn, uint64_t Address,
                                 const MCDisassembler *Decoder);
static DecodeStatus DecodeVST2LN(MCInst &Inst, unsigned Insn, uint64_t Address,
                                 const MCDisassembler *Decoder);
static DecodeStatus DecodeVST3LN(MCInst &Inst, unsigned Insn, uint64_t Address,
                                 const MCDisassembler *Decoder);
static DecodeStatus DecodeVST4LN(MCInst &Inst, unsigned Insn, uint64_t Address,
                                 const MCDisassembler *Decoder);
static DecodeStatus DecodeVMOVSRR(MCInst &Inst, unsigned Insn, uint64_t Address,
                                  const MCDisassembler *Decoder);
static DecodeStatus DecodeVMOVRRS(MCInst &Inst, unsigned Insn, uint64_t Address,
                                  const MCDisassembler *Decoder);
static DecodeStatus DecodeSwap(MCInst &Inst, unsigned Insn, uint64_t Address,
                               const MCDisassembler *Decoder);
static DecodeStatus DecodeVCVTD(MCInst &Inst, unsigned Insn, uint64_t Address,
                                const MCDisassembler *Decoder);
static DecodeStatus DecodeVCVTQ(MCInst &Inst, unsigned Insn, uint64_t Address,
                                const MCDisassembler *Decoder);
static DecodeStatus DecodeVCVTImmOperand(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder);
static DecodeStatus
DecodeNEONComplexLane64Instruction(MCInst &Inst, unsigned Val, uint64_t Address,
                                   const MCDisassembler *Decoder);

static DecodeStatus DecodeThumbAddSpecialReg(MCInst &Inst, uint16_t Insn,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder);
static DecodeStatus DecodeThumbBROperand(MCInst &Inst, unsigned Val,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder);
static DecodeStatus DecodeT2BROperand(MCInst &Inst, unsigned Val,
                                      uint64_t Address,
                                      const MCDisassembler *Decoder);
static DecodeStatus DecodeThumbCmpBROperand(MCInst &Inst, unsigned Val,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder);
static DecodeStatus DecodeThumbAddrModeRR(MCInst &Inst, unsigned Val,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder);
static DecodeStatus DecodeThumbAddrModeIS(MCInst &Inst, unsigned Val,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder);
static DecodeStatus DecodeThumbAddrModePC(MCInst &Inst, unsigned Val,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder);
static DecodeStatus DecodeThumbAddrModeSP(MCInst &Inst, unsigned Val,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder);
static DecodeStatus DecodeT2AddrModeSOReg(MCInst &Inst, unsigned Val,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder);
static DecodeStatus DecodeT2LoadShift(MCInst &Inst, unsigned Val,
                                      uint64_t Address,
                                      const MCDisassembler *Decoder);
static DecodeStatus DecodeT2LoadImm8(MCInst &Inst, unsigned Insn,
                                     uint64_t Address,
                                     const MCDisassembler *Decoder);
static DecodeStatus DecodeT2LoadImm12(MCInst &Inst, unsigned Insn,
                                      uint64_t Address,
                                      const MCDisassembler *Decoder);
static DecodeStatus DecodeT2LoadT(MCInst &Inst, unsigned Insn, uint64_t Address,
                                  const MCDisassembler *Decoder);
static DecodeStatus DecodeT2LoadLabel(MCInst &Inst, unsigned Insn,
                                      uint64_t Address,
                                      const MCDisassembler *Decoder);
static DecodeStatus DecodeT2Imm8S4(MCInst &Inst, unsigned Val, uint64_t Address,
                                   const MCDisassembler *Decoder);
static DecodeStatus DecodeT2Imm7S4(MCInst &Inst, unsigned Val, uint64_t Address,
                                   const MCDisassembler *Decoder);
static DecodeStatus DecodeT2AddrModeImm8s4(MCInst &Inst, unsigned Val,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder);
static DecodeStatus DecodeT2AddrModeImm7s4(MCInst &Inst, unsigned Val,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder);
static DecodeStatus DecodeT2AddrModeImm0_1020s4(MCInst &Inst, unsigned Val,
                                                uint64_t Address,
                                                const MCDisassembler *Decoder);
static DecodeStatus DecodeT2Imm8(MCInst &Inst, unsigned Val, uint64_t Address,
                                 const MCDisassembler *Decoder);
template <int shift>
static DecodeStatus DecodeT2Imm7(MCInst &Inst, unsigned Val, uint64_t Address,
                                 const MCDisassembler *Decoder);
static DecodeStatus DecodeT2AddrModeImm8(MCInst &Inst, unsigned Val,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder);
template <int shift>
static DecodeStatus DecodeTAddrModeImm7(MCInst &Inst, unsigned Val,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder);
template <int shift, int WriteBack>
static DecodeStatus DecodeT2AddrModeImm7(MCInst &Inst, unsigned Val,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder);
static DecodeStatus DecodeThumbAddSPImm(MCInst &Inst, uint16_t Val,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder);
static DecodeStatus DecodeThumbAddSPReg(MCInst &Inst, uint16_t Insn,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder);
static DecodeStatus DecodeThumbCPS(MCInst &Inst, uint16_t Insn,
                                   uint64_t Address,
                                   const MCDisassembler *Decoder);
static DecodeStatus DecodeQADDInstruction(MCInst &Inst, unsigned Insn,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder);
static DecodeStatus DecodeThumbBLXOffset(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder);
static DecodeStatus DecodeT2AddrModeImm12(MCInst &Inst, unsigned Val,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder);
static DecodeStatus DecodeThumbTableBranch(MCInst &Inst, unsigned Val,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder);
static DecodeStatus DecodeThumb2BCCInstruction(MCInst &Inst, unsigned Val,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder);
static DecodeStatus DecodeT2SOImm(MCInst &Inst, unsigned Val, uint64_t Address,
                                  const MCDisassembler *Decoder);
static DecodeStatus DecodeThumbBCCTargetOperand(MCInst &Inst, unsigned Val,
                                                uint64_t Address,
                                                const MCDisassembler *Decoder);
static DecodeStatus DecodeThumbBLTargetOperand(MCInst &Inst, unsigned Val,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder);
static DecodeStatus DecodeIT(MCInst &Inst, unsigned Val, uint64_t Address,
                             const MCDisassembler *Decoder);
static DecodeStatus DecodeT2LDRDPreInstruction(MCInst &Inst, unsigned Insn,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder);
static DecodeStatus DecodeT2STRDPreInstruction(MCInst &Inst, unsigned Insn,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder);
static DecodeStatus DecodeT2Adr(MCInst &Inst, unsigned Val, uint64_t Address,
                                const MCDisassembler *Decoder);
static DecodeStatus DecodeT2LdStPre(MCInst &Inst, unsigned Val,
                                    uint64_t Address,
                                    const MCDisassembler *Decoder);
static DecodeStatus DecodeT2ShifterImmOperand(MCInst &Inst, unsigned Val,
                                              uint64_t Address,
                                              const MCDisassembler *Decoder);

static DecodeStatus DecodeLDR(MCInst &Inst, unsigned Val, uint64_t Address,
                              const MCDisassembler *Decoder);
static DecodeStatus DecoderForMRRC2AndMCRR2(MCInst &Inst, unsigned Val,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder);
static DecodeStatus DecodeForVMRSandVMSR(MCInst &Inst, unsigned Val,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder);

template <bool isSigned, bool isNeg, bool zeroPermitted, int size>
static DecodeStatus DecodeBFLabelOperand(MCInst &Inst, unsigned val,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder);
static DecodeStatus DecodeBFAfterTargetOperand(MCInst &Inst, unsigned val,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder);
static DecodeStatus DecodePredNoALOperand(MCInst &Inst, unsigned Val,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder);
static DecodeStatus DecodeLOLoop(MCInst &Inst, unsigned Insn, uint64_t Address,
                                 const MCDisassembler *Decoder);
static DecodeStatus DecodeLongShiftOperand(MCInst &Inst, unsigned Val,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder);
static DecodeStatus DecodeVSCCLRM(MCInst &Inst, unsigned Insn, uint64_t Address,
                                  const MCDisassembler *Decoder);
static DecodeStatus DecodeVPTMaskOperand(MCInst &Inst, unsigned Val,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder);
static DecodeStatus DecodeVpredROperand(MCInst &Inst, unsigned Val,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder);
static DecodeStatus DecodeVpredNOperand(MCInst &Inst, unsigned Val,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder);
static DecodeStatus
DecodeRestrictedIPredicateOperand(MCInst &Inst, unsigned Val, uint64_t Address,
                                  const MCDisassembler *Decoder);
static DecodeStatus
DecodeRestrictedSPredicateOperand(MCInst &Inst, unsigned Val, uint64_t Address,
                                  const MCDisassembler *Decoder);
static DecodeStatus
DecodeRestrictedUPredicateOperand(MCInst &Inst, unsigned Val, uint64_t Address,
                                  const MCDisassembler *Decoder);
static DecodeStatus
DecodeRestrictedFPPredicateOperand(MCInst &Inst, unsigned Val, uint64_t Address,
                                   const MCDisassembler *Decoder);
template <bool Writeback>
static DecodeStatus DecodeVSTRVLDR_SYSREG(MCInst &Inst, unsigned Insn,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder);
template <int shift>
static DecodeStatus DecodeMVE_MEM_1_pre(MCInst &Inst, unsigned Val,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder);
template <int shift>
static DecodeStatus DecodeMVE_MEM_2_pre(MCInst &Inst, unsigned Val,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder);
template <int shift>
static DecodeStatus DecodeMVE_MEM_3_pre(MCInst &Inst, unsigned Val,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder);
template <unsigned MinLog, unsigned MaxLog>
static DecodeStatus DecodePowerTwoOperand(MCInst &Inst, unsigned Val,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder);
template <unsigned start>
static DecodeStatus
DecodeMVEPairVectorIndexOperand(MCInst &Inst, unsigned Val, uint64_t Address,
                                const MCDisassembler *Decoder);
static DecodeStatus DecodeMVEVMOVQtoDReg(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder);
static DecodeStatus DecodeMVEVMOVDRegtoQ(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder);
static DecodeStatus DecodeMVEVCVTt1fp(MCInst &Inst, unsigned Insn,
                                      uint64_t Address,
                                      const MCDisassembler *Decoder);
typedef DecodeStatus OperandDecoder(MCInst &Inst, unsigned Val,
                                    uint64_t Address,
                                    const MCDisassembler *Decoder);
template <bool scalar, OperandDecoder predicate_decoder>
static DecodeStatus DecodeMVEVCMP(MCInst &Inst, unsigned Insn, uint64_t Address,
                                  const MCDisassembler *Decoder);
static DecodeStatus DecodeMveVCTP(MCInst &Inst, unsigned Insn, uint64_t Address,
                                  const MCDisassembler *Decoder);
static DecodeStatus DecodeMVEVPNOT(MCInst &Inst, unsigned Insn,
                                   uint64_t Address,
                                   const MCDisassembler *Decoder);
static DecodeStatus
DecodeMVEOverlappingLongShift(MCInst &Inst, unsigned Insn, uint64_t Address,
                              const MCDisassembler *Decoder);
static DecodeStatus DecodeT2AddSubSPImm(MCInst &Inst, unsigned Insn,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder);
static DecodeStatus DecodeLazyLoadStoreMul(MCInst &Inst, unsigned Insn,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder);

#include "ARMGenDisassemblerTables.inc"

static MCDisassembler *createARMDisassembler(const Target &T,
                                             const MCSubtargetInfo &STI,
                                             MCContext &Ctx) {
  return new ARMDisassembler(STI, Ctx, T.createMCInstrInfo());
}

// Post-decoding checks
static DecodeStatus checkDecodedInstruction(MCInst &MI, uint64_t &Size,
                                            uint64_t Address, raw_ostream &CS,
                                            uint32_t Insn,
                                            DecodeStatus Result) {
  switch (MI.getOpcode()) {
    case ARM::HVC: {
      // HVC is undefined if condition = 0xf otherwise upredictable
      // if condition != 0xe
      uint32_t Cond = (Insn >> 28) & 0xF;
      if (Cond == 0xF)
        return MCDisassembler::Fail;
      if (Cond != 0xE)
        return MCDisassembler::SoftFail;
      return Result;
    }
    case ARM::t2ADDri:
    case ARM::t2ADDri12:
    case ARM::t2ADDrr:
    case ARM::t2ADDrs:
    case ARM::t2SUBri:
    case ARM::t2SUBri12:
    case ARM::t2SUBrr:
    case ARM::t2SUBrs:
      if (MI.getOperand(0).getReg() == ARM::SP &&
          MI.getOperand(1).getReg() != ARM::SP)
        return MCDisassembler::SoftFail;
      return Result;
    default: return Result;
  }
}

uint64_t ARMDisassembler::suggestBytesToSkip(ArrayRef<uint8_t> Bytes,
                                             uint64_t Address) const {
  // In Arm state, instructions are always 4 bytes wide, so there's no
  // point in skipping any smaller number of bytes if an instruction
  // can't be decoded.
  if (!STI.hasFeature(ARM::ModeThumb))
    return 4;

  // In a Thumb instruction stream, a halfword is a standalone 2-byte
  // instruction if and only if its value is less than 0xE800.
  // Otherwise, it's the first halfword of a 4-byte instruction.
  //
  // So, if we can see the upcoming halfword, we can judge on that
  // basis, and maybe skip a whole 4-byte instruction that we don't
  // know how to decode, without accidentally trying to interpret its
  // second half as something else.
  //
  // If we don't have the instruction data available, we just have to
  // recommend skipping the minimum sensible distance, which is 2
  // bytes.
  if (Bytes.size() < 2)
    return 2;

  uint16_t Insn16 = llvm::support::endian::read<uint16_t>(
      Bytes.data(), InstructionEndianness);
  return Insn16 < 0xE800 ? 2 : 4;
}

DecodeStatus ARMDisassembler::getInstruction(MCInst &MI, uint64_t &Size,
                                             ArrayRef<uint8_t> Bytes,
                                             uint64_t Address,
                                             raw_ostream &CS) const {
  if (STI.hasFeature(ARM::ModeThumb))
    return getThumbInstruction(MI, Size, Bytes, Address, CS);
  return getARMInstruction(MI, Size, Bytes, Address, CS);
}

DecodeStatus ARMDisassembler::getARMInstruction(MCInst &MI, uint64_t &Size,
                                                ArrayRef<uint8_t> Bytes,
                                                uint64_t Address,
                                                raw_ostream &CS) const {
  CommentStream = &CS;

  assert(!STI.hasFeature(ARM::ModeThumb) &&
         "Asked to disassemble an ARM instruction but Subtarget is in Thumb "
         "mode!");

  // We want to read exactly 4 bytes of data.
  if (Bytes.size() < 4) {
    Size = 0;
    return MCDisassembler::Fail;
  }

  // Encoded as a 32-bit word in the stream.
  uint32_t Insn = llvm::support::endian::read<uint32_t>(Bytes.data(),
                                                        InstructionEndianness);

  // Calling the auto-generated decoder function.
  DecodeStatus Result =
      decodeInstruction(DecoderTableARM32, MI, Insn, Address, this, STI);
  if (Result != MCDisassembler::Fail) {
    Size = 4;
    return checkDecodedInstruction(MI, Size, Address, CS, Insn, Result);
  }

  struct DecodeTable {
    const uint8_t *P;
    bool DecodePred;
  };

  const DecodeTable Tables[] = {
      {DecoderTableVFP32, false},      {DecoderTableVFPV832, false},
      {DecoderTableNEONData32, true},  {DecoderTableNEONLoadStore32, true},
      {DecoderTableNEONDup32, true},   {DecoderTablev8NEON32, false},
      {DecoderTablev8Crypto32, false},
  };

  for (auto Table : Tables) {
    Result = decodeInstruction(Table.P, MI, Insn, Address, this, STI);
    if (Result != MCDisassembler::Fail) {
      Size = 4;
      // Add a fake predicate operand, because we share these instruction
      // definitions with Thumb2 where these instructions are predicable.
      if (Table.DecodePred && !DecodePredicateOperand(MI, 0xE, Address, this))
        return MCDisassembler::Fail;
      return Result;
    }
  }

  Result =
      decodeInstruction(DecoderTableCoProc32, MI, Insn, Address, this, STI);
  if (Result != MCDisassembler::Fail) {
    Size = 4;
    return checkDecodedInstruction(MI, Size, Address, CS, Insn, Result);
  }

  Size = 4;
  return MCDisassembler::Fail;
}

/// tryAddingSymbolicOperand - trys to add a symbolic operand in place of the
/// immediate Value in the MCInst.  The immediate Value has had any PC
/// adjustment made by the caller.  If the instruction is a branch instruction
/// then isBranch is true, else false.  If the getOpInfo() function was set as
/// part of the setupForSymbolicDisassembly() call then that function is called
/// to get any symbolic information at the Address for this instruction.  If
/// that returns non-zero then the symbolic information it returns is used to
/// create an MCExpr and that is added as an operand to the MCInst.  If
/// getOpInfo() returns zero and isBranch is true then a symbol look up for
/// Value is done and if a symbol is found an MCExpr is created with that, else
/// an MCExpr with Value is created.  This function returns true if it adds an
/// operand to the MCInst and false otherwise.
static bool tryAddingSymbolicOperand(uint64_t Address, int32_t Value,
                                     bool isBranch, uint64_t InstSize,
                                     MCInst &MI,
                                     const MCDisassembler *Decoder) {
  // FIXME: Does it make sense for value to be negative?
  return Decoder->tryAddingSymbolicOperand(MI, (uint32_t)Value, Address,
                                           isBranch, /*Offset=*/0, /*OpSize=*/0,
                                           InstSize);
}

/// tryAddingPcLoadReferenceComment - trys to add a comment as to what is being
/// referenced by a load instruction with the base register that is the Pc.
/// These can often be values in a literal pool near the Address of the
/// instruction.  The Address of the instruction and its immediate Value are
/// used as a possible literal pool entry.  The SymbolLookUp call back will
/// return the name of a symbol referenced by the literal pool's entry if
/// the referenced address is that of a symbol.  Or it will return a pointer to
/// a literal 'C' string if the referenced address of the literal pool's entry
/// is an address into a section with 'C' string literals.
static void tryAddingPcLoadReferenceComment(uint64_t Address, int Value,
                                            const MCDisassembler *Decoder) {
  const MCDisassembler *Dis = static_cast<const MCDisassembler*>(Decoder);
  Dis->tryAddingPcLoadReferenceComment(Value, Address);
}

// Thumb1 instructions don't have explicit S bits.  Rather, they
// implicitly set CPSR.  Since it's not represented in the encoding, the
// auto-generated decoder won't inject the CPSR operand.  We need to fix
// that as a post-pass.
void ARMDisassembler::AddThumb1SBit(MCInst &MI, bool InITBlock) const {
  const MCInstrDesc &MCID = MCII->get(MI.getOpcode());
  MCInst::iterator I = MI.begin();
  for (unsigned i = 0; i < MCID.NumOperands; ++i, ++I) {
    if (I == MI.end()) break;
    if (MCID.operands()[i].isOptionalDef() &&
        MCID.operands()[i].RegClass == ARM::CCRRegClassID) {
      if (i > 0 && MCID.operands()[i - 1].isPredicate())
        continue;
      MI.insert(I, MCOperand::createReg(InITBlock ? 0 : ARM::CPSR));
      return;
    }
  }

  MI.insert(I, MCOperand::createReg(InITBlock ? 0 : ARM::CPSR));
}

bool ARMDisassembler::isVectorPredicable(const MCInst &MI) const {
  const MCInstrDesc &MCID = MCII->get(MI.getOpcode());
  for (unsigned i = 0; i < MCID.NumOperands; ++i) {
    if (ARM::isVpred(MCID.operands()[i].OperandType))
      return true;
  }
  return false;
}

// Most Thumb instructions don't have explicit predicates in the
// encoding, but rather get their predicates from IT context.  We need
// to fix up the predicate operands using this context information as a
// post-pass.
MCDisassembler::DecodeStatus
ARMDisassembler::AddThumbPredicate(MCInst &MI) const {
  MCDisassembler::DecodeStatus S = Success;

  const FeatureBitset &FeatureBits = getSubtargetInfo().getFeatureBits();

  // A few instructions actually have predicates encoded in them.  Don't
  // try to overwrite it if we're seeing one of those.
  switch (MI.getOpcode()) {
    case ARM::tBcc:
    case ARM::t2Bcc:
    case ARM::tCBZ:
    case ARM::tCBNZ:
    case ARM::tCPS:
    case ARM::t2CPS3p:
    case ARM::t2CPS2p:
    case ARM::t2CPS1p:
    case ARM::t2CSEL:
    case ARM::t2CSINC:
    case ARM::t2CSINV:
    case ARM::t2CSNEG:
    case ARM::tMOVSr:
    case ARM::tSETEND:
      // Some instructions (mostly conditional branches) are not
      // allowed in IT blocks.
      if (ITBlock.instrInITBlock())
        S = SoftFail;
      else
        return Success;
      break;
    case ARM::t2HINT:
      if (MI.getOperand(0).getImm() == 0x10 && (FeatureBits[ARM::FeatureRAS]) != 0)
        S = SoftFail;
      break;
    case ARM::tB:
    case ARM::t2B:
    case ARM::t2TBB:
    case ARM::t2TBH:
      // Some instructions (mostly unconditional branches) can
      // only appears at the end of, or outside of, an IT.
      if (ITBlock.instrInITBlock() && !ITBlock.instrLastInITBlock())
        S = SoftFail;
      break;
    default:
      break;
  }

  // Warn on non-VPT predicable instruction in a VPT block and a VPT
  // predicable instruction in an IT block
  if ((!isVectorPredicable(MI) && VPTBlock.instrInVPTBlock()) ||
      (isVectorPredicable(MI) && ITBlock.instrInITBlock()))
    S = SoftFail;

  // If we're in an IT/VPT block, base the predicate on that.  Otherwise,
  // assume a predicate of AL.
  unsigned CC = ARMCC::AL;
  unsigned VCC = ARMVCC::None;
  if (ITBlock.instrInITBlock()) {
    CC = ITBlock.getITCC();
    ITBlock.advanceITState();
  } else if (VPTBlock.instrInVPTBlock()) {
    VCC = VPTBlock.getVPTPred();
    VPTBlock.advanceVPTState();
  }

  const MCInstrDesc &MCID = MCII->get(MI.getOpcode());

  MCInst::iterator CCI = MI.begin();
  for (unsigned i = 0; i < MCID.NumOperands; ++i, ++CCI) {
    if (MCID.operands()[i].isPredicate() || CCI == MI.end())
      break;
  }

  if (MCID.isPredicable()) {
    CCI = MI.insert(CCI, MCOperand::createImm(CC));
    ++CCI;
    if (CC == ARMCC::AL)
      MI.insert(CCI, MCOperand::createReg(0));
    else
      MI.insert(CCI, MCOperand::createReg(ARM::CPSR));
  } else if (CC != ARMCC::AL) {
    Check(S, SoftFail);
  }

  MCInst::iterator VCCI = MI.begin();
  unsigned VCCPos;
  for (VCCPos = 0; VCCPos < MCID.NumOperands; ++VCCPos, ++VCCI) {
    if (ARM::isVpred(MCID.operands()[VCCPos].OperandType) || VCCI == MI.end())
      break;
  }

  if (isVectorPredicable(MI)) {
    VCCI = MI.insert(VCCI, MCOperand::createImm(VCC));
    ++VCCI;
    if (VCC == ARMVCC::None)
      VCCI = MI.insert(VCCI, MCOperand::createReg(0));
    else
      VCCI = MI.insert(VCCI, MCOperand::createReg(ARM::P0));
    ++VCCI;
    VCCI = MI.insert(VCCI, MCOperand::createReg(0));
    ++VCCI;
    if (MCID.operands()[VCCPos].OperandType == ARM::OPERAND_VPRED_R) {
      int TiedOp = MCID.getOperandConstraint(VCCPos + 3, MCOI::TIED_TO);
      assert(TiedOp >= 0 &&
             "Inactive register in vpred_r is not tied to an output!");
      // Copy the operand to ensure it's not invalidated when MI grows.
      MI.insert(VCCI, MCOperand(MI.getOperand(TiedOp)));
    }
  } else if (VCC != ARMVCC::None) {
    Check(S, SoftFail);
  }

  return S;
}

// Thumb VFP instructions are a special case.  Because we share their
// encodings between ARM and Thumb modes, and they are predicable in ARM
// mode, the auto-generated decoder will give them an (incorrect)
// predicate operand.  We need to rewrite these operands based on the IT
// context as a post-pass.
void ARMDisassembler::UpdateThumbVFPPredicate(
  DecodeStatus &S, MCInst &MI) const {
  unsigned CC;
  CC = ITBlock.getITCC();
  if (CC == 0xF)
    CC = ARMCC::AL;
  if (ITBlock.instrInITBlock())
    ITBlock.advanceITState();
  else if (VPTBlock.instrInVPTBlock()) {
    CC = VPTBlock.getVPTPred();
    VPTBlock.advanceVPTState();
  }

  const MCInstrDesc &MCID = MCII->get(MI.getOpcode());
  ArrayRef<MCOperandInfo> OpInfo = MCID.operands();
  MCInst::iterator I = MI.begin();
  unsigned short NumOps = MCID.NumOperands;
  for (unsigned i = 0; i < NumOps; ++i, ++I) {
    if (OpInfo[i].isPredicate() ) {
      if (CC != ARMCC::AL && !MCID.isPredicable())
        Check(S, SoftFail);
      I->setImm(CC);
      ++I;
      if (CC == ARMCC::AL)
        I->setReg(0);
      else
        I->setReg(ARM::CPSR);
      return;
    }
  }
}

DecodeStatus ARMDisassembler::getThumbInstruction(MCInst &MI, uint64_t &Size,
                                                  ArrayRef<uint8_t> Bytes,
                                                  uint64_t Address,
                                                  raw_ostream &CS) const {
  CommentStream = &CS;

  assert(STI.hasFeature(ARM::ModeThumb) &&
         "Asked to disassemble in Thumb mode but Subtarget is in ARM mode!");

  // We want to read exactly 2 bytes of data.
  if (Bytes.size() < 2) {
    Size = 0;
    return MCDisassembler::Fail;
  }

  uint16_t Insn16 = llvm::support::endian::read<uint16_t>(
      Bytes.data(), InstructionEndianness);
  DecodeStatus Result =
      decodeInstruction(DecoderTableThumb16, MI, Insn16, Address, this, STI);
  if (Result != MCDisassembler::Fail) {
    Size = 2;
    Check(Result, AddThumbPredicate(MI));
    return Result;
  }

  Result = decodeInstruction(DecoderTableThumbSBit16, MI, Insn16, Address, this,
                             STI);
  if (Result) {
    Size = 2;
    bool InITBlock = ITBlock.instrInITBlock();
    Check(Result, AddThumbPredicate(MI));
    AddThumb1SBit(MI, InITBlock);
    return Result;
  }

  Result =
      decodeInstruction(DecoderTableThumb216, MI, Insn16, Address, this, STI);
  if (Result != MCDisassembler::Fail) {
    Size = 2;

    // Nested IT blocks are UNPREDICTABLE.  Must be checked before we add
    // the Thumb predicate.
    if (MI.getOpcode() == ARM::t2IT && ITBlock.instrInITBlock())
      Result = MCDisassembler::SoftFail;

    Check(Result, AddThumbPredicate(MI));

    // If we find an IT instruction, we need to parse its condition
    // code and mask operands so that we can apply them correctly
    // to the subsequent instructions.
    if (MI.getOpcode() == ARM::t2IT) {
      unsigned Firstcond = MI.getOperand(0).getImm();
      unsigned Mask = MI.getOperand(1).getImm();
      ITBlock.setITState(Firstcond, Mask);

      // An IT instruction that would give a 'NV' predicate is unpredictable.
      if (Firstcond == ARMCC::AL && !isPowerOf2_32(Mask))
        CS << "unpredictable IT predicate sequence";
    }

    return Result;
  }

  // We want to read exactly 4 bytes of data.
  if (Bytes.size() < 4) {
    Size = 0;
    return MCDisassembler::Fail;
  }

  uint32_t Insn32 =
      (uint32_t(Insn16) << 16) | llvm::support::endian::read<uint16_t>(
                                     Bytes.data() + 2, InstructionEndianness);

  Result =
      decodeInstruction(DecoderTableMVE32, MI, Insn32, Address, this, STI);
  if (Result != MCDisassembler::Fail) {
    Size = 4;

    // Nested VPT blocks are UNPREDICTABLE. Must be checked before we add
    // the VPT predicate.
    if (isVPTOpcode(MI.getOpcode()) && VPTBlock.instrInVPTBlock())
      Result = MCDisassembler::SoftFail;

    Check(Result, AddThumbPredicate(MI));

    if (isVPTOpcode(MI.getOpcode())) {
      unsigned Mask = MI.getOperand(0).getImm();
      VPTBlock.setVPTState(Mask);
    }

    return Result;
  }

  Result =
      decodeInstruction(DecoderTableThumb32, MI, Insn32, Address, this, STI);
  if (Result != MCDisassembler::Fail) {
    Size = 4;
    bool InITBlock = ITBlock.instrInITBlock();
    Check(Result, AddThumbPredicate(MI));
    AddThumb1SBit(MI, InITBlock);
    return Result;
  }

  Result =
      decodeInstruction(DecoderTableThumb232, MI, Insn32, Address, this, STI);
  if (Result != MCDisassembler::Fail) {
    Size = 4;
    Check(Result, AddThumbPredicate(MI));
    return checkDecodedInstruction(MI, Size, Address, CS, Insn32, Result);
  }

  if (fieldFromInstruction(Insn32, 28, 4) == 0xE) {
    Result =
        decodeInstruction(DecoderTableVFP32, MI, Insn32, Address, this, STI);
    if (Result != MCDisassembler::Fail) {
      Size = 4;
      UpdateThumbVFPPredicate(Result, MI);
      return Result;
    }
  }

  Result =
      decodeInstruction(DecoderTableVFPV832, MI, Insn32, Address, this, STI);
  if (Result != MCDisassembler::Fail) {
    Size = 4;
    return Result;
  }

  if (fieldFromInstruction(Insn32, 28, 4) == 0xE) {
    Result = decodeInstruction(DecoderTableNEONDup32, MI, Insn32, Address, this,
                               STI);
    if (Result != MCDisassembler::Fail) {
      Size = 4;
      Check(Result, AddThumbPredicate(MI));
      return Result;
    }
  }

  if (fieldFromInstruction(Insn32, 24, 8) == 0xF9) {
    uint32_t NEONLdStInsn = Insn32;
    NEONLdStInsn &= 0xF0FFFFFF;
    NEONLdStInsn |= 0x04000000;
    Result = decodeInstruction(DecoderTableNEONLoadStore32, MI, NEONLdStInsn,
                               Address, this, STI);
    if (Result != MCDisassembler::Fail) {
      Size = 4;
      Check(Result, AddThumbPredicate(MI));
      return Result;
    }
  }

  if (fieldFromInstruction(Insn32, 24, 4) == 0xF) {
    uint32_t NEONDataInsn = Insn32;
    NEONDataInsn &= 0xF0FFFFFF; // Clear bits 27-24
    NEONDataInsn |= (NEONDataInsn & 0x10000000) >> 4; // Move bit 28 to bit 24
    NEONDataInsn |= 0x12000000; // Set bits 28 and 25
    Result = decodeInstruction(DecoderTableNEONData32, MI, NEONDataInsn,
                               Address, this, STI);
    if (Result != MCDisassembler::Fail) {
      Size = 4;
      Check(Result, AddThumbPredicate(MI));
      return Result;
    }

    uint32_t NEONCryptoInsn = Insn32;
    NEONCryptoInsn &= 0xF0FFFFFF; // Clear bits 27-24
    NEONCryptoInsn |= (NEONCryptoInsn & 0x10000000) >> 4; // Move bit 28 to bit 24
    NEONCryptoInsn |= 0x12000000; // Set bits 28 and 25
    Result = decodeInstruction(DecoderTablev8Crypto32, MI, NEONCryptoInsn,
                               Address, this, STI);
    if (Result != MCDisassembler::Fail) {
      Size = 4;
      return Result;
    }

    uint32_t NEONv8Insn = Insn32;
    NEONv8Insn &= 0xF3FFFFFF; // Clear bits 27-26
    Result = decodeInstruction(DecoderTablev8NEON32, MI, NEONv8Insn, Address,
                               this, STI);
    if (Result != MCDisassembler::Fail) {
      Size = 4;
      return Result;
    }
  }

  uint32_t Coproc = fieldFromInstruction(Insn32, 8, 4);
  const uint8_t *DecoderTable = ARM::isCDECoproc(Coproc, STI)
                                    ? DecoderTableThumb2CDE32
                                    : DecoderTableThumb2CoProc32;
  Result =
      decodeInstruction(DecoderTable, MI, Insn32, Address, this, STI);
  if (Result != MCDisassembler::Fail) {
    Size = 4;
    Check(Result, AddThumbPredicate(MI));
    return Result;
  }

  Size = 0;
  return MCDisassembler::Fail;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeARMDisassembler() {
  TargetRegistry::RegisterMCDisassembler(getTheARMLETarget(),
                                         createARMDisassembler);
  TargetRegistry::RegisterMCDisassembler(getTheARMBETarget(),
                                         createARMDisassembler);
  TargetRegistry::RegisterMCDisassembler(getTheThumbLETarget(),
                                         createARMDisassembler);
  TargetRegistry::RegisterMCDisassembler(getTheThumbBETarget(),
                                         createARMDisassembler);
}

static const uint16_t GPRDecoderTable[] = {
  ARM::R0, ARM::R1, ARM::R2, ARM::R3,
  ARM::R4, ARM::R5, ARM::R6, ARM::R7,
  ARM::R8, ARM::R9, ARM::R10, ARM::R11,
  ARM::R12, ARM::SP, ARM::LR, ARM::PC
};

static const uint16_t CLRMGPRDecoderTable[] = {
  ARM::R0, ARM::R1, ARM::R2, ARM::R3,
  ARM::R4, ARM::R5, ARM::R6, ARM::R7,
  ARM::R8, ARM::R9, ARM::R10, ARM::R11,
  ARM::R12, 0, ARM::LR, ARM::APSR
};

static DecodeStatus DecodeGPRRegisterClass(MCInst &Inst, unsigned RegNo,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder) {
  if (RegNo > 15)
    return MCDisassembler::Fail;

  unsigned Register = GPRDecoderTable[RegNo];
  Inst.addOperand(MCOperand::createReg(Register));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeCLRMGPRRegisterClass(MCInst &Inst, unsigned RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  if (RegNo > 15)
    return MCDisassembler::Fail;

  unsigned Register = CLRMGPRDecoderTable[RegNo];
  if (Register == 0)
    return MCDisassembler::Fail;

  Inst.addOperand(MCOperand::createReg(Register));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeGPRnopcRegisterClass(MCInst &Inst, unsigned RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  if (RegNo == 15)
    S = MCDisassembler::SoftFail;

  Check(S, DecodeGPRRegisterClass(Inst, RegNo, Address, Decoder));

  return S;
}

static DecodeStatus DecodeGPRnospRegisterClass(MCInst &Inst, unsigned RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  if (RegNo == 13)
    S = MCDisassembler::SoftFail;

  Check(S, DecodeGPRRegisterClass(Inst, RegNo, Address, Decoder));

  return S;
}

static DecodeStatus
DecodeGPRwithAPSRRegisterClass(MCInst &Inst, unsigned RegNo, uint64_t Address,
                               const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  if (RegNo == 15)
  {
    Inst.addOperand(MCOperand::createReg(ARM::APSR_NZCV));
    return MCDisassembler::Success;
  }

  Check(S, DecodeGPRRegisterClass(Inst, RegNo, Address, Decoder));
  return S;
}

static DecodeStatus
DecodeGPRwithZRRegisterClass(MCInst &Inst, unsigned RegNo, uint64_t Address,
                             const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  if (RegNo == 15)
  {
    Inst.addOperand(MCOperand::createReg(ARM::ZR));
    return MCDisassembler::Success;
  }

  if (RegNo == 13)
    Check(S, MCDisassembler::SoftFail);

  Check(S, DecodeGPRRegisterClass(Inst, RegNo, Address, Decoder));
  return S;
}

static DecodeStatus
DecodeGPRwithZRnospRegisterClass(MCInst &Inst, unsigned RegNo, uint64_t Address,
                                 const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;
  if (RegNo == 13)
    return MCDisassembler::Fail;
  Check(S, DecodeGPRwithZRRegisterClass(Inst, RegNo, Address, Decoder));
  return S;
}

static DecodeStatus DecodetGPRRegisterClass(MCInst &Inst, unsigned RegNo,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder) {
  if (RegNo > 7)
    return MCDisassembler::Fail;
  return DecodeGPRRegisterClass(Inst, RegNo, Address, Decoder);
}

static const uint16_t GPRPairDecoderTable[] = {
  ARM::R0_R1, ARM::R2_R3,   ARM::R4_R5,  ARM::R6_R7,
  ARM::R8_R9, ARM::R10_R11, ARM::R12_SP
};

static DecodeStatus DecodeGPRPairRegisterClass(MCInst &Inst, unsigned RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  // According to the Arm ARM RegNo = 14 is undefined, but we return fail
  // rather than SoftFail as there is no GPRPair table entry for index 7.
  if (RegNo > 13)
    return MCDisassembler::Fail;

  if (RegNo & 1)
     S = MCDisassembler::SoftFail;

  unsigned RegisterPair = GPRPairDecoderTable[RegNo/2];
  Inst.addOperand(MCOperand::createReg(RegisterPair));
  return S;
}

static DecodeStatus
DecodeGPRPairnospRegisterClass(MCInst &Inst, unsigned RegNo, uint64_t Address,
                               const MCDisassembler *Decoder) {
  if (RegNo > 13)
    return MCDisassembler::Fail;

  unsigned RegisterPair = GPRPairDecoderTable[RegNo/2];
  Inst.addOperand(MCOperand::createReg(RegisterPair));

  if ((RegNo & 1) || RegNo > 10)
     return MCDisassembler::SoftFail;
  return MCDisassembler::Success;
}

static DecodeStatus DecodeGPRspRegisterClass(MCInst &Inst, unsigned RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  if (RegNo != 13)
    return MCDisassembler::Fail;

  unsigned Register = GPRDecoderTable[RegNo];
  Inst.addOperand(MCOperand::createReg(Register));
  return MCDisassembler::Success;
}

static DecodeStatus DecodetcGPRRegisterClass(MCInst &Inst, unsigned RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  unsigned Register = 0;
  switch (RegNo) {
    case 0:
      Register = ARM::R0;
      break;
    case 1:
      Register = ARM::R1;
      break;
    case 2:
      Register = ARM::R2;
      break;
    case 3:
      Register = ARM::R3;
      break;
    case 9:
      Register = ARM::R9;
      break;
    case 12:
      Register = ARM::R12;
      break;
    default:
      return MCDisassembler::Fail;
    }

  Inst.addOperand(MCOperand::createReg(Register));
  return MCDisassembler::Success;
}

static DecodeStatus DecoderGPRRegisterClass(MCInst &Inst, unsigned RegNo,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  const FeatureBitset &featureBits =
    ((const MCDisassembler*)Decoder)->getSubtargetInfo().getFeatureBits();

  if ((RegNo == 13 && !featureBits[ARM::HasV8Ops]) || RegNo == 15)
    S = MCDisassembler::SoftFail;

  Check(S, DecodeGPRRegisterClass(Inst, RegNo, Address, Decoder));
  return S;
}

static const uint16_t SPRDecoderTable[] = {
     ARM::S0,  ARM::S1,  ARM::S2,  ARM::S3,
     ARM::S4,  ARM::S5,  ARM::S6,  ARM::S7,
     ARM::S8,  ARM::S9, ARM::S10, ARM::S11,
    ARM::S12, ARM::S13, ARM::S14, ARM::S15,
    ARM::S16, ARM::S17, ARM::S18, ARM::S19,
    ARM::S20, ARM::S21, ARM::S22, ARM::S23,
    ARM::S24, ARM::S25, ARM::S26, ARM::S27,
    ARM::S28, ARM::S29, ARM::S30, ARM::S31
};

static DecodeStatus DecodeSPRRegisterClass(MCInst &Inst, unsigned RegNo,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder) {
  if (RegNo > 31)
    return MCDisassembler::Fail;

  unsigned Register = SPRDecoderTable[RegNo];
  Inst.addOperand(MCOperand::createReg(Register));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeHPRRegisterClass(MCInst &Inst, unsigned RegNo,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder) {
  return DecodeSPRRegisterClass(Inst, RegNo, Address, Decoder);
}

static const uint16_t DPRDecoderTable[] = {
     ARM::D0,  ARM::D1,  ARM::D2,  ARM::D3,
     ARM::D4,  ARM::D5,  ARM::D6,  ARM::D7,
     ARM::D8,  ARM::D9, ARM::D10, ARM::D11,
    ARM::D12, ARM::D13, ARM::D14, ARM::D15,
    ARM::D16, ARM::D17, ARM::D18, ARM::D19,
    ARM::D20, ARM::D21, ARM::D22, ARM::D23,
    ARM::D24, ARM::D25, ARM::D26, ARM::D27,
    ARM::D28, ARM::D29, ARM::D30, ARM::D31
};

static DecodeStatus DecodeDPRRegisterClass(MCInst &Inst, unsigned RegNo,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder) {
  const FeatureBitset &featureBits =
    ((const MCDisassembler*)Decoder)->getSubtargetInfo().getFeatureBits();

  bool hasD32 = featureBits[ARM::FeatureD32];

  if (RegNo > 31 || (!hasD32 && RegNo > 15))
    return MCDisassembler::Fail;

  unsigned Register = DPRDecoderTable[RegNo];
  Inst.addOperand(MCOperand::createReg(Register));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeDPR_8RegisterClass(MCInst &Inst, unsigned RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  if (RegNo > 7)
    return MCDisassembler::Fail;
  return DecodeDPRRegisterClass(Inst, RegNo, Address, Decoder);
}

static DecodeStatus DecodeSPR_8RegisterClass(MCInst &Inst, unsigned RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  if (RegNo > 15)
    return MCDisassembler::Fail;
  return DecodeSPRRegisterClass(Inst, RegNo, Address, Decoder);
}

static DecodeStatus DecodeDPR_VFP2RegisterClass(MCInst &Inst, unsigned RegNo,
                                                uint64_t Address,
                                                const MCDisassembler *Decoder) {
  if (RegNo > 15)
    return MCDisassembler::Fail;
  return DecodeDPRRegisterClass(Inst, RegNo, Address, Decoder);
}

static const uint16_t QPRDecoderTable[] = {
     ARM::Q0,  ARM::Q1,  ARM::Q2,  ARM::Q3,
     ARM::Q4,  ARM::Q5,  ARM::Q6,  ARM::Q7,
     ARM::Q8,  ARM::Q9, ARM::Q10, ARM::Q11,
    ARM::Q12, ARM::Q13, ARM::Q14, ARM::Q15
};

static DecodeStatus DecodeQPRRegisterClass(MCInst &Inst, unsigned RegNo,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder) {
  if (RegNo > 31 || (RegNo & 1) != 0)
    return MCDisassembler::Fail;
  RegNo >>= 1;

  unsigned Register = QPRDecoderTable[RegNo];
  Inst.addOperand(MCOperand::createReg(Register));
  return MCDisassembler::Success;
}

static const uint16_t DPairDecoderTable[] = {
  ARM::Q0,  ARM::D1_D2,   ARM::Q1,  ARM::D3_D4,   ARM::Q2,  ARM::D5_D6,
  ARM::Q3,  ARM::D7_D8,   ARM::Q4,  ARM::D9_D10,  ARM::Q5,  ARM::D11_D12,
  ARM::Q6,  ARM::D13_D14, ARM::Q7,  ARM::D15_D16, ARM::Q8,  ARM::D17_D18,
  ARM::Q9,  ARM::D19_D20, ARM::Q10, ARM::D21_D22, ARM::Q11, ARM::D23_D24,
  ARM::Q12, ARM::D25_D26, ARM::Q13, ARM::D27_D28, ARM::Q14, ARM::D29_D30,
  ARM::Q15
};

static DecodeStatus DecodeDPairRegisterClass(MCInst &Inst, unsigned RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  if (RegNo > 30)
    return MCDisassembler::Fail;

  unsigned Register = DPairDecoderTable[RegNo];
  Inst.addOperand(MCOperand::createReg(Register));
  return MCDisassembler::Success;
}

static const uint16_t DPairSpacedDecoderTable[] = {
  ARM::D0_D2,   ARM::D1_D3,   ARM::D2_D4,   ARM::D3_D5,
  ARM::D4_D6,   ARM::D5_D7,   ARM::D6_D8,   ARM::D7_D9,
  ARM::D8_D10,  ARM::D9_D11,  ARM::D10_D12, ARM::D11_D13,
  ARM::D12_D14, ARM::D13_D15, ARM::D14_D16, ARM::D15_D17,
  ARM::D16_D18, ARM::D17_D19, ARM::D18_D20, ARM::D19_D21,
  ARM::D20_D22, ARM::D21_D23, ARM::D22_D24, ARM::D23_D25,
  ARM::D24_D26, ARM::D25_D27, ARM::D26_D28, ARM::D27_D29,
  ARM::D28_D30, ARM::D29_D31
};

static DecodeStatus
DecodeDPairSpacedRegisterClass(MCInst &Inst, unsigned RegNo, uint64_t Address,
                               const MCDisassembler *Decoder) {
  if (RegNo > 29)
    return MCDisassembler::Fail;

  unsigned Register = DPairSpacedDecoderTable[RegNo];
  Inst.addOperand(MCOperand::createReg(Register));
  return MCDisassembler::Success;
}

static DecodeStatus DecodePredicateOperand(MCInst &Inst, unsigned Val,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;
  if (Val == 0xF) return MCDisassembler::Fail;
  // AL predicate is not allowed on Thumb1 branches.
  if (Inst.getOpcode() == ARM::tBcc && Val == 0xE)
    return MCDisassembler::Fail;
  const MCInstrInfo *MCII =
      static_cast<const ARMDisassembler *>(Decoder)->MCII.get();
  if (Val != ARMCC::AL && !MCII->get(Inst.getOpcode()).isPredicable())
    Check(S, MCDisassembler::SoftFail);
  Inst.addOperand(MCOperand::createImm(Val));
  if (Val == ARMCC::AL) {
    Inst.addOperand(MCOperand::createReg(0));
  } else
    Inst.addOperand(MCOperand::createReg(ARM::CPSR));
  return S;
}

static DecodeStatus DecodeCCOutOperand(MCInst &Inst, unsigned Val,
                                       uint64_t Address,
                                       const MCDisassembler *Decoder) {
  if (Val)
    Inst.addOperand(MCOperand::createReg(ARM::CPSR));
  else
    Inst.addOperand(MCOperand::createReg(0));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeSORegImmOperand(MCInst &Inst, unsigned Val,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rm = fieldFromInstruction(Val, 0, 4);
  unsigned type = fieldFromInstruction(Val, 5, 2);
  unsigned imm = fieldFromInstruction(Val, 7, 5);

  // Register-immediate
  if (!Check(S, DecoderGPRRegisterClass(Inst, Rm, Address, Decoder)))
    return MCDisassembler::Fail;

  ARM_AM::ShiftOpc Shift = ARM_AM::lsl;
  switch (type) {
    case 0:
      Shift = ARM_AM::lsl;
      break;
    case 1:
      Shift = ARM_AM::lsr;
      break;
    case 2:
      Shift = ARM_AM::asr;
      break;
    case 3:
      Shift = ARM_AM::ror;
      break;
  }

  if (Shift == ARM_AM::ror && imm == 0)
    Shift = ARM_AM::rrx;

  unsigned Op = Shift | (imm << 3);
  Inst.addOperand(MCOperand::createImm(Op));

  return S;
}

static DecodeStatus DecodeSORegRegOperand(MCInst &Inst, unsigned Val,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rm = fieldFromInstruction(Val, 0, 4);
  unsigned type = fieldFromInstruction(Val, 5, 2);
  unsigned Rs = fieldFromInstruction(Val, 8, 4);

  // Register-register
  if (!Check(S, DecodeGPRnopcRegisterClass(Inst, Rm, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeGPRnopcRegisterClass(Inst, Rs, Address, Decoder)))
    return MCDisassembler::Fail;

  ARM_AM::ShiftOpc Shift = ARM_AM::lsl;
  switch (type) {
    case 0:
      Shift = ARM_AM::lsl;
      break;
    case 1:
      Shift = ARM_AM::lsr;
      break;
    case 2:
      Shift = ARM_AM::asr;
      break;
    case 3:
      Shift = ARM_AM::ror;
      break;
  }

  Inst.addOperand(MCOperand::createImm(Shift));

  return S;
}

static DecodeStatus DecodeRegListOperand(MCInst &Inst, unsigned Val,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  bool NeedDisjointWriteback = false;
  unsigned WritebackReg = 0;
  bool CLRM = false;
  switch (Inst.getOpcode()) {
  default:
    break;
  case ARM::LDMIA_UPD:
  case ARM::LDMDB_UPD:
  case ARM::LDMIB_UPD:
  case ARM::LDMDA_UPD:
  case ARM::t2LDMIA_UPD:
  case ARM::t2LDMDB_UPD:
  case ARM::t2STMIA_UPD:
  case ARM::t2STMDB_UPD:
    NeedDisjointWriteback = true;
    WritebackReg = Inst.getOperand(0).getReg();
    break;
  case ARM::t2CLRM:
    CLRM = true;
    break;
  }

  // Empty register lists are not allowed.
  if (Val == 0) return MCDisassembler::Fail;
  for (unsigned i = 0; i < 16; ++i) {
    if (Val & (1 << i)) {
      if (CLRM) {
        if (!Check(S, DecodeCLRMGPRRegisterClass(Inst, i, Address, Decoder))) {
          return MCDisassembler::Fail;
        }
      } else {
        if (!Check(S, DecodeGPRRegisterClass(Inst, i, Address, Decoder)))
          return MCDisassembler::Fail;
        // Writeback not allowed if Rn is in the target list.
        if (NeedDisjointWriteback && WritebackReg == Inst.end()[-1].getReg())
          Check(S, MCDisassembler::SoftFail);
      }
    }
  }

  return S;
}

static DecodeStatus DecodeSPRRegListOperand(MCInst &Inst, unsigned Val,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Vd = fieldFromInstruction(Val, 8, 5);
  unsigned regs = fieldFromInstruction(Val, 0, 8);

  // In case of unpredictable encoding, tweak the operands.
  if (regs == 0 || (Vd + regs) > 32) {
    regs = Vd + regs > 32 ? 32 - Vd : regs;
    regs = std::max( 1u, regs);
    S = MCDisassembler::SoftFail;
  }

  if (!Check(S, DecodeSPRRegisterClass(Inst, Vd, Address, Decoder)))
    return MCDisassembler::Fail;
  for (unsigned i = 0; i < (regs - 1); ++i) {
    if (!Check(S, DecodeSPRRegisterClass(Inst, ++Vd, Address, Decoder)))
      return MCDisassembler::Fail;
  }

  return S;
}

static DecodeStatus DecodeDPRRegListOperand(MCInst &Inst, unsigned Val,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Vd = fieldFromInstruction(Val, 8, 5);
  unsigned regs = fieldFromInstruction(Val, 1, 7);

  // In case of unpredictable encoding, tweak the operands.
  if (regs == 0 || regs > 16 || (Vd + regs) > 32) {
    regs = Vd + regs > 32 ? 32 - Vd : regs;
    regs = std::max( 1u, regs);
    regs = std::min(16u, regs);
    S = MCDisassembler::SoftFail;
  }

  if (!Check(S, DecodeDPRRegisterClass(Inst, Vd, Address, Decoder)))
      return MCDisassembler::Fail;
  for (unsigned i = 0; i < (regs - 1); ++i) {
    if (!Check(S, DecodeDPRRegisterClass(Inst, ++Vd, Address, Decoder)))
      return MCDisassembler::Fail;
  }

  return S;
}

static DecodeStatus DecodeBitfieldMaskOperand(MCInst &Inst, unsigned Val,
                                              uint64_t Address,
                                              const MCDisassembler *Decoder) {
  // This operand encodes a mask of contiguous zeros between a specified MSB
  // and LSB.  To decode it, we create the mask of all bits MSB-and-lower,
  // the mask of all bits LSB-and-lower, and then xor them to create
  // the mask of that's all ones on [msb, lsb].  Finally we not it to
  // create the final mask.
  unsigned msb = fieldFromInstruction(Val, 5, 5);
  unsigned lsb = fieldFromInstruction(Val, 0, 5);

  DecodeStatus S = MCDisassembler::Success;
  if (lsb > msb) {
    Check(S, MCDisassembler::SoftFail);
    // The check above will cause the warning for the "potentially undefined
    // instruction encoding" but we can't build a bad MCOperand value here
    // with a lsb > msb or else printing the MCInst will cause a crash.
    lsb = msb;
  }

  uint32_t msb_mask = 0xFFFFFFFF;
  if (msb != 31) msb_mask = (1U << (msb+1)) - 1;
  uint32_t lsb_mask = (1U << lsb) - 1;

  Inst.addOperand(MCOperand::createImm(~(msb_mask ^ lsb_mask)));
  return S;
}

static DecodeStatus DecodeCopMemInstruction(MCInst &Inst, unsigned Insn,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned pred = fieldFromInstruction(Insn, 28, 4);
  unsigned CRd = fieldFromInstruction(Insn, 12, 4);
  unsigned coproc = fieldFromInstruction(Insn, 8, 4);
  unsigned imm = fieldFromInstruction(Insn, 0, 8);
  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  unsigned U = fieldFromInstruction(Insn, 23, 1);
  const FeatureBitset &featureBits =
    ((const MCDisassembler*)Decoder)->getSubtargetInfo().getFeatureBits();

  switch (Inst.getOpcode()) {
    case ARM::LDC_OFFSET:
    case ARM::LDC_PRE:
    case ARM::LDC_POST:
    case ARM::LDC_OPTION:
    case ARM::LDCL_OFFSET:
    case ARM::LDCL_PRE:
    case ARM::LDCL_POST:
    case ARM::LDCL_OPTION:
    case ARM::STC_OFFSET:
    case ARM::STC_PRE:
    case ARM::STC_POST:
    case ARM::STC_OPTION:
    case ARM::STCL_OFFSET:
    case ARM::STCL_PRE:
    case ARM::STCL_POST:
    case ARM::STCL_OPTION:
    case ARM::t2LDC_OFFSET:
    case ARM::t2LDC_PRE:
    case ARM::t2LDC_POST:
    case ARM::t2LDC_OPTION:
    case ARM::t2LDCL_OFFSET:
    case ARM::t2LDCL_PRE:
    case ARM::t2LDCL_POST:
    case ARM::t2LDCL_OPTION:
    case ARM::t2STC_OFFSET:
    case ARM::t2STC_PRE:
    case ARM::t2STC_POST:
    case ARM::t2STC_OPTION:
    case ARM::t2STCL_OFFSET:
    case ARM::t2STCL_PRE:
    case ARM::t2STCL_POST:
    case ARM::t2STCL_OPTION:
    case ARM::t2LDC2_OFFSET:
    case ARM::t2LDC2L_OFFSET:
    case ARM::t2LDC2_PRE:
    case ARM::t2LDC2L_PRE:
    case ARM::t2STC2_OFFSET:
    case ARM::t2STC2L_OFFSET:
    case ARM::t2STC2_PRE:
    case ARM::t2STC2L_PRE:
    case ARM::LDC2_OFFSET:
    case ARM::LDC2L_OFFSET:
    case ARM::LDC2_PRE:
    case ARM::LDC2L_PRE:
    case ARM::STC2_OFFSET:
    case ARM::STC2L_OFFSET:
    case ARM::STC2_PRE:
    case ARM::STC2L_PRE:
    case ARM::t2LDC2_OPTION:
    case ARM::t2STC2_OPTION:
    case ARM::t2LDC2_POST:
    case ARM::t2LDC2L_POST:
    case ARM::t2STC2_POST:
    case ARM::t2STC2L_POST:
    case ARM::LDC2_POST:
    case ARM::LDC2L_POST:
    case ARM::STC2_POST:
    case ARM::STC2L_POST:
      if (coproc == 0xA || coproc == 0xB ||
          (featureBits[ARM::HasV8_1MMainlineOps] &&
           (coproc == 0x8 || coproc == 0x9 || coproc == 0xA || coproc == 0xB ||
            coproc == 0xE || coproc == 0xF)))
        return MCDisassembler::Fail;
      break;
    default:
      break;
  }

  if (featureBits[ARM::HasV8Ops] && (coproc != 14))
    return MCDisassembler::Fail;

  Inst.addOperand(MCOperand::createImm(coproc));
  Inst.addOperand(MCOperand::createImm(CRd));
  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;

  switch (Inst.getOpcode()) {
    case ARM::t2LDC2_OFFSET:
    case ARM::t2LDC2L_OFFSET:
    case ARM::t2LDC2_PRE:
    case ARM::t2LDC2L_PRE:
    case ARM::t2STC2_OFFSET:
    case ARM::t2STC2L_OFFSET:
    case ARM::t2STC2_PRE:
    case ARM::t2STC2L_PRE:
    case ARM::LDC2_OFFSET:
    case ARM::LDC2L_OFFSET:
    case ARM::LDC2_PRE:
    case ARM::LDC2L_PRE:
    case ARM::STC2_OFFSET:
    case ARM::STC2L_OFFSET:
    case ARM::STC2_PRE:
    case ARM::STC2L_PRE:
    case ARM::t2LDC_OFFSET:
    case ARM::t2LDCL_OFFSET:
    case ARM::t2LDC_PRE:
    case ARM::t2LDCL_PRE:
    case ARM::t2STC_OFFSET:
    case ARM::t2STCL_OFFSET:
    case ARM::t2STC_PRE:
    case ARM::t2STCL_PRE:
    case ARM::LDC_OFFSET:
    case ARM::LDCL_OFFSET:
    case ARM::LDC_PRE:
    case ARM::LDCL_PRE:
    case ARM::STC_OFFSET:
    case ARM::STCL_OFFSET:
    case ARM::STC_PRE:
    case ARM::STCL_PRE:
      imm = ARM_AM::getAM5Opc(U ? ARM_AM::add : ARM_AM::sub, imm);
      Inst.addOperand(MCOperand::createImm(imm));
      break;
    case ARM::t2LDC2_POST:
    case ARM::t2LDC2L_POST:
    case ARM::t2STC2_POST:
    case ARM::t2STC2L_POST:
    case ARM::LDC2_POST:
    case ARM::LDC2L_POST:
    case ARM::STC2_POST:
    case ARM::STC2L_POST:
    case ARM::t2LDC_POST:
    case ARM::t2LDCL_POST:
    case ARM::t2STC_POST:
    case ARM::t2STCL_POST:
    case ARM::LDC_POST:
    case ARM::LDCL_POST:
    case ARM::STC_POST:
    case ARM::STCL_POST:
      imm |= U << 8;
      [[fallthrough]];
    default:
      // The 'option' variant doesn't encode 'U' in the immediate since
      // the immediate is unsigned [0,255].
      Inst.addOperand(MCOperand::createImm(imm));
      break;
  }

  switch (Inst.getOpcode()) {
    case ARM::LDC_OFFSET:
    case ARM::LDC_PRE:
    case ARM::LDC_POST:
    case ARM::LDC_OPTION:
    case ARM::LDCL_OFFSET:
    case ARM::LDCL_PRE:
    case ARM::LDCL_POST:
    case ARM::LDCL_OPTION:
    case ARM::STC_OFFSET:
    case ARM::STC_PRE:
    case ARM::STC_POST:
    case ARM::STC_OPTION:
    case ARM::STCL_OFFSET:
    case ARM::STCL_PRE:
    case ARM::STCL_POST:
    case ARM::STCL_OPTION:
      if (!Check(S, DecodePredicateOperand(Inst, pred, Address, Decoder)))
        return MCDisassembler::Fail;
      break;
    default:
      break;
  }

  return S;
}

static DecodeStatus
DecodeAddrMode2IdxInstruction(MCInst &Inst, unsigned Insn, uint64_t Address,
                              const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  unsigned Rt = fieldFromInstruction(Insn, 12, 4);
  unsigned Rm = fieldFromInstruction(Insn, 0, 4);
  unsigned imm = fieldFromInstruction(Insn, 0, 12);
  unsigned pred = fieldFromInstruction(Insn, 28, 4);
  unsigned reg = fieldFromInstruction(Insn, 25, 1);
  unsigned P = fieldFromInstruction(Insn, 24, 1);
  unsigned W = fieldFromInstruction(Insn, 21, 1);

  // On stores, the writeback operand precedes Rt.
  switch (Inst.getOpcode()) {
    case ARM::STR_POST_IMM:
    case ARM::STR_POST_REG:
    case ARM::STRB_POST_IMM:
    case ARM::STRB_POST_REG:
    case ARM::STRT_POST_REG:
    case ARM::STRT_POST_IMM:
    case ARM::STRBT_POST_REG:
    case ARM::STRBT_POST_IMM:
      if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
        return MCDisassembler::Fail;
      break;
    default:
      break;
  }

  if (!Check(S, DecodeGPRRegisterClass(Inst, Rt, Address, Decoder)))
    return MCDisassembler::Fail;

  // On loads, the writeback operand comes after Rt.
  switch (Inst.getOpcode()) {
    case ARM::LDR_POST_IMM:
    case ARM::LDR_POST_REG:
    case ARM::LDRB_POST_IMM:
    case ARM::LDRB_POST_REG:
    case ARM::LDRBT_POST_REG:
    case ARM::LDRBT_POST_IMM:
    case ARM::LDRT_POST_REG:
    case ARM::LDRT_POST_IMM:
      if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
        return MCDisassembler::Fail;
      break;
    default:
      break;
  }

  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;

  ARM_AM::AddrOpc Op = ARM_AM::add;
  if (!fieldFromInstruction(Insn, 23, 1))
    Op = ARM_AM::sub;

  bool writeback = (P == 0) || (W == 1);
  unsigned idx_mode = 0;
  if (P && writeback)
    idx_mode = ARMII::IndexModePre;
  else if (!P && writeback)
    idx_mode = ARMII::IndexModePost;

  if (writeback && (Rn == 15 || Rn == Rt))
    S = MCDisassembler::SoftFail; // UNPREDICTABLE

  if (reg) {
    if (!Check(S, DecodeGPRnopcRegisterClass(Inst, Rm, Address, Decoder)))
      return MCDisassembler::Fail;
    ARM_AM::ShiftOpc Opc = ARM_AM::lsl;
    switch( fieldFromInstruction(Insn, 5, 2)) {
      case 0:
        Opc = ARM_AM::lsl;
        break;
      case 1:
        Opc = ARM_AM::lsr;
        break;
      case 2:
        Opc = ARM_AM::asr;
        break;
      case 3:
        Opc = ARM_AM::ror;
        break;
      default:
        return MCDisassembler::Fail;
    }
    unsigned amt = fieldFromInstruction(Insn, 7, 5);
    if (Opc == ARM_AM::ror && amt == 0)
      Opc = ARM_AM::rrx;
    unsigned imm = ARM_AM::getAM2Opc(Op, amt, Opc, idx_mode);

    Inst.addOperand(MCOperand::createImm(imm));
  } else {
    Inst.addOperand(MCOperand::createReg(0));
    unsigned tmp = ARM_AM::getAM2Opc(Op, imm, ARM_AM::lsl, idx_mode);
    Inst.addOperand(MCOperand::createImm(tmp));
  }

  if (!Check(S, DecodePredicateOperand(Inst, pred, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

static DecodeStatus DecodeSORegMemOperand(MCInst &Inst, unsigned Val,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rn = fieldFromInstruction(Val, 13, 4);
  unsigned Rm = fieldFromInstruction(Val,  0, 4);
  unsigned type = fieldFromInstruction(Val, 5, 2);
  unsigned imm = fieldFromInstruction(Val, 7, 5);
  unsigned U = fieldFromInstruction(Val, 12, 1);

  ARM_AM::ShiftOpc ShOp = ARM_AM::lsl;
  switch (type) {
    case 0:
      ShOp = ARM_AM::lsl;
      break;
    case 1:
      ShOp = ARM_AM::lsr;
      break;
    case 2:
      ShOp = ARM_AM::asr;
      break;
    case 3:
      ShOp = ARM_AM::ror;
      break;
  }

  if (ShOp == ARM_AM::ror && imm == 0)
    ShOp = ARM_AM::rrx;

  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeGPRRegisterClass(Inst, Rm, Address, Decoder)))
    return MCDisassembler::Fail;
  unsigned shift;
  if (U)
    shift = ARM_AM::getAM2Opc(ARM_AM::add, imm, ShOp);
  else
    shift = ARM_AM::getAM2Opc(ARM_AM::sub, imm, ShOp);
  Inst.addOperand(MCOperand::createImm(shift));

  return S;
}

static DecodeStatus DecodeTSBInstruction(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder) {
  if (Inst.getOpcode() != ARM::TSB && Inst.getOpcode() != ARM::t2TSB)
    return MCDisassembler::Fail;

  // The "csync" operand is not encoded into the "tsb" instruction (as this is
  // the only available operand), but LLVM expects the instruction to have one
  // operand, so we need to add the csync when decoding.
  Inst.addOperand(MCOperand::createImm(ARM_TSB::CSYNC));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeAddrMode3Instruction(MCInst &Inst, unsigned Insn,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rt = fieldFromInstruction(Insn, 12, 4);
  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  unsigned Rm = fieldFromInstruction(Insn, 0, 4);
  unsigned type = fieldFromInstruction(Insn, 22, 1);
  unsigned imm = fieldFromInstruction(Insn, 8, 4);
  unsigned U = ((~fieldFromInstruction(Insn, 23, 1)) & 1) << 8;
  unsigned pred = fieldFromInstruction(Insn, 28, 4);
  unsigned W = fieldFromInstruction(Insn, 21, 1);
  unsigned P = fieldFromInstruction(Insn, 24, 1);
  unsigned Rt2 = Rt + 1;

  bool writeback = (W == 1) | (P == 0);

  // For {LD,ST}RD, Rt must be even, else undefined.
  switch (Inst.getOpcode()) {
    case ARM::STRD:
    case ARM::STRD_PRE:
    case ARM::STRD_POST:
    case ARM::LDRD:
    case ARM::LDRD_PRE:
    case ARM::LDRD_POST:
      if (Rt & 0x1) S = MCDisassembler::SoftFail;
      break;
    default:
      break;
  }
  switch (Inst.getOpcode()) {
    case ARM::STRD:
    case ARM::STRD_PRE:
    case ARM::STRD_POST:
      if (P == 0 && W == 1)
        S = MCDisassembler::SoftFail;

      if (writeback && (Rn == 15 || Rn == Rt || Rn == Rt2))
        S = MCDisassembler::SoftFail;
      if (type && Rm == 15)
        S = MCDisassembler::SoftFail;
      if (Rt2 == 15)
        S = MCDisassembler::SoftFail;
      if (!type && fieldFromInstruction(Insn, 8, 4))
        S = MCDisassembler::SoftFail;
      break;
    case ARM::STRH:
    case ARM::STRH_PRE:
    case ARM::STRH_POST:
      if (Rt == 15)
        S = MCDisassembler::SoftFail;
      if (writeback && (Rn == 15 || Rn == Rt))
        S = MCDisassembler::SoftFail;
      if (!type && Rm == 15)
        S = MCDisassembler::SoftFail;
      break;
    case ARM::LDRD:
    case ARM::LDRD_PRE:
    case ARM::LDRD_POST:
      if (type && Rn == 15) {
        if (Rt2 == 15)
          S = MCDisassembler::SoftFail;
        break;
      }
      if (P == 0 && W == 1)
        S = MCDisassembler::SoftFail;
      if (!type && (Rt2 == 15 || Rm == 15 || Rm == Rt || Rm == Rt2))
        S = MCDisassembler::SoftFail;
      if (!type && writeback && Rn == 15)
        S = MCDisassembler::SoftFail;
      if (writeback && (Rn == Rt || Rn == Rt2))
        S = MCDisassembler::SoftFail;
      break;
    case ARM::LDRH:
    case ARM::LDRH_PRE:
    case ARM::LDRH_POST:
      if (type && Rn == 15) {
        if (Rt == 15)
          S = MCDisassembler::SoftFail;
        break;
      }
      if (Rt == 15)
        S = MCDisassembler::SoftFail;
      if (!type && Rm == 15)
        S = MCDisassembler::SoftFail;
      if (!type && writeback && (Rn == 15 || Rn == Rt))
        S = MCDisassembler::SoftFail;
      break;
    case ARM::LDRSH:
    case ARM::LDRSH_PRE:
    case ARM::LDRSH_POST:
    case ARM::LDRSB:
    case ARM::LDRSB_PRE:
    case ARM::LDRSB_POST:
      if (type && Rn == 15) {
        if (Rt == 15)
          S = MCDisassembler::SoftFail;
        break;
      }
      if (type && (Rt == 15 || (writeback && Rn == Rt)))
        S = MCDisassembler::SoftFail;
      if (!type && (Rt == 15 || Rm == 15))
        S = MCDisassembler::SoftFail;
      if (!type && writeback && (Rn == 15 || Rn == Rt))
        S = MCDisassembler::SoftFail;
      break;
    default:
      break;
  }

  if (writeback) { // Writeback
    if (P)
      U |= ARMII::IndexModePre << 9;
    else
      U |= ARMII::IndexModePost << 9;

    // On stores, the writeback operand precedes Rt.
    switch (Inst.getOpcode()) {
    case ARM::STRD:
    case ARM::STRD_PRE:
    case ARM::STRD_POST:
    case ARM::STRH:
    case ARM::STRH_PRE:
    case ARM::STRH_POST:
      if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
        return MCDisassembler::Fail;
      break;
    default:
      break;
    }
  }

  if (!Check(S, DecodeGPRRegisterClass(Inst, Rt, Address, Decoder)))
    return MCDisassembler::Fail;
  switch (Inst.getOpcode()) {
    case ARM::STRD:
    case ARM::STRD_PRE:
    case ARM::STRD_POST:
    case ARM::LDRD:
    case ARM::LDRD_PRE:
    case ARM::LDRD_POST:
      if (!Check(S, DecodeGPRRegisterClass(Inst, Rt+1, Address, Decoder)))
        return MCDisassembler::Fail;
      break;
    default:
      break;
  }

  if (writeback) {
    // On loads, the writeback operand comes after Rt.
    switch (Inst.getOpcode()) {
    case ARM::LDRD:
    case ARM::LDRD_PRE:
    case ARM::LDRD_POST:
    case ARM::LDRH:
    case ARM::LDRH_PRE:
    case ARM::LDRH_POST:
    case ARM::LDRSH:
    case ARM::LDRSH_PRE:
    case ARM::LDRSH_POST:
    case ARM::LDRSB:
    case ARM::LDRSB_PRE:
    case ARM::LDRSB_POST:
    case ARM::LDRHTr:
    case ARM::LDRSBTr:
      if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
        return MCDisassembler::Fail;
      break;
    default:
      break;
    }
  }

  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;

  if (type) {
    Inst.addOperand(MCOperand::createReg(0));
    Inst.addOperand(MCOperand::createImm(U | (imm << 4) | Rm));
  } else {
    if (!Check(S, DecodeGPRRegisterClass(Inst, Rm, Address, Decoder)))
    return MCDisassembler::Fail;
    Inst.addOperand(MCOperand::createImm(U));
  }

  if (!Check(S, DecodePredicateOperand(Inst, pred, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

static DecodeStatus DecodeRFEInstruction(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  unsigned mode = fieldFromInstruction(Insn, 23, 2);

  switch (mode) {
    case 0:
      mode = ARM_AM::da;
      break;
    case 1:
      mode = ARM_AM::ia;
      break;
    case 2:
      mode = ARM_AM::db;
      break;
    case 3:
      mode = ARM_AM::ib;
      break;
  }

  Inst.addOperand(MCOperand::createImm(mode));
  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

static DecodeStatus DecodeQADDInstruction(MCInst &Inst, unsigned Insn,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rd = fieldFromInstruction(Insn, 12, 4);
  unsigned Rm = fieldFromInstruction(Insn, 0, 4);
  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  unsigned pred = fieldFromInstruction(Insn, 28, 4);

  if (pred == 0xF)
    return DecodeCPSInstruction(Inst, Insn, Address, Decoder);

  if (!Check(S, DecodeGPRnopcRegisterClass(Inst, Rd, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeGPRnopcRegisterClass(Inst, Rm, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeGPRnopcRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodePredicateOperand(Inst, pred, Address, Decoder)))
    return MCDisassembler::Fail;
  return S;
}

static DecodeStatus
DecodeMemMultipleWritebackInstruction(MCInst &Inst, unsigned Insn,
                                      uint64_t Address,
                                      const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  unsigned pred = fieldFromInstruction(Insn, 28, 4);
  unsigned reglist = fieldFromInstruction(Insn, 0, 16);

  if (pred == 0xF) {
    // Ambiguous with RFE and SRS
    switch (Inst.getOpcode()) {
      case ARM::LDMDA:
        Inst.setOpcode(ARM::RFEDA);
        break;
      case ARM::LDMDA_UPD:
        Inst.setOpcode(ARM::RFEDA_UPD);
        break;
      case ARM::LDMDB:
        Inst.setOpcode(ARM::RFEDB);
        break;
      case ARM::LDMDB_UPD:
        Inst.setOpcode(ARM::RFEDB_UPD);
        break;
      case ARM::LDMIA:
        Inst.setOpcode(ARM::RFEIA);
        break;
      case ARM::LDMIA_UPD:
        Inst.setOpcode(ARM::RFEIA_UPD);
        break;
      case ARM::LDMIB:
        Inst.setOpcode(ARM::RFEIB);
        break;
      case ARM::LDMIB_UPD:
        Inst.setOpcode(ARM::RFEIB_UPD);
        break;
      case ARM::STMDA:
        Inst.setOpcode(ARM::SRSDA);
        break;
      case ARM::STMDA_UPD:
        Inst.setOpcode(ARM::SRSDA_UPD);
        break;
      case ARM::STMDB:
        Inst.setOpcode(ARM::SRSDB);
        break;
      case ARM::STMDB_UPD:
        Inst.setOpcode(ARM::SRSDB_UPD);
        break;
      case ARM::STMIA:
        Inst.setOpcode(ARM::SRSIA);
        break;
      case ARM::STMIA_UPD:
        Inst.setOpcode(ARM::SRSIA_UPD);
        break;
      case ARM::STMIB:
        Inst.setOpcode(ARM::SRSIB);
        break;
      case ARM::STMIB_UPD:
        Inst.setOpcode(ARM::SRSIB_UPD);
        break;
      default:
        return MCDisassembler::Fail;
    }

    // For stores (which become SRS's, the only operand is the mode.
    if (fieldFromInstruction(Insn, 20, 1) == 0) {
      // Check SRS encoding constraints
      if (!(fieldFromInstruction(Insn, 22, 1) == 1 &&
            fieldFromInstruction(Insn, 20, 1) == 0))
        return MCDisassembler::Fail;

      Inst.addOperand(
          MCOperand::createImm(fieldFromInstruction(Insn, 0, 4)));
      return S;
    }

    return DecodeRFEInstruction(Inst, Insn, Address, Decoder);
  }

  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail; // Tied
  if (!Check(S, DecodePredicateOperand(Inst, pred, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeRegListOperand(Inst, reglist, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

// Check for UNPREDICTABLE predicated ESB instruction
static DecodeStatus DecodeHINTInstruction(MCInst &Inst, unsigned Insn,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder) {
  unsigned pred = fieldFromInstruction(Insn, 28, 4);
  unsigned imm8 = fieldFromInstruction(Insn, 0, 8);
  const MCDisassembler *Dis = static_cast<const MCDisassembler*>(Decoder);
  const FeatureBitset &FeatureBits = Dis->getSubtargetInfo().getFeatureBits();

  DecodeStatus S = MCDisassembler::Success;

  Inst.addOperand(MCOperand::createImm(imm8));

  if (!Check(S, DecodePredicateOperand(Inst, pred, Address, Decoder)))
    return MCDisassembler::Fail;

  // ESB is unpredictable if pred != AL. Without the RAS extension, it is a NOP,
  // so all predicates should be allowed.
  if (imm8 == 0x10 && pred != 0xe && ((FeatureBits[ARM::FeatureRAS]) != 0))
    S = MCDisassembler::SoftFail;

  return S;
}

static DecodeStatus DecodeCPSInstruction(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder) {
  unsigned imod = fieldFromInstruction(Insn, 18, 2);
  unsigned M = fieldFromInstruction(Insn, 17, 1);
  unsigned iflags = fieldFromInstruction(Insn, 6, 3);
  unsigned mode = fieldFromInstruction(Insn, 0, 5);

  DecodeStatus S = MCDisassembler::Success;

  // This decoder is called from multiple location that do not check
  // the full encoding is valid before they do.
  if (fieldFromInstruction(Insn, 5, 1) != 0 ||
      fieldFromInstruction(Insn, 16, 1) != 0 ||
      fieldFromInstruction(Insn, 20, 8) != 0x10)
    return MCDisassembler::Fail;

  // imod == '01' --> UNPREDICTABLE
  // NOTE: Even though this is technically UNPREDICTABLE, we choose to
  // return failure here.  The '01' imod value is unprintable, so there's
  // nothing useful we could do even if we returned UNPREDICTABLE.

  if (imod == 1) return MCDisassembler::Fail;

  if (imod && M) {
    Inst.setOpcode(ARM::CPS3p);
    Inst.addOperand(MCOperand::createImm(imod));
    Inst.addOperand(MCOperand::createImm(iflags));
    Inst.addOperand(MCOperand::createImm(mode));
  } else if (imod && !M) {
    Inst.setOpcode(ARM::CPS2p);
    Inst.addOperand(MCOperand::createImm(imod));
    Inst.addOperand(MCOperand::createImm(iflags));
    if (mode) S = MCDisassembler::SoftFail;
  } else if (!imod && M) {
    Inst.setOpcode(ARM::CPS1p);
    Inst.addOperand(MCOperand::createImm(mode));
    if (iflags) S = MCDisassembler::SoftFail;
  } else {
    // imod == '00' && M == '0' --> UNPREDICTABLE
    Inst.setOpcode(ARM::CPS1p);
    Inst.addOperand(MCOperand::createImm(mode));
    S = MCDisassembler::SoftFail;
  }

  return S;
}

static DecodeStatus DecodeT2CPSInstruction(MCInst &Inst, unsigned Insn,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder) {
  unsigned imod = fieldFromInstruction(Insn, 9, 2);
  unsigned M = fieldFromInstruction(Insn, 8, 1);
  unsigned iflags = fieldFromInstruction(Insn, 5, 3);
  unsigned mode = fieldFromInstruction(Insn, 0, 5);

  DecodeStatus S = MCDisassembler::Success;

  // imod == '01' --> UNPREDICTABLE
  // NOTE: Even though this is technically UNPREDICTABLE, we choose to
  // return failure here.  The '01' imod value is unprintable, so there's
  // nothing useful we could do even if we returned UNPREDICTABLE.

  if (imod == 1) return MCDisassembler::Fail;

  if (imod && M) {
    Inst.setOpcode(ARM::t2CPS3p);
    Inst.addOperand(MCOperand::createImm(imod));
    Inst.addOperand(MCOperand::createImm(iflags));
    Inst.addOperand(MCOperand::createImm(mode));
  } else if (imod && !M) {
    Inst.setOpcode(ARM::t2CPS2p);
    Inst.addOperand(MCOperand::createImm(imod));
    Inst.addOperand(MCOperand::createImm(iflags));
    if (mode) S = MCDisassembler::SoftFail;
  } else if (!imod && M) {
    Inst.setOpcode(ARM::t2CPS1p);
    Inst.addOperand(MCOperand::createImm(mode));
    if (iflags) S = MCDisassembler::SoftFail;
  } else {
    // imod == '00' && M == '0' --> this is a HINT instruction
    int imm = fieldFromInstruction(Insn, 0, 8);
    // HINT are defined only for immediate in [0..4]
    if(imm > 4) return MCDisassembler::Fail;
    Inst.setOpcode(ARM::t2HINT);
    Inst.addOperand(MCOperand::createImm(imm));
  }

  return S;
}

static DecodeStatus
DecodeT2HintSpaceInstruction(MCInst &Inst, unsigned Insn, uint64_t Address,
                             const MCDisassembler *Decoder) {
  unsigned imm = fieldFromInstruction(Insn, 0, 8);

  unsigned Opcode = ARM::t2HINT;

  if (imm == 0x0D) {
    Opcode = ARM::t2PACBTI;
  } else if (imm == 0x1D) {
    Opcode = ARM::t2PAC;
  } else if (imm == 0x2D) {
    Opcode = ARM::t2AUT;
  } else if (imm == 0x0F) {
    Opcode = ARM::t2BTI;
  }

  Inst.setOpcode(Opcode);
  if (Opcode == ARM::t2HINT) {
    Inst.addOperand(MCOperand::createImm(imm));
  }

  return MCDisassembler::Success;
}

static DecodeStatus DecodeT2MOVTWInstruction(MCInst &Inst, unsigned Insn,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rd = fieldFromInstruction(Insn, 8, 4);
  unsigned imm = 0;

  imm |= (fieldFromInstruction(Insn, 0, 8) << 0);
  imm |= (fieldFromInstruction(Insn, 12, 3) << 8);
  imm |= (fieldFromInstruction(Insn, 16, 4) << 12);
  imm |= (fieldFromInstruction(Insn, 26, 1) << 11);

  if (Inst.getOpcode() == ARM::t2MOVTi16)
    if (!Check(S, DecoderGPRRegisterClass(Inst, Rd, Address, Decoder)))
      return MCDisassembler::Fail;
  if (!Check(S, DecoderGPRRegisterClass(Inst, Rd, Address, Decoder)))
    return MCDisassembler::Fail;

  if (!tryAddingSymbolicOperand(Address, imm, false, 4, Inst, Decoder))
    Inst.addOperand(MCOperand::createImm(imm));

  return S;
}

static DecodeStatus DecodeArmMOVTWInstruction(MCInst &Inst, unsigned Insn,
                                              uint64_t Address,
                                              const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rd = fieldFromInstruction(Insn, 12, 4);
  unsigned pred = fieldFromInstruction(Insn, 28, 4);
  unsigned imm = 0;

  imm |= (fieldFromInstruction(Insn, 0, 12) << 0);
  imm |= (fieldFromInstruction(Insn, 16, 4) << 12);

  if (Inst.getOpcode() == ARM::MOVTi16)
    if (!Check(S, DecodeGPRnopcRegisterClass(Inst, Rd, Address, Decoder)))
      return MCDisassembler::Fail;

  if (!Check(S, DecodeGPRnopcRegisterClass(Inst, Rd, Address, Decoder)))
    return MCDisassembler::Fail;

  if (!tryAddingSymbolicOperand(Address, imm, false, 4, Inst, Decoder))
    Inst.addOperand(MCOperand::createImm(imm));

  if (!Check(S, DecodePredicateOperand(Inst, pred, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

static DecodeStatus DecodeSMLAInstruction(MCInst &Inst, unsigned Insn,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rd = fieldFromInstruction(Insn, 16, 4);
  unsigned Rn = fieldFromInstruction(Insn, 0, 4);
  unsigned Rm = fieldFromInstruction(Insn, 8, 4);
  unsigned Ra = fieldFromInstruction(Insn, 12, 4);
  unsigned pred = fieldFromInstruction(Insn, 28, 4);

  if (pred == 0xF)
    return DecodeCPSInstruction(Inst, Insn, Address, Decoder);

  if (!Check(S, DecodeGPRnopcRegisterClass(Inst, Rd, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeGPRnopcRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeGPRnopcRegisterClass(Inst, Rm, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeGPRnopcRegisterClass(Inst, Ra, Address, Decoder)))
    return MCDisassembler::Fail;

  if (!Check(S, DecodePredicateOperand(Inst, pred, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

static DecodeStatus DecodeTSTInstruction(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Pred = fieldFromInstruction(Insn, 28, 4);
  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  unsigned Rm = fieldFromInstruction(Insn, 0, 4);

  if (Pred == 0xF)
    return DecodeSETPANInstruction(Inst, Insn, Address, Decoder);

  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeGPRRegisterClass(Inst, Rm, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodePredicateOperand(Inst, Pred, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

static DecodeStatus DecodeSETPANInstruction(MCInst &Inst, unsigned Insn,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Imm = fieldFromInstruction(Insn, 9, 1);

  const MCDisassembler *Dis = static_cast<const MCDisassembler*>(Decoder);
  const FeatureBitset &FeatureBits = Dis->getSubtargetInfo().getFeatureBits();

  if (!FeatureBits[ARM::HasV8_1aOps] ||
      !FeatureBits[ARM::HasV8Ops])
    return MCDisassembler::Fail;

  // Decoder can be called from DecodeTST, which does not check the full
  // encoding is valid.
  if (fieldFromInstruction(Insn, 20,12) != 0xf11 ||
      fieldFromInstruction(Insn, 4,4) != 0)
    return MCDisassembler::Fail;
  if (fieldFromInstruction(Insn, 10,10) != 0 ||
      fieldFromInstruction(Insn, 0,4) != 0)
    S = MCDisassembler::SoftFail;

  Inst.setOpcode(ARM::SETPAN);
  Inst.addOperand(MCOperand::createImm(Imm));

  return S;
}

static DecodeStatus DecodeAddrModeImm12Operand(MCInst &Inst, unsigned Val,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned add = fieldFromInstruction(Val, 12, 1);
  unsigned imm = fieldFromInstruction(Val, 0, 12);
  unsigned Rn = fieldFromInstruction(Val, 13, 4);

  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;

  if (!add) imm *= -1;
  if (imm == 0 && !add) imm = INT32_MIN;
  Inst.addOperand(MCOperand::createImm(imm));
  if (Rn == 15)
    tryAddingPcLoadReferenceComment(Address, Address + imm + 8, Decoder);

  return S;
}

static DecodeStatus DecodeAddrMode5Operand(MCInst &Inst, unsigned Val,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rn = fieldFromInstruction(Val, 9, 4);
  // U == 1 to add imm, 0 to subtract it.
  unsigned U = fieldFromInstruction(Val, 8, 1);
  unsigned imm = fieldFromInstruction(Val, 0, 8);

  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;

  if (U)
    Inst.addOperand(MCOperand::createImm(ARM_AM::getAM5Opc(ARM_AM::add, imm)));
  else
    Inst.addOperand(MCOperand::createImm(ARM_AM::getAM5Opc(ARM_AM::sub, imm)));

  return S;
}

static DecodeStatus DecodeAddrMode5FP16Operand(MCInst &Inst, unsigned Val,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rn = fieldFromInstruction(Val, 9, 4);
  // U == 1 to add imm, 0 to subtract it.
  unsigned U = fieldFromInstruction(Val, 8, 1);
  unsigned imm = fieldFromInstruction(Val, 0, 8);

  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;

  if (U)
    Inst.addOperand(MCOperand::createImm(ARM_AM::getAM5FP16Opc(ARM_AM::add, imm)));
  else
    Inst.addOperand(MCOperand::createImm(ARM_AM::getAM5FP16Opc(ARM_AM::sub, imm)));

  return S;
}

static DecodeStatus DecodeAddrMode7Operand(MCInst &Inst, unsigned Val,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder) {
  return DecodeGPRRegisterClass(Inst, Val, Address, Decoder);
}

static DecodeStatus DecodeT2BInstruction(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder) {
  DecodeStatus Status = MCDisassembler::Success;

  // Note the J1 and J2 values are from the encoded instruction.  So here
  // change them to I1 and I2 values via as documented:
  // I1 = NOT(J1 EOR S);
  // I2 = NOT(J2 EOR S);
  // and build the imm32 with one trailing zero as documented:
  // imm32 = SignExtend(S:I1:I2:imm10:imm11:'0', 32);
  unsigned S = fieldFromInstruction(Insn, 26, 1);
  unsigned J1 = fieldFromInstruction(Insn, 13, 1);
  unsigned J2 = fieldFromInstruction(Insn, 11, 1);
  unsigned I1 = !(J1 ^ S);
  unsigned I2 = !(J2 ^ S);
  unsigned imm10 = fieldFromInstruction(Insn, 16, 10);
  unsigned imm11 = fieldFromInstruction(Insn, 0, 11);
  unsigned tmp = (S << 23) | (I1 << 22) | (I2 << 21) | (imm10 << 11) | imm11;
  int imm32 = SignExtend32<25>(tmp << 1);
  if (!tryAddingSymbolicOperand(Address, Address + imm32 + 4,
                                true, 4, Inst, Decoder))
    Inst.addOperand(MCOperand::createImm(imm32));

  return Status;
}

static DecodeStatus DecodeBranchImmInstruction(MCInst &Inst, unsigned Insn,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned pred = fieldFromInstruction(Insn, 28, 4);
  unsigned imm = fieldFromInstruction(Insn, 0, 24) << 2;

  if (pred == 0xF) {
    Inst.setOpcode(ARM::BLXi);
    imm |= fieldFromInstruction(Insn, 24, 1) << 1;
    if (!tryAddingSymbolicOperand(Address, Address + SignExtend32<26>(imm) + 8,
                                  true, 4, Inst, Decoder))
    Inst.addOperand(MCOperand::createImm(SignExtend32<26>(imm)));
    return S;
  }

  if (!tryAddingSymbolicOperand(Address, Address + SignExtend32<26>(imm) + 8,
                                true, 4, Inst, Decoder))
    Inst.addOperand(MCOperand::createImm(SignExtend32<26>(imm)));

  // We already have BL_pred for BL w/ predicate, no need to add addition
  // predicate opreands for BL
  if (Inst.getOpcode() != ARM::BL)
    if (!Check(S, DecodePredicateOperand(Inst, pred, Address, Decoder)))
      return MCDisassembler::Fail;

  return S;
}

static DecodeStatus DecodeAddrMode6Operand(MCInst &Inst, unsigned Val,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rm = fieldFromInstruction(Val, 0, 4);
  unsigned align = fieldFromInstruction(Val, 4, 2);

  if (!Check(S, DecodeGPRRegisterClass(Inst, Rm, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!align)
    Inst.addOperand(MCOperand::createImm(0));
  else
    Inst.addOperand(MCOperand::createImm(4 << align));

  return S;
}

static DecodeStatus DecodeVLDInstruction(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rd = fieldFromInstruction(Insn, 12, 4);
  Rd |= fieldFromInstruction(Insn, 22, 1) << 4;
  unsigned wb = fieldFromInstruction(Insn, 16, 4);
  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  Rn |= fieldFromInstruction(Insn, 4, 2) << 4;
  unsigned Rm = fieldFromInstruction(Insn, 0, 4);

  // First output register
  switch (Inst.getOpcode()) {
  case ARM::VLD1q16: case ARM::VLD1q32: case ARM::VLD1q64: case ARM::VLD1q8:
  case ARM::VLD1q16wb_fixed: case ARM::VLD1q16wb_register:
  case ARM::VLD1q32wb_fixed: case ARM::VLD1q32wb_register:
  case ARM::VLD1q64wb_fixed: case ARM::VLD1q64wb_register:
  case ARM::VLD1q8wb_fixed: case ARM::VLD1q8wb_register:
  case ARM::VLD2d16: case ARM::VLD2d32: case ARM::VLD2d8:
  case ARM::VLD2d16wb_fixed: case ARM::VLD2d16wb_register:
  case ARM::VLD2d32wb_fixed: case ARM::VLD2d32wb_register:
  case ARM::VLD2d8wb_fixed: case ARM::VLD2d8wb_register:
    if (!Check(S, DecodeDPairRegisterClass(Inst, Rd, Address, Decoder)))
      return MCDisassembler::Fail;
    break;
  case ARM::VLD2b16:
  case ARM::VLD2b32:
  case ARM::VLD2b8:
  case ARM::VLD2b16wb_fixed:
  case ARM::VLD2b16wb_register:
  case ARM::VLD2b32wb_fixed:
  case ARM::VLD2b32wb_register:
  case ARM::VLD2b8wb_fixed:
  case ARM::VLD2b8wb_register:
    if (!Check(S, DecodeDPairSpacedRegisterClass(Inst, Rd, Address, Decoder)))
      return MCDisassembler::Fail;
    break;
  default:
    if (!Check(S, DecodeDPRRegisterClass(Inst, Rd, Address, Decoder)))
      return MCDisassembler::Fail;
  }

  // Second output register
  switch (Inst.getOpcode()) {
    case ARM::VLD3d8:
    case ARM::VLD3d16:
    case ARM::VLD3d32:
    case ARM::VLD3d8_UPD:
    case ARM::VLD3d16_UPD:
    case ARM::VLD3d32_UPD:
    case ARM::VLD4d8:
    case ARM::VLD4d16:
    case ARM::VLD4d32:
    case ARM::VLD4d8_UPD:
    case ARM::VLD4d16_UPD:
    case ARM::VLD4d32_UPD:
      if (!Check(S, DecodeDPRRegisterClass(Inst, (Rd+1)%32, Address, Decoder)))
        return MCDisassembler::Fail;
      break;
    case ARM::VLD3q8:
    case ARM::VLD3q16:
    case ARM::VLD3q32:
    case ARM::VLD3q8_UPD:
    case ARM::VLD3q16_UPD:
    case ARM::VLD3q32_UPD:
    case ARM::VLD4q8:
    case ARM::VLD4q16:
    case ARM::VLD4q32:
    case ARM::VLD4q8_UPD:
    case ARM::VLD4q16_UPD:
    case ARM::VLD4q32_UPD:
      if (!Check(S, DecodeDPRRegisterClass(Inst, (Rd+2)%32, Address, Decoder)))
        return MCDisassembler::Fail;
      break;
    default:
      break;
  }

  // Third output register
  switch(Inst.getOpcode()) {
    case ARM::VLD3d8:
    case ARM::VLD3d16:
    case ARM::VLD3d32:
    case ARM::VLD3d8_UPD:
    case ARM::VLD3d16_UPD:
    case ARM::VLD3d32_UPD:
    case ARM::VLD4d8:
    case ARM::VLD4d16:
    case ARM::VLD4d32:
    case ARM::VLD4d8_UPD:
    case ARM::VLD4d16_UPD:
    case ARM::VLD4d32_UPD:
      if (!Check(S, DecodeDPRRegisterClass(Inst, (Rd+2)%32, Address, Decoder)))
        return MCDisassembler::Fail;
      break;
    case ARM::VLD3q8:
    case ARM::VLD3q16:
    case ARM::VLD3q32:
    case ARM::VLD3q8_UPD:
    case ARM::VLD3q16_UPD:
    case ARM::VLD3q32_UPD:
    case ARM::VLD4q8:
    case ARM::VLD4q16:
    case ARM::VLD4q32:
    case ARM::VLD4q8_UPD:
    case ARM::VLD4q16_UPD:
    case ARM::VLD4q32_UPD:
      if (!Check(S, DecodeDPRRegisterClass(Inst, (Rd+4)%32, Address, Decoder)))
        return MCDisassembler::Fail;
      break;
    default:
      break;
  }

  // Fourth output register
  switch (Inst.getOpcode()) {
    case ARM::VLD4d8:
    case ARM::VLD4d16:
    case ARM::VLD4d32:
    case ARM::VLD4d8_UPD:
    case ARM::VLD4d16_UPD:
    case ARM::VLD4d32_UPD:
      if (!Check(S, DecodeDPRRegisterClass(Inst, (Rd+3)%32, Address, Decoder)))
        return MCDisassembler::Fail;
      break;
    case ARM::VLD4q8:
    case ARM::VLD4q16:
    case ARM::VLD4q32:
    case ARM::VLD4q8_UPD:
    case ARM::VLD4q16_UPD:
    case ARM::VLD4q32_UPD:
      if (!Check(S, DecodeDPRRegisterClass(Inst, (Rd+6)%32, Address, Decoder)))
        return MCDisassembler::Fail;
      break;
    default:
      break;
  }

  // Writeback operand
  switch (Inst.getOpcode()) {
    case ARM::VLD1d8wb_fixed:
    case ARM::VLD1d16wb_fixed:
    case ARM::VLD1d32wb_fixed:
    case ARM::VLD1d64wb_fixed:
    case ARM::VLD1d8wb_register:
    case ARM::VLD1d16wb_register:
    case ARM::VLD1d32wb_register:
    case ARM::VLD1d64wb_register:
    case ARM::VLD1q8wb_fixed:
    case ARM::VLD1q16wb_fixed:
    case ARM::VLD1q32wb_fixed:
    case ARM::VLD1q64wb_fixed:
    case ARM::VLD1q8wb_register:
    case ARM::VLD1q16wb_register:
    case ARM::VLD1q32wb_register:
    case ARM::VLD1q64wb_register:
    case ARM::VLD1d8Twb_fixed:
    case ARM::VLD1d8Twb_register:
    case ARM::VLD1d16Twb_fixed:
    case ARM::VLD1d16Twb_register:
    case ARM::VLD1d32Twb_fixed:
    case ARM::VLD1d32Twb_register:
    case ARM::VLD1d64Twb_fixed:
    case ARM::VLD1d64Twb_register:
    case ARM::VLD1d8Qwb_fixed:
    case ARM::VLD1d8Qwb_register:
    case ARM::VLD1d16Qwb_fixed:
    case ARM::VLD1d16Qwb_register:
    case ARM::VLD1d32Qwb_fixed:
    case ARM::VLD1d32Qwb_register:
    case ARM::VLD1d64Qwb_fixed:
    case ARM::VLD1d64Qwb_register:
    case ARM::VLD2d8wb_fixed:
    case ARM::VLD2d16wb_fixed:
    case ARM::VLD2d32wb_fixed:
    case ARM::VLD2q8wb_fixed:
    case ARM::VLD2q16wb_fixed:
    case ARM::VLD2q32wb_fixed:
    case ARM::VLD2d8wb_register:
    case ARM::VLD2d16wb_register:
    case ARM::VLD2d32wb_register:
    case ARM::VLD2q8wb_register:
    case ARM::VLD2q16wb_register:
    case ARM::VLD2q32wb_register:
    case ARM::VLD2b8wb_fixed:
    case ARM::VLD2b16wb_fixed:
    case ARM::VLD2b32wb_fixed:
    case ARM::VLD2b8wb_register:
    case ARM::VLD2b16wb_register:
    case ARM::VLD2b32wb_register:
      Inst.addOperand(MCOperand::createImm(0));
      break;
    case ARM::VLD3d8_UPD:
    case ARM::VLD3d16_UPD:
    case ARM::VLD3d32_UPD:
    case ARM::VLD3q8_UPD:
    case ARM::VLD3q16_UPD:
    case ARM::VLD3q32_UPD:
    case ARM::VLD4d8_UPD:
    case ARM::VLD4d16_UPD:
    case ARM::VLD4d32_UPD:
    case ARM::VLD4q8_UPD:
    case ARM::VLD4q16_UPD:
    case ARM::VLD4q32_UPD:
      if (!Check(S, DecodeGPRRegisterClass(Inst, wb, Address, Decoder)))
        return MCDisassembler::Fail;
      break;
    default:
      break;
  }

  // AddrMode6 Base (register+alignment)
  if (!Check(S, DecodeAddrMode6Operand(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;

  // AddrMode6 Offset (register)
  switch (Inst.getOpcode()) {
  default:
    // The below have been updated to have explicit am6offset split
    // between fixed and register offset. For those instructions not
    // yet updated, we need to add an additional reg0 operand for the
    // fixed variant.
    //
    // The fixed offset encodes as Rm == 0xd, so we check for that.
    if (Rm == 0xd) {
      Inst.addOperand(MCOperand::createReg(0));
      break;
    }
    // Fall through to handle the register offset variant.
    [[fallthrough]];
  case ARM::VLD1d8wb_fixed:
  case ARM::VLD1d16wb_fixed:
  case ARM::VLD1d32wb_fixed:
  case ARM::VLD1d64wb_fixed:
  case ARM::VLD1d8Twb_fixed:
  case ARM::VLD1d16Twb_fixed:
  case ARM::VLD1d32Twb_fixed:
  case ARM::VLD1d64Twb_fixed:
  case ARM::VLD1d8Qwb_fixed:
  case ARM::VLD1d16Qwb_fixed:
  case ARM::VLD1d32Qwb_fixed:
  case ARM::VLD1d64Qwb_fixed:
  case ARM::VLD1d8wb_register:
  case ARM::VLD1d16wb_register:
  case ARM::VLD1d32wb_register:
  case ARM::VLD1d64wb_register:
  case ARM::VLD1q8wb_fixed:
  case ARM::VLD1q16wb_fixed:
  case ARM::VLD1q32wb_fixed:
  case ARM::VLD1q64wb_fixed:
  case ARM::VLD1q8wb_register:
  case ARM::VLD1q16wb_register:
  case ARM::VLD1q32wb_register:
  case ARM::VLD1q64wb_register:
    // The fixed offset post-increment encodes Rm == 0xd. The no-writeback
    // variant encodes Rm == 0xf. Anything else is a register offset post-
    // increment and we need to add the register operand to the instruction.
    if (Rm != 0xD && Rm != 0xF &&
        !Check(S, DecodeGPRRegisterClass(Inst, Rm, Address, Decoder)))
      return MCDisassembler::Fail;
    break;
  case ARM::VLD2d8wb_fixed:
  case ARM::VLD2d16wb_fixed:
  case ARM::VLD2d32wb_fixed:
  case ARM::VLD2b8wb_fixed:
  case ARM::VLD2b16wb_fixed:
  case ARM::VLD2b32wb_fixed:
  case ARM::VLD2q8wb_fixed:
  case ARM::VLD2q16wb_fixed:
  case ARM::VLD2q32wb_fixed:
    break;
  }

  return S;
}

static DecodeStatus DecodeVLDST1Instruction(MCInst &Inst, unsigned Insn,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder) {
  unsigned type = fieldFromInstruction(Insn, 8, 4);
  unsigned align = fieldFromInstruction(Insn, 4, 2);
  if (type == 6 && (align & 2)) return MCDisassembler::Fail;
  if (type == 7 && (align & 2)) return MCDisassembler::Fail;
  if (type == 10 && align == 3) return MCDisassembler::Fail;

  unsigned load = fieldFromInstruction(Insn, 21, 1);
  return load ? DecodeVLDInstruction(Inst, Insn, Address, Decoder)
              : DecodeVSTInstruction(Inst, Insn, Address, Decoder);
}

static DecodeStatus DecodeVLDST2Instruction(MCInst &Inst, unsigned Insn,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder) {
  unsigned size = fieldFromInstruction(Insn, 6, 2);
  if (size == 3) return MCDisassembler::Fail;

  unsigned type = fieldFromInstruction(Insn, 8, 4);
  unsigned align = fieldFromInstruction(Insn, 4, 2);
  if (type == 8 && align == 3) return MCDisassembler::Fail;
  if (type == 9 && align == 3) return MCDisassembler::Fail;

  unsigned load = fieldFromInstruction(Insn, 21, 1);
  return load ? DecodeVLDInstruction(Inst, Insn, Address, Decoder)
              : DecodeVSTInstruction(Inst, Insn, Address, Decoder);
}

static DecodeStatus DecodeVLDST3Instruction(MCInst &Inst, unsigned Insn,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder) {
  unsigned size = fieldFromInstruction(Insn, 6, 2);
  if (size == 3) return MCDisassembler::Fail;

  unsigned align = fieldFromInstruction(Insn, 4, 2);
  if (align & 2) return MCDisassembler::Fail;

  unsigned load = fieldFromInstruction(Insn, 21, 1);
  return load ? DecodeVLDInstruction(Inst, Insn, Address, Decoder)
              : DecodeVSTInstruction(Inst, Insn, Address, Decoder);
}

static DecodeStatus DecodeVLDST4Instruction(MCInst &Inst, unsigned Insn,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder) {
  unsigned size = fieldFromInstruction(Insn, 6, 2);
  if (size == 3) return MCDisassembler::Fail;

  unsigned load = fieldFromInstruction(Insn, 21, 1);
  return load ? DecodeVLDInstruction(Inst, Insn, Address, Decoder)
              : DecodeVSTInstruction(Inst, Insn, Address, Decoder);
}

static DecodeStatus DecodeVSTInstruction(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rd = fieldFromInstruction(Insn, 12, 4);
  Rd |= fieldFromInstruction(Insn, 22, 1) << 4;
  unsigned wb = fieldFromInstruction(Insn, 16, 4);
  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  Rn |= fieldFromInstruction(Insn, 4, 2) << 4;
  unsigned Rm = fieldFromInstruction(Insn, 0, 4);

  // Writeback Operand
  switch (Inst.getOpcode()) {
    case ARM::VST1d8wb_fixed:
    case ARM::VST1d16wb_fixed:
    case ARM::VST1d32wb_fixed:
    case ARM::VST1d64wb_fixed:
    case ARM::VST1d8wb_register:
    case ARM::VST1d16wb_register:
    case ARM::VST1d32wb_register:
    case ARM::VST1d64wb_register:
    case ARM::VST1q8wb_fixed:
    case ARM::VST1q16wb_fixed:
    case ARM::VST1q32wb_fixed:
    case ARM::VST1q64wb_fixed:
    case ARM::VST1q8wb_register:
    case ARM::VST1q16wb_register:
    case ARM::VST1q32wb_register:
    case ARM::VST1q64wb_register:
    case ARM::VST1d8Twb_fixed:
    case ARM::VST1d16Twb_fixed:
    case ARM::VST1d32Twb_fixed:
    case ARM::VST1d64Twb_fixed:
    case ARM::VST1d8Twb_register:
    case ARM::VST1d16Twb_register:
    case ARM::VST1d32Twb_register:
    case ARM::VST1d64Twb_register:
    case ARM::VST1d8Qwb_fixed:
    case ARM::VST1d16Qwb_fixed:
    case ARM::VST1d32Qwb_fixed:
    case ARM::VST1d64Qwb_fixed:
    case ARM::VST1d8Qwb_register:
    case ARM::VST1d16Qwb_register:
    case ARM::VST1d32Qwb_register:
    case ARM::VST1d64Qwb_register:
    case ARM::VST2d8wb_fixed:
    case ARM::VST2d16wb_fixed:
    case ARM::VST2d32wb_fixed:
    case ARM::VST2d8wb_register:
    case ARM::VST2d16wb_register:
    case ARM::VST2d32wb_register:
    case ARM::VST2q8wb_fixed:
    case ARM::VST2q16wb_fixed:
    case ARM::VST2q32wb_fixed:
    case ARM::VST2q8wb_register:
    case ARM::VST2q16wb_register:
    case ARM::VST2q32wb_register:
    case ARM::VST2b8wb_fixed:
    case ARM::VST2b16wb_fixed:
    case ARM::VST2b32wb_fixed:
    case ARM::VST2b8wb_register:
    case ARM::VST2b16wb_register:
    case ARM::VST2b32wb_register:
      if (Rm == 0xF)
        return MCDisassembler::Fail;
      Inst.addOperand(MCOperand::createImm(0));
      break;
    case ARM::VST3d8_UPD:
    case ARM::VST3d16_UPD:
    case ARM::VST3d32_UPD:
    case ARM::VST3q8_UPD:
    case ARM::VST3q16_UPD:
    case ARM::VST3q32_UPD:
    case ARM::VST4d8_UPD:
    case ARM::VST4d16_UPD:
    case ARM::VST4d32_UPD:
    case ARM::VST4q8_UPD:
    case ARM::VST4q16_UPD:
    case ARM::VST4q32_UPD:
      if (!Check(S, DecodeGPRRegisterClass(Inst, wb, Address, Decoder)))
        return MCDisassembler::Fail;
      break;
    default:
      break;
  }

  // AddrMode6 Base (register+alignment)
  if (!Check(S, DecodeAddrMode6Operand(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;

  // AddrMode6 Offset (register)
  switch (Inst.getOpcode()) {
    default:
      if (Rm == 0xD)
        Inst.addOperand(MCOperand::createReg(0));
      else if (Rm != 0xF) {
        if (!Check(S, DecodeGPRRegisterClass(Inst, Rm, Address, Decoder)))
          return MCDisassembler::Fail;
      }
      break;
    case ARM::VST1d8wb_fixed:
    case ARM::VST1d16wb_fixed:
    case ARM::VST1d32wb_fixed:
    case ARM::VST1d64wb_fixed:
    case ARM::VST1q8wb_fixed:
    case ARM::VST1q16wb_fixed:
    case ARM::VST1q32wb_fixed:
    case ARM::VST1q64wb_fixed:
    case ARM::VST1d8Twb_fixed:
    case ARM::VST1d16Twb_fixed:
    case ARM::VST1d32Twb_fixed:
    case ARM::VST1d64Twb_fixed:
    case ARM::VST1d8Qwb_fixed:
    case ARM::VST1d16Qwb_fixed:
    case ARM::VST1d32Qwb_fixed:
    case ARM::VST1d64Qwb_fixed:
    case ARM::VST2d8wb_fixed:
    case ARM::VST2d16wb_fixed:
    case ARM::VST2d32wb_fixed:
    case ARM::VST2q8wb_fixed:
    case ARM::VST2q16wb_fixed:
    case ARM::VST2q32wb_fixed:
    case ARM::VST2b8wb_fixed:
    case ARM::VST2b16wb_fixed:
    case ARM::VST2b32wb_fixed:
      break;
  }

  // First input register
  switch (Inst.getOpcode()) {
  case ARM::VST1q16:
  case ARM::VST1q32:
  case ARM::VST1q64:
  case ARM::VST1q8:
  case ARM::VST1q16wb_fixed:
  case ARM::VST1q16wb_register:
  case ARM::VST1q32wb_fixed:
  case ARM::VST1q32wb_register:
  case ARM::VST1q64wb_fixed:
  case ARM::VST1q64wb_register:
  case ARM::VST1q8wb_fixed:
  case ARM::VST1q8wb_register:
  case ARM::VST2d16:
  case ARM::VST2d32:
  case ARM::VST2d8:
  case ARM::VST2d16wb_fixed:
  case ARM::VST2d16wb_register:
  case ARM::VST2d32wb_fixed:
  case ARM::VST2d32wb_register:
  case ARM::VST2d8wb_fixed:
  case ARM::VST2d8wb_register:
    if (!Check(S, DecodeDPairRegisterClass(Inst, Rd, Address, Decoder)))
      return MCDisassembler::Fail;
    break;
  case ARM::VST2b16:
  case ARM::VST2b32:
  case ARM::VST2b8:
  case ARM::VST2b16wb_fixed:
  case ARM::VST2b16wb_register:
  case ARM::VST2b32wb_fixed:
  case ARM::VST2b32wb_register:
  case ARM::VST2b8wb_fixed:
  case ARM::VST2b8wb_register:
    if (!Check(S, DecodeDPairSpacedRegisterClass(Inst, Rd, Address, Decoder)))
      return MCDisassembler::Fail;
    break;
  default:
    if (!Check(S, DecodeDPRRegisterClass(Inst, Rd, Address, Decoder)))
      return MCDisassembler::Fail;
  }

  // Second input register
  switch (Inst.getOpcode()) {
    case ARM::VST3d8:
    case ARM::VST3d16:
    case ARM::VST3d32:
    case ARM::VST3d8_UPD:
    case ARM::VST3d16_UPD:
    case ARM::VST3d32_UPD:
    case ARM::VST4d8:
    case ARM::VST4d16:
    case ARM::VST4d32:
    case ARM::VST4d8_UPD:
    case ARM::VST4d16_UPD:
    case ARM::VST4d32_UPD:
      if (!Check(S, DecodeDPRRegisterClass(Inst, (Rd+1)%32, Address, Decoder)))
        return MCDisassembler::Fail;
      break;
    case ARM::VST3q8:
    case ARM::VST3q16:
    case ARM::VST3q32:
    case ARM::VST3q8_UPD:
    case ARM::VST3q16_UPD:
    case ARM::VST3q32_UPD:
    case ARM::VST4q8:
    case ARM::VST4q16:
    case ARM::VST4q32:
    case ARM::VST4q8_UPD:
    case ARM::VST4q16_UPD:
    case ARM::VST4q32_UPD:
      if (!Check(S, DecodeDPRRegisterClass(Inst, (Rd+2)%32, Address, Decoder)))
        return MCDisassembler::Fail;
      break;
    default:
      break;
  }

  // Third input register
  switch (Inst.getOpcode()) {
    case ARM::VST3d8:
    case ARM::VST3d16:
    case ARM::VST3d32:
    case ARM::VST3d8_UPD:
    case ARM::VST3d16_UPD:
    case ARM::VST3d32_UPD:
    case ARM::VST4d8:
    case ARM::VST4d16:
    case ARM::VST4d32:
    case ARM::VST4d8_UPD:
    case ARM::VST4d16_UPD:
    case ARM::VST4d32_UPD:
      if (!Check(S, DecodeDPRRegisterClass(Inst, (Rd+2)%32, Address, Decoder)))
        return MCDisassembler::Fail;
      break;
    case ARM::VST3q8:
    case ARM::VST3q16:
    case ARM::VST3q32:
    case ARM::VST3q8_UPD:
    case ARM::VST3q16_UPD:
    case ARM::VST3q32_UPD:
    case ARM::VST4q8:
    case ARM::VST4q16:
    case ARM::VST4q32:
    case ARM::VST4q8_UPD:
    case ARM::VST4q16_UPD:
    case ARM::VST4q32_UPD:
      if (!Check(S, DecodeDPRRegisterClass(Inst, (Rd+4)%32, Address, Decoder)))
        return MCDisassembler::Fail;
      break;
    default:
      break;
  }

  // Fourth input register
  switch (Inst.getOpcode()) {
    case ARM::VST4d8:
    case ARM::VST4d16:
    case ARM::VST4d32:
    case ARM::VST4d8_UPD:
    case ARM::VST4d16_UPD:
    case ARM::VST4d32_UPD:
      if (!Check(S, DecodeDPRRegisterClass(Inst, (Rd+3)%32, Address, Decoder)))
        return MCDisassembler::Fail;
      break;
    case ARM::VST4q8:
    case ARM::VST4q16:
    case ARM::VST4q32:
    case ARM::VST4q8_UPD:
    case ARM::VST4q16_UPD:
    case ARM::VST4q32_UPD:
      if (!Check(S, DecodeDPRRegisterClass(Inst, (Rd+6)%32, Address, Decoder)))
        return MCDisassembler::Fail;
      break;
    default:
      break;
  }

  return S;
}

static DecodeStatus DecodeVLD1DupInstruction(MCInst &Inst, unsigned Insn,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rd = fieldFromInstruction(Insn, 12, 4);
  Rd |= fieldFromInstruction(Insn, 22, 1) << 4;
  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  unsigned Rm = fieldFromInstruction(Insn, 0, 4);
  unsigned align = fieldFromInstruction(Insn, 4, 1);
  unsigned size = fieldFromInstruction(Insn, 6, 2);

  if (size == 0 && align == 1)
    return MCDisassembler::Fail;
  align *= (1 << size);

  switch (Inst.getOpcode()) {
  case ARM::VLD1DUPq16: case ARM::VLD1DUPq32: case ARM::VLD1DUPq8:
  case ARM::VLD1DUPq16wb_fixed: case ARM::VLD1DUPq16wb_register:
  case ARM::VLD1DUPq32wb_fixed: case ARM::VLD1DUPq32wb_register:
  case ARM::VLD1DUPq8wb_fixed: case ARM::VLD1DUPq8wb_register:
    if (!Check(S, DecodeDPairRegisterClass(Inst, Rd, Address, Decoder)))
      return MCDisassembler::Fail;
    break;
  default:
    if (!Check(S, DecodeDPRRegisterClass(Inst, Rd, Address, Decoder)))
      return MCDisassembler::Fail;
    break;
  }
  if (Rm != 0xF) {
    if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
      return MCDisassembler::Fail;
  }

  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createImm(align));

  // The fixed offset post-increment encodes Rm == 0xd. The no-writeback
  // variant encodes Rm == 0xf. Anything else is a register offset post-
  // increment and we need to add the register operand to the instruction.
  if (Rm != 0xD && Rm != 0xF &&
      !Check(S, DecodeGPRRegisterClass(Inst, Rm, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

static DecodeStatus DecodeVLD2DupInstruction(MCInst &Inst, unsigned Insn,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rd = fieldFromInstruction(Insn, 12, 4);
  Rd |= fieldFromInstruction(Insn, 22, 1) << 4;
  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  unsigned Rm = fieldFromInstruction(Insn, 0, 4);
  unsigned align = fieldFromInstruction(Insn, 4, 1);
  unsigned size = 1 << fieldFromInstruction(Insn, 6, 2);
  align *= 2*size;

  switch (Inst.getOpcode()) {
  case ARM::VLD2DUPd16: case ARM::VLD2DUPd32: case ARM::VLD2DUPd8:
  case ARM::VLD2DUPd16wb_fixed: case ARM::VLD2DUPd16wb_register:
  case ARM::VLD2DUPd32wb_fixed: case ARM::VLD2DUPd32wb_register:
  case ARM::VLD2DUPd8wb_fixed: case ARM::VLD2DUPd8wb_register:
    if (!Check(S, DecodeDPairRegisterClass(Inst, Rd, Address, Decoder)))
      return MCDisassembler::Fail;
    break;
  case ARM::VLD2DUPd16x2: case ARM::VLD2DUPd32x2: case ARM::VLD2DUPd8x2:
  case ARM::VLD2DUPd16x2wb_fixed: case ARM::VLD2DUPd16x2wb_register:
  case ARM::VLD2DUPd32x2wb_fixed: case ARM::VLD2DUPd32x2wb_register:
  case ARM::VLD2DUPd8x2wb_fixed: case ARM::VLD2DUPd8x2wb_register:
    if (!Check(S, DecodeDPairSpacedRegisterClass(Inst, Rd, Address, Decoder)))
      return MCDisassembler::Fail;
    break;
  default:
    if (!Check(S, DecodeDPRRegisterClass(Inst, Rd, Address, Decoder)))
      return MCDisassembler::Fail;
    break;
  }

  if (Rm != 0xF)
    Inst.addOperand(MCOperand::createImm(0));

  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createImm(align));

  if (Rm != 0xD && Rm != 0xF) {
    if (!Check(S, DecodeGPRRegisterClass(Inst, Rm, Address, Decoder)))
      return MCDisassembler::Fail;
  }

  return S;
}

static DecodeStatus DecodeVLD3DupInstruction(MCInst &Inst, unsigned Insn,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rd = fieldFromInstruction(Insn, 12, 4);
  Rd |= fieldFromInstruction(Insn, 22, 1) << 4;
  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  unsigned Rm = fieldFromInstruction(Insn, 0, 4);
  unsigned inc = fieldFromInstruction(Insn, 5, 1) + 1;

  if (!Check(S, DecodeDPRRegisterClass(Inst, Rd, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeDPRRegisterClass(Inst, (Rd+inc)%32, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeDPRRegisterClass(Inst, (Rd+2*inc)%32, Address, Decoder)))
    return MCDisassembler::Fail;
  if (Rm != 0xF) {
    if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
      return MCDisassembler::Fail;
  }

  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createImm(0));

  if (Rm == 0xD)
    Inst.addOperand(MCOperand::createReg(0));
  else if (Rm != 0xF) {
    if (!Check(S, DecodeGPRRegisterClass(Inst, Rm, Address, Decoder)))
      return MCDisassembler::Fail;
  }

  return S;
}

static DecodeStatus DecodeVLD4DupInstruction(MCInst &Inst, unsigned Insn,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rd = fieldFromInstruction(Insn, 12, 4);
  Rd |= fieldFromInstruction(Insn, 22, 1) << 4;
  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  unsigned Rm = fieldFromInstruction(Insn, 0, 4);
  unsigned size = fieldFromInstruction(Insn, 6, 2);
  unsigned inc = fieldFromInstruction(Insn, 5, 1) + 1;
  unsigned align = fieldFromInstruction(Insn, 4, 1);

  if (size == 0x3) {
    if (align == 0)
      return MCDisassembler::Fail;
    align = 16;
  } else {
    if (size == 2) {
      align *= 8;
    } else {
      size = 1 << size;
      align *= 4*size;
    }
  }

  if (!Check(S, DecodeDPRRegisterClass(Inst, Rd, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeDPRRegisterClass(Inst, (Rd+inc)%32, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeDPRRegisterClass(Inst, (Rd+2*inc)%32, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeDPRRegisterClass(Inst, (Rd+3*inc)%32, Address, Decoder)))
    return MCDisassembler::Fail;
  if (Rm != 0xF) {
    if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
      return MCDisassembler::Fail;
  }

  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createImm(align));

  if (Rm == 0xD)
    Inst.addOperand(MCOperand::createReg(0));
  else if (Rm != 0xF) {
    if (!Check(S, DecodeGPRRegisterClass(Inst, Rm, Address, Decoder)))
      return MCDisassembler::Fail;
  }

  return S;
}

static DecodeStatus DecodeVMOVModImmInstruction(MCInst &Inst, unsigned Insn,
                                                uint64_t Address,
                                                const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rd = fieldFromInstruction(Insn, 12, 4);
  Rd |= fieldFromInstruction(Insn, 22, 1) << 4;
  unsigned imm = fieldFromInstruction(Insn, 0, 4);
  imm |= fieldFromInstruction(Insn, 16, 3) << 4;
  imm |= fieldFromInstruction(Insn, 24, 1) << 7;
  imm |= fieldFromInstruction(Insn, 8, 4) << 8;
  imm |= fieldFromInstruction(Insn, 5, 1) << 12;
  unsigned Q = fieldFromInstruction(Insn, 6, 1);

  if (Q) {
    if (!Check(S, DecodeQPRRegisterClass(Inst, Rd, Address, Decoder)))
    return MCDisassembler::Fail;
  } else {
    if (!Check(S, DecodeDPRRegisterClass(Inst, Rd, Address, Decoder)))
    return MCDisassembler::Fail;
  }

  Inst.addOperand(MCOperand::createImm(imm));

  switch (Inst.getOpcode()) {
    case ARM::VORRiv4i16:
    case ARM::VORRiv2i32:
    case ARM::VBICiv4i16:
    case ARM::VBICiv2i32:
      if (!Check(S, DecodeDPRRegisterClass(Inst, Rd, Address, Decoder)))
        return MCDisassembler::Fail;
      break;
    case ARM::VORRiv8i16:
    case ARM::VORRiv4i32:
    case ARM::VBICiv8i16:
    case ARM::VBICiv4i32:
      if (!Check(S, DecodeQPRRegisterClass(Inst, Rd, Address, Decoder)))
        return MCDisassembler::Fail;
      break;
    default:
      break;
  }

  return S;
}

static DecodeStatus DecodeMVEModImmInstruction(MCInst &Inst, unsigned Insn,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Qd = ((fieldFromInstruction(Insn, 22, 1) << 3) |
                 fieldFromInstruction(Insn, 13, 3));
  unsigned cmode = fieldFromInstruction(Insn, 8, 4);
  unsigned imm = fieldFromInstruction(Insn, 0, 4);
  imm |= fieldFromInstruction(Insn, 16, 3) << 4;
  imm |= fieldFromInstruction(Insn, 28, 1) << 7;
  imm |= cmode                             << 8;
  imm |= fieldFromInstruction(Insn, 5, 1)  << 12;

  if (cmode == 0xF && Inst.getOpcode() == ARM::MVE_VMVNimmi32)
    return MCDisassembler::Fail;

  if (!Check(S, DecodeMQPRRegisterClass(Inst, Qd, Address, Decoder)))
    return MCDisassembler::Fail;

  Inst.addOperand(MCOperand::createImm(imm));

  Inst.addOperand(MCOperand::createImm(ARMVCC::None));
  Inst.addOperand(MCOperand::createReg(0));
  Inst.addOperand(MCOperand::createImm(0));

  return S;
}

static DecodeStatus DecodeMVEVADCInstruction(MCInst &Inst, unsigned Insn,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Qd = fieldFromInstruction(Insn, 13, 3);
  Qd |= fieldFromInstruction(Insn, 22, 1) << 3;
  if (!Check(S, DecodeMQPRRegisterClass(Inst, Qd, Address, Decoder)))
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createReg(ARM::FPSCR_NZCV));

  unsigned Qn = fieldFromInstruction(Insn, 17, 3);
  Qn |= fieldFromInstruction(Insn, 7, 1) << 3;
  if (!Check(S, DecodeMQPRRegisterClass(Inst, Qn, Address, Decoder)))
    return MCDisassembler::Fail;
  unsigned Qm = fieldFromInstruction(Insn, 1, 3);
  Qm |= fieldFromInstruction(Insn, 5, 1) << 3;
  if (!Check(S, DecodeMQPRRegisterClass(Inst, Qm, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!fieldFromInstruction(Insn, 12, 1)) // I bit clear => need input FPSCR
    Inst.addOperand(MCOperand::createReg(ARM::FPSCR_NZCV));
  Inst.addOperand(MCOperand::createImm(Qd));

  return S;
}

static DecodeStatus DecodeVSHLMaxInstruction(MCInst &Inst, unsigned Insn,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rd = fieldFromInstruction(Insn, 12, 4);
  Rd |= fieldFromInstruction(Insn, 22, 1) << 4;
  unsigned Rm = fieldFromInstruction(Insn, 0, 4);
  Rm |= fieldFromInstruction(Insn, 5, 1) << 4;
  unsigned size = fieldFromInstruction(Insn, 18, 2);

  if (!Check(S, DecodeQPRRegisterClass(Inst, Rd, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeDPRRegisterClass(Inst, Rm, Address, Decoder)))
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createImm(8 << size));

  return S;
}

static DecodeStatus DecodeShiftRight8Imm(MCInst &Inst, unsigned Val,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder) {
  Inst.addOperand(MCOperand::createImm(8 - Val));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeShiftRight16Imm(MCInst &Inst, unsigned Val,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder) {
  Inst.addOperand(MCOperand::createImm(16 - Val));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeShiftRight32Imm(MCInst &Inst, unsigned Val,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder) {
  Inst.addOperand(MCOperand::createImm(32 - Val));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeShiftRight64Imm(MCInst &Inst, unsigned Val,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder) {
  Inst.addOperand(MCOperand::createImm(64 - Val));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeTBLInstruction(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rd = fieldFromInstruction(Insn, 12, 4);
  Rd |= fieldFromInstruction(Insn, 22, 1) << 4;
  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  Rn |= fieldFromInstruction(Insn, 7, 1) << 4;
  unsigned Rm = fieldFromInstruction(Insn, 0, 4);
  Rm |= fieldFromInstruction(Insn, 5, 1) << 4;
  unsigned op = fieldFromInstruction(Insn, 6, 1);

  if (!Check(S, DecodeDPRRegisterClass(Inst, Rd, Address, Decoder)))
    return MCDisassembler::Fail;
  if (op) {
    if (!Check(S, DecodeDPRRegisterClass(Inst, Rd, Address, Decoder)))
    return MCDisassembler::Fail; // Writeback
  }

  switch (Inst.getOpcode()) {
  case ARM::VTBL2:
  case ARM::VTBX2:
    if (!Check(S, DecodeDPairRegisterClass(Inst, Rn, Address, Decoder)))
      return MCDisassembler::Fail;
    break;
  default:
    if (!Check(S, DecodeDPRRegisterClass(Inst, Rn, Address, Decoder)))
      return MCDisassembler::Fail;
  }

  if (!Check(S, DecodeDPRRegisterClass(Inst, Rm, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

static DecodeStatus DecodeThumbAddSpecialReg(MCInst &Inst, uint16_t Insn,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned dst = fieldFromInstruction(Insn, 8, 3);
  unsigned imm = fieldFromInstruction(Insn, 0, 8);

  if (!Check(S, DecodetGPRRegisterClass(Inst, dst, Address, Decoder)))
    return MCDisassembler::Fail;

  switch(Inst.getOpcode()) {
    default:
      return MCDisassembler::Fail;
    case ARM::tADR:
      break; // tADR does not explicitly represent the PC as an operand.
    case ARM::tADDrSPi:
      Inst.addOperand(MCOperand::createReg(ARM::SP));
      break;
  }

  Inst.addOperand(MCOperand::createImm(imm));
  return S;
}

static DecodeStatus DecodeThumbBROperand(MCInst &Inst, unsigned Val,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder) {
  if (!tryAddingSymbolicOperand(Address, Address + SignExtend32<12>(Val<<1) + 4,
                                true, 2, Inst, Decoder))
    Inst.addOperand(MCOperand::createImm(SignExtend32<12>(Val << 1)));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeT2BROperand(MCInst &Inst, unsigned Val,
                                      uint64_t Address,
                                      const MCDisassembler *Decoder) {
  if (!tryAddingSymbolicOperand(Address, Address + SignExtend32<21>(Val) + 4,
                                true, 4, Inst, Decoder))
    Inst.addOperand(MCOperand::createImm(SignExtend32<21>(Val)));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeThumbCmpBROperand(MCInst &Inst, unsigned Val,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder) {
  if (!tryAddingSymbolicOperand(Address, Address + (Val<<1) + 4,
                                true, 2, Inst, Decoder))
    Inst.addOperand(MCOperand::createImm(Val << 1));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeThumbAddrModeRR(MCInst &Inst, unsigned Val,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rn = fieldFromInstruction(Val, 0, 3);
  unsigned Rm = fieldFromInstruction(Val, 3, 3);

  if (!Check(S, DecodetGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodetGPRRegisterClass(Inst, Rm, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

static DecodeStatus DecodeThumbAddrModeIS(MCInst &Inst, unsigned Val,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rn = fieldFromInstruction(Val, 0, 3);
  unsigned imm = fieldFromInstruction(Val, 3, 5);

  if (!Check(S, DecodetGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createImm(imm));

  return S;
}

static DecodeStatus DecodeThumbAddrModePC(MCInst &Inst, unsigned Val,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder) {
  unsigned imm = Val << 2;

  Inst.addOperand(MCOperand::createImm(imm));
  tryAddingPcLoadReferenceComment(Address, (Address & ~2u) + imm + 4, Decoder);

  return MCDisassembler::Success;
}

static DecodeStatus DecodeThumbAddrModeSP(MCInst &Inst, unsigned Val,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder) {
  Inst.addOperand(MCOperand::createReg(ARM::SP));
  Inst.addOperand(MCOperand::createImm(Val));

  return MCDisassembler::Success;
}

static DecodeStatus DecodeT2AddrModeSOReg(MCInst &Inst, unsigned Val,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rn = fieldFromInstruction(Val, 6, 4);
  unsigned Rm = fieldFromInstruction(Val, 2, 4);
  unsigned imm = fieldFromInstruction(Val, 0, 2);

  // Thumb stores cannot use PC as dest register.
  switch (Inst.getOpcode()) {
  case ARM::t2STRHs:
  case ARM::t2STRBs:
  case ARM::t2STRs:
    if (Rn == 15)
      return MCDisassembler::Fail;
    break;
  default:
    break;
  }

  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecoderGPRRegisterClass(Inst, Rm, Address, Decoder)))
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createImm(imm));

  return S;
}

static DecodeStatus DecodeT2LoadShift(MCInst &Inst, unsigned Insn,
                                      uint64_t Address,
                                      const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rt = fieldFromInstruction(Insn, 12, 4);
  unsigned Rn = fieldFromInstruction(Insn, 16, 4);

  const FeatureBitset &featureBits =
    ((const MCDisassembler*)Decoder)->getSubtargetInfo().getFeatureBits();

  bool hasMP = featureBits[ARM::FeatureMP];
  bool hasV7Ops = featureBits[ARM::HasV7Ops];

  if (Rn == 15) {
    switch (Inst.getOpcode()) {
    case ARM::t2LDRBs:
      Inst.setOpcode(ARM::t2LDRBpci);
      break;
    case ARM::t2LDRHs:
      Inst.setOpcode(ARM::t2LDRHpci);
      break;
    case ARM::t2LDRSHs:
      Inst.setOpcode(ARM::t2LDRSHpci);
      break;
    case ARM::t2LDRSBs:
      Inst.setOpcode(ARM::t2LDRSBpci);
      break;
    case ARM::t2LDRs:
      Inst.setOpcode(ARM::t2LDRpci);
      break;
    case ARM::t2PLDs:
      Inst.setOpcode(ARM::t2PLDpci);
      break;
    case ARM::t2PLIs:
      Inst.setOpcode(ARM::t2PLIpci);
      break;
    default:
      return MCDisassembler::Fail;
    }

    return DecodeT2LoadLabel(Inst, Insn, Address, Decoder);
  }

  if (Rt == 15) {
    switch (Inst.getOpcode()) {
    case ARM::t2LDRSHs:
      return MCDisassembler::Fail;
    case ARM::t2LDRHs:
      Inst.setOpcode(ARM::t2PLDWs);
      break;
    case ARM::t2LDRSBs:
      Inst.setOpcode(ARM::t2PLIs);
      break;
    default:
      break;
    }
  }

  switch (Inst.getOpcode()) {
    case ARM::t2PLDs:
      break;
    case ARM::t2PLIs:
      if (!hasV7Ops)
        return MCDisassembler::Fail;
      break;
    case ARM::t2PLDWs:
      if (!hasV7Ops || !hasMP)
        return MCDisassembler::Fail;
      break;
    default:
      if (!Check(S, DecodeGPRRegisterClass(Inst, Rt, Address, Decoder)))
        return MCDisassembler::Fail;
  }

  unsigned addrmode = fieldFromInstruction(Insn, 4, 2);
  addrmode |= fieldFromInstruction(Insn, 0, 4) << 2;
  addrmode |= fieldFromInstruction(Insn, 16, 4) << 6;
  if (!Check(S, DecodeT2AddrModeSOReg(Inst, addrmode, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

static DecodeStatus DecodeT2LoadImm8(MCInst &Inst, unsigned Insn,
                                     uint64_t Address,
                                     const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  unsigned Rt = fieldFromInstruction(Insn, 12, 4);
  unsigned U = fieldFromInstruction(Insn, 9, 1);
  unsigned imm = fieldFromInstruction(Insn, 0, 8);
  imm |= (U << 8);
  imm |= (Rn << 9);
  unsigned add = fieldFromInstruction(Insn, 9, 1);

  const FeatureBitset &featureBits =
    ((const MCDisassembler*)Decoder)->getSubtargetInfo().getFeatureBits();

  bool hasMP = featureBits[ARM::FeatureMP];
  bool hasV7Ops = featureBits[ARM::HasV7Ops];

  if (Rn == 15) {
    switch (Inst.getOpcode()) {
    case ARM::t2LDRi8:
      Inst.setOpcode(ARM::t2LDRpci);
      break;
    case ARM::t2LDRBi8:
      Inst.setOpcode(ARM::t2LDRBpci);
      break;
    case ARM::t2LDRSBi8:
      Inst.setOpcode(ARM::t2LDRSBpci);
      break;
    case ARM::t2LDRHi8:
      Inst.setOpcode(ARM::t2LDRHpci);
      break;
    case ARM::t2LDRSHi8:
      Inst.setOpcode(ARM::t2LDRSHpci);
      break;
    case ARM::t2PLDi8:
      Inst.setOpcode(ARM::t2PLDpci);
      break;
    case ARM::t2PLIi8:
      Inst.setOpcode(ARM::t2PLIpci);
      break;
    default:
      return MCDisassembler::Fail;
    }
    return DecodeT2LoadLabel(Inst, Insn, Address, Decoder);
  }

  if (Rt == 15) {
    switch (Inst.getOpcode()) {
    case ARM::t2LDRSHi8:
      return MCDisassembler::Fail;
    case ARM::t2LDRHi8:
      if (!add)
        Inst.setOpcode(ARM::t2PLDWi8);
      break;
    case ARM::t2LDRSBi8:
      Inst.setOpcode(ARM::t2PLIi8);
      break;
    default:
      break;
    }
  }

  switch (Inst.getOpcode()) {
  case ARM::t2PLDi8:
    break;
  case ARM::t2PLIi8:
    if (!hasV7Ops)
      return MCDisassembler::Fail;
    break;
  case ARM::t2PLDWi8:
      if (!hasV7Ops || !hasMP)
        return MCDisassembler::Fail;
      break;
  default:
    if (!Check(S, DecodeGPRRegisterClass(Inst, Rt, Address, Decoder)))
      return MCDisassembler::Fail;
  }

  if (!Check(S, DecodeT2AddrModeImm8(Inst, imm, Address, Decoder)))
    return MCDisassembler::Fail;
  return S;
}

static DecodeStatus DecodeT2LoadImm12(MCInst &Inst, unsigned Insn,
                                      uint64_t Address,
                                      const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  unsigned Rt = fieldFromInstruction(Insn, 12, 4);
  unsigned imm = fieldFromInstruction(Insn, 0, 12);
  imm |= (Rn << 13);

  const FeatureBitset &featureBits =
    ((const MCDisassembler*)Decoder)->getSubtargetInfo().getFeatureBits();

  bool hasMP = featureBits[ARM::FeatureMP];
  bool hasV7Ops = featureBits[ARM::HasV7Ops];

  if (Rn == 15) {
    switch (Inst.getOpcode()) {
    case ARM::t2LDRi12:
      Inst.setOpcode(ARM::t2LDRpci);
      break;
    case ARM::t2LDRHi12:
      Inst.setOpcode(ARM::t2LDRHpci);
      break;
    case ARM::t2LDRSHi12:
      Inst.setOpcode(ARM::t2LDRSHpci);
      break;
    case ARM::t2LDRBi12:
      Inst.setOpcode(ARM::t2LDRBpci);
      break;
    case ARM::t2LDRSBi12:
      Inst.setOpcode(ARM::t2LDRSBpci);
      break;
    case ARM::t2PLDi12:
      Inst.setOpcode(ARM::t2PLDpci);
      break;
    case ARM::t2PLIi12:
      Inst.setOpcode(ARM::t2PLIpci);
      break;
    default:
      return MCDisassembler::Fail;
    }
    return DecodeT2LoadLabel(Inst, Insn, Address, Decoder);
  }

  if (Rt == 15) {
    switch (Inst.getOpcode()) {
    case ARM::t2LDRSHi12:
      return MCDisassembler::Fail;
    case ARM::t2LDRHi12:
      Inst.setOpcode(ARM::t2PLDWi12);
      break;
    case ARM::t2LDRSBi12:
      Inst.setOpcode(ARM::t2PLIi12);
      break;
    default:
      break;
    }
  }

  switch (Inst.getOpcode()) {
  case ARM::t2PLDi12:
    break;
  case ARM::t2PLIi12:
    if (!hasV7Ops)
      return MCDisassembler::Fail;
    break;
  case ARM::t2PLDWi12:
      if (!hasV7Ops || !hasMP)
        return MCDisassembler::Fail;
      break;
  default:
    if (!Check(S, DecodeGPRRegisterClass(Inst, Rt, Address, Decoder)))
      return MCDisassembler::Fail;
  }

  if (!Check(S, DecodeT2AddrModeImm12(Inst, imm, Address, Decoder)))
    return MCDisassembler::Fail;
  return S;
}

static DecodeStatus DecodeT2LoadT(MCInst &Inst, unsigned Insn, uint64_t Address,
                                  const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  unsigned Rt = fieldFromInstruction(Insn, 12, 4);
  unsigned imm = fieldFromInstruction(Insn, 0, 8);
  imm |= (Rn << 9);

  if (Rn == 15) {
    switch (Inst.getOpcode()) {
    case ARM::t2LDRT:
      Inst.setOpcode(ARM::t2LDRpci);
      break;
    case ARM::t2LDRBT:
      Inst.setOpcode(ARM::t2LDRBpci);
      break;
    case ARM::t2LDRHT:
      Inst.setOpcode(ARM::t2LDRHpci);
      break;
    case ARM::t2LDRSBT:
      Inst.setOpcode(ARM::t2LDRSBpci);
      break;
    case ARM::t2LDRSHT:
      Inst.setOpcode(ARM::t2LDRSHpci);
      break;
    default:
      return MCDisassembler::Fail;
    }
    return DecodeT2LoadLabel(Inst, Insn, Address, Decoder);
  }

  if (!Check(S, DecoderGPRRegisterClass(Inst, Rt, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeT2AddrModeImm8(Inst, imm, Address, Decoder)))
    return MCDisassembler::Fail;
  return S;
}

static DecodeStatus DecodeT2LoadLabel(MCInst &Inst, unsigned Insn,
                                      uint64_t Address,
                                      const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rt = fieldFromInstruction(Insn, 12, 4);
  unsigned U = fieldFromInstruction(Insn, 23, 1);
  int imm = fieldFromInstruction(Insn, 0, 12);

  const FeatureBitset &featureBits =
    ((const MCDisassembler*)Decoder)->getSubtargetInfo().getFeatureBits();

  bool hasV7Ops = featureBits[ARM::HasV7Ops];

  if (Rt == 15) {
    switch (Inst.getOpcode()) {
      case ARM::t2LDRBpci:
      case ARM::t2LDRHpci:
        Inst.setOpcode(ARM::t2PLDpci);
        break;
      case ARM::t2LDRSBpci:
        Inst.setOpcode(ARM::t2PLIpci);
        break;
      case ARM::t2LDRSHpci:
        return MCDisassembler::Fail;
      default:
        break;
    }
  }

  switch(Inst.getOpcode()) {
  case ARM::t2PLDpci:
    break;
  case ARM::t2PLIpci:
    if (!hasV7Ops)
      return MCDisassembler::Fail;
    break;
  default:
    if (!Check(S, DecodeGPRRegisterClass(Inst, Rt, Address, Decoder)))
      return MCDisassembler::Fail;
  }

  if (!U) {
    // Special case for #-0.
    if (imm == 0)
      imm = INT32_MIN;
    else
      imm = -imm;
  }
  Inst.addOperand(MCOperand::createImm(imm));

  return S;
}

static DecodeStatus DecodeT2Imm8S4(MCInst &Inst, unsigned Val, uint64_t Address,
                                   const MCDisassembler *Decoder) {
  if (Val == 0)
    Inst.addOperand(MCOperand::createImm(INT32_MIN));
  else {
    int imm = Val & 0xFF;

    if (!(Val & 0x100)) imm *= -1;
    Inst.addOperand(MCOperand::createImm(imm * 4));
  }

  return MCDisassembler::Success;
}

static DecodeStatus DecodeT2Imm7S4(MCInst &Inst, unsigned Val, uint64_t Address,
                                   const MCDisassembler *Decoder) {
  if (Val == 0)
    Inst.addOperand(MCOperand::createImm(INT32_MIN));
  else {
    int imm = Val & 0x7F;

    if (!(Val & 0x80))
      imm *= -1;
    Inst.addOperand(MCOperand::createImm(imm * 4));
  }

  return MCDisassembler::Success;
}

static DecodeStatus DecodeT2AddrModeImm8s4(MCInst &Inst, unsigned Val,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rn = fieldFromInstruction(Val, 9, 4);
  unsigned imm = fieldFromInstruction(Val, 0, 9);

  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeT2Imm8S4(Inst, imm, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

static DecodeStatus DecodeT2AddrModeImm7s4(MCInst &Inst, unsigned Val,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rn = fieldFromInstruction(Val, 8, 4);
  unsigned imm = fieldFromInstruction(Val, 0, 8);

  if (!Check(S, DecodeGPRnopcRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeT2Imm7S4(Inst, imm, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

static DecodeStatus DecodeT2AddrModeImm0_1020s4(MCInst &Inst, unsigned Val,
                                                uint64_t Address,
                                                const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rn = fieldFromInstruction(Val, 8, 4);
  unsigned imm = fieldFromInstruction(Val, 0, 8);

  if (!Check(S, DecodeGPRnopcRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;

  Inst.addOperand(MCOperand::createImm(imm));

  return S;
}

static DecodeStatus DecodeT2Imm8(MCInst &Inst, unsigned Val, uint64_t Address,
                                 const MCDisassembler *Decoder) {
  int imm = Val & 0xFF;
  if (Val == 0)
    imm = INT32_MIN;
  else if (!(Val & 0x100))
    imm *= -1;
  Inst.addOperand(MCOperand::createImm(imm));

  return MCDisassembler::Success;
}

template <int shift>
static DecodeStatus DecodeT2Imm7(MCInst &Inst, unsigned Val, uint64_t Address,
                                 const MCDisassembler *Decoder) {
  int imm = Val & 0x7F;
  if (Val == 0)
    imm = INT32_MIN;
  else if (!(Val & 0x80))
    imm *= -1;
  if (imm != INT32_MIN)
    imm *= (1U << shift);
  Inst.addOperand(MCOperand::createImm(imm));

  return MCDisassembler::Success;
}

static DecodeStatus DecodeT2AddrModeImm8(MCInst &Inst, unsigned Val,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rn = fieldFromInstruction(Val, 9, 4);
  unsigned imm = fieldFromInstruction(Val, 0, 9);

  // Thumb stores cannot use PC as dest register.
  switch (Inst.getOpcode()) {
  case ARM::t2STRT:
  case ARM::t2STRBT:
  case ARM::t2STRHT:
  case ARM::t2STRi8:
  case ARM::t2STRHi8:
  case ARM::t2STRBi8:
    if (Rn == 15)
      return MCDisassembler::Fail;
    break;
  default:
    break;
  }

  // Some instructions always use an additive offset.
  switch (Inst.getOpcode()) {
    case ARM::t2LDRT:
    case ARM::t2LDRBT:
    case ARM::t2LDRHT:
    case ARM::t2LDRSBT:
    case ARM::t2LDRSHT:
    case ARM::t2STRT:
    case ARM::t2STRBT:
    case ARM::t2STRHT:
      imm |= 0x100;
      break;
    default:
      break;
  }

  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeT2Imm8(Inst, imm, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

template <int shift>
static DecodeStatus DecodeTAddrModeImm7(MCInst &Inst, unsigned Val,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rn = fieldFromInstruction(Val, 8, 3);
  unsigned imm = fieldFromInstruction(Val, 0, 8);

  if (!Check(S, DecodetGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeT2Imm7<shift>(Inst, imm, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

template <int shift, int WriteBack>
static DecodeStatus DecodeT2AddrModeImm7(MCInst &Inst, unsigned Val,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rn = fieldFromInstruction(Val, 8, 4);
  unsigned imm = fieldFromInstruction(Val, 0, 8);
  if (WriteBack) {
    if (!Check(S, DecoderGPRRegisterClass(Inst, Rn, Address, Decoder)))
      return MCDisassembler::Fail;
  } else if (!Check(S, DecodeGPRnopcRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeT2Imm7<shift>(Inst, imm, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

static DecodeStatus DecodeT2LdStPre(MCInst &Inst, unsigned Insn,
                                    uint64_t Address,
                                    const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rt = fieldFromInstruction(Insn, 12, 4);
  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  unsigned addr = fieldFromInstruction(Insn, 0, 8);
  addr |= fieldFromInstruction(Insn, 9, 1) << 8;
  addr |= Rn << 9;
  unsigned load = fieldFromInstruction(Insn, 20, 1);

  if (Rn == 15) {
    switch (Inst.getOpcode()) {
    case ARM::t2LDR_PRE:
    case ARM::t2LDR_POST:
      Inst.setOpcode(ARM::t2LDRpci);
      break;
    case ARM::t2LDRB_PRE:
    case ARM::t2LDRB_POST:
      Inst.setOpcode(ARM::t2LDRBpci);
      break;
    case ARM::t2LDRH_PRE:
    case ARM::t2LDRH_POST:
      Inst.setOpcode(ARM::t2LDRHpci);
      break;
    case ARM::t2LDRSB_PRE:
    case ARM::t2LDRSB_POST:
      if (Rt == 15)
        Inst.setOpcode(ARM::t2PLIpci);
      else
        Inst.setOpcode(ARM::t2LDRSBpci);
      break;
    case ARM::t2LDRSH_PRE:
    case ARM::t2LDRSH_POST:
      Inst.setOpcode(ARM::t2LDRSHpci);
      break;
    default:
      return MCDisassembler::Fail;
    }
    return DecodeT2LoadLabel(Inst, Insn, Address, Decoder);
  }

  if (!load) {
    if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
      return MCDisassembler::Fail;
  }

  if (!Check(S, DecodeGPRRegisterClass(Inst, Rt, Address, Decoder)))
    return MCDisassembler::Fail;

  if (load) {
    if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
      return MCDisassembler::Fail;
  }

  if (!Check(S, DecodeT2AddrModeImm8(Inst, addr, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

static DecodeStatus DecodeT2AddrModeImm12(MCInst &Inst, unsigned Val,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rn = fieldFromInstruction(Val, 13, 4);
  unsigned imm = fieldFromInstruction(Val, 0, 12);

  // Thumb stores cannot use PC as dest register.
  switch (Inst.getOpcode()) {
  case ARM::t2STRi12:
  case ARM::t2STRBi12:
  case ARM::t2STRHi12:
    if (Rn == 15)
      return MCDisassembler::Fail;
    break;
  default:
    break;
  }

  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createImm(imm));

  return S;
}

static DecodeStatus DecodeThumbAddSPImm(MCInst &Inst, uint16_t Insn,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder) {
  unsigned imm = fieldFromInstruction(Insn, 0, 7);

  Inst.addOperand(MCOperand::createReg(ARM::SP));
  Inst.addOperand(MCOperand::createReg(ARM::SP));
  Inst.addOperand(MCOperand::createImm(imm));

  return MCDisassembler::Success;
}

static DecodeStatus DecodeThumbAddSPReg(MCInst &Inst, uint16_t Insn,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  if (Inst.getOpcode() == ARM::tADDrSP) {
    unsigned Rdm = fieldFromInstruction(Insn, 0, 3);
    Rdm |= fieldFromInstruction(Insn, 7, 1) << 3;

    if (!Check(S, DecodeGPRRegisterClass(Inst, Rdm, Address, Decoder)))
    return MCDisassembler::Fail;
    Inst.addOperand(MCOperand::createReg(ARM::SP));
    if (!Check(S, DecodeGPRRegisterClass(Inst, Rdm, Address, Decoder)))
    return MCDisassembler::Fail;
  } else if (Inst.getOpcode() == ARM::tADDspr) {
    unsigned Rm = fieldFromInstruction(Insn, 3, 4);

    Inst.addOperand(MCOperand::createReg(ARM::SP));
    Inst.addOperand(MCOperand::createReg(ARM::SP));
    if (!Check(S, DecodeGPRRegisterClass(Inst, Rm, Address, Decoder)))
    return MCDisassembler::Fail;
  }

  return S;
}

static DecodeStatus DecodeThumbCPS(MCInst &Inst, uint16_t Insn,
                                   uint64_t Address,
                                   const MCDisassembler *Decoder) {
  unsigned imod = fieldFromInstruction(Insn, 4, 1) | 0x2;
  unsigned flags = fieldFromInstruction(Insn, 0, 3);

  Inst.addOperand(MCOperand::createImm(imod));
  Inst.addOperand(MCOperand::createImm(flags));

  return MCDisassembler::Success;
}

static DecodeStatus DecodePostIdxReg(MCInst &Inst, unsigned Insn,
                                     uint64_t Address,
                                     const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;
  unsigned Rm = fieldFromInstruction(Insn, 0, 4);
  unsigned add = fieldFromInstruction(Insn, 4, 1);

  if (!Check(S, DecodeGPRnopcRegisterClass(Inst, Rm, Address, Decoder)))
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createImm(add));

  return S;
}

static DecodeStatus DecodeMveAddrModeRQ(MCInst &Inst, unsigned Insn,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;
  unsigned Rn = fieldFromInstruction(Insn, 3, 4);
  unsigned Qm = fieldFromInstruction(Insn, 0, 3);

  if (!Check(S, DecodeGPRnopcRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeMQPRRegisterClass(Inst, Qm, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

template <int shift>
static DecodeStatus DecodeMveAddrModeQ(MCInst &Inst, unsigned Insn,
                                       uint64_t Address,
                                       const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;
  unsigned Qm = fieldFromInstruction(Insn, 8, 3);
  int imm = fieldFromInstruction(Insn, 0, 7);

  if (!Check(S, DecodeMQPRRegisterClass(Inst, Qm, Address, Decoder)))
    return MCDisassembler::Fail;

  if(!fieldFromInstruction(Insn, 7, 1)) {
    if (imm == 0)
      imm = INT32_MIN;                 // indicate -0
    else
      imm *= -1;
  }
  if (imm != INT32_MIN)
    imm *= (1U << shift);
  Inst.addOperand(MCOperand::createImm(imm));

  return S;
}

static DecodeStatus DecodeThumbBLXOffset(MCInst &Inst, unsigned Val,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder) {
  // Val is passed in as S:J1:J2:imm10H:imm10L:'0'
  // Note only one trailing zero not two.  Also the J1 and J2 values are from
  // the encoded instruction.  So here change to I1 and I2 values via:
  // I1 = NOT(J1 EOR S);
  // I2 = NOT(J2 EOR S);
  // and build the imm32 with two trailing zeros as documented:
  // imm32 = SignExtend(S:I1:I2:imm10H:imm10L:'00', 32);
  unsigned S = (Val >> 23) & 1;
  unsigned J1 = (Val >> 22) & 1;
  unsigned J2 = (Val >> 21) & 1;
  unsigned I1 = !(J1 ^ S);
  unsigned I2 = !(J2 ^ S);
  unsigned tmp = (Val & ~0x600000) | (I1 << 22) | (I2 << 21);
  int imm32 = SignExtend32<25>(tmp << 1);

  if (!tryAddingSymbolicOperand(Address,
                                (Address & ~2u) + imm32 + 4,
                                true, 4, Inst, Decoder))
    Inst.addOperand(MCOperand::createImm(imm32));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeCoprocessor(MCInst &Inst, unsigned Val,
                                      uint64_t Address,
                                      const MCDisassembler *Decoder) {
  if (Val == 0xA || Val == 0xB)
    return MCDisassembler::Fail;

  const FeatureBitset &featureBits =
    ((const MCDisassembler*)Decoder)->getSubtargetInfo().getFeatureBits();

  if (!isValidCoprocessorNumber(Val, featureBits))
    return MCDisassembler::Fail;

  Inst.addOperand(MCOperand::createImm(Val));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeThumbTableBranch(MCInst &Inst, unsigned Insn,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder) {
  const FeatureBitset &FeatureBits =
    ((const MCDisassembler*)Decoder)->getSubtargetInfo().getFeatureBits();
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  unsigned Rm = fieldFromInstruction(Insn, 0, 4);

  if (Rn == 13 && !FeatureBits[ARM::HasV8Ops]) S = MCDisassembler::SoftFail;
  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecoderGPRRegisterClass(Inst, Rm, Address, Decoder)))
    return MCDisassembler::Fail;
  return S;
}

static DecodeStatus DecodeThumb2BCCInstruction(MCInst &Inst, unsigned Insn,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned pred = fieldFromInstruction(Insn, 22, 4);
  if (pred == 0xE || pred == 0xF) {
    unsigned opc = fieldFromInstruction(Insn, 4, 28);
    switch (opc) {
      default:
        return MCDisassembler::Fail;
      case 0xf3bf8f4:
        Inst.setOpcode(ARM::t2DSB);
        break;
      case 0xf3bf8f5:
        Inst.setOpcode(ARM::t2DMB);
        break;
      case 0xf3bf8f6:
        Inst.setOpcode(ARM::t2ISB);
        break;
    }

    unsigned imm = fieldFromInstruction(Insn, 0, 4);
    return DecodeMemBarrierOption(Inst, imm, Address, Decoder);
  }

  unsigned brtarget = fieldFromInstruction(Insn, 0, 11) << 1;
  brtarget |= fieldFromInstruction(Insn, 11, 1) << 19;
  brtarget |= fieldFromInstruction(Insn, 13, 1) << 18;
  brtarget |= fieldFromInstruction(Insn, 16, 6) << 12;
  brtarget |= fieldFromInstruction(Insn, 26, 1) << 20;

  if (!Check(S, DecodeT2BROperand(Inst, brtarget, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodePredicateOperand(Inst, pred, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

// Decode a shifted immediate operand.  These basically consist
// of an 8-bit value, and a 4-bit directive that specifies either
// a splat operation or a rotation.
static DecodeStatus DecodeT2SOImm(MCInst &Inst, unsigned Val, uint64_t Address,
                                  const MCDisassembler *Decoder) {
  unsigned ctrl = fieldFromInstruction(Val, 10, 2);
  if (ctrl == 0) {
    unsigned byte = fieldFromInstruction(Val, 8, 2);
    unsigned imm = fieldFromInstruction(Val, 0, 8);
    switch (byte) {
      case 0:
        Inst.addOperand(MCOperand::createImm(imm));
        break;
      case 1:
        Inst.addOperand(MCOperand::createImm((imm << 16) | imm));
        break;
      case 2:
        Inst.addOperand(MCOperand::createImm((imm << 24) | (imm << 8)));
        break;
      case 3:
        Inst.addOperand(MCOperand::createImm((imm << 24) | (imm << 16) |
                                             (imm << 8)  |  imm));
        break;
    }
  } else {
    unsigned unrot = fieldFromInstruction(Val, 0, 7) | 0x80;
    unsigned rot = fieldFromInstruction(Val, 7, 5);
    unsigned imm = llvm::rotr<uint32_t>(unrot, rot);
    Inst.addOperand(MCOperand::createImm(imm));
  }

  return MCDisassembler::Success;
}

static DecodeStatus DecodeThumbBCCTargetOperand(MCInst &Inst, unsigned Val,
                                                uint64_t Address,
                                                const MCDisassembler *Decoder) {
  if (!tryAddingSymbolicOperand(Address, Address + SignExtend32<9>(Val<<1) + 4,
                                true, 2, Inst, Decoder))
    Inst.addOperand(MCOperand::createImm(SignExtend32<9>(Val << 1)));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeThumbBLTargetOperand(MCInst &Inst, unsigned Val,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  // Val is passed in as S:J1:J2:imm10:imm11
  // Note no trailing zero after imm11.  Also the J1 and J2 values are from
  // the encoded instruction.  So here change to I1 and I2 values via:
  // I1 = NOT(J1 EOR S);
  // I2 = NOT(J2 EOR S);
  // and build the imm32 with one trailing zero as documented:
  // imm32 = SignExtend(S:I1:I2:imm10:imm11:'0', 32);
  unsigned S = (Val >> 23) & 1;
  unsigned J1 = (Val >> 22) & 1;
  unsigned J2 = (Val >> 21) & 1;
  unsigned I1 = !(J1 ^ S);
  unsigned I2 = !(J2 ^ S);
  unsigned tmp = (Val & ~0x600000) | (I1 << 22) | (I2 << 21);
  int imm32 = SignExtend32<25>(tmp << 1);

  if (!tryAddingSymbolicOperand(Address, Address + imm32 + 4,
                                true, 4, Inst, Decoder))
    Inst.addOperand(MCOperand::createImm(imm32));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeMemBarrierOption(MCInst &Inst, unsigned Val,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder) {
  if (Val & ~0xf)
    return MCDisassembler::Fail;

  Inst.addOperand(MCOperand::createImm(Val));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeInstSyncBarrierOption(MCInst &Inst, unsigned Val,
                                                uint64_t Address,
                                                const MCDisassembler *Decoder) {
  if (Val & ~0xf)
    return MCDisassembler::Fail;

  Inst.addOperand(MCOperand::createImm(Val));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeMSRMask(MCInst &Inst, unsigned Val, uint64_t Address,
                                  const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;
  const FeatureBitset &FeatureBits =
    ((const MCDisassembler*)Decoder)->getSubtargetInfo().getFeatureBits();

  if (FeatureBits[ARM::FeatureMClass]) {
    unsigned ValLow = Val & 0xff;

    // Validate the SYSm value first.
    switch (ValLow) {
    case  0: // apsr
    case  1: // iapsr
    case  2: // eapsr
    case  3: // xpsr
    case  5: // ipsr
    case  6: // epsr
    case  7: // iepsr
    case  8: // msp
    case  9: // psp
    case 16: // primask
    case 20: // control
      break;
    case 17: // basepri
    case 18: // basepri_max
    case 19: // faultmask
      if (!(FeatureBits[ARM::HasV7Ops]))
        // Values basepri, basepri_max and faultmask are only valid for v7m.
        return MCDisassembler::Fail;
      break;
    case 0x8a: // msplim_ns
    case 0x8b: // psplim_ns
    case 0x91: // basepri_ns
    case 0x93: // faultmask_ns
      if (!(FeatureBits[ARM::HasV8MMainlineOps]))
        return MCDisassembler::Fail;
      [[fallthrough]];
    case 10:   // msplim
    case 11:   // psplim
    case 0x88: // msp_ns
    case 0x89: // psp_ns
    case 0x90: // primask_ns
    case 0x94: // control_ns
    case 0x98: // sp_ns
      if (!(FeatureBits[ARM::Feature8MSecExt]))
        return MCDisassembler::Fail;
      break;
    case 0x20: // pac_key_p_0
    case 0x21: // pac_key_p_1
    case 0x22: // pac_key_p_2
    case 0x23: // pac_key_p_3
    case 0x24: // pac_key_u_0
    case 0x25: // pac_key_u_1
    case 0x26: // pac_key_u_2
    case 0x27: // pac_key_u_3
    case 0xa0: // pac_key_p_0_ns
    case 0xa1: // pac_key_p_1_ns
    case 0xa2: // pac_key_p_2_ns
    case 0xa3: // pac_key_p_3_ns
    case 0xa4: // pac_key_u_0_ns
    case 0xa5: // pac_key_u_1_ns
    case 0xa6: // pac_key_u_2_ns
    case 0xa7: // pac_key_u_3_ns
      if (!(FeatureBits[ARM::FeaturePACBTI]))
        return MCDisassembler::Fail;
      break;
    default:
      // Architecturally defined as unpredictable
      S = MCDisassembler::SoftFail;
      break;
    }

    if (Inst.getOpcode() == ARM::t2MSR_M) {
      unsigned Mask = fieldFromInstruction(Val, 10, 2);
      if (!(FeatureBits[ARM::HasV7Ops])) {
        // The ARMv6-M MSR bits {11-10} can be only 0b10, other values are
        // unpredictable.
        if (Mask != 2)
          S = MCDisassembler::SoftFail;
      }
      else {
        // The ARMv7-M architecture stores an additional 2-bit mask value in
        // MSR bits {11-10}. The mask is used only with apsr, iapsr, eapsr and
        // xpsr, it has to be 0b10 in other cases. Bit mask{1} indicates if
        // the NZCVQ bits should be moved by the instruction. Bit mask{0}
        // indicates the move for the GE{3:0} bits, the mask{0} bit can be set
        // only if the processor includes the DSP extension.
        if (Mask == 0 || (Mask != 2 && ValLow > 3) ||
            (!(FeatureBits[ARM::FeatureDSP]) && (Mask & 1)))
          S = MCDisassembler::SoftFail;
      }
    }
  } else {
    // A/R class
    if (Val == 0)
      return MCDisassembler::Fail;
  }
  Inst.addOperand(MCOperand::createImm(Val));
  return S;
}

static DecodeStatus DecodeBankedReg(MCInst &Inst, unsigned Val,
                                    uint64_t Address,
                                    const MCDisassembler *Decoder) {
  unsigned R = fieldFromInstruction(Val, 5, 1);
  unsigned SysM = fieldFromInstruction(Val, 0, 5);

  // The table of encodings for these banked registers comes from B9.2.3 of the
  // ARM ARM. There are patterns, but nothing regular enough to make this logic
  // neater. So by fiat, these values are UNPREDICTABLE:
  if (!ARMBankedReg::lookupBankedRegByEncoding((R << 5) | SysM))
    return MCDisassembler::Fail;

  Inst.addOperand(MCOperand::createImm(Val));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeDoubleRegLoad(MCInst &Inst, unsigned Insn,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rt = fieldFromInstruction(Insn, 12, 4);
  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  unsigned pred = fieldFromInstruction(Insn, 28, 4);

  if (Rn == 0xF)
    S = MCDisassembler::SoftFail;

  if (!Check(S, DecodeGPRPairRegisterClass(Inst, Rt, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodePredicateOperand(Inst, pred, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

static DecodeStatus DecodeDoubleRegStore(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rd = fieldFromInstruction(Insn, 12, 4);
  unsigned Rt = fieldFromInstruction(Insn, 0, 4);
  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  unsigned pred = fieldFromInstruction(Insn, 28, 4);

  if (!Check(S, DecodeGPRnopcRegisterClass(Inst, Rd, Address, Decoder)))
    return MCDisassembler::Fail;

  if (Rn == 0xF || Rd == Rn || Rd == Rt || Rd == Rt+1)
    S = MCDisassembler::SoftFail;

  if (!Check(S, DecodeGPRPairRegisterClass(Inst, Rt, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodePredicateOperand(Inst, pred, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

static DecodeStatus DecodeLDRPreImm(MCInst &Inst, unsigned Insn,
                                    uint64_t Address,
                                    const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  unsigned Rt = fieldFromInstruction(Insn, 12, 4);
  unsigned imm = fieldFromInstruction(Insn, 0, 12);
  imm |= fieldFromInstruction(Insn, 16, 4) << 13;
  imm |= fieldFromInstruction(Insn, 23, 1) << 12;
  unsigned pred = fieldFromInstruction(Insn, 28, 4);

  if (Rn == 0xF || Rn == Rt) S = MCDisassembler::SoftFail;

  if (!Check(S, DecodeGPRRegisterClass(Inst, Rt, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeAddrModeImm12Operand(Inst, imm, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodePredicateOperand(Inst, pred, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

static DecodeStatus DecodeLDRPreReg(MCInst &Inst, unsigned Insn,
                                    uint64_t Address,
                                    const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  unsigned Rt = fieldFromInstruction(Insn, 12, 4);
  unsigned imm = fieldFromInstruction(Insn, 0, 12);
  imm |= fieldFromInstruction(Insn, 16, 4) << 13;
  imm |= fieldFromInstruction(Insn, 23, 1) << 12;
  unsigned pred = fieldFromInstruction(Insn, 28, 4);
  unsigned Rm = fieldFromInstruction(Insn, 0, 4);

  if (Rn == 0xF || Rn == Rt) S = MCDisassembler::SoftFail;
  if (Rm == 0xF) S = MCDisassembler::SoftFail;

  if (!Check(S, DecodeGPRRegisterClass(Inst, Rt, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeSORegMemOperand(Inst, imm, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodePredicateOperand(Inst, pred, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

static DecodeStatus DecodeSTRPreImm(MCInst &Inst, unsigned Insn,
                                    uint64_t Address,
                                    const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  unsigned Rt = fieldFromInstruction(Insn, 12, 4);
  unsigned imm = fieldFromInstruction(Insn, 0, 12);
  imm |= fieldFromInstruction(Insn, 16, 4) << 13;
  imm |= fieldFromInstruction(Insn, 23, 1) << 12;
  unsigned pred = fieldFromInstruction(Insn, 28, 4);

  if (Rn == 0xF || Rn == Rt) S = MCDisassembler::SoftFail;

  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeGPRRegisterClass(Inst, Rt, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeAddrModeImm12Operand(Inst, imm, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodePredicateOperand(Inst, pred, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

static DecodeStatus DecodeSTRPreReg(MCInst &Inst, unsigned Insn,
                                    uint64_t Address,
                                    const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  unsigned Rt = fieldFromInstruction(Insn, 12, 4);
  unsigned imm = fieldFromInstruction(Insn, 0, 12);
  imm |= fieldFromInstruction(Insn, 16, 4) << 13;
  imm |= fieldFromInstruction(Insn, 23, 1) << 12;
  unsigned pred = fieldFromInstruction(Insn, 28, 4);

  if (Rn == 0xF || Rn == Rt) S = MCDisassembler::SoftFail;

  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeGPRRegisterClass(Inst, Rt, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeSORegMemOperand(Inst, imm, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodePredicateOperand(Inst, pred, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

static DecodeStatus DecodeVLD1LN(MCInst &Inst, unsigned Insn, uint64_t Address,
                                 const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  unsigned Rm = fieldFromInstruction(Insn, 0, 4);
  unsigned Rd = fieldFromInstruction(Insn, 12, 4);
  Rd |= fieldFromInstruction(Insn, 22, 1) << 4;
  unsigned size = fieldFromInstruction(Insn, 10, 2);

  unsigned align = 0;
  unsigned index = 0;
  switch (size) {
    default:
      return MCDisassembler::Fail;
    case 0:
      if (fieldFromInstruction(Insn, 4, 1))
        return MCDisassembler::Fail; // UNDEFINED
      index = fieldFromInstruction(Insn, 5, 3);
      break;
    case 1:
      if (fieldFromInstruction(Insn, 5, 1))
        return MCDisassembler::Fail; // UNDEFINED
      index = fieldFromInstruction(Insn, 6, 2);
      if (fieldFromInstruction(Insn, 4, 1))
        align = 2;
      break;
    case 2:
      if (fieldFromInstruction(Insn, 6, 1))
        return MCDisassembler::Fail; // UNDEFINED
      index = fieldFromInstruction(Insn, 7, 1);

      switch (fieldFromInstruction(Insn, 4, 2)) {
        case 0 :
          align = 0; break;
        case 3:
          align = 4; break;
        default:
          return MCDisassembler::Fail;
      }
      break;
  }

  if (!Check(S, DecodeDPRRegisterClass(Inst, Rd, Address, Decoder)))
    return MCDisassembler::Fail;
  if (Rm != 0xF) { // Writeback
    if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
      return MCDisassembler::Fail;
  }
  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createImm(align));
  if (Rm != 0xF) {
    if (Rm != 0xD) {
      if (!Check(S, DecodeGPRRegisterClass(Inst, Rm, Address, Decoder)))
        return MCDisassembler::Fail;
    } else
      Inst.addOperand(MCOperand::createReg(0));
  }

  if (!Check(S, DecodeDPRRegisterClass(Inst, Rd, Address, Decoder)))
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createImm(index));

  return S;
}

static DecodeStatus DecodeVST1LN(MCInst &Inst, unsigned Insn, uint64_t Address,
                                 const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  unsigned Rm = fieldFromInstruction(Insn, 0, 4);
  unsigned Rd = fieldFromInstruction(Insn, 12, 4);
  Rd |= fieldFromInstruction(Insn, 22, 1) << 4;
  unsigned size = fieldFromInstruction(Insn, 10, 2);

  unsigned align = 0;
  unsigned index = 0;
  switch (size) {
    default:
      return MCDisassembler::Fail;
    case 0:
      if (fieldFromInstruction(Insn, 4, 1))
        return MCDisassembler::Fail; // UNDEFINED
      index = fieldFromInstruction(Insn, 5, 3);
      break;
    case 1:
      if (fieldFromInstruction(Insn, 5, 1))
        return MCDisassembler::Fail; // UNDEFINED
      index = fieldFromInstruction(Insn, 6, 2);
      if (fieldFromInstruction(Insn, 4, 1))
        align = 2;
      break;
    case 2:
      if (fieldFromInstruction(Insn, 6, 1))
        return MCDisassembler::Fail; // UNDEFINED
      index = fieldFromInstruction(Insn, 7, 1);

      switch (fieldFromInstruction(Insn, 4, 2)) {
        case 0:
          align = 0; break;
        case 3:
          align = 4; break;
        default:
          return MCDisassembler::Fail;
      }
      break;
  }

  if (Rm != 0xF) { // Writeback
    if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  }
  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createImm(align));
  if (Rm != 0xF) {
    if (Rm != 0xD) {
      if (!Check(S, DecodeGPRRegisterClass(Inst, Rm, Address, Decoder)))
    return MCDisassembler::Fail;
    } else
      Inst.addOperand(MCOperand::createReg(0));
  }

  if (!Check(S, DecodeDPRRegisterClass(Inst, Rd, Address, Decoder)))
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createImm(index));

  return S;
}

static DecodeStatus DecodeVLD2LN(MCInst &Inst, unsigned Insn, uint64_t Address,
                                 const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  unsigned Rm = fieldFromInstruction(Insn, 0, 4);
  unsigned Rd = fieldFromInstruction(Insn, 12, 4);
  Rd |= fieldFromInstruction(Insn, 22, 1) << 4;
  unsigned size = fieldFromInstruction(Insn, 10, 2);

  unsigned align = 0;
  unsigned index = 0;
  unsigned inc = 1;
  switch (size) {
    default:
      return MCDisassembler::Fail;
    case 0:
      index = fieldFromInstruction(Insn, 5, 3);
      if (fieldFromInstruction(Insn, 4, 1))
        align = 2;
      break;
    case 1:
      index = fieldFromInstruction(Insn, 6, 2);
      if (fieldFromInstruction(Insn, 4, 1))
        align = 4;
      if (fieldFromInstruction(Insn, 5, 1))
        inc = 2;
      break;
    case 2:
      if (fieldFromInstruction(Insn, 5, 1))
        return MCDisassembler::Fail; // UNDEFINED
      index = fieldFromInstruction(Insn, 7, 1);
      if (fieldFromInstruction(Insn, 4, 1) != 0)
        align = 8;
      if (fieldFromInstruction(Insn, 6, 1))
        inc = 2;
      break;
  }

  if (!Check(S, DecodeDPRRegisterClass(Inst, Rd, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeDPRRegisterClass(Inst, Rd+inc, Address, Decoder)))
    return MCDisassembler::Fail;
  if (Rm != 0xF) { // Writeback
    if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
      return MCDisassembler::Fail;
  }
  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createImm(align));
  if (Rm != 0xF) {
    if (Rm != 0xD) {
      if (!Check(S, DecodeGPRRegisterClass(Inst, Rm, Address, Decoder)))
        return MCDisassembler::Fail;
    } else
      Inst.addOperand(MCOperand::createReg(0));
  }

  if (!Check(S, DecodeDPRRegisterClass(Inst, Rd, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeDPRRegisterClass(Inst, Rd+inc, Address, Decoder)))
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createImm(index));

  return S;
}

static DecodeStatus DecodeVST2LN(MCInst &Inst, unsigned Insn, uint64_t Address,
                                 const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  unsigned Rm = fieldFromInstruction(Insn, 0, 4);
  unsigned Rd = fieldFromInstruction(Insn, 12, 4);
  Rd |= fieldFromInstruction(Insn, 22, 1) << 4;
  unsigned size = fieldFromInstruction(Insn, 10, 2);

  unsigned align = 0;
  unsigned index = 0;
  unsigned inc = 1;
  switch (size) {
    default:
      return MCDisassembler::Fail;
    case 0:
      index = fieldFromInstruction(Insn, 5, 3);
      if (fieldFromInstruction(Insn, 4, 1))
        align = 2;
      break;
    case 1:
      index = fieldFromInstruction(Insn, 6, 2);
      if (fieldFromInstruction(Insn, 4, 1))
        align = 4;
      if (fieldFromInstruction(Insn, 5, 1))
        inc = 2;
      break;
    case 2:
      if (fieldFromInstruction(Insn, 5, 1))
        return MCDisassembler::Fail; // UNDEFINED
      index = fieldFromInstruction(Insn, 7, 1);
      if (fieldFromInstruction(Insn, 4, 1) != 0)
        align = 8;
      if (fieldFromInstruction(Insn, 6, 1))
        inc = 2;
      break;
  }

  if (Rm != 0xF) { // Writeback
    if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
      return MCDisassembler::Fail;
  }
  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createImm(align));
  if (Rm != 0xF) {
    if (Rm != 0xD) {
      if (!Check(S, DecodeGPRRegisterClass(Inst, Rm, Address, Decoder)))
        return MCDisassembler::Fail;
    } else
      Inst.addOperand(MCOperand::createReg(0));
  }

  if (!Check(S, DecodeDPRRegisterClass(Inst, Rd, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeDPRRegisterClass(Inst, Rd+inc, Address, Decoder)))
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createImm(index));

  return S;
}

static DecodeStatus DecodeVLD3LN(MCInst &Inst, unsigned Insn, uint64_t Address,
                                 const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  unsigned Rm = fieldFromInstruction(Insn, 0, 4);
  unsigned Rd = fieldFromInstruction(Insn, 12, 4);
  Rd |= fieldFromInstruction(Insn, 22, 1) << 4;
  unsigned size = fieldFromInstruction(Insn, 10, 2);

  unsigned align = 0;
  unsigned index = 0;
  unsigned inc = 1;
  switch (size) {
    default:
      return MCDisassembler::Fail;
    case 0:
      if (fieldFromInstruction(Insn, 4, 1))
        return MCDisassembler::Fail; // UNDEFINED
      index = fieldFromInstruction(Insn, 5, 3);
      break;
    case 1:
      if (fieldFromInstruction(Insn, 4, 1))
        return MCDisassembler::Fail; // UNDEFINED
      index = fieldFromInstruction(Insn, 6, 2);
      if (fieldFromInstruction(Insn, 5, 1))
        inc = 2;
      break;
    case 2:
      if (fieldFromInstruction(Insn, 4, 2))
        return MCDisassembler::Fail; // UNDEFINED
      index = fieldFromInstruction(Insn, 7, 1);
      if (fieldFromInstruction(Insn, 6, 1))
        inc = 2;
      break;
  }

  if (!Check(S, DecodeDPRRegisterClass(Inst, Rd, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeDPRRegisterClass(Inst, Rd+inc, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeDPRRegisterClass(Inst, Rd+2*inc, Address, Decoder)))
    return MCDisassembler::Fail;

  if (Rm != 0xF) { // Writeback
    if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  }
  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createImm(align));
  if (Rm != 0xF) {
    if (Rm != 0xD) {
      if (!Check(S, DecodeGPRRegisterClass(Inst, Rm, Address, Decoder)))
    return MCDisassembler::Fail;
    } else
      Inst.addOperand(MCOperand::createReg(0));
  }

  if (!Check(S, DecodeDPRRegisterClass(Inst, Rd, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeDPRRegisterClass(Inst, Rd+inc, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeDPRRegisterClass(Inst, Rd+2*inc, Address, Decoder)))
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createImm(index));

  return S;
}

static DecodeStatus DecodeVST3LN(MCInst &Inst, unsigned Insn, uint64_t Address,
                                 const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  unsigned Rm = fieldFromInstruction(Insn, 0, 4);
  unsigned Rd = fieldFromInstruction(Insn, 12, 4);
  Rd |= fieldFromInstruction(Insn, 22, 1) << 4;
  unsigned size = fieldFromInstruction(Insn, 10, 2);

  unsigned align = 0;
  unsigned index = 0;
  unsigned inc = 1;
  switch (size) {
    default:
      return MCDisassembler::Fail;
    case 0:
      if (fieldFromInstruction(Insn, 4, 1))
        return MCDisassembler::Fail; // UNDEFINED
      index = fieldFromInstruction(Insn, 5, 3);
      break;
    case 1:
      if (fieldFromInstruction(Insn, 4, 1))
        return MCDisassembler::Fail; // UNDEFINED
      index = fieldFromInstruction(Insn, 6, 2);
      if (fieldFromInstruction(Insn, 5, 1))
        inc = 2;
      break;
    case 2:
      if (fieldFromInstruction(Insn, 4, 2))
        return MCDisassembler::Fail; // UNDEFINED
      index = fieldFromInstruction(Insn, 7, 1);
      if (fieldFromInstruction(Insn, 6, 1))
        inc = 2;
      break;
  }

  if (Rm != 0xF) { // Writeback
    if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  }
  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createImm(align));
  if (Rm != 0xF) {
    if (Rm != 0xD) {
      if (!Check(S, DecodeGPRRegisterClass(Inst, Rm, Address, Decoder)))
    return MCDisassembler::Fail;
    } else
      Inst.addOperand(MCOperand::createReg(0));
  }

  if (!Check(S, DecodeDPRRegisterClass(Inst, Rd, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeDPRRegisterClass(Inst, Rd+inc, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeDPRRegisterClass(Inst, Rd+2*inc, Address, Decoder)))
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createImm(index));

  return S;
}

static DecodeStatus DecodeVLD4LN(MCInst &Inst, unsigned Insn, uint64_t Address,
                                 const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  unsigned Rm = fieldFromInstruction(Insn, 0, 4);
  unsigned Rd = fieldFromInstruction(Insn, 12, 4);
  Rd |= fieldFromInstruction(Insn, 22, 1) << 4;
  unsigned size = fieldFromInstruction(Insn, 10, 2);

  unsigned align = 0;
  unsigned index = 0;
  unsigned inc = 1;
  switch (size) {
    default:
      return MCDisassembler::Fail;
    case 0:
      if (fieldFromInstruction(Insn, 4, 1))
        align = 4;
      index = fieldFromInstruction(Insn, 5, 3);
      break;
    case 1:
      if (fieldFromInstruction(Insn, 4, 1))
        align = 8;
      index = fieldFromInstruction(Insn, 6, 2);
      if (fieldFromInstruction(Insn, 5, 1))
        inc = 2;
      break;
    case 2:
      switch (fieldFromInstruction(Insn, 4, 2)) {
        case 0:
          align = 0; break;
        case 3:
          return MCDisassembler::Fail;
        default:
          align = 4 << fieldFromInstruction(Insn, 4, 2); break;
      }

      index = fieldFromInstruction(Insn, 7, 1);
      if (fieldFromInstruction(Insn, 6, 1))
        inc = 2;
      break;
  }

  if (!Check(S, DecodeDPRRegisterClass(Inst, Rd, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeDPRRegisterClass(Inst, Rd+inc, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeDPRRegisterClass(Inst, Rd+2*inc, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeDPRRegisterClass(Inst, Rd+3*inc, Address, Decoder)))
    return MCDisassembler::Fail;

  if (Rm != 0xF) { // Writeback
    if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
      return MCDisassembler::Fail;
  }
  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createImm(align));
  if (Rm != 0xF) {
    if (Rm != 0xD) {
      if (!Check(S, DecodeGPRRegisterClass(Inst, Rm, Address, Decoder)))
        return MCDisassembler::Fail;
    } else
      Inst.addOperand(MCOperand::createReg(0));
  }

  if (!Check(S, DecodeDPRRegisterClass(Inst, Rd, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeDPRRegisterClass(Inst, Rd+inc, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeDPRRegisterClass(Inst, Rd+2*inc, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeDPRRegisterClass(Inst, Rd+3*inc, Address, Decoder)))
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createImm(index));

  return S;
}

static DecodeStatus DecodeVST4LN(MCInst &Inst, unsigned Insn, uint64_t Address,
                                 const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  unsigned Rm = fieldFromInstruction(Insn, 0, 4);
  unsigned Rd = fieldFromInstruction(Insn, 12, 4);
  Rd |= fieldFromInstruction(Insn, 22, 1) << 4;
  unsigned size = fieldFromInstruction(Insn, 10, 2);

  unsigned align = 0;
  unsigned index = 0;
  unsigned inc = 1;
  switch (size) {
    default:
      return MCDisassembler::Fail;
    case 0:
      if (fieldFromInstruction(Insn, 4, 1))
        align = 4;
      index = fieldFromInstruction(Insn, 5, 3);
      break;
    case 1:
      if (fieldFromInstruction(Insn, 4, 1))
        align = 8;
      index = fieldFromInstruction(Insn, 6, 2);
      if (fieldFromInstruction(Insn, 5, 1))
        inc = 2;
      break;
    case 2:
      switch (fieldFromInstruction(Insn, 4, 2)) {
        case 0:
          align = 0; break;
        case 3:
          return MCDisassembler::Fail;
        default:
          align = 4 << fieldFromInstruction(Insn, 4, 2); break;
      }

      index = fieldFromInstruction(Insn, 7, 1);
      if (fieldFromInstruction(Insn, 6, 1))
        inc = 2;
      break;
  }

  if (Rm != 0xF) { // Writeback
    if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  }
  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createImm(align));
  if (Rm != 0xF) {
    if (Rm != 0xD) {
      if (!Check(S, DecodeGPRRegisterClass(Inst, Rm, Address, Decoder)))
    return MCDisassembler::Fail;
    } else
      Inst.addOperand(MCOperand::createReg(0));
  }

  if (!Check(S, DecodeDPRRegisterClass(Inst, Rd, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeDPRRegisterClass(Inst, Rd+inc, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeDPRRegisterClass(Inst, Rd+2*inc, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeDPRRegisterClass(Inst, Rd+3*inc, Address, Decoder)))
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createImm(index));

  return S;
}

static DecodeStatus DecodeVMOVSRR(MCInst &Inst, unsigned Insn, uint64_t Address,
                                  const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;
  unsigned Rt  = fieldFromInstruction(Insn, 12, 4);
  unsigned Rt2 = fieldFromInstruction(Insn, 16, 4);
  unsigned Rm  = fieldFromInstruction(Insn,  5, 1);
  unsigned pred = fieldFromInstruction(Insn, 28, 4);
  Rm |= fieldFromInstruction(Insn, 0, 4) << 1;

  if (Rt == 0xF || Rt2 == 0xF || Rm == 0x1F)
    S = MCDisassembler::SoftFail;

  if (!Check(S, DecodeSPRRegisterClass(Inst, Rm  , Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeSPRRegisterClass(Inst, Rm+1, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeGPRRegisterClass(Inst, Rt  , Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeGPRRegisterClass(Inst, Rt2 , Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodePredicateOperand(Inst, pred, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

static DecodeStatus DecodeVMOVRRS(MCInst &Inst, unsigned Insn, uint64_t Address,
                                  const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;
  unsigned Rt  = fieldFromInstruction(Insn, 12, 4);
  unsigned Rt2 = fieldFromInstruction(Insn, 16, 4);
  unsigned Rm  = fieldFromInstruction(Insn,  5, 1);
  unsigned pred = fieldFromInstruction(Insn, 28, 4);
  Rm |= fieldFromInstruction(Insn, 0, 4) << 1;

  if (Rt == 0xF || Rt2 == 0xF || Rm == 0x1F)
    S = MCDisassembler::SoftFail;

  if (!Check(S, DecodeGPRRegisterClass(Inst, Rt  , Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeGPRRegisterClass(Inst, Rt2 , Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeSPRRegisterClass(Inst, Rm  , Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeSPRRegisterClass(Inst, Rm+1, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodePredicateOperand(Inst, pred, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

static DecodeStatus DecodeIT(MCInst &Inst, unsigned Insn, uint64_t Address,
                             const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;
  unsigned pred = fieldFromInstruction(Insn, 4, 4);
  unsigned mask = fieldFromInstruction(Insn, 0, 4);

  if (pred == 0xF) {
    pred = 0xE;
    S = MCDisassembler::SoftFail;
  }

  if (mask == 0x0)
    return MCDisassembler::Fail;

  // IT masks are encoded as a sequence of replacement low-order bits
  // for the condition code. So if the low bit of the starting
  // condition code is 1, then we have to flip all the bits above the
  // terminating bit (which is the lowest 1 bit).
  if (pred & 1) {
    unsigned LowBit = mask & -mask;
    unsigned BitsAboveLowBit = 0xF & (-LowBit << 1);
    mask ^= BitsAboveLowBit;
  }

  Inst.addOperand(MCOperand::createImm(pred));
  Inst.addOperand(MCOperand::createImm(mask));
  return S;
}

static DecodeStatus DecodeT2LDRDPreInstruction(MCInst &Inst, unsigned Insn,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rt = fieldFromInstruction(Insn, 12, 4);
  unsigned Rt2 = fieldFromInstruction(Insn, 8, 4);
  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  unsigned addr = fieldFromInstruction(Insn, 0, 8);
  unsigned W = fieldFromInstruction(Insn, 21, 1);
  unsigned U = fieldFromInstruction(Insn, 23, 1);
  unsigned P = fieldFromInstruction(Insn, 24, 1);
  bool writeback = (W == 1) | (P == 0);

  addr |= (U << 8) | (Rn << 9);

  if (writeback && (Rn == Rt || Rn == Rt2))
    Check(S, MCDisassembler::SoftFail);
  if (Rt == Rt2)
    Check(S, MCDisassembler::SoftFail);

  // Rt
  if (!Check(S, DecoderGPRRegisterClass(Inst, Rt, Address, Decoder)))
    return MCDisassembler::Fail;
  // Rt2
  if (!Check(S, DecoderGPRRegisterClass(Inst, Rt2, Address, Decoder)))
    return MCDisassembler::Fail;
  // Writeback operand
  if (!Check(S, DecoderGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  // addr
  if (!Check(S, DecodeT2AddrModeImm8s4(Inst, addr, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

static DecodeStatus DecodeT2STRDPreInstruction(MCInst &Inst, unsigned Insn,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rt = fieldFromInstruction(Insn, 12, 4);
  unsigned Rt2 = fieldFromInstruction(Insn, 8, 4);
  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  unsigned addr = fieldFromInstruction(Insn, 0, 8);
  unsigned W = fieldFromInstruction(Insn, 21, 1);
  unsigned U = fieldFromInstruction(Insn, 23, 1);
  unsigned P = fieldFromInstruction(Insn, 24, 1);
  bool writeback = (W == 1) | (P == 0);

  addr |= (U << 8) | (Rn << 9);

  if (writeback && (Rn == Rt || Rn == Rt2))
    Check(S, MCDisassembler::SoftFail);

  // Writeback operand
  if (!Check(S, DecoderGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  // Rt
  if (!Check(S, DecoderGPRRegisterClass(Inst, Rt, Address, Decoder)))
    return MCDisassembler::Fail;
  // Rt2
  if (!Check(S, DecoderGPRRegisterClass(Inst, Rt2, Address, Decoder)))
    return MCDisassembler::Fail;
  // addr
  if (!Check(S, DecodeT2AddrModeImm8s4(Inst, addr, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

static DecodeStatus DecodeT2Adr(MCInst &Inst, uint32_t Insn, uint64_t Address,
                                const MCDisassembler *Decoder) {
  unsigned sign1 = fieldFromInstruction(Insn, 21, 1);
  unsigned sign2 = fieldFromInstruction(Insn, 23, 1);
  if (sign1 != sign2) return MCDisassembler::Fail;
  const unsigned Rd = fieldFromInstruction(Insn, 8, 4);
  assert(Inst.getNumOperands() == 0 && "We should receive an empty Inst");
  DecodeStatus S = DecoderGPRRegisterClass(Inst, Rd, Address, Decoder);

  unsigned Val = fieldFromInstruction(Insn, 0, 8);
  Val |= fieldFromInstruction(Insn, 12, 3) << 8;
  Val |= fieldFromInstruction(Insn, 26, 1) << 11;
  // If sign, then it is decreasing the address.
  if (sign1) {
    // Following ARMv7 Architecture Manual, when the offset
    // is zero, it is decoded as a subw, not as a adr.w
    if (!Val) {
      Inst.setOpcode(ARM::t2SUBri12);
      Inst.addOperand(MCOperand::createReg(ARM::PC));
    } else
      Val = -Val;
  }
  Inst.addOperand(MCOperand::createImm(Val));
  return S;
}

static DecodeStatus DecodeT2ShifterImmOperand(MCInst &Inst, uint32_t Val,
                                              uint64_t Address,
                                              const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  // Shift of "asr #32" is not allowed in Thumb2 mode.
  if (Val == 0x20) S = MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createImm(Val));
  return S;
}

static DecodeStatus DecodeSwap(MCInst &Inst, unsigned Insn, uint64_t Address,
                               const MCDisassembler *Decoder) {
  unsigned Rt   = fieldFromInstruction(Insn, 12, 4);
  unsigned Rt2  = fieldFromInstruction(Insn, 0,  4);
  unsigned Rn   = fieldFromInstruction(Insn, 16, 4);
  unsigned pred = fieldFromInstruction(Insn, 28, 4);

  if (pred == 0xF)
    return DecodeCPSInstruction(Inst, Insn, Address, Decoder);

  DecodeStatus S = MCDisassembler::Success;

  if (Rt == Rn || Rn == Rt2)
    S = MCDisassembler::SoftFail;

  if (!Check(S, DecodeGPRnopcRegisterClass(Inst, Rt, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeGPRnopcRegisterClass(Inst, Rt2, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeGPRnopcRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodePredicateOperand(Inst, pred, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

static DecodeStatus DecodeVCVTD(MCInst &Inst, unsigned Insn, uint64_t Address,
                                const MCDisassembler *Decoder) {
  const FeatureBitset &featureBits =
      ((const MCDisassembler *)Decoder)->getSubtargetInfo().getFeatureBits();
  bool hasFullFP16 = featureBits[ARM::FeatureFullFP16];

  unsigned Vd = (fieldFromInstruction(Insn, 12, 4) << 0);
  Vd |= (fieldFromInstruction(Insn, 22, 1) << 4);
  unsigned Vm = (fieldFromInstruction(Insn, 0, 4) << 0);
  Vm |= (fieldFromInstruction(Insn, 5, 1) << 4);
  unsigned imm = fieldFromInstruction(Insn, 16, 6);
  unsigned cmode = fieldFromInstruction(Insn, 8, 4);
  unsigned op = fieldFromInstruction(Insn, 5, 1);

  DecodeStatus S = MCDisassembler::Success;

  // If the top 3 bits of imm are clear, this is a VMOV (immediate)
  if (!(imm & 0x38)) {
    if (cmode == 0xF) {
      if (op == 1) return MCDisassembler::Fail;
      Inst.setOpcode(ARM::VMOVv2f32);
    }
    if (hasFullFP16) {
      if (cmode == 0xE) {
        if (op == 1) {
          Inst.setOpcode(ARM::VMOVv1i64);
        } else {
          Inst.setOpcode(ARM::VMOVv8i8);
        }
      }
      if (cmode == 0xD) {
        if (op == 1) {
          Inst.setOpcode(ARM::VMVNv2i32);
        } else {
          Inst.setOpcode(ARM::VMOVv2i32);
        }
      }
      if (cmode == 0xC) {
        if (op == 1) {
          Inst.setOpcode(ARM::VMVNv2i32);
        } else {
          Inst.setOpcode(ARM::VMOVv2i32);
        }
      }
    }
    return DecodeVMOVModImmInstruction(Inst, Insn, Address, Decoder);
  }

  if (!(imm & 0x20)) return MCDisassembler::Fail;

  if (!Check(S, DecodeDPRRegisterClass(Inst, Vd, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeDPRRegisterClass(Inst, Vm, Address, Decoder)))
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createImm(64 - imm));

  return S;
}

static DecodeStatus DecodeVCVTQ(MCInst &Inst, unsigned Insn, uint64_t Address,
                                const MCDisassembler *Decoder) {
  const FeatureBitset &featureBits =
      ((const MCDisassembler *)Decoder)->getSubtargetInfo().getFeatureBits();
  bool hasFullFP16 = featureBits[ARM::FeatureFullFP16];

  unsigned Vd = (fieldFromInstruction(Insn, 12, 4) << 0);
  Vd |= (fieldFromInstruction(Insn, 22, 1) << 4);
  unsigned Vm = (fieldFromInstruction(Insn, 0, 4) << 0);
  Vm |= (fieldFromInstruction(Insn, 5, 1) << 4);
  unsigned imm = fieldFromInstruction(Insn, 16, 6);
  unsigned cmode = fieldFromInstruction(Insn, 8, 4);
  unsigned op = fieldFromInstruction(Insn, 5, 1);

  DecodeStatus S = MCDisassembler::Success;

  // If the top 3 bits of imm are clear, this is a VMOV (immediate)
  if (!(imm & 0x38)) {
    if (cmode == 0xF) {
      if (op == 1) return MCDisassembler::Fail;
      Inst.setOpcode(ARM::VMOVv4f32);
    }
    if (hasFullFP16) {
      if (cmode == 0xE) {
        if (op == 1) {
          Inst.setOpcode(ARM::VMOVv2i64);
        } else {
          Inst.setOpcode(ARM::VMOVv16i8);
        }
      }
      if (cmode == 0xD) {
        if (op == 1) {
          Inst.setOpcode(ARM::VMVNv4i32);
        } else {
          Inst.setOpcode(ARM::VMOVv4i32);
        }
      }
      if (cmode == 0xC) {
        if (op == 1) {
          Inst.setOpcode(ARM::VMVNv4i32);
        } else {
          Inst.setOpcode(ARM::VMOVv4i32);
        }
      }
    }
    return DecodeVMOVModImmInstruction(Inst, Insn, Address, Decoder);
  }

  if (!(imm & 0x20)) return MCDisassembler::Fail;

  if (!Check(S, DecodeQPRRegisterClass(Inst, Vd, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeQPRRegisterClass(Inst, Vm, Address, Decoder)))
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createImm(64 - imm));

  return S;
}

static DecodeStatus
DecodeNEONComplexLane64Instruction(MCInst &Inst, unsigned Insn,
                                   uint64_t Address,
                                   const MCDisassembler *Decoder) {
  unsigned Vd = (fieldFromInstruction(Insn, 12, 4) << 0);
  Vd |= (fieldFromInstruction(Insn, 22, 1) << 4);
  unsigned Vn = (fieldFromInstruction(Insn, 16, 4) << 0);
  Vn |= (fieldFromInstruction(Insn, 7, 1) << 4);
  unsigned Vm = (fieldFromInstruction(Insn, 0, 4) << 0);
  Vm |= (fieldFromInstruction(Insn, 5, 1) << 4);
  unsigned q = (fieldFromInstruction(Insn, 6, 1) << 0);
  unsigned rotate = (fieldFromInstruction(Insn, 20, 2) << 0);

  DecodeStatus S = MCDisassembler::Success;

  auto DestRegDecoder = q ? DecodeQPRRegisterClass : DecodeDPRRegisterClass;

  if (!Check(S, DestRegDecoder(Inst, Vd, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DestRegDecoder(Inst, Vd, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DestRegDecoder(Inst, Vn, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeDPRRegisterClass(Inst, Vm, Address, Decoder)))
    return MCDisassembler::Fail;
  // The lane index does not have any bits in the encoding, because it can only
  // be 0.
  Inst.addOperand(MCOperand::createImm(0));
  Inst.addOperand(MCOperand::createImm(rotate));

  return S;
}

static DecodeStatus DecodeLDR(MCInst &Inst, unsigned Val, uint64_t Address,
                              const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Rn = fieldFromInstruction(Val, 16, 4);
  unsigned Rt = fieldFromInstruction(Val, 12, 4);
  unsigned Rm = fieldFromInstruction(Val, 0, 4);
  Rm |= (fieldFromInstruction(Val, 23, 1) << 4);
  unsigned Cond = fieldFromInstruction(Val, 28, 4);

  if (fieldFromInstruction(Val, 8, 4) != 0 || Rn == Rt)
    S = MCDisassembler::SoftFail;

  if (!Check(S, DecodeGPRnopcRegisterClass(Inst, Rt, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeGPRnopcRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeAddrMode7Operand(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodePostIdxReg(Inst, Rm, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodePredicateOperand(Inst, Cond, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

static DecodeStatus DecoderForMRRC2AndMCRR2(MCInst &Inst, unsigned Val,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned CRm = fieldFromInstruction(Val, 0, 4);
  unsigned opc1 = fieldFromInstruction(Val, 4, 4);
  unsigned cop = fieldFromInstruction(Val, 8, 4);
  unsigned Rt = fieldFromInstruction(Val, 12, 4);
  unsigned Rt2 = fieldFromInstruction(Val, 16, 4);

  if ((cop & ~0x1) == 0xa)
    return MCDisassembler::Fail;

  if (Rt == Rt2)
    S = MCDisassembler::SoftFail;

  // We have to check if the instruction is MRRC2
  // or MCRR2 when constructing the operands for
  // Inst. Reason is because MRRC2 stores to two
  // registers so it's tablegen desc has two
  // outputs whereas MCRR doesn't store to any
  // registers so all of it's operands are listed
  // as inputs, therefore the operand order for
  // MRRC2 needs to be [Rt, Rt2, cop, opc1, CRm]
  // and MCRR2 operand order is [cop, opc1, Rt, Rt2, CRm]

  if (Inst.getOpcode() == ARM::MRRC2) {
    if (!Check(S, DecodeGPRnopcRegisterClass(Inst, Rt, Address, Decoder)))
      return MCDisassembler::Fail;
    if (!Check(S, DecodeGPRnopcRegisterClass(Inst, Rt2, Address, Decoder)))
      return MCDisassembler::Fail;
  }
  Inst.addOperand(MCOperand::createImm(cop));
  Inst.addOperand(MCOperand::createImm(opc1));
  if (Inst.getOpcode() == ARM::MCRR2) {
    if (!Check(S, DecodeGPRnopcRegisterClass(Inst, Rt, Address, Decoder)))
      return MCDisassembler::Fail;
    if (!Check(S, DecodeGPRnopcRegisterClass(Inst, Rt2, Address, Decoder)))
      return MCDisassembler::Fail;
  }
  Inst.addOperand(MCOperand::createImm(CRm));

  return S;
}

static DecodeStatus DecodeForVMRSandVMSR(MCInst &Inst, unsigned Val,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder) {
  const FeatureBitset &featureBits =
      ((const MCDisassembler *)Decoder)->getSubtargetInfo().getFeatureBits();
  DecodeStatus S = MCDisassembler::Success;

  // Add explicit operand for the destination sysreg, for cases where
  // we have to model it for code generation purposes.
  switch (Inst.getOpcode()) {
  case ARM::VMSR_FPSCR_NZCVQC:
    Inst.addOperand(MCOperand::createReg(ARM::FPSCR_NZCV));
    break;
  case ARM::VMSR_P0:
    Inst.addOperand(MCOperand::createReg(ARM::VPR));
    break;
  }

  if (Inst.getOpcode() != ARM::FMSTAT) {
    unsigned Rt = fieldFromInstruction(Val, 12, 4);

    if (featureBits[ARM::ModeThumb] && !featureBits[ARM::HasV8Ops]) {
      if (Rt == 13 || Rt == 15)
        S = MCDisassembler::SoftFail;
      Check(S, DecodeGPRRegisterClass(Inst, Rt, Address, Decoder));
    } else
      Check(S, DecodeGPRnopcRegisterClass(Inst, Rt, Address, Decoder));
  }

  // Add explicit operand for the source sysreg, similarly to above.
  switch (Inst.getOpcode()) {
  case ARM::VMRS_FPSCR_NZCVQC:
    Inst.addOperand(MCOperand::createReg(ARM::FPSCR_NZCV));
    break;
  case ARM::VMRS_P0:
    Inst.addOperand(MCOperand::createReg(ARM::VPR));
    break;
  }

  if (featureBits[ARM::ModeThumb]) {
    Inst.addOperand(MCOperand::createImm(ARMCC::AL));
    Inst.addOperand(MCOperand::createReg(0));
  } else {
    unsigned pred = fieldFromInstruction(Val, 28, 4);
    if (!Check(S, DecodePredicateOperand(Inst, pred, Address, Decoder)))
      return MCDisassembler::Fail;
  }

  return S;
}

template <bool isSigned, bool isNeg, bool zeroPermitted, int size>
static DecodeStatus DecodeBFLabelOperand(MCInst &Inst, unsigned Val,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;
  if (Val == 0 && !zeroPermitted)
    S = MCDisassembler::Fail;

  uint64_t DecVal;
  if (isSigned)
    DecVal = SignExtend32<size + 1>(Val << 1);
  else
    DecVal = (Val << 1);

  if (!tryAddingSymbolicOperand(Address, Address + DecVal + 4, true, 4, Inst,
                                Decoder))
    Inst.addOperand(MCOperand::createImm(isNeg ? -DecVal : DecVal));
  return S;
}

static DecodeStatus DecodeBFAfterTargetOperand(MCInst &Inst, unsigned Val,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {

  uint64_t LocImm = Inst.getOperand(0).getImm();
  Val = LocImm + (2 << Val);
  if (!tryAddingSymbolicOperand(Address, Address + Val + 4, true, 4, Inst,
                                Decoder))
    Inst.addOperand(MCOperand::createImm(Val));
  return MCDisassembler::Success;
}

static DecodeStatus DecodePredNoALOperand(MCInst &Inst, unsigned Val,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder) {
  if (Val >= ARMCC::AL)  // also exclude the non-condition NV
    return MCDisassembler::Fail;
  Inst.addOperand(MCOperand::createImm(Val));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeLOLoop(MCInst &Inst, unsigned Insn, uint64_t Address,
                                 const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  if (Inst.getOpcode() == ARM::MVE_LCTP)
    return S;

  unsigned Imm = fieldFromInstruction(Insn, 11, 1) |
                 fieldFromInstruction(Insn, 1, 10) << 1;
  switch (Inst.getOpcode()) {
  case ARM::t2LEUpdate:
  case ARM::MVE_LETP:
    Inst.addOperand(MCOperand::createReg(ARM::LR));
    Inst.addOperand(MCOperand::createReg(ARM::LR));
    [[fallthrough]];
  case ARM::t2LE:
    if (!Check(S, DecodeBFLabelOperand<false, true, true, 11>(
                   Inst, Imm, Address, Decoder)))
      return MCDisassembler::Fail;
    break;
  case ARM::t2WLS:
  case ARM::MVE_WLSTP_8:
  case ARM::MVE_WLSTP_16:
  case ARM::MVE_WLSTP_32:
  case ARM::MVE_WLSTP_64:
    Inst.addOperand(MCOperand::createReg(ARM::LR));
    if (!Check(S,
               DecoderGPRRegisterClass(Inst, fieldFromInstruction(Insn, 16, 4),
                                       Address, Decoder)) ||
        !Check(S, DecodeBFLabelOperand<false, false, true, 11>(
                   Inst, Imm, Address, Decoder)))
      return MCDisassembler::Fail;
    break;
  case ARM::t2DLS:
  case ARM::MVE_DLSTP_8:
  case ARM::MVE_DLSTP_16:
  case ARM::MVE_DLSTP_32:
  case ARM::MVE_DLSTP_64:
    unsigned Rn = fieldFromInstruction(Insn, 16, 4);
    if (Rn == 0xF) {
      // Enforce all the rest of the instruction bits in LCTP, which
      // won't have been reliably checked based on LCTP's own tablegen
      // record, because we came to this decode by a roundabout route.
      uint32_t CanonicalLCTP = 0xF00FE001, SBZMask = 0x00300FFE;
      if ((Insn & ~SBZMask) != CanonicalLCTP)
        return MCDisassembler::Fail;   // a mandatory bit is wrong: hard fail
      if (Insn != CanonicalLCTP)
        Check(S, MCDisassembler::SoftFail); // an SBZ bit is wrong: soft fail

      Inst.setOpcode(ARM::MVE_LCTP);
    } else {
      Inst.addOperand(MCOperand::createReg(ARM::LR));
      if (!Check(S, DecoderGPRRegisterClass(Inst,
                                            fieldFromInstruction(Insn, 16, 4),
                                            Address, Decoder)))
        return MCDisassembler::Fail;
    }
    break;
  }
  return S;
}

static DecodeStatus DecodeLongShiftOperand(MCInst &Inst, unsigned Val,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  if (Val == 0)
    Val = 32;

  Inst.addOperand(MCOperand::createImm(Val));

  return S;
}

static DecodeStatus DecodetGPROddRegisterClass(MCInst &Inst, unsigned RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  if ((RegNo) + 1 > 11)
    return MCDisassembler::Fail;

  unsigned Register = GPRDecoderTable[(RegNo) + 1];
  Inst.addOperand(MCOperand::createReg(Register));
  return MCDisassembler::Success;
}

static DecodeStatus DecodetGPREvenRegisterClass(MCInst &Inst, unsigned RegNo,
                                                uint64_t Address,
                                                const MCDisassembler *Decoder) {
  if ((RegNo) > 14)
    return MCDisassembler::Fail;

  unsigned Register = GPRDecoderTable[(RegNo)];
  Inst.addOperand(MCOperand::createReg(Register));
  return MCDisassembler::Success;
}

static DecodeStatus
DecodeGPRwithAPSR_NZCVnospRegisterClass(MCInst &Inst, unsigned RegNo,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder) {
  if (RegNo == 15) {
    Inst.addOperand(MCOperand::createReg(ARM::APSR_NZCV));
    return MCDisassembler::Success;
  }

  unsigned Register = GPRDecoderTable[RegNo];
  Inst.addOperand(MCOperand::createReg(Register));

  if (RegNo == 13)
    return MCDisassembler::SoftFail;

  return MCDisassembler::Success;
}

static DecodeStatus DecodeVSCCLRM(MCInst &Inst, unsigned Insn, uint64_t Address,
                                  const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  Inst.addOperand(MCOperand::createImm(ARMCC::AL));
  Inst.addOperand(MCOperand::createReg(0));
  if (Inst.getOpcode() == ARM::VSCCLRMD) {
    unsigned reglist = (fieldFromInstruction(Insn, 1, 7) << 1) |
                       (fieldFromInstruction(Insn, 12, 4) << 8) |
                       (fieldFromInstruction(Insn, 22, 1) << 12);
    if (!Check(S, DecodeDPRRegListOperand(Inst, reglist, Address, Decoder))) {
      return MCDisassembler::Fail;
    }
  } else {
    unsigned reglist = fieldFromInstruction(Insn, 0, 8) |
                       (fieldFromInstruction(Insn, 22, 1) << 8) |
                       (fieldFromInstruction(Insn, 12, 4) << 9);
    if (!Check(S, DecodeSPRRegListOperand(Inst, reglist, Address, Decoder))) {
      return MCDisassembler::Fail;
    }
  }
  Inst.addOperand(MCOperand::createReg(ARM::VPR));

  return S;
}

static DecodeStatus DecodeMQPRRegisterClass(MCInst &Inst, unsigned RegNo,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder) {
  if (RegNo > 7)
    return MCDisassembler::Fail;

  unsigned Register = QPRDecoderTable[RegNo];
  Inst.addOperand(MCOperand::createReg(Register));
  return MCDisassembler::Success;
}

static const uint16_t QQPRDecoderTable[] = {
     ARM::Q0_Q1,  ARM::Q1_Q2,  ARM::Q2_Q3,  ARM::Q3_Q4,
     ARM::Q4_Q5,  ARM::Q5_Q6,  ARM::Q6_Q7
};

static DecodeStatus DecodeMQQPRRegisterClass(MCInst &Inst, unsigned RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  if (RegNo > 6)
    return MCDisassembler::Fail;

  unsigned Register = QQPRDecoderTable[RegNo];
  Inst.addOperand(MCOperand::createReg(Register));
  return MCDisassembler::Success;
}

static const uint16_t QQQQPRDecoderTable[] = {
     ARM::Q0_Q1_Q2_Q3,  ARM::Q1_Q2_Q3_Q4,  ARM::Q2_Q3_Q4_Q5,
     ARM::Q3_Q4_Q5_Q6,  ARM::Q4_Q5_Q6_Q7
};

static DecodeStatus DecodeMQQQQPRRegisterClass(MCInst &Inst, unsigned RegNo,
                                               uint64_t Address,
                                               const MCDisassembler *Decoder) {
  if (RegNo > 4)
    return MCDisassembler::Fail;

  unsigned Register = QQQQPRDecoderTable[RegNo];
  Inst.addOperand(MCOperand::createReg(Register));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeVPTMaskOperand(MCInst &Inst, unsigned Val,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  // Parse VPT mask and encode it in the MCInst as an immediate with the same
  // format as the it_mask.  That is, from the second 'e|t' encode 'e' as 1 and
  // 't' as 0 and finish with a 1.
  unsigned Imm = 0;
  // We always start with a 't'.
  unsigned CurBit = 0;
  for (int i = 3; i >= 0; --i) {
    // If the bit we are looking at is not the same as last one, invert the
    // CurBit, if it is the same leave it as is.
    CurBit ^= (Val >> i) & 1U;

    // Encode the CurBit at the right place in the immediate.
    Imm |= (CurBit << i);

    // If we are done, finish the encoding with a 1.
    if ((Val & ~(~0U << i)) == 0) {
      Imm |= 1U << i;
      break;
    }
  }

  Inst.addOperand(MCOperand::createImm(Imm));

  return S;
}

static DecodeStatus DecodeVpredROperand(MCInst &Inst, unsigned RegNo,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder) {
  // The vpred_r operand type includes an MQPR register field derived
  // from the encoding. But we don't actually want to add an operand
  // to the MCInst at this stage, because AddThumbPredicate will do it
  // later, and will infer the register number from the TIED_TO
  // constraint. So this is a deliberately empty decoder method that
  // will inhibit the auto-generated disassembly code from adding an
  // operand at all.
  return MCDisassembler::Success;
}

[[maybe_unused]] static DecodeStatus
DecodeVpredNOperand(MCInst &Inst, unsigned RegNo, uint64_t Address,
                    const MCDisassembler *Decoder) {
  // Similar to above, we want to ensure that no operands are added for the
  // vpred operands. (This is marked "maybe_unused" for the moment; because
  // DecoderEmitter currently (wrongly) omits operands with no instruction bits,
  // the decoder doesn't actually call it yet. That will be addressed in a
  // future change.)
  return MCDisassembler::Success;
}

static DecodeStatus
DecodeRestrictedIPredicateOperand(MCInst &Inst, unsigned Val, uint64_t Address,
                                  const MCDisassembler *Decoder) {
  Inst.addOperand(MCOperand::createImm((Val & 0x1) == 0 ? ARMCC::EQ : ARMCC::NE));
  return MCDisassembler::Success;
}

static DecodeStatus
DecodeRestrictedSPredicateOperand(MCInst &Inst, unsigned Val, uint64_t Address,
                                  const MCDisassembler *Decoder) {
  unsigned Code;
  switch (Val & 0x3) {
  case 0:
    Code = ARMCC::GE;
    break;
  case 1:
    Code = ARMCC::LT;
    break;
  case 2:
    Code = ARMCC::GT;
    break;
  case 3:
    Code = ARMCC::LE;
    break;
  }
  Inst.addOperand(MCOperand::createImm(Code));
  return MCDisassembler::Success;
}

static DecodeStatus
DecodeRestrictedUPredicateOperand(MCInst &Inst, unsigned Val, uint64_t Address,
                                  const MCDisassembler *Decoder) {
  Inst.addOperand(MCOperand::createImm((Val & 0x1) == 0 ? ARMCC::HS : ARMCC::HI));
  return MCDisassembler::Success;
}

static DecodeStatus
DecodeRestrictedFPPredicateOperand(MCInst &Inst, unsigned Val, uint64_t Address,
                                   const MCDisassembler *Decoder) {
  unsigned Code;
  switch (Val) {
  default:
    return MCDisassembler::Fail;
  case 0:
    Code = ARMCC::EQ;
    break;
  case 1:
    Code = ARMCC::NE;
    break;
  case 4:
    Code = ARMCC::GE;
    break;
  case 5:
    Code = ARMCC::LT;
    break;
  case 6:
    Code = ARMCC::GT;
    break;
  case 7:
    Code = ARMCC::LE;
    break;
  }

  Inst.addOperand(MCOperand::createImm(Code));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeVCVTImmOperand(MCInst &Inst, unsigned Val,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned DecodedVal = 64 - Val;

  switch (Inst.getOpcode()) {
  case ARM::MVE_VCVTf16s16_fix:
  case ARM::MVE_VCVTs16f16_fix:
  case ARM::MVE_VCVTf16u16_fix:
  case ARM::MVE_VCVTu16f16_fix:
    if (DecodedVal > 16)
      return MCDisassembler::Fail;
    break;
  case ARM::MVE_VCVTf32s32_fix:
  case ARM::MVE_VCVTs32f32_fix:
  case ARM::MVE_VCVTf32u32_fix:
  case ARM::MVE_VCVTu32f32_fix:
    if (DecodedVal > 32)
      return MCDisassembler::Fail;
    break;
  }

  Inst.addOperand(MCOperand::createImm(64 - Val));

  return S;
}

static unsigned FixedRegForVSTRVLDR_SYSREG(unsigned Opcode) {
  switch (Opcode) {
  case ARM::VSTR_P0_off:
  case ARM::VSTR_P0_pre:
  case ARM::VSTR_P0_post:
  case ARM::VLDR_P0_off:
  case ARM::VLDR_P0_pre:
  case ARM::VLDR_P0_post:
    return ARM::P0;
  default:
    return 0;
  }
}

template <bool Writeback>
static DecodeStatus DecodeVSTRVLDR_SYSREG(MCInst &Inst, unsigned Val,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder) {
  switch (Inst.getOpcode()) {
  case ARM::VSTR_FPSCR_pre:
  case ARM::VSTR_FPSCR_NZCVQC_pre:
  case ARM::VLDR_FPSCR_pre:
  case ARM::VLDR_FPSCR_NZCVQC_pre:
  case ARM::VSTR_FPSCR_off:
  case ARM::VSTR_FPSCR_NZCVQC_off:
  case ARM::VLDR_FPSCR_off:
  case ARM::VLDR_FPSCR_NZCVQC_off:
  case ARM::VSTR_FPSCR_post:
  case ARM::VSTR_FPSCR_NZCVQC_post:
  case ARM::VLDR_FPSCR_post:
  case ARM::VLDR_FPSCR_NZCVQC_post:
    const FeatureBitset &featureBits =
        ((const MCDisassembler *)Decoder)->getSubtargetInfo().getFeatureBits();

    if (!featureBits[ARM::HasMVEIntegerOps] && !featureBits[ARM::FeatureVFP2])
      return MCDisassembler::Fail;
  }

  DecodeStatus S = MCDisassembler::Success;
  if (unsigned Sysreg = FixedRegForVSTRVLDR_SYSREG(Inst.getOpcode()))
    Inst.addOperand(MCOperand::createReg(Sysreg));
  unsigned Rn = fieldFromInstruction(Val, 16, 4);
  unsigned addr = fieldFromInstruction(Val, 0, 7) |
                  (fieldFromInstruction(Val, 23, 1) << 7) | (Rn << 8);

  if (Writeback) {
    if (!Check(S, DecodeGPRnopcRegisterClass(Inst, Rn, Address, Decoder)))
      return MCDisassembler::Fail;
  }
  if (!Check(S, DecodeT2AddrModeImm7s4(Inst, addr, Address, Decoder)))
    return MCDisassembler::Fail;

  Inst.addOperand(MCOperand::createImm(ARMCC::AL));
  Inst.addOperand(MCOperand::createReg(0));

  return S;
}

static inline DecodeStatus
DecodeMVE_MEM_pre(MCInst &Inst, unsigned Val, uint64_t Address,
                  const MCDisassembler *Decoder, unsigned Rn,
                  OperandDecoder RnDecoder, OperandDecoder AddrDecoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned Qd = fieldFromInstruction(Val, 13, 3);
  unsigned addr = fieldFromInstruction(Val, 0, 7) |
                  (fieldFromInstruction(Val, 23, 1) << 7) | (Rn << 8);

  if (!Check(S, RnDecoder(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeMQPRRegisterClass(Inst, Qd, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, AddrDecoder(Inst, addr, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

template <int shift>
static DecodeStatus DecodeMVE_MEM_1_pre(MCInst &Inst, unsigned Val,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder) {
  return DecodeMVE_MEM_pre(Inst, Val, Address, Decoder,
                           fieldFromInstruction(Val, 16, 3),
                           DecodetGPRRegisterClass,
                           DecodeTAddrModeImm7<shift>);
}

template <int shift>
static DecodeStatus DecodeMVE_MEM_2_pre(MCInst &Inst, unsigned Val,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder) {
  return DecodeMVE_MEM_pre(Inst, Val, Address, Decoder,
                           fieldFromInstruction(Val, 16, 4),
                           DecoderGPRRegisterClass,
                           DecodeT2AddrModeImm7<shift,1>);
}

template <int shift>
static DecodeStatus DecodeMVE_MEM_3_pre(MCInst &Inst, unsigned Val,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder) {
  return DecodeMVE_MEM_pre(Inst, Val, Address, Decoder,
                           fieldFromInstruction(Val, 17, 3),
                           DecodeMQPRRegisterClass,
                           DecodeMveAddrModeQ<shift>);
}

template <unsigned MinLog, unsigned MaxLog>
static DecodeStatus DecodePowerTwoOperand(MCInst &Inst, unsigned Val,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  if (Val < MinLog || Val > MaxLog)
    return MCDisassembler::Fail;

  Inst.addOperand(MCOperand::createImm(1LL << Val));
  return S;
}

template <unsigned start>
static DecodeStatus
DecodeMVEPairVectorIndexOperand(MCInst &Inst, unsigned Val, uint64_t Address,
                                const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  Inst.addOperand(MCOperand::createImm(start + Val));

  return S;
}

static DecodeStatus DecodeMVEVMOVQtoDReg(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;
  unsigned Rt = fieldFromInstruction(Insn, 0, 4);
  unsigned Rt2 = fieldFromInstruction(Insn, 16, 4);
  unsigned Qd = ((fieldFromInstruction(Insn, 22, 1) << 3) |
                 fieldFromInstruction(Insn, 13, 3));
  unsigned index = fieldFromInstruction(Insn, 4, 1);

  if (!Check(S, DecodeGPRRegisterClass(Inst, Rt, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeGPRRegisterClass(Inst, Rt2, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeMQPRRegisterClass(Inst, Qd, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeMVEPairVectorIndexOperand<2>(Inst, index, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeMVEPairVectorIndexOperand<0>(Inst, index, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

static DecodeStatus DecodeMVEVMOVDRegtoQ(MCInst &Inst, unsigned Insn,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;
  unsigned Rt = fieldFromInstruction(Insn, 0, 4);
  unsigned Rt2 = fieldFromInstruction(Insn, 16, 4);
  unsigned Qd = ((fieldFromInstruction(Insn, 22, 1) << 3) |
                 fieldFromInstruction(Insn, 13, 3));
  unsigned index = fieldFromInstruction(Insn, 4, 1);

  if (!Check(S, DecodeMQPRRegisterClass(Inst, Qd, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeMQPRRegisterClass(Inst, Qd, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeGPRRegisterClass(Inst, Rt, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeGPRRegisterClass(Inst, Rt2, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeMVEPairVectorIndexOperand<2>(Inst, index, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeMVEPairVectorIndexOperand<0>(Inst, index, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

static DecodeStatus
DecodeMVEOverlappingLongShift(MCInst &Inst, unsigned Insn, uint64_t Address,
                              const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  unsigned RdaLo = fieldFromInstruction(Insn, 17, 3) << 1;
  unsigned RdaHi = fieldFromInstruction(Insn, 9, 3) << 1;
  unsigned Rm = fieldFromInstruction(Insn, 12, 4);

  if (RdaHi == 14) {
    // This value of RdaHi (really indicating pc, because RdaHi has to
    // be an odd-numbered register, so the low bit will be set by the
    // decode function below) indicates that we must decode as SQRSHR
    // or UQRSHL, which both have a single Rda register field with all
    // four bits.
    unsigned Rda = fieldFromInstruction(Insn, 16, 4);

    switch (Inst.getOpcode()) {
      case ARM::MVE_ASRLr:
      case ARM::MVE_SQRSHRL:
        Inst.setOpcode(ARM::MVE_SQRSHR);
        break;
      case ARM::MVE_LSLLr:
      case ARM::MVE_UQRSHLL:
        Inst.setOpcode(ARM::MVE_UQRSHL);
        break;
      default:
        llvm_unreachable("Unexpected starting opcode!");
    }

    // Rda as output parameter
    if (!Check(S, DecoderGPRRegisterClass(Inst, Rda, Address, Decoder)))
      return MCDisassembler::Fail;

    // Rda again as input parameter
    if (!Check(S, DecoderGPRRegisterClass(Inst, Rda, Address, Decoder)))
      return MCDisassembler::Fail;

    // Rm, the amount to shift by
    if (!Check(S, DecoderGPRRegisterClass(Inst, Rm, Address, Decoder)))
      return MCDisassembler::Fail;

    if (fieldFromInstruction (Insn, 6, 3) != 4)
      return MCDisassembler::SoftFail;

    if (Rda == Rm)
      return MCDisassembler::SoftFail;

    return S;
  }

  // Otherwise, we decode as whichever opcode our caller has already
  // put into Inst. Those all look the same:

  // RdaLo,RdaHi as output parameters
  if (!Check(S, DecodetGPREvenRegisterClass(Inst, RdaLo, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodetGPROddRegisterClass(Inst, RdaHi, Address, Decoder)))
    return MCDisassembler::Fail;

  // RdaLo,RdaHi again as input parameters
  if (!Check(S, DecodetGPREvenRegisterClass(Inst, RdaLo, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodetGPROddRegisterClass(Inst, RdaHi, Address, Decoder)))
    return MCDisassembler::Fail;

  // Rm, the amount to shift by
  if (!Check(S, DecoderGPRRegisterClass(Inst, Rm, Address, Decoder)))
    return MCDisassembler::Fail;

  if (Inst.getOpcode() == ARM::MVE_SQRSHRL ||
      Inst.getOpcode() == ARM::MVE_UQRSHLL) {
    unsigned Saturate = fieldFromInstruction(Insn, 7, 1);
    // Saturate, the bit position for saturation
    Inst.addOperand(MCOperand::createImm(Saturate));
  }

  return S;
}

static DecodeStatus DecodeMVEVCVTt1fp(MCInst &Inst, unsigned Insn,
                                      uint64_t Address,
                                      const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;
  unsigned Qd = ((fieldFromInstruction(Insn, 22, 1) << 3) |
                 fieldFromInstruction(Insn, 13, 3));
  unsigned Qm = ((fieldFromInstruction(Insn, 5, 1) << 3) |
                 fieldFromInstruction(Insn, 1, 3));
  unsigned imm6 = fieldFromInstruction(Insn, 16, 6);

  if (!Check(S, DecodeMQPRRegisterClass(Inst, Qd, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeMQPRRegisterClass(Inst, Qm, Address, Decoder)))
    return MCDisassembler::Fail;
  if (!Check(S, DecodeVCVTImmOperand(Inst, imm6, Address, Decoder)))
    return MCDisassembler::Fail;

  return S;
}

template <bool scalar, OperandDecoder predicate_decoder>
static DecodeStatus DecodeMVEVCMP(MCInst &Inst, unsigned Insn, uint64_t Address,
                                  const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;
  Inst.addOperand(MCOperand::createReg(ARM::VPR));
  unsigned Qn = fieldFromInstruction(Insn, 17, 3);
  if (!Check(S, DecodeMQPRRegisterClass(Inst, Qn, Address, Decoder)))
    return MCDisassembler::Fail;

  unsigned fc;

  if (scalar) {
    fc = fieldFromInstruction(Insn, 12, 1) << 2 |
         fieldFromInstruction(Insn, 7, 1) |
         fieldFromInstruction(Insn, 5, 1) << 1;
    unsigned Rm = fieldFromInstruction(Insn, 0, 4);
    if (!Check(S, DecodeGPRwithZRRegisterClass(Inst, Rm, Address, Decoder)))
      return MCDisassembler::Fail;
  } else {
    fc = fieldFromInstruction(Insn, 12, 1) << 2 |
         fieldFromInstruction(Insn, 7, 1) |
         fieldFromInstruction(Insn, 0, 1) << 1;
    unsigned Qm = fieldFromInstruction(Insn, 5, 1) << 4 |
                  fieldFromInstruction(Insn, 1, 3);
    if (!Check(S, DecodeMQPRRegisterClass(Inst, Qm, Address, Decoder)))
      return MCDisassembler::Fail;
  }

  if (!Check(S, predicate_decoder(Inst, fc, Address, Decoder)))
    return MCDisassembler::Fail;

  Inst.addOperand(MCOperand::createImm(ARMVCC::None));
  Inst.addOperand(MCOperand::createReg(0));
  Inst.addOperand(MCOperand::createImm(0));

  return S;
}

static DecodeStatus DecodeMveVCTP(MCInst &Inst, unsigned Insn, uint64_t Address,
                                  const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;
  Inst.addOperand(MCOperand::createReg(ARM::VPR));
  unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  if (!Check(S, DecoderGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  return S;
}

static DecodeStatus DecodeMVEVPNOT(MCInst &Inst, unsigned Insn,
                                   uint64_t Address,
                                   const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;
  Inst.addOperand(MCOperand::createReg(ARM::VPR));
  Inst.addOperand(MCOperand::createReg(ARM::VPR));
  return S;
}

static DecodeStatus DecodeT2AddSubSPImm(MCInst &Inst, unsigned Insn,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder) {
  const unsigned Rd = fieldFromInstruction(Insn, 8, 4);
  const unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  const unsigned Imm12 = fieldFromInstruction(Insn, 26, 1) << 11 |
                         fieldFromInstruction(Insn, 12, 3) << 8 |
                         fieldFromInstruction(Insn, 0, 8);
  const unsigned TypeT3 = fieldFromInstruction(Insn, 25, 1);
  unsigned sign1 = fieldFromInstruction(Insn, 21, 1);
  unsigned sign2 = fieldFromInstruction(Insn, 23, 1);
  unsigned S = fieldFromInstruction(Insn, 20, 1);
  if (sign1 != sign2)
    return MCDisassembler::Fail;

  // T3 does a zext of imm12, where T2 does a ThumbExpandImm (T2SOImm)
  DecodeStatus DS = MCDisassembler::Success;
  if ((!Check(DS,
              DecodeGPRspRegisterClass(Inst, Rd, Address, Decoder))) || // dst
      (!Check(DS, DecodeGPRspRegisterClass(Inst, Rn, Address, Decoder))))
    return MCDisassembler::Fail;
  if (TypeT3) {
    Inst.setOpcode(sign1 ? ARM::t2SUBspImm12 : ARM::t2ADDspImm12);
    Inst.addOperand(MCOperand::createImm(Imm12)); // zext imm12
  } else {
    Inst.setOpcode(sign1 ? ARM::t2SUBspImm : ARM::t2ADDspImm);
    if (!Check(DS, DecodeT2SOImm(Inst, Imm12, Address, Decoder))) // imm12
      return MCDisassembler::Fail;
    if (!Check(DS, DecodeCCOutOperand(Inst, S, Address, Decoder))) // cc_out
      return MCDisassembler::Fail;
  }

  return DS;
}

static DecodeStatus DecodeLazyLoadStoreMul(MCInst &Inst, unsigned Insn,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder) {
  DecodeStatus S = MCDisassembler::Success;

  const unsigned Rn = fieldFromInstruction(Insn, 16, 4);
  // Adding Rn, holding memory location to save/load to/from, the only argument
  // that is being encoded.
  // '$Rn' in the assembly.
  if (!Check(S, DecodeGPRRegisterClass(Inst, Rn, Address, Decoder)))
    return MCDisassembler::Fail;
  // An optional predicate, '$p' in the assembly.
  DecodePredicateOperand(Inst, ARMCC::AL, Address, Decoder);
  // An immediate that represents a floating point registers list. '$regs' in
  // the assembly.
  Inst.addOperand(MCOperand::createImm(0)); // Arbitrary value, has no effect.

  return S;
}

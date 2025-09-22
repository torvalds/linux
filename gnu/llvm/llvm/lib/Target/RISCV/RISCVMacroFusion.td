//==----- RISCVMacroFusion.td - Macro Fusion Definitions -----*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// ===---------------------------------------------------------------------===//
// The following definitions describe the macro fusion predicators.

// Fuse LUI followed by ADDI or ADDIW:
//   rd = imm[31:0] which decomposes to
//   lui rd, imm[31:12]
//   addi(w) rd, rd, imm[11:0]
def TuneLUIADDIFusion
  : SimpleFusion<"lui-addi-fusion", "HasLUIADDIFusion",
                 "Enable LUI+ADDI macro fusion",
                 CheckOpcode<[LUI]>,
                 CheckOpcode<[ADDI, ADDIW]>>;

// Fuse AUIPC followed by ADDI:
//   auipc rd, imm20
//   addi rd, rd, imm12
def TuneAUIPCADDIFusion
  : SimpleFusion<"auipc-addi-fusion", "HasAUIPCADDIFusion",
                 "Enable AUIPC+ADDI macrofusion",
                 CheckOpcode<[AUIPC]>,
                 CheckOpcode<[ADDI]>>;

// Fuse zero extension of halfword:
//   slli rd, rs1, 48
//   srli rd, rd, 48
def TuneZExtHFusion
  : SimpleFusion<"zexth-fusion", "HasZExtHFusion",
                 "Enable SLLI+SRLI to be fused to zero extension of halfword",
                 CheckAll<[
                   CheckOpcode<[SLLI]>,
                   CheckIsImmOperand<2>,
                   CheckImmOperand<2, 48>
                 ]>,
                 CheckAll<[
                   CheckOpcode<[SRLI]>,
                   CheckIsImmOperand<2>,
                   CheckImmOperand<2, 48>
                 ]>>;

// Fuse zero extension of word:
//   slli rd, rs1, 32
//   srli rd, rd, 32
def TuneZExtWFusion
  : SimpleFusion<"zextw-fusion", "HasZExtWFusion",
                 "Enable SLLI+SRLI to be fused to zero extension of word",
                 CheckAll<[
                   CheckOpcode<[SLLI]>,
                   CheckIsImmOperand<2>,
                   CheckImmOperand<2, 32>
                 ]>,
                 CheckAll<[
                   CheckOpcode<[SRLI]>,
                   CheckIsImmOperand<2>,
                   CheckImmOperand<2, 32>
                 ]>>;

// Fuse shifted zero extension of word:
//   slli rd, rs1, 32
//   srli rd, rd, x
//   where 0 <= x < 32
def TuneShiftedZExtWFusion
  : SimpleFusion<"shifted-zextw-fusion", "HasShiftedZExtWFusion",
                 "Enable SLLI+SRLI to be fused when computing (shifted) word zero extension",
                 CheckAll<[
                   CheckOpcode<[SLLI]>,
                   CheckIsImmOperand<2>,
                   CheckImmOperand<2, 32>
                 ]>,
                 CheckAll<[
                   CheckOpcode<[SRLI]>,
                   CheckIsImmOperand<2>,
                   CheckImmOperandRange<2, 0, 31>
                 ]>>;

// Fuse load with add:
//   add rd, rs1, rs2
//   ld rd, 0(rd)
def TuneLDADDFusion
  : SimpleFusion<"ld-add-fusion", "HasLDADDFusion", "Enable LD+ADD macrofusion",
                 CheckOpcode<[ADD]>,
                 CheckAll<[
                   CheckOpcode<[LD]>,
                   CheckIsImmOperand<2>,
                   CheckImmOperand<2, 0>
                 ]>>;

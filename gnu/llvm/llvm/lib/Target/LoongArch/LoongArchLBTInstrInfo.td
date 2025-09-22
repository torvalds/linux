//===- LoongArchLBTInstrInfo.td - LoongArch LBT instructions -*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the LBT extension instructions.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

let hasSideEffects = 0, mayLoad = 0, mayStore = 0, Predicates = [HasExtLBT] in {

def MOVGR2SCR : FmtGR2SCR<0x00000800>;
def MOVSCR2GR : FmtSCR2GR<0x00000c00>;

def JISCR0 : FmtJISCR<0x48000200>;
def JISCR1 : FmtJISCR<0x48000300>;

def ADDU12I_W : ALU_2RI5<0x00290000, simm5>;

def ADC_B : ALU_3R<0x00300000>;
def ADC_H : ALU_3R<0x00308000>;
def ADC_W : ALU_3R<0x00310000>;

def SBC_B : ALU_3R<0x00320000>;
def SBC_H : ALU_3R<0x00328000>;
def SBC_W : ALU_3R<0x00330000>;

def ROTR_B : ALU_3R<0x001a0000>;
def ROTR_H : ALU_3R<0x001a8000>;

def ROTRI_B : Fmt2RI3<0x004c2000>;
def ROTRI_H : Fmt2RI4<0x004c4000>;

def RCR_B : ALU_3R<0x00340000>;
def RCR_H : ALU_3R<0x00348000>;
def RCR_W : ALU_3R<0x00350000>;

def RCRI_B : Fmt2RI3<0x00502000>;
def RCRI_H : Fmt2RI4<0x00504000>;
def RCRI_W : ALU_2RI5<0x00508000, uimm5>;

def FCVT_UD_D : FP_CONV<0x0114e400>;
def FCVT_LD_D : FP_CONV<0x0114e000>;
def FCVT_D_LD : FP_ALU_3R<0x01150000>;

let mayLoad = 1 in {
def LDL_W : LOAD_2RI12<0x2e000000>;
def LDR_W : LOAD_2RI12<0x2e400000>;
} // mayLoad = 1

let mayStore = 1 in {
def STL_W : STORE_2RI12<0x2f000000>;
def STR_W : STORE_2RI12<0x2f400000>;
} // mayStore = 1

def X86ADC_B : NoDstFmt2R<0x003f000c>;
def X86ADC_H : NoDstFmt2R<0x003f000d>;
def X86ADC_W : NoDstFmt2R<0x003f000e>;
def X86ADD_B : NoDstFmt2R<0x003f0004>;
def X86ADD_H : NoDstFmt2R<0x003f0005>;
def X86ADD_W : NoDstFmt2R<0x003f0006>;

def X86INC_B : NoDstFmt1R<0x00008000>;
def X86INC_H : NoDstFmt1R<0x00008001>;
def X86INC_W : NoDstFmt1R<0x00008002>;

def X86SBC_B : NoDstFmt2R<0x003f0010>;
def X86SBC_H : NoDstFmt2R<0x003f0011>;
def X86SBC_W : NoDstFmt2R<0x003f0012>;
def X86SUB_B : NoDstFmt2R<0x003f0008>;
def X86SUB_H : NoDstFmt2R<0x003f0009>;
def X86SUB_W : NoDstFmt2R<0x003f000a>;

def X86DEC_B : NoDstFmt1R<0x00008004>;
def X86DEC_H : NoDstFmt1R<0x00008005>;
def X86DEC_W : NoDstFmt1R<0x00008006>;

def X86AND_B : NoDstFmt2R<0x003f8010>;
def X86AND_H : NoDstFmt2R<0x003f8011>;
def X86AND_W : NoDstFmt2R<0x003f8012>;

def X86OR_B : NoDstFmt2R<0x003f8014>;
def X86OR_H : NoDstFmt2R<0x003f8015>;
def X86OR_W : NoDstFmt2R<0x003f8016>;

def X86XOR_B : NoDstFmt2R<0x003f8018>;
def X86XOR_H : NoDstFmt2R<0x003f8019>;
def X86XOR_W : NoDstFmt2R<0x003f801a>;

def X86MUL_B : NoDstFmt2R<0x003e8000>;
def X86MUL_H : NoDstFmt2R<0x003e8001>;
def X86MUL_W : NoDstFmt2R<0x003e8002>;
def X86MUL_BU : NoDstFmt2R<0x003e8004>;
def X86MUL_HU : NoDstFmt2R<0x003e8005>;

def X86RCL_B : NoDstFmt2R<0x003f800c>;
def X86RCL_H : NoDstFmt2R<0x003f800d>;
def X86RCL_W : NoDstFmt2R<0x003f800e>;
def X86RCLI_B : NoDstFmt1RI3<0x00542018>;
def X86RCLI_H : NoDstFmt1RI4<0x00544019>;
def X86RCLI_W : NoDstFmt1RI5<0x0054801a>;

def X86RCR_B : NoDstFmt2R<0x003f8008>;
def X86RCR_H : NoDstFmt2R<0x003f8009>;
def X86RCR_W : NoDstFmt2R<0x003f800a>;
def X86RCRI_B : NoDstFmt1RI3<0x00542010>;
def X86RCRI_H : NoDstFmt1RI4<0x00544011>;
def X86RCRI_W : NoDstFmt1RI5<0x00548012>;

def X86ROTL_B : NoDstFmt2R<0x003f8004>;
def X86ROTL_H : NoDstFmt2R<0x003f8005>;
def X86ROTL_W : NoDstFmt2R<0x003f8006>;
def X86ROTLI_B : NoDstFmt1RI3<0x00542014>;
def X86ROTLI_H : NoDstFmt1RI4<0x00544015>;
def X86ROTLI_W : NoDstFmt1RI5<0x00548016>;

def X86ROTR_B : NoDstFmt2R<0x003f8000>;
def X86ROTR_H : NoDstFmt2R<0x003f8001>;
def X86ROTR_W : NoDstFmt2R<0x003f8003>;
def X86ROTRI_B : NoDstFmt1RI3<0x0054200c>;
def X86ROTRI_H : NoDstFmt1RI4<0x0054400d>;
def X86ROTRI_W : NoDstFmt1RI5<0x0054800e>;

def X86SLL_B : NoDstFmt2R<0x003f0014>;
def X86SLL_H : NoDstFmt2R<0x003f0015>;
def X86SLL_W : NoDstFmt2R<0x003f0016>;
def X86SLLI_B : NoDstFmt1RI3<0x00542000>;
def X86SLLI_H : NoDstFmt1RI4<0x00544001>;
def X86SLLI_W : NoDstFmt1RI5<0x00548002>;

def X86SRL_B : NoDstFmt2R<0x003f0018>;
def X86SRL_H : NoDstFmt2R<0x003f0019>;
def X86SRL_W : NoDstFmt2R<0x003f001a>;
def X86SRLI_B : NoDstFmt1RI3<0x00542004>;
def X86SRLI_H : NoDstFmt1RI4<0x00544005>;
def X86SRLI_W : NoDstFmt1RI5<0x00548006>;

def X86SRA_B : NoDstFmt2R<0x003f001c>;
def X86SRA_H : NoDstFmt2R<0x003f001d>;
def X86SRA_W : NoDstFmt2R<0x003f001e>;
def X86SRAI_B : NoDstFmt1RI3<0x00542008>;
def X86SRAI_H : NoDstFmt1RI4<0x00544009>;
def X86SRAI_W : NoDstFmt1RI5<0x0054800a>;

def SETX86J : Fmt1RI4<0x00368000>;
def SETX86LOOPE : ALU_2R<0x00007800>;
def SETX86LOOPNE : ALU_2R<0x00007c00>;
def X86MFFLAG : Fmt1RI8<0x005c0000>;
def X86MTFLAG : Fmt1RI8<0x005c0020>;
def X86MFTOP : FmtMFTOP<0x00007400>;
def X86MTTOP : FmtMTTOP<0x00007000>;

def X86INCTOP : FmtI32<0x00008009>;
def X86DECTOP : FmtI32<0x00008029>;
def X86SETTM : FmtI32<0x00008008>;
def X86CLRTM : FmtI32<0x00008028>;
def X86SETTAG : Fmt1RI5I8<0x00580000>;

def ARMADD_W : NoDstFmt2RI4<0x00370010>;
def ARMSUB_W : NoDstFmt2RI4<0x00378010>;
def ARMADC_W : NoDstFmt2RI4<0x00380010>;
def ARMSBC_W : NoDstFmt2RI4<0x00388010>;
def ARMAND_W : NoDstFmt2RI4<0x00390010>;
def ARMOR_W : NoDstFmt2RI4<0x00398010>;
def ARMXOR_W : NoDstFmt2RI4<0x003a0010>;
def ARMNOT_W : NoDstFmt1RI4<0x003fc01c>;
def ARMSLL_W : NoDstFmt2RI4<0x003a8010>;
def ARMSRL_W : NoDstFmt2RI4<0x003b0010>;
def ARMSRA_W : NoDstFmt2RI4<0x003b8010>;
def ARMROTR_W : NoDstFmt2RI4<0x003c0010>;
def ARMSLLI_W : NoDstFmt1RI5I4<0x003c8010>;
def ARMSRLI_W : NoDstFmt1RI5I4<0x003d0010>;
def ARMSRAI_W : NoDstFmt1RI5I4<0x003d8010>;
def ARMROTRI_W : NoDstFmt1RI5I4<0x003e0010>;
def ARMRRX_W : NoDstFmt1RI4<0x003fc01f>;
def ARMMOVE : Fmt2RI4<0x00364000>;
def ARMMOV_W : NoDstFmt1RI4<0x003fc01d>;

def ARMMFFLAG : Fmt1RI8<0x005c0040>;
def ARMMTFLAG : Fmt1RI8<0x005c0060>;
def SETARMJ : Fmt1RI4<0x0036c000>;

let Predicates = [IsLA64] in {
def ADDU12I_D : ALU_2RI5<0x00298000, simm5>;
def ADC_D : ALU_3R<0x00318000>;
def SBC_D : ALU_3R<0x00338000>;
def RCR_D : ALU_3R<0x00358000>;
def RCRI_D : ALU_2RI6<0x00510000, uimm6>;

// mayLoad = 1
let mayLoad = 1 in {
def LDL_D : LOAD_2RI12<0x2e800000>;
def LDR_D : LOAD_2RI12<0x2ec00000>;
} // mayLoad = 1

let mayStore = 1 in {
def STL_D : STORE_2RI12<0x2f800000>;
def STR_D : STORE_2RI12<0x2fc00000>;
} // mayStore = 1

def X86ADC_D : NoDstFmt2R<0x003f000f>;
def X86ADD_D : NoDstFmt2R<0x003f0007>;
def X86ADD_WU : NoDstFmt2R<0x003f0000>;
def X86ADD_DU : NoDstFmt2R<0x003f0001>;
def X86INC_D : NoDstFmt1R<0x00008003>;
def X86SBC_D : NoDstFmt2R<0x003f0013>;
def X86SUB_WU : NoDstFmt2R<0x003f0002>;
def X86SUB_D : NoDstFmt2R<0x003f000b>;
def X86SUB_DU : NoDstFmt2R<0x003f0003>;
def X86DEC_D : NoDstFmt1R<0x00008007>;
def X86AND_D : NoDstFmt2R<0x003f8013>;
def X86OR_D : NoDstFmt2R<0x003f8017>;
def X86XOR_D : NoDstFmt2R<0x003f801b>;
def X86MUL_D : NoDstFmt2R<0x003e8003>;
def X86MUL_WU : NoDstFmt2R<0x003e8006>;
def X86MUL_DU : NoDstFmt2R<0x003e8007>;
def X86RCL_D : NoDstFmt2R<0x003f800f>;
def X86RCLI_D : NoDstFmt1RI6<0x0055001b>;
def X86RCR_D : NoDstFmt2R<0x003f800b>;
def X86RCRI_D : NoDstFmt1RI6<0x00550013>;
def X86ROTL_D : NoDstFmt2R<0x003f8007>;
def X86ROTLI_D : NoDstFmt1RI6<0x00550017>;
def X86ROTR_D : NoDstFmt2R<0x003f8002>;
def X86ROTRI_D : NoDstFmt1RI6<0x0055000f>;
def X86SLL_D : NoDstFmt2R<0x003f0017>;
def X86SLLI_D : NoDstFmt1RI6<0x00550003>;
def X86SRL_D : NoDstFmt2R<0x003f001b>;
def X86SRLI_D : NoDstFmt1RI6<0x00550007>;
def X86SRA_D : NoDstFmt2R<0x003f001f>;
def X86SRAI_D : NoDstFmt1RI6<0x0055000b>;
def ARMMOV_D : NoDstFmt1RI4<0x003fc01e>;

} // Predicates = [IsLA64]
} // hasSideEffects = 0, mayLoad = 0, mayStore = 0, Predicates = [HasExtLBT]

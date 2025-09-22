//=- MicroMips32r6InstrFormats.td - Mips32r6 Instruction Formats -*- tablegen -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes microMIPS32r6 instruction formats.
//
//===----------------------------------------------------------------------===//

class MMR6Arch<string opstr> {
  string Arch = "micromipsr6";
  string BaseOpcode = opstr;
  string DecoderNamespace = "MicroMipsR6";
}

//===----------------------------------------------------------------------===//
//
// Disambiguators
//
//===----------------------------------------------------------------------===//
//
// Some encodings are ambiguous except by comparing field values.

class MMDecodeDisambiguatedBy<string Name> : DecodeDisambiguates<Name> {
  string DecoderNamespace = "MicroMipsR6_Ambiguous";
}

//===----------------------------------------------------------------------===//
//
// Encoding Formats
//
//===----------------------------------------------------------------------===//

class BC16_FM_MM16R6 {
  bits<10> offset;

  bits<16> Inst;

  let Inst{15-10} = 0x33;
  let Inst{9-0}   = offset;
}

class BEQZC_BNEZC_FM_MM16R6<bits<6> op> {
  bits<3> rs;
  bits<7> offset;

  bits<16> Inst;

  let Inst{15-10} = op;
  let Inst{9-7}   = rs;
  let Inst{6-0}   = offset;
}

class POOL16C_JALRC_FM_MM16R6<bits<5> op> {
  bits<5> rs;

  bits<16> Inst;

  let Inst{15-10} = 0x11;
  let Inst{9-5}   = rs;
  let Inst{4-0}   = op;
}

class POP35_BOVC_FM_MMR6<string instr_asm> : MipsR6Inst, MMR6Arch<instr_asm> {
  bits<5> rt;
  bits<5> rs;
  bits<16> offset;

  bits<32> Inst;

  let Inst{31-26} = 0b011101;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-0} = offset;
}

class POP37_BNVC_FM_MMR6<string instr_asm> : MipsR6Inst, MMR6Arch<instr_asm> {
  bits<5> rt;
  bits<5> rs;
  bits<16> offset;

  bits<32> Inst;

  let Inst{31-26} = 0b011111;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-0} = offset;
}

class POOL16C_JRCADDIUSP_FM_MM16R6<bits<5> op> {
  bits<5> imm;

  bits<16> Inst;

  let Inst{15-10} = 0x11;
  let Inst{9-5}   = imm;
  let Inst{4-0}   = op;
}

class POOL16C_LWM_SWM_FM_MM16R6<bits<4> funct> {
  bits<2> rt;
  bits<4> addr;

  bits<16> Inst;

  let Inst{15-10} = 0x11;
  let Inst{9-8}   = rt;
  let Inst{7-4}   = addr;
  let Inst{3-0}   = funct;
}

class POOL32A_BITSWAP_FM_MMR6<bits<6> funct> : MipsR6Inst {
  bits<5> rd;
  bits<5> rt;

  bits<32> Inst;

  let Inst{31-26} = 0b000000;
  let Inst{25-21} = rt;
  let Inst{20-16} = rd;
  let Inst{15-12} = 0b0000;
  let Inst{11-6} = funct;
  let Inst{5-0} = 0b111100;
}

class CACHE_PREF_FM_MMR6<bits<6> opgroup, bits<4> funct> : MipsR6Inst {
  bits<21> addr;
  bits<5> hint;

  bits<32> Inst;

  let Inst{31-26} = opgroup;
  let Inst{25-21} = hint;
  let Inst{20-16} = addr{20-16};
  let Inst{15-12} = funct;
  let Inst{11-0}  = addr{11-0};
}

class ARITH_FM_MMR6<string instr_asm, bits<10> funct> : MMR6Arch<instr_asm> {
  bits<5> rd;
  bits<5> rt;
  bits<5> rs;

  bits<32> Inst;

  let Inst{31-26} = 0;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-11} = rd;
  let Inst{10}    = 0;
  let Inst{9-0}   = funct;
}

class ADDI_FM_MMR6<string instr_asm, bits<6> op> : MMR6Arch<instr_asm> {
  bits<5>  rt;
  bits<5>  rs;
  bits<16> imm16;

  bits<32> Inst;

  let Inst{31-26} = op;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-0}  = imm16;
}

class LB32_FM_MMR6 : MipsR6Inst {
  bits<21> addr;
  bits<5> rt;
  bits<5> base = addr{20-16};
  bits<16> offset = addr{15-0};

  bits<32> Inst;

  let Inst{31-26} = 0b000111;
  let Inst{25-21} = rt;
  let Inst{20-16} = base;
  let Inst{15-0}  = offset;
}

class LBU32_FM_MMR6 : MipsR6Inst {
  bits<21> addr;
  bits<5> rt;
  bits<5> base = addr{20-16};
  bits<16> offset = addr{15-0};

  bits<32> Inst;

  let Inst{31-26} = 0b000101;
  let Inst{25-21} = rt;
  let Inst{20-16} = base;
  let Inst{15-0}  = offset;
}

class PCREL19_FM_MMR6<bits<2> funct> : MipsR6Inst {
  bits<5> rt;
  bits<19> imm;

  bits<32> Inst;

  let Inst{31-26} = 0b011110;
  let Inst{25-21} = rt;
  let Inst{20-19} = funct;
  let Inst{18-0}  = imm;
}

class PCREL16_FM_MMR6<bits<5> funct> : MipsR6Inst {
  bits<5> rt;
  bits<16> imm;

  bits<32> Inst;

  let Inst{31-26} = 0b011110;
  let Inst{25-21} = rt;
  let Inst{20-16} = funct;
  let Inst{15-0}  = imm;
}

class POOL32A_FM_MMR6<bits<10> funct> : MipsR6Inst {
  bits<5> rd;
  bits<5> rs;
  bits<5> rt;

  bits<32> Inst;

  let Inst{31-26} = 0b000000;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-11} = rd;
  let Inst{10}    = 0;
  let Inst{9-0}   = funct;
}

class POOL32A_PAUSE_FM_MMR6<string instr_asm, bits<5> op>
    : MMR6Arch<instr_asm> {
  bits<32> Inst;

  let Inst{31-26} = 0;
  let Inst{25-21} = 0;
  let Inst{20-16} = 0;
  let Inst{15-11} = op;
  let Inst{10-6} = 0;
  let Inst{5-0} = 0;
}

class POOL32A_RDPGPR_FM_MMR6<bits<10> funct> {
  bits<5> rt;
  bits<5> rd;
  bits<32> Inst;

  let Inst{31-26} = 0;
  let Inst{25-21} = rt;
  let Inst{20-16} = rd;
  let Inst{15-6} = funct;
  let Inst{5-0} = 0b111100;
}

class POOL32A_RDHWR_FM_MMR6 {
  bits<5> rt;
  bits<5> rs;
  bits<3> sel;
  bits<32> Inst;

  let Inst{31-26} = 0;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-14} = 0;
  let Inst{13-11} = sel;
  let Inst{10} = 0;
  let Inst{9-0} = 0b0111000000;
}

class POOL32A_SYNC_FM_MMR6 {
  bits<5> stype;

  bits<32> Inst;

  let Inst{31-26} = 0;
  let Inst{25-21} = 0;
  let Inst{20-16} = stype;
  let Inst{15-6}  = 0b0110101101;
  let Inst{5-0}   = 0b111100;
}

class POOL32I_SYNCI_FM_MMR6 {
  bits<21> addr;
  bits<5> base = addr{20-16};
  bits<16> immediate = addr{15-0};

  bits<32> Inst;

  let Inst{31-26} = 0b010000;
  let Inst{25-21} = 0b01100;
  let Inst{20-16} = base;
  let Inst{15-0}  = immediate;
}

class POOL32A_2R_FM_MMR6<bits<10> funct> : MipsR6Inst {
  bits<5> rs;
  bits<5> rt;

  bits<32> Inst;

  let Inst{31-26} = 0b000000;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-6}  = funct;
  let Inst{5-0}   = 0b111100;
}

class SPECIAL_2R_FM_MMR6<bits<6> funct> : MipsR6Inst {
  bits<5> rs;
  bits<5> rt;

  bits<32> Inst;

  let Inst{31-26} = 0b000000;
  let Inst{25-21} = rs;
  let Inst{20-16} = 0b00000;
  let Inst{15-11} = rt;
  let Inst{10-6}  = 0b00001;
  let Inst{5-0}   = funct;
}

class POOL32A_ALIGN_FM_MMR6<bits<6> funct> : MipsR6Inst {
  bits<5> rd;
  bits<5> rs;
  bits<5> rt;
  bits<2> bp;

  bits<32> Inst;

  let Inst{31-26} = 0b000000;
  let Inst{25-21} = rs;
  let Inst{20-16} = rt;
  let Inst{15-11} = rd;
  let Inst{10-9}  = bp;
  let Inst{8-6}   = 0b000;
  let Inst{5-0}   = funct;
}

class AUI_FM_MMR6 : MipsR6Inst {
  bits<5> rs;
  bits<5> rt;
  bits<16> imm;

  bits<32> Inst;

  let Inst{31-26} = 0b000100;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-0} = imm;
}

class POOL32A_LSA_FM<bits<6> funct> : MipsR6Inst {
  bits<5> rd;
  bits<5> rs;
  bits<5> rt;
  bits<2> imm2;

  bits<32> Inst;

  let Inst{31-26} = 0b000000;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-11} = rd;
  let Inst{10-9}  = imm2;
  let Inst{8-6}   = 0b000;
  let Inst{5-0}   = funct;
}

class SB32_SH32_STORE_FM_MMR6<bits<6> op> {
  bits<5> rt;
  bits<21> addr;
  bits<5> base = addr{20-16};
  bits<16> offset = addr{15-0};

  bits<32> Inst;

  let Inst{31-26} = op;
  let Inst{25-21} = rt;
  let Inst{20-16} = base;
  let Inst{15-0}  = offset;
}

class LOAD_WORD_FM_MMR6 {
  bits<5> rt;
  bits<21> addr;
  bits<5> base = addr{20-16};
  bits<16> offset = addr{15-0};

  bits<32> Inst;

  let Inst{31-26} = 0b111111;
  let Inst{25-21} = rt;
  let Inst{20-16} = base;
  let Inst{15-0}  = offset;
}

class LOAD_UPPER_IMM_FM_MMR6 {
  bits<5> rt;
  bits<16> imm16;

  bits<32> Inst;

  let Inst{31-26} = 0b000100;
  let Inst{25-21} = rt;
  let Inst{20-16} = 0;
  let Inst{15-0}  = imm16;
}

class CMP_BRANCH_1R_RT_OFF16_FM_MMR6<string instr_asm, bits<6> funct>
    : MMR6Arch<instr_asm>, MipsR6Inst {
  bits<5> rt;
  bits<16> offset;

  bits<32> Inst;

  let Inst{31-26} = funct;
  let Inst{25-21} = rt;
  let Inst{20-16} = 0b00000;
  let Inst{15-0}  = offset;
}

class CMP_BRANCH_1R_BOTH_OFF16_FM_MMR6<string instr_asm, bits<6> funct>
    : MMR6Arch<instr_asm>, MipsR6Inst {
  bits<5> rt;
  bits<16> offset;

  bits<32> Inst;

  let Inst{31-26} = funct;
  let Inst{25-21} = rt;
  let Inst{20-16} = rt;
  let Inst{15-0}  = offset;
}

class POOL32A_JALRC_FM_MMR6<string instr_asm, bits<10> funct>
    : MipsR6Inst, MMR6Arch<instr_asm> {
  bits<5> rt;
  bits<5> rs;

  bits<32> Inst;

  let Inst{31-26} = 0;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-6} = funct;
  let Inst{5-0} = 0b111100;
}

class POOL32A_EXT_INS_FM_MMR6<string instr_asm, bits<6> funct>
    : MMR6Arch<instr_asm>, MipsR6Inst {
  bits<5> rt;
  bits<5> rs;
  bits<5> size;
  bits<5> pos;

  bits<32> Inst;

  let Inst{31-26} = 0;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-11} = size;
  let Inst{10-6}  = pos;
  let Inst{5-0}   = funct;
}

class POOL32A_ERET_FM_MMR6<string instr_asm, bits<10> funct>
    : MMR6Arch<instr_asm> {
  bits<32> Inst;

  let Inst{31-26} = 0x00;
  let Inst{25-16} = 0x00;
  let Inst{15-6}  = funct;
  let Inst{5-0}   = 0x3c;
}

class ERETNC_FM_MMR6<string instr_asm> : MMR6Arch<instr_asm> {
  bits<32> Inst;

  let Inst{31-26} = 0x00;
  let Inst{25-17} = 0x00;
  let Inst{16-16} = 0x01;
  let Inst{15-6}  = 0x3cd;
  let Inst{5-0}   = 0x3c;
}

class BREAK_MMR6_ENC<string instr_asm> : MMR6Arch<instr_asm> {
  bits<10> code_1;
  bits<10> code_2;
  bits<32> Inst;
  let Inst{31-26} = 0x0;
  let Inst{25-16} = code_1;
  let Inst{15-6}  = code_2;
  let Inst{5-0}   = 0x07;
}

class BARRIER_MMR6_ENC<string instr_asm, bits<5> op> : MMR6Arch<instr_asm> {
  bits<32> Inst;

  let Inst{31-26} = 0x0;
  let Inst{25-21} = 0x0;
  let Inst{20-16} = 0x0;
  let Inst{15-11} = op;
  let Inst{10-6}  = 0x0;
  let Inst{5-0}   = 0x0;
}

class POOL32A_EIDI_MMR6_ENC<string instr_asm, bits<10> funct>
    : MMR6Arch<instr_asm> {
  bits<32> Inst;
  bits<5> rt; // Actually rs but we're sharing code with the standard encodings
              // which call it rt

  let Inst{31-26} = 0x00;
  let Inst{25-21} = 0x00;
  let Inst{20-16} = rt;
  let Inst{15-6}  = funct;
  let Inst{5-0}   = 0x3c;
}

class SHIFT_MMR6_ENC<string instr_asm, bits<10> funct, bit rotate>
    : MMR6Arch<instr_asm> {
  bits<5> rd;
  bits<5> rt;
  bits<5> shamt;

  bits<32> Inst;

  let Inst{31-26} = 0;
  let Inst{25-21} = rd;
  let Inst{20-16} = rt;
  let Inst{15-11} = shamt;
  let Inst{10}    = rotate;
  let Inst{9-0}   = funct;
}

class SW32_FM_MMR6<string instr_asm, bits<6> op> : MMR6Arch<instr_asm> {
  bits<5> rt;
  bits<21> addr;

  bits<32> Inst;

  let Inst{31-26} = op;
  let Inst{25-21} = rt;
  let Inst{20-16} = addr{20-16};
  let Inst{15-0}  = addr{15-0};
}

class POOL32F_ARITH_FM_MMR6<string instr_asm, bits<2> fmt, bits<8> funct>
    : MMR6Arch<instr_asm>, MipsR6Inst {
  bits<5> ft;
  bits<5> fs;
  bits<5> fd;

  bits<32> Inst;

  let Inst{31-26} = 0b010101;
  let Inst{25-21} = ft;
  let Inst{20-16} = fs;
  let Inst{15-11} = fd;
  let Inst{10}    = 0;
  let Inst{9-8}   = fmt;
  let Inst{7-0}   = funct;
}

class POOL32F_ARITHF_FM_MMR6<string instr_asm, bits<2> fmt, bits<9> funct>
    : MMR6Arch<instr_asm>, MipsR6Inst {
  bits<5> ft;
  bits<5> fs;
  bits<5> fd;

  bits<32> Inst;

  let Inst{31-26} = 0b010101;
  let Inst{25-21} = ft;
  let Inst{20-16} = fs;
  let Inst{15-11} = fd;
  let Inst{10-9}  = fmt;
  let Inst{8-0}   = funct;
}

class POOL32F_MOV_NEG_FM_MMR6<string instr_asm, bits<2> fmt, bits<7> funct>
    : MMR6Arch<instr_asm>, MipsR6Inst {
  bits<5> ft;
  bits<5> fs;

  bits<32> Inst;

  let Inst{31-26} = 0b010101;
  let Inst{25-21} = ft;
  let Inst{20-16} = fs;
  let Inst{15}    = 0;
  let Inst{14-13} = fmt;
  let Inst{12-6}  = funct;
  let Inst{5-0}   = 0b111011;
}

class POOL32F_MINMAX_FM<string instr_asm, bits<2> fmt, bits<9> funct>
    : MMR6Arch<instr_asm>, MipsR6Inst {
  bits<5> ft;
  bits<5> fs;
  bits<5> fd;

  bits<32> Inst;

  let Inst{31-26} = 0b010101;
  let Inst{25-21} = ft;
  let Inst{20-16} = fs;
  let Inst{15-11} = fd;
  let Inst{10-9} = fmt;
  let Inst{8-0} = funct;
}

class POOL32F_CMP_FM<string instr_asm, bits<6> format, FIELD_CMP_COND Cond>
    : MMR6Arch<instr_asm>, MipsR6Inst {
  bits<5> ft;
  bits<5> fs;
  bits<5> fd;

  bits<32> Inst;

  let Inst{31-26} = 0b010101;
  let Inst{25-21} = ft;
  let Inst{20-16} = fs;
  let Inst{15-11} = fd;
  let Inst{10-6} = Cond.Value;
  let Inst{5-0} = format;
}

class POOL32F_CVT_LW_FM<string instr_asm, bit fmt, bits<8> funct>
    : MMR6Arch<instr_asm>, MipsR6Inst {
  bits<5> ft;
  bits<5> fs;

  bits<32> Inst;
  let Inst{31-26} = 0b010101;
  let Inst{25-21} = ft;
  let Inst{20-16} = fs;
  let Inst{15} = 0;
  let Inst{14} = fmt;
  let Inst{13-6} = funct;
  let Inst{5-0} = 0b111011;
}

class POOL32F_CVT_DS_FM<string instr_asm, bits<2> fmt, bits<7> funct>
    : MMR6Arch<instr_asm>, MipsR6Inst {
  bits<5> ft;
  bits<5> fs;

  bits<32> Inst;
  let Inst{31-26} = 0b010101;
  let Inst{25-21} = ft;
  let Inst{20-16} = fs;
  let Inst{15} = 0;
  let Inst{14-13} = fmt;
  let Inst{12-6} = funct;
  let Inst{5-0} = 0b111011;
}

class POOL32F_ABS_FM_MMR6<string instr_asm, bits<2> fmt, bits<7> funct>
    : MMR6Arch<instr_asm>, MipsR6Inst {
  bits<5> ft;
  bits<5> fs;

  bits<32> Inst;

  let Inst{31-26} = 0b010101;
  let Inst{25-21} = ft;
  let Inst{20-16} = fs;
  let Inst{15}    = 0;
  let Inst{14-13} = fmt;
  let Inst{12-6}  = funct;
  let Inst{5-0}   = 0b111011;
}

class POOL32F_MATH_FM_MMR6<string instr_asm, bits<1> fmt, bits<8> funct>
    : MMR6Arch<instr_asm>, MipsR6Inst {
  bits<5> ft;
  bits<5> fs;

  bits<32> Inst;

  let Inst{31-26} = 0b010101;
  let Inst{25-21} = ft;
  let Inst{20-16} = fs;
  let Inst{15}    = 0;
  let Inst{14}    = fmt;
  let Inst{13-6}  = funct;
  let Inst{5-0}   = 0b111011;
}

class POOL16A_ADDU16_FM_MMR6 {
  bits<3> rs;
  bits<3> rt;
  bits<3> rd;

  bits<16> Inst;

  let Inst{15-10} = 0b000001;
  let Inst{9-7}   = rs;
  let Inst{6-4}   = rt;
  let Inst{3-1}   = rd;
  let Inst{0}     = 0;
}

class POOL16C_AND16_FM_MMR6 {
  bits<3> rt;
  bits<3> rs;

  bits<16> Inst;

  let Inst{15-10} = 0b010001;
  let Inst{9-7}   = rt;
  let Inst{6-4}   = rs;
  let Inst{3-0}   = 0b0001;
}

class POOL16C_NOT16_FM_MMR6 {
  bits<3> rt;
  bits<3> rs;

  bits<16> Inst;

  let Inst{15-10} = 0x11;
  let Inst{9-7}   = rt;
  let Inst{6-4}   = rs;
  let Inst{3-0}   = 0b0000;
}

class POOL16C_MOVEP16_FM_MMR6 {
  bits<3> rt;
  bits<3> rs;

  bits<16> Inst;

  let Inst{15-10} = 0b010001;
  // bits 7-9 are populated by MipsMCCodeEmitter::encodeInstruction, with a
  // special encoding of both rd1 and rd2.
  let Inst{9-7}   = ?;
  let Inst{6-4}   = rt;
  let Inst{3}     = rs{2};
  let Inst{2}     = 0b1;
  let Inst{1-0}   = rs{1-0};
}

class POOL16C_OR16_XOR16_FM_MMR6<bits<4> op> {
  bits<3> rt;
  bits<3> rs;

  bits<16> Inst;

  let Inst{15-10} = 0b010001;
  let Inst{9-7}   = rt;
  let Inst{6-4}   = rs;
  let Inst{3-0}   = op;
}

class POOL16C_BREAKPOINT_FM_MMR6<bits<6> op> {
  bits<4> code_;
  bits<16> Inst;

  let Inst{15-10} = 0b010001;
  let Inst{9-6}   = code_;
  let Inst{5-0}   = op;
}

class POOL16A_SUBU16_FM_MMR6 {
  bits<3> rs;
  bits<3> rt;
  bits<3> rd;

  bits<16> Inst;

  let Inst{15-10} = 0b000001;
  let Inst{9-7}   = rs;
  let Inst{6-4}   = rt;
  let Inst{3-1}   = rd;
  let Inst{0}     = 0b1;
}

class POOL32A_WRPGPR_WSBH_FM_MMR6<string instr_asm, bits<10> funct>
    : MMR6Arch<instr_asm>, MipsR6Inst {
  bits<5> rt;
  bits<5> rs;

  bits<32> Inst;

  let Inst{31-26} = 0x00;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-6}  = funct;
  let Inst{5-0}   = 0x3c;
}

class POOL32F_RECIP_ROUND_FM_MMR6<string instr_asm, bits<1> fmt, bits<8> funct>
    : MMR6Arch<instr_asm>, MipsR6Inst {
  bits<5> ft;
  bits<5> fs;

  bits<32> Inst;

  let Inst{31-26} = 0b010101;
  let Inst{25-21} = ft;
  let Inst{20-16} = fs;
  let Inst{15}    = 0;
  let Inst{14}    = fmt;
  let Inst{13-6}  = funct;
  let Inst{5-0}   = 0b111011;
}

class POOL32F_RINT_FM_MMR6<string instr_asm, bits<2> fmt> : MMR6Arch<instr_asm>,
                                                            MipsR6Inst {
  bits<5> fs;
  bits<5> fd;

  bits<32> Inst;

  let Inst{31-26} = 0b010101;
  let Inst{25-21} = fs;
  let Inst{20-16} = fd;
  let Inst{15-11} = 0;
  let Inst{10-9}  = fmt;
  let Inst{8-0}   = 0b000100000;
}

class POOL32F_SEL_FM_MMR6<string instr_asm, bits<2> fmt, bits<9> funct>
    : MMR6Arch<instr_asm>, MipsR6Inst {
  bits<5> ft;
  bits<5> fs;
  bits<5> fd;

  bits<32> Inst;

  let Inst{31-26} = 0b010101;
  let Inst{25-21} = ft;
  let Inst{20-16} = fs;
  let Inst{15-11} = fd;
  let Inst{10-9}  = fmt;
  let Inst{8-0}   = funct;
}

class POOL32F_CLASS_FM_MMR6<string instr_asm, bits<2> fmt, bits<9> funct>
    : MMR6Arch<instr_asm>, MipsR6Inst {
  bits<5> fs;
  bits<5> fd;

  bits<32> Inst;

  let Inst{31-26} = 0b010101;
  let Inst{25-21} = fs;
  let Inst{20-16} = fd;
  let Inst{15-11} = 0b00000;
  let Inst{10-9}  = fmt;
  let Inst{8-0}   = funct;
}

class POOL32A_TLBINV_FM_MMR6<string instr_asm, bits<10> funct>
    : MMR6Arch<instr_asm>, MipsR6Inst {
  bits<32> Inst;

  let Inst{31-26} = 0x0;
  let Inst{25-16} = 0x0;
  let Inst{15-6}  = funct;
  let Inst{5-0}   = 0b111100;
}

class POOL32A_MFTC0_FM_MMR6<string instr_asm, bits<5> funct, bits<6> opcode>
    : MMR6Arch<instr_asm>, MipsR6Inst {
  bits<5> rt;
  bits<5> rs;
  bits<3> sel;

  bits<32> Inst;

  let Inst{31-26} = 0b000000;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-14} = 0;
  let Inst{13-11} = sel;
  let Inst{10-6}  = funct;
  let Inst{5-0}   = opcode;
}

class POOL32A_GINV_FM_MMR6<string instr_asm, bits<2> ginv>
    : MMR6Arch<instr_asm>, MipsR6Inst {
  bits<5> rs;
  bits<2> type;

  bits<32> Inst;

  let Inst{31-26} = 0x0;
  let Inst{25-21} = 0x0;
  let Inst{20-16} = rs;
  let Inst{15-13} = 0b011;
  let Inst{12-11} = ginv;
  let Inst{10-9}  = type;
  let Inst{8-6}   = 0b101;
  let Inst{5-0}   = 0b111100;
}

class POOL32F_MFTC1_FM_MMR6<string instr_asm, bits<8> funct>
    : MMR6Arch<instr_asm> {
  bits<5> rt;
  bits<5> fs;

  bits<32> Inst;

  let Inst{31-26} = 0b010101;
  let Inst{25-21} = rt;
  let Inst{20-16} = fs;
  let Inst{15-14} = 0;
  let Inst{13-6}  = funct;
  let Inst{5-0}   = 0b111011;
}

class POOL32A_MFTC2_FM_MMR6<string instr_asm, bits<10> funct>
    : MMR6Arch<instr_asm>, MipsR6Inst {
  bits<5> rt;
  bits<5> impl;

  bits<32> Inst;

  let Inst{31-26} = 0b000000;
  let Inst{25-21} = rt;
  let Inst{20-16} = impl;
  let Inst{15-6}  = funct;
  let Inst{5-0}   = 0b111100;
}

class CMP_BRANCH_2R_OFF16_FM_MMR6<string opstr, bits<6> funct>
    : MipsR6Inst, MMR6Arch<opstr> {
  bits<5> rt;
  bits<5> rs;
  bits<16> offset;

  bits<32> Inst;

  let Inst{31-26} = funct;
  let Inst{25-21} = rt;
  let Inst{20-16} = rs;
  let Inst{15-0}  = offset;
}

class POOL32A_DVPEVP_FM_MMR6<string instr_asm, bits<10> funct>
    : MMR6Arch<instr_asm>, MipsR6Inst {
  bits<5> rs;

  bits<32> Inst;

  let Inst{31-26} = 0b000000;
  let Inst{25-21} = 0b00000;
  let Inst{20-16} = rs;
  let Inst{15-6}  = funct;
  let Inst{5-0}   = 0b111100;
}

class CMP_BRANCH_OFF21_FM_MMR6<bits<6> funct> : MipsR6Inst {
  bits<5> rs;
  bits<21> offset;

  bits<32> Inst;

  let Inst{31-26} = funct;
  let Inst{25-21} = rs;
  let Inst{20-0} = offset;
}

class POOL32I_BRANCH_COP_1_2_FM_MMR6<string instr_asm, bits<5> funct>
    : MMR6Arch<instr_asm> {
  bits<5> rt;
  bits<16> offset;

  bits<32> Inst;

  let Inst{31-26} = 0b010000;
  let Inst{25-21} = funct;
  let Inst{20-16} = rt;
  let Inst{15-0}  = offset;
}

class LDWC1_SDWC1_FM_MMR6<string instr_asm, bits<6> funct>
    : MMR6Arch<instr_asm> {
  bits<5> ft;
  bits<21> addr;
  bits<5> base = addr{20-16};
  bits<16> offset = addr{15-0};

  bits<32> Inst;

  let Inst{31-26} = funct;
  let Inst{25-21} = ft;
  let Inst{20-16} = base;
  let Inst{15-0}  = offset;
}

class POOL32B_LDWC2_SDWC2_FM_MMR6<string instr_asm, bits<4> funct>
    : MMR6Arch<instr_asm>, MipsR6Inst {
  bits<5> rt;
  bits<21> addr;
  bits<5> base = addr{20-16};
  bits<11> offset = addr{10-0};

  bits<32> Inst;

  let Inst{31-26} = 0b001000;
  let Inst{25-21} = rt;
  let Inst{20-16} = base;
  let Inst{15-12} = funct;
  let Inst{11}    = 0;
  let Inst{10-0}  = offset;
}

class POOL32C_LL_E_SC_E_FM_MMR6<string instr_asm, bits<4> majorFunc,
                                bits<3> minorFunc> : MMR6Arch<instr_asm>,
                                                     MipsR6Inst {
  bits<5> rt;
  bits<21> addr;
  bits<5> base = addr{20-16};
  bits<9> offset = addr{8-0};

  bits<32> Inst;

  let Inst{31-26} = 0b011000;
  let Inst{25-21} = rt;
  let Inst{20-16} = base;
  let Inst{15-12} = majorFunc;
  let Inst{11-9}  = minorFunc;
  let Inst{8-0}   = offset;
}

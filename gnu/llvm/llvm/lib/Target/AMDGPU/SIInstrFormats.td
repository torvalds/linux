//===-- SIInstrFormats.td - SI Instruction Encodings ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// SI Instruction format definitions.
//
//===----------------------------------------------------------------------===//

class InstSI <dag outs, dag ins, string asm = "",
              list<dag> pattern = []> :
  AMDGPUInst<outs, ins, asm, pattern>, PredicateControl {
  // Low bits - basic encoding information.
  field bit SALU = 0;
  field bit VALU = 0;

  // SALU instruction formats.
  field bit SOP1 = 0;
  field bit SOP2 = 0;
  field bit SOPC = 0;
  field bit SOPK = 0;
  field bit SOPP = 0;

  // VALU instruction formats.
  field bit VOP1 = 0;
  field bit VOP2 = 0;
  field bit VOPC = 0;
  field bit VOP3 = 0;
  field bit VOP3P = 0;
  field bit VINTRP = 0;
  field bit SDWA = 0;
  field bit DPP = 0;
  field bit TRANS = 0;

  // Memory instruction formats.
  field bit MUBUF = 0;
  field bit MTBUF = 0;
  field bit SMRD = 0;
  field bit MIMG = 0;
  field bit VIMAGE = 0;
  field bit VSAMPLE = 0;
  field bit EXP = 0;
  field bit FLAT = 0;
  field bit DS = 0;

  // Combined SGPR/VGPR spill bit
  field bit Spill = 0;

  // LDSDIR instruction format.
  field bit LDSDIR = 0;

  // VINTERP instruction format.
  field bit VINTERP = 0;

  // High bits - other information.
  field bit VM_CNT = 0;
  field bit EXP_CNT = 0;
  field bit LGKM_CNT = 0;

  // Whether WQM _must_ be enabled for this instruction.
  field bit WQM = 0;

  // Whether WQM _must_ be disabled for this instruction.
  field bit DisableWQM = 0;

  field bit Gather4 = 0;

  // This is an s_store_dword* instruction that requires a cache flush
  // on wave termination. It is necessary to distinguish from mayStore
  // SMEM instructions like the cache flush ones.
  field bit ScalarStore = 0;

  // Whether the operands can be ignored when computing the
  // instruction size.
  field bit FixedSize = 0;

  // This bit indicates that this is a VOP3 opcode which supports op_sel
  // modifier.
  field bit VOP3_OPSEL = 0;

  // Is it possible for this instruction to be atomic?
  field bit maybeAtomic = 1;

  // This bit indicates that this is a VI instruction which is renamed
  // in GFX9. Required for correct mapping from pseudo to MC.
  field bit renamedInGFX9 = 0;

  // This bit indicates that this has a floating point result type, so
  // the clamp modifier has floating point semantics.
  field bit FPClamp = 0;

  // This bit indicates that instruction may support integer clamping
  // which depends on GPU features.
  field bit IntClamp = 0;

  // This field indicates that the clamp applies to the low component
  // of a packed output register.
  field bit ClampLo = 0;

  // This field indicates that the clamp applies to the high component
  // of a packed output register.
  field bit ClampHi = 0;

  // This bit indicates that this is a packed VOP3P instruction
  field bit IsPacked = 0;

  // This bit indicates that this is a D16 buffer instruction.
  field bit D16Buf = 0;

  // This field indicates that FLAT instruction accesses FLAT_GLBL segment.
  // Must be 0 for non-FLAT instructions.
  field bit FlatGlobal = 0;

  // Reads the mode register, usually for FP environment.
  field bit ReadsModeReg = 0;

  // This bit indicates that this uses the floating point double precision
  // rounding mode flags
  field bit FPDPRounding = 0;

  // Instruction is FP atomic.
  field bit FPAtomic = 0;

  // This bit indicates that this is one of MFMA instructions.
  field bit IsMAI = 0;

  // This bit indicates that this is one of DOT instructions.
  field bit IsDOT = 0;

  // This field indicates that FLAT instruction accesses FLAT_SCRATCH segment.
  // Must be 0 for non-FLAT instructions.
  field bit FlatScratch = 0;

  // Atomic without a return.
  field bit IsAtomicNoRet = 0;

  // Atomic with return.
  field bit IsAtomicRet = 0;

  // This bit indicates that this is one of WMMA instructions.
  field bit IsWMMA = 0;

  // This bit indicates that tied source will not be read.
  field bit TiedSourceNotRead = 0;

  // This bit indicates that the instruction is never-uniform/divergent
  field bit IsNeverUniform = 0;

  // ds_gws_* instructions.
  field bit GWS = 0;

  // This bit indicates that this is one of SWMMAC instructions.
  field bit IsSWMMAC = 0;

  // These need to be kept in sync with the enum in SIInstrFlags.
  let TSFlags{0} = SALU;
  let TSFlags{1} = VALU;

  let TSFlags{2} = SOP1;
  let TSFlags{3} = SOP2;
  let TSFlags{4} = SOPC;
  let TSFlags{5} = SOPK;
  let TSFlags{6} = SOPP;

  let TSFlags{7} = VOP1;
  let TSFlags{8} = VOP2;
  let TSFlags{9} = VOPC;
  let TSFlags{10} = VOP3;
  let TSFlags{12} = VOP3P;

  let TSFlags{13} = VINTRP;
  let TSFlags{14} = SDWA;
  let TSFlags{15} = DPP;
  let TSFlags{16} = TRANS;

  let TSFlags{17} = MUBUF;
  let TSFlags{18} = MTBUF;
  let TSFlags{19} = SMRD;
  let TSFlags{20} = MIMG;
  let TSFlags{21} = VIMAGE;
  let TSFlags{22} = VSAMPLE;
  let TSFlags{23} = EXP;
  let TSFlags{24} = FLAT;
  let TSFlags{25} = DS;

  let TSFlags{26} = Spill;

  // Reserved, must be 0
  let TSFlags{27} = 0;

  let TSFlags{28} = LDSDIR;
  let TSFlags{29} = VINTERP;

  let TSFlags{32} = VM_CNT;
  let TSFlags{33} = EXP_CNT;
  let TSFlags{34} = LGKM_CNT;

  let TSFlags{35} = WQM;
  let TSFlags{36} = DisableWQM;
  let TSFlags{37} = Gather4;

  // Reserved, must be 0.
  let TSFlags{38} = 0;

  let TSFlags{39} = ScalarStore;
  let TSFlags{40} = FixedSize;

  // Reserved, must be 0.
  let TSFlags{41} = 0;

  let TSFlags{42} = VOP3_OPSEL;

  let TSFlags{43} = maybeAtomic;
  let TSFlags{44} = renamedInGFX9;

  let TSFlags{45} = FPClamp;
  let TSFlags{46} = IntClamp;
  let TSFlags{47} = ClampLo;
  let TSFlags{48} = ClampHi;

  let TSFlags{49} = IsPacked;

  let TSFlags{50} = D16Buf;

  let TSFlags{51} = FlatGlobal;

  let TSFlags{52} = FPDPRounding;

  let TSFlags{53} = FPAtomic;

  let TSFlags{54} = IsMAI;

  let TSFlags{55} = IsDOT;

  let TSFlags{56} = FlatScratch;

  let TSFlags{57} = IsAtomicNoRet;

  let TSFlags{58} = IsAtomicRet;

  let TSFlags{59} = IsWMMA;

  let TSFlags{60} = TiedSourceNotRead;

  let TSFlags{61} = IsNeverUniform;
  
  let TSFlags{62} = GWS;

  let TSFlags{63} = IsSWMMAC;

  let SchedRW = [Write32Bit];

  let AsmVariantName = AMDGPUAsmVariants.Default;

  // Avoid changing source registers in a way that violates constant bus read limitations.
  let hasExtraSrcRegAllocReq = !or(VOP1, VOP2, VOP3, VOPC, SDWA, VALU);
}

class PseudoInstSI<dag outs, dag ins, list<dag> pattern = [], string asm = "">
  : InstSI<outs, ins, asm, pattern> {
  let isPseudo = 1;
  let isCodeGenOnly = 1;
}

class SPseudoInstSI<dag outs, dag ins, list<dag> pattern = [], string asm = "">
  : PseudoInstSI<outs, ins, pattern, asm> {
  let SALU = 1;
}

class VPseudoInstSI<dag outs, dag ins, list<dag> pattern = [], string asm = "">
  : PseudoInstSI<outs, ins, pattern, asm> {
  let VALU = 1;
  let Uses = [EXEC];
}

class CFPseudoInstSI<dag outs, dag ins, list<dag> pattern = [],
  bit UseExec = 0, bit DefExec = 0> :
  SPseudoInstSI<outs, ins, pattern> {

  let Uses = !if(UseExec, [EXEC], []);
  let Defs = !if(DefExec, [EXEC, SCC], [SCC]);
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
}

class Enc32 {
  field bits<32> Inst;
  int Size = 4;
}

class Enc64 {
  field bits<64> Inst;
  int Size = 8;
}

class Enc96 {
  field bits<96> Inst;
  int Size = 12;
}

def CPolBit {
  int GLC = 0;
  int SLC = 1;
  int DLC = 2;
  int SCC = 4;
}

class VOPDstOperand <RegisterClass rc> : RegisterOperand <rc, "printVOPDst">;

def VOPDstOperand_t16 : VOPDstOperand <VGPR_16> {
  let EncoderMethod = "getMachineOpValueT16";
  let DecoderMethod = "DecodeVGPR_16RegisterClass";
}

def VOPDstOperand_t16Lo128 : VOPDstOperand <VGPR_16_Lo128> {
  let EncoderMethod = "getMachineOpValueT16Lo128";
  let DecoderMethod = "DecodeVGPR_16_Lo128RegisterClass";
}

class VINTRPe <bits<2> op> : Enc32 {
  bits<8> vdst;
  bits<8> vsrc;
  bits<2> attrchan;
  bits<6> attr;

  let Inst{7-0} = vsrc;
  let Inst{9-8} = attrchan;
  let Inst{15-10} = attr;
  let Inst{17-16} = op;
  let Inst{25-18} = vdst;
  let Inst{31-26} = 0x32; // encoding
}

class MIMGe_gfxpre11 : Enc64 {
  bits<10> vdata;
  bits<4> dmask;
  bits<1> unorm;
  bits<5> cpol;
  bits<1> r128;
  bits<1> tfe;
  bits<1> lwe;
  bit d16;
  bits<7> srsrc;
  bits<7> ssamp;

  let Inst{11-8} = dmask;
  let Inst{12} = unorm;
  let Inst{13} = cpol{CPolBit.GLC};
  let Inst{15} = r128;
  let Inst{17} = lwe;
  let Inst{25} = cpol{CPolBit.SLC};
  let Inst{31-26} = 0x3c;
  let Inst{47-40} = vdata{7-0};
  let Inst{52-48} = srsrc{6-2};
  let Inst{57-53} = ssamp{6-2};
  let Inst{63} = d16;
}

class MIMGe_gfx6789 <bits<8> op> : MIMGe_gfxpre11 {
  bits<8> vaddr;
  bits<1> da;

  let Inst{0} = op{7};
  let Inst{7} = cpol{CPolBit.SCC};
  let Inst{14} = da;
  let Inst{16} = tfe;
  let Inst{24-18} = op{6-0};
  let Inst{39-32} = vaddr;
}

class MIMGe_gfx90a <bits<8> op> : MIMGe_gfxpre11 {
  bits<8> vaddr;
  bits<1> da;

  let Inst{0} = op{7};
  let Inst{7} = cpol{CPolBit.SCC};
  let Inst{14} = da;
  let Inst{16} = vdata{9}; // ACC bit
  let Inst{24-18} = op{6-0};
  let Inst{39-32} = vaddr;
}

class MIMGe_gfx10 <bits<8> op> : MIMGe_gfxpre11 {
  bits<8> vaddr0;
  bits<3> dim;
  bits<2> nsa;
  bits<1> a16;

  let Inst{0} = op{7};
  let Inst{2-1} = nsa;
  let Inst{5-3} = dim;
  let Inst{7} = cpol{CPolBit.DLC};
  let Inst{16} = tfe;
  let Inst{24-18} = op{6-0};
  let Inst{39-32} = vaddr0;
  let Inst{62} = a16;
}

class MIMGe_gfx11 <bits<8> op> : Enc64 {
  bits<8> vdata;
  bits<4> dmask;
  bits<1> unorm;
  bits<5> cpol;
  bits<1> r128;
  bits<1> tfe;
  bits<1> lwe;
  bits<7> srsrc;
  bits<7> ssamp;
  bit d16;
  bits<1> a16;
  bits<8> vaddr0;
  bits<3> dim;
  bits<1> nsa;

  let Inst{0} = nsa;
  let Inst{4-2} = dim;
  let Inst{7} = unorm;
  let Inst{11-8} = dmask;
  let Inst{12} = cpol{CPolBit.SLC};
  let Inst{13} = cpol{CPolBit.DLC};
  let Inst{14} = cpol{CPolBit.GLC};
  let Inst{15} = r128;
  let Inst{16} = a16;
  let Inst{17} = d16;
  let Inst{25-18} = op;
  let Inst{31-26} = 0x3c;
  let Inst{39-32} = vaddr0;
  let Inst{47-40} = vdata;
  let Inst{52-48} = srsrc{6-2};
  let Inst{53} = tfe;
  let Inst{54} = lwe;
  let Inst{62-58} = ssamp{6-2};
}

class VIMAGE_VSAMPLE_Common <bits<8> op> : Enc96 {
  bits<3> dim;
  bits<1> tfe;
  bits<1> r128;
  bit d16;
  bits<1> a16;
  bits<4> dmask;
  bits<8> vdata;
  bits<9> rsrc;
  bits<6> cpol;
  bits<8> vaddr0;
  bits<8> vaddr1;
  bits<8> vaddr2;
  bits<8> vaddr3;

  let Inst{2-0} = dim;
  let Inst{4} = r128;
  let Inst{5} = d16;
  let Inst{6} = a16;
  let Inst{21-14} = op;
  let Inst{25-22} = dmask;
  let Inst{39-32} = vdata;
  let Inst{49-41} = rsrc;
  let Inst{51-50} = cpol{4-3}; // scope
  let Inst{54-52} = cpol{2-0}; // th
  let Inst{71-64} = vaddr0;
  let Inst{79-72} = vaddr1;
  let Inst{87-80} = vaddr2;
  let Inst{95-88} = vaddr3;
}

class VSAMPLEe <bits<8> op> : VIMAGE_VSAMPLE_Common<op> {
  bits<1> unorm;
  bits<1> lwe;
  bits<9> samp;

  let Inst{3} = tfe;
  let Inst{13} = unorm;
  let Inst{31-26} = 0x39;
  let Inst{40} = lwe;
  let Inst{63-55} = samp;
}

class VIMAGEe <bits<8> op> : VIMAGE_VSAMPLE_Common<op> {
  bits<8> vaddr4;

  let Inst{31-26} = 0x34;
  let Inst{55} = tfe;
  let Inst{63-56} = vaddr4;
}

class EXPe : Enc64 {
  bits<4> en;
  bits<6> tgt;
  bits<1> done;
  bits<8> src0;
  bits<8> src1;
  bits<8> src2;
  bits<8> src3;

  let Inst{3-0} = en;
  let Inst{9-4} = tgt;
  let Inst{11} = done;
  let Inst{31-26} = 0x3e;
  let Inst{39-32} = src0;
  let Inst{47-40} = src1;
  let Inst{55-48} = src2;
  let Inst{63-56} = src3;
}

// Pre-GFX11 encoding has compr and vm bits.
class EXPe_ComprVM : EXPe {
  bits<1> compr;
  bits<1> vm;

  let Inst{10} = compr;
  let Inst{12} = vm;
}

// GFX11+ encoding has row bit.
class EXPe_Row : EXPe {
  bits<1> row;

  let Inst{13} = row;
}

let Uses = [EXEC] in {

class VINTRPCommon <dag outs, dag ins, string asm, list<dag> pattern> :
    InstSI <outs, ins, asm, pattern> {
  let VINTRP = 1;
  // VINTRP instructions read parameter values from LDS, but these parameter
  // values are stored outside of the LDS memory that is allocated to the
  // shader for general purpose use.
  //
  // While it may be possible for ds_read/ds_write instructions to access
  // the parameter values in LDS, this would essentially be an out-of-bounds
  // memory access which we consider to be undefined behavior.
  //
  // So even though these instructions read memory, this memory is outside the
  // addressable memory space for the shader, and we consider these instructions
  // to be readnone.
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let VALU = 1;
}

} // End Uses = [EXEC]

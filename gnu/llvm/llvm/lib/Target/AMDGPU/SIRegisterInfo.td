//===-- SIRegisterInfo.td - SI Register defs ---------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Subregister declarations
//===----------------------------------------------------------------------===//

let Namespace = "AMDGPU" in {

def lo16 : SubRegIndex<16, 0>;
def hi16 : SubRegIndex<16, 16>;

foreach Index = 0...31 in {
  def sub#Index : SubRegIndex<32, !shl(Index, 5)>;
}

foreach Index = 1...31 in {
  def sub#Index#_lo16 : ComposedSubRegIndex<!cast<SubRegIndex>(sub#Index), lo16>;
  def sub#Index#_hi16 : ComposedSubRegIndex<!cast<SubRegIndex>(sub#Index), hi16>;
}

foreach Size = {2...6,8,16} in {
  foreach Index = !range(!sub(33, Size)) in {
    def !interleave(!foreach(cur, !range(Size), "sub"#!add(cur, Index)), "_") :
      SubRegIndex<!mul(Size, 32), !shl(Index, 5)> {
      let CoveringSubRegIndices =
        !foreach(cur, !range(Size), !cast<SubRegIndex>(sub#!add(cur, Index)));
    }
  }
}

}

//===----------------------------------------------------------------------===//
//  Helpers
//===----------------------------------------------------------------------===//

class getSubRegs<int size> {
  list<SubRegIndex> ret2 = [sub0, sub1];
  list<SubRegIndex> ret3 = [sub0, sub1, sub2];
  list<SubRegIndex> ret4 = [sub0, sub1, sub2, sub3];
  list<SubRegIndex> ret5 = [sub0, sub1, sub2, sub3, sub4];
  list<SubRegIndex> ret6 = [sub0, sub1, sub2, sub3, sub4, sub5];
  list<SubRegIndex> ret7 = [sub0, sub1, sub2, sub3, sub4, sub5, sub6];
  list<SubRegIndex> ret8 = [sub0, sub1, sub2, sub3, sub4, sub5, sub6, sub7];
  list<SubRegIndex> ret9 = [sub0, sub1, sub2, sub3, sub4, sub5, sub6, sub7, sub8];
  list<SubRegIndex> ret10 = [sub0, sub1, sub2, sub3,
                             sub4, sub5, sub6, sub7,
                             sub8, sub9];
  list<SubRegIndex> ret11 = [sub0, sub1, sub2, sub3,
                             sub4, sub5, sub6, sub7,
                             sub8, sub9, sub10];
  list<SubRegIndex> ret12 = [sub0, sub1, sub2, sub3,
                             sub4, sub5, sub6, sub7,
                             sub8, sub9, sub10, sub11];
  list<SubRegIndex> ret16 = [sub0, sub1, sub2, sub3,
                             sub4, sub5, sub6, sub7,
                             sub8, sub9, sub10, sub11,
                             sub12, sub13, sub14, sub15];
  list<SubRegIndex> ret32 = [sub0, sub1, sub2, sub3,
                             sub4, sub5, sub6, sub7,
                             sub8, sub9, sub10, sub11,
                             sub12, sub13, sub14, sub15,
                             sub16, sub17, sub18, sub19,
                             sub20, sub21, sub22, sub23,
                             sub24, sub25, sub26, sub27,
                             sub28, sub29, sub30, sub31];

  list<SubRegIndex> ret = !if(!eq(size, 2), ret2,
                              !if(!eq(size, 3), ret3,
                                  !if(!eq(size, 4), ret4,
                                      !if(!eq(size, 5), ret5,
                                          !if(!eq(size, 6), ret6,
                                              !if(!eq(size, 7), ret7,
                                                  !if(!eq(size, 8), ret8,
                                                      !if(!eq(size, 9), ret9,
                                                          !if(!eq(size, 10), ret10,
                                                              !if(!eq(size, 11), ret11,
                                                                  !if(!eq(size, 12), ret12,
                                                                      !if(!eq(size, 16), ret16,
                                                                          ret32))))))))))));
}

// Generates list of sequential register tuple names.
// E.g. RegSeq<3,2,2,"s">.ret -> [ "s[0:1]", "s[2:3]" ]
class RegSeqNames<int last_reg, int stride, int size, string prefix,
                  int start = 0> {
  int next = !add(start, stride);
  int end_reg = !add(start, size, -1);
  list<string> ret =
    !if(!le(end_reg, last_reg),
        !listconcat([prefix # "[" # start # ":" # end_reg # "]"],
                    RegSeqNames<last_reg, stride, size, prefix, next>.ret),
                    []);
}

// Generates list of dags for register tuples.
class RegSeqDags<RegisterClass RC, int last_reg, int stride, int size,
                int start = 0> {
  dag trunc_rc = (trunc RC,
                  !if(!and(!eq(stride, 1), !eq(start, 0)),
                      !sub(!add(last_reg, 2), size),
                      !add(last_reg, 1)));
  list<dag> ret =
    !if(!lt(start, size),
        !listconcat([(add (decimate (shl trunc_rc, start), stride))],
                    RegSeqDags<RC, last_reg, stride, size, !add(start, 1)>.ret),
        []);
}

class SIRegisterTuples<list<SubRegIndex> Indices, RegisterClass RC,
                       int last_reg, int stride, int size, string prefix> :
  RegisterTuples<Indices,
                 RegSeqDags<RC, last_reg, stride, size>.ret,
                 RegSeqNames<last_reg, stride, size, prefix>.ret>;

//===----------------------------------------------------------------------===//
//  Declarations that describe the SI registers
//===----------------------------------------------------------------------===//
class SIReg <string n, bits<8> regIdx = 0, bit isAGPROrVGPR = 0,
             bit isHi = 0> : Register<n> {
  let Namespace = "AMDGPU";

  // These are generic helper values we use to form actual register
  // codes. They should not be assumed to match any particular register
  // encodings on any particular subtargets.
  let HWEncoding{7-0} = regIdx;
  let HWEncoding{8} = isAGPROrVGPR;
  let HWEncoding{9} = isHi;

  int Index = !cast<int>(regIdx);
}

// For register classes that use TSFlags.
class SIRegisterClass <string n, list<ValueType> rTypes, int Align, dag rList>
  : RegisterClass <n, rTypes, Align, rList> {
  // For vector register classes.
  field bit HasVGPR = 0;
  field bit HasAGPR = 0;

  // For scalar register classes.
  field bit HasSGPR = 0;

  // Alignment of the first register in tuple (in 32-bit units).
  field int RegTupleAlignUnits = 1;

  // These need to be kept in sync with the enum SIRCFlags.
  let TSFlags{1-0} = RegTupleAlignUnits;
  let TSFlags{2} = HasVGPR;
  let TSFlags{3} = HasAGPR;
  let TSFlags{4} = HasSGPR;
}

multiclass SIRegLoHi16 <string n, bits<8> regIdx, bit ArtificialHigh = 1,
                        bit isAGPROrVGPR = 0> {
  def _LO16 : SIReg<n#".l", regIdx, isAGPROrVGPR>;
  def _HI16 : SIReg<!if(ArtificialHigh, "", n#".h"), regIdx, isAGPROrVGPR,
                    /* isHi */ 1> {
    let isArtificial = ArtificialHigh;
  }
  def "" : RegisterWithSubRegs<n, [!cast<Register>(NAME#"_LO16"),
                                   !cast<Register>(NAME#"_HI16")]> {
    let Namespace = "AMDGPU";
    let SubRegIndices = [lo16, hi16];
    let CoveredBySubRegs = !not(ArtificialHigh);
    let HWEncoding{7-0} = regIdx;
    let HWEncoding{8} = isAGPROrVGPR;

    int Index = !cast<int>(regIdx);
  }
}

// Special Registers
defm VCC_LO : SIRegLoHi16<"vcc_lo", 106>;
defm VCC_HI : SIRegLoHi16<"vcc_hi", 107>;

// Pseudo-registers: Used as placeholders during isel and immediately
// replaced, never seeing the verifier.
def PRIVATE_RSRC_REG : SIReg<"private_rsrc", 0>;
def FP_REG : SIReg<"fp", 0>;
def SP_REG : SIReg<"sp", 0>;

// Pseudo-register to represent the program-counter DWARF register.
def PC_REG : SIReg<"pc", 0>, DwarfRegNum<[16, 16]> {
  // There is no physical register corresponding to a "program counter", but
  // we need to encode the concept in debug information in order to represent
  // things like the return value in unwind information.
  let isArtificial = 1;
}

// VCC for 64-bit instructions
def VCC : RegisterWithSubRegs<"vcc", [VCC_LO, VCC_HI]> {
  let Namespace = "AMDGPU";
  let SubRegIndices = [sub0, sub1];
  let HWEncoding = VCC_LO.HWEncoding;
}

defm EXEC_LO : SIRegLoHi16<"exec_lo", 126>, DwarfRegNum<[1, 1]>;
defm EXEC_HI : SIRegLoHi16<"exec_hi", 127>;

def EXEC : RegisterWithSubRegs<"exec", [EXEC_LO, EXEC_HI]>, DwarfRegNum<[17, 1]> {
  let Namespace = "AMDGPU";
  let SubRegIndices = [sub0, sub1];
  let HWEncoding = EXEC_LO.HWEncoding;
}

// 32-bit real registers, for MC only.
// May be used with both 32-bit and 64-bit operands.
defm SRC_VCCZ : SIRegLoHi16<"src_vccz", 251>;
defm SRC_EXECZ : SIRegLoHi16<"src_execz", 252>;
defm SRC_SCC : SIRegLoHi16<"src_scc", 253>;

// 1-bit pseudo register, for codegen only.
// Should never be emitted.
def SCC : SIReg<"scc">;

// Encoding changes between subtarget generations.
// See also Utils/AMDGPUBaseInfo.cpp MAP_REG2REG.
defm M0_gfxpre11 : SIRegLoHi16 <"m0", 124>;
defm M0_gfx11plus : SIRegLoHi16 <"m0", 125>;
defm M0 : SIRegLoHi16 <"m0", 0>;

defm SGPR_NULL_gfxpre11 : SIRegLoHi16 <"null", 125>;
defm SGPR_NULL_gfx11plus : SIRegLoHi16 <"null", 124>;
let isConstant = true in {
defm SGPR_NULL : SIRegLoHi16 <"null", 0>;
defm SGPR_NULL_HI : SIRegLoHi16 <"", 0>;
} // isConstant = true

def SGPR_NULL64 :
    RegisterWithSubRegs<"null", [SGPR_NULL, SGPR_NULL_HI]> {
  let Namespace = "AMDGPU";
  let SubRegIndices = [sub0, sub1];
  let HWEncoding = SGPR_NULL.HWEncoding;
  let isConstant = true;
}

// Aperture registers are 64 bit registers with a LO/HI 32 bit.
// HI 32 bit cannot be used, and LO 32 is used by instructions
// with 32 bit sources.
//
// Note that the low 32 bits are essentially useless as they
// don't contain the lower 32 bits of the address - they are in
// the high 32 bits. The lower 32 bits are always zero (for base) or
// -1 (for limit). Since we cannot access the high 32 bits, when we
// need them, we need to do a 64 bit load and extract the bits manually.
multiclass ApertureRegister<string name, bits<8> regIdx> {
  let isConstant = true in {
    // FIXME: We shouldn't need to define subregisters for these (nor add them to any 16 bit
    //  register classes), but if we don't it seems to confuse the TableGen
    //  backend and we end up with a lot of weird register pressure sets and classes.
    defm _LO : SIRegLoHi16 <name, regIdx>;
    defm _HI : SIRegLoHi16 <"", regIdx>;

    def "" : RegisterWithSubRegs<name, [!cast<Register>(NAME#_LO), !cast<Register>(NAME#_HI)]> {
      let Namespace = "AMDGPU";
      let SubRegIndices = [sub0, sub1];
      let HWEncoding = !cast<Register>(NAME#_LO).HWEncoding;
    }
  } // isConstant = true
}

defm SRC_SHARED_BASE   : ApertureRegister<"src_shared_base",   235>;
defm SRC_SHARED_LIMIT  : ApertureRegister<"src_shared_limit",  236>;
defm SRC_PRIVATE_BASE  : ApertureRegister<"src_private_base",  237>;
defm SRC_PRIVATE_LIMIT : ApertureRegister<"src_private_limit", 238>;

defm SRC_POPS_EXITING_WAVE_ID : SIRegLoHi16<"src_pops_exiting_wave_id", 239>;

// Not addressable
def MODE : SIReg <"mode", 0>;

def LDS_DIRECT : SIReg <"src_lds_direct", 254> {
  // There is no physical register corresponding to this. This is an
  // encoding value in a source field, which will ultimately trigger a
  // read from m0.
  let isArtificial = 1;
}

defm XNACK_MASK_LO : SIRegLoHi16<"xnack_mask_lo", 104>;
defm XNACK_MASK_HI : SIRegLoHi16<"xnack_mask_hi", 105>;

def XNACK_MASK :
    RegisterWithSubRegs<"xnack_mask", [XNACK_MASK_LO, XNACK_MASK_HI]> {
  let Namespace = "AMDGPU";
  let SubRegIndices = [sub0, sub1];
  let HWEncoding = XNACK_MASK_LO.HWEncoding;
}

// Trap handler registers
defm TBA_LO : SIRegLoHi16<"tba_lo", 108>;
defm TBA_HI : SIRegLoHi16<"tba_hi", 109>;

def TBA : RegisterWithSubRegs<"tba", [TBA_LO, TBA_HI]> {
  let Namespace = "AMDGPU";
  let SubRegIndices = [sub0, sub1];
  let HWEncoding = TBA_LO.HWEncoding;
}

defm TMA_LO : SIRegLoHi16<"tma_lo", 110>;
defm TMA_HI : SIRegLoHi16<"tma_hi", 111>;

def TMA : RegisterWithSubRegs<"tma", [TMA_LO, TMA_HI]> {
  let Namespace = "AMDGPU";
  let SubRegIndices = [sub0, sub1];
  let HWEncoding = TMA_LO.HWEncoding;
}

foreach Index = 0...15 in {
  defm TTMP#Index#_vi       : SIRegLoHi16<"ttmp"#Index, !add(112, Index)>;
  defm TTMP#Index#_gfx9plus : SIRegLoHi16<"ttmp"#Index, !add(108, Index)>;
  defm TTMP#Index           : SIRegLoHi16<"ttmp"#Index, 0>;
}

multiclass FLAT_SCR_LOHI_m <string n, bits<8> ci_e, bits<8> vi_e> {
  defm _ci : SIRegLoHi16<n, ci_e>;
  defm _vi : SIRegLoHi16<n, vi_e>;
  defm "" : SIRegLoHi16<n, 0>;
}

class FlatReg <Register lo, Register hi, bits<16> encoding> :
    RegisterWithSubRegs<"flat_scratch", [lo, hi]> {
  let Namespace = "AMDGPU";
  let SubRegIndices = [sub0, sub1];
  let HWEncoding = encoding;
}

defm FLAT_SCR_LO : FLAT_SCR_LOHI_m<"flat_scratch_lo", 104, 102>; // Offset in units of 256-bytes.
defm FLAT_SCR_HI : FLAT_SCR_LOHI_m<"flat_scratch_hi", 105, 103>; // Size is the per-thread scratch size, in bytes.

def FLAT_SCR_ci : FlatReg<FLAT_SCR_LO_ci, FLAT_SCR_HI_ci, 104>;
def FLAT_SCR_vi : FlatReg<FLAT_SCR_LO_vi, FLAT_SCR_HI_vi, 102>;
def FLAT_SCR : FlatReg<FLAT_SCR_LO, FLAT_SCR_HI, 0>;

// SGPR registers
foreach Index = 0...105 in {
  defm SGPR#Index :
     SIRegLoHi16 <"s"#Index, Index>,
     DwarfRegNum<[!if(!le(Index, 63), !add(Index, 32), !add(Index, 1024)),
                  !if(!le(Index, 63), !add(Index, 32), !add(Index, 1024))]>;
}

// VGPR registers
foreach Index = 0...255 in {
  defm VGPR#Index :
    SIRegLoHi16 <"v"#Index, Index, 0, 1>,
    DwarfRegNum<[!add(Index, 2560), !add(Index, 1536)]>;
}

// AccVGPR registers
foreach Index = 0...255 in {
  defm AGPR#Index :
      SIRegLoHi16 <"a"#Index, Index, 1, 1>,
      DwarfRegNum<[!add(Index, 3072), !add(Index, 2048)]>;
}

//===----------------------------------------------------------------------===//
//  Groupings using register classes and tuples
//===----------------------------------------------------------------------===//

def SCC_CLASS : SIRegisterClass<"AMDGPU", [i1], 1, (add SCC)> {
  let CopyCost = -1;
  let isAllocatable = 0;
  let HasSGPR = 1;
  let BaseClassOrder = 10000;
}

def M0_CLASS : SIRegisterClass<"AMDGPU", [i32], 32, (add M0)> {
  let CopyCost = 1;
  let isAllocatable = 0;
  let HasSGPR = 1;
}

def M0_CLASS_LO16 : SIRegisterClass<"AMDGPU", [i16, f16, bf16], 16, (add M0_LO16)> {
  let CopyCost = 1;
  let Size = 16;
  let isAllocatable = 0;
  let HasSGPR = 1;
}

// TODO: Do we need to set DwarfRegAlias on register tuples?

def SGPR_LO16 : SIRegisterClass<"AMDGPU", [i16, f16, bf16], 16,
                              (add (sequence "SGPR%u_LO16", 0, 105))> {
  let AllocationPriority = 0;
  let Size = 16;
  let GeneratePressureSet = 0;
  let HasSGPR = 1;
}

def SGPR_HI16 : SIRegisterClass<"AMDGPU", [i16, f16, bf16], 16,
                              (add (sequence "SGPR%u_HI16", 0, 105))> {
  let isAllocatable = 0;
  let Size = 16;
  let GeneratePressureSet = 0;
  let HasSGPR = 1;
}

// SGPR 32-bit registers
def SGPR_32 : SIRegisterClass<"AMDGPU", [i32, f32, i16, f16, bf16, v2i16, v2f16, v2bf16], 32,
                            (add (sequence "SGPR%u", 0, 105))> {
  // Give all SGPR classes higher priority than VGPR classes, because
  // we want to spill SGPRs to VGPRs.
  let AllocationPriority = 0;
  let GeneratePressureSet = 0;
  let HasSGPR = 1;
}

// SGPR 64-bit registers
def SGPR_64Regs : SIRegisterTuples<getSubRegs<2>.ret, SGPR_32, 105, 2, 2, "s">;

// SGPR 96-bit registers.
def SGPR_96Regs : SIRegisterTuples<getSubRegs<3>.ret, SGPR_32, 105, 4, 3, "s">;

// SGPR 128-bit registers
def SGPR_128Regs : SIRegisterTuples<getSubRegs<4>.ret, SGPR_32, 105, 4, 4, "s">;

// SGPR 160-bit registers. No operations use these, but for symmetry with 160-bit VGPRs.
def SGPR_160Regs : SIRegisterTuples<getSubRegs<5>.ret, SGPR_32, 105, 4, 5, "s">;

// SGPR 192-bit registers. No operations use these, but for symmetry with 192-bit VGPRs.
def SGPR_192Regs : SIRegisterTuples<getSubRegs<6>.ret, SGPR_32, 105, 4, 6, "s">;

// SGPR 224-bit registers. No operations use these, but for symmetry with 224-bit VGPRs.
def SGPR_224Regs : SIRegisterTuples<getSubRegs<7>.ret, SGPR_32, 105, 4, 7, "s">;

// SGPR 256-bit registers
def SGPR_256Regs : SIRegisterTuples<getSubRegs<8>.ret, SGPR_32, 105, 4, 8, "s">;

// SGPR 288-bit registers. No operations use these, but for symmetry with 288-bit VGPRs.
def SGPR_288Regs : SIRegisterTuples<getSubRegs<9>.ret, SGPR_32, 105, 4, 9, "s">;

// SGPR 320-bit registers. No operations use these, but for symmetry with 320-bit VGPRs.
def SGPR_320Regs : SIRegisterTuples<getSubRegs<10>.ret, SGPR_32, 105, 4, 10, "s">;

// SGPR 352-bit registers. No operations use these, but for symmetry with 352-bit VGPRs.
def SGPR_352Regs : SIRegisterTuples<getSubRegs<11>.ret, SGPR_32, 105, 4, 11, "s">;

// SGPR 384-bit registers. No operations use these, but for symmetry with 384-bit VGPRs.
def SGPR_384Regs : SIRegisterTuples<getSubRegs<12>.ret, SGPR_32, 105, 4, 12, "s">;

// SGPR 512-bit registers
def SGPR_512Regs : SIRegisterTuples<getSubRegs<16>.ret, SGPR_32, 105, 4, 16, "s">;

// SGPR 1024-bit registers
def SGPR_1024Regs : SIRegisterTuples<getSubRegs<32>.ret, SGPR_32, 105, 4, 32, "s">;

// Trap handler TMP 32-bit registers
def TTMP_32 : SIRegisterClass<"AMDGPU", [i32, f32, v2i16, v2f16, v2bf16], 32,
                            (add (sequence "TTMP%u", 0, 15))> {
  let isAllocatable = 0;
  let HasSGPR = 1;
}

// Trap handler TMP 16-bit registers
def TTMP_LO16 : SIRegisterClass<"AMDGPU", [i16, f16, bf16], 16,
                              (add (sequence "TTMP%u_LO16", 0, 15))> {
  let Size = 16;
  let isAllocatable = 0;
  let HasSGPR = 1;
}

// Trap handler TMP 64-bit registers
def TTMP_64Regs : SIRegisterTuples<getSubRegs<2>.ret, TTMP_32, 15, 2, 2, "ttmp">;

// Trap handler TMP 96-bit registers
def TTMP_96Regs : SIRegisterTuples<getSubRegs<3>.ret, TTMP_32, 15, 3, 3, "ttmp">;

// Trap handler TMP 128-bit registers
def TTMP_128Regs : SIRegisterTuples<getSubRegs<4>.ret, TTMP_32, 15, 4, 4, "ttmp">;

// Trap handler TMP 160-bit registers
def TTMP_160Regs : SIRegisterTuples<getSubRegs<5>.ret, TTMP_32, 15, 4, 5, "ttmp">;

// Trap handler TMP 192-bit registers
def TTMP_192Regs : SIRegisterTuples<getSubRegs<6>.ret, TTMP_32, 15, 4, 6, "ttmp">;

// Trap handler TMP 224-bit registers
def TTMP_224Regs : SIRegisterTuples<getSubRegs<7>.ret, TTMP_32, 15, 4, 7, "ttmp">;

// Trap handler TMP 256-bit registers
def TTMP_256Regs : SIRegisterTuples<getSubRegs<8>.ret, TTMP_32, 15, 4, 8, "ttmp">;

// Trap handler TMP 288-bit registers
def TTMP_288Regs : SIRegisterTuples<getSubRegs<9>.ret, TTMP_32, 15, 4, 9, "ttmp">;

// Trap handler TMP 320-bit registers
def TTMP_320Regs : SIRegisterTuples<getSubRegs<10>.ret, TTMP_32, 15, 4, 10, "ttmp">;

// Trap handler TMP 352-bit registers
def TTMP_352Regs : SIRegisterTuples<getSubRegs<11>.ret, TTMP_32, 15, 4, 11, "ttmp">;

// Trap handler TMP 384-bit registers
def TTMP_384Regs : SIRegisterTuples<getSubRegs<12>.ret, TTMP_32, 15, 4, 12, "ttmp">;

// Trap handler TMP 512-bit registers
def TTMP_512Regs : SIRegisterTuples<getSubRegs<16>.ret, TTMP_32, 15, 4, 16, "ttmp">;

class TmpRegTuplesBase<int index, int size,
                       list<Register> subRegs,
                       list<SubRegIndex> indices = getSubRegs<size>.ret,
                       int index1 = !add(index, size, -1),
                       string name = "ttmp["#index#":"#index1#"]"> :
  RegisterWithSubRegs<name, subRegs> {
  let HWEncoding = subRegs[0].HWEncoding;
  let SubRegIndices = indices;
}

class TmpRegTuples<string tgt,
                   int size,
                   int index0,
                   int index1 = !add(index0, 1),
                   int index2 = !add(index0, !if(!eq(size, 2), 1, 2)),
                   int index3 = !add(index0, !if(!eq(size, 2), 1, 3)),
                   int index4 = !add(index0, !if(!eq(size, 8), 4, 1)),
                   int index5 = !add(index0, !if(!eq(size, 8), 5, 1)),
                   int index6 = !add(index0, !if(!eq(size, 8), 6, 1)),
                   int index7 = !add(index0, !if(!eq(size, 8), 7, 1)),
                   Register r0 = !cast<Register>("TTMP"#index0#tgt),
                   Register r1 = !cast<Register>("TTMP"#index1#tgt),
                   Register r2 = !cast<Register>("TTMP"#index2#tgt),
                   Register r3 = !cast<Register>("TTMP"#index3#tgt),
                   Register r4 = !cast<Register>("TTMP"#index4#tgt),
                   Register r5 = !cast<Register>("TTMP"#index5#tgt),
                   Register r6 = !cast<Register>("TTMP"#index6#tgt),
                   Register r7 = !cast<Register>("TTMP"#index7#tgt)> :
  TmpRegTuplesBase<index0, size,
                   !if(!eq(size, 2), [r0, r1],
                       !if(!eq(size, 4), [r0, r1, r2, r3],
                                         [r0, r1, r2, r3, r4, r5, r6, r7])),
                   getSubRegs<size>.ret>;

foreach Index = {0, 2, 4, 6, 8, 10, 12, 14} in {
  def TTMP#Index#_TTMP#!add(Index,1)#_vi       : TmpRegTuples<"_vi",   2, Index>;
  def TTMP#Index#_TTMP#!add(Index,1)#_gfx9plus : TmpRegTuples<"_gfx9plus", 2, Index>;
}

foreach Index = {0, 4, 8, 12} in {
  def TTMP#Index#_TTMP#!add(Index,1)#
                 _TTMP#!add(Index,2)#
                 _TTMP#!add(Index,3)#_vi : TmpRegTuples<"_vi",   4, Index>;
  def TTMP#Index#_TTMP#!add(Index,1)#
                 _TTMP#!add(Index,2)#
                 _TTMP#!add(Index,3)#_gfx9plus : TmpRegTuples<"_gfx9plus", 4, Index>;
}

foreach Index = {0, 4, 8} in {
  def TTMP#Index#_TTMP#!add(Index,1)#
                 _TTMP#!add(Index,2)#
                 _TTMP#!add(Index,3)#
                 _TTMP#!add(Index,4)#
                 _TTMP#!add(Index,5)#
                 _TTMP#!add(Index,6)#
                 _TTMP#!add(Index,7)#_vi : TmpRegTuples<"_vi",   8, Index>;
  def TTMP#Index#_TTMP#!add(Index,1)#
                 _TTMP#!add(Index,2)#
                 _TTMP#!add(Index,3)#
                 _TTMP#!add(Index,4)#
                 _TTMP#!add(Index,5)#
                 _TTMP#!add(Index,6)#
                 _TTMP#!add(Index,7)#_gfx9plus : TmpRegTuples<"_gfx9plus", 8, Index>;
}

def TTMP0_TTMP1_TTMP2_TTMP3_TTMP4_TTMP5_TTMP6_TTMP7_TTMP8_TTMP9_TTMP10_TTMP11_TTMP12_TTMP13_TTMP14_TTMP15_vi :
  TmpRegTuplesBase<0, 16,
                   [TTMP0_vi, TTMP1_vi, TTMP2_vi, TTMP3_vi,
                    TTMP4_vi, TTMP5_vi, TTMP6_vi, TTMP7_vi,
                    TTMP8_vi, TTMP9_vi, TTMP10_vi, TTMP11_vi,
                    TTMP12_vi, TTMP13_vi, TTMP14_vi, TTMP15_vi]>;

def TTMP0_TTMP1_TTMP2_TTMP3_TTMP4_TTMP5_TTMP6_TTMP7_TTMP8_TTMP9_TTMP10_TTMP11_TTMP12_TTMP13_TTMP14_TTMP15_gfx9plus :
  TmpRegTuplesBase<0, 16,
                   [TTMP0_gfx9plus, TTMP1_gfx9plus, TTMP2_gfx9plus, TTMP3_gfx9plus,
                    TTMP4_gfx9plus, TTMP5_gfx9plus, TTMP6_gfx9plus, TTMP7_gfx9plus,
                    TTMP8_gfx9plus, TTMP9_gfx9plus, TTMP10_gfx9plus, TTMP11_gfx9plus,
                    TTMP12_gfx9plus, TTMP13_gfx9plus, TTMP14_gfx9plus, TTMP15_gfx9plus]>;

class RegisterTypes<list<ValueType> reg_types> {
  list<ValueType> types = reg_types;
}

def Reg16Types : RegisterTypes<[i16, f16, bf16]>;
def Reg32Types : RegisterTypes<[i32, f32, v2i16, v2f16, v2bf16, p2, p3, p5, p6]>;
def Reg64Types : RegisterTypes<[i64, f64, v2i32, v2f32, p0, v4i16, v4f16, v4bf16]>;
def Reg96Types : RegisterTypes<[v3i32, v3f32]>;
def Reg128Types : RegisterTypes<[v4i32, v4f32, v2i64, v2f64, v8i16, v8f16, v8bf16]>;

let HasVGPR = 1 in {
// VOP3 and VINTERP can access 256 lo and 256 hi registers.
def VGPR_16 : SIRegisterClass<"AMDGPU",  Reg16Types.types, 16,
                            (add (interleave (sequence "VGPR%u_LO16", 0, 255),
                                             (sequence "VGPR%u_HI16", 0, 255)))> {
  let AllocationPriority = 2;
  let Size = 16;
  let GeneratePressureSet = 0;

  // This is the base class for VGPR{128..255}_{LO16,HI16}.
  let BaseClassOrder = 17;
}

// VOP1/2/C can access the First 128 lo and 128 hi registers.
// The order of registers in the class determines order of allocation, so it is
// important to interleave lo and hi registers.
def VGPR_16_Lo128 : SIRegisterClass<"AMDGPU",  Reg16Types.types, 16,
                            (add (interleave (sequence "VGPR%u_LO16", 0, 127),
                                             (sequence "VGPR%u_HI16", 0, 127)))> {
  let Size = 16;
  let GeneratePressureSet = 0;
  let isAllocatable = 0;

  // This is the base class for VGPR{0..127}_{LO16,HI16}.
  let BaseClassOrder = 16;
}

// VGPR 32-bit registers
// i16/f16 only on VI+
def VGPR_32 : SIRegisterClass<"AMDGPU", !listconcat(Reg32Types.types, Reg16Types.types), 32,
                            (add (sequence "VGPR%u", 0, 255))> {
  let AllocationPriority = 0;
  let Size = 32;
  let Weight = 1;
  let BaseClassOrder = 32;
}

// Identical to VGPR_32 except it only contains the low 128 (Lo128) registers.
def VGPR_32_Lo128 : SIRegisterClass<"AMDGPU", !listconcat(Reg32Types.types, Reg16Types.types), 32,
                            (add (sequence "VGPR%u", 0, 127))> {
  let AllocationPriority = 0;
  let GeneratePressureSet = 0;
  let Size = 32;
  let Weight = 1;
}
} // End HasVGPR = 1

// VGPR 64-bit registers
def VGPR_64 : SIRegisterTuples<getSubRegs<2>.ret, VGPR_32, 255, 1, 2, "v">;

// VGPR 96-bit registers
def VGPR_96 : SIRegisterTuples<getSubRegs<3>.ret, VGPR_32, 255, 1, 3, "v">;

// VGPR 128-bit registers
def VGPR_128 : SIRegisterTuples<getSubRegs<4>.ret, VGPR_32, 255, 1, 4, "v">;

// VGPR 160-bit registers
def VGPR_160 : SIRegisterTuples<getSubRegs<5>.ret, VGPR_32, 255, 1, 5, "v">;

// VGPR 192-bit registers
def VGPR_192 : SIRegisterTuples<getSubRegs<6>.ret, VGPR_32, 255, 1, 6, "v">;

// VGPR 224-bit registers
def VGPR_224 : SIRegisterTuples<getSubRegs<7>.ret, VGPR_32, 255, 1, 7, "v">;

// VGPR 256-bit registers
def VGPR_256 : SIRegisterTuples<getSubRegs<8>.ret, VGPR_32, 255, 1, 8, "v">;

// VGPR 288-bit registers
def VGPR_288 : SIRegisterTuples<getSubRegs<9>.ret, VGPR_32, 255, 1, 9, "v">;

// VGPR 320-bit registers
def VGPR_320 : SIRegisterTuples<getSubRegs<10>.ret, VGPR_32, 255, 1, 10, "v">;

// VGPR 352-bit registers
def VGPR_352 : SIRegisterTuples<getSubRegs<11>.ret, VGPR_32, 255, 1, 11, "v">;

// VGPR 384-bit registers
def VGPR_384 : SIRegisterTuples<getSubRegs<12>.ret, VGPR_32, 255, 1, 12, "v">;

// VGPR 512-bit registers
def VGPR_512 : SIRegisterTuples<getSubRegs<16>.ret, VGPR_32, 255, 1, 16, "v">;

// VGPR 1024-bit registers
def VGPR_1024 : SIRegisterTuples<getSubRegs<32>.ret, VGPR_32, 255, 1, 32, "v">;

let HasAGPR = 1 in {
def AGPR_LO16 : SIRegisterClass<"AMDGPU", Reg16Types.types, 16,
                              (add (sequence "AGPR%u_LO16", 0, 255))> {
  let isAllocatable = 0;
  let Size = 16;
  let GeneratePressureSet = 0;
  let BaseClassOrder = 16;
}

// AccVGPR 32-bit registers
def AGPR_32 : SIRegisterClass<"AMDGPU", [i32, f32, i16, f16, bf16, v2i16, v2f16, v2bf16], 32,
                            (add (sequence "AGPR%u", 0, 255))> {
  let AllocationPriority = 0;
  let Size = 32;
  let Weight = 1;
  let BaseClassOrder = 32;
}
} // End HasAGPR = 1

// AGPR 64-bit registers
def AGPR_64 : SIRegisterTuples<getSubRegs<2>.ret, AGPR_32, 255, 1, 2, "a">;

// AGPR 96-bit registers
def AGPR_96 : SIRegisterTuples<getSubRegs<3>.ret, AGPR_32, 255, 1, 3, "a">;

// AGPR 128-bit registers
def AGPR_128 : SIRegisterTuples<getSubRegs<4>.ret, AGPR_32, 255, 1, 4, "a">;

// AGPR 160-bit registers
def AGPR_160 : SIRegisterTuples<getSubRegs<5>.ret, AGPR_32, 255, 1, 5, "a">;

// AGPR 192-bit registers
def AGPR_192 : SIRegisterTuples<getSubRegs<6>.ret, AGPR_32, 255, 1, 6, "a">;

// AGPR 224-bit registers
def AGPR_224 : SIRegisterTuples<getSubRegs<7>.ret, AGPR_32, 255, 1, 7, "a">;

// AGPR 256-bit registers
def AGPR_256 : SIRegisterTuples<getSubRegs<8>.ret, AGPR_32, 255, 1, 8, "a">;

// AGPR 288-bit registers
def AGPR_288 : SIRegisterTuples<getSubRegs<9>.ret, AGPR_32, 255, 1, 9, "a">;

// AGPR 320-bit registers
def AGPR_320 : SIRegisterTuples<getSubRegs<10>.ret, AGPR_32, 255, 1, 10, "a">;

// AGPR 352-bit registers
def AGPR_352 : SIRegisterTuples<getSubRegs<11>.ret, AGPR_32, 255, 1, 11, "a">;

// AGPR 384-bit registers
def AGPR_384 : SIRegisterTuples<getSubRegs<12>.ret, AGPR_32, 255, 1, 12, "a">;

// AGPR 512-bit registers
def AGPR_512 : SIRegisterTuples<getSubRegs<16>.ret, AGPR_32, 255, 1, 16, "a">;

// AGPR 1024-bit registers
def AGPR_1024 : SIRegisterTuples<getSubRegs<32>.ret, AGPR_32, 255, 1, 32, "a">;

//===----------------------------------------------------------------------===//
//  Register classes used as source and destination
//===----------------------------------------------------------------------===//

def Pseudo_SReg_32 : SIRegisterClass<"AMDGPU", [i32, f32, i16, f16, bf16, v2i16, v2f16, v2bf16], 32,
  (add FP_REG, SP_REG)> {
  let isAllocatable = 0;
  let CopyCost = -1;
  let HasSGPR = 1;
  let BaseClassOrder = 10000;
}

def Pseudo_SReg_128 : SIRegisterClass<"AMDGPU", Reg128Types.types, 32,
  (add PRIVATE_RSRC_REG)> {
  let isAllocatable = 0;
  let CopyCost = -1;
  let HasSGPR = 1;
  let BaseClassOrder = 10000;
}

def LDS_DIRECT_CLASS : RegisterClass<"AMDGPU", [i32], 32,
  (add LDS_DIRECT)> {
  let isAllocatable = 0;
  let CopyCost = -1;
}

let GeneratePressureSet = 0, HasSGPR = 1 in {
// Subset of SReg_32 without M0 for SMRD instructions and alike.
// See comments in SIInstructions.td for more info.
def SReg_32_XM0_XEXEC : SIRegisterClass<"AMDGPU", [i32, f32, i16, f16, bf16, v2i16, v2f16, v2bf16, i1], 32,
  (add SGPR_32, VCC_LO, VCC_HI, FLAT_SCR_LO, FLAT_SCR_HI, XNACK_MASK_LO, XNACK_MASK_HI,
   SGPR_NULL, SGPR_NULL_HI, TTMP_32, TMA_LO, TMA_HI, TBA_LO, TBA_HI, SRC_SHARED_BASE_LO,
   SRC_SHARED_LIMIT_LO, SRC_PRIVATE_BASE_LO, SRC_PRIVATE_LIMIT_LO, SRC_SHARED_BASE_HI,
   SRC_SHARED_LIMIT_HI, SRC_PRIVATE_BASE_HI, SRC_PRIVATE_LIMIT_HI, SRC_POPS_EXITING_WAVE_ID,
   SRC_VCCZ, SRC_EXECZ, SRC_SCC)> {
  let AllocationPriority = 0;
}

def SReg_LO16 : SIRegisterClass<"AMDGPU", [i16, f16, bf16], 16,
  (add SGPR_LO16, VCC_LO_LO16, VCC_HI_LO16, FLAT_SCR_LO_LO16, FLAT_SCR_HI_LO16,
   XNACK_MASK_LO_LO16, XNACK_MASK_HI_LO16, SGPR_NULL_LO16, SGPR_NULL_HI_LO16, TTMP_LO16,
   TMA_LO_LO16, TMA_HI_LO16, TBA_LO_LO16, TBA_HI_LO16, SRC_SHARED_BASE_LO_LO16,
   SRC_SHARED_LIMIT_LO_LO16, SRC_PRIVATE_BASE_LO_LO16, SRC_PRIVATE_LIMIT_LO_LO16,
   SRC_SHARED_BASE_HI_LO16, SRC_SHARED_LIMIT_HI_LO16, SRC_PRIVATE_BASE_HI_LO16,
   SRC_PRIVATE_LIMIT_HI_LO16, SRC_POPS_EXITING_WAVE_ID_LO16, SRC_VCCZ_LO16,
   SRC_EXECZ_LO16, SRC_SCC_LO16, EXEC_LO_LO16, EXEC_HI_LO16, M0_CLASS_LO16)> {
  let Size = 16;
  let isAllocatable = 0;
  let BaseClassOrder = 16;
}

def SReg_32_XEXEC : SIRegisterClass<"AMDGPU", [i32, f32, i16, f16, bf16, v2i16, v2f16, v2bf16, i1], 32,
  (add SReg_32_XM0_XEXEC, M0_CLASS)> {
  let AllocationPriority = 0;
}

def SReg_32_XEXEC_HI : SIRegisterClass<"AMDGPU", [i32, f32, i16, f16, bf16, v2i16, v2f16, v2bf16, i1], 32,
  (add SReg_32_XEXEC, EXEC_LO)> {
  let AllocationPriority = 0;
}

def SReg_32_XM0 : SIRegisterClass<"AMDGPU", [i32, f32, i16, f16, bf16, v2i16, v2f16, v2bf16, i1], 32,
  (add SReg_32_XM0_XEXEC, EXEC_LO, EXEC_HI)> {
  let AllocationPriority = 0;
}

} // End GeneratePressureSet = 0

// Register class for all scalar registers (SGPRs + Special Registers)
def SReg_32 : SIRegisterClass<"AMDGPU", [i32, f32, i16, f16, bf16, v2i16, v2f16, v2bf16, i1], 32,
  (add SReg_32_XM0, M0_CLASS)> {
  let AllocationPriority = 0;
  let HasSGPR = 1;
  let BaseClassOrder = 32;
}

let GeneratePressureSet = 0 in {
def SRegOrLds_32 : SIRegisterClass<"AMDGPU", [i32, f32, i16, f16, bf16, v2i16, v2f16, v2bf16], 32,
  (add SReg_32, LDS_DIRECT_CLASS)> {
  let isAllocatable = 0;
  let HasSGPR = 1;
}

def SGPR_64 : SIRegisterClass<"AMDGPU", Reg64Types.types, 32,
                            (add SGPR_64Regs)> {
  let CopyCost = 1;
  let AllocationPriority = 1;
  let HasSGPR = 1;
}

// CCR (call clobbered registers) SGPR 64-bit registers
def CCR_SGPR_64 : SIRegisterClass<"AMDGPU", SGPR_64.RegTypes, 32, (add (trunc SGPR_64, 15))> {
  let CopyCost = SGPR_64.CopyCost;
  let AllocationPriority = SGPR_64.AllocationPriority;
  let HasSGPR = 1;
}

// Call clobbered 64-bit SGPRs for AMDGPU_Gfx CC
def Gfx_CCR_SGPR_64 : SIRegisterClass<"AMDGPU", SGPR_64.RegTypes, 32,
                                (add (trunc (shl SGPR_64, 18), 14))> { // s[36:37]-s[s62:63]
  let CopyCost = SGPR_64.CopyCost;
  let AllocationPriority = SGPR_64.AllocationPriority;
  let HasSGPR = 1;
}

def TTMP_64 : SIRegisterClass<"AMDGPU", [v2i32, i64, f64, v4i16, v4f16, v4bf16], 32,
                            (add TTMP_64Regs)> {
  let isAllocatable = 0;
  let HasSGPR = 1;
}

def SReg_64_XEXEC : SIRegisterClass<"AMDGPU", [v2i32, i64, v2f32, f64, i1, v4i16, v4f16, v4bf16], 32,
  (add SGPR_64, VCC, FLAT_SCR, XNACK_MASK, SGPR_NULL64, SRC_SHARED_BASE,
       SRC_SHARED_LIMIT, SRC_PRIVATE_BASE, SRC_PRIVATE_LIMIT, TTMP_64, TBA, TMA)> {
  let CopyCost = 1;
  let AllocationPriority = 1;
  let HasSGPR = 1;
}

def SReg_64 : SIRegisterClass<"AMDGPU", [v2i32, i64, v2f32, f64, i1, v4i16, v4f16, v4bf16], 32,
  (add SReg_64_XEXEC, EXEC)> {
  let CopyCost = 1;
  let AllocationPriority = 1;
  let HasSGPR = 1;
  let BaseClassOrder = 64;
}

def SReg_1_XEXEC : SIRegisterClass<"AMDGPU", [i1], 32,
  (add SReg_64_XEXEC, SReg_32_XEXEC)> {
  let CopyCost = 1;
  let isAllocatable = 0;
  let HasSGPR = 1;
}

def SReg_1 : SIRegisterClass<"AMDGPU", [i1], 32,
  (add SReg_1_XEXEC, EXEC, EXEC_LO, EXEC_HI)> {
  let CopyCost = 1;
  let isAllocatable = 0;
  let HasSGPR = 1;
}

multiclass SRegClass<int numRegs,
                     list<ValueType> regTypes,
                     SIRegisterTuples regList,
                     SIRegisterTuples ttmpList = regList,
                     int copyCost = !sra(!add(numRegs, 1), 1)> {
  defvar hasTTMP = !ne(regList, ttmpList);
  defvar suffix = !cast<string>(!mul(numRegs, 32));
  defvar sgprName = !strconcat("SGPR_", suffix);
  defvar ttmpName = !strconcat("TTMP_", suffix);

  let AllocationPriority = !sub(numRegs, 1), CopyCost = copyCost, HasSGPR = 1 in {
    def "" # sgprName : SIRegisterClass<"AMDGPU", regTypes, 32, (add regList)> {
    }

    if hasTTMP then {
      def "" # ttmpName : SIRegisterClass<"AMDGPU", regTypes, 32, (add ttmpList)> {
        let isAllocatable = 0;
      }
    }

    def SReg_ # suffix :
      SIRegisterClass<"AMDGPU", regTypes, 32,
                    !con(!dag(add, [!cast<RegisterClass>(sgprName)], ["sgpr"]),
                    !if(hasTTMP,
                        !dag(add, [!cast<RegisterClass>(ttmpName)], ["ttmp"]),
                        (add)))> {
      let isAllocatable = 0;
      let BaseClassOrder = !mul(numRegs, 32);
    }
  }
}

defm "" : SRegClass<3, Reg96Types.types, SGPR_96Regs, TTMP_96Regs>;
defm "" : SRegClass<4, Reg128Types.types, SGPR_128Regs, TTMP_128Regs>;
defm "" : SRegClass<5, [v5i32, v5f32], SGPR_160Regs, TTMP_160Regs>;
defm "" : SRegClass<6, [v6i32, v6f32, v3i64, v3f64], SGPR_192Regs, TTMP_192Regs>;
defm "" : SRegClass<7, [v7i32, v7f32], SGPR_224Regs, TTMP_224Regs>;
defm "" : SRegClass<8, [v8i32, v8f32, v4i64, v4f64, v16i16, v16f16, v16bf16], SGPR_256Regs, TTMP_256Regs>;
defm "" : SRegClass<9, [v9i32, v9f32], SGPR_288Regs, TTMP_288Regs>;
defm "" : SRegClass<10, [v10i32, v10f32], SGPR_320Regs, TTMP_320Regs>;
defm "" : SRegClass<11, [v11i32, v11f32], SGPR_352Regs, TTMP_352Regs>;
defm "" : SRegClass<12, [v12i32, v12f32], SGPR_384Regs, TTMP_384Regs>;

let GlobalPriority = true in {
defm "" : SRegClass<16, [v16i32, v16f32, v8i64, v8f64, v32i16, v32f16, v32bf16], SGPR_512Regs, TTMP_512Regs>;
defm "" : SRegClass<32, [v32i32, v32f32, v16i64, v16f64], SGPR_1024Regs>;
}

def VRegOrLds_32 : SIRegisterClass<"AMDGPU", [i32, f32, i16, f16, bf16, v2i16, v2f16, v2bf16], 32,
                                 (add VGPR_32, LDS_DIRECT_CLASS)> {
  let isAllocatable = 0;
  let HasVGPR = 1;
}

// Register class for all vector registers (VGPRs + Interpolation Registers)
class VRegClassBase<int numRegs, list<ValueType> regTypes, dag regList> :
    SIRegisterClass<"AMDGPU", regTypes, 32, regList> {
  let Size = !mul(numRegs, 32);

  // Requires n v_mov_b32 to copy
  let CopyCost = numRegs;
  let AllocationPriority = !sub(numRegs, 1);
  let Weight = numRegs;
}

// Define a register tuple class, along with one requiring an even
// aligned base register.
multiclass VRegClass<int numRegs, list<ValueType> regTypes, dag regList> {
  let HasVGPR = 1 in {
    // Define the regular class.
    def "" : VRegClassBase<numRegs, regTypes, regList> {
      let BaseClassOrder = !mul(numRegs, 32);
    }

    // Define 2-aligned variant
    def _Align2 : VRegClassBase<numRegs, regTypes, (decimate regList, 2)> {
      // Give aligned class higher priority in base class resolution
      let BaseClassOrder = !sub(!mul(numRegs, 32), 1);
      let RegTupleAlignUnits = 2;
    }
  }
}

defm VReg_64 : VRegClass<2, [i64, f64, v2i32, v2f32, v4f16, v4bf16, v4i16, p0, p1, p4],
                                (add VGPR_64)>;
defm VReg_96 : VRegClass<3, Reg96Types.types, (add VGPR_96)>;
defm VReg_128 : VRegClass<4, Reg128Types.types, (add VGPR_128)>;
defm VReg_160 : VRegClass<5, [v5i32, v5f32], (add VGPR_160)>;

defm VReg_192 : VRegClass<6, [v6i32, v6f32, v3i64, v3f64], (add VGPR_192)>;
defm VReg_224 : VRegClass<7, [v7i32, v7f32], (add VGPR_224)>;
defm VReg_256 : VRegClass<8, [v8i32, v8f32, v4i64, v4f64, v16i16, v16f16, v16bf16], (add VGPR_256)>;
defm VReg_288 : VRegClass<9, [v9i32, v9f32], (add VGPR_288)>;
defm VReg_320 : VRegClass<10, [v10i32, v10f32], (add VGPR_320)>;
defm VReg_352 : VRegClass<11, [v11i32, v11f32], (add VGPR_352)>;
defm VReg_384 : VRegClass<12, [v12i32, v12f32], (add VGPR_384)>;

let GlobalPriority = true in {
defm VReg_512 : VRegClass<16, [v16i32, v16f32, v8i64, v8f64, v32i16, v32f16, v32bf16], (add VGPR_512)>;
defm VReg_1024 : VRegClass<32, [v32i32, v32f32, v16i64, v16f64], (add VGPR_1024)>;
}

multiclass ARegClass<int numRegs, list<ValueType> regTypes, dag regList> {
  let CopyCost = !add(numRegs, numRegs, 1), HasAGPR = 1 in {
    // Define the regular class.
    def "" : VRegClassBase<numRegs, regTypes, regList> {
      let BaseClassOrder = !mul(numRegs, 32);
    }

    // Define 2-aligned variant
    def _Align2 : VRegClassBase<numRegs, regTypes, (decimate regList, 2)> {
      // Give aligned class higher priority in base class resolution
      let BaseClassOrder = !sub(!mul(numRegs, 32), 1);
      let RegTupleAlignUnits = 2;
    }
  }
}

defm AReg_64 : ARegClass<2, [i64, f64, v2i32, v2f32, v4f16, v4i16],
                        (add AGPR_64)>;
defm AReg_96 : ARegClass<3, [v3i32, v3f32], (add AGPR_96)>;
defm AReg_128 : ARegClass<4, [v4i32, v4f32, v2i64, v2f64, v8i16, v8f16, v8bf16], (add AGPR_128)>;
defm AReg_160 : ARegClass<5, [v5i32, v5f32], (add AGPR_160)>;
defm AReg_192 : ARegClass<6, [v6i32, v6f32, v3i64, v3f64], (add AGPR_192)>;
defm AReg_224 : ARegClass<7, [v7i32, v7f32], (add AGPR_224)>;
defm AReg_256 : ARegClass<8, [v8i32, v8f32, v4i64, v4f64], (add AGPR_256)>;
defm AReg_288 : ARegClass<9, [v9i32, v9f32], (add AGPR_288)>;
defm AReg_320 : ARegClass<10, [v10i32, v10f32], (add AGPR_320)>;
defm AReg_352 : ARegClass<11, [v11i32, v11f32], (add AGPR_352)>;
defm AReg_384 : ARegClass<12, [v12i32, v12f32], (add AGPR_384)>;

let GlobalPriority = true in {
defm AReg_512 : ARegClass<16, [v16i32, v16f32, v8i64, v8f64], (add AGPR_512)>;
defm AReg_1024 : ARegClass<32, [v32i32, v32f32, v16i64, v16f64], (add AGPR_1024)>;
}

} // End GeneratePressureSet = 0

let GeneratePressureSet = 0 in {
// No register should ever be allocated using VReg_1. This is a hack for
// SelectionDAG that should always be lowered by SILowerI1Copies.  TableGen
// sorts register classes based on the number of registers in them so this is
// sorted to the end and not preferred over VGPR_32.
def VReg_1 : SIRegisterClass<"AMDGPU", [i1], 32, (add)> {
  let Size = 1;
  let HasVGPR = 1;
}

def VS_16 : SIRegisterClass<"AMDGPU", Reg16Types.types, 16,
                          (add VGPR_16, SReg_32, LDS_DIRECT_CLASS)> {
  let isAllocatable = 0;
  let HasVGPR = 1;
}

def VS_16_Lo128 : SIRegisterClass<"AMDGPU", Reg16Types.types, 16,
                          (add VGPR_16_Lo128, SReg_32, LDS_DIRECT_CLASS)> {
  let isAllocatable = 0;
  let HasVGPR = 1;
}

def VS_32 : SIRegisterClass<"AMDGPU", [i32, f32, i16, f16, bf16, v2i16, v2f16, v2bf16], 32,
                          (add VGPR_32, SReg_32, LDS_DIRECT_CLASS)> {
  let isAllocatable = 0;
  let HasVGPR = 1;
  let HasSGPR = 1;
}

def VS_32_Lo128 : SIRegisterClass<"AMDGPU", [i32, f32, i16, f16, bf16, v2i16, v2f16, v2bf16], 32,
                          (add VGPR_32_Lo128, SReg_32, LDS_DIRECT_CLASS)> {
  let isAllocatable = 0;
  let HasVGPR = 1;
  let HasSGPR = 1;
}

def VS_64 : SIRegisterClass<"AMDGPU", VReg_64.RegTypes, 32, (add VReg_64, SReg_64)> {
  let isAllocatable = 0;
  let HasVGPR = 1;
  let HasSGPR = 1;
}

def AV_32 : SIRegisterClass<"AMDGPU", VGPR_32.RegTypes, 32, (add VGPR_32, AGPR_32)> {
  let HasVGPR = 1;
  let HasAGPR = 1;
}
} // End GeneratePressureSet = 0

// Define a register tuple class, along with one requiring an even
// aligned base register.
multiclass AVRegClass<int numRegs, list<ValueType> regTypes,
                      dag vregList,  dag aregList> {
  let HasVGPR = 1, HasAGPR = 1 in {
    // Define the regular class.
    def "" : VRegClassBase<numRegs, regTypes, (add vregList, aregList)>;

    // Define 2-aligned variant
    def _Align2 : VRegClassBase<numRegs, regTypes,
                                (add (decimate vregList, 2),
                                     (decimate aregList, 2))> {
      let RegTupleAlignUnits = 2;
    }
  }
}

defm AV_64 : AVRegClass<2, VReg_64.RegTypes, (add VGPR_64), (add AGPR_64)>;
defm AV_96 : AVRegClass<3, VReg_96.RegTypes, (add VGPR_96), (add AGPR_96)>;
defm AV_128 : AVRegClass<4, VReg_128.RegTypes, (add VGPR_128), (add AGPR_128)>;
defm AV_160 : AVRegClass<5, VReg_160.RegTypes, (add VGPR_160), (add AGPR_160)>;
defm AV_192 : AVRegClass<6, VReg_192.RegTypes, (add VGPR_192), (add AGPR_192)>;
defm AV_224 : AVRegClass<7, VReg_224.RegTypes, (add VGPR_224), (add AGPR_224)>;
defm AV_256 : AVRegClass<8, VReg_256.RegTypes, (add VGPR_256), (add AGPR_256)>;
defm AV_288 : AVRegClass<9, VReg_288.RegTypes, (add VGPR_288), (add AGPR_288)>;
defm AV_320 : AVRegClass<10, VReg_320.RegTypes, (add VGPR_320), (add AGPR_320)>;
defm AV_352 : AVRegClass<11, VReg_352.RegTypes, (add VGPR_352), (add AGPR_352)>;
defm AV_384 : AVRegClass<12, VReg_384.RegTypes, (add VGPR_384), (add AGPR_384)>;

let GlobalPriority = true in {
defm AV_512 : AVRegClass<16, VReg_512.RegTypes, (add VGPR_512), (add AGPR_512)>;
defm AV_1024 : AVRegClass<32, VReg_1024.RegTypes, (add VGPR_1024), (add AGPR_1024)>;
}

//===----------------------------------------------------------------------===//
//  Register operands
//===----------------------------------------------------------------------===//

class RegImmMatcher<string name> : AsmOperandClass {
  let Name = name;
  let RenderMethod = "addRegOrImmOperands";
}

class RegOrImmOperand <RegisterClass RegClass, string OperandTypeName>
  : RegisterOperand<RegClass> {
    let OperandNamespace = "AMDGPU";
    let OperandType = OperandTypeName;
    let ParserMatchClass = RegImmMatcher<!subst("_Deferred", "", NAME)>;
}

// Should be in sync with the OperandSemantics defined in SIDefines.h
def OperandSemantics {
  int INT = 0;
  int FP16 = 1;
  int BF16 = 2;
  int FP32 = 3;
  int FP64 = 4;
}

//===----------------------------------------------------------------------===//
//  SSrc_* Operands with an SGPR or a 32-bit immediate
//===----------------------------------------------------------------------===//

class SrcRegOrImm9<RegisterClass regClass, string opWidth, string operandType,
                   int immWidth, int OperandSemantics>
    : RegOrImmOperand<regClass, operandType> {
  let DecoderMethod = "decodeSrcRegOrImm9<AMDGPUDisassembler::" # opWidth #
                      ", " # immWidth # ", " # OperandSemantics # ">";
}

def SSrc_b16 : SrcRegOrImm9 <SReg_32, "OPW32", "OPERAND_REG_IMM_INT16", 16, OperandSemantics.INT>;
def SSrc_bf16: SrcRegOrImm9 <SReg_32, "OPW32", "OPERAND_REG_IMM_BF16", 16, OperandSemantics.BF16>;
def SSrc_f16 : SrcRegOrImm9 <SReg_32, "OPW32", "OPERAND_REG_IMM_FP16", 16, OperandSemantics.FP16>;
def SSrc_b32 : SrcRegOrImm9 <SReg_32, "OPW32", "OPERAND_REG_IMM_INT32", 32, OperandSemantics.INT>;
def SSrc_f32 : SrcRegOrImm9 <SReg_32, "OPW32", "OPERAND_REG_IMM_FP32", 32, OperandSemantics.FP32>;
def SSrc_b64 : SrcRegOrImm9 <SReg_64, "OPW64", "OPERAND_REG_IMM_INT64", 64, OperandSemantics.INT>;

def SSrcOrLds_b32 : SrcRegOrImm9 <SRegOrLds_32, "OPW32", "OPERAND_REG_IMM_INT32", 32, OperandSemantics.INT>;

//===----------------------------------------------------------------------===//
//  SSrc_32_Deferred Operands with an SGPR or a 32-bit immediate for use with
//  FMAMK/FMAAK
//===----------------------------------------------------------------------===//

class SrcRegOrImmDeferred9<RegisterClass regClass, string opWidth,
                           string operandType, int immWidth, int OperandSemantics>
    : RegOrImmOperand<regClass, operandType> {
  let DecoderMethod = "decodeSrcRegOrImmDeferred9<AMDGPUDisassembler::" #
                      opWidth # ", " # immWidth # ", " # OperandSemantics # ">";
}

def SSrc_f32_Deferred : SrcRegOrImmDeferred9<SReg_32, "OPW32", "OPERAND_REG_IMM_FP32_DEFERRED", 32, OperandSemantics.FP32>;

//===----------------------------------------------------------------------===//
//  SCSrc_* Operands with an SGPR or a inline constant
//===----------------------------------------------------------------------===//

def SCSrc_b32 : SrcRegOrImm9 <SReg_32, "OPW32", "OPERAND_REG_INLINE_C_INT32", 32, OperandSemantics.INT>;
def SCSrc_b64 : SrcRegOrImm9 <SReg_64, "OPW64", "OPERAND_REG_INLINE_C_INT64", 64, OperandSemantics.INT>;

//===----------------------------------------------------------------------===//
//  VSrc_* Operands with an SGPR, VGPR or a 32-bit immediate
//===----------------------------------------------------------------------===//

// The current and temporary future default used case for VOP3.
def VSrc_b16 : SrcRegOrImm9 <VS_32, "OPW32", "OPERAND_REG_IMM_INT16", 16, OperandSemantics.INT>;
def VSrc_bf16 : SrcRegOrImm9 <VS_32, "OPW32", "OPERAND_REG_IMM_BF16", 16, OperandSemantics.BF16>;
def VSrc_f16 : SrcRegOrImm9 <VS_32, "OPW32", "OPERAND_REG_IMM_FP16", 16, OperandSemantics.FP16>;

// True16 VOP3 operands.
def VSrcT_b16 : RegOrImmOperand <VS_16, "OPERAND_REG_IMM_INT16"> {
  let EncoderMethod = "getMachineOpValueT16";
  let DecoderMethod = "decodeOperand_VSrcT16";
}
def VSrcT_bf16 : RegOrImmOperand <VS_16, "OPERAND_REG_IMM_BF16"> {
  let EncoderMethod = "getMachineOpValueT16";
  let DecoderMethod = "decodeOperand_VSrcT16";
}
def VSrcT_f16 : RegOrImmOperand <VS_16, "OPERAND_REG_IMM_FP16"> {
  let EncoderMethod = "getMachineOpValueT16";
  let DecoderMethod = "decodeOperand_VSrcT16";
}

// True16 VOP1/2/C operands.
def VSrcT_b16_Lo128 : RegOrImmOperand <VS_16_Lo128, "OPERAND_REG_IMM_INT16"> {
  let EncoderMethod = "getMachineOpValueT16Lo128";
  let DecoderMethod = "decodeOperand_VSrcT16_Lo128";
}
def VSrcT_bf16_Lo128 : RegOrImmOperand <VS_16_Lo128, "OPERAND_REG_IMM_BF16"> {
  let EncoderMethod = "getMachineOpValueT16Lo128";
  let DecoderMethod = "decodeOperand_VSrcT16_Lo128";
}
def VSrcT_f16_Lo128 : RegOrImmOperand <VS_16_Lo128, "OPERAND_REG_IMM_FP16"> {
  let EncoderMethod = "getMachineOpValueT16Lo128";
  let DecoderMethod = "decodeOperand_VSrcT16_Lo128";
}

// The current and temporary future default used case for fake VOP1/2/C.
// For VOP1,2,C True16 instructions. _Lo128 use first 128 32-bit VGPRs only.
def VSrcFake16_b16_Lo128 : SrcRegOrImm9 <VS_32_Lo128, "OPW16", "OPERAND_REG_IMM_INT16", 16, OperandSemantics.INT>;
def VSrcFake16_bf16_Lo128 : SrcRegOrImm9 <VS_32_Lo128, "OPW16", "OPERAND_REG_IMM_BF16", 16, OperandSemantics.BF16>;
def VSrcFake16_f16_Lo128 : SrcRegOrImm9 <VS_32_Lo128, "OPW16", "OPERAND_REG_IMM_FP16", 16, OperandSemantics.FP16>;

def VSrc_b32 : SrcRegOrImm9 <VS_32, "OPW32", "OPERAND_REG_IMM_INT32", 32, OperandSemantics.INT>;
def VSrc_f32 : SrcRegOrImm9 <VS_32, "OPW32", "OPERAND_REG_IMM_FP32", 32, OperandSemantics.FP32>;
def VSrc_v2b16 : SrcRegOrImm9 <VS_32, "OPW32", "OPERAND_REG_IMM_V2INT16", 32, OperandSemantics.INT>;
def VSrc_v2bf16 : SrcRegOrImm9 <VS_32, "OPW32", "OPERAND_REG_IMM_V2BF16", 16, OperandSemantics.BF16>;
def VSrc_v2f16 : SrcRegOrImm9 <VS_32, "OPW32", "OPERAND_REG_IMM_V2FP16", 16, OperandSemantics.FP16>;
def VSrc_b64 : SrcRegOrImm9 <VS_64, "OPW64", "OPERAND_REG_IMM_INT64", 64, OperandSemantics.INT>;
def VSrc_f64 : SrcRegOrImm9 <VS_64, "OPW64", "OPERAND_REG_IMM_FP64", 64, OperandSemantics.FP64> {
  let DecoderMethod = "decodeOperand_VSrc_f64";
}
def VSrc_v2b32 : SrcRegOrImm9 <VS_64, "OPW64", "OPERAND_REG_IMM_V2INT32", 32, OperandSemantics.INT>;
def VSrc_v2f32 : SrcRegOrImm9 <VS_64, "OPW64", "OPERAND_REG_IMM_V2FP32", 32, OperandSemantics.FP32>;

//===----------------------------------------------------------------------===//
//  VSrc_*_Deferred Operands with an SGPR, VGPR or a 32-bit immediate for use
//  with FMAMK/FMAAK
//===----------------------------------------------------------------------===//

def VSrc_bf16_Deferred : SrcRegOrImmDeferred9<VS_32, "OPW16", "OPERAND_REG_IMM_BF16_DEFERRED", 16, OperandSemantics.BF16>;
def VSrc_f16_Deferred : SrcRegOrImmDeferred9<VS_32, "OPW16", "OPERAND_REG_IMM_FP16_DEFERRED", 16, OperandSemantics.FP16>;
def VSrc_f32_Deferred : SrcRegOrImmDeferred9<VS_32, "OPW32", "OPERAND_REG_IMM_FP32_DEFERRED", 32, OperandSemantics.FP32>;

def VSrcFake16_bf16_Lo128_Deferred
  : SrcRegOrImmDeferred9<VS_32_Lo128, "OPW16", "OPERAND_REG_IMM_BF16_DEFERRED", 16, OperandSemantics.BF16>;
def VSrcFake16_f16_Lo128_Deferred
  : SrcRegOrImmDeferred9<VS_32_Lo128, "OPW16", "OPERAND_REG_IMM_FP16_DEFERRED", 16, OperandSemantics.FP16>;

//===----------------------------------------------------------------------===//
//  VRegSrc_* Operands with a VGPR
//===----------------------------------------------------------------------===//

// This is for operands with the enum(9), VSrc encoding restriction,
// but only allows VGPRs.
class SrcReg9<RegisterClass regClass, string width> : RegisterOperand<regClass> {
  let DecoderMethod = "decodeSrcReg9<AMDGPUDisassembler::" # width # ">";
}

def VRegSrc_32 : SrcReg9<VGPR_32, "OPW32">;
def VRegSrc_64 : SrcReg9<VReg_64, "OPW64">;
def VRegSrc_96 : SrcReg9<VReg_96, "OPW96">;
def VRegSrc_128: SrcReg9<VReg_128, "OPW128">;
def VRegSrc_256: SrcReg9<VReg_256, "OPW256">;
def VRegOrLdsSrc_32 : SrcReg9<VRegOrLds_32, "OPW32">;

//===----------------------------------------------------------------------===//
// VGPRSrc_*
//===----------------------------------------------------------------------===//

// An 8-bit RegisterOperand wrapper for a VGPR
def VGPRSrc_32 : RegisterOperand<VGPR_32> {
  let DecoderMethod = "DecodeVGPR_32RegisterClass";
}
def VGPRSrc_32_Lo128 : RegisterOperand<VGPR_32_Lo128> {
  let DecoderMethod = "DecodeVGPR_32RegisterClass";
}

def VGPRSrc_96 : RegisterOperand<VReg_96> {
  let DecoderMethod = "DecodeVReg_96RegisterClass";
}

def VGPRSrc_16_Lo128 : RegisterOperand<VGPR_16_Lo128> {
  let DecoderMethod = "DecodeVGPR_16_Lo128RegisterClass";
  let EncoderMethod = "getMachineOpValueT16Lo128";
}

// True 16 operands.
def VGPRSrc_16 : RegisterOperand<VGPR_16> {
  let DecoderMethod = "DecodeVGPR_16RegisterClass";
  let EncoderMethod = "getMachineOpValueT16";
}

//===----------------------------------------------------------------------===//
//  ASrc_* Operands with an AccVGPR
//===----------------------------------------------------------------------===//

class AVOperand<RegisterClass regClass, string decoder, string width>
    : RegisterOperand<regClass> {
  let DecoderMethod = decoder # "<AMDGPUDisassembler::" # width # ">";
  let EncoderMethod = "getAVOperandEncoding";
}

def ARegSrc_32 : AVOperand<AGPR_32, "decodeSrcA9", "OPW32">;

//===----------------------------------------------------------------------===//
//  VCSrc_* Operands with an SGPR, VGPR or an inline constant
//===----------------------------------------------------------------------===//

def VCSrc_b16 : SrcRegOrImm9 <VS_32, "OPW32", "OPERAND_REG_INLINE_C_INT16", 16, OperandSemantics.INT>;
def VCSrc_bf16 : SrcRegOrImm9 <VS_32, "OPW32", "OPERAND_REG_INLINE_C_BF16", 16, OperandSemantics.BF16>;
def VCSrc_f16 : SrcRegOrImm9 <VS_32, "OPW32", "OPERAND_REG_INLINE_C_FP16", 16, OperandSemantics.FP16>;
def VCSrc_b32 : SrcRegOrImm9 <VS_32, "OPW32", "OPERAND_REG_INLINE_C_INT32", 32, OperandSemantics.INT>;
def VCSrc_f32 : SrcRegOrImm9 <VS_32, "OPW32", "OPERAND_REG_INLINE_C_FP32", 32, OperandSemantics.FP32>;
def VCSrc_v2b16 : SrcRegOrImm9 <VS_32, "OPW32", "OPERAND_REG_INLINE_C_V2INT16", 32, OperandSemantics.INT>;
def VCSrc_v2bf16: SrcRegOrImm9 <VS_32, "OPW32", "OPERAND_REG_INLINE_C_V2BF16", 16, OperandSemantics.BF16>;
def VCSrc_v2f16 : SrcRegOrImm9 <VS_32, "OPW32", "OPERAND_REG_INLINE_C_V2FP16", 16, OperandSemantics.FP16>;

//===----------------------------------------------------------------------===//
//  VISrc_* Operands with a VGPR or an inline constant
//===----------------------------------------------------------------------===//

def VISrc_64_bf16 : SrcRegOrImm9 <VReg_64, "OPW64", "OPERAND_REG_INLINE_C_BF16", 16, OperandSemantics.BF16>;
def VISrc_64_f16 : SrcRegOrImm9 <VReg_64, "OPW64", "OPERAND_REG_INLINE_C_FP16", 16, OperandSemantics.FP16>;
def VISrc_64_b32 : SrcRegOrImm9 <VReg_64, "OPW64", "OPERAND_REG_INLINE_C_INT32", 32, OperandSemantics.INT>;
def VISrc_64_f64 : SrcRegOrImm9 <VReg_64, "OPW64", "OPERAND_REG_INLINE_C_FP64", 64, OperandSemantics.FP64>;
def VISrc_128_bf16 : SrcRegOrImm9 <VReg_128, "OPW128", "OPERAND_REG_INLINE_C_BF16", 16, OperandSemantics.BF16>;
def VISrc_128_f16 : SrcRegOrImm9 <VReg_128, "OPW128", "OPERAND_REG_INLINE_C_FP16", 16, OperandSemantics.FP16>;
def VISrc_128_b32 : SrcRegOrImm9 <VReg_128, "OPW128", "OPERAND_REG_INLINE_C_INT32", 32, OperandSemantics.INT>;
def VISrc_128_f32 : SrcRegOrImm9 <VReg_128, "OPW128", "OPERAND_REG_INLINE_C_FP32", 32, OperandSemantics.FP32>;
def VISrc_256_b32 : SrcRegOrImm9 <VReg_256, "OPW256", "OPERAND_REG_INLINE_C_INT32", 32, OperandSemantics.INT>;
def VISrc_256_f32 : SrcRegOrImm9 <VReg_256, "OPW256", "OPERAND_REG_INLINE_C_FP32", 32, OperandSemantics.FP32>;
def VISrc_256_f64 : SrcRegOrImm9 <VReg_256, "OPW256", "OPERAND_REG_INLINE_C_FP64", 64, OperandSemantics.FP64>;
def VISrc_512_b32 : SrcRegOrImm9 <VReg_512, "OPW512", "OPERAND_REG_INLINE_C_INT32", 32, OperandSemantics.INT>;
def VISrc_512_f32 : SrcRegOrImm9 <VReg_512, "OPW512", "OPERAND_REG_INLINE_C_FP32", 32, OperandSemantics.FP32>;
def VISrc_1024_b32 : SrcRegOrImm9 <VReg_1024, "OPW1024", "OPERAND_REG_INLINE_C_INT32", 32, OperandSemantics.INT>;
def VISrc_1024_f32 : SrcRegOrImm9 <VReg_1024, "OPW1024", "OPERAND_REG_INLINE_C_FP32", 32, OperandSemantics.FP32>;

//===----------------------------------------------------------------------===//
//  AVSrc_*, AVDst_*, AVLdSt_* Operands with an AGPR or VGPR
//===----------------------------------------------------------------------===//

class AVSrcOperand<RegisterClass regClass, string width>
  : AVOperand<regClass, "decodeSrcAV10", width>;

def AVSrc_32 : AVSrcOperand<AV_32, "OPW32">;
def AVSrc_64 : AVSrcOperand<AV_64, "OPW64">;
def AVSrc_128 : AVSrcOperand<AV_128, "OPW128">;

class AVDstOperand<RegisterClass regClass, string width>
  : AVOperand<regClass, "decodeAV10", width>;

def AVDst_128 : AVDstOperand<AV_128, "OPW128">;
def AVDst_512 : AVDstOperand<AV_512, "OPW512">;

class AVLdStOperand<RegisterClass regClass, string width>
  : AVOperand<regClass, "decodeAVLdSt", width>;

def AVLdSt_32 : AVLdStOperand<AV_32, "OPW32">;
def AVLdSt_64 : AVLdStOperand<AV_64, "OPW64">;
def AVLdSt_96 : AVLdStOperand<AV_96, "OPW96">;
def AVLdSt_128 : AVLdStOperand<AV_128, "OPW128">;
def AVLdSt_160 : AVLdStOperand<AV_160, "OPW160">;
def AVLdSt_1024 : AVLdStOperand<AV_1024, "OPW1024">;

//===----------------------------------------------------------------------===//
//  ACSrc_* Operands with an AGPR or an inline constant
//===----------------------------------------------------------------------===//

class SrcRegOrImmA9<RegisterClass regClass, string opWidth, string operandType,
                    int immWidth, int OperandSemantics>
    : RegOrImmOperand<regClass, operandType> {
  let DecoderMethod = "decodeSrcRegOrImmA9<AMDGPUDisassembler::" # opWidth #
                      ", " # immWidth # ", " # OperandSemantics # ">";
}

def AISrc_64_f64 : SrcRegOrImmA9 <AReg_64, "OPW64", "OPERAND_REG_INLINE_AC_FP64", 64, OperandSemantics.FP64>;
def AISrc_128_f32 : SrcRegOrImmA9 <AReg_128, "OPW128", "OPERAND_REG_INLINE_AC_FP32", 32, OperandSemantics.FP32>;
def AISrc_128_b32 : SrcRegOrImmA9 <AReg_128, "OPW128", "OPERAND_REG_INLINE_AC_INT32", 32, OperandSemantics.INT>;
def AISrc_256_f64 : SrcRegOrImmA9 <AReg_256, "OPW256", "OPERAND_REG_INLINE_AC_FP64", 64, OperandSemantics.FP64>;
def AISrc_512_f32 : SrcRegOrImmA9 <AReg_512, "OPW512", "OPERAND_REG_INLINE_AC_FP32", 32, OperandSemantics.FP32>;
def AISrc_512_b32 : SrcRegOrImmA9 <AReg_512, "OPW512", "OPERAND_REG_INLINE_AC_INT32", 32, OperandSemantics.INT>;
def AISrc_1024_f32 : SrcRegOrImmA9 <AReg_1024, "OPW1024", "OPERAND_REG_INLINE_AC_FP32", 32, OperandSemantics.FP32>;
def AISrc_1024_b32 : SrcRegOrImmA9 <AReg_1024, "OPW1024", "OPERAND_REG_INLINE_AC_INT32", 32, OperandSemantics.INT>;

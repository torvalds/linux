//===-- X86InstrFormats.td - X86 Instruction Formats -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// X86 Instruction Format Definitions.
//

// Format specifies the encoding used by the instruction.  This is part of the
// ad-hoc solution used to emit machine instruction encodings by our machine
// code emitter.
class Format<bits<7> val> {
  bits<7> Value = val;
}

def Pseudo        : Format<0>;
def RawFrm        : Format<1>;
def AddRegFrm     : Format<2>;
def RawFrmMemOffs : Format<3>;
def RawFrmSrc     : Format<4>;
def RawFrmDst     : Format<5>;
def RawFrmDstSrc  : Format<6>;
def RawFrmImm8    : Format<7>;
def RawFrmImm16   : Format<8>;
def AddCCFrm      : Format<9>;
def PrefixByte    : Format<10>;
def MRMDestRegCC  : Format<18>;
def MRMDestMemCC  : Format<19>;
def MRMDestMem4VOp3CC : Format<20>;
def MRMr0          : Format<21>;
def MRMSrcMemFSIB  : Format<22>;
def MRMDestMemFSIB : Format<23>;
def MRMDestMem     : Format<24>;
def MRMSrcMem      : Format<25>;
def MRMSrcMem4VOp3 : Format<26>;
def MRMSrcMemOp4   : Format<27>;
def MRMSrcMemCC    : Format<28>;
def MRMXmCC: Format<30>;
def MRMXm  : Format<31>;
def MRM0m  : Format<32>;  def MRM1m  : Format<33>;  def MRM2m  : Format<34>;
def MRM3m  : Format<35>;  def MRM4m  : Format<36>;  def MRM5m  : Format<37>;
def MRM6m  : Format<38>;  def MRM7m  : Format<39>;
def MRMDestReg     : Format<40>;
def MRMSrcReg      : Format<41>;
def MRMSrcReg4VOp3 : Format<42>;
def MRMSrcRegOp4   : Format<43>;
def MRMSrcRegCC    : Format<44>;
def MRMXrCC: Format<46>;
def MRMXr  : Format<47>;
def MRM0r  : Format<48>;  def MRM1r  : Format<49>;  def MRM2r  : Format<50>;
def MRM3r  : Format<51>;  def MRM4r  : Format<52>;  def MRM5r  : Format<53>;
def MRM6r  : Format<54>;  def MRM7r  : Format<55>;
def MRM0X  : Format<56>;  def MRM1X  : Format<57>;  def MRM2X  : Format<58>;
def MRM3X  : Format<59>;  def MRM4X  : Format<60>;  def MRM5X  : Format<61>;
def MRM6X  : Format<62>;  def MRM7X  : Format<63>;
def MRM_C0 : Format<64>;  def MRM_C1 : Format<65>;  def MRM_C2 : Format<66>;
def MRM_C3 : Format<67>;  def MRM_C4 : Format<68>;  def MRM_C5 : Format<69>;
def MRM_C6 : Format<70>;  def MRM_C7 : Format<71>;  def MRM_C8 : Format<72>;
def MRM_C9 : Format<73>;  def MRM_CA : Format<74>;  def MRM_CB : Format<75>;
def MRM_CC : Format<76>;  def MRM_CD : Format<77>;  def MRM_CE : Format<78>;
def MRM_CF : Format<79>;  def MRM_D0 : Format<80>;  def MRM_D1 : Format<81>;
def MRM_D2 : Format<82>;  def MRM_D3 : Format<83>;  def MRM_D4 : Format<84>;
def MRM_D5 : Format<85>;  def MRM_D6 : Format<86>;  def MRM_D7 : Format<87>;
def MRM_D8 : Format<88>;  def MRM_D9 : Format<89>;  def MRM_DA : Format<90>;
def MRM_DB : Format<91>;  def MRM_DC : Format<92>;  def MRM_DD : Format<93>;
def MRM_DE : Format<94>;  def MRM_DF : Format<95>;  def MRM_E0 : Format<96>;
def MRM_E1 : Format<97>;  def MRM_E2 : Format<98>;  def MRM_E3 : Format<99>;
def MRM_E4 : Format<100>; def MRM_E5 : Format<101>; def MRM_E6 : Format<102>;
def MRM_E7 : Format<103>; def MRM_E8 : Format<104>; def MRM_E9 : Format<105>;
def MRM_EA : Format<106>; def MRM_EB : Format<107>; def MRM_EC : Format<108>;
def MRM_ED : Format<109>; def MRM_EE : Format<110>; def MRM_EF : Format<111>;
def MRM_F0 : Format<112>; def MRM_F1 : Format<113>; def MRM_F2 : Format<114>;
def MRM_F3 : Format<115>; def MRM_F4 : Format<116>; def MRM_F5 : Format<117>;
def MRM_F6 : Format<118>; def MRM_F7 : Format<119>; def MRM_F8 : Format<120>;
def MRM_F9 : Format<121>; def MRM_FA : Format<122>; def MRM_FB : Format<123>;
def MRM_FC : Format<124>; def MRM_FD : Format<125>; def MRM_FE : Format<126>;
def MRM_FF : Format<127>;

// ImmType - This specifies the immediate type used by an instruction. This is
// part of the ad-hoc solution used to emit machine instruction encodings by our
// machine code emitter.
class ImmType<bits<4> val> {
  bits<4> Value = val;
}
def NoImm      : ImmType<0>;
def Imm8       : ImmType<1>;
def Imm8PCRel  : ImmType<2>;
def Imm8Reg    : ImmType<3>; // Register encoded in [7:4].
def Imm16      : ImmType<4>;
def Imm16PCRel : ImmType<5>;
def Imm32      : ImmType<6>;
def Imm32PCRel : ImmType<7>;
def Imm32S     : ImmType<8>;
def Imm64      : ImmType<9>;

// FPFormat - This specifies what form this FP instruction has.  This is used by
// the Floating-Point stackifier pass.
class FPFormat<bits<3> val> {
  bits<3> Value = val;
}
def NotFP      : FPFormat<0>;
def ZeroArgFP  : FPFormat<1>;
def OneArgFP   : FPFormat<2>;
def OneArgFPRW : FPFormat<3>;
def TwoArgFP   : FPFormat<4>;
def CompareFP  : FPFormat<5>;
def CondMovFP  : FPFormat<6>;
def SpecialFP  : FPFormat<7>;

// Class specifying the SSE execution domain, used by the SSEDomainFix pass.
// Keep in sync with tables in X86InstrInfo.cpp.
class Domain<bits<2> val> {
  bits<2> Value = val;
}
def GenericDomain   : Domain<0>;
def SSEPackedSingle : Domain<1>;
def SSEPackedDouble : Domain<2>;
def SSEPackedInt    : Domain<3>;

// Class specifying the vector form of the decompressed
// displacement of 8-bit.
class CD8VForm<bits<3> val> {
  bits<3> Value = val;
}
def CD8VF  : CD8VForm<0>;  // v := VL
def CD8VH  : CD8VForm<1>;  // v := VL/2
def CD8VQ  : CD8VForm<2>;  // v := VL/4
def CD8VO  : CD8VForm<3>;  // v := VL/8
// The tuple (subvector) forms.
def CD8VT1 : CD8VForm<4>;  // v := 1
def CD8VT2 : CD8VForm<5>;  // v := 2
def CD8VT4 : CD8VForm<6>;  // v := 4
def CD8VT8 : CD8VForm<7>;  // v := 8

// Class specifying the prefix used an opcode extension.
class Prefix<bits<3> val> {
  bits<3> Value = val;
}
def NoPrfx : Prefix<0>;
def PD     : Prefix<1>;
def XS     : Prefix<2>;
def XD     : Prefix<3>;
def PS     : Prefix<4>; // Similar to NoPrfx, but disassembler uses this to know
                        // that other instructions with this opcode use PD/XS/XD
                        // and if any of those is not supported they shouldn't
                        // decode to this instruction. e.g. ANDSS/ANDSD don't
                        // exist, but the 0xf2/0xf3 encoding shouldn't
                        // disable to ANDPS.

// Class specifying the opcode map.
class Map<bits<4> val> {
  bits<4> Value = val;
}
def OB        : Map<0>;
def TB        : Map<1>;
def T8        : Map<2>;
def TA        : Map<3>;
def XOP8      : Map<4>;
def XOP9      : Map<5>;
def XOPA      : Map<6>;
def ThreeDNow : Map<7>;
def T_MAP4    : Map<8>;
def T_MAP5    : Map<9>;
def T_MAP6    : Map<10>;
def T_MAP7    : Map<11>;

// Class specifying the encoding
class Encoding<bits<2> val> {
  bits<2> Value = val;
}
def EncNormal : Encoding<0>;
def EncVEX    : Encoding<1>;
def EncXOP    : Encoding<2>;
def EncEVEX   : Encoding<3>;

// Operand size for encodings that change based on mode.
class OperandSize<bits<2> val> {
  bits<2> Value = val;
}
def OpSizeFixed  : OperandSize<0>; // Never needs a 0x66 prefix.
def OpSize16     : OperandSize<1>; // Needs 0x66 prefix in 32/64-bit mode.
def OpSize32     : OperandSize<2>; // Needs 0x66 prefix in 16-bit mode.

// Address size for encodings that change based on mode.
class AddressSize<bits<2> val> {
  bits<2> Value = val;
}
def AdSizeX  : AddressSize<0>; // Address size determined using addr operand.
def AdSize16 : AddressSize<1>; // Encodes a 16-bit address.
def AdSize32 : AddressSize<2>; // Encodes a 32-bit address.
def AdSize64 : AddressSize<3>; // Encodes a 64-bit address.

// Force the instruction to use REX2/VEX/EVEX encoding.
class ExplicitOpPrefix<bits<2> val> {
  bits<2> Value = val;
}
def NoExplicitOpPrefix : ExplicitOpPrefix<0>;
def ExplicitREX2       : ExplicitOpPrefix<1>;
def ExplicitVEX        : ExplicitOpPrefix<2>;
def ExplicitEVEX       : ExplicitOpPrefix<3>;

class X86Inst<bits<8> opcod, Format f, ImmType i, dag outs, dag ins,
              string AsmStr, Domain d = GenericDomain>
  : Instruction {
  let Namespace = "X86";

  bits<8> Opcode = opcod;
  Format Form = f;
  bits<7> FormBits = Form.Value;
  ImmType ImmT = i;

  dag OutOperandList = outs;
  dag InOperandList = ins;
  string AsmString = AsmStr;

  // If this is a pseudo instruction, mark it isCodeGenOnly.
  let isCodeGenOnly = !eq(!cast<string>(f), "Pseudo");

  let HasPositionOrder = 1;

  //
  // Attributes specific to X86 instructions...
  //
  bit ForceDisassemble = 0; // Force instruction to disassemble even though it's
                            // isCodeGenonly. Needed to hide an ambiguous
                            // AsmString from the parser, but still disassemble.

  OperandSize OpSize = OpSizeFixed; // Does this instruction's encoding change
                                    // based on operand size of the mode?
  bits<2> OpSizeBits = OpSize.Value;
  AddressSize AdSize = AdSizeX; // Does this instruction's encoding change
                                // based on address size of the mode?
  bits<2> AdSizeBits = AdSize.Value;

  Encoding OpEnc = EncNormal; // Encoding used by this instruction
  // Which prefix byte does this inst have?
  Prefix OpPrefix = !if(!eq(OpEnc, EncNormal), NoPrfx, PS);
  bits<3> OpPrefixBits = OpPrefix.Value;
  Map OpMap = OB;           // Which opcode map does this inst have?
  bits<4> OpMapBits = OpMap.Value;
  bit hasREX_W  = 0;  // Does this inst require the REX.W prefix?
  FPFormat FPForm = NotFP;  // What flavor of FP instruction is this?
  bit hasLockPrefix = 0;    // Does this inst have a 0xF0 prefix?
  Domain ExeDomain = d;
  bit hasREPPrefix = 0;     // Does this inst have a REP prefix?
  bits<2> OpEncBits = OpEnc.Value;
  bit IgnoresW = 0;         // Does this inst ignore REX_W field?
  bit hasVEX_4V = 0;        // Does this inst require the VEX.VVVV field?
  bit hasVEX_L = 0;         // Does this inst use large (256-bit) registers?
  bit ignoresVEX_L = 0;     // Does this instruction ignore the L-bit
  bit hasEVEX_K = 0;        // Does this inst require masking?
  bit hasEVEX_Z = 0;        // Does this inst set the EVEX_Z field?
  bit hasEVEX_L2 = 0;       // Does this inst set the EVEX_L2 field?
  bit hasEVEX_B = 0;        // Does this inst set the EVEX_B field?
  bit hasEVEX_NF = 0;       // Does this inst set the EVEX_NF field?
  bit hasTwoConditionalOps = 0;   // Does this inst have two conditional operands?
  bits<3> CD8_Form = 0;     // Compressed disp8 form - vector-width.
  // Declare it int rather than bits<4> so that all bits are defined when
  // assigning to bits<7>.
  int CD8_EltSize = 0;      // Compressed disp8 form - element-size in bytes.
  bit hasEVEX_RC = 0;       // Explicitly specified rounding control in FP instruction.
  bit hasNoTrackPrefix = 0; // Does this inst has 0x3E (NoTrack) prefix?

  // Vector size in bytes.
  bits<7> VectSize = !if(hasEVEX_L2, 64, !if(hasVEX_L, 32, 16));

  // The scaling factor for AVX512's compressed displacement is either
  //   - the size of a  power-of-two number of elements or
  //   - the size of a single element for broadcasts or
  //   - the total vector size divided by a power-of-two number.
  // Possible values are: 0 (non-AVX512 inst), 1, 2, 4, 8, 16, 32 and 64.
  bits<7> CD8_Scale = !if (!eq (OpEnc.Value, EncEVEX.Value),
                           !if (CD8_Form{2},
                                !shl(CD8_EltSize, CD8_Form{1-0}),
                                !if (hasEVEX_B,
                                     CD8_EltSize,
                                     !srl(VectSize, CD8_Form{1-0}))), 0);

  ExplicitOpPrefix explicitOpPrefix = NoExplicitOpPrefix;
  bits<2> explicitOpPrefixBits = explicitOpPrefix.Value;
  // TSFlags layout should be kept in sync with X86BaseInfo.h.
  let TSFlags{6-0}   = FormBits;
  let TSFlags{8-7}   = OpSizeBits;
  let TSFlags{10-9}  = AdSizeBits;
  // No need for 3rd bit, we don't need to distinguish NoPrfx from PS.
  let TSFlags{12-11} = OpPrefixBits{1-0};
  let TSFlags{16-13} = OpMapBits;
  let TSFlags{17}    = hasREX_W;
  let TSFlags{21-18} = ImmT.Value;
  let TSFlags{24-22} = FPForm.Value;
  let TSFlags{25}    = hasLockPrefix;
  let TSFlags{26}    = hasREPPrefix;
  let TSFlags{28-27} = ExeDomain.Value;
  let TSFlags{30-29} = OpEncBits;
  let TSFlags{38-31} = Opcode;
  let TSFlags{39}    = hasVEX_4V;
  let TSFlags{40}    = hasVEX_L;
  let TSFlags{41}    = hasEVEX_K;
  let TSFlags{42}    = hasEVEX_Z;
  let TSFlags{43}    = hasEVEX_L2;
  let TSFlags{44}    = hasEVEX_B;
  let TSFlags{47-45} = !if(!eq(CD8_Scale, 0), 0, !add(!logtwo(CD8_Scale), 1));
  let TSFlags{48}    = hasEVEX_RC;
  let TSFlags{49}    = hasNoTrackPrefix;
  let TSFlags{51-50} = explicitOpPrefixBits;
  let TSFlags{52}    = hasEVEX_NF;
  let TSFlags{53}    = hasTwoConditionalOps;
}

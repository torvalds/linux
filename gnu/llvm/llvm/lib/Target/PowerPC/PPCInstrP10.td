//===-- PPCInstrP10.td - Power10 Instruction Set -----------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the instructions introduced for the Power10 CPU.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Naming convention for future instruction formats
//
// <INSTR_FORM>{_<OP_TYPE><OP_LENGTH>}+
//
// Where:
// <INSTR_FORM> - name of instruction format as per the ISA
//                (X-Form, VX-Form, etc.)
// <OP_TYPE> - operand type
//             * FRT/RT/VT/XT/BT - target register
//                                 (FPR, GPR, VR, VSR, CR-bit respectively)
//                                 In some situations, the 'T' is replaced by
//                                 'D' when describing the target register.
//             * [FR|R|V|X|B][A-Z] - register source (i.e. FRA, RA, XB, etc.)
//             * IMM - immediate (where signedness matters,
//                     this is SI/UI for signed/unsigned)
//             * [R|X|FR]Tp - register pair target (i.e. FRTp, RTp)
//             * R - PC-Relative bit
//                   (denotes that the address is computed pc-relative)
//             * VRM - Masked Registers
//             * AT - target accumulator
//             * N - the Nth bit in a VSR
//             * Additional 1-bit operands may be required for certain
//               instruction formats such as: MC, P, MP
//             * X / Y / P - mask values. In the instruction encoding, this is
//                           represented as XMSK, YMSK and PMSK.
//             * MEM - indicates if the instruction format requires any memory
//                     accesses. This does not have <OP_LENGTH> attached to it.
// <OP_LENGTH> - the length of each operand in bits.
//               For operands that are 1 bit, the '1' is omitted from the name.
//
// Example: 8RR_XX4Form_IMM8_XTAB6
//          8RR_XX4Form is the instruction format.
//          The operand is an 8-bit immediate (IMM), the destination (XT)
//          and sources (XA, XB) that are all 6-bits. The destination and
//          source registers are combined if they are of the same length.
//          Moreover, the order of operands reflects the order of operands
//          in the encoding.

//-------------------------- Predicate definitions ---------------------------//
def IsPPC32 : Predicate<"!Subtarget->isPPC64()">;


//===----------------------------------------------------------------------===//
// PowerPC ISA 3.1 specific type constraints.
//

def SDT_PPCSplat32 : SDTypeProfile<1, 3, [ SDTCisVT<0, v2i64>,
  SDTCisVec<1>, SDTCisInt<2>, SDTCisInt<3>
]>;
def SDT_PPCAccBuild : SDTypeProfile<1, 4, [
  SDTCisVT<0, v512i1>, SDTCisVT<1, v4i32>, SDTCisVT<2, v4i32>,
                       SDTCisVT<3, v4i32>, SDTCisVT<4, v4i32>
]>;
def SDT_PPCPairBuild : SDTypeProfile<1, 2, [
  SDTCisVT<0, v256i1>, SDTCisVT<1, v4i32>, SDTCisVT<2, v4i32>
]>;
def SDT_PPCAccExtractVsx : SDTypeProfile<1, 2, [
  SDTCisVT<0, v4i32>, SDTCisVT<1, v512i1>, SDTCisPtrTy<2>
]>;
def SDT_PPCPairExtractVsx : SDTypeProfile<1, 2, [
  SDTCisVT<0, v4i32>, SDTCisVT<1, v256i1>, SDTCisPtrTy<2>
]>;
def SDT_PPCxxmfacc : SDTypeProfile<1, 1, [
  SDTCisVT<0, v512i1>, SDTCisVT<1, v512i1>
]>;

//===----------------------------------------------------------------------===//
// ISA 3.1 specific PPCISD nodes.
//

def PPCxxsplti32dx : SDNode<"PPCISD::XXSPLTI32DX", SDT_PPCSplat32, []>;
def PPCAccBuild : SDNode<"PPCISD::ACC_BUILD", SDT_PPCAccBuild, []>;
def PPCPairBuild : SDNode<"PPCISD::PAIR_BUILD", SDT_PPCPairBuild, []>;
def PPCAccExtractVsx : SDNode<"PPCISD::EXTRACT_VSX_REG", SDT_PPCAccExtractVsx,
                       []>;
def PPCPairExtractVsx : SDNode<"PPCISD::EXTRACT_VSX_REG", SDT_PPCPairExtractVsx,
                        []>;
def PPCxxmfacc : SDNode<"PPCISD::XXMFACC", SDT_PPCxxmfacc, []>;

//===----------------------------------------------------------------------===//

// PC Relative flag (for instructions that use the address of the prefix for
// address computations).
class isPCRel { bit PCRel = 1; }

// PowerPC specific type constraints.
def SDT_PPCLXVRZX : SDTypeProfile<1, 2, [
  SDTCisVT<0, v1i128>, SDTCisPtrTy<1>, SDTCisPtrTy<2>
]>;

// PPC Specific DAG Nodes.
def PPClxvrzx : SDNode<"PPCISD::LXVRZX", SDT_PPCLXVRZX,
                       [SDNPHasChain, SDNPMayLoad]>;

// Top-level class for prefixed instructions.
class PI<bits<6> pref, bits<6> opcode, dag OOL, dag IOL, string asmstr,
         InstrItinClass itin> : Instruction {
  field bits<64> Inst;
  field bits<64> SoftFail = 0;
  bit PCRel = 0; // Default value, set by isPCRel.
  let Size = 8;

  let Namespace = "PPC";
  let OutOperandList = OOL;
  let InOperandList = IOL;
  let AsmString = asmstr;
  let Itinerary = itin;
  let Inst{0-5} = pref;
  let Inst{32-37} = opcode;

  bits<1> PPC970_First = 0;
  bits<1> PPC970_Single = 0;
  bits<1> PPC970_Cracked = 0;
  bits<3> PPC970_Unit = 0;

  /// These fields correspond to the fields in PPCInstrInfo.h.  Any changes to
  /// these must be reflected there!  See comments there for what these are.
  let TSFlags{0}   = PPC970_First;
  let TSFlags{1}   = PPC970_Single;
  let TSFlags{2}   = PPC970_Cracked;
  let TSFlags{5-3} = PPC970_Unit;

  bits<1> Prefixed = 1;  // This is a prefixed instruction.
  let TSFlags{7}  = Prefixed;

  // For cases where multiple instruction definitions really represent the
  // same underlying instruction but with one definition for 64-bit arguments
  // and one for 32-bit arguments, this bit breaks the degeneracy between
  // the two forms and allows TableGen to generate mapping tables.
  bit Interpretation64Bit = 0;

  // Fields used for relation models.
  string BaseName = "";
}

// VX-Form: [ PO VT R VB RC XO ]
class VXForm_VTB5_RC<bits<10> xo, bits<5> R, dag OOL, dag IOL, string asmstr,
                      InstrItinClass itin, list<dag> pattern>
  : I<4, OOL, IOL, asmstr, itin> {
  bits<5> VT;
  bits<5> VB;
  bit RC = 0;

  let Pattern = pattern;

  let Inst{6-10} = VT;
  let Inst{11-15} = R;
  let Inst{16-20} = VB;
  let Inst{21} = RC;
  let Inst{22-31} = xo;
}

// Multiclass definition to account for record and non-record form
// instructions of VXRForm.
multiclass VXForm_VTB5_RCr<bits<10> xo, bits<5> R, dag OOL, dag IOL,
                            string asmbase, string asmstr,
                            InstrItinClass itin, list<dag> pattern> {
  let BaseName = asmbase in {
    def NAME : VXForm_VTB5_RC<xo, R, OOL, IOL,
                               !strconcat(asmbase, !strconcat(" ", asmstr)),
                               itin, pattern>, RecFormRel;
    let Defs = [CR6] in
    def _rec : VXForm_VTB5_RC<xo, R, OOL, IOL,
                               !strconcat(asmbase, !strconcat(". ", asmstr)),
                               itin, []>, isRecordForm, RecFormRel;
  }
}

class MLS_DForm_R_SI34_RTA5_MEM<bits<6> opcode, dag OOL, dag IOL, string asmstr,
                                InstrItinClass itin, list<dag> pattern>
  : PI<1, opcode, OOL, IOL, asmstr, itin> {
  bits<5> RST;
  bits<5> RA;
  bits<34> D;

  let Pattern = pattern;

  // The prefix.
  let Inst{6-7} = 2;
  let Inst{8-10} = 0;
  let Inst{11} = PCRel;
  let Inst{12-13} = 0;
  let Inst{14-31} = D{33-16}; // d0

  // The instruction.
  let Inst{38-42} = RST{4-0};
  let Inst{43-47} = RA;
  let Inst{48-63} = D{15-0}; // d1
}

class MLS_DForm_R_SI34_RTA5<bits<6> opcode, dag OOL, dag IOL, string asmstr,
                            InstrItinClass itin, list<dag> pattern>
  : PI<1, opcode, OOL, IOL, asmstr, itin> {
  bits<5> RT;
  bits<5> RA;
  bits<34> SI;

  let Pattern = pattern;

  // The prefix.
  let Inst{6-7} = 2;
  let Inst{8-10} = 0;
  let Inst{11} = PCRel;
  let Inst{12-13} = 0;
  let Inst{14-31} = SI{33-16};

  // The instruction.
  let Inst{38-42} = RT;
  let Inst{43-47} = RA;
  let Inst{48-63} = SI{15-0};
}

class MLS_DForm_SI34_RT5<bits<6> opcode, dag OOL, dag IOL, string asmstr,
                         InstrItinClass itin, list<dag> pattern>
  : PI<1, opcode, OOL, IOL, asmstr, itin> {
  bits<5> RT;
  bits<34> SI;

  let Pattern = pattern;

  // The prefix.
  let Inst{6-7} = 2;
  let Inst{8-10} = 0;
  let Inst{11} = 0;
  let Inst{12-13} = 0;
  let Inst{14-31} = SI{33-16};

  // The instruction.
  let Inst{38-42} = RT;
  let Inst{43-47} = 0;
  let Inst{48-63} = SI{15-0};
}

multiclass MLS_DForm_R_SI34_RTA5_p<bits<6> opcode, dag OOL, dag IOL,
                                   dag PCRel_IOL, string asmstr,
                                   InstrItinClass itin> {
  def NAME : MLS_DForm_R_SI34_RTA5<opcode, OOL, IOL,
                                   !strconcat(asmstr, ", 0"), itin, []>;
  def pc : MLS_DForm_R_SI34_RTA5<opcode, OOL, PCRel_IOL,
                                 !strconcat(asmstr, ", 1"), itin, []>, isPCRel;
}

class 8LS_DForm_R_SI34_RTA5_MEM<bits<6> opcode, dag OOL, dag IOL, string asmstr,
                                InstrItinClass itin, list<dag> pattern>
  : PI<1, opcode, OOL, IOL, asmstr, itin> {
  bits<5> RST;
  bits<5> RA;
  bits<34> D;

  let Pattern = pattern;

  // The prefix.
  let Inst{6-10} = 0;
  let Inst{11} = PCRel;
  let Inst{12-13} = 0;
  let Inst{14-31} = D{33-16}; // d0

  // The instruction.
  let Inst{38-42} = RST{4-0};
  let Inst{43-47} = RA;
  let Inst{48-63} = D{15-0}; // d1
}

// 8LS:D-Form: [ 1 0 0 // R // d0
//               PO TX T RA d1 ]
class 8LS_DForm_R_SI34_XT6_RA5_MEM<bits<5> opcode, dag OOL, dag IOL,
                                   string asmstr, InstrItinClass itin,
                                   list<dag> pattern>
  : PI<1, { opcode, ? }, OOL, IOL, asmstr, itin> {
  bits<6> XST;
  bits<5> RA;
  bits<34> D;

  let Pattern = pattern;

  // The prefix.
  let Inst{6-7} = 0;
  let Inst{8} = 0;
  let Inst{9-10} = 0; // reserved
  let Inst{11} = PCRel;
  let Inst{12-13} = 0; // reserved
  let Inst{14-31} = D{33-16}; // d0

  // The instruction.
  let Inst{37} = XST{5};
  let Inst{38-42} = XST{4-0};
  let Inst{43-47} = RA;
  let Inst{48-63} = D{15-0}; // d1
}

// X-Form: [PO T IMM VRB XO TX]
class XForm_XT6_IMM5_VB5<bits<6> opcode, bits<10> xo, dag OOL, dag IOL,
                         string asmstr, InstrItinClass itin, list<dag> pattern>
  : I<opcode, OOL, IOL, asmstr, itin> {
  bits<6> XT;
  bits<5> VRB;
  bits<5> IMM;

  let Pattern = pattern;
  let Inst{6-10} = XT{4-0};
  let Inst{11-15} = IMM;
  let Inst{16-20} = VRB;
  let Inst{21-30} = xo;
  let Inst{31} = XT{5};
}

class 8RR_XX4Form_IMM8_XTAB6<bits<6> opcode, bits<2> xo,
                             dag OOL, dag IOL, string asmstr,
                             InstrItinClass itin, list<dag> pattern>
  : PI<1, opcode, OOL, IOL, asmstr, itin> {
    bits<6> XT;
    bits<6> XA;
    bits<6> XB;
    bits<6> XC;
    bits<8> IMM;

    let Pattern = pattern;

    // The prefix.
    let Inst{6-7} = 1;
    let Inst{8} = 0;
    let Inst{9-11} = 0;
    let Inst{12-13} = 0;
    let Inst{14-23} = 0;
    let Inst{24-31} = IMM;

    // The instruction.
    let Inst{38-42} = XT{4-0};
    let Inst{43-47} = XA{4-0};
    let Inst{48-52} = XB{4-0};
    let Inst{53-57} = XC{4-0};
    let Inst{58-59} = xo;
    let Inst{60} = XC{5};
    let Inst{61} = XA{5};
    let Inst{62} = XB{5};
    let Inst{63} = XT{5};
}

class VXForm_RD5_N3_VB5<bits<11> xo, dag OOL, dag IOL, string asmstr,
                        InstrItinClass itin, list<dag> pattern>
  : I<4, OOL, IOL, asmstr, itin> {
  bits<5> RD;
  bits<5> VB;
  bits<3> N;

  let Pattern = pattern;

  let Inst{6-10}  = RD;
  let Inst{11-12} = 0;
  let Inst{13-15} = N;
  let Inst{16-20} = VB;
  let Inst{21-31} = xo;
}


// VX-Form: [PO VRT RA VRB XO].
// Destructive (insert) forms are suffixed with _ins.
class VXForm_VTB5_RA5_ins<bits<11> xo, string opc, list<dag> pattern>
  : VXForm_1<xo, (outs vrrc:$VD), (ins vrrc:$VDi, gprc:$VA, vrrc:$VB),
             !strconcat(opc, " $VD, $VA, $VB"), IIC_VecGeneral, pattern>,
             RegConstraint<"$VDi = $VD">, NoEncode<"$VDi">;

// VX-Form: [PO VRT RA RB XO].
// Destructive (insert) forms are suffixed with _ins.
class VXForm_VRT5_RAB5_ins<bits<11> xo, string opc, list<dag> pattern>
  : VXForm_1<xo, (outs vrrc:$VD), (ins vrrc:$VDi, gprc:$VA, gprc:$VB),
             !strconcat(opc, " $VD, $VA, $VB"), IIC_VecGeneral, pattern>,
             RegConstraint<"$VDi = $VD">, NoEncode<"$VDi">;

// VX-Form: [ PO BF // VRA VRB XO ]
class VXForm_BF3_VAB5<bits<11> xo, dag OOL, dag IOL, string asmstr,
                      InstrItinClass itin, list<dag> pattern>
  : I<4, OOL, IOL, asmstr, itin> {
  bits<3> BF;
  bits<5> VA;
  bits<5> VB;

  let Pattern = pattern;

  let Inst{6-8} = BF;
  let Inst{9-10} = 0;
  let Inst{11-15} = VA;
  let Inst{16-20} = VB;
  let Inst{21-31} = xo;
}

// VN-Form: [PO VRT VRA VRB PS SD XO]
// SD is "Shift Direction"
class VNForm_VTAB5_SD3<bits<6> xo, bits<2> ps, dag OOL, dag IOL, string asmstr,
                       InstrItinClass itin, list<dag> pattern>
    : I<4, OOL, IOL, asmstr, itin> {
  bits<5> VRT;
  bits<5> VRA;
  bits<5> VRB;
  bits<3> SD;

  let Pattern = pattern;

  let Inst{6-10}  = VRT;
  let Inst{11-15} = VRA;
  let Inst{16-20} = VRB;
  let Inst{21-22} = ps;
  let Inst{23-25} = SD;
  let Inst{26-31} = xo;
}

class VXForm_RD5_MP_VB5<bits<11> xo, bits<4> eo, dag OOL, dag IOL,
                        string asmstr, InstrItinClass itin, list<dag> pattern>
  : I<4, OOL, IOL, asmstr, itin> {
  bits<5> RD;
  bits<5> VB;
  bit MP;

  let Pattern = pattern;

  let Inst{6-10}  = RD;
  let Inst{11-14} = eo;
  let Inst{15} = MP;
  let Inst{16-20} = VB;
  let Inst{21-31} = xo;
}

// 8RR:D-Form: [ 1 1 0 // // imm0
//               PO T XO TX imm1 ].
class 8RR_DForm_IMM32_XT6<bits<6> opcode, bits<4> xo, dag OOL, dag IOL,
                          string asmstr, InstrItinClass itin,
                          list<dag> pattern>
  : PI<1, opcode, OOL, IOL, asmstr, itin> {
  bits<6> XT;
  bits<32> IMM32;

  let Pattern = pattern;

  // The prefix.
  let Inst{6-7} = 1;
  let Inst{8-11} = 0;
  let Inst{12-13} = 0; // reserved
  let Inst{14-15} = 0; // reserved
  let Inst{16-31} = IMM32{31-16};

  // The instruction.
  let Inst{38-42} = XT{4-0};
  let Inst{43-46} = xo;
  let Inst{47} = XT{5};
  let Inst{48-63} = IMM32{15-0};
}

// 8RR:D-Form: [ 1 1 0 // // imm0
//               PO T XO IX TX imm1 ].
class 8RR_DForm_IMM32_XT6_IX<bits<6> opcode, bits<3> xo, dag OOL, dag IOL,
                             string asmstr, InstrItinClass itin,
                             list<dag> pattern>
  : PI<1, opcode, OOL, IOL, asmstr, itin> {
  bits<6> XT;
  bit IX;
  bits<32> IMM32;

  let Pattern = pattern;

  // The prefix.
  let Inst{6-7} = 1;
  let Inst{8-11} = 0;
  let Inst{12-13} = 0; // reserved
  let Inst{14-15} = 0; // reserved
  let Inst{16-31} = IMM32{31-16};

  // The instruction.
  let Inst{38-42} = XT{4-0};
  let Inst{43-45} = xo;
  let Inst{46} = IX;
  let Inst{47} = XT{5};
  let Inst{48-63} = IMM32{15-0};
}

class 8RR_XX4Form_XTABC6<bits<6> opcode, bits<2> xo, dag OOL, dag IOL,
                         string asmstr, InstrItinClass itin, list<dag> pattern>
  : PI<1, opcode, OOL, IOL, asmstr, itin> {
  bits<6> XT;
  bits<6> XA;
  bits<6> XB;
  bits<6> XC;

  let Pattern = pattern;

  // The prefix.
  let Inst{6-7} = 1;
  let Inst{8-11} = 0;
  let Inst{12-13} = 0;
  let Inst{14-31} = 0;

  // The instruction.
  let Inst{38-42} = XT{4-0};
  let Inst{43-47} = XA{4-0};
  let Inst{48-52} = XB{4-0};
  let Inst{53-57} = XC{4-0};
  let Inst{58-59} = xo;
  let Inst{60} = XC{5};
  let Inst{61} = XA{5};
  let Inst{62} = XB{5};
  let Inst{63} = XT{5};
}

class 8RR_XX4Form_IMM3_XTABC6<bits<6> opcode, bits<2> xo, dag OOL, dag IOL,
                              string asmstr, InstrItinClass itin,
                              list<dag> pattern>
  : PI<1, opcode, OOL, IOL, asmstr, itin> {
  bits<6> XT;
  bits<6> XA;
  bits<6> XB;
  bits<6> XC;
  bits<3> IMM;

  let Pattern = pattern;

  // The prefix.
  let Inst{6-7} = 1;
  let Inst{8-11} = 0;
  let Inst{12-13} = 0;
  let Inst{14-28} = 0;
  let Inst{29-31} = IMM;

  // The instruction.
  let Inst{38-42} = XT{4-0};
  let Inst{43-47} = XA{4-0};
  let Inst{48-52} = XB{4-0};
  let Inst{53-57} = XC{4-0};
  let Inst{58-59} = xo;
  let Inst{60} = XC{5};
  let Inst{61} = XA{5};
  let Inst{62} = XB{5};
  let Inst{63} = XT{5};
}

// [PO BF / XO2 B XO BX /]
class XX2_BF3_XO5_XB6_XO9<bits<6> opcode, bits<5> xo2, bits<9> xo, dag OOL,
                          dag IOL, string asmstr, InstrItinClass itin,
                          list<dag> pattern>
  : I<opcode, OOL, IOL, asmstr, itin> {
  bits<3> BF;
  bits<6> XB;

  let Pattern = pattern;

  let Inst{6-8}   = BF;
  let Inst{9-10}  = 0;
  let Inst{11-15} = xo2;
  let Inst{16-20} = XB{4-0};
  let Inst{21-29} = xo;
  let Inst{30}    = XB{5};
  let Inst{31}    = 0;
}

// X-Form: [ PO RT BI /// XO / ]
class XForm_XT5_BI5<bits<6> opcode, bits<10> xo, dag OOL, dag IOL,
                    string asmstr, InstrItinClass itin, list<dag> pattern>
  : XForm_base_r3xo<opcode, xo, OOL, IOL, asmstr, itin, pattern> {
  bits<5> BI;
  let RA = BI;
  let RB = 0;
}

multiclass MLS_DForm_R_SI34_RTA5_MEM_p<bits<6> opcode, dag OOL, dag IOL,
                                       dag PCRel_IOL, dag PCRelOnly_IOL,
                                       string asmstr, string asmstr_pcext,
                                       InstrItinClass itin> {
  def NAME : MLS_DForm_R_SI34_RTA5_MEM<opcode, OOL, IOL,
                                       !strconcat(asmstr, ", 0"), itin, []>;
  def pc : MLS_DForm_R_SI34_RTA5_MEM<opcode, OOL, PCRel_IOL,
                                     !strconcat(asmstr, ", 1"), itin, []>,
                                     isPCRel;
  let isAsmParserOnly = 1, hasNoSchedulingInfo = 1 in {
    def nopc : MLS_DForm_R_SI34_RTA5_MEM<opcode, OOL, IOL, asmstr, itin, []>;
    let RA = 0 in
      def onlypc : MLS_DForm_R_SI34_RTA5_MEM<opcode, OOL, PCRelOnly_IOL,
                                             asmstr_pcext, itin, []>, isPCRel;
  }
}

multiclass 8LS_DForm_R_SI34_RTA5_MEM_p<bits<6> opcode, dag OOL, dag IOL,
                                       dag PCRel_IOL, dag PCRelOnly_IOL,
                                       string asmstr, string asmstr_pcext,
                                       InstrItinClass itin> {
  def NAME : 8LS_DForm_R_SI34_RTA5_MEM<opcode, OOL, IOL,
                                       !strconcat(asmstr, ", 0"), itin, []>;
  def pc : 8LS_DForm_R_SI34_RTA5_MEM<opcode, OOL, PCRel_IOL,
                                     !strconcat(asmstr, ", 1"), itin, []>,
                                     isPCRel;
  let isAsmParserOnly = 1, hasNoSchedulingInfo = 1 in {
    def nopc : 8LS_DForm_R_SI34_RTA5_MEM<opcode, OOL, IOL, asmstr, itin, []>;
    let RA = 0 in
      def onlypc : 8LS_DForm_R_SI34_RTA5_MEM<opcode, OOL, PCRelOnly_IOL,
                                             asmstr_pcext, itin, []>, isPCRel;
  }
}

multiclass 8LS_DForm_R_SI34_XT6_RA5_MEM_p<bits<5> opcode, dag OOL, dag IOL,
                                          dag PCRel_IOL, dag PCRelOnly_IOL,
                                          string asmstr, string asmstr_pcext,
                                          InstrItinClass itin> {
  def NAME : 8LS_DForm_R_SI34_XT6_RA5_MEM<opcode, OOL, IOL,
                                          !strconcat(asmstr, ", 0"), itin, []>;
  def pc : 8LS_DForm_R_SI34_XT6_RA5_MEM<opcode, OOL, PCRel_IOL,
                                        !strconcat(asmstr, ", 1"), itin, []>,
                                        isPCRel;
  let isAsmParserOnly = 1, hasNoSchedulingInfo = 1 in {
    def nopc : 8LS_DForm_R_SI34_XT6_RA5_MEM<opcode, OOL, IOL, asmstr, itin, []>;
    let RA = 0 in
      def onlypc : 8LS_DForm_R_SI34_XT6_RA5_MEM<opcode, OOL, PCRelOnly_IOL,
                                                asmstr_pcext, itin, []>, isPCRel;
  }
}

def PrefixInstrs : Predicate<"Subtarget->hasPrefixInstrs()">;
def IsISA3_1 : Predicate<"Subtarget->isISA3_1()">;
def PairedVectorMemops : Predicate<"Subtarget->pairedVectorMemops()">;
def RCCp {
  dag AToVSRC = (COPY_TO_REGCLASS $XA, VSRC);
  dag BToVSRC = (COPY_TO_REGCLASS $XB, VSRC);
}

let Predicates = [PrefixInstrs] in {
  let Interpretation64Bit = 1, isCodeGenOnly = 1 in {
    defm PADDI8 :
      MLS_DForm_R_SI34_RTA5_p<14, (outs g8rc:$RT), (ins g8rc_nox0:$RA, s34imm:$SI),
                              (ins immZero:$RA, s34imm_pcrel:$SI),
                              "paddi $RT, $RA, $SI", IIC_LdStLFD>;
    let isReMaterializable = 1, isAsCheapAsAMove = 1, isMoveImm = 1 in {
      def PLI8 : MLS_DForm_SI34_RT5<14, (outs g8rc:$RT),
                                    (ins s34imm:$SI),
                                    "pli $RT, $SI", IIC_IntSimple, []>;
    }
  }
  defm PADDI :
    MLS_DForm_R_SI34_RTA5_p<14, (outs gprc:$RT), (ins gprc_nor0:$RA, s34imm:$SI),
                            (ins immZero:$RA, s34imm_pcrel:$SI),
                            "paddi $RT, $RA, $SI", IIC_LdStLFD>;
  let isReMaterializable = 1, isAsCheapAsAMove = 1, isMoveImm = 1 in {
    def PLI : MLS_DForm_SI34_RT5<14, (outs gprc:$RT),
                                 (ins s34imm:$SI),
                                 "pli $RT, $SI", IIC_IntSimple, []>;
  }

  let mayLoad = 1, mayStore = 0 in {
    let Interpretation64Bit = 1, isCodeGenOnly = 1 in {
      defm PLBZ8 :
        MLS_DForm_R_SI34_RTA5_MEM_p<34, (outs g8rc:$RST), (ins (memri34 $D, $RA):$addr),
                                    (ins (memri34_pcrel $D, $RA):$addr),
                                    (ins s34imm_pcrel:$D), "plbz $RST, $addr",
                                    "plbz $RST, $D", IIC_LdStLFD>;
      defm PLHZ8 :
        MLS_DForm_R_SI34_RTA5_MEM_p<40, (outs g8rc:$RST), (ins (memri34 $D, $RA):$addr),
                                    (ins (memri34_pcrel $D, $RA):$addr),
                                    (ins s34imm_pcrel:$D), "plhz $RST, $addr",
                                    "plhz $RST, $D", IIC_LdStLFD>;
      defm PLHA8 :
        MLS_DForm_R_SI34_RTA5_MEM_p<42, (outs g8rc:$RST), (ins (memri34 $D, $RA):$addr),
                                    (ins (memri34_pcrel $D, $RA):$addr),
                                    (ins s34imm_pcrel:$D), "plha $RST, $addr",
                                    "plha $RST, $D", IIC_LdStLFD>;
      defm PLWA8 :
        8LS_DForm_R_SI34_RTA5_MEM_p<41, (outs g8rc:$RST), (ins (memri34 $D, $RA):$addr),
                                    (ins (memri34_pcrel $D, $RA):$addr),
                                    (ins s34imm_pcrel:$D),
                                    "plwa $RST, $addr", "plwa $RST, $D",  IIC_LdStLFD>;
      defm PLWZ8 :
        MLS_DForm_R_SI34_RTA5_MEM_p<32, (outs g8rc:$RST), (ins (memri34 $D, $RA):$addr),
                                    (ins (memri34_pcrel $D, $RA):$addr),
                                    (ins s34imm_pcrel:$D), "plwz $RST, $addr",
                                    "plwz $RST, $D", IIC_LdStLFD>;
    }
    defm PLBZ :
      MLS_DForm_R_SI34_RTA5_MEM_p<34, (outs gprc:$RST), (ins (memri34 $D, $RA):$addr),
                                  (ins (memri34_pcrel $D, $RA):$addr),
                                  (ins s34imm_pcrel:$D), "plbz $RST, $addr",
                                  "plbz $RST, $D", IIC_LdStLFD>;
    defm PLHZ :
      MLS_DForm_R_SI34_RTA5_MEM_p<40, (outs gprc:$RST), (ins (memri34 $D, $RA):$addr),
                                  (ins (memri34_pcrel $D, $RA):$addr),
                                  (ins s34imm_pcrel:$D), "plhz $RST, $addr",
                                  "plhz $RST, $D", IIC_LdStLFD>;
    defm PLHA :
      MLS_DForm_R_SI34_RTA5_MEM_p<42, (outs gprc:$RST), (ins (memri34 $D, $RA):$addr),
                                  (ins (memri34_pcrel $D, $RA):$addr),
                                  (ins s34imm_pcrel:$D), "plha $RST, $addr",
                                  "plha $RST, $D", IIC_LdStLFD>;
    defm PLWZ :
      MLS_DForm_R_SI34_RTA5_MEM_p<32, (outs gprc:$RST), (ins (memri34 $D, $RA):$addr),
                                  (ins (memri34_pcrel $D, $RA):$addr),
                                  (ins s34imm_pcrel:$D), "plwz $RST, $addr",
                                  "plwz $RST, $D", IIC_LdStLFD>;
    defm PLWA :
      8LS_DForm_R_SI34_RTA5_MEM_p<41, (outs gprc:$RST), (ins (memri34 $D, $RA):$addr),
                                  (ins (memri34_pcrel $D, $RA):$addr),
                                  (ins s34imm_pcrel:$D),
                                  "plwa $RST, $addr", "plwa $RST, $D",
                                  IIC_LdStLFD>;
    defm PLD :
      8LS_DForm_R_SI34_RTA5_MEM_p<57, (outs g8rc:$RST), (ins (memri34 $D, $RA):$addr),
                                  (ins (memri34_pcrel $D, $RA):$addr),
                                  (ins s34imm_pcrel:$D),
                                  "pld $RST, $addr", "pld $RST, $D",
                                  IIC_LdStLFD>;
  }

  let mayStore = 1, mayLoad = 0 in {
    let Interpretation64Bit = 1, isCodeGenOnly = 1 in {
      defm PSTB8 :
        MLS_DForm_R_SI34_RTA5_MEM_p<38, (outs), (ins g8rc:$RST, (memri34 $D, $RA):$addr),
                                    (ins g8rc:$RST, (memri34_pcrel $D, $RA):$addr),
                                    (ins g8rc:$RST, s34imm_pcrel:$D),
                                    "pstb $RST, $addr", "pstb $RST, $D", IIC_LdStLFD>;
      defm PSTH8 :
        MLS_DForm_R_SI34_RTA5_MEM_p<44, (outs), (ins g8rc:$RST, (memri34 $D, $RA):$addr),
                                    (ins g8rc:$RST, (memri34_pcrel $D, $RA):$addr),
                                    (ins g8rc:$RST, s34imm_pcrel:$D),
                                    "psth $RST, $addr", "psth $RST, $D", IIC_LdStLFD>;
      defm PSTW8 :
        MLS_DForm_R_SI34_RTA5_MEM_p<36, (outs), (ins g8rc:$RST, (memri34 $D, $RA):$addr),
                                    (ins g8rc:$RST, (memri34_pcrel $D, $RA):$addr),
                                    (ins g8rc:$RST, s34imm_pcrel:$D),
                                    "pstw $RST, $addr", "pstw $RST, $D", IIC_LdStLFD>;
    }
    defm PSTB :
      MLS_DForm_R_SI34_RTA5_MEM_p<38, (outs), (ins gprc:$RST, (memri34 $D, $RA):$addr),
                                  (ins gprc:$RST, (memri34_pcrel $D, $RA):$addr),
                                  (ins gprc:$RST, s34imm_pcrel:$D),
                                  "pstb $RST, $addr", "pstb $RST, $D", IIC_LdStLFD>;
    defm PSTH :
      MLS_DForm_R_SI34_RTA5_MEM_p<44, (outs), (ins gprc:$RST, (memri34 $D, $RA):$addr),
                                  (ins gprc:$RST, (memri34_pcrel $D, $RA):$addr),
                                  (ins gprc:$RST, s34imm_pcrel:$D),
                                  "psth $RST, $addr", "psth $RST, $D", IIC_LdStLFD>;
    defm PSTW :
      MLS_DForm_R_SI34_RTA5_MEM_p<36, (outs), (ins gprc:$RST, (memri34 $D, $RA):$addr),
                                  (ins gprc:$RST, (memri34_pcrel $D, $RA):$addr),
                                  (ins gprc:$RST, s34imm_pcrel:$D),
                                  "pstw $RST, $addr", "pstw $RST, $D", IIC_LdStLFD>;
    defm PSTD :
      8LS_DForm_R_SI34_RTA5_MEM_p<61, (outs), (ins g8rc:$RST, (memri34 $D, $RA):$addr),
                                  (ins g8rc:$RST, (memri34_pcrel $D, $RA):$addr),
                                  (ins g8rc:$RST, s34imm_pcrel:$D),
                                  "pstd $RST, $addr", "pstd $RST, $D", IIC_LdStLFD>;
  }
}

let Predicates = [PrefixInstrs, HasFPU] in {
  let mayLoad = 1, mayStore = 0 in {
    defm PLFS :
      MLS_DForm_R_SI34_RTA5_MEM_p<48, (outs f4rc:$RST), (ins (memri34 $D, $RA):$addr),
                                  (ins (memri34_pcrel $D, $RA):$addr),
                                  (ins s34imm_pcrel:$D), "plfs $RST, $addr",
                                  "plfs $RST, $D", IIC_LdStLFD>;
    defm PLFD :
      MLS_DForm_R_SI34_RTA5_MEM_p<50, (outs f8rc:$RST), (ins (memri34 $D, $RA):$addr),
                                  (ins  (memri34_pcrel $D, $RA):$addr),
                                  (ins s34imm_pcrel:$D), "plfd $RST, $addr",
                                  "plfd $RST, $D", IIC_LdStLFD>;
  }
  let mayStore = 1, mayLoad = 0 in {
    defm PSTFS :
      MLS_DForm_R_SI34_RTA5_MEM_p<52, (outs), (ins f4rc:$RST, (memri34 $D, $RA):$addr),
                                  (ins f4rc:$RST, (memri34_pcrel $D, $RA):$addr),
                                  (ins f4rc:$RST, s34imm_pcrel:$D),
                                  "pstfs $RST, $addr", "pstfs $RST, $D", IIC_LdStLFD>;
    defm PSTFD :
      MLS_DForm_R_SI34_RTA5_MEM_p<54, (outs), (ins f8rc:$RST, (memri34 $D, $RA):$addr),
                                  (ins f8rc:$RST, (memri34_pcrel $D, $RA):$addr),
                                  (ins f8rc:$RST, s34imm_pcrel:$D),
                                  "pstfd $RST, $addr", "pstfd $RST, $D", IIC_LdStLFD>;
  }
}

let Predicates = [PrefixInstrs, HasP10Vector] in {
  let mayLoad = 1, mayStore = 0 in {
    defm PLXV :
      8LS_DForm_R_SI34_XT6_RA5_MEM_p<25, (outs vsrc:$XST), (ins (memri34 $D, $RA):$addr),
                                     (ins (memri34_pcrel $D, $RA):$addr),
                                     (ins s34imm_pcrel:$D),
                                     "plxv $XST, $addr", "plxv $XST, $D", IIC_LdStLFD>;
    defm PLXSSP :
      8LS_DForm_R_SI34_RTA5_MEM_p<43, (outs vfrc:$RST), (ins (memri34 $D, $RA):$addr),
                                  (ins (memri34_pcrel $D, $RA):$addr),
                                  (ins s34imm_pcrel:$D),
                                  "plxssp $RST, $addr",  "plxssp $RST, $D",
                                  IIC_LdStLFD>;
    defm PLXSD :
      8LS_DForm_R_SI34_RTA5_MEM_p<42, (outs vfrc:$RST), (ins (memri34 $D, $RA):$addr),
                                  (ins (memri34_pcrel $D, $RA):$addr),
                                  (ins s34imm_pcrel:$D),
                                  "plxsd $RST, $addr", "plxsd $RST, $D",
                                  IIC_LdStLFD>;
  }
 let mayStore = 1, mayLoad = 0 in {
    defm PSTXV :
      8LS_DForm_R_SI34_XT6_RA5_MEM_p<27, (outs), (ins vsrc:$XST, (memri34 $D, $RA):$addr),
                                     (ins vsrc:$XST, (memri34_pcrel $D, $RA):$addr),
                                     (ins vsrc:$XST, s34imm_pcrel:$D),
                                     "pstxv $XST, $addr", "pstxv $XST, $D", IIC_LdStLFD>;
    defm PSTXSSP :
      8LS_DForm_R_SI34_RTA5_MEM_p<47, (outs), (ins vfrc:$RST, (memri34 $D, $RA):$addr),
                                  (ins vfrc:$RST, (memri34_pcrel $D, $RA):$addr),
                                  (ins vfrc:$RST, s34imm_pcrel:$D),
                                  "pstxssp $RST, $addr", "pstxssp $RST, $D", IIC_LdStLFD>;
    defm PSTXSD :
      8LS_DForm_R_SI34_RTA5_MEM_p<46, (outs), (ins vfrc:$RST, (memri34 $D, $RA):$addr),
                                  (ins vfrc:$RST, (memri34_pcrel $D, $RA):$addr),
                                  (ins vfrc:$RST, s34imm_pcrel:$D),
                                  "pstxsd $RST, $addr", "pstxsd $RST, $D", IIC_LdStLFD>;
  }
  def XXPERMX :
    8RR_XX4Form_IMM3_XTABC6<34, 0, (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB,
                            vsrc:$XC, u3imm:$IMM),
                            "xxpermx $XT, $XA, $XB, $XC, $IMM",
                            IIC_VecPerm, []>;
  def XXBLENDVB :
    8RR_XX4Form_XTABC6<33, 0, (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB,
                       vsrc:$XC), "xxblendvb $XT, $XA, $XB, $XC",
                       IIC_VecGeneral, []>;
  def XXBLENDVH :
    8RR_XX4Form_XTABC6<33, 1, (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB,
                       vsrc:$XC), "xxblendvh $XT, $XA, $XB, $XC",
                       IIC_VecGeneral, []>;
  def XXBLENDVW :
    8RR_XX4Form_XTABC6<33, 2, (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB,
                       vsrc:$XC), "xxblendvw $XT, $XA, $XB, $XC",
                       IIC_VecGeneral, []>;
  def XXBLENDVD :
    8RR_XX4Form_XTABC6<33, 3, (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB,
                       vsrc:$XC), "xxblendvd $XT, $XA, $XB, $XC",
                       IIC_VecGeneral, []>;
}

class DQForm_XTp5_RA17_MEM<bits<6> opcode, bits<4> xo, dag OOL, dag IOL,
                           string asmstr, InstrItinClass itin, list<dag> pattern>
  : I<opcode, OOL, IOL, asmstr, itin> {
  bits<5> XTp;
  bits<5> RA;
  bits<12> DQ;

  let Pattern = pattern;

  let Inst{6-9} = XTp{3-0};
  let Inst{10} = XTp{4};
  let Inst{11-15} = RA;
  let Inst{16-27} = DQ;
  let Inst{28-31} = xo;
}

class XForm_XTp5_XAB5<bits<6> opcode, bits<10> xo, dag OOL, dag IOL,
                      string asmstr, InstrItinClass itin, list<dag> pattern>
  : I<opcode, OOL, IOL, asmstr, itin>, XFormMemOp {
  bits<5> XTp;
  bits<5> RA;
  bits<5> RB;

  let Pattern = pattern;
  let Inst{6-9} = XTp{3-0};
  let Inst{10} = XTp{4};
  let Inst{11-15} = RA;
  let Inst{16-20} = RB;
  let Inst{21-30} = xo;
  let Inst{31} = 0;
}

class 8LS_DForm_R_XTp5_SI34_MEM<bits<6> opcode, dag OOL, dag IOL, string asmstr,
                                InstrItinClass itin, list<dag> pattern>
  : PI<1, opcode, OOL, IOL, asmstr, itin> {
  bits<5> XTp;
  bits<5> RA;
  bits<34> D;

  let Pattern = pattern;

  // The prefix.
  let Inst{6-10} = 0;
  let Inst{11} = PCRel;
  let Inst{12-13} = 0;
  let Inst{14-31} = D{33-16}; // Imm18

  // The instruction.
  let Inst{38-41} = XTp{3-0};
  let Inst{42}    = XTp{4};
  let Inst{43-47} = RA;
  let Inst{48-63} = D{15-0};
}

multiclass 8LS_DForm_R_XTp5_SI34_MEM_p<bits<6> opcode, dag OOL,
                                       dag IOL, dag PCRel_IOL, dag PCRelOnly_IOL,
                                       string asmstr, string asmstr_pcext,
                                       InstrItinClass itin> {
  def NAME : 8LS_DForm_R_XTp5_SI34_MEM<opcode, OOL, IOL,
                                       !strconcat(asmstr, ", 0"), itin, []>;
  def pc : 8LS_DForm_R_XTp5_SI34_MEM<opcode, OOL, PCRel_IOL,
                                     !strconcat(asmstr, ", 1"), itin, []>,
                                     isPCRel;
  let isAsmParserOnly = 1, hasNoSchedulingInfo = 1 in {
    def nopc : 8LS_DForm_R_XTp5_SI34_MEM<opcode, OOL, IOL, asmstr, itin, []>;
    let RA = 0 in
      def onlypc : 8LS_DForm_R_XTp5_SI34_MEM<opcode, OOL, PCRelOnly_IOL,
                                             asmstr_pcext, itin, []>, isPCRel;
  }
}



// [PO AS XO2 XO]
class XForm_AT3<bits<6> opcode, bits<5> xo2, bits<10> xo, dag OOL, dag IOL,
                    string asmstr, InstrItinClass itin, list<dag> pattern>
  : I<opcode, OOL, IOL, asmstr, itin> {
  bits<3> AT;

  let Pattern = pattern;

  let Inst{6-8}  = AT;
  let Inst{9-10}  = 0;
  let Inst{11-15} = xo2;
  let Inst{16-20} = 0;
  let Inst{21-30} = xo;
  let Inst{31} = 0;
}

// X-Form: [ PO T EO UIM XO TX ]
class XForm_XT6_IMM5<bits<6> opcode, bits<5> eo, bits<10> xo, dag OOL, dag IOL,
                     string asmstr, InstrItinClass itin, list<dag> pattern>
  : I<opcode, OOL, IOL, asmstr, itin> {
  bits<6> XT;
  bits<5> UIM;

  let Pattern = pattern;

  let Inst{6-10} = XT{4-0};
  let Inst{11-15} = eo;
  let Inst{16-20} = UIM;
  let Inst{21-30} = xo;
  let Inst{31} = XT{5};
}

class XX3Form_AT3_XAB6<bits<6> opcode, bits<8> xo, dag OOL, dag IOL,
                           string asmstr, InstrItinClass itin,
                           list<dag> pattern>
  : I<opcode, OOL, IOL, asmstr, itin> {
  bits<3> AT;
  bits<6> XA;
  bits<6> XB;

  let Pattern = pattern;

  let Inst{6-8} = AT;
  let Inst{9-10} = 0;
  let Inst{11-15} = XA{4-0};
  let Inst{16-20} = XB{4-0};
  let Inst{21-28} = xo;
  let Inst{29}    = XA{5};
  let Inst{30}    = XB{5};
  let Inst{31} = 0;
}

class MMIRR_XX3Form_XY4P2_XAB6<bits<6> opcode, bits<8> xo, dag OOL, dag IOL,
                               string asmstr, InstrItinClass itin,
                               list<dag> pattern>
  : PI<1, opcode, OOL, IOL, asmstr, itin> {
  bits<3> AT;
  bits<6> XA;
  bits<6> XB;
  bits<4> XMSK;
  bits<4> YMSK;
  bits<2> PMSK;

  let Pattern = pattern;

  // The prefix.
  let Inst{6-7} = 3;
  let Inst{8-11} = 9;
  let Inst{12-15} = 0;
  let Inst{16-17} = PMSK;
  let Inst{18-23} = 0;
  let Inst{24-27} = XMSK;
  let Inst{28-31} = YMSK;

  // The instruction.
  let Inst{38-40} = AT;
  let Inst{41-42} = 0;
  let Inst{43-47} = XA{4-0};
  let Inst{48-52} = XB{4-0};
  let Inst{53-60} = xo;
  let Inst{61} = XA{5};
  let Inst{62} = XB{5};
  let Inst{63} = 0;
}

class MMIRR_XX3Form_XY4_XAB6<bits<6> opcode, bits<8> xo, dag OOL, dag IOL,
                             string asmstr, InstrItinClass itin,
                             list<dag> pattern>
  : PI<1, opcode, OOL, IOL, asmstr, itin> {
  bits<3> AT;
  bits<6> XA;
  bits<6> XB;
  bits<4> XMSK;
  bits<4> YMSK;

  let Pattern = pattern;

  // The prefix.
  let Inst{6-7} = 3;
  let Inst{8-11} = 9;
  let Inst{12-23} = 0;
  let Inst{24-27} = XMSK;
  let Inst{28-31} = YMSK;

  // The instruction.
  let Inst{38-40} = AT;
  let Inst{41-42} = 0;
  let Inst{43-47} = XA{4-0};
  let Inst{48-52} = XB{4-0};
  let Inst{53-60} = xo;
  let Inst{61} = XA{5};
  let Inst{62} = XB{5};
  let Inst{63} = 0;
}

class MMIRR_XX3Form_X4Y2_XAB6<bits<6> opcode, bits<8> xo, dag OOL, dag IOL,
                              string asmstr, InstrItinClass itin,
                              list<dag> pattern>
  : PI<1, opcode, OOL, IOL, asmstr, itin> {
  bits<3> AT;
  bits<6> XA;
  bits<6> XB;
  bits<4> XMSK;
  bits<2> YMSK;

  let Pattern = pattern;

  // The prefix.
  let Inst{6-7} = 3;
  let Inst{8-11} = 9;
  let Inst{12-23} = 0;
  let Inst{24-27} = XMSK;
  let Inst{28-29} = YMSK;
  let Inst{30-31} = 0;

  // The instruction.
  let Inst{38-40} = AT;
  let Inst{41-42} = 0;
  let Inst{43-47} = XA{4-0};
  let Inst{48-52} = XB{4-0};
  let Inst{53-60} = xo;
  let Inst{61} = XA{5};
  let Inst{62} = XB{5};
  let Inst{63} = 0;
}

class MMIRR_XX3Form_XY4P8_XAB6<bits<6> opcode, bits<8> xo, dag OOL, dag IOL,
                               string asmstr, InstrItinClass itin,
                               list<dag> pattern>
  : PI<1, opcode, OOL, IOL, asmstr, itin> {
  bits<3> AT;
  bits<6> XA;
  bits<6> XB;
  bits<4> XMSK;
  bits<4> YMSK;
  bits<8> PMSK;

  let Pattern = pattern;

  // The prefix.
  let Inst{6-7} = 3;
  let Inst{8-11} = 9;
  let Inst{12-15} = 0;
  let Inst{16-23} = PMSK;
  let Inst{24-27} = XMSK;
  let Inst{28-31} = YMSK;

  // The instruction.
  let Inst{38-40} = AT;
  let Inst{41-42} = 0;
  let Inst{43-47} = XA{4-0};
  let Inst{48-52} = XB{4-0};
  let Inst{53-60} = xo;
  let Inst{61} = XA{5};
  let Inst{62} = XB{5};
  let Inst{63} = 0;
}

class MMIRR_XX3Form_XYP4_XAB6<bits<6> opcode, bits<8> xo, dag OOL, dag IOL,
                              string asmstr, InstrItinClass itin,
                              list<dag> pattern>
  : PI<1, opcode, OOL, IOL, asmstr, itin> {
  bits<3> AT;
  bits<6> XA;
  bits<6> XB;
  bits<4> XMSK;
  bits<4> YMSK;
  bits<4> PMSK;

  let Pattern = pattern;

  // The prefix.
  let Inst{6-7} = 3;
  let Inst{8-11} = 9;
  let Inst{12-15} = 0;
  let Inst{16-19} = PMSK;
  let Inst{20-23} = 0;
  let Inst{24-27} = XMSK;
  let Inst{28-31} = YMSK;

  // The instruction.
  let Inst{38-40} = AT;
  let Inst{41-42} = 0;
  let Inst{43-47} = XA{4-0};
  let Inst{48-52} = XB{4-0};
  let Inst{53-60} = xo;
  let Inst{61} = XA{5};
  let Inst{62} = XB{5};
  let Inst{63} = 0;
}



def Concats {
  dag VecsToVecPair0 =
    (v256i1 (INSERT_SUBREG
      (INSERT_SUBREG (IMPLICIT_DEF), $vs0, sub_vsx1),
      $vs1, sub_vsx0));
  dag VecsToVecPair1 =
    (v256i1 (INSERT_SUBREG
      (INSERT_SUBREG (IMPLICIT_DEF), $vs2, sub_vsx1),
      $vs3, sub_vsx0));
}

let Predicates = [PairedVectorMemops] in {
  def : Pat<(v256i1 (PPCPairBuild v4i32:$vs1, v4i32:$vs0)),
            Concats.VecsToVecPair0>;
  def : Pat<(v256i1 (int_ppc_vsx_assemble_pair v16i8:$vs1, v16i8:$vs0)),
            Concats.VecsToVecPair0>;
  def : Pat<(v4i32 (PPCPairExtractVsx vsrpevenrc:$v, 0)),
            (v4i32 (EXTRACT_SUBREG $v, sub_vsx0))>;
  def : Pat<(v4i32 (PPCPairExtractVsx vsrpevenrc:$v, 1)),
            (v4i32 (EXTRACT_SUBREG $v, sub_vsx1))>;

  let mayLoad = 1, mayStore = 0 in {
    def LXVP : DQForm_XTp5_RA17_MEM<6, 0, (outs vsrprc:$XTp),
                                    (ins (memrix16 $DQ, $RA):$addr), "lxvp $XTp, $addr",
                                    IIC_LdStLFD, []>;
    def LXVPX : XForm_XTp5_XAB5<31, 333, (outs vsrprc:$XTp), (ins (memrr $RA, $RB):$addr),
                                "lxvpx $XTp, $addr", IIC_LdStLFD,
                                []>;
  }

  let mayLoad = 0, mayStore = 1 in {
    def STXVP : DQForm_XTp5_RA17_MEM<6, 1, (outs), (ins vsrprc:$XTp,
                                     (memrix16 $DQ, $RA):$addr), "stxvp $XTp, $addr",
                                     IIC_LdStLFD, []>;
    def STXVPX : XForm_XTp5_XAB5<31, 461, (outs), (ins vsrprc:$XTp, (memrr $RA, $RB):$addr),
                                 "stxvpx $XTp, $addr", IIC_LdStLFD,
                                 []>;
  }
}
let mayLoad = 1, mayStore = 0, Predicates = [PairedVectorMemops, PrefixInstrs, HasP10Vector] in {
  defm PLXVP :
    8LS_DForm_R_XTp5_SI34_MEM_p<58, (outs vsrprc:$XTp), (ins (memri34 $D, $RA):$addr),
                                (ins (memri34_pcrel $D, $RA):$addr),
                                (ins s34imm_pcrel:$D),
                                "plxvp $XTp, $addr", "plxvp $XTp, $D",
                                IIC_LdStLFD>;
}

let mayLoad = 0, mayStore = 1, Predicates = [PairedVectorMemops, PrefixInstrs, HasP10Vector] in {
  defm PSTXVP :
    8LS_DForm_R_XTp5_SI34_MEM_p<62, (outs), (ins vsrprc:$XTp, (memri34 $D, $RA):$addr),
                                (ins vsrprc:$XTp, (memri34_pcrel $D, $RA):$addr),
                                (ins vsrprc:$XTp, s34imm_pcrel:$D),
                                "pstxvp $XTp, $addr", "pstxvp $XTp, $D", IIC_LdStLFD>;
}

let Predicates = [PairedVectorMemops] in {
  // Intrinsics for Paired Vector Loads.
  def : Pat<(v256i1 (int_ppc_vsx_lxvp DQForm:$src)), (LXVP memrix16:$src)>;
  def : Pat<(v256i1 (int_ppc_vsx_lxvp XForm:$src)), (LXVPX XForm:$src)>;
  let Predicates = [PairedVectorMemops, PrefixInstrs, HasP10Vector] in {
    def : Pat<(v256i1 (int_ppc_vsx_lxvp PDForm:$src)), (PLXVP memri34:$src)>;
  }
  // Intrinsics for Paired Vector Stores.
  def : Pat<(int_ppc_vsx_stxvp v256i1:$XSp, DQForm:$dst),
            (STXVP $XSp, memrix16:$dst)>;
  def : Pat<(int_ppc_vsx_stxvp v256i1:$XSp, XForm:$dst),
            (STXVPX $XSp, XForm:$dst)>;
  let Predicates = [PairedVectorMemops, PrefixInstrs, HasP10Vector] in {
    def : Pat<(int_ppc_vsx_stxvp v256i1:$XSp, PDForm:$dst),
              (PSTXVP $XSp, memri34:$dst)>;
  }
}

let Predicates = [IsISA3_1] in {
  def XSCMPEQQP : X_VT5_VA5_VB5<63, 68, "xscmpeqqp", []>;
  def XSCMPGEQP : X_VT5_VA5_VB5<63, 196, "xscmpgeqp", []>;
  def XSCMPGTQP : X_VT5_VA5_VB5<63, 228, "xscmpgtqp", []>;
}

let Predicates = [PCRelativeMemops] in {
  // Load i32
  def : Pat<(i32 (zextloadi1  (PPCmatpcreladdr PCRelForm:$ga))),
            (PLBZpc $ga, 0)>;
  def : Pat<(i32 (extloadi1  (PPCmatpcreladdr PCRelForm:$ga))),
            (PLBZpc $ga, 0)>;
  def : Pat<(i32 (zextloadi8  (PPCmatpcreladdr PCRelForm:$ga))),
            (PLBZpc $ga, 0)>;
  def : Pat<(i32 (extloadi8   (PPCmatpcreladdr PCRelForm:$ga))),
            (PLBZpc $ga, 0)>;
  def : Pat<(i32 (sextloadi16 (PPCmatpcreladdr PCRelForm:$ga))),
            (PLHApc $ga, 0)>;
  def : Pat<(i32 (zextloadi16 (PPCmatpcreladdr PCRelForm:$ga))),
            (PLHZpc $ga, 0)>;
  def : Pat<(i32 (extloadi16 (PPCmatpcreladdr PCRelForm:$ga))),
            (PLHZpc $ga, 0)>;
  def : Pat<(i32 (load (PPCmatpcreladdr PCRelForm:$ga))), (PLWZpc $ga, 0)>;

  // Store i32
  def : Pat<(truncstorei8 i32:$RS, (PPCmatpcreladdr PCRelForm:$ga)),
            (PSTBpc $RS, $ga, 0)>;
  def : Pat<(truncstorei16 i32:$RS, (PPCmatpcreladdr PCRelForm:$ga)),
            (PSTHpc $RS, $ga, 0)>;
  def : Pat<(store i32:$RS, (PPCmatpcreladdr PCRelForm:$ga)),
            (PSTWpc $RS, $ga, 0)>;

  // Load i64
  def : Pat<(i64 (zextloadi1  (PPCmatpcreladdr PCRelForm:$ga))),
            (PLBZ8pc $ga, 0)>;
  def : Pat<(i64 (extloadi1  (PPCmatpcreladdr PCRelForm:$ga))),
            (PLBZ8pc $ga, 0)>;
  def : Pat<(i64 (zextloadi8  (PPCmatpcreladdr PCRelForm:$ga))),
            (PLBZ8pc $ga, 0)>;
  def : Pat<(i64 (extloadi8   (PPCmatpcreladdr PCRelForm:$ga))),
            (PLBZ8pc $ga, 0)>;
  def : Pat<(i64 (sextloadi16 (PPCmatpcreladdr PCRelForm:$ga))),
            (PLHA8pc $ga, 0)>;
  def : Pat<(i64 (zextloadi16 (PPCmatpcreladdr PCRelForm:$ga))),
            (PLHZ8pc $ga, 0)>;
  def : Pat<(i64 (extloadi16 (PPCmatpcreladdr PCRelForm:$ga))),
            (PLHZ8pc $ga, 0)>;
  def : Pat<(i64 (zextloadi32 (PPCmatpcreladdr PCRelForm:$ga))),
            (PLWZ8pc $ga, 0)>;
  def : Pat<(i64 (sextloadi32 (PPCmatpcreladdr PCRelForm:$ga))),
            (PLWA8pc $ga, 0)>;
  def : Pat<(i64 (extloadi32 (PPCmatpcreladdr PCRelForm:$ga))),
            (PLWZ8pc $ga, 0)>;
  def : Pat<(i64 (load (PPCmatpcreladdr PCRelForm:$ga))), (PLDpc $ga, 0)>;

  // Store i64
  def : Pat<(truncstorei8 i64:$RS, (PPCmatpcreladdr PCRelForm:$ga)),
            (PSTB8pc $RS, $ga, 0)>;
  def : Pat<(truncstorei16 i64:$RS, (PPCmatpcreladdr PCRelForm:$ga)),
            (PSTH8pc $RS, $ga, 0)>;
  def : Pat<(truncstorei32 i64:$RS, (PPCmatpcreladdr PCRelForm:$ga)),
            (PSTW8pc $RS, $ga, 0)>;
  def : Pat<(store i64:$RS, (PPCmatpcreladdr PCRelForm:$ga)),
            (PSTDpc $RS, $ga, 0)>;

  // Atomic Load
  def : Pat<(i32 (atomic_load_8 (PPCmatpcreladdr PCRelForm:$ga))),
            (PLBZpc $ga, 0)>;
  def : Pat<(i32 (atomic_load_16 (PPCmatpcreladdr PCRelForm:$ga))),
            (PLHZpc $ga, 0)>;
  def : Pat<(i32 (atomic_load_32 (PPCmatpcreladdr PCRelForm:$ga))),
            (PLWZpc $ga, 0)>;
  def : Pat<(i64 (atomic_load_64 (PPCmatpcreladdr PCRelForm:$ga))),
            (PLDpc $ga, 0)>;

  // Atomic Store
  def : Pat<(atomic_store_8 i32:$RS, (PPCmatpcreladdr PCRelForm:$ga)),
            (PSTBpc $RS, $ga, 0)>;
  def : Pat<(atomic_store_16 i32:$RS, (PPCmatpcreladdr PCRelForm:$ga)),
            (PSTHpc $RS, $ga, 0)>;
  def : Pat<(atomic_store_32 i32:$RS, (PPCmatpcreladdr PCRelForm:$ga)),
            (PSTWpc $RS, $ga, 0)>;
  def : Pat<(atomic_store_8 i64:$RS, (PPCmatpcreladdr PCRelForm:$ga)),
            (PSTB8pc $RS, $ga, 0)>;
  def : Pat<(atomic_store_16 i64:$RS, (PPCmatpcreladdr PCRelForm:$ga)),
            (PSTH8pc $RS, $ga, 0)>;
  def : Pat<(atomic_store_32 i64:$RS, (PPCmatpcreladdr PCRelForm:$ga)),
            (PSTW8pc $RS, $ga, 0)>;
  def : Pat<(atomic_store_64 i64:$RS, (PPCmatpcreladdr PCRelForm:$ga)),
            (PSTDpc $RS, $ga, 0)>;

  // If the PPCmatpcreladdr node is not caught by any other pattern it should be
  // caught here and turned into a paddi instruction to materialize the address.
  def : Pat<(PPCmatpcreladdr PCRelForm:$addr), (PADDI8pc 0, $addr)>;
  // PPCtlsdynamatpcreladdr node is used for TLS dynamic models to materialize
  // tls global address with paddi instruction.
  def : Pat<(PPCtlsdynamatpcreladdr PCRelForm:$addr), (PADDI8pc 0, $addr)>;
  // PPCtlslocalexecmataddr node is used for TLS local exec models to
  // materialize tls global address with paddi instruction.
  def : Pat<(PPCaddTls i64:$in, (PPCtlslocalexecmataddr tglobaltlsaddr:$addr)),
            (PADDI8 $in, $addr)>;
}

let Predicates = [PCRelativeMemops, HasFPU] in {
  // Load f32
  def : Pat<(f32 (load (PPCmatpcreladdr PCRelForm:$addr))), (PLFSpc $addr, 0)>;

  // Store f32
  def : Pat<(store f32:$FRS, (PPCmatpcreladdr PCRelForm:$ga)),
            (PSTFSpc $FRS, $ga, 0)>;

  // Load f64
  def : Pat<(f64 (extloadf32 (PPCmatpcreladdr PCRelForm:$addr))),
            (COPY_TO_REGCLASS (PLFSpc $addr, 0), VSFRC)>;
  def : Pat<(f64 (load (PPCmatpcreladdr PCRelForm:$addr))), (PLFDpc $addr, 0)>;

  // Store f64
  def : Pat<(store f64:$FRS, (PPCmatpcreladdr PCRelForm:$ga)),
            (PSTFDpc $FRS, $ga, 0)>;

  def : Pat<(v4f32 (PPCldvsxlh (PPCmatpcreladdr PCRelForm:$addr))),
            (SUBREG_TO_REG (i64 1), (PLFDpc $addr, 0), sub_64)>;
}

let Predicates = [PCRelativeMemops, HasP10Vector] in {
  // Load f128
  def : Pat<(f128 (load (PPCmatpcreladdr PCRelForm:$addr))),
            (COPY_TO_REGCLASS (PLXVpc $addr, 0), VRRC)>;

  // Store f128
  def : Pat<(store f128:$XS, (PPCmatpcreladdr PCRelForm:$ga)),
            (PSTXVpc (COPY_TO_REGCLASS $XS, VSRC), $ga, 0)>;

  // Load v4i32
  def : Pat<(v4i32 (load (PPCmatpcreladdr PCRelForm:$addr))), (PLXVpc $addr, 0)>;

  // Store v4i32
  def : Pat<(store v4i32:$XS, (PPCmatpcreladdr PCRelForm:$ga)),
            (PSTXVpc $XS, $ga, 0)>;

  // Load v2i64
  def : Pat<(v2i64 (load (PPCmatpcreladdr PCRelForm:$addr))), (PLXVpc $addr, 0)>;

  // Store v2i64
  def : Pat<(store v2i64:$XS, (PPCmatpcreladdr PCRelForm:$ga)),
            (PSTXVpc $XS, $ga, 0)>;

  // Load v4f32
  def : Pat<(v4f32 (load (PPCmatpcreladdr PCRelForm:$addr))), (PLXVpc $addr, 0)>;

  // Store v4f32
  def : Pat<(store v4f32:$XS, (PPCmatpcreladdr PCRelForm:$ga)),
            (PSTXVpc $XS, $ga, 0)>;

  // Load v2f64
  def : Pat<(v2f64 (load (PPCmatpcreladdr PCRelForm:$addr))), (PLXVpc $addr, 0)>;

  // Store v2f64
  def : Pat<(store v2f64:$XS, (PPCmatpcreladdr PCRelForm:$ga)),
            (PSTXVpc $XS, $ga, 0)>;

  // Special Cases For PPCstore_scal_int_from_vsr
  def : Pat<(PPCstore_scal_int_from_vsr f64:$src, (PPCmatpcreladdr PCRelForm:$dst), 8),
            (PSTXSDpc $src, $dst, 0)>;
  def : Pat<(PPCstore_scal_int_from_vsr f128:$src, (PPCmatpcreladdr PCRelForm:$dst), 8),
            (PSTXSDpc (COPY_TO_REGCLASS $src, VFRC), $dst, 0)>;
}

// XXSPLTIW/DP/32DX need extra flags to make sure the compiler does not attempt
// to spill part of the instruction when the values are similar.
let isReMaterializable = 1, isAsCheapAsAMove = 1, isMoveImm = 1, Predicates = [PrefixInstrs] in {
  def XXSPLTIW : 8RR_DForm_IMM32_XT6<32, 3, (outs vsrc:$XT),
                                     (ins i32imm:$IMM32),
                                     "xxspltiw $XT, $IMM32", IIC_VecGeneral,
                                     []>;
  def XXSPLTIDP : 8RR_DForm_IMM32_XT6<32, 2, (outs vsrc:$XT),
                                      (ins i32imm:$IMM32),
                                      "xxspltidp $XT, $IMM32", IIC_VecGeneral,
                                      [(set v2f64:$XT,
                                            (PPCxxspltidp i32:$IMM32))]>;
  def XXSPLTI32DX :
      8RR_DForm_IMM32_XT6_IX<32, 0, (outs vsrc:$XT),
                             (ins vsrc:$XTi, u1imm:$IX, i32imm:$IMM32),
                             "xxsplti32dx $XT, $IX, $IMM32", IIC_VecGeneral,
                             [(set v2i64:$XT,
                                   (PPCxxsplti32dx v2i64:$XTi, i32:$IX,
                                                   i32:$IMM32))]>,
                             RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">;
}

let Predicates = [IsISA3_1] in {
  def SETBC : XForm_XT5_BI5<31, 384, (outs gprc:$RST), (ins crbitrc:$BI),
                            "setbc $RST, $BI", IIC_IntCompare, []>,
                            SExt32To64, ZExt32To64;
  def SETBCR : XForm_XT5_BI5<31, 416, (outs gprc:$RST), (ins crbitrc:$BI),
                             "setbcr $RST, $BI", IIC_IntCompare, []>,
                             SExt32To64, ZExt32To64;
  def SETNBC : XForm_XT5_BI5<31, 448, (outs gprc:$RST), (ins crbitrc:$BI),
                             "setnbc $RST, $BI", IIC_IntCompare, []>,
                             SExt32To64;
  def SETNBCR : XForm_XT5_BI5<31, 480, (outs gprc:$RST), (ins crbitrc:$BI),
                              "setnbcr $RST, $BI", IIC_IntCompare, []>,
                              SExt32To64;

  let Interpretation64Bit = 1, isCodeGenOnly = 1 in {
    def SETBC8 : XForm_XT5_BI5<31, 384, (outs g8rc:$RST), (ins crbitrc:$BI),
                               "setbc $RST, $BI", IIC_IntCompare, []>,
                               SExt32To64, ZExt32To64;
    def SETBCR8 : XForm_XT5_BI5<31, 416, (outs g8rc:$RST), (ins crbitrc:$BI),
                                "setbcr $RST, $BI", IIC_IntCompare, []>,
                                SExt32To64, ZExt32To64;
    def SETNBC8 : XForm_XT5_BI5<31, 448, (outs g8rc:$RST), (ins crbitrc:$BI),
                                "setnbc $RST, $BI", IIC_IntCompare, []>,
                                SExt32To64;
    def SETNBCR8 : XForm_XT5_BI5<31, 480, (outs g8rc:$RST), (ins crbitrc:$BI),
                                 "setnbcr $RST, $BI", IIC_IntCompare, []>,
                                 SExt32To64;
  }

  def VSLDBI : VNForm_VTAB5_SD3<22, 0, (outs vrrc:$VRT),
                                (ins vrrc:$VRA, vrrc:$VRB, u3imm:$SD),
                                "vsldbi $VRT, $VRA, $VRB, $SD",
                                IIC_VecGeneral,
                                [(set v16i8:$VRT,
                                      (int_ppc_altivec_vsldbi v16i8:$VRA,
                                                              v16i8:$VRB,
                                                              timm:$SD))]>;
  def VSRDBI : VNForm_VTAB5_SD3<22, 1, (outs vrrc:$VRT),
                                (ins vrrc:$VRA, vrrc:$VRB, u3imm:$SD),
                                "vsrdbi $VRT, $VRA, $VRB, $SD",
                                IIC_VecGeneral,
                                [(set v16i8:$VRT,
                                      (int_ppc_altivec_vsrdbi v16i8:$VRA,
                                                              v16i8:$VRB, 
                                                              timm:$SD))]>;
  defm VSTRIBR : VXForm_VTB5_RCr<13, 1, (outs vrrc:$VT), (ins vrrc:$VB),
                                 "vstribr", "$VT, $VB", IIC_VecGeneral,
				 [(set v16i8:$VT,
                                       (int_ppc_altivec_vstribr v16i8:$VB))]>;
  defm VSTRIBL : VXForm_VTB5_RCr<13, 0, (outs vrrc:$VT), (ins vrrc:$VB),
                                 "vstribl", "$VT, $VB", IIC_VecGeneral,
                                 [(set v16i8:$VT,
                                       (int_ppc_altivec_vstribl v16i8:$VB))]>;
  defm VSTRIHR : VXForm_VTB5_RCr<13, 3, (outs vrrc:$VT), (ins vrrc:$VB),
                                 "vstrihr", "$VT, $VB", IIC_VecGeneral,
                                 [(set v8i16:$VT,
                                       (int_ppc_altivec_vstrihr v8i16:$VB))]>;
  defm VSTRIHL : VXForm_VTB5_RCr<13, 2, (outs vrrc:$VT), (ins vrrc:$VB),
                                 "vstrihl", "$VT, $VB", IIC_VecGeneral,
                                 [(set v8i16:$VT,
                                       (int_ppc_altivec_vstrihl v8i16:$VB))]>;
  def VINSW :
    VXForm_1<207, (outs vrrc:$VD), (ins vrrc:$VDi, u4imm:$VA, gprc:$VB),
             "vinsw $VD, $VB, $VA", IIC_VecGeneral,
             [(set v4i32:$VD,
                   (int_ppc_altivec_vinsw v4i32:$VDi, i32:$VB, timm:$VA))]>,
             RegConstraint<"$VDi = $VD">, NoEncode<"$VDi">;
  def VINSD :
    VXForm_1<463, (outs vrrc:$VD), (ins vrrc:$VDi, u4imm:$VA, g8rc:$VB),
             "vinsd $VD, $VB, $VA", IIC_VecGeneral,
             [(set v2i64:$VD,
                   (int_ppc_altivec_vinsd v2i64:$VDi, i64:$VB, timm:$VA))]>,
             RegConstraint<"$VDi = $VD">, NoEncode<"$VDi">;
  def VINSBVLX :
    VXForm_VTB5_RA5_ins<15, "vinsbvlx",
                        [(set v16i8:$VD,
                              (int_ppc_altivec_vinsbvlx v16i8:$VDi, i32:$VA,
                                                        v16i8:$VB))]>;
  def VINSBVRX :
    VXForm_VTB5_RA5_ins<271, "vinsbvrx",
                        [(set v16i8:$VD,
                              (int_ppc_altivec_vinsbvrx v16i8:$VDi, i32:$VA,
                                                        v16i8:$VB))]>;
  def VINSHVLX :
    VXForm_VTB5_RA5_ins<79, "vinshvlx",
                        [(set v8i16:$VD,
                              (int_ppc_altivec_vinshvlx v8i16:$VDi, i32:$VA,
                                                        v8i16:$VB))]>;
  def VINSHVRX :
    VXForm_VTB5_RA5_ins<335, "vinshvrx",
                        [(set v8i16:$VD,
                              (int_ppc_altivec_vinshvrx v8i16:$VDi, i32:$VA,
                                                        v8i16:$VB))]>;
  def VINSWVLX :
    VXForm_VTB5_RA5_ins<143, "vinswvlx",
                        [(set v4i32:$VD,
                              (int_ppc_altivec_vinswvlx v4i32:$VDi, i32:$VA,
                                                        v4i32:$VB))]>;
  def VINSWVRX :
    VXForm_VTB5_RA5_ins<399, "vinswvrx",
                        [(set v4i32:$VD,
                              (int_ppc_altivec_vinswvrx v4i32:$VDi, i32:$VA,
                                                        v4i32:$VB))]>;
  def VINSBLX :
    VXForm_VRT5_RAB5_ins<527, "vinsblx",
                         [(set v16i8:$VD,
                               (int_ppc_altivec_vinsblx v16i8:$VDi, i32:$VA,
                                                        i32:$VB))]>;
  def VINSBRX :
    VXForm_VRT5_RAB5_ins<783, "vinsbrx",
                         [(set v16i8:$VD,
                               (int_ppc_altivec_vinsbrx v16i8:$VDi, i32:$VA,
                                                        i32:$VB))]>;
  def VINSHLX :
    VXForm_VRT5_RAB5_ins<591, "vinshlx",
                         [(set v8i16:$VD,
                               (int_ppc_altivec_vinshlx v8i16:$VDi, i32:$VA,
                                                        i32:$VB))]>;
  def VINSHRX :
    VXForm_VRT5_RAB5_ins<847, "vinshrx",
                         [(set v8i16:$VD,
                               (int_ppc_altivec_vinshrx v8i16:$VDi, i32:$VA,
                                                        i32:$VB))]>;
  def VINSWLX :
    VXForm_VRT5_RAB5_ins<655, "vinswlx",
                         [(set v4i32:$VD,
                               (int_ppc_altivec_vinswlx v4i32:$VDi, i32:$VA,
                                                        i32:$VB))]>;
  def VINSWRX :
    VXForm_VRT5_RAB5_ins<911, "vinswrx",
                         [(set v4i32:$VD,
                               (int_ppc_altivec_vinswrx v4i32:$VDi, i32:$VA,
                                                        i32:$VB))]>;
  def VINSDLX :
    VXForm_1<719, (outs vrrc:$VD), (ins vrrc:$VDi, g8rc:$VA, g8rc:$VB),
             "vinsdlx $VD, $VA, $VB", IIC_VecGeneral,
              [(set v2i64:$VD,
                    (int_ppc_altivec_vinsdlx v2i64:$VDi, i64:$VA, i64:$VB))]>,
              RegConstraint<"$VDi = $VD">, NoEncode<"$VDi">;
  def VINSDRX :
    VXForm_1<975, (outs vrrc:$VD), (ins vrrc:$VDi, g8rc:$VA, g8rc:$VB),
             "vinsdrx $VD, $VA, $VB", IIC_VecGeneral,
              [(set v2i64:$VD,
                    (int_ppc_altivec_vinsdrx v2i64:$VDi, i64:$VA, i64:$VB))]>,
              RegConstraint<"$VDi = $VD">, NoEncode<"$VDi">;
  def VEXTRACTBM : VXForm_RD5_XO5_RS5<1602, 8, (outs gprc:$VD), (ins vrrc:$VB),
                                      "vextractbm $VD, $VB", IIC_VecGeneral,
                                      [(set i32:$VD,
                                      (int_ppc_altivec_vextractbm v16i8:$VB))]>,
                                      ZExt32To64;
  def VEXTRACTHM : VXForm_RD5_XO5_RS5<1602, 9, (outs gprc:$VD), (ins vrrc:$VB),
                                      "vextracthm $VD, $VB", IIC_VecGeneral,
                                      [(set i32:$VD,
                                      (int_ppc_altivec_vextracthm v8i16:$VB))]>,
                                      ZExt32To64;
  def VEXTRACTWM : VXForm_RD5_XO5_RS5<1602, 10, (outs gprc:$VD), (ins vrrc:$VB),
                                      "vextractwm $VD, $VB", IIC_VecGeneral,
                                      [(set i32:$VD,
                                      (int_ppc_altivec_vextractwm v4i32:$VB))]>,
                                      ZExt32To64;
  def VEXTRACTDM : VXForm_RD5_XO5_RS5<1602, 11, (outs gprc:$VD), (ins vrrc:$VB),
                                      "vextractdm $VD, $VB", IIC_VecGeneral,
                                      [(set i32:$VD,
                                      (int_ppc_altivec_vextractdm v2i64:$VB))]>,
                                      ZExt32To64;
  def VEXTRACTQM : VXForm_RD5_XO5_RS5<1602, 12, (outs gprc:$VD), (ins vrrc:$VB),
                                      "vextractqm $VD, $VB", IIC_VecGeneral,
                                      [(set i32:$VD,
                                      (int_ppc_altivec_vextractqm v1i128:$VB))]>;
  def VEXPANDBM : VXForm_RD5_XO5_RS5<1602, 0, (outs vrrc:$VD), (ins vrrc:$VB),
                                     "vexpandbm $VD, $VB", IIC_VecGeneral,
                                     [(set v16i8:$VD, (int_ppc_altivec_vexpandbm
                                           v16i8:$VB))]>;
  def VEXPANDHM : VXForm_RD5_XO5_RS5<1602, 1, (outs vrrc:$VD), (ins vrrc:$VB),
                                     "vexpandhm $VD, $VB", IIC_VecGeneral,
                                     [(set v8i16:$VD, (int_ppc_altivec_vexpandhm
                                           v8i16:$VB))]>;
  def VEXPANDWM : VXForm_RD5_XO5_RS5<1602, 2, (outs vrrc:$VD), (ins vrrc:$VB),
                                     "vexpandwm $VD, $VB", IIC_VecGeneral,
                                     [(set v4i32:$VD, (int_ppc_altivec_vexpandwm
                                           v4i32:$VB))]>;
  def VEXPANDDM : VXForm_RD5_XO5_RS5<1602, 3, (outs vrrc:$VD), (ins vrrc:$VB),
                                     "vexpanddm $VD, $VB", IIC_VecGeneral,
                                     [(set v2i64:$VD, (int_ppc_altivec_vexpanddm
                                           v2i64:$VB))]>;
  def VEXPANDQM : VXForm_RD5_XO5_RS5<1602, 4, (outs vrrc:$VD), (ins vrrc:$VB),
                                     "vexpandqm $VD, $VB", IIC_VecGeneral,
                                     [(set v1i128:$VD, (int_ppc_altivec_vexpandqm
                                           v1i128:$VB))]>;
  def MTVSRBM : VXForm_RD5_XO5_RS5<1602, 16, (outs vrrc:$VD), (ins g8rc:$VB),
                                   "mtvsrbm $VD, $VB", IIC_VecGeneral,
                                   [(set v16i8:$VD,
                                         (int_ppc_altivec_mtvsrbm i64:$VB))]>;
  def MTVSRHM : VXForm_RD5_XO5_RS5<1602, 17, (outs vrrc:$VD), (ins g8rc:$VB),
                                   "mtvsrhm $VD, $VB", IIC_VecGeneral,
                                   [(set v8i16:$VD,
                                         (int_ppc_altivec_mtvsrhm i64:$VB))]>;
  def MTVSRWM : VXForm_RD5_XO5_RS5<1602, 18, (outs vrrc:$VD), (ins g8rc:$VB),
                                   "mtvsrwm $VD, $VB", IIC_VecGeneral,
                                   [(set v4i32:$VD,
                                         (int_ppc_altivec_mtvsrwm i64:$VB))]>;
  def MTVSRDM : VXForm_RD5_XO5_RS5<1602, 19, (outs vrrc:$VD), (ins g8rc:$VB),
                                   "mtvsrdm $VD, $VB", IIC_VecGeneral,
                                   [(set v2i64:$VD,
                                         (int_ppc_altivec_mtvsrdm i64:$VB))]>;
  def MTVSRQM : VXForm_RD5_XO5_RS5<1602, 20, (outs vrrc:$VD), (ins g8rc:$VB),
                                   "mtvsrqm $VD, $VB", IIC_VecGeneral,
                                   [(set v1i128:$VD,
                                         (int_ppc_altivec_mtvsrqm i64:$VB))]>;
  def MTVSRBMI : DXForm<4, 10, (outs vrrc:$RT), (ins u16imm64:$D),
                        "mtvsrbmi $RT, $D", IIC_VecGeneral,
                        [(set v16i8:$RT,
                              (int_ppc_altivec_mtvsrbm imm:$D))]>;
  def VCNTMBB : VXForm_RD5_MP_VB5<1602, 12, (outs g8rc:$RD),
                                  (ins vrrc:$VB, u1imm:$MP),
                                  "vcntmbb $RD, $VB, $MP", IIC_VecGeneral,
                                  [(set i64:$RD, (int_ppc_altivec_vcntmbb
                                        v16i8:$VB, timm:$MP))]>;
  def VCNTMBH : VXForm_RD5_MP_VB5<1602, 13, (outs g8rc:$RD),
                                  (ins vrrc:$VB, u1imm:$MP),
                                  "vcntmbh $RD, $VB, $MP", IIC_VecGeneral,
                                  [(set i64:$RD, (int_ppc_altivec_vcntmbh
                                        v8i16:$VB, timm:$MP))]>;
  def VCNTMBW : VXForm_RD5_MP_VB5<1602, 14, (outs g8rc:$RD),
                                  (ins vrrc:$VB, u1imm:$MP),
                                  "vcntmbw $RD, $VB, $MP", IIC_VecGeneral,
                                  [(set i64:$RD, (int_ppc_altivec_vcntmbw
                                        v4i32:$VB, timm:$MP))]>;
  def VCNTMBD : VXForm_RD5_MP_VB5<1602, 15, (outs g8rc:$RD),
                                  (ins vrrc:$VB, u1imm:$MP),
                                  "vcntmbd $RD, $VB, $MP", IIC_VecGeneral,
                                  [(set i64:$RD, (int_ppc_altivec_vcntmbd
                                        v2i64:$VB, timm:$MP))]>;
  def VEXTDUBVLX : VAForm_1a<24, (outs vrrc:$RT),
                             (ins vrrc:$RA, vrrc:$RB, gprc:$RC),
                             "vextdubvlx $RT, $RA, $RB, $RC",
                             IIC_VecGeneral,
                             [(set v2i64:$RT,
                                   (int_ppc_altivec_vextdubvlx v16i8:$RA,
                                                               v16i8:$RB,
                                                               i32:$RC))]>;
  def VEXTDUBVRX : VAForm_1a<25, (outs vrrc:$RT),
                             (ins vrrc:$RA, vrrc:$RB, gprc:$RC),
                             "vextdubvrx $RT, $RA, $RB, $RC",
                             IIC_VecGeneral,
                             [(set v2i64:$RT,
                                   (int_ppc_altivec_vextdubvrx v16i8:$RA,
                                                               v16i8:$RB,
                                                               i32:$RC))]>;
  def VEXTDUHVLX : VAForm_1a<26, (outs vrrc:$RT),
                             (ins vrrc:$RA, vrrc:$RB, gprc:$RC),
                             "vextduhvlx $RT, $RA, $RB, $RC",
                             IIC_VecGeneral,
                             [(set v2i64:$RT,
                                   (int_ppc_altivec_vextduhvlx v8i16:$RA,
                                                               v8i16:$RB,
                                                               i32:$RC))]>;
  def VEXTDUHVRX : VAForm_1a<27, (outs vrrc:$RT),
                             (ins vrrc:$RA, vrrc:$RB, gprc:$RC),
                             "vextduhvrx $RT, $RA, $RB, $RC",
                             IIC_VecGeneral,
                             [(set v2i64:$RT,
                                   (int_ppc_altivec_vextduhvrx v8i16:$RA,
                                                               v8i16:$RB,
                                                               i32:$RC))]>;
  def VEXTDUWVLX : VAForm_1a<28, (outs vrrc:$RT),
                             (ins vrrc:$RA, vrrc:$RB, gprc:$RC),
                             "vextduwvlx $RT, $RA, $RB, $RC",
                             IIC_VecGeneral,
                             [(set v2i64:$RT,
                                   (int_ppc_altivec_vextduwvlx v4i32:$RA,
                                                               v4i32:$RB,
                                                               i32:$RC))]>;
  def VEXTDUWVRX : VAForm_1a<29, (outs vrrc:$RT),
                             (ins vrrc:$RA, vrrc:$RB, gprc:$RC),
                             "vextduwvrx $RT, $RA, $RB, $RC",
                             IIC_VecGeneral,
                             [(set v2i64:$RT,
                                   (int_ppc_altivec_vextduwvrx v4i32:$RA,
                                                               v4i32:$RB,
                                                               i32:$RC))]>;
  def VEXTDDVLX : VAForm_1a<30, (outs vrrc:$RT),
                            (ins vrrc:$RA, vrrc:$RB, gprc:$RC),
                            "vextddvlx $RT, $RA, $RB, $RC",
                            IIC_VecGeneral,
                            [(set v2i64:$RT,
                                  (int_ppc_altivec_vextddvlx v2i64:$RA,
                                                             v2i64:$RB,
                                                             i32:$RC))]>;
  def VEXTDDVRX : VAForm_1a<31, (outs vrrc:$RT),
                            (ins vrrc:$RA, vrrc:$RB, gprc:$RC),
                            "vextddvrx $RT, $RA, $RB, $RC",
                            IIC_VecGeneral,
                            [(set v2i64:$RT,
                                  (int_ppc_altivec_vextddvrx v2i64:$RA,
                                                             v2i64:$RB,
                                                             i32:$RC))]>;
   def VPDEPD : VXForm_1<1485, (outs vrrc:$VD), (ins vrrc:$VA, vrrc:$VB),
                         "vpdepd $VD, $VA, $VB", IIC_VecGeneral,
                         [(set v2i64:$VD,
                         (int_ppc_altivec_vpdepd v2i64:$VA, v2i64:$VB))]>;
   def VPEXTD : VXForm_1<1421, (outs vrrc:$VD), (ins vrrc:$VA, vrrc:$VB),
                         "vpextd $VD, $VA, $VB", IIC_VecGeneral,
                         [(set v2i64:$VD,
                         (int_ppc_altivec_vpextd v2i64:$VA, v2i64:$VB))]>;
   def PDEPD : XForm_6<31, 156, (outs g8rc:$RA), (ins g8rc:$RST, g8rc:$RB),
                       "pdepd $RA, $RST, $RB", IIC_IntGeneral,
                       [(set i64:$RA, (int_ppc_pdepd i64:$RST, i64:$RB))]>;
   def PEXTD : XForm_6<31, 188, (outs g8rc:$RA), (ins g8rc:$RST, g8rc:$RB),
                       "pextd $RA, $RST, $RB", IIC_IntGeneral,
                       [(set i64:$RA, (int_ppc_pextd i64:$RST, i64:$RB))]>;
   def VCFUGED : VXForm_1<1357, (outs vrrc:$VD), (ins vrrc:$VA, vrrc:$VB),
                          "vcfuged $VD, $VA, $VB", IIC_VecGeneral,
                          [(set v2i64:$VD,
                          (int_ppc_altivec_vcfuged v2i64:$VA, v2i64:$VB))]>;
   def VGNB : VXForm_RD5_N3_VB5<1228, (outs g8rc:$RD), (ins vrrc:$VB, u3imm:$N),
                                "vgnb $RD, $VB, $N", IIC_VecGeneral,
                                [(set i64:$RD,
                                (int_ppc_altivec_vgnb v1i128:$VB, timm:$N))]>;
   def CFUGED : XForm_6<31, 220, (outs g8rc:$RA), (ins g8rc:$RST, g8rc:$RB),
                        "cfuged $RA, $RST, $RB", IIC_IntGeneral,
                        [(set i64:$RA, (int_ppc_cfuged i64:$RST, i64:$RB))]>;
   def XXEVAL :
     8RR_XX4Form_IMM8_XTAB6<34, 1, (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB,
                            vsrc:$XC, u8imm:$IMM),
                            "xxeval $XT, $XA, $XB, $XC, $IMM", IIC_VecGeneral,
                            [(set v2i64:$XT, (int_ppc_vsx_xxeval v2i64:$XA,
                                  v2i64:$XB, v2i64:$XC, timm:$IMM))]>;
   def VCLZDM : VXForm_1<1924, (outs vrrc:$VD), (ins vrrc:$VA, vrrc:$VB),
                         "vclzdm $VD, $VA, $VB", IIC_VecGeneral,
                         [(set v2i64:$VD,
                         (int_ppc_altivec_vclzdm v2i64:$VA, v2i64:$VB))]>;
   def VCTZDM : VXForm_1<1988, (outs vrrc:$VD), (ins vrrc:$VA, vrrc:$VB),
                         "vctzdm $VD, $VA, $VB", IIC_VecGeneral,
                         [(set v2i64:$VD,
                         (int_ppc_altivec_vctzdm v2i64:$VA, v2i64:$VB))]>;
   def CNTLZDM : XForm_6<31, 59, (outs g8rc:$RA), (ins g8rc:$RST, g8rc:$RB),
                         "cntlzdm $RA, $RST, $RB", IIC_IntGeneral,
                         [(set i64:$RA,
                         (int_ppc_cntlzdm i64:$RST, i64:$RB))]>;
   def CNTTZDM : XForm_6<31, 571, (outs g8rc:$RA), (ins g8rc:$RST, g8rc:$RB),
                         "cnttzdm $RA, $RST, $RB", IIC_IntGeneral,
                         [(set i64:$RA,
                         (int_ppc_cnttzdm i64:$RST, i64:$RB))]>;
   def XXGENPCVBM :
     XForm_XT6_IMM5_VB5<60, 916, (outs vsrc:$XT), (ins vrrc:$VRB, s5imm:$IMM),
                        "xxgenpcvbm $XT, $VRB, $IMM", IIC_VecGeneral, []>;
   def XXGENPCVHM :
     XForm_XT6_IMM5_VB5<60, 917, (outs vsrc:$XT), (ins vrrc:$VRB, s5imm:$IMM),
                        "xxgenpcvhm $XT, $VRB, $IMM", IIC_VecGeneral, []>;
   def XXGENPCVWM :
     XForm_XT6_IMM5_VB5<60, 948, (outs vsrc:$XT), (ins vrrc:$VRB, s5imm:$IMM),
                        "xxgenpcvwm $XT, $VRB, $IMM", IIC_VecGeneral, []>;
   def XXGENPCVDM :
     XForm_XT6_IMM5_VB5<60, 949, (outs vsrc:$XT), (ins vrrc:$VRB, s5imm:$IMM),
                        "xxgenpcvdm $XT, $VRB, $IMM", IIC_VecGeneral, []>;
   def VCLRLB : VXForm_1<397, (outs vrrc:$VD), (ins vrrc:$VA, gprc:$VB),
                         "vclrlb $VD, $VA, $VB", IIC_VecGeneral,
                         [(set v16i8:$VD,
                               (int_ppc_altivec_vclrlb v16i8:$VA, i32:$VB))]>;
   def VCLRRB : VXForm_1<461, (outs vrrc:$VD), (ins vrrc:$VA, gprc:$VB),
                         "vclrrb $VD, $VA, $VB", IIC_VecGeneral,
                         [(set v16i8:$VD,
                               (int_ppc_altivec_vclrrb v16i8:$VA, i32:$VB))]>;
  def VMULLD : VXForm_1<457, (outs vrrc:$VD), (ins vrrc:$VA, vrrc:$VB),
                        "vmulld $VD, $VA, $VB", IIC_VecGeneral,
                        [(set v2i64:$VD, (mul v2i64:$VA, v2i64:$VB))]>;
  def VMULHSW : VXForm_1<905, (outs vrrc:$VD), (ins vrrc:$VA, vrrc:$VB),
                         "vmulhsw $VD, $VA, $VB", IIC_VecGeneral,
                         [(set v4i32:$VD, (mulhs v4i32:$VA, v4i32:$VB))]>;
  def VMULHUW : VXForm_1<649, (outs vrrc:$VD), (ins vrrc:$VA, vrrc:$VB),
                         "vmulhuw $VD, $VA, $VB", IIC_VecGeneral,
                         [(set v4i32:$VD, (mulhu v4i32:$VA, v4i32:$VB))]>;
  def VMULHSD : VXForm_1<969, (outs vrrc:$VD), (ins vrrc:$VA, vrrc:$VB),
                         "vmulhsd $VD, $VA, $VB", IIC_VecGeneral,
                         [(set v2i64:$VD, (mulhs v2i64:$VA, v2i64:$VB))]>;
  def VMULHUD : VXForm_1<713, (outs vrrc:$VD), (ins vrrc:$VA, vrrc:$VB),
                         "vmulhud $VD, $VA, $VB", IIC_VecGeneral,
                         [(set v2i64:$VD, (mulhu v2i64:$VA, v2i64:$VB))]>;
  def VMODSW : VXForm_1<1931, (outs vrrc:$VD), (ins vrrc:$VA, vrrc:$VB),
                        "vmodsw $VD, $VA, $VB", IIC_VecGeneral,
                        [(set v4i32:$VD, (srem v4i32:$VA, v4i32:$VB))]>;
  def VMODUW : VXForm_1<1675, (outs vrrc:$VD), (ins vrrc:$VA, vrrc:$VB),
                        "vmoduw $VD, $VA, $VB", IIC_VecGeneral,
                        [(set v4i32:$VD, (urem v4i32:$VA, v4i32:$VB))]>;
  def VMODSD : VXForm_1<1995, (outs vrrc:$VD), (ins vrrc:$VA, vrrc:$VB),
                        "vmodsd $VD, $VA, $VB", IIC_VecGeneral,
                        [(set v2i64:$VD, (srem v2i64:$VA, v2i64:$VB))]>;
  def VMODUD : VXForm_1<1739, (outs vrrc:$VD), (ins vrrc:$VA, vrrc:$VB),
                        "vmodud $VD, $VA, $VB", IIC_VecGeneral,
                        [(set v2i64:$VD, (urem v2i64:$VA, v2i64:$VB))]>;
  def VDIVSW : VXForm_1<395, (outs vrrc:$VD), (ins vrrc:$VA, vrrc:$VB),
                        "vdivsw $VD, $VA, $VB", IIC_VecGeneral,
                        [(set v4i32:$VD, (sdiv v4i32:$VA, v4i32:$VB))]>;
  def VDIVUW : VXForm_1<139, (outs vrrc:$VD), (ins vrrc:$VA, vrrc:$VB),
                        "vdivuw $VD, $VA, $VB", IIC_VecGeneral,
                        [(set v4i32:$VD, (udiv v4i32:$VA, v4i32:$VB))]>;
  def VDIVSD : VXForm_1<459, (outs vrrc:$VD), (ins vrrc:$VA, vrrc:$VB),
                        "vdivsd $VD, $VA, $VB", IIC_VecGeneral,
                        [(set v2i64:$VD, (sdiv v2i64:$VA, v2i64:$VB))]>;
  def VDIVUD : VXForm_1<203, (outs vrrc:$VD), (ins vrrc:$VA, vrrc:$VB),
                        "vdivud $VD, $VA, $VB", IIC_VecGeneral,
                        [(set v2i64:$VD, (udiv v2i64:$VA, v2i64:$VB))]>;
  def VDIVESW : VXForm_1<907, (outs vrrc:$VD), (ins vrrc:$VA, vrrc:$VB),
                         "vdivesw $VD, $VA, $VB", IIC_VecGeneral,
                         [(set v4i32:$VD, (int_ppc_altivec_vdivesw v4i32:$VA,
                               v4i32:$VB))]>;
  def VDIVEUW : VXForm_1<651, (outs vrrc:$VD), (ins vrrc:$VA, vrrc:$VB),
                         "vdiveuw $VD, $VA, $VB", IIC_VecGeneral,
                         [(set v4i32:$VD, (int_ppc_altivec_vdiveuw v4i32:$VA,
                               v4i32:$VB))]>;
  def VDIVESD : VXForm_1<971, (outs vrrc:$VD), (ins vrrc:$VA, vrrc:$VB),
                         "vdivesd $VD, $VA, $VB", IIC_VecGeneral,
                         [(set v2i64:$VD, (int_ppc_altivec_vdivesd v2i64:$VA,
                               v2i64:$VB))]>;
  def VDIVEUD : VXForm_1<715, (outs vrrc:$VD), (ins vrrc:$VA, vrrc:$VB),
                         "vdiveud $VD, $VA, $VB", IIC_VecGeneral,
                         [(set v2i64:$VD, (int_ppc_altivec_vdiveud v2i64:$VA,
                               v2i64:$VB))]>;
  def XVTLSBB : XX2_BF3_XO5_XB6_XO9<60, 2, 475, (outs crrc:$BF), (ins vsrc:$XB),
                                    "xvtlsbb $BF, $XB", IIC_VecGeneral, []>;
  def BRH : XForm_11<31, 219, (outs gprc:$RA), (ins gprc:$RST),
                     "brh $RA, $RST", IIC_IntRotate, []>;
  def BRW : XForm_11<31, 155, (outs gprc:$RA), (ins gprc:$RST),
                     "brw $RA, $RST", IIC_IntRotate,
                     [(set i32:$RA, (bswap i32:$RST))]>;
  let isCodeGenOnly = 1 in {
    def BRH8 : XForm_11<31, 219, (outs g8rc:$RA), (ins g8rc:$RST),
                        "brh $RA, $RST", IIC_IntRotate, []>;
    def BRW8 : XForm_11<31, 155, (outs g8rc:$RA), (ins g8rc:$RST),
                        "brw $RA, $RST", IIC_IntRotate, []>;
  }
  def BRD : XForm_11<31, 187, (outs g8rc:$RA), (ins g8rc:$RST),
                     "brd $RA, $RST", IIC_IntRotate,
                     [(set i64:$RA, (bswap i64:$RST))]>;

  // The XFormMemOp flag for the following 8 instructions is set on
  // the instruction format.
  let mayLoad = 1, mayStore = 0 in {
    def LXVRBX : X_XT6_RA5_RB5<31, 13, "lxvrbx", vsrc, []>;
    def LXVRHX : X_XT6_RA5_RB5<31, 45, "lxvrhx", vsrc, []>;
    def LXVRWX : X_XT6_RA5_RB5<31, 77, "lxvrwx", vsrc, []>;
    def LXVRDX : X_XT6_RA5_RB5<31, 109, "lxvrdx", vsrc, []>;
  }

  let mayLoad = 0, mayStore = 1 in {
    def STXVRBX : X_XS6_RA5_RB5<31, 141, "stxvrbx", vsrc, []>;
    def STXVRHX : X_XS6_RA5_RB5<31, 173, "stxvrhx", vsrc, []>;
    def STXVRWX : X_XS6_RA5_RB5<31, 205, "stxvrwx", vsrc, []>;
    def STXVRDX : X_XS6_RA5_RB5<31, 237, "stxvrdx", vsrc, []>;
  }

  def VMULESD : VXForm_1<968, (outs vrrc:$VD), (ins vrrc:$VA, vrrc:$VB),
                         "vmulesd $VD, $VA, $VB", IIC_VecGeneral,
                         [(set v1i128:$VD, (int_ppc_altivec_vmulesd v2i64:$VA,
                               v2i64:$VB))]>;
  def VMULEUD : VXForm_1<712, (outs vrrc:$VD), (ins vrrc:$VA, vrrc:$VB),
                         "vmuleud $VD, $VA, $VB", IIC_VecGeneral,
                         [(set v1i128:$VD, (int_ppc_altivec_vmuleud v2i64:$VA,
                               v2i64:$VB))]>;
  def VMULOSD : VXForm_1<456, (outs vrrc:$VD), (ins vrrc:$VA, vrrc:$VB),
                         "vmulosd $VD, $VA, $VB", IIC_VecGeneral,
                         [(set v1i128:$VD, (int_ppc_altivec_vmulosd v2i64:$VA,
                               v2i64:$VB))]>;
  def VMULOUD : VXForm_1<200, (outs vrrc:$VD), (ins vrrc:$VA, vrrc:$VB),
                         "vmuloud $VD, $VA, $VB", IIC_VecGeneral,
                         [(set v1i128:$VD, (int_ppc_altivec_vmuloud v2i64:$VA,
                               v2i64:$VB))]>;
  def VMSUMCUD : VAForm_1a<23, (outs vrrc:$RT), (ins vrrc:$RA, vrrc:$RB, vrrc:$RC),
                           "vmsumcud $RT, $RA, $RB, $RC", IIC_VecGeneral,
                           [(set v1i128:$RT, (int_ppc_altivec_vmsumcud
                                 v2i64:$RA, v2i64:$RB, v1i128:$RC))]>;
  def VDIVSQ : VXForm_1<267, (outs vrrc:$VD), (ins vrrc:$VA, vrrc:$VB),
                        "vdivsq $VD, $VA, $VB", IIC_VecGeneral,
                        [(set v1i128:$VD, (sdiv v1i128:$VA, v1i128:$VB))]>;
  def VDIVUQ : VXForm_1<11, (outs vrrc:$VD), (ins vrrc:$VA, vrrc:$VB),
                        "vdivuq $VD, $VA, $VB", IIC_VecGeneral,
                        [(set v1i128:$VD, (udiv v1i128:$VA, v1i128:$VB))]>;
  def VDIVESQ : VXForm_1<779, (outs vrrc:$VD), (ins vrrc:$VA, vrrc:$VB),
                         "vdivesq $VD, $VA, $VB", IIC_VecGeneral,
                         [(set v1i128:$VD, (int_ppc_altivec_vdivesq v1i128:$VA,
			       v1i128:$VB))]>;
  def VDIVEUQ : VXForm_1<523, (outs vrrc:$VD), (ins vrrc:$VA, vrrc:$VB),
                         "vdiveuq $VD, $VA, $VB", IIC_VecGeneral,
                         [(set v1i128:$VD, (int_ppc_altivec_vdiveuq v1i128:$VA,
			       v1i128:$VB))]>;
  def VCMPEQUQ : VCMP <455, "vcmpequq $VD, $VA, $VB" , v1i128>;
  def VCMPGTSQ : VCMP <903, "vcmpgtsq $VD, $VA, $VB" , v1i128>;
  def VCMPGTUQ : VCMP <647, "vcmpgtuq $VD, $VA, $VB" , v1i128>;
  def VCMPEQUQ_rec : VCMP_rec <455, "vcmpequq. $VD, $VA, $VB" , v1i128>;
  def VCMPGTSQ_rec : VCMP_rec <903, "vcmpgtsq. $VD, $VA, $VB" , v1i128>;
  def VCMPGTUQ_rec : VCMP_rec <647, "vcmpgtuq. $VD, $VA, $VB" , v1i128>;
  def VMODSQ : VXForm_1<1803, (outs vrrc:$VD), (ins vrrc:$VA, vrrc:$VB),
                        "vmodsq $VD, $VA, $VB", IIC_VecGeneral,
                        [(set v1i128:$VD, (srem v1i128:$VA, v1i128:$VB))]>;
  def VMODUQ : VXForm_1<1547, (outs vrrc:$VD), (ins vrrc:$VA, vrrc:$VB),
                        "vmoduq $VD, $VA, $VB", IIC_VecGeneral,
                        [(set v1i128:$VD, (urem v1i128:$VA, v1i128:$VB))]>;
  def VEXTSD2Q : VXForm_RD5_XO5_RS5<1538, 27, (outs vrrc:$VD), (ins vrrc:$VB),
                               "vextsd2q $VD, $VB", IIC_VecGeneral,
                               [(set v1i128:$VD, (int_ppc_altivec_vextsd2q v2i64:$VB))]>;
  def VCMPUQ : VXForm_BF3_VAB5<257, (outs crrc:$BF), (ins vrrc:$VA, vrrc:$VB),
                               "vcmpuq $BF, $VA, $VB", IIC_VecGeneral, []>;
  def VCMPSQ : VXForm_BF3_VAB5<321, (outs crrc:$BF), (ins vrrc:$VA, vrrc:$VB),
                               "vcmpsq $BF, $VA, $VB", IIC_VecGeneral, []>;
  def VRLQNM : VX1_VT5_VA5_VB5<325, "vrlqnm",
                               [(set v1i128:$VD,
                                   (int_ppc_altivec_vrlqnm v1i128:$VA,
                                                           v1i128:$VB))]>;
  def VRLQMI : VXForm_1<69, (outs vrrc:$VD),
                        (ins vrrc:$VA, vrrc:$VB, vrrc:$VDi),
                        "vrlqmi $VD, $VA, $VB", IIC_VecFP,
                        [(set v1i128:$VD,
                          (int_ppc_altivec_vrlqmi v1i128:$VA, v1i128:$VB,
                                                  v1i128:$VDi))]>,
                        RegConstraint<"$VDi = $VD">, NoEncode<"$VDi">;
  def VSLQ : VX1_VT5_VA5_VB5<261, "vslq", []>;
  def VSRAQ : VX1_VT5_VA5_VB5<773, "vsraq", []>;
  def VSRQ : VX1_VT5_VA5_VB5<517, "vsrq", []>;
  def VRLQ : VX1_VT5_VA5_VB5<5, "vrlq", []>;
  def XSCVQPUQZ : X_VT5_XO5_VB5<63, 0, 836, "xscvqpuqz", []>;
  def XSCVQPSQZ : X_VT5_XO5_VB5<63, 8, 836, "xscvqpsqz", []>;
  def XSCVUQQP : X_VT5_XO5_VB5<63, 3, 836, "xscvuqqp", []>;
  def XSCVSQQP : X_VT5_XO5_VB5<63, 11, 836, "xscvsqqp", []>;
  def LXVKQ : XForm_XT6_IMM5<60, 31, 360, (outs vsrc:$XT), (ins u5imm:$UIM),
                             "lxvkq $XT, $UIM", IIC_VecGeneral, []>;
}

let Predicates = [IsISA3_1, HasVSX] in {
  def XVCVSPBF16 : XX2_XT6_XO5_XB6<60, 17, 475, "xvcvspbf16", vsrc, []>;
  def XVCVBF16SPN : XX2_XT6_XO5_XB6<60, 16, 475, "xvcvbf16spn", vsrc, []>;
  def XSMAXCQP : X_VT5_VA5_VB5<63, 676, "xsmaxcqp",
                               [(set f128:$RST, (PPCxsmaxc f128:$RA, f128:$RB))]>;
  def XSMINCQP : X_VT5_VA5_VB5<63, 740, "xsmincqp",
                               [(set f128:$RST, (PPCxsminc f128:$RA, f128:$RB))]>;
}

let Predicates = [IsISA3_1] in {
  def WAITP10 : XForm_IMM2_IMM2<31, 30, (outs), (ins u2imm:$L, u2imm:$PL),
                                "wait $L $PL", IIC_LdStLoad, []>;
  def SYNCP10 : XForm_IMM3_IMM2<31, 598, (outs), (ins u3imm:$L, u2imm:$SC),
                                "sync $L, $SC", IIC_LdStSync, []>;
}

// Multiclass defining patterns for Set Boolean Extension Reverse Instructions.
// This is analogous to the CRNotPat multiclass but specifically for Power10
// and newer subtargets since the extended forms use Set Boolean instructions.
// The first two anonymous patterns defined are actually a duplicate of those
// in CRNotPat, but it is preferable to define both multiclasses as complete
// ones rather than pulling that small common section out.
multiclass P10ReverseSetBool<dag pattern, dag result> {
  def : Pat<pattern, (crnot result)>;
  def : Pat<(not pattern), result>;

  def : Pat<(i32 (zext pattern)),
            (SETBCR result)>;
  def : Pat<(i64 (zext pattern)),
            (SETBCR8 result)>;

  def : Pat<(i32 (sext pattern)),
            (SETNBCR result)>;
  def : Pat<(i64 (sext pattern)),
            (SETNBCR8 result)>;

  def : Pat<(i32 (anyext pattern)),
            (SETBCR result)>;
  def : Pat<(i64 (anyext pattern)),
            (SETBCR8 result)>;
}

multiclass IntSetP10RevSetBool<SDNode SetCC, ValueType Ty, PatLeaf ZExtTy,
                               ImmLeaf SExtTy, I Cmpi, I Cmpli,
                               I Cmp, I Cmpl> {
  defm : P10ReverseSetBool<(i1 (SetCC Ty:$s1, Ty:$s2, SETUGE)),
                           (EXTRACT_SUBREG (Cmpl $s1, $s2), sub_lt)>;
  defm : P10ReverseSetBool<(i1 (SetCC Ty:$s1, Ty:$s2, SETGE)),
                           (EXTRACT_SUBREG (Cmp $s1, $s2), sub_lt)>;
  defm : P10ReverseSetBool<(i1 (SetCC Ty:$s1, Ty:$s2, SETULE)),
                           (EXTRACT_SUBREG (Cmpl $s1, $s2), sub_gt)>;
  defm : P10ReverseSetBool<(i1 (SetCC Ty:$s1, Ty:$s2, SETLE)),
                           (EXTRACT_SUBREG (Cmp $s1, $s2), sub_gt)>;
  defm : P10ReverseSetBool<(i1 (SetCC Ty:$s1, Ty:$s2, SETNE)),
                           (EXTRACT_SUBREG (Cmp $s1, $s2), sub_eq)>;

  defm : P10ReverseSetBool<(i1 (SetCC Ty:$s1, ZExtTy:$imm, SETUGE)),
                           (EXTRACT_SUBREG (Cmpli $s1, imm:$imm), sub_lt)>;
  defm : P10ReverseSetBool<(i1 (SetCC Ty:$s1, SExtTy:$imm, SETGE)),
                           (EXTRACT_SUBREG (Cmpi $s1, imm:$imm), sub_lt)>;
  defm : P10ReverseSetBool<(i1 (SetCC Ty:$s1, ZExtTy:$imm, SETULE)),
                           (EXTRACT_SUBREG (Cmpli $s1, imm:$imm), sub_gt)>;
  defm : P10ReverseSetBool<(i1 (SetCC Ty:$s1, SExtTy:$imm, SETLE)),
                           (EXTRACT_SUBREG (Cmpi $s1, imm:$imm), sub_gt)>;
  defm : P10ReverseSetBool<(i1 (SetCC Ty:$s1, SExtTy:$imm, SETNE)),
                           (EXTRACT_SUBREG (Cmpi $s1, imm:$imm), sub_eq)>;
  defm : P10ReverseSetBool<(i1 (SetCC Ty:$s1, ZExtTy:$imm, SETNE)),
                           (EXTRACT_SUBREG (Cmpli $s1, imm:$imm), sub_eq)>;
}

multiclass FSetP10RevSetBool<SDNode SetCC, ValueType Ty, I FCmp> {
  defm : P10ReverseSetBool<(i1 (SetCC Ty:$s1, Ty:$s2, SETUGE)),
                           (EXTRACT_SUBREG (FCmp $s1, $s2), sub_lt)>;
  defm : P10ReverseSetBool<(i1 (SetCC Ty:$s1, Ty:$s2, SETGE)),
                           (EXTRACT_SUBREG (FCmp $s1, $s2), sub_lt)>;
  defm : P10ReverseSetBool<(i1 (SetCC Ty:$s1, Ty:$s2, SETULE)),
                           (EXTRACT_SUBREG (FCmp $s1, $s2), sub_gt)>;
  defm : P10ReverseSetBool<(i1 (SetCC Ty:$s1, Ty:$s2, SETLE)),
                           (EXTRACT_SUBREG (FCmp $s1, $s2), sub_gt)>;
  defm : P10ReverseSetBool<(i1 (SetCC Ty:$s1, Ty:$s2, SETUNE)),
                           (EXTRACT_SUBREG (FCmp $s1, $s2), sub_eq)>;
  defm : P10ReverseSetBool<(i1 (SetCC Ty:$s1, Ty:$s2, SETNE)),
                           (EXTRACT_SUBREG (FCmp $s1, $s2), sub_eq)>;
  defm : P10ReverseSetBool<(i1 (SetCC Ty:$s1, Ty:$s2, SETO)),
                           (EXTRACT_SUBREG (FCmp $s1, $s2), sub_un)>;
}

let Predicates = [IsISA3_1] in {
  def : Pat<(i32 (zext i1:$in)),
            (SETBC $in)>;
  def : Pat<(i64 (zext i1:$in)),
            (SETBC8 $in)>;
  def : Pat<(i32 (sext i1:$in)),
            (SETNBC $in)>;
  def : Pat<(i64 (sext i1:$in)),
            (SETNBC8 $in)>;
  def : Pat<(i32 (anyext i1:$in)),
            (SETBC $in)>;
  def : Pat<(i64 (anyext i1:$in)),
            (SETBC8 $in)>;

  // Instantiation of the set boolean reverse patterns for 32-bit integers.
  defm : IntSetP10RevSetBool<setcc, i32, immZExt16, imm32SExt16,
                             CMPWI, CMPLWI, CMPW, CMPLW>;
  defm : P10ReverseSetBool<(i1 (setcc i32:$s1, imm:$imm, SETNE)),
                           (EXTRACT_SUBREG (CMPLWI (XORIS $s1, (HI16 imm:$imm)),
                                           (LO16 imm:$imm)), sub_eq)>;

  // Instantiation of the set boolean reverse patterns for 64-bit integers.
  defm : IntSetP10RevSetBool<setcc, i64, immZExt16, imm64SExt16,
                             CMPDI, CMPLDI, CMPD, CMPLD>;
  defm : P10ReverseSetBool<(i1 (setcc i64:$s1, imm64ZExt32:$imm, SETNE)),
                           (EXTRACT_SUBREG (CMPLDI (XORIS8 $s1, (HI16 imm:$imm)),
                                           (LO16 imm:$imm)), sub_eq)>;
}

// Instantiation of the set boolean reverse patterns for f32, f64, f128.
let Predicates = [IsISA3_1, HasFPU] in {
  defm : FSetP10RevSetBool<setcc, f32, FCMPUS>;
  defm : FSetP10RevSetBool<setcc, f64, FCMPUD>;
  defm : FSetP10RevSetBool<setcc, f128, XSCMPUQP>;
}

//---------------------------- Anonymous Patterns ----------------------------//
let Predicates = [IsISA3_1] in {
  // Exploit the vector multiply high instructions using intrinsics.
  def : Pat<(v4i32 (int_ppc_altivec_vmulhsw v4i32:$vA, v4i32:$vB)),
            (v4i32 (VMULHSW $vA, $vB))>;
  def : Pat<(v4i32 (int_ppc_altivec_vmulhuw v4i32:$vA, v4i32:$vB)),
            (v4i32 (VMULHUW $vA, $vB))>;
  def : Pat<(v2i64 (int_ppc_altivec_vmulhsd v2i64:$vA, v2i64:$vB)),
            (v2i64 (VMULHSD $vA, $vB))>;
  def : Pat<(v2i64 (int_ppc_altivec_vmulhud v2i64:$vA, v2i64:$vB)),
            (v2i64 (VMULHUD $vA, $vB))>;
  def : Pat<(v16i8 (int_ppc_vsx_xxgenpcvbm v16i8:$VRB, imm:$IMM)),
            (v16i8 (COPY_TO_REGCLASS (XXGENPCVBM $VRB, imm:$IMM), VRRC))>;
  def : Pat<(v8i16 (int_ppc_vsx_xxgenpcvhm v8i16:$VRB, imm:$IMM)),
            (v8i16 (COPY_TO_REGCLASS (XXGENPCVHM $VRB, imm:$IMM), VRRC))>;
  def : Pat<(v4i32 (int_ppc_vsx_xxgenpcvwm v4i32:$VRB, imm:$IMM)),
            (v4i32 (COPY_TO_REGCLASS (XXGENPCVWM $VRB, imm:$IMM), VRRC))>;
  def : Pat<(v2i64 (int_ppc_vsx_xxgenpcvdm v2i64:$VRB, imm:$IMM)),
            (v2i64 (COPY_TO_REGCLASS (XXGENPCVDM $VRB, imm:$IMM), VRRC))>;
  def : Pat<(i32 (int_ppc_vsx_xvtlsbb v16i8:$XB, 1)),
            (EXTRACT_SUBREG (XVTLSBB (COPY_TO_REGCLASS $XB, VSRC)), sub_lt)>;
  def : Pat<(i32 (int_ppc_vsx_xvtlsbb v16i8:$XB, 0)),
            (EXTRACT_SUBREG (XVTLSBB (COPY_TO_REGCLASS $XB, VSRC)), sub_eq)>;
  def : Pat<(srl (bswap i32:$RS), (i32 16)),
            (RLDICL_32 (BRH $RS), 0, 48)>;
  def : Pat<(i64 (zext (i32 (srl (bswap i32:$RS), (i32 16))))),
            (RLDICL_32_64 (BRH $RS), 0, 48)>;
  def : Pat <(v1i128 (PPClxvrzx ForceXForm:$src, 8)),
             (v1i128 (COPY_TO_REGCLASS (LXVRBX ForceXForm:$src), VRRC))>;
  def : Pat <(v1i128 (PPClxvrzx ForceXForm:$src, 16)),
             (v1i128 (COPY_TO_REGCLASS (LXVRHX ForceXForm:$src), VRRC))>;
  def : Pat <(v1i128 (PPClxvrzx ForceXForm:$src, 32)),
             (v1i128 (COPY_TO_REGCLASS (LXVRWX ForceXForm:$src), VRRC))>;
  def : Pat <(v1i128 (PPClxvrzx ForceXForm:$src, 64)),
             (v1i128 (COPY_TO_REGCLASS (LXVRDX ForceXForm:$src), VRRC))>;

  def : Pat<(v1i128 (rotl v1i128:$vA, v1i128:$vB)),
            (v1i128 (VRLQ v1i128:$vA, v1i128:$vB))>;

  def : Pat <(v2i64 (PPCxxsplti32dx v2i64:$XT, i32:$XI, i32:$IMM32)),
             (v2i64 (XXSPLTI32DX v2i64:$XT, i32:$XI, i32:$IMM32))>;
}

let Predicates = [IsISA3_1, HasVSX] in {
  def : Pat<(v16i8 (int_ppc_vsx_xvcvspbf16 v16i8:$XA)),
            (COPY_TO_REGCLASS (XVCVSPBF16 RCCp.AToVSRC), VRRC)>;
  def : Pat<(v16i8 (int_ppc_vsx_xvcvbf16spn v16i8:$XA)),
            (COPY_TO_REGCLASS (XVCVBF16SPN RCCp.AToVSRC), VRRC)>;
}

let AddedComplexity = 400, Predicates = [IsISA3_1, IsLittleEndian] in {
  // Store element 0 of a VSX register to memory
  def : Pat<(truncstorei8 (i32 (vector_extract v16i8:$src, 0)), ForceXForm:$dst),
            (STXVRBX (COPY_TO_REGCLASS v16i8:$src, VSRC), ForceXForm:$dst)>;
  def : Pat<(truncstorei16 (i32 (vector_extract v8i16:$src, 0)), ForceXForm:$dst),
            (STXVRHX (COPY_TO_REGCLASS v8i16:$src, VSRC), ForceXForm:$dst)>;
  def : Pat<(store (i32 (extractelt v4i32:$src, 0)), ForceXForm:$dst),
            (STXVRWX $src, ForceXForm:$dst)>;
  def : Pat<(store (f32 (extractelt v4f32:$src, 0)), ForceXForm:$dst),
            (STXVRWX $src, ForceXForm:$dst)>;
  def : Pat<(store (i64 (extractelt v2i64:$src, 0)), ForceXForm:$dst),
            (STXVRDX $src, ForceXForm:$dst)>;
  def : Pat<(store (f64 (extractelt v2f64:$src, 0)), ForceXForm:$dst),
            (STXVRDX $src, ForceXForm:$dst)>;
  // Load element 0 of a VSX register to memory
  def : Pat<(v8i16 (scalar_to_vector (i32 (extloadi16 ForceXForm:$src)))),
            (v8i16 (COPY_TO_REGCLASS (LXVRHX ForceXForm:$src), VSRC))>;
  def : Pat<(v16i8 (scalar_to_vector (i32 (extloadi8 ForceXForm:$src)))),
            (v16i8 (COPY_TO_REGCLASS (LXVRBX ForceXForm:$src), VSRC))>;
  def : Pat<(store (i64 (extractelt v2i64:$A, 1)), ForceXForm:$src),
            (XFSTOREf64 (EXTRACT_SUBREG $A, sub_64), ForceXForm:$src)>;
 }

let Predicates = [IsISA3_1, IsBigEndian] in {
  def : Pat<(store (i64 (extractelt v2i64:$A, 0)), ForceXForm:$src),
            (XFSTOREf64 (EXTRACT_SUBREG $A, sub_64), ForceXForm:$src)>;
}

// FIXME: The swap is overkill when the shift amount is a constant.
// We should just fix the constant in the DAG.
let AddedComplexity = 400, Predicates = [IsISA3_1, HasVSX] in {
  def : Pat<(v1i128 (shl v1i128:$VRA, v1i128:$VRB)),
            (v1i128 (VSLQ v1i128:$VRA,
                     (XXPERMDI (COPY_TO_REGCLASS $VRB, VSRC),
                               (COPY_TO_REGCLASS $VRB, VSRC), 2)))>;
  def : Pat<(v1i128 (PPCshl v1i128:$VRA, v1i128:$VRB)),
            (v1i128 (VSLQ v1i128:$VRA,
                     (XXPERMDI (COPY_TO_REGCLASS $VRB, VSRC),
                               (COPY_TO_REGCLASS $VRB, VSRC), 2)))>;
  def : Pat<(v1i128 (srl v1i128:$VRA, v1i128:$VRB)),
            (v1i128 (VSRQ v1i128:$VRA,
                     (XXPERMDI (COPY_TO_REGCLASS $VRB, VSRC),
                               (COPY_TO_REGCLASS $VRB, VSRC), 2)))>;
  def : Pat<(v1i128 (PPCsrl v1i128:$VRA, v1i128:$VRB)),
            (v1i128 (VSRQ v1i128:$VRA,
                     (XXPERMDI (COPY_TO_REGCLASS $VRB, VSRC),
                               (COPY_TO_REGCLASS $VRB, VSRC), 2)))>;
  def : Pat<(v1i128 (sra v1i128:$VRA, v1i128:$VRB)),
            (v1i128 (VSRAQ v1i128:$VRA,
                     (XXPERMDI (COPY_TO_REGCLASS $VRB, VSRC),
                               (COPY_TO_REGCLASS $VRB, VSRC), 2)))>;
  def : Pat<(v1i128 (PPCsra v1i128:$VRA, v1i128:$VRB)),
            (v1i128 (VSRAQ v1i128:$VRA,
                     (XXPERMDI (COPY_TO_REGCLASS $VRB, VSRC),
                               (COPY_TO_REGCLASS $VRB, VSRC), 2)))>;
}

class xxevalPattern <dag pattern, bits<8> imm> :
  Pat<(v4i32 pattern), (XXEVAL $vA, $vB, $vC, imm)> {}

let Predicates = [PrefixInstrs, HasP10Vector] in {
  let AddedComplexity = 400 in {
    def : Pat<(v4i32 (build_vector i32immNonAllOneNonZero:$A,
				   i32immNonAllOneNonZero:$A,
                                   i32immNonAllOneNonZero:$A,
                                   i32immNonAllOneNonZero:$A)),
              (v4i32 (XXSPLTIW imm:$A))>;
    def : Pat<(f32 nzFPImmAsi32:$A),
              (COPY_TO_REGCLASS (XXSPLTIDP (getFPAs32BitInt fpimm:$A)),
                              VSFRC)>;
    def : Pat<(f64 nzFPImmAsi32:$A),
              (COPY_TO_REGCLASS (XXSPLTIDP (getFPAs32BitInt fpimm:$A)),
                              VSFRC)>;

    // To replace constant pool with XXSPLTI32DX for scalars.
    def : Pat<(f32 nzFPImmAsi64:$A),
              (COPY_TO_REGCLASS (XXSPLTI32DX (XXSPLTI32DX(IMPLICIT_DEF), 0,
                                             (getFPAs64BitIntHi $A)),
                                             1, (getFPAs64BitIntLo $A)),
                                VSSRC)>;

    def : Pat<(f64 nzFPImmAsi64:$A),
              (COPY_TO_REGCLASS (XXSPLTI32DX (XXSPLTI32DX (IMPLICIT_DEF), 0,
                                             (getFPAs64BitIntHi $A)),
                                             1, (getFPAs64BitIntLo $A)),
                                 VSFRC)>;

    // Anonymous patterns for XXEVAL
    // AND
    // and(A, B, C)
    def : xxevalPattern<(and v4i32:$vA, (and v4i32:$vB, v4i32:$vC)), 1>;
    // and(A, xor(B, C))
    def : xxevalPattern<(and v4i32:$vA, (xor v4i32:$vB, v4i32:$vC)), 6>;
    // and(A, or(B, C))
    def : xxevalPattern<(and v4i32:$vA, (or v4i32:$vB, v4i32:$vC)), 7>;
    // and(A, nor(B, C))
    def : xxevalPattern<(and v4i32:$vA, (vnot (or v4i32:$vB, v4i32:$vC))), 8>;
    // and(A, eqv(B, C))
    def : xxevalPattern<(and v4i32:$vA, (vnot (xor v4i32:$vB, v4i32:$vC))), 9>;
    // and(A, nand(B, C))
    def : xxevalPattern<(and v4i32:$vA, (vnot (and v4i32:$vB, v4i32:$vC))), 14>;

    // NAND
    // nand(A, B, C)
    def : xxevalPattern<(vnot (and v4i32:$vA, (and v4i32:$vB, v4i32:$vC))),
                         !sub(255, 1)>;
    // nand(A, xor(B, C))
    def : xxevalPattern<(vnot (and v4i32:$vA, (xor v4i32:$vB, v4i32:$vC))),
                         !sub(255, 6)>;
    // nand(A, or(B, C))
    def : xxevalPattern<(vnot (and v4i32:$vA, (or v4i32:$vB, v4i32:$vC))),
                         !sub(255, 7)>;
    // nand(A, nor(B, C))
    def : xxevalPattern<(or (vnot v4i32:$vA), (or v4i32:$vB, v4i32:$vC)),
                         !sub(255, 8)>;
    // nand(A, eqv(B, C))
    def : xxevalPattern<(or (vnot v4i32:$vA), (xor v4i32:$vB, v4i32:$vC)),
                         !sub(255, 9)>;
    // nand(A, nand(B, C))
    def : xxevalPattern<(or (vnot v4i32:$vA), (and v4i32:$vB, v4i32:$vC)),
                         !sub(255, 14)>;

    // EQV
    // (eqv A, B, C)
    def : xxevalPattern<(or (and v4i32:$vA, (and v4i32:$vB, v4i32:$vC)),
                            (vnot (or v4i32:$vA, (or v4i32:$vB, v4i32:$vC)))),
                         150>;
    // (eqv A, (and B, C))
    def : xxevalPattern<(vnot (xor v4i32:$vA, (and v4i32:$vB, v4i32:$vC))), 225>;
    // (eqv A, (or B, C))
    def : xxevalPattern<(vnot (xor v4i32:$vA, (or v4i32:$vB, v4i32:$vC))), 135>;

    // NOR
    // (nor A, B, C)
    def : xxevalPattern<(vnot (or v4i32:$vA, (or v4i32:$vB, v4i32:$vC))), 128>;
    // (nor A, (and B, C))
    def : xxevalPattern<(vnot (or v4i32:$vA, (and v4i32:$vB, v4i32:$vC))), 224>;
    // (nor A, (eqv B, C))
    def : xxevalPattern<(and (vnot v4i32:$vA), (xor v4i32:$vB, v4i32:$vC)), 96>;
    // (nor A, (nand B, C))
    def : xxevalPattern<(and (vnot v4i32:$vA), (and v4i32:$vB, v4i32:$vC)), 16>;
    // (nor A, (nor B, C))
    def : xxevalPattern<(and (vnot v4i32:$vA), (or v4i32:$vB, v4i32:$vC)), 112>;
    // (nor A, (xor B, C))
    def : xxevalPattern<(vnot (or v4i32:$vA, (xor v4i32:$vB, v4i32:$vC))), 144>;

    // OR
    // (or A, B, C)
    def : xxevalPattern<(or v4i32:$vA, (or v4i32:$vB, v4i32:$vC)), 127>;
    // (or A, (and B, C))
    def : xxevalPattern<(or v4i32:$vA, (and v4i32:$vB, v4i32:$vC)), 31>;
    // (or A, (eqv B, C))
    def : xxevalPattern<(or v4i32:$vA, (vnot (xor v4i32:$vB, v4i32:$vC))), 159>;
    // (or A, (nand B, C))
    def : xxevalPattern<(or v4i32:$vA, (vnot (and v4i32:$vB, v4i32:$vC))), 239>;
    // (or A, (nor B, C))
    def : xxevalPattern<(or v4i32:$vA, (vnot (or v4i32:$vB, v4i32:$vC))), 143>;
    // (or A, (xor B, C))
    def : xxevalPattern<(or v4i32:$vA, (xor v4i32:$vB, v4i32:$vC)), 111>;

    // XOR
    // (xor A, B, C)
    def : xxevalPattern<(xor v4i32:$vA, (xor v4i32:$vB, v4i32:$vC)), 105>;
    // (xor A, (and B, C))
    def : xxevalPattern<(xor v4i32:$vA, (and v4i32:$vB, v4i32:$vC)), 30>;
    // (xor A, (or B, C))
    def : xxevalPattern<(xor v4i32:$vA, (or v4i32:$vB, v4i32:$vC)), 120>;

    // Anonymous patterns to select prefixed VSX loads and stores.
    // Load / Store f128
    def : Pat<(f128 (load PDForm:$src)),
              (COPY_TO_REGCLASS (PLXV memri34:$src), VRRC)>;
    def : Pat<(store f128:$XS, PDForm:$dst),
              (PSTXV (COPY_TO_REGCLASS $XS, VSRC), memri34:$dst)>;

    // Load / Store v4i32
    def : Pat<(v4i32 (load PDForm:$src)), (PLXV memri34:$src)>;
    def : Pat<(store v4i32:$XS, PDForm:$dst), (PSTXV $XS, memri34:$dst)>;

    // Load / Store v2i64
    def : Pat<(v2i64 (load PDForm:$src)), (PLXV memri34:$src)>;
    def : Pat<(store v2i64:$XS, PDForm:$dst), (PSTXV $XS, memri34:$dst)>;

    // Load / Store v4f32
    def : Pat<(v4f32 (load PDForm:$src)), (PLXV memri34:$src)>;
    def : Pat<(store v4f32:$XS, PDForm:$dst), (PSTXV $XS, memri34:$dst)>;

    // Load / Store v2f64
    def : Pat<(v2f64 (load PDForm:$src)), (PLXV memri34:$src)>;
    def : Pat<(store v2f64:$XS, PDForm:$dst), (PSTXV $XS, memri34:$dst)>;

    // Cases For PPCstore_scal_int_from_vsr
    def : Pat<(PPCstore_scal_int_from_vsr f64:$src, PDForm:$dst, 8),
              (PSTXSD $src, PDForm:$dst)>;
    def : Pat<(PPCstore_scal_int_from_vsr f128:$src, PDForm:$dst, 8),
              (PSTXSD (COPY_TO_REGCLASS $src, VFRC), PDForm:$dst)>;
    }


  def : Pat<(i32 imm34:$imm), (PLI (getImmAs64BitInt imm:$imm))>;
  def : Pat<(i64 imm34:$imm), (PLI8 (getImmAs64BitInt imm:$imm))>;
  def : Pat<(v16i8 (int_ppc_vsx_xxpermx v16i8:$A, v16i8:$B, v16i8:$C, timm:$D)),
            (COPY_TO_REGCLASS (XXPERMX (COPY_TO_REGCLASS $A, VSRC),
                                       (COPY_TO_REGCLASS $B, VSRC),
                                       (COPY_TO_REGCLASS $C, VSRC), $D), VSRC)>;
  def : Pat<(v16i8 (int_ppc_vsx_xxblendvb v16i8:$A, v16i8:$B, v16i8:$C)),
            (COPY_TO_REGCLASS
                   (XXBLENDVB (COPY_TO_REGCLASS $A, VSRC),
                              (COPY_TO_REGCLASS $B, VSRC),
                              (COPY_TO_REGCLASS $C, VSRC)), VSRC)>;
  def : Pat<(v8i16 (int_ppc_vsx_xxblendvh v8i16:$A, v8i16:$B, v8i16:$C)),
            (COPY_TO_REGCLASS
                   (XXBLENDVH (COPY_TO_REGCLASS $A, VSRC),
                              (COPY_TO_REGCLASS $B, VSRC),
                              (COPY_TO_REGCLASS $C, VSRC)), VSRC)>;
  def : Pat<(int_ppc_vsx_xxblendvw v4i32:$A, v4i32:$B, v4i32:$C),
            (XXBLENDVW $A, $B, $C)>;
  def : Pat<(int_ppc_vsx_xxblendvd v2i64:$A, v2i64:$B, v2i64:$C),
            (XXBLENDVD $A, $B, $C)>;
}

let Predicates = [PrefixInstrs] in {
  // Anonymous patterns to select prefixed loads and stores.
  // Load i32
  def : Pat<(i32 (extloadi1 PDForm:$src)), (PLBZ memri34:$src)>;
  def : Pat<(i32 (zextloadi1 PDForm:$src)), (PLBZ memri34:$src)>;
  def : Pat<(i32 (extloadi8 PDForm:$src)), (PLBZ memri34:$src)>;
  def : Pat<(i32 (zextloadi8 PDForm:$src)), (PLBZ memri34:$src)>;
  def : Pat<(i32 (extloadi16 PDForm:$src)), (PLHZ memri34:$src)>;
  def : Pat<(i32 (zextloadi16 PDForm:$src)), (PLHZ memri34:$src)>;
  def : Pat<(i32 (sextloadi16 PDForm:$src)), (PLHA memri34:$src)>;
  def : Pat<(i32 (load PDForm:$src)), (PLWZ memri34:$src)>;

  // Store i32
  def : Pat<(truncstorei8 i32:$rS, PDForm:$dst), (PSTB gprc:$rS, memri34:$dst)>;
  def : Pat<(truncstorei16 i32:$rS, PDForm:$dst), (PSTH gprc:$rS, memri34:$dst)>;
  def : Pat<(store i32:$rS, PDForm:$dst), (PSTW gprc:$rS, memri34:$dst)>;

  // Load i64
  def : Pat<(i64 (extloadi1 PDForm:$src)), (PLBZ8 memri34:$src)>;
  def : Pat<(i64 (zextloadi1 PDForm:$src)), (PLBZ8 memri34:$src)>;
  def : Pat<(i64 (extloadi8 PDForm:$src)), (PLBZ8 memri34:$src)>;
  def : Pat<(i64 (zextloadi8 PDForm:$src)), (PLBZ8 memri34:$src)>;
  def : Pat<(i64 (extloadi16 PDForm:$src)), (PLHZ8 memri34:$src)>;
  def : Pat<(i64 (zextloadi16 PDForm:$src)), (PLHZ8 memri34:$src)>;
  def : Pat<(i64 (sextloadi16 PDForm:$src)), (PLHA8 memri34:$src)>;
  def : Pat<(i64 (extloadi32 PDForm:$src)), (PLWZ8 memri34:$src)>;
  def : Pat<(i64 (zextloadi32 PDForm:$src)), (PLWZ8 memri34:$src)>;
  def : Pat<(i64 (sextloadi32 PDForm:$src)), (PLWA8 memri34:$src)>;
  def : Pat<(i64 (load PDForm:$src)), (PLD memri34:$src)>;

  // Store i64
  def : Pat<(truncstorei8 i64:$rS, PDForm:$dst), (PSTB8 g8rc:$rS, memri34:$dst)>;
  def : Pat<(truncstorei16 i64:$rS, PDForm:$dst), (PSTH8 g8rc:$rS, memri34:$dst)>;
  def : Pat<(truncstorei32 i64:$rS, PDForm:$dst), (PSTW8 g8rc:$rS, memri34:$dst)>;
  def : Pat<(store i64:$rS, PDForm:$dst), (PSTD g8rc:$rS, memri34:$dst)>;

  // Atomic Load
  def : Pat<(i32 (atomic_load_8 PDForm:$src)), (PLBZ memri34:$src)>;
  def : Pat<(i32 (atomic_load_16 PDForm:$src)), (PLHZ memri34:$src)>;
  def : Pat<(i32 (atomic_load_32 PDForm:$src)), (PLWZ memri34:$src)>;
  def : Pat<(i64 (atomic_load_64 PDForm:$src)), (PLD memri34:$src)>;

  // Atomic Store
  def : Pat<(atomic_store_8 i32:$RS, PDForm:$dst), (PSTB $RS, memri34:$dst)>;
  def : Pat<(atomic_store_16 i32:$RS, PDForm:$dst), (PSTH $RS, memri34:$dst)>;
  def : Pat<(atomic_store_32 i32:$RS, PDForm:$dst), (PSTW $RS, memri34:$dst)>;
  def : Pat<(atomic_store_64 i64:$RS, PDForm:$dst), (PSTD $RS, memri34:$dst)>;
}

let Predicates = [PrefixInstrs, HasFPU] in {
  // Load / Store f32
  def : Pat<(f32 (load PDForm:$src)), (PLFS memri34:$src)>;
  def : Pat<(store f32:$FRS, PDForm:$dst), (PSTFS $FRS, memri34:$dst)>;

  // Load / Store f64
  def : Pat<(f64 (extloadf32 PDForm:$src)),
            (COPY_TO_REGCLASS (PLFS memri34:$src), VSFRC)>;
  def : Pat<(f64 (load PDForm:$src)), (PLFD memri34:$src)>;
  def : Pat<(store f64:$FRS, PDForm:$dst), (PSTFD $FRS, memri34:$dst)>;
  // Prefixed fpext to v2f64
  def : Pat<(v4f32 (PPCldvsxlh PDForm:$src)),
            (SUBREG_TO_REG (i64 1), (PLFD PDForm:$src), sub_64)>;

}

def InsertEltShift {
  dag Sub32 = (i32 (EXTRACT_SUBREG $rB, sub_32));
  dag Sub32Left1 = (RLWINM (EXTRACT_SUBREG $rB, sub_32), 1, 0, 30);
  dag Sub32Left2 = (RLWINM (EXTRACT_SUBREG $rB, sub_32), 2, 0, 29);
  dag Left1 = (RLWINM $rB, 1, 0, 30);
  dag Left2 = (RLWINM $rB, 2, 0, 29);
  dag Left3 = (RLWINM8 $rB, 3, 0, 28);
}

let Predicates = [IsISA3_1, HasVSX, IsLittleEndian] in {
  // Indexed vector insert element
  def : Pat<(v16i8 (vector_insert v16i8:$vDi, i32:$rA, i64:$rB)),
            (VINSBRX $vDi, InsertEltShift.Sub32, $rA)>;
  def : Pat<(v8i16 (vector_insert v8i16:$vDi, i32:$rA, i64:$rB)),
            (VINSHRX $vDi, InsertEltShift.Sub32Left1, $rA)>;
  def : Pat<(v4i32 (insertelt v4i32:$vDi, i32:$rA, i64:$rB)),
            (VINSWRX $vDi, InsertEltShift.Sub32Left2, $rA)>;
  def : Pat<(v2i64 (insertelt v2i64:$vDi, i64:$rA, i64:$rB)),
            (VINSDRX $vDi, InsertEltShift.Left3, $rA)>;

  def : Pat<(v4f32 (insertelt v4f32:$vDi, f32:$rA, i64:$rB)),
            (VINSWVRX $vDi, InsertEltShift.Sub32Left2, (XSCVDPSPN $rA))>;

  def : Pat<(v2f64 (insertelt v2f64:$vDi,  f64:$A, i64:$rB)),
            (VINSDRX $vDi, InsertEltShift.Left3, Bitcast.DblToLong)>;
  def : Pat<(v2f64 (insertelt v2f64:$vDi, (f64 (load DSForm:$rA)), i64:$rB)),
            (VINSDRX $vDi, InsertEltShift.Left3, (LD memrix:$rA))>;
  def : Pat<(v2f64 (insertelt v2f64:$vDi, (f64 (load PDForm:$rA)), i64:$rB)),
            (VINSDRX $vDi, InsertEltShift.Left3, (PLD memri34:$rA))>;
  def : Pat<(v2f64 (insertelt v2f64:$vDi, (f64 (load XForm:$rA)), i64:$rB)),
            (VINSDRX $vDi, InsertEltShift.Left3, (LDX memrr:$rA))>;
  let AddedComplexity = 400 in {
    // Immediate vector insert element
    foreach Idx = [0, 1, 2, 3] in {
      def : Pat<(v4i32 (insertelt v4i32:$vDi, i32:$rA, Idx)),
                (VINSW $vDi, !mul(!sub(3, Idx), 4), $rA)>;
    }
    foreach i = [0, 1] in
     def : Pat<(v2i64 (insertelt v2i64:$vDi, i64:$rA, (i64 i))),
               (VINSD $vDi, !mul(!sub(1, i), 8), $rA)>;
  }
}

let Predicates = [IsISA3_1, HasVSX, IsBigEndian, IsPPC32] in {
  // Indexed vector insert element
  def : Pat<(v16i8 (vector_insert v16i8:$vDi, i32:$rA, i32:$rB)),
            (VINSBLX $vDi, $rB, $rA)>;
  def : Pat<(v8i16 (vector_insert v8i16:$vDi, i32:$rA, i32:$rB)),
            (VINSHLX $vDi, InsertEltShift.Left1, $rA)>;
  def : Pat<(v4i32 (insertelt v4i32:$vDi, i32:$rA, i32:$rB)),
            (VINSWLX $vDi, InsertEltShift.Left2, $rA)>;

  def : Pat<(v4f32 (insertelt v4f32:$vDi,  f32:$rA, i32:$rB)),
            (VINSWVLX $vDi, InsertEltShift.Left2, (XSCVDPSPN $rA))>;
}

let Predicates = [IsISA3_1, HasVSX, IsBigEndian, IsPPC64] in {
  // Indexed vector insert element
  def : Pat<(v16i8 (vector_insert v16i8:$vDi, i32:$rA, i64:$rB)),
            (VINSBLX $vDi, InsertEltShift.Sub32, $rA)>;
  def : Pat<(v8i16 (vector_insert v8i16:$vDi, i32:$rA, i64:$rB)),
            (VINSHLX $vDi, InsertEltShift.Sub32Left1, $rA)>;
  def : Pat<(v4i32 (insertelt v4i32:$vDi, i32:$rA, i64:$rB)),
            (VINSWLX $vDi, InsertEltShift.Sub32Left2, $rA)>;
  def : Pat<(v2i64 (insertelt v2i64:$vDi, i64:$rA, i64:$rB)),
            (VINSDLX $vDi, InsertEltShift.Left3, $rA)>;

  def : Pat<(v4f32 (insertelt v4f32:$vDi,  f32:$rA, i64:$rB)),
            (VINSWVLX $vDi, InsertEltShift.Sub32Left2, (XSCVDPSPN $rA))>;

  def : Pat<(v2f64 (insertelt v2f64:$vDi,  f64:$A, i64:$rB)),
            (VINSDLX $vDi, InsertEltShift.Left3, Bitcast.DblToLong)>;
  def : Pat<(v2f64 (insertelt v2f64:$vDi, (f64 (load DSForm:$rA)), i64:$rB)),
            (VINSDLX $vDi, InsertEltShift.Left3, (LD memrix:$rA))>;
  def : Pat<(v2f64 (insertelt v2f64:$vDi, (f64 (load PDForm:$rA)), i64:$rB)),
            (VINSDLX $vDi, InsertEltShift.Left3, (PLD memri34:$rA))>;
  def : Pat<(v2f64 (insertelt v2f64:$vDi, (f64 (load XForm:$rA)), i64:$rB)),
            (VINSDLX $vDi, InsertEltShift.Left3, (LDX memrr:$rA))>;
}

let AddedComplexity = 400, Predicates = [IsISA3_1, HasVSX, IsBigEndian] in {
  // Immediate vector insert element
  foreach Ty = [i32, i64] in {
    foreach Idx = [0, 1, 2, 3] in {
      def : Pat<(v4i32 (insertelt v4i32:$vDi, i32:$rA, (Ty Idx))),
               (VINSW $vDi, !mul(Idx, 4), $rA)>;
    }
  }

  foreach Idx = [0, 1] in
    def : Pat<(v2i64 (insertelt v2i64:$vDi, i64:$rA, Idx)),
              (VINSD $vDi, !mul(Idx, 8), $rA)>;
}


//===----------------------------------------------------------------------===//
// PowerPC ISA 3.1 Extended Mnemonics.
//

let Predicates = [IsISA3_1] in {
  def : InstAlias<"wait", (WAITP10 0, 0)>;
  def : InstAlias<"wait 0", (WAITP10 0, 0), 0>;
  def : InstAlias<"wait 1", (WAITP10 1, 0), 0>;
  def : InstAlias<"waitrsv", (WAITP10 1, 0)>;
  def : InstAlias<"pause_short", (WAITP10 2, 0), 0>;

  def : InstAlias<"sync", (SYNCP10 0, 0)>;
  def : InstAlias<"hwsync", (SYNCP10 0, 0), 0>;
  def : InstAlias<"wsync", (SYNCP10 1, 0), 0>;
  def : InstAlias<"ptesync", (SYNCP10 2, 0)>;
  def : InstAlias<"phwsync", (SYNCP10 4, 0)>;
  def : InstAlias<"plwsync", (SYNCP10 5, 0)>;
  def : InstAlias<"sync $L", (SYNCP10 u3imm:$L, 0)>;
  def : InstAlias<"stncisync", (SYNCP10 1, 1)>;
  def : InstAlias<"stcisync", (SYNCP10 0, 2)>;
  def : InstAlias<"stsync", (SYNCP10 0, 3)>;

  def : InstAlias<"paddi $RT, $RA, $SI", (PADDI8 g8rc:$RT, g8rc_nox0:$RA, s34imm:$SI)>;
}

let Predicates = [IsISA3_1, PrefixInstrs], isAsmParserOnly = 1, hasNoSchedulingInfo = 1 in {
  let Interpretation64Bit = 1 in {
    def PLA8 : MLS_DForm_SI34_RT5<14, (outs g8rc:$RT),
                                  (ins g8rc_nox0:$RA, s34imm:$SI),
                                  "pla $RT, ${SI} ${RA}", IIC_IntSimple, []>;
    def PLA8pc : MLS_DForm_SI34_RT5<14, (outs g8rc:$RT),
                                    (ins s34imm_pcrel:$SI),
                                    "pla $RT, $SI", IIC_IntSimple, []>, isPCRel;
  }

  def PSUBI : PPCAsmPseudo<"psubi $RT, $RA, $SI",
                           (ins g8rc:$RT, g8rc_nox0:$RA, s34imm:$SI)>;

  def PLA : MLS_DForm_SI34_RT5<14, (outs gprc:$RT),
                               (ins gprc_nor0:$RA, s34imm:$SI),
                               "pla $RT, ${SI} ${RA}", IIC_IntSimple, []>;
  def PLApc : MLS_DForm_SI34_RT5<14, (outs gprc:$RT),
                                 (ins s34imm_pcrel:$SI),
                                 "pla $RT, $SI", IIC_IntSimple, []>, isPCRel;
}

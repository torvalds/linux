//===-- PPCInstrFutureMMA.td - Future Instruction Set ------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the instructions introduced for the Future CPU for MMA.
//
//===----------------------------------------------------------------------===//

class XX3Form_AT3_XABp5_P1<bits<6> opcode, bits<8> xo, dag OOL, dag IOL,
                           string asmstr, list<dag> pattern>
  : I<opcode, OOL, IOL, asmstr, NoItinerary> {
  bits<3> AT;
  bits<5> XAp;
  bits<5> XBp;
  bits<1> P;

  let Pattern = pattern;

  let Inst{6-8} = AT{2-0};
  let Inst{9-10} = 0;
  let Inst{11-14} = XAp{3-0};
  let Inst{15} = P;
  let Inst{16-19} = XBp{3-0};
  let Inst{20} = 0;
  let Inst{21-28} = xo;
  let Inst{29} = XAp{4};
  let Inst{30} = XBp{4};
  let Inst{31} = 0;
}

class XX2Form_AT3_XBp5_P2<bits<6> opcode, bits<9> xo, dag OOL, dag IOL,
                          string asmstr, list<dag> pattern>
  : I<opcode, OOL, IOL, asmstr, NoItinerary> {
  bits<3> AT;
  bits<5> XBp;
  bits<2> P;

  let Pattern = pattern;

  let Inst{6-8} = AT{2-0};
  let Inst{9-14} = 0;
  let Inst{15} = P{0};
  let Inst{16-19} = XBp{3-0};
  let Inst{20} = P{1};
  let Inst{21-29} = xo;
  let Inst{30} = XBp{4};
  let Inst{31} = 0;
}

class XForm_ATB3<bits<6> opcode, bits<5> o, bits<10> xo, dag OOL, dag IOL,
                 string asmstr, list<dag> pattern>
  : I <opcode, OOL, IOL, asmstr, NoItinerary> {
  bits<3> AT;
  bits<3> AB;

  let Pattern = pattern;

  let Inst{6-8} = AT{2-0};
  let Inst{9-10} = 0;
  let Inst{11-15} = o;
  let Inst{16-18} = AB{2-0};
  let Inst{19-20} = 0;
  let Inst{21-30} = xo;
  let Inst{31} = 0;
}

let Predicates = [IsISAFuture] in {
  def DMXXEXTFDMR512 : XX3Form_AT3_XABp5_P1<60, 226,
                                            (outs vsrprc:$XAp, vsrprc:$XBp),
                                            (ins wacc:$AT),
                                            "dmxxextfdmr512 $AT, $XAp, $XBp, 0", []> {
    let P = 0;
  }

  def DMXXEXTFDMR512_HI : XX3Form_AT3_XABp5_P1<60, 226,
                                               (outs vsrprc:$XAp, vsrprc:$XBp),
                                               (ins wacc_hi:$AT),
                                               "dmxxextfdmr512 $AT, $XAp, $XBp, 1", []> {
    let P = 1;
  }

  def DMXXINSTFDMR512 : XX3Form_AT3_XABp5_P1<60, 234, (outs wacc:$AT),
                                             (ins vsrprc:$XAp, vsrprc:$XBp),
                                             "dmxxinstfdmr512 $AT, $XAp, $XBp, 0", []> {
    let P = 0;
  }

  def DMXXINSTFDMR512_HI : XX3Form_AT3_XABp5_P1<60, 234, (outs wacc_hi:$AT),
                                                (ins vsrprc:$XAp, vsrprc:$XBp),
                                                "dmxxinstfdmr512 $AT, $XAp, $XBp, 1", []> {
    let P = 1;
  }

  def DMXXEXTFDMR256 : XX2Form_AT3_XBp5_P2<60, 484, (outs vsrprc:$XBp),
                                           (ins dmrrowp:$AT, u2imm:$P),
                                           "dmxxextfdmr256 $AT, $XBp, $P", []>;

  def DMXXINSTFDMR256 : XX2Form_AT3_XBp5_P2<60, 485, (outs dmrrowp:$AT),
                                            (ins vsrprc:$XBp, u2imm:$P),
                                            "dmxxinstfdmr256 $AT, $XBp, $P", []>;

  def DMMR : XForm_ATB3<31, 6, 177, (outs dmr:$AT), (ins dmr:$AB),
                        "dmmr $AT, $AB", []>;

  def DMXOR : XForm_ATB3<31, 7, 177, (outs dmr:$AT), (ins dmr:$ATi, dmr:$AB),
                         "dmxor $AT, $AB", []>,
                         RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;

  def DMSETDMRZ : XForm_AT3<31, 2, 177, (outs dmr:$AT), (ins),
                            "dmsetdmrz $AT", NoItinerary, []>;
}

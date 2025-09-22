//===-- PPCInstrFuture.td - Future Instruction Set --------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the instructions introduced for the Future CPU.
//
//===----------------------------------------------------------------------===//

class XOForm_RTAB5_L1<bits<6> opcode, bits<9> xo, dag OOL, dag IOL,
                      string asmstr, list<dag> pattern>
         : I<opcode, OOL, IOL, asmstr, NoItinerary> {
  bits<5> RT;
  bits<5> RA;
  bits<5> RB;
  bit L;

  let Pattern = pattern;

  bit RC = 0;    // set by isRecordForm

  let Inst{6-10}  = RT;
  let Inst{11-15} = RA;
  let Inst{16-20} = RB;
  let Inst{21}    = L;
  let Inst{22-30} = xo;
  let Inst{31}    = RC;
}

multiclass XOForm_RTAB5_L1r<bits<6> opcode, bits<9> xo, dag OOL, dag IOL,
                            string asmbase, string asmstr,
                            list<dag> pattern> {
  let BaseName = asmbase in {
    def NAME : XOForm_RTAB5_L1<opcode, xo, OOL, IOL,
                               !strconcat(asmbase, !strconcat(" ", asmstr)),
                               pattern>, RecFormRel;
    let Defs = [CR0] in
    def _rec : XOForm_RTAB5_L1<opcode, xo, OOL, IOL,
                               !strconcat(asmbase, !strconcat(". ", asmstr)),
                               []>, isRecordForm, RecFormRel;
  }
}

let Predicates = [IsISAFuture] in {
  defm SUBFUS : XOForm_RTAB5_L1r<31, 72, (outs g8rc:$RT),
                                 (ins g8rc:$RA, g8rc:$RB, u1imm:$L),
                                 "subfus",  "$RT, $L, $RA, $RB", []>;
}

let Predicates = [HasVSX, IsISAFuture] in {
  let mayLoad = 1 in {
    def LXVRL : XX1Form_memOp<31, 525, (outs vsrc:$XT), (ins memr:$RA, g8rc:$RB),
                              "lxvrl $XT, $RA, $RB", IIC_LdStLoad, []>;

    def LXVRLL : XX1Form_memOp<31, 557, (outs vsrc:$XT), (ins memr:$RA, g8rc:$RB),
                               "lxvrll $XT, $RA, $RB", IIC_LdStLoad, []>;

    def LXVPRL : XForm_XTp5_XAB5<31, 589, (outs vsrprc:$XTp),
                                 (ins memr:$RA, g8rc:$RB),
                                 "lxvprl $XTp, $RA, $RB", IIC_LdStLFD, []>;

    def LXVPRLL : XForm_XTp5_XAB5<31, 621, (outs vsrprc:$XTp),
                                  (ins memr:$RA, g8rc:$RB),
                                  "lxvprll $XTp, $RA, $RB", IIC_LdStLFD, []>;
  }

  let mayStore = 1 in {
    def STXVRL : XX1Form_memOp<31, 653, (outs),
                               (ins vsrc:$XT, memr:$RA, g8rc:$RB),
                               "stxvrl $XT, $RA, $RB", IIC_LdStLoad, []>;

    def STXVRLL : XX1Form_memOp<31, 685, (outs),
                                (ins vsrc:$XT, memr:$RA, g8rc:$RB),
                                "stxvrll $XT, $RA, $RB", IIC_LdStLoad, []>;

    def STXVPRL : XForm_XTp5_XAB5<31, 717, (outs),
                                  (ins vsrprc:$XTp, memr:$RA, g8rc:$RB),
                                  "stxvprl $XTp, $RA, $RB", IIC_LdStLFD, []>;

    def STXVPRLL : XForm_XTp5_XAB5<31, 749, (outs),
                                   (ins vsrprc:$XTp, memr:$RA, g8rc:$RB),
                                   "stxvprll $XTp, $RA, $RB", IIC_LdStLFD, []>;
  }
}

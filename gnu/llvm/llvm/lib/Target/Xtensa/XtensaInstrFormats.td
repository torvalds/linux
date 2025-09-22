//===- XtensaInstrFormats.td - Xtensa Instruction Formats --*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Base class for Xtensa 16 & 24 bit Formats
class XtensaInst<int size, dag outs, dag ins, string asmstr, list<dag> pattern,
                 InstrItinClass itin = NoItinerary>
  : Instruction {
  let Namespace = "Xtensa";

  let Size = size;

  let OutOperandList = outs;
  let InOperandList  = ins;

  let AsmString   = asmstr;
  let Pattern     = pattern;
  let Itinerary   = itin;

}

// Base class for Xtensa 24 bit Format
class XtensaInst24<dag outs, dag ins, string asmstr, list<dag> pattern,
                   InstrItinClass itin = NoItinerary>
  : XtensaInst<3, outs, ins, asmstr, pattern, itin> {
  field bits<24> Inst;
  field bits<24> SoftFail = 0;
}

// Base class for Xtensa 16 bit Format
class XtensaInst16<dag outs, dag ins, string asmstr, list<dag> pattern,
                   InstrItinClass itin = NoItinerary>
  : XtensaInst<2, outs, ins, asmstr, pattern, itin> {
  field bits<16> Inst;
  field bits<16> SoftFail = 0;
  let Predicates = [HasDensity];
}

class RRR_Inst<bits<4> op0, bits<4> op1, bits<4> op2, dag outs, dag ins,
               string asmstr, list<dag> pattern, InstrItinClass itin = NoItinerary>
  : XtensaInst24<outs, ins, asmstr, pattern, itin> {
  bits<4> r;
  bits<4> s;
  bits<4> t;

  let Inst{23-20} = op2;
  let Inst{19-16} = op1;
  let Inst{15-12} = r;
  let Inst{11-8} = s;
  let Inst{7-4} = t;
  let Inst{3-0} = op0;
}

class RRI4_Inst<bits<4> op0, bits<4> op1, dag outs, dag ins,
                string asmstr, list<dag> pattern, InstrItinClass itin = NoItinerary>
  : XtensaInst24<outs, ins, asmstr, pattern, itin> {
  bits<4> r;
  bits<4> s;
  bits<4> t;
  bits<4> imm4;

  let Inst{23-20} = imm4;
  let Inst{19-16} = op1;
  let Inst{15-12} = r;
  let Inst{11-8} = s;
  let Inst{7-4} = t;
  let Inst{3-0} = op0;
}

class RRI8_Inst<bits<4> op0, dag outs, dag ins,
                string asmstr, list<dag> pattern, InstrItinClass itin = NoItinerary>
  : XtensaInst24<outs, ins, asmstr, pattern, itin> {
  bits<4> r;
  bits<4> s;
  bits<4> t;
  bits<8> imm8;

  let Inst{23-16} = imm8;
  let Inst{15-12} = r;
  let Inst{11-8} = s;
  let Inst{7-4} = t;
  let Inst{3-0} = op0;
}

class RI16_Inst<bits<4> op0, dag outs, dag ins,
                string asmstr, list<dag> pattern, InstrItinClass itin = NoItinerary>
  : XtensaInst24<outs, ins, asmstr, pattern, itin> {
  bits<4> t;
  bits<16> imm16;

  let Inst{23-8} = imm16;
  let Inst{7-4} = t;
  let Inst{3-0} = op0;
}

class RSR_Inst<bits<4> op0, bits<4> op1, bits<4> op2, dag outs, dag ins,
               string asmstr, list<dag> pattern, InstrItinClass itin = NoItinerary>
  : XtensaInst24<outs, ins, asmstr, pattern, itin> {
  bits<8> sr;
  bits<4> t;

  let Inst{23-20} = op2;
  let Inst{19-16} = op1;
  let Inst{15-8} = sr;
  let Inst{7-4} = t;
  let Inst{3-0} = op0;
}

class CALL_Inst<bits<4> op0, dag outs, dag ins,
                string asmstr, list<dag> pattern, InstrItinClass itin = NoItinerary>
  : XtensaInst24<outs, ins, asmstr, pattern, itin> {
  bits<18> offset;
  bits<2> n;

  let Inst{23-6} = offset;
  let Inst{5-4} = n;
  let Inst{3-0} = op0;
}

class CALLX_Inst<bits<4> op0, bits<4> op1, bits<4> op2, dag outs, dag ins,
                 string asmstr, list<dag> pattern, InstrItinClass itin = NoItinerary>
  : XtensaInst24<outs, ins, asmstr, pattern, itin> {
  bits<4> r;
  bits<4> s;
  bits<2> m;
  bits<2> n;

  let Inst{23-20} = op2;
  let Inst{19-16} = op1;
  let Inst{15-12} = r;
  let Inst{11-8} = s;
  let Inst{7-6} = m;
  let Inst{5-4} = n;
  let Inst{3-0} = op0;
}

class BRI8_Inst<bits<4> op0, dag outs, dag ins,
                string asmstr, list<dag> pattern, InstrItinClass itin = NoItinerary>
  : XtensaInst24<outs, ins, asmstr, pattern, itin> {
  bits<8> imm8;
  bits<4> r;
  bits<4> s;
  bits<2> m;
  bits<2> n;

  let Inst{23-16} = imm8;
  let Inst{15-12} = r;
  let Inst{11-8} = s;
  let Inst{7-6} = m;
  let Inst{5-4} = n;
  let Inst{3-0} = op0;
}

class BRI12_Inst<bits<4> op0, bits<2> n, bits<2> m, dag outs, dag ins,
                 string asmstr, list<dag> pattern, InstrItinClass itin = NoItinerary>
  : XtensaInst24<outs, ins, asmstr, pattern, itin> {
  bits<12> imm12;
  bits<4> s;

  let Inst{23-12} = imm12;
  let Inst{11-8} = s;
  let Inst{7-6} = m;
  let Inst{5-4} = n;
  let Inst{3-0} = op0;
}

class RRRN_Inst<bits<4> op0, dag outs, dag ins,
                string asmstr, list<dag> pattern, InstrItinClass itin = NoItinerary>
  : XtensaInst16<outs, ins, asmstr, pattern, itin> {
  bits<4> r;
  bits<4> s;
  bits<4> t;

  let Inst{15-12} = r;
  let Inst{11-8} = s;
  let Inst{7-4} = t;
  let Inst{3-0} = op0;
}

class RI7_Inst<bits<4> op0, bits<1> i, dag outs, dag ins,
               string asmstr, list<dag> pattern, InstrItinClass itin = NoItinerary>
  : XtensaInst16<outs, ins, asmstr, pattern, itin> {
  bits<7> imm7;
  bits<4> s;

  let Inst{15-12} = imm7{3-0};
  let Inst{11-8} = s;
  let Inst{7} = i;
  let Inst{6-4} = imm7{6-4};
  let Inst{3-0} = op0;
}

class RI6_Inst<bits<4> op0, bits<1> i,  bits<1> z, dag outs, dag ins,
               string asmstr, list<dag> pattern, InstrItinClass itin = NoItinerary>
  : XtensaInst16<outs, ins, asmstr, pattern, itin> {
  bits<6> imm6;
  bits<4> s;

  let Inst{15-12} = imm6{3-0};
  let Inst{11-8} = s;
  let Inst{7} = i;
  let Inst{6} = z;
  let Inst{5-4} = imm6{5-4};
  let Inst{3-0} = op0;
}

// Pseudo instructions
class Pseudo<dag outs, dag ins, string asmstr, list<dag> pattern>
  : XtensaInst<2, outs, ins, asmstr, pattern> {
  let isPseudo = 1;
  let isCodeGenOnly = 1;
}

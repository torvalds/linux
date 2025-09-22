//===- Mips16InstrFormats.td - Mips Instruction Formats ----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Describe MIPS instructions format
//
//  CPU INSTRUCTION FORMATS
//
//  funct or f      Function field
//
//  immediate       4-,5-,8- or 11-bit immediate, branch displacement, or
//  or imm          address displacement
//
//  op              5-bit major operation code
//
//  rx              3-bit source or destination register
//
//  ry              3-bit source or destination register
//
//  rz              3-bit source or destination register
//
//  sa              3- or 5-bit shift amount
//
//===----------------------------------------------------------------------===//


// Base class for Mips 16 Format
// This class does not depend on the instruction size
//
class MipsInst16_Base<dag outs, dag ins, string asmstr, list<dag> pattern,
                      InstrItinClass itin>: Instruction
{

  let Namespace = "Mips";

  let OutOperandList = outs;
  let InOperandList  = ins;

  let AsmString   = asmstr;
  let Pattern     = pattern;
  let Itinerary   = itin;

  let Predicates = [InMips16Mode];
}

//
// Generic Mips 16 Format
//
class MipsInst16<dag outs, dag ins, string asmstr, list<dag> pattern,
                 InstrItinClass itin>:
  MipsInst16_Base<outs, ins, asmstr, pattern, itin>
{
  field bits<16> Inst;
  bits<5> Opcode = 0;

  // Top 5 bits are the 'opcode' field
  let Inst{15-11} = Opcode;

  let Size=2;
  field bits<16> SoftFail = 0;
}

//
// For 32 bit extended instruction forms.
//
class MipsInst16_32<dag outs, dag ins, string asmstr, list<dag> pattern,
                    InstrItinClass itin>:
  MipsInst16_Base<outs, ins, asmstr, pattern, itin>
{
  field bits<32> Inst;

  let Size=4;
  field bits<32> SoftFail = 0;
}

class MipsInst16_EXTEND<dag outs, dag ins, string asmstr, list<dag> pattern,
                        InstrItinClass itin>:
  MipsInst16_32<outs, ins, asmstr, pattern, itin>
{
  let Inst{31-27} = 0b11110;
}



// Mips Pseudo Instructions Format
class MipsPseudo16<dag outs, dag ins, string asmstr, list<dag> pattern>:
  MipsInst16<outs, ins, asmstr, pattern, IIPseudo> {
  let isCodeGenOnly = 1;
  let isPseudo = 1;
}


//===----------------------------------------------------------------------===//
// Format I instruction class in Mips : <|opcode|imm11|>
//===----------------------------------------------------------------------===//

class FI16<bits<5> op, dag outs, dag ins, string asmstr, list<dag> pattern,
           InstrItinClass itin>:
  MipsInst16<outs, ins, asmstr, pattern, itin>
{
  bits<11> imm11;

  let Opcode = op;

  let Inst{10-0}  = imm11;
}

//===----------------------------------------------------------------------===//
// Format RI instruction class in Mips : <|opcode|rx|imm8|>
//===----------------------------------------------------------------------===//

class FRI16<bits<5> op, dag outs, dag ins, string asmstr,
            list<dag> pattern, InstrItinClass itin>:
  MipsInst16<outs, ins, asmstr, pattern, itin>
{
  bits<3>  rx;
  bits<8>   imm8;

  let Opcode = op;

  let Inst{10-8} = rx;
  let Inst{7-0} = imm8;
}

//===----------------------------------------------------------------------===//
// Format RR instruction class in Mips : <|opcode|rx|ry|funct|>
//===----------------------------------------------------------------------===//

class FRR16<bits<5> _funct, dag outs, dag ins, string asmstr,
            list<dag> pattern, InstrItinClass itin>:
  MipsInst16<outs, ins, asmstr, pattern, itin>
{
  bits<3>  rx;
  bits<3>  ry;
  bits<5>  funct;

  let Opcode = 0b11101;
  let funct  = _funct;

  let Inst{10-8} = rx;
  let Inst{7-5} = ry;
  let Inst{4-0}   = funct;
}

class FRRBreak16<dag outs, dag ins, string asmstr,
                 list<dag> pattern, InstrItinClass itin>:
  MipsInst16<outs, ins, asmstr, pattern, itin>
{
  bits<6>  Code;
  bits<5>  funct;

  let Opcode = 0b11101;
  let funct  = 0b00101;

  let Inst{10-5} = Code;
  let Inst{4-0}   = funct;
}

//
// For conversion functions.
//
class FRR_SF16<bits<5> _funct, bits<3> _subfunct, dag outs, dag ins,
               string asmstr, list<dag> pattern, InstrItinClass itin>:
  MipsInst16<outs, ins, asmstr, pattern, itin>
{
  bits<3>  rx;
  bits<3>  subfunct;
  bits<5>  funct;

  let Opcode = 0b11101; // RR
  let funct  = _funct;
  let subfunct = _subfunct;

  let Inst{10-8} = rx;
  let Inst{7-5} = subfunct;
  let Inst{4-0}   = funct;
}

//
// just used for breakpoint (hardware and software) instructions.
//
class FC16<bits<5> _funct, dag outs, dag ins, string asmstr,
           list<dag> pattern, InstrItinClass itin>:
  MipsInst16<outs, ins, asmstr, pattern, itin>
{
  bits<6>  _code;  // code is a keyword in tablegen
  bits<5>  funct;

  let Opcode = 0b11101; // RR
  let funct  = _funct;

  let Inst{10-5} = _code;
  let Inst{4-0}   = funct;
}

//
// J(AL)R(C) subformat
//
class FRR16_JALRC<bits<1> _nd, bits<1> _l, bits<1> r_a,
                  dag outs, dag ins, string asmstr,
                  list<dag> pattern, InstrItinClass itin>:
  MipsInst16<outs, ins, asmstr, pattern, itin>
{
  bits<3>  rx;
  bits<1>  nd;
  bits<1>  l;
  bits<1>  ra;

  let nd = _nd;
  let l = _l;
  let ra = r_a;

  let Opcode = 0b11101;

  let Inst{10-8} = rx;
  let Inst{7} = nd;
  let Inst{6} = l;
  let Inst{5} = ra;
  let Inst{4-0} = 0;
}

//===----------------------------------------------------------------------===//
// Format RRI instruction class in Mips : <|opcode|rx|ry|imm5|>
//===----------------------------------------------------------------------===//

class FRRI16<bits<5> op, dag outs, dag ins, string asmstr,
             list<dag> pattern, InstrItinClass itin>:
  MipsInst16<outs, ins, asmstr, pattern, itin>
{
  bits<3>  rx;
  bits<3>  ry;
  bits<5>  imm5;

  let Opcode = op;


  let Inst{10-8} = rx;
  let Inst{7-5} = ry;
  let Inst{4-0}   = imm5;
}

//===----------------------------------------------------------------------===//
// Format RRR instruction class in Mips : <|opcode|rx|ry|rz|f|>
//===----------------------------------------------------------------------===//

class FRRR16<bits<2> _f, dag outs, dag ins, string asmstr,
             list<dag> pattern, InstrItinClass itin>:
  MipsInst16<outs, ins, asmstr, pattern, itin>
{
  bits<3>  rx;
  bits<3>  ry;
  bits<3>  rz;
  bits<2>  f;

  let Opcode = 0b11100;
  let f  = _f;

  let Inst{10-8} = rx;
  let Inst{7-5} = ry;
  let Inst{4-2} = rz;
  let Inst{1-0}   = f;
}

//===----------------------------------------------------------------------===//
// Format RRI-A instruction class in Mips : <|opcode|rx|ry|f|imm4|>
//===----------------------------------------------------------------------===//

class FRRI_A16<bits<1> _f, dag outs, dag ins, string asmstr,
               list<dag> pattern, InstrItinClass itin>:
  MipsInst16<outs, ins, asmstr, pattern, itin>
{
  bits<3>  rx;
  bits<3>  ry;
  bits<1>  f;
  bits<4>  imm4;

  let Opcode = 0b01000;
  let  f = _f;

  let Inst{10-8} = rx;
  let Inst{7-5} = ry;
  let Inst{4} = f;
  let Inst{3-0}   = imm4;
}

//===----------------------------------------------------------------------===//
// Format Shift instruction class in Mips : <|opcode|rx|ry|sa|f|>
//===----------------------------------------------------------------------===//

class FSHIFT16<bits<2> _f, dag outs, dag ins, string asmstr,
               list<dag> pattern, InstrItinClass itin>:
  MipsInst16<outs, ins, asmstr, pattern, itin>
{
  bits<3>  rx;
  bits<3>  ry;
  bits<3>  sa;
  bits<2>  f;

  let Opcode = 0b00110;
  let f  = _f;

  let Inst{10-8} = rx;
  let Inst{7-5} = ry;
  let Inst{4-2} = sa;
  let Inst{1-0}   = f;
}

//===----------------------------------------------------------------------===//
// Format i8 instruction class in Mips : <|opcode|funct|imm8>
//===----------------------------------------------------------------------===//

class FI816<bits<3> _func, dag outs, dag ins, string asmstr,
            list<dag> pattern, InstrItinClass itin>:
  MipsInst16<outs, ins, asmstr, pattern, itin>
{
  bits<3>  func;
  bits<8>   imm8;

  let Opcode = 0b01100;
  let func  = _func;

  let Inst{10-8} = func;
  let Inst{7-0} = imm8;
}

//===----------------------------------------------------------------------===//
// Format i8_MOVR32 instruction class in Mips : <|opcode|func|ry|r32>
//===----------------------------------------------------------------------===//

class FI8_MOVR3216<dag outs, dag ins, string asmstr,
                   list<dag> pattern, InstrItinClass itin>:
  MipsInst16<outs, ins, asmstr, pattern, itin>
{
  // FIXME: this seems wrong? 'ry' should be 3 bits, and 'r32' 5?
  bits<4> ry;
  bits<4> r32;

  let Opcode = 0b01100;

  let Inst{10-8} = 0b111;
  let Inst{7-4} = ry;
  let Inst{3-0} = r32;

}



//===----------------------------------------------------------------------===//
// Format i8_MOV32R instruction class in Mips : <|opcode|func|r32|rz>
//===----------------------------------------------------------------------===//

class FI8_MOV32R16<dag outs, dag ins, string asmstr,
                   list<dag> pattern, InstrItinClass itin>:
  MipsInst16<outs, ins, asmstr, pattern, itin>
{

  bits<3>  func;
  bits<5> r32;
  bits<3> rz;


  let Opcode = 0b01100;

  let Inst{10-8} = 0b101;
  let Inst{7-5} = r32{2-0};
  let Inst{4-3} = r32{4-3};
  let Inst{2-0} = rz;

}

//===----------------------------------------------------------------------===//
// Format i8_SVRS instruction class in Mips :
//    <|opcode|svrs|s|ra|s0|s1|framesize>
//===----------------------------------------------------------------------===//

class FI8_SVRS16<bits<1> _s, dag outs, dag ins, string asmstr,
                 list<dag> pattern, InstrItinClass itin>:
  MipsInst16<outs, ins, asmstr, pattern, itin>
{
  bits<1> s;
  bits<1> ra = 0;
  bits<1> s0 = 0;
  bits<1> s1 = 0;
  bits<4> framesize = 0;

  let s =_s;
  let Opcode = 0b01100;

  let Inst{10-8} = 0b100;
  let Inst{7} = s;
  let Inst{6} = ra;
  let Inst{5} = s0;
  let Inst{4} = s1;
  let Inst{3-0} = framesize;

}

//===----------------------------------------------------------------------===//
// Format JAL instruction class in Mips16 :
//    <|opcode|svrs|s|ra|s0|s1|framesize>
//===----------------------------------------------------------------------===//

class FJAL16<bits<1> _X, dag outs, dag ins, string asmstr,
             list<dag> pattern, InstrItinClass itin>:
  MipsInst16_32<outs, ins, asmstr, pattern, itin>
{
  bits<1> X;
  bits<26> imm26;


  let X = _X;

  let Inst{31-27} = 0b00011;
  let Inst{26} = X;
  let Inst{25-21} = imm26{20-16};
  let Inst{20-16} = imm26{25-21};
  let Inst{15-0}  = imm26{15-0};

}

//===----------------------------------------------------------------------===//
// Format EXT-I instruction class in Mips16 :
//     <|EXTEND|imm10:5|imm15:11|op|0|0|0|0|0|0|imm4:0>
//===----------------------------------------------------------------------===//

class FEXT_I16<bits<5> _eop, dag outs, dag ins, string asmstr,
               list<dag> pattern, InstrItinClass itin>:
  MipsInst16_EXTEND<outs, ins, asmstr, pattern, itin>
{
  bits<16> imm16;
  bits<5> eop;

  let eop = _eop;

  let Inst{26-21} = imm16{10-5};
  let Inst{20-16} = imm16{15-11};
  let Inst{15-11} = eop;
  let Inst{10-5} = 0;
  let Inst{4-0} = imm16{4-0};

}

//===----------------------------------------------------------------------===//
// Format ASMACRO instruction class in Mips16 :
//    <EXTEND|select|p4|p3|RRR|p2|p1|p0>
//===----------------------------------------------------------------------===//

class FASMACRO16<dag outs, dag ins, string asmstr,
                 list<dag> pattern, InstrItinClass itin>:
  MipsInst16_EXTEND<outs, ins, asmstr, pattern, itin>
{
  bits<3> select;
  bits<3> p4;
  bits<5> p3;
  bits<5> RRR = 0b11100;
  bits<3> p2;
  bits<3> p1;
  bits<5> p0;


  let Inst{26-24} = select;
  let Inst{23-21} = p4;
  let Inst{20-16} = p3;
  let Inst{15-11} = RRR;
  let Inst{10-8} = p2;
  let Inst{7-5} = p1;
  let Inst{4-0} = p0;

}


//===----------------------------------------------------------------------===//
// Format EXT-RI instruction class in Mips16 :
//    <|EXTEND|imm10:5|imm15:11|op|rx|0|0|0|imm4:0>
//===----------------------------------------------------------------------===//

class FEXT_RI16<bits<5> _op, dag outs, dag ins, string asmstr,
                list<dag> pattern, InstrItinClass itin>:
  MipsInst16_EXTEND<outs, ins, asmstr, pattern, itin>
{
  bits<16> imm16;
  bits<5> op;
  bits<3> rx;

  let op = _op;

  let Inst{26-21} = imm16{10-5};
  let Inst{20-16} = imm16{15-11};
  let Inst{15-11} = op;
  let Inst{10-8} = rx;
  let Inst{7-5} = 0;
  let Inst{4-0} = imm16{4-0};

}

//===----------------------------------------------------------------------===//
// Format EXT-RRI instruction class in Mips16 :
//     <|EXTEND|imm10:5|imm15:11|op|rx|ry|imm4:0>
//===----------------------------------------------------------------------===//

class FEXT_RRI16<bits<5> _op, dag outs, dag ins, string asmstr,
                 list<dag> pattern, InstrItinClass itin>:
  MipsInst16_EXTEND<outs, ins, asmstr, pattern, itin>
{
  bits<5> op;
  bits<16> imm16;
  bits<3> rx;
  bits<3> ry;

  let op=_op;

  let Inst{26-21} = imm16{10-5};
  let Inst{20-16} = imm16{15-11};
  let Inst{15-11} = op;
  let Inst{10-8} = rx;
  let Inst{7-5} = ry;
  let Inst{4-0} = imm16{4-0};

}

//===----------------------------------------------------------------------===//
// Format EXT-RRI-A instruction class in Mips16 :
//    <|EXTEND|imm10:4|imm14:11|RRI-A|rx|ry|f|imm3:0>
//===----------------------------------------------------------------------===//

class FEXT_RRI_A16<bits<1> _f, dag outs, dag ins, string asmstr,
                   list<dag> pattern, InstrItinClass itin>:
  MipsInst16_EXTEND<outs, ins, asmstr, pattern, itin>
{
  bits<15> imm15;
  bits<3> rx;
  bits<3> ry;
  bits<1> f;

  let f = _f;

  let Inst{26-20} = imm15{10-4};
  let Inst{19-16} = imm15{14-11};
  let Inst{15-11} = 0b01000;
  let Inst{10-8} = rx;
  let Inst{7-5} = ry;
  let Inst{4} = f;
  let Inst{3-0} = imm15{3-0};

}

//===----------------------------------------------------------------------===//
// Format EXT-SHIFT instruction class in Mips16 :
//    <|EXTEND|sa 4:0|s5|0|SHIFT|rx|ry|0|f>
//===----------------------------------------------------------------------===//

class FEXT_SHIFT16<bits<2> _f, dag outs, dag ins, string asmstr,
                   list<dag> pattern, InstrItinClass itin>:
  MipsInst16_EXTEND<outs, ins, asmstr, pattern, itin>
{
  bits<6> sa6;
  bits<3> rx;
  bits<3> ry;
  bits<2> f;

  let f = _f;

  let Inst{26-22} = sa6{4-0};
  let Inst{21} = sa6{5};
  let Inst{20-16} = 0;
  let Inst{15-11} = 0b00110;
  let Inst{10-8} = rx;
  let Inst{7-5} = ry;
  let Inst{4-2} = 0;
  let Inst{1-0} = f;

}

//===----------------------------------------------------------------------===//
// Format EXT-I8 instruction class in Mips16 :
//    <|EXTEND|imm10:5|imm15:11|I8|funct|0|imm4:0>
//===----------------------------------------------------------------------===//

class FEXT_I816<bits<3> _funct, dag outs, dag ins, string asmstr,
                list<dag> pattern, InstrItinClass itin>:
  MipsInst16_EXTEND<outs, ins, asmstr, pattern, itin>
{
  bits<16> imm16;
  bits<5> I8;
  bits<3> funct;

  let funct = _funct;
  let I8 = 0b00110;

  let Inst{26-21} = imm16{10-5};
  let Inst{20-16} = imm16{15-11};
  let Inst{15-11} = I8;
  let Inst{10-8} = funct;
  let Inst{7-5} = 0;
  let Inst{4-0} = imm16{4-0};

}

//===----------------------------------------------------------------------===//
// Format EXT-I8_SVRS instruction class in Mips16 :
//    <|EXTEND|xsregs|framesize7:4|aregs|I8|SVRS|s|ra|s0|s1|framesize3:0>
//===----------------------------------------------------------------------===//

class FEXT_I8_SVRS16<bits<1> s_, dag outs, dag ins, string asmstr,
                     list<dag> pattern, InstrItinClass itin>:
  MipsInst16_EXTEND<outs, ins, asmstr, pattern, itin>
{
  bits<3> xsregs =0;
  bits<8> framesize =0;
  bits<3> aregs =0;
  bits<5> I8 = 0b01100;
  bits<3> SVRS = 0b100;
  bits<1> s;
  bits<1> ra = 0;
  bits<1> s0 = 0;
  bits<1> s1 = 0;

  let s= s_;

  let Inst{26-24} = xsregs;
  let Inst{23-20} = framesize{7-4};
  let Inst{19} = 0;
  let Inst{18-16} = aregs;
  let Inst{15-11} = I8;
  let Inst{10-8} = SVRS;
  let Inst{7} = s;
  let Inst{6} = ra;
  let Inst{5} = s0;
  let Inst{4} = s1;
  let Inst{3-0} = framesize{3-0};


}


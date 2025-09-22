//===-- SparcInstrFormats.td - Sparc Instruction Formats ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

class InstSP<dag outs, dag ins, string asmstr, list<dag> pattern,
             InstrItinClass itin = NoItinerary>
   : Instruction {
  field bits<32> Inst;

  let Namespace = "SP";
  let Size = 4;

  bits<2> op;
  let Inst{31-30} = op;               // Top two bits are the 'op' field

  dag OutOperandList = outs;
  dag InOperandList = ins;
  let AsmString   = asmstr;
  let Pattern = pattern;

  let DecoderNamespace = "Sparc";
  field bits<32> SoftFail = 0;

  let Itinerary = itin;
}

//===----------------------------------------------------------------------===//
// Format #2 instruction classes in the Sparc
//===----------------------------------------------------------------------===//

// Format 2 instructions
class F2<dag outs, dag ins, string asmstr, list<dag> pattern,
         InstrItinClass itin = NoItinerary>
   : InstSP<outs, ins, asmstr, pattern, itin> {
  bits<3>  op2;
  bits<22> imm22;
  let op          = 0;    // op = 0
  let Inst{24-22} = op2;
  let Inst{21-0}  = imm22;
}

// Specific F2 classes: SparcV8 manual, page 44
//
class F2_1<bits<3> op2Val, dag outs, dag ins, string asmstr, list<dag> pattern,
           InstrItinClass itin = NoItinerary>
   : F2<outs, ins, asmstr, pattern, itin> {
  bits<5>  rd;

  let op2         = op2Val;

  let Inst{29-25} = rd;
}

class F2_2<bits<3> op2Val, bit annul, dag outs, dag ins, string asmstr,
           list<dag> pattern, InstrItinClass itin = NoItinerary>
   : F2<outs, ins, asmstr, pattern, itin> {
  bits<4>   cond;
  let op2         = op2Val;

  let Inst{29}    = annul;
  let Inst{28-25} = cond;
}

class F2_3<bits<3> op2Val, bit annul, bit pred,
           dag outs, dag ins, string asmstr, list<dag> pattern,
           InstrItinClass itin = NoItinerary>
   : InstSP<outs, ins, asmstr, pattern, itin> {
  bits<2>  cc;
  bits<4>  cond;
  bits<19> imm19;

  let op          = 0;    // op = 0

  let Inst{29}    = annul;
  let Inst{28-25} = cond;
  let Inst{24-22} = op2Val;
  let Inst{21-20} = cc;
  let Inst{19}    = pred;
  let Inst{18-0}  = imm19;
}

class F2_4<bit annul, bit pred, dag outs, dag ins,
           string asmstr, list<dag> pattern, InstrItinClass itin = NoItinerary>
   : InstSP<outs, ins, asmstr, pattern, itin> {
  bits<16> imm16;
  bits<5>  rs1;
  bits<3>  rcond;

  let op          = 0;    // op = 0

  let Inst{29}    = annul;
  let Inst{28}    = 0;
  let Inst{27-25} = rcond;
  let Inst{24-22} = 0b011;
  let Inst{21-20} = imm16{15-14};
  let Inst{19}    = pred;
  let Inst{18-14} = rs1;
  let Inst{13-0}  = imm16{13-0};
}


//===----------------------------------------------------------------------===//
// Format #3 instruction classes in the Sparc
//===----------------------------------------------------------------------===//

class F3<dag outs, dag ins, string asmstr, list<dag> pattern,
         InstrItinClass itin = NoItinerary>
   : InstSP<outs, ins, asmstr, pattern, itin> {
  bits<5> rd;
  bits<6> op3;
  bits<5> rs1;
  let op{1} = 1;   // Op = 2 or 3
  let Inst{29-25} = rd;
  let Inst{24-19} = op3;
  let Inst{18-14} = rs1;
}

// Specific F3 classes: SparcV8 manual, page 44
//
class F3_1_asi<bits<2> opVal, bits<6> op3val, dag outs, dag ins,
           string asmstr, list<dag> pattern, InstrItinClass itin = NoItinerary>
   : F3<outs, ins, asmstr, pattern, itin> {
  bits<8> asi;
  bits<5> rs2;

  let op         = opVal;
  let op3        = op3val;

  let Inst{13}   = 0;     // i field = 0
  let Inst{12-5} = asi;   // address space identifier
  let Inst{4-0}  = rs2;
}

// CAS instructions does not use an immediate even when i=1
class F3_1_cas_asi<bits<2> opVal, bits<6> op3val, dag outs, dag ins,
           string asmstr, list<dag> pattern, InstrItinClass itin = NoItinerary>
   : F3_1_asi<opVal, op3val, outs, ins, asmstr, pattern, itin> {
  let asi = 0;
  let Inst{13}   = 1;     // i field = 1
}

class F3_1<bits<2> opVal, bits<6> op3val, dag outs, dag ins, string asmstr,
       list<dag> pattern, InstrItinClass itin = IIC_iu_instr>
  : F3_1_asi<opVal, op3val, outs, ins, asmstr, pattern, itin> {
  let asi = 0;
}

class F3_2<bits<2> opVal, bits<6> op3val, dag outs, dag ins,
           string asmstr, list<dag> pattern, InstrItinClass itin = IIC_iu_instr>
   : F3<outs, ins, asmstr, pattern, itin> {
  bits<13> simm13;

  let op         = opVal;
  let op3        = op3val;

  let Inst{13}   = 1;     // i field = 1
  let Inst{12-0} = simm13;
}

// floating-point
class F3_3<bits<2> opVal, bits<6> op3val, bits<9> opfval, dag outs, dag ins,
           string asmstr, list<dag> pattern, InstrItinClass itin = NoItinerary>
   : F3<outs, ins, asmstr, pattern, itin> {
  bits<5> rs2;

  let op         = opVal;
  let op3        = op3val;

  let Inst{13-5} = opfval;   // fp opcode
  let Inst{4-0}  = rs2;
}

// floating-point unary operations.
class F3_3u<bits<2> opVal, bits<6> op3val, bits<9> opfval, dag outs, dag ins,
           string asmstr, list<dag> pattern, InstrItinClass itin = NoItinerary>
   : F3<outs, ins, asmstr, pattern, itin> {
  bits<5> rs2;

  let op         = opVal;
  let op3        = op3val;
  let rs1        = 0;

  let Inst{13-5} = opfval;   // fp opcode
  let Inst{4-0}  = rs2;
}

// floating-point compares.
class F3_3c<bits<2> opVal, bits<6> op3val, bits<9> opfval, dag outs, dag ins,
           string asmstr, list<dag> pattern, InstrItinClass itin = NoItinerary>
   : F3<outs, ins, asmstr, pattern, itin> {
  bits<5> rs2;

  let op         = opVal;
  let op3        = op3val;

  let Inst{13-5} = opfval;   // fp opcode
  let Inst{4-0}  = rs2;
}

// Shift by register rs2.
class F3_Sr<bits<2> opVal, bits<6> op3val, bit xVal, dag outs, dag ins,
            string asmstr, list<dag> pattern, InstrItinClass itin = IIC_iu_instr>
   : F3<outs, ins, asmstr, pattern, itin> {
  bit x = xVal;           // 1 for 64-bit shifts.
  bits<5> rs2;

  let op         = opVal;
  let op3        = op3val;

  let Inst{13}   = 0;     // i field = 0
  let Inst{12}   = x;     // extended registers.
  let Inst{4-0}  = rs2;
}

// Shift by immediate.
class F3_Si<bits<2> opVal, bits<6> op3val, bit xVal, dag outs, dag ins,
            string asmstr, list<dag> pattern, InstrItinClass itin = IIC_iu_instr>
   : F3<outs, ins, asmstr, pattern, itin> {
  bit x = xVal;           // 1 for 64-bit shifts.
  bits<6> shcnt;          // shcnt32 / shcnt64.

  let op         = opVal;
  let op3        = op3val;

  let Inst{13}   = 1;     // i field = 1
  let Inst{12}   = x;     // extended registers.
  let Inst{5-0}  = shcnt;
}

// Define rr and ri shift instructions with patterns.
multiclass F3_S<string OpcStr, bits<6> Op3Val, bit XVal, SDNode OpNode,
                ValueType VT, Operand SIT, RegisterClass RC,
                InstrItinClass itin = IIC_iu_instr> {
  def rr : F3_Sr<2, Op3Val, XVal, (outs RC:$rd), (ins RC:$rs1, IntRegs:$rs2),
                 !strconcat(OpcStr, " $rs1, $rs2, $rd"),
                 [(set VT:$rd, (OpNode VT:$rs1, i32:$rs2))],
                 itin>;
  def ri : F3_Si<2, Op3Val, XVal, (outs RC:$rd), (ins RC:$rs1, SIT:$shcnt),
                 !strconcat(OpcStr, " $rs1, $shcnt, $rd"),
                 [(set VT:$rd, (OpNode VT:$rs1, (i32 imm:$shcnt)))],
                 itin>;
}

class F4<bits<6> op3, dag outs, dag ins, string asmstr, list<dag> pattern,
         InstrItinClass itin = NoItinerary>
   : InstSP<outs, ins, asmstr, pattern, itin> {
  bits<5> rd;

  let op          = 2;
  let Inst{29-25} = rd;
  let Inst{24-19} = op3;
}


class F4_1<bits<6> op3, dag outs, dag ins,
           string asmstr, list<dag> pattern,
           InstrItinClass itin = NoItinerary>
   : F4<op3, outs, ins, asmstr, pattern, itin> {
  bit    intcc;
  bits<2> cc;
  bits<4> cond;
  bits<5> rs2;

  let Inst{4-0}   = rs2;
  let Inst{12-11} = cc;
  let Inst{13}    = 0;
  let Inst{17-14} = cond;
  let Inst{18}    = intcc;
}

class F4_2<bits<6> op3, dag outs, dag ins,
            string asmstr, list<dag> pattern,
            InstrItinClass itin = NoItinerary>
   : F4<op3, outs, ins, asmstr, pattern, itin> {
  bit      intcc;
  bits<2>  cc;
  bits<4>  cond;
  bits<11> simm11;

  let Inst{10-0}  = simm11;
  let Inst{12-11} = cc;
  let Inst{13}    = 1;
  let Inst{17-14} = cond;
  let Inst{18}    = intcc;
}

class F4_3<bits<6> op3, bits<6> opf_low, dag outs, dag ins,
           string asmstr, list<dag> pattern,
           InstrItinClass itin = NoItinerary>
   : F4<op3, outs, ins, asmstr, pattern, itin> {
  bits<4> cond;
  bit     intcc;
  bits<2> opf_cc;
  bits<5> rs2;

  let Inst{18}     = 0;
  let Inst{17-14}  = cond;
  let Inst{13}     = intcc;
  let Inst{12-11}  = opf_cc;
  let Inst{10-5}   = opf_low;
  let Inst{4-0}    = rs2;
}

class F4_4r<bits<6> op3, bits<5> opf_low, dag outs, dag ins,
            string asmstr, list<dag> pattern,
            InstrItinClass itin = NoItinerary>
   : F4<op3, outs, ins, asmstr, pattern, itin> {
  bits<5> rs1;
  bits<5> rs2;
  bits<3> rcond;
  let Inst{18-14} = rs1;
  let Inst{13}    = 0;  // IsImm
  let Inst{12-10} = rcond;
  let Inst{9-5}   = opf_low;
  let Inst{4-0}   = rs2;
}


class F4_4i<bits<6> op3, dag outs, dag ins,
            string asmstr, list<dag> pattern,
           InstrItinClass itin = NoItinerary>
   : F4<op3, outs, ins, asmstr, pattern, itin> {
  bits<5>  rs1;
  bits<10> simm10;
  bits<3>  rcond;
  let Inst{18-14} = rs1;
  let Inst{13}    = 1;  // IsImm
  let Inst{12-10} = rcond;
  let Inst{9-0}   = simm10;
}


class TRAPSP<bits<6> op3Val, bit isimm, dag outs, dag ins,
             string asmstr, list<dag> pattern,
             InstrItinClass itin = NoItinerary>
   : F3<outs, ins, asmstr, pattern, itin> {
   bits<4> cond;
   bits<2> cc;

   let op = 0b10;
   let rd{4} = 0;
   let rd{3-0} = cond;
   let op3 = op3Val;
   let Inst{13} = isimm;
   let Inst{12-11} = cc;

}

class TRAPSPrr<bits<6> op3Val, dag outs, dag ins,
               string asmstr, list<dag> pattern,
               InstrItinClass itin = NoItinerary>
   : TRAPSP<op3Val, 0, outs, ins, asmstr, pattern, itin> {
   bits<5> rs2;

   let Inst{10-5} = 0;
   let Inst{4-0}  = rs2;
}

class TRAPSPri<bits<6> op3Val, dag outs, dag ins,
               string asmstr, list<dag> pattern,
               InstrItinClass itin = NoItinerary>
   : TRAPSP<op3Val, 1, outs, ins, asmstr, pattern, itin> {
   bits<8> imm;

   let Inst{10-8} = 0;
   let Inst{7-0}  = imm;
}

// Pseudo-instructions for alternate assembly syntax (never used by codegen).
// These are aliases that require C++ handling to convert to the target
// instruction, while InstAliases can be handled directly by tblgen.
class AsmPseudoInst<dag outs, dag ins, string asm>
  : InstSP<outs, ins, asm, []> {
  let isPseudo = 1;
}

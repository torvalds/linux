//===-- RISCVInstrInfoXCV.td - CORE-V instructions ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the vendor extensions defined by Core-V extensions.
//
//===----------------------------------------------------------------------===//

let DecoderNamespace = "XCVbitmanip" in {
  class CVInstBitManipRII<bits<2> funct2, bits<3> funct3, dag outs, dag ins,
                      string opcodestr, string argstr>
      : RVInstIBase<funct3, OPC_CUSTOM_2, outs, ins, opcodestr, argstr> {
    bits<5> is3;
    bits<5> is2;
    let Inst{31-30} = funct2;
    let Inst{29-25} = is3;
    let Inst{24-20} = is2;
  }

  class CVBitManipRII<bits<2> funct2, bits<3> funct3, string opcodestr,
                      Operand i3type = uimm5>
      : CVInstBitManipRII<funct2, funct3, (outs GPR:$rd),
                          (ins GPR:$rs1, i3type:$is3, uimm5:$is2),
                          opcodestr, "$rd, $rs1, $is3, $is2">;

  class CVBitManipRR<bits<7> funct7, string opcodestr>
      : RVInstR<funct7, 0b011, OPC_CUSTOM_1, (outs GPR:$rd),
                (ins GPR:$rs1, GPR:$rs2), opcodestr, "$rd, $rs1, $rs2">;

  class CVBitManipR<bits<7> funct7, string opcodestr>
      : RVInstR<funct7, 0b011, OPC_CUSTOM_1, (outs GPR:$rd),
                (ins GPR:$rs1), opcodestr, "$rd, $rs1"> {
    let rs2 = 0b00000;
  }
}

let Predicates = [HasVendorXCVbitmanip, IsRV32],
    hasSideEffects = 0, mayLoad = 0, mayStore = 0 in {
  def CV_EXTRACT : CVBitManipRII<0b00, 0b000, "cv.extract">;
  def CV_EXTRACTU : CVBitManipRII<0b01, 0b000, "cv.extractu">;

  def CV_BCLR : CVBitManipRII<0b00, 0b001, "cv.bclr">;
  def CV_BSET : CVBitManipRII<0b01, 0b001, "cv.bset">;
  def CV_BITREV : CVBitManipRII<0b11, 0b001, "cv.bitrev", uimm2>;

  def CV_EXTRACTR : CVBitManipRR<0b0011000, "cv.extractr">;
  def CV_EXTRACTUR : CVBitManipRR<0b0011001, "cv.extractur">;

  let Constraints = "$rd = $rd_wb" in {
    def CV_INSERT : CVInstBitManipRII<0b10, 0b000, (outs GPR:$rd_wb),
                             (ins GPR:$rd, GPR:$rs1, uimm5:$is3, uimm5:$is2),
                             "cv.insert", "$rd, $rs1, $is3, $is2">;
    def CV_INSERTR : RVInstR<0b0011010, 0b011, OPC_CUSTOM_1, (outs GPR:$rd_wb),
                             (ins GPR:$rd, GPR:$rs1, GPR:$rs2),
                             "cv.insertr", "$rd, $rs1, $rs2">;
  }

  def CV_BCLRR : CVBitManipRR<0b0011100, "cv.bclrr">;
  def CV_BSETR : CVBitManipRR<0b0011101, "cv.bsetr">;

  def CV_ROR : CVBitManipRR<0b0100000, "cv.ror">;
  def CV_FF1 : CVBitManipR<0b0100001, "cv.ff1">;
  def CV_FL1 : CVBitManipR<0b0100010, "cv.fl1">;
  def CV_CLB : CVBitManipR<0b0100011, "cv.clb">;
  def CV_CNT : CVBitManipR<0b0100100, "cv.cnt">;
}

class CVInstMac<bits<7> funct7, bits<3> funct3, string opcodestr>
    : RVInstR<funct7, funct3, OPC_CUSTOM_1,
              (outs GPR:$rd_wb), (ins GPR:$rd, GPR:$rs1, GPR:$rs2),
              opcodestr, "$rd, $rs1, $rs2"> {
  let DecoderNamespace = "XCVmac";
}

class CVInstMacMulN<bits<2> funct2, bits<3> funct3, dag outs, dag ins,
                    string opcodestr>
    : RVInstRBase<funct3, OPC_CUSTOM_2, outs, ins, opcodestr,
                  "$rd, $rs1, $rs2, $imm5"> {
  bits<5> imm5;

  let Inst{31-30} = funct2;
  let Inst{29-25} = imm5;
  let DecoderNamespace = "XCVmac";
}

class CVInstMacN<bits<2> funct2, bits<3> funct3, string opcodestr>
    : CVInstMacMulN<funct2, funct3, (outs GPR:$rd_wb),
                    (ins GPR:$rd, GPR:$rs1, GPR:$rs2, uimm5:$imm5), opcodestr>;

class CVInstMulN<bits<2> funct2, bits<3> funct3, string opcodestr>
    : CVInstMacMulN<funct2, funct3, (outs GPR:$rd),
                    (ins GPR:$rs1, GPR:$rs2, uimm5:$imm5), opcodestr>;

let Predicates = [HasVendorXCVmac, IsRV32], hasSideEffects = 0, mayLoad = 0,
    mayStore = 0, Constraints = "$rd = $rd_wb" in {
  // 32x32 bit macs
  def CV_MAC      : CVInstMac<0b1001000, 0b011, "cv.mac">,
                    Sched<[]>;
  def CV_MSU      : CVInstMac<0b1001001, 0b011, "cv.msu">,
                    Sched<[]>;

  // Signed 16x16 bit macs with imm
  def CV_MACSN    : CVInstMacN<0b00, 0b110, "cv.macsn">,
                    Sched<[]>;
  def CV_MACHHSN  : CVInstMacN<0b01, 0b110, "cv.machhsn">,
                    Sched<[]>;
  def CV_MACSRN   : CVInstMacN<0b10, 0b110, "cv.macsrn">,
                    Sched<[]>;
  def CV_MACHHSRN : CVInstMacN<0b11, 0b110, "cv.machhsrn">,
                    Sched<[]>;

  // Unsigned 16x16 bit macs with imm
  def CV_MACUN    : CVInstMacN<0b00, 0b111, "cv.macun">,
                    Sched<[]>;
  def CV_MACHHUN  : CVInstMacN<0b01, 0b111, "cv.machhun">,
                    Sched<[]>;
  def CV_MACURN   : CVInstMacN<0b10, 0b111, "cv.macurn">,
                    Sched<[]>;
  def CV_MACHHURN : CVInstMacN<0b11, 0b111, "cv.machhurn">,
                    Sched<[]>;
} // Predicates = [HasVendorXCVmac, IsRV32], hasSideEffects = 0, mayLoad = 0...

let Predicates = [HasVendorXCVmac, IsRV32], hasSideEffects = 0, mayLoad = 0, mayStore = 0 in {
  // Signed 16x16 bit muls with imm
  def CV_MULSN    : CVInstMulN<0b00, 0b100, "cv.mulsn">,
                    Sched<[]>;
  def CV_MULHHSN  : CVInstMulN<0b01, 0b100, "cv.mulhhsn">,
                    Sched<[]>;
  def CV_MULSRN   : CVInstMulN<0b10, 0b100, "cv.mulsrn">,
                    Sched<[]>;
  def CV_MULHHSRN : CVInstMulN<0b11, 0b100, "cv.mulhhsrn">,
                    Sched<[]>;

  // Unsigned 16x16 bit muls with imm
  def CV_MULUN    : CVInstMulN<0b00, 0b101, "cv.mulun">,
                    Sched<[]>;
  def CV_MULHHUN  : CVInstMulN<0b01, 0b101, "cv.mulhhun">,
                    Sched<[]>;
  def CV_MULURN   : CVInstMulN<0b10, 0b101, "cv.mulurn">,
                    Sched<[]>;
  def CV_MULHHURN : CVInstMulN<0b11, 0b101, "cv.mulhhurn">,
                    Sched<[]>;
} // Predicates = [HasVendorXCVmac, IsRV32], hasSideEffects = 0, mayLoad = 0...

let Predicates = [HasVendorXCVmac, IsRV32] in {
  // Xcvmac Pseudo Instructions
  // Signed 16x16 bit muls
  def : InstAlias<"cv.muls $rd1, $rs1, $rs2",
                  (CV_MULSN GPR:$rd1,   GPR:$rs1, GPR:$rs2, 0)>;
  def : InstAlias<"cv.mulhhs $rd1, $rs1, $rs2",
                  (CV_MULHHSN GPR:$rd1, GPR:$rs1, GPR:$rs2, 0)>;

  // Unsigned 16x16 bit muls
  def : InstAlias<"cv.mulu $rd1, $rs1, $rs2",
                  (CV_MULUN GPR:$rd1,   GPR:$rs1, GPR:$rs2, 0)>;
  def : InstAlias<"cv.mulhhu $rd1, $rs1, $rs2",
                  (CV_MULHHUN GPR:$rd1, GPR:$rs1, GPR:$rs2, 0)>;
} // Predicates = [HasVendorXCVmac, IsRV32]

let DecoderNamespace = "XCValu" in {
  class CVInstAluRRI<bits<2> funct2, bits<3> funct3, string opcodestr>
      : RVInstRBase<funct3, OPC_CUSTOM_2, (outs GPR:$rd),
                    (ins GPR:$rs1, GPR:$rs2, uimm5:$imm5), opcodestr,
                    "$rd, $rs1, $rs2, $imm5"> {
    bits<5> imm5;

    let Inst{31-30} = funct2;
    let Inst{29-25} = imm5;
  }

  class CVInstAluRR<bits<7> funct7, bits<3> funct3, string opcodestr>
    : RVInstR<funct7, funct3, OPC_CUSTOM_1, (outs GPR:$rd),
              (ins GPR:$rs1, GPR:$rs2), opcodestr, "$rd, $rs1, $rs2">;

  class CVInstAluRRNR<bits<7> funct7, bits<3> funct3, string opcodestr>
    : RVInstR<funct7, funct3, OPC_CUSTOM_1, (outs GPR:$rd_wb),
              (ins GPR:$rd, GPR:$rs1, GPR:$rs2), opcodestr, "$rd, $rs1, $rs2">;

  class CVInstAluRI<bits<7> funct7, bits<3> funct3, string opcodestr>
      : RVInstIBase<funct3, OPC_CUSTOM_1, (outs GPR:$rd),
                    (ins GPR:$rs1, uimm5:$imm5), opcodestr,
                    "$rd, $rs1, $imm5"> {
    bits<5> imm5;

    let Inst{31-25} = funct7;
    let Inst{24-20} = imm5;
  }

  class CVInstAluR<bits<7> funct7, bits<3> funct3, string opcodestr>
    : RVInstR<funct7, funct3, OPC_CUSTOM_1, (outs GPR:$rd), (ins GPR:$rs1),
              opcodestr, "$rd, $rs1"> {
     let rs2 = 0b00000;
  }

} // DecoderNamespace = "XCValu"

let Predicates = [HasVendorXCValu, IsRV32],
  hasSideEffects = 0, mayLoad = 0, mayStore = 0 in {
  // General ALU Operations
  def CV_ABS    : CVInstAluR<0b0101000, 0b011, "cv.abs">,
                  Sched<[]>;
  def CV_SLET   : CVInstAluRR<0b0101001, 0b011, "cv.slet">,
                  Sched<[]>;
  def CV_SLETU  : CVInstAluRR<0b0101010, 0b011, "cv.sletu">,
                  Sched<[]>;
  def CV_MIN    : CVInstAluRR<0b0101011, 0b011, "cv.min">,
                  Sched<[]>;
  def CV_MINU   : CVInstAluRR<0b0101100, 0b011, "cv.minu">,
                  Sched<[]>;
  def CV_MAX    : CVInstAluRR<0b0101101, 0b011, "cv.max">,
                  Sched<[]>;
  def CV_MAXU   : CVInstAluRR<0b0101110, 0b011, "cv.maxu">,
                  Sched<[]>;
  def CV_EXTHS  : CVInstAluR<0b0110000, 0b011, "cv.exths">,
                  Sched<[]>;
  def CV_EXTHZ  : CVInstAluR<0b0110001, 0b011, "cv.exthz">,
                  Sched<[]>;
  def CV_EXTBS  : CVInstAluR<0b0110010, 0b011, "cv.extbs">,
                  Sched<[]>;
  def CV_EXTBZ  : CVInstAluR<0b0110011, 0b011, "cv.extbz">,
                  Sched<[]>;

  def CV_CLIP   : CVInstAluRI<0b0111000, 0b011, "cv.clip">,
                  Sched<[]>;
  def CV_CLIPU  : CVInstAluRI<0b0111001, 0b011, "cv.clipu">,
                  Sched<[]>;
  def CV_CLIPR  : CVInstAluRR<0b0111010, 0b011, "cv.clipr">,
                  Sched<[]>;
  def CV_CLIPUR : CVInstAluRR<0b0111011, 0b011, "cv.clipur">,
                  Sched<[]>;

  def CV_ADDN   : CVInstAluRRI<0b00, 0b010, "cv.addn">,
                  Sched<[]>;
  def CV_ADDUN  : CVInstAluRRI<0b01, 0b010, "cv.addun">,
                  Sched<[]>;
  def CV_ADDRN  : CVInstAluRRI<0b10, 0b010, "cv.addrn">,
                  Sched<[]>;
  def CV_ADDURN : CVInstAluRRI<0b11, 0b010, "cv.addurn">,
                  Sched<[]>;
  def CV_SUBN   : CVInstAluRRI<0b00, 0b011, "cv.subn">,
                  Sched<[]>;
  def CV_SUBUN  : CVInstAluRRI<0b01, 0b011, "cv.subun">,
                  Sched<[]>;
  def CV_SUBRN  : CVInstAluRRI<0b10, 0b011, "cv.subrn">,
                  Sched<[]>;
  def CV_SUBURN : CVInstAluRRI<0b11, 0b011, "cv.suburn">,
                  Sched<[]>;
} // Predicates = [HasVendorXCValu, IsRV32],
  //   hasSideEffects = 0, mayLoad = 0, mayStore = 0

let Predicates = [HasVendorXCValu, IsRV32],
  hasSideEffects = 0, mayLoad = 0, mayStore = 0,
  Constraints = "$rd = $rd_wb" in {
  def CV_ADDNR   : CVInstAluRRNR<0b1000000, 0b011, "cv.addnr">,
                   Sched<[]>;
  def CV_ADDUNR  : CVInstAluRRNR<0b1000001, 0b011, "cv.addunr">,
                   Sched<[]>;
  def CV_ADDRNR  : CVInstAluRRNR<0b1000010, 0b011, "cv.addrnr">,
                   Sched<[]>;
  def CV_ADDURNR : CVInstAluRRNR<0b1000011, 0b011, "cv.addurnr">,
                   Sched<[]>;
  def CV_SUBNR   : CVInstAluRRNR<0b1000100, 0b011, "cv.subnr">,
                   Sched<[]>;
  def CV_SUBUNR  : CVInstAluRRNR<0b1000101, 0b011, "cv.subunr">,
                   Sched<[]>;
  def CV_SUBRNR  : CVInstAluRRNR<0b1000110, 0b011, "cv.subrnr">,
                   Sched<[]>;
  def CV_SUBURNR : CVInstAluRRNR<0b1000111, 0b011, "cv.suburnr">,
                   Sched<[]>;

} // Predicates = [HasVendorXCValu, IsRV32],
  //   hasSideEffects = 0, mayLoad = 0, mayStore = 0,
  //   Constraints = "$rd = $rd_wb"


class CVInstSIMDRR<bits<5> funct5, bit F, bit funct1, bits<3> funct3,
                   RISCVOpcode opcode, dag outs,
                   dag ins, string opcodestr, string argstr>
    : RVInstRBase<funct3, opcode, outs, ins, opcodestr, argstr> {
  let Inst{31-27} = funct5;
  let Inst{26} = F;
  let Inst{25} = funct1;
  let DecoderNamespace = "XCVsimd";
}

class CVInstSIMDRI<bits<5> funct5, bit F, bits<3> funct3, RISCVOpcode opcode,
                   dag outs, dag ins, string opcodestr, string argstr>
    : RVInstIBase<funct3, opcode, outs, ins, opcodestr, argstr> {
  bits<6> imm6;

  let Inst{31-27} = funct5;
  let Inst{26} = F;
  let Inst{25} = imm6{0}; // funct1 unused
  let Inst{24-20} = imm6{5-1};
  let DecoderNamespace = "XCVsimd";
}

class CVSIMDRR<bits<5> funct5, bit F, bit funct1, bits<3> funct3,
               string opcodestr>
    : CVInstSIMDRR<funct5, F, funct1, funct3, OPC_CUSTOM_3, (outs GPR:$rd),
              (ins GPR:$rs1, GPR:$rs2), opcodestr, "$rd, $rs1, $rs2">;

class CVSIMDRRWb<bits<5> funct5, bit F, bit funct1, bits<3> funct3,
                 string opcodestr>
    : CVInstSIMDRR<funct5, F, funct1, funct3, OPC_CUSTOM_3, (outs GPR:$rd_wb),
              (ins GPR:$rd, GPR:$rs1, GPR:$rs2), opcodestr, "$rd, $rs1, $rs2"> {
  let Constraints = "$rd = $rd_wb";
}

class CVSIMDRI<bits<5> funct5, bit F, bits<3> funct3, string opcodestr>
    : CVInstSIMDRI<funct5, F, funct3, OPC_CUSTOM_3, (outs GPR:$rd),
              (ins GPR:$rs1, simm6:$imm6), opcodestr, "$rd, $rs1, $imm6">;

class CVSIMDRIWb<bits<5> funct5, bit F, bits<3> funct3, string opcodestr>
    : CVInstSIMDRI<funct5, F, funct3, OPC_CUSTOM_3,
                   (outs GPR:$rd_wb), (ins GPR:$rd, GPR:$rs1, simm6:$imm6),
                   opcodestr, "$rd, $rs1, $imm6"> {
  let Constraints = "$rd = $rd_wb";
}

class CVSIMDRU<bits<5> funct5, bit F, bits<3> funct3, string opcodestr,
               Operand immtype = uimm6>
    : CVInstSIMDRI<funct5, F, funct3, OPC_CUSTOM_3,
                   (outs GPR:$rd), (ins GPR:$rs1, immtype:$imm6),
                   opcodestr, "$rd, $rs1, $imm6">;

class CVSIMDRUWb<bits<5> funct5, bit F, bits<3> funct3, string opcodestr>
    : CVInstSIMDRI<funct5, F, funct3, OPC_CUSTOM_3,
                   (outs GPR:$rd_wb),
                   (ins GPR:$rd, GPR:$rs1, uimm6:$imm6),
                   opcodestr, "$rd, $rs1, $imm6"> {
  let Constraints = "$rd = $rd_wb";
}

class CVSIMDR<bits<5> funct5, bit F, bit funct1, bits<3> funct3,
              string opcodestr>
    : CVInstSIMDRR<funct5, F, funct1, funct3, OPC_CUSTOM_3, (outs GPR:$rd),
                  (ins GPR:$rs1), opcodestr, "$rd, $rs1"> {
  let rs2 = 0b00000;
}

multiclass CVSIMDBinarySigned<bits<5> funct5, bit F, bit funct1, string mnemonic> {
  def CV_ # NAME # _H : CVSIMDRR<funct5, F, funct1, 0b000, "cv." # mnemonic # ".h">;
  def CV_ # NAME # _B : CVSIMDRR<funct5, F, funct1, 0b001, "cv." # mnemonic # ".b">;
  def CV_ # NAME # _SC_H : CVSIMDRR<funct5, F, funct1, 0b100, "cv." # mnemonic # ".sc.h">;
  def CV_ # NAME # _SC_B : CVSIMDRR<funct5, F, funct1, 0b101, "cv." # mnemonic # ".sc.b">;
  def CV_ # NAME # _SCI_H : CVSIMDRI<funct5, F, 0b110, "cv." # mnemonic # ".sci.h">;
  def CV_ # NAME # _SCI_B : CVSIMDRI<funct5, F, 0b111, "cv." # mnemonic # ".sci.b">;
}

multiclass CVSIMDBinaryUnsigned<bits<5> funct5, bit F, bit funct1, string mnemonic> {
  def CV_ # NAME # _H : CVSIMDRR<funct5, F, funct1, 0b000, "cv." # mnemonic # ".h">;
  def CV_ # NAME # _B : CVSIMDRR<funct5, F, funct1, 0b001, "cv." # mnemonic # ".b">;
  def CV_ # NAME # _SC_H : CVSIMDRR<funct5, F, funct1, 0b100, "cv." # mnemonic # ".sc.h">;
  def CV_ # NAME # _SC_B : CVSIMDRR<funct5, F, funct1, 0b101, "cv." # mnemonic # ".sc.b">;
  def CV_ # NAME # _SCI_H : CVSIMDRU<funct5, F, 0b110, "cv." # mnemonic # ".sci.h">;
  def CV_ # NAME # _SCI_B : CVSIMDRU<funct5, F, 0b111, "cv." # mnemonic # ".sci.b">;
}

multiclass CVSIMDShift<bits<5> funct5, bit F, bit funct1, string mnemonic> {
  def CV_ # NAME # _H : CVSIMDRR<funct5, F, funct1, 0b000, "cv." # mnemonic # ".h">;
  def CV_ # NAME # _B : CVSIMDRR<funct5, F, funct1, 0b001, "cv." # mnemonic # ".b">;
  def CV_ # NAME # _SC_H : CVSIMDRR<funct5, F, funct1, 0b100, "cv." # mnemonic # ".sc.h">;
  def CV_ # NAME # _SC_B : CVSIMDRR<funct5, F, funct1, 0b101, "cv." # mnemonic # ".sc.b">;
  def CV_ # NAME # _SCI_H : CVSIMDRU<funct5, F, 0b110, "cv." # mnemonic # ".sci.h", uimm4>;
  def CV_ # NAME # _SCI_B : CVSIMDRU<funct5, F, 0b111, "cv." # mnemonic # ".sci.b", uimm3>;
}

multiclass CVSIMDBinarySignedWb<bits<5> funct5, bit F, bit funct1, string mnemonic> {
  def CV_ # NAME # _H : CVSIMDRRWb<funct5, F, funct1, 0b000, "cv." # mnemonic # ".h">;
  def CV_ # NAME # _B : CVSIMDRRWb<funct5, F, funct1, 0b001, "cv." # mnemonic # ".b">;
  def CV_ # NAME # _SC_H : CVSIMDRRWb<funct5, F, funct1, 0b100, "cv." # mnemonic # ".sc.h">;
  def CV_ # NAME # _SC_B : CVSIMDRRWb<funct5, F, funct1, 0b101, "cv." # mnemonic # ".sc.b">;
  def CV_ # NAME # _SCI_H : CVSIMDRIWb<funct5, F, 0b110, "cv." # mnemonic # ".sci.h">;
  def CV_ # NAME # _SCI_B : CVSIMDRIWb<funct5, F, 0b111, "cv." # mnemonic # ".sci.b">;
}

multiclass CVSIMDBinaryUnsignedWb<bits<5> funct5, bit F, bit funct1, string mnemonic> {
  def CV_ # NAME # _H : CVSIMDRRWb<funct5, F, funct1, 0b000, "cv." # mnemonic # ".h">;
  def CV_ # NAME # _B : CVSIMDRRWb<funct5, F, funct1, 0b001, "cv." # mnemonic # ".b">;
  def CV_ # NAME # _SC_H : CVSIMDRRWb<funct5, F, funct1, 0b100, "cv." # mnemonic # ".sc.h">;
  def CV_ # NAME # _SC_B : CVSIMDRRWb<funct5, F, funct1, 0b101, "cv." # mnemonic # ".sc.b">;
  def CV_ # NAME # _SCI_H : CVSIMDRUWb<funct5, F, 0b110, "cv." # mnemonic # ".sci.h">;
  def CV_ # NAME # _SCI_B : CVSIMDRUWb<funct5, F, 0b111, "cv." # mnemonic # ".sci.b">;
}


let Predicates = [HasVendorXCVsimd, IsRV32],
    hasSideEffects = 0, mayLoad = 0, mayStore = 0 in {
  defm ADD :    CVSIMDBinarySigned<0b00000, 0, 0, "add">;
  defm SUB :    CVSIMDBinarySigned<0b00001, 0, 0, "sub">;
  defm AVG :    CVSIMDBinarySigned<0b00010, 0, 0, "avg">;
  defm AVGU : CVSIMDBinaryUnsigned<0b00011, 0, 0, "avgu">;
  defm MIN :    CVSIMDBinarySigned<0b00100, 0, 0, "min">;
  defm MINU : CVSIMDBinaryUnsigned<0b00101, 0, 0, "minu">;
  defm MAX :    CVSIMDBinarySigned<0b00110, 0, 0, "max">;
  defm MAXU : CVSIMDBinaryUnsigned<0b00111, 0, 0, "maxu">;
  defm SRL :           CVSIMDShift<0b01000, 0, 0, "srl">;
  defm SRA :           CVSIMDShift<0b01001, 0, 0, "sra">;
  defm SLL :           CVSIMDShift<0b01010, 0, 0, "sll">;
  defm OR :     CVSIMDBinarySigned<0b01011, 0, 0, "or">;
  defm XOR :    CVSIMDBinarySigned<0b01100, 0, 0, "xor">;
  defm AND :    CVSIMDBinarySigned<0b01101, 0, 0, "and">;

  def CV_ABS_H :    CVSIMDR<0b01110, 0, 0, 0b000, "cv.abs.h">;
  def CV_ABS_B :    CVSIMDR<0b01110, 0, 0, 0b001, "cv.abs.b">;

  // 0b01111xx: UNDEF

  defm DOTUP :   CVSIMDBinaryUnsigned<0b10000, 0, 0, "dotup">;
  defm DOTUSP :  CVSIMDBinarySigned<0b10001, 0, 0, "dotusp">;
  defm DOTSP :   CVSIMDBinarySigned<0b10010, 0, 0, "dotsp">;
  defm SDOTUP :  CVSIMDBinaryUnsignedWb<0b10011, 0, 0, "sdotup">;
  defm SDOTUSP : CVSIMDBinarySignedWb<0b10100, 0, 0, "sdotusp">;
  defm SDOTSP :  CVSIMDBinarySignedWb<0b10101, 0, 0, "sdotsp">;

  // 0b10110xx: UNDEF

  def CV_EXTRACT_H :    CVSIMDRU<0b10111, 0, 0b000, "cv.extract.h">;
  def CV_EXTRACT_B :    CVSIMDRU<0b10111, 0, 0b001, "cv.extract.b">;
  def CV_EXTRACTU_H :   CVSIMDRU<0b10111, 0, 0b010, "cv.extractu.h">;
  def CV_EXTRACTU_B :   CVSIMDRU<0b10111, 0, 0b011, "cv.extractu.b">;
  def CV_INSERT_H :     CVSIMDRUWb<0b10111, 0, 0b100, "cv.insert.h">;
  def CV_INSERT_B :     CVSIMDRUWb<0b10111, 0, 0b101, "cv.insert.b">;

  def CV_SHUFFLE_H :    CVSIMDRR<0b11000, 0, 0, 0b000, "cv.shuffle.h">;
  def CV_SHUFFLE_B :    CVSIMDRR<0b11000, 0, 0, 0b001, "cv.shuffle.b">;
  def CV_SHUFFLE_SCI_H :   CVSIMDRU<0b11000, 0, 0b110, "cv.shuffle.sci.h">;
  def CV_SHUFFLEI0_SCI_B : CVSIMDRU<0b11000, 0, 0b111, "cv.shufflei0.sci.b">;

  def CV_SHUFFLEI1_SCI_B : CVSIMDRU<0b11001, 0, 0b111, "cv.shufflei1.sci.b">;

  def CV_SHUFFLEI2_SCI_B : CVSIMDRU<0b11010, 0, 0b111, "cv.shufflei2.sci.b">;

  def CV_SHUFFLEI3_SCI_B : CVSIMDRU<0b11011, 0, 0b111, "cv.shufflei3.sci.b">;

  def CV_SHUFFLE2_H :    CVSIMDRRWb<0b11100, 0, 0, 0b000, "cv.shuffle2.h">;
  def CV_SHUFFLE2_B :    CVSIMDRRWb<0b11100, 0, 0, 0b001, "cv.shuffle2.b">;

  // 0b11101xx: UNDEF

  def CV_PACK :      CVSIMDRR<0b11110, 0, 0, 0b000, "cv.pack">;
  def CV_PACK_H :    CVSIMDRR<0b11110, 0, 1, 0b000, "cv.pack.h">;

  def CV_PACKHI_B : CVSIMDRRWb<0b11111, 0, 1, 0b001, "cv.packhi.b">;
  def CV_PACKLO_B : CVSIMDRRWb<0b11111, 0, 0, 0b001, "cv.packlo.b">;

  defm CMPEQ :  CVSIMDBinarySigned<0b00000, 1, 0, "cmpeq">;
  defm CMPNE :  CVSIMDBinarySigned<0b00001, 1, 0, "cmpne">;
  defm CMPGT :  CVSIMDBinarySigned<0b00010, 1, 0, "cmpgt">;
  defm CMPGE :  CVSIMDBinarySigned<0b00011, 1, 0, "cmpge">;
  defm CMPLT :  CVSIMDBinarySigned<0b00100, 1, 0, "cmplt">;
  defm CMPLE :  CVSIMDBinarySigned<0b00101, 1, 0, "cmple">;
  defm CMPGTU : CVSIMDBinaryUnsigned<0b00110, 1, 0, "cmpgtu">;
  defm CMPGEU : CVSIMDBinaryUnsigned<0b00111, 1, 0, "cmpgeu">;
  defm CMPLTU : CVSIMDBinaryUnsigned<0b01000, 1, 0, "cmpltu">;
  defm CMPLEU : CVSIMDBinaryUnsigned<0b01001, 1, 0, "cmpleu">;

  def CV_CPLXMUL_R :      CVSIMDRRWb<0b01010, 1, 0, 0b000, "cv.cplxmul.r">;
  def CV_CPLXMUL_I :      CVSIMDRRWb<0b01010, 1, 1, 0b000, "cv.cplxmul.i">;
  def CV_CPLXMUL_R_DIV2 : CVSIMDRRWb<0b01010, 1, 0, 0b010, "cv.cplxmul.r.div2">;
  def CV_CPLXMUL_I_DIV2 : CVSIMDRRWb<0b01010, 1, 1, 0b010, "cv.cplxmul.i.div2">;
  def CV_CPLXMUL_R_DIV4 : CVSIMDRRWb<0b01010, 1, 0, 0b100, "cv.cplxmul.r.div4">;
  def CV_CPLXMUL_I_DIV4 : CVSIMDRRWb<0b01010, 1, 1, 0b100, "cv.cplxmul.i.div4">;
  def CV_CPLXMUL_R_DIV8 : CVSIMDRRWb<0b01010, 1, 0, 0b110, "cv.cplxmul.r.div8">;
  def CV_CPLXMUL_I_DIV8 : CVSIMDRRWb<0b01010, 1, 1, 0b110, "cv.cplxmul.i.div8">;

  def CV_CPLXCONJ :    CVSIMDR<0b01011, 1, 0, 0b000, "cv.cplxconj">;

  // 0b01011xx: UNDEF

  def CV_SUBROTMJ :      CVSIMDRR<0b01100, 1, 0, 0b000, "cv.subrotmj">;
  def CV_SUBROTMJ_DIV2 : CVSIMDRR<0b01100, 1, 0, 0b010, "cv.subrotmj.div2">;
  def CV_SUBROTMJ_DIV4 : CVSIMDRR<0b01100, 1, 0, 0b100, "cv.subrotmj.div4">;
  def CV_SUBROTMJ_DIV8 : CVSIMDRR<0b01100, 1, 0, 0b110, "cv.subrotmj.div8">;

  def CV_ADD_DIV2 :    CVSIMDRR<0b01101, 1, 0, 0b010, "cv.add.div2">;
  def CV_ADD_DIV4 :    CVSIMDRR<0b01101, 1, 0, 0b100, "cv.add.div4">;
  def CV_ADD_DIV8 :    CVSIMDRR<0b01101, 1, 0, 0b110, "cv.add.div8">;

  def CV_SUB_DIV2 :    CVSIMDRR<0b01110, 1, 0, 0b010, "cv.sub.div2">;
  def CV_SUB_DIV4 :    CVSIMDRR<0b01110, 1, 0, 0b100, "cv.sub.div4">;
  def CV_SUB_DIV8 :    CVSIMDRR<0b01110, 1, 0, 0b110, "cv.sub.div8">;
}

class CVInstImmBranch<bits<3> funct3, dag outs, dag ins,
                      string opcodestr, string argstr>
    : RVInstB<funct3, OPC_CUSTOM_0, outs, ins, opcodestr, argstr> {
  bits<5> imm5;
  let rs2 = imm5;
  let DecoderNamespace = "XCVbi";
}

let Predicates = [HasVendorXCVbi, IsRV32], hasSideEffects = 0, mayLoad = 0,
    mayStore = 0, isBranch = 1, isTerminator = 1 in {
  // Immediate branching operations
  def CV_BEQIMM : CVInstImmBranch<0b110, (outs),
        (ins GPR:$rs1, simm5:$imm5, simm13_lsb0:$imm12),
        "cv.beqimm", "$rs1, $imm5, $imm12">, Sched<[]>;
  def CV_BNEIMM : CVInstImmBranch<0b111, (outs),
        (ins GPR:$rs1, simm5:$imm5, simm13_lsb0:$imm12),
        "cv.bneimm", "$rs1, $imm5, $imm12">, Sched<[]>;
}

def CVrrAsmOperand : AsmOperandClass {
  let Name = "RegReg";
  let ParserMethod = "parseRegReg";
  let DiagnosticType = "InvalidRegReg";
}

def CVrr : Operand<i32>,
           ComplexPattern<i32, 2, "SelectAddrRegReg",[]> {
   let ParserMatchClass = CVrrAsmOperand;
   let EncoderMethod =  "getRegReg";
   let DecoderMethod = "decodeRegReg";
   let PrintMethod = "printRegReg";
   let MIOperandInfo = (ops GPR:$base, GPR:$offset);
}

class CVLoad_ri_inc<bits<3> funct3, string opcodestr>
    : RVInstI<funct3, OPC_CUSTOM_0, (outs GPR:$rd, GPR:$rs1_wb), (ins GPRMem:$rs1, simm12:$imm12),
              opcodestr, "$rd, (${rs1}), ${imm12}"> {
  let Constraints = "$rs1_wb = $rs1";
  let DecoderNamespace = "XCVmem";
}

class CVLoad_rr_inc<bits<7> funct7, bits<3> funct3, string opcodestr>
    : RVInstR<funct7, funct3, OPC_CUSTOM_1, (outs GPR:$rd, GPR:$rs1_wb), (ins GPRMem:$rs1, GPR:$rs2),
              opcodestr, "$rd, (${rs1}), ${rs2}"> {
  let Constraints = "$rs1_wb = $rs1";
  let DecoderNamespace = "XCVmem";
}

class CVLoad_rr<bits<7> funct7, bits<3> funct3, string opcodestr>
    : RVInstR<funct7, funct3, OPC_CUSTOM_1, (outs GPR:$rd), (ins CVrr:$cvrr),
              opcodestr, "$rd, $cvrr"> {
  bits<5> rd;
  bits<10> cvrr;

  let Inst{31-25} = funct7;
  let Inst{24-20} = cvrr{4-0};
  let Inst{19-15} = cvrr{9-5};
  let Inst{14-12} = funct3;
  let Inst{11-7} = rd;
  let DecoderNamespace = "XCVmem";
}

let Predicates = [HasVendorXCVmem, IsRV32], hasSideEffects = 0,
                 mayLoad = 1, mayStore = 0, Constraints = "$rs1_wb = $rs1" in {
  // Register-Immediate load with post-increment
  def CV_LB_ri_inc  : CVLoad_ri_inc<0b000, "cv.lb">;
  def CV_LBU_ri_inc : CVLoad_ri_inc<0b100, "cv.lbu">;
  def CV_LH_ri_inc  : CVLoad_ri_inc<0b001, "cv.lh">;
  def CV_LHU_ri_inc : CVLoad_ri_inc<0b101, "cv.lhu">;
  def CV_LW_ri_inc  : CVLoad_ri_inc<0b010, "cv.lw">;

  // Register-Register load with post-increment
  def CV_LB_rr_inc  : CVLoad_rr_inc<0b0000000, 0b011, "cv.lb">;
  def CV_LBU_rr_inc : CVLoad_rr_inc<0b0001000, 0b011, "cv.lbu">;
  def CV_LH_rr_inc  : CVLoad_rr_inc<0b0000001, 0b011, "cv.lh">;
  def CV_LHU_rr_inc : CVLoad_rr_inc<0b0001001, 0b011, "cv.lhu">;
  def CV_LW_rr_inc  : CVLoad_rr_inc<0b0000010, 0b011, "cv.lw">;
}

let Predicates = [HasVendorXCVmem, IsRV32], hasSideEffects = 0, 
                 mayLoad = 1, mayStore = 0 in {
  // Register-Register load
  def CV_LB_rr  : CVLoad_rr<0b0000100, 0b011, "cv.lb">;
  def CV_LBU_rr : CVLoad_rr<0b0001100, 0b011, "cv.lbu">;
  def CV_LH_rr  : CVLoad_rr<0b0000101, 0b011, "cv.lh">;
  def CV_LHU_rr : CVLoad_rr<0b0001101, 0b011, "cv.lhu">;
  def CV_LW_rr  : CVLoad_rr<0b0000110, 0b011, "cv.lw">;
}

class CVStore_ri_inc<bits<3> funct3, string opcodestr>
    : RVInstS<funct3, OPC_CUSTOM_1, (outs GPR:$rs1_wb), 
              (ins GPR:$rs2, GPR:$rs1, simm12:$imm12),
              opcodestr, "$rs2, (${rs1}), ${imm12}"> {
  let Constraints = "$rs1_wb = $rs1";
  let DecoderNamespace = "XCVmem";
}

class CVStore_rr_inc<bits<3> funct3, bits<7> funct7, dag outs, dag ins,
                     string opcodestr, string argstr>
    : RVInst<outs, ins, opcodestr, argstr, [], InstFormatOther> {
  bits<5> rs3;
  bits<5> rs2;
  bits<5> rs1;

  let Inst{31-25} = funct7;
  let Inst{24-20} = rs2;
  let Inst{19-15} = rs1;
  let Inst{14-12} = funct3;
  let Inst{11-7} = rs3;
  let Inst{6-0} = OPC_CUSTOM_1.Value;
  let DecoderNamespace = "XCVmem";
}


class CVStore_rr<bits<3> funct3, bits<7> funct7, dag outs, dag ins,
                     string opcodestr, string argstr>
    : RVInst<outs, ins, opcodestr, argstr, [], InstFormatOther> {
  bits<5> rs2;
  bits<10> cvrr;

  let Inst{31-25} = funct7;
  let Inst{24-20} = rs2;
  let Inst{19-15} = cvrr{9-5};
  let Inst{14-12} = funct3;
  let Inst{11-7} = cvrr{4-0};
  let Inst{6-0} = OPC_CUSTOM_1.Value;
  let DecoderNamespace = "XCVmem";
}

let Predicates = [HasVendorXCVmem, IsRV32], hasSideEffects = 0, 
                 mayLoad = 0, mayStore = 1, Constraints = "$rs1_wb = $rs1" in {
  // Register-Immediate store with post-increment
  def CV_SB_ri_inc : CVStore_ri_inc<0b000, "cv.sb">;
  def CV_SH_ri_inc : CVStore_ri_inc<0b001, "cv.sh">;
  def CV_SW_ri_inc : CVStore_ri_inc<0b010, "cv.sw">;

  // Register-Register store with post-increment
  def CV_SB_rr_inc : CVStore_rr_inc<0b011, 0b0010000, 
                     (outs GPR:$rs1_wb), (ins GPR:$rs2, GPR:$rs1, GPR:$rs3),
                      "cv.sb", "$rs2, (${rs1}), ${rs3}">;
  def CV_SH_rr_inc : CVStore_rr_inc<0b011, 0b0010001, 
                     (outs GPR:$rs1_wb), (ins GPR:$rs2, GPR:$rs1, GPR:$rs3),
                      "cv.sh", "$rs2, (${rs1}), ${rs3}">;
  def CV_SW_rr_inc : CVStore_rr_inc<0b011, 0b0010010, 
                     (outs GPR:$rs1_wb), (ins GPR:$rs2, GPR:$rs1, GPR:$rs3),
                      "cv.sw", "$rs2, (${rs1}), ${rs3}">;
}


let Predicates = [HasVendorXCVmem, IsRV32], hasSideEffects = 0, 
                  mayLoad = 0, mayStore = 1 in {
  // Register-Register store
  def CV_SB_rr : CVStore_rr<0b011, 0b0010100,
                 (outs), (ins GPR:$rs2, CVrr:$cvrr),
                 "cv.sb", "$rs2, $cvrr">;
  def CV_SH_rr : CVStore_rr<0b011, 0b0010101,
                 (outs), (ins GPR:$rs2, CVrr:$cvrr),
                 "cv.sh", "$rs2, $cvrr">;
  def CV_SW_rr : CVStore_rr<0b011, 0b0010110,
                 (outs), (ins GPR:$rs2, CVrr:$cvrr),
                 "cv.sw", "$rs2, $cvrr">;
} 

let DecoderNamespace = "XCVelw" in
class CVLoad_ri<bits<3> funct3, string opcodestr>
    : RVInstI<funct3, OPC_CUSTOM_0, (outs GPR:$rd), 
      (ins GPRMem:$rs1, simm12:$imm12), opcodestr, "$rd, ${imm12}(${rs1})">;

let Predicates = [HasVendorXCVelw, IsRV32], hasSideEffects = 0, 
    mayLoad = 1, mayStore = 0 in {
  // Event load
  def CV_ELW : CVLoad_ri<0b011, "cv.elw">;
}

//===----------------------------------------------------------------------===//
// Patterns for load & store operations
//===----------------------------------------------------------------------===//
class CVLdrrPat<PatFrag LoadOp, RVInst Inst>
    : Pat<(XLenVT (LoadOp CVrr:$regreg)),
          (Inst CVrr:$regreg)>;

class CVStriPat<PatFrag StoreOp, RVInst Inst>
    : Pat<(StoreOp (XLenVT GPR:$rs2), GPR:$rs1, simm12:$imm12),
          (Inst GPR:$rs2, GPR:$rs1, simm12:$imm12)>;

class CVStrriPat<PatFrag StoreOp, RVInst Inst>
    : Pat<(StoreOp (XLenVT GPR:$rs2), GPR:$rs1, GPR:$rs3),
          (Inst GPR:$rs2, GPR:$rs1, GPR:$rs3)>;

class CVStrrPat<PatFrag StoreOp, RVInst Inst>
    : Pat<(StoreOp (XLenVT GPR:$rs2), CVrr:$regreg),
          (Inst GPR:$rs2, CVrr:$regreg)>;

let Predicates = [HasVendorXCVmem, IsRV32], AddedComplexity = 1 in {
  def : CVLdrrPat<sextloadi8, CV_LB_rr>;
  def : CVLdrrPat<zextloadi8, CV_LBU_rr>;
  def : CVLdrrPat<extloadi8, CV_LBU_rr>;
  def : CVLdrrPat<sextloadi16, CV_LH_rr>;
  def : CVLdrrPat<zextloadi16, CV_LHU_rr>;
  def : CVLdrrPat<extloadi16, CV_LHU_rr>;
  def : CVLdrrPat<load, CV_LW_rr>;

  def : CVStriPat<post_truncsti8, CV_SB_ri_inc>;
  def : CVStriPat<post_truncsti16, CV_SH_ri_inc>;
  def : CVStriPat<post_store, CV_SW_ri_inc>;

  def : CVStrriPat<post_truncsti8, CV_SB_rr_inc>;
  def : CVStrriPat<post_truncsti16, CV_SH_rr_inc>;
  def : CVStrriPat<post_store, CV_SW_rr_inc>;

  def : CVStrrPat<truncstorei8, CV_SB_rr>;
  def : CVStrrPat<truncstorei16, CV_SH_rr>;
  def : CVStrrPat<store, CV_SW_rr>;
}

def cv_tuimm2 : TImmLeaf<XLenVT, [{return isUInt<2>(Imm);}]>;
def cv_tuimm5 : TImmLeaf<XLenVT, [{return isUInt<5>(Imm);}]>;
def cv_uimm10 : ImmLeaf<XLenVT, [{return isUInt<10>(Imm);}]>;

def CV_LO5: SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(N->getZExtValue() & 0x1f, SDLoc(N),
                                   N->getValueType(0));
}]>;

def CV_HI5: SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(N->getZExtValue() >> 5, SDLoc(N),
                                   N->getValueType(0));
}]>;

def powerOf2Minus1 : ImmLeaf<XLenVT, [{ return isPowerOf2_32(Imm+1); }]>;
def trailing1sPlus1 : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(
                          llvm::countr_one(N->getZExtValue()) + 1,
                          SDLoc(N), N->getValueType(0));
}]>;

multiclass PatCoreVBitManip<Intrinsic intr> {
  def : PatGprGpr<intr, !cast<RVInst>("CV_" # NAME # "R")>;
  def : Pat<(intr GPR:$rs1, cv_uimm10:$imm),
            (!cast<RVInst>("CV_" # NAME) 
             GPR:$rs1, (CV_HI5 cv_uimm10:$imm), (CV_LO5 cv_uimm10:$imm))>;
}

let Predicates = [HasVendorXCVbitmanip, IsRV32] in {
  defm EXTRACT : PatCoreVBitManip<int_riscv_cv_bitmanip_extract>;
  defm EXTRACTU : PatCoreVBitManip<int_riscv_cv_bitmanip_extractu>;
  defm BCLR : PatCoreVBitManip<int_riscv_cv_bitmanip_bclr>;
  defm BSET : PatCoreVBitManip<int_riscv_cv_bitmanip_bset>;

  def : Pat<(int_riscv_cv_bitmanip_insert GPR:$rs1, GPR:$rs2, GPR:$rd),
            (CV_INSERTR GPR:$rd, GPR:$rs1, GPR:$rs2)>;
  def : Pat<(int_riscv_cv_bitmanip_insert GPR:$rs1, cv_uimm10:$imm, GPR:$rd),
            (CV_INSERT GPR:$rd, GPR:$rs1, (CV_HI5 cv_uimm10:$imm), 
                                          (CV_LO5 cv_uimm10:$imm))>;

  def : PatGpr<cttz, CV_FF1>;
  def : PatGpr<ctlz, CV_FL1>;
  def : PatGpr<int_riscv_cv_bitmanip_clb, CV_CLB>;
  def : PatGpr<ctpop, CV_CNT>;

  def : PatGprGpr<rotr, CV_ROR>;

  def : Pat<(int_riscv_cv_bitmanip_bitrev GPR:$rs1, cv_tuimm5:$pts, 
             cv_tuimm2:$radix),
            (CV_BITREV GPR:$rs1, cv_tuimm2:$radix, cv_tuimm5:$pts)>;
  def : Pat<(bitreverse (XLenVT GPR:$rs)), (CV_BITREV GPR:$rs, 0, 0)>;
}

class PatCoreVAluGpr<string intr, string asm> :
  PatGpr<!cast<Intrinsic>("int_riscv_cv_alu_" # intr),
            !cast<RVInst>("CV_" # asm)>;
class PatCoreVAluGprGpr <string intr, string asm> :
  PatGprGpr<!cast<Intrinsic>("int_riscv_cv_alu_" # intr),
               !cast<RVInst>("CV_" # asm)>;

multiclass PatCoreVAluGprImm<Intrinsic intr> {
  def : PatGprGpr<intr, !cast<RVInst>("CV_" # NAME # "R")>;
  def : Pat<(intr (XLenVT GPR:$rs1), powerOf2Minus1:$upperBound),
            (!cast<RVInst>("CV_" # NAME) GPR:$rs1,
            (trailing1sPlus1 imm:$upperBound))>;
}

multiclass PatCoreVAluGprGprImm<Intrinsic intr> {
  def : Pat<(intr GPR:$rs1, GPR:$rs2, GPR:$rs3),
            (!cast<RVInst>("CV_" # NAME # "R") GPR:$rs1, GPR:$rs2, GPR:$rs3)>;
  def : Pat<(intr GPR:$rs1, GPR:$rs2, uimm5:$imm),
            (!cast<RVInst>("CV_" # NAME) GPR:$rs1, GPR:$rs2, uimm5:$imm)>;
}

let Predicates = [HasVendorXCValu, IsRV32], AddedComplexity = 1 in {
  def : PatGpr<abs, CV_ABS>;
  def : PatGprGpr<setle, CV_SLET>;
  def : PatGprGpr<setule, CV_SLETU>;
  def : PatGprGpr<smin, CV_MIN>;
  def : PatGprGpr<umin, CV_MINU>;
  def : PatGprGpr<smax, CV_MAX>;
  def : PatGprGpr<umax, CV_MAXU>;

  def : Pat<(sext_inreg (XLenVT GPR:$rs1), i16), (CV_EXTHS GPR:$rs1)>;
  def : Pat<(sext_inreg (XLenVT GPR:$rs1), i8), (CV_EXTBS GPR:$rs1)>;
  def : Pat<(and (XLenVT GPR:$rs1), 0xffff), (CV_EXTHZ GPR:$rs1)>;

  defm CLIP   : PatCoreVAluGprImm<int_riscv_cv_alu_clip>;
  defm CLIPU  : PatCoreVAluGprImm<int_riscv_cv_alu_clipu>;
  defm ADDN   : PatCoreVAluGprGprImm<int_riscv_cv_alu_addn>;
  defm ADDUN  : PatCoreVAluGprGprImm<int_riscv_cv_alu_addun>;
  defm ADDRN  : PatCoreVAluGprGprImm<int_riscv_cv_alu_addrn>;
  defm ADDURN : PatCoreVAluGprGprImm<int_riscv_cv_alu_addurn>;
  defm SUBN   : PatCoreVAluGprGprImm<int_riscv_cv_alu_subn>;
  defm SUBUN  : PatCoreVAluGprGprImm<int_riscv_cv_alu_subun>;
  defm SUBRN  : PatCoreVAluGprGprImm<int_riscv_cv_alu_subrn>;
  defm SUBURN : PatCoreVAluGprGprImm<int_riscv_cv_alu_suburn>;
} // Predicates = [HasVendorXCValu, IsRV32]

//===----------------------------------------------------------------------===//
// Patterns for immediate branching operations
//===----------------------------------------------------------------------===//

let Predicates = [HasVendorXCVbi, IsRV32], AddedComplexity = 2 in {
  def : Pat<(riscv_brcc GPR:$rs1, simm5:$imm5, SETEQ, bb:$imm12),
            (CV_BEQIMM GPR:$rs1, simm5:$imm5, simm13_lsb0:$imm12)>;
  def : Pat<(riscv_brcc GPR:$rs1, simm5:$imm5, SETNE, bb:$imm12),
            (CV_BNEIMM GPR:$rs1, simm5:$imm5, simm13_lsb0:$imm12)>;

  let usesCustomInserter = 1 in
  def Select_GPR_Using_CC_Imm : Pseudo<(outs GPR:$dst),
                             (ins GPR:$lhs, simm5:$imm5, ixlenimm:$cc,
                              GPR:$truev, GPR:$falsev), []>;


  class Selectbi<CondCode Cond>
      : Pat<(riscv_selectcc_frag:$cc (i32 GPR:$lhs), simm5:$Constant, Cond,
                                     (i32 GPR:$truev), GPR:$falsev),
            (Select_GPR_Using_CC_Imm GPR:$lhs, simm5:$Constant,
             (IntCCtoRISCVCC $cc), GPR:$truev, GPR:$falsev)>;

  def : Selectbi<SETEQ>;
  def : Selectbi<SETNE>;
}

class PatCoreVMacGprGprGpr <string intr, string asm>
  : Pat<(!cast<Intrinsic>("int_riscv_cv_mac_" # intr) GPR:$rs1, GPR:$rs2, GPR:$rd),
        (!cast<RVInst>("CV_" # asm) GPR:$rd, GPR:$rs1, GPR:$rs2)>;
class PatCoreVMacGprGprGprUimm5 <string intr, string asm>
  : Pat<(!cast<Intrinsic>("int_riscv_cv_mac_" # intr) GPR:$rs1, GPR:$rs2, GPR:$rd, cv_tuimm5:$imm5),
        (!cast<RVInst>("CV_" # asm) GPR:$rd, GPR:$rs1, GPR:$rs2, cv_tuimm5:$imm5)>;
class PatCoreVMacGprGprUimm5 <string intr, string asm>
  : Pat<(!cast<Intrinsic>("int_riscv_cv_mac_" # intr) GPR:$rs1, GPR:$rs2, cv_tuimm5:$imm5),
        (!cast<RVInst>("CV_" # asm) GPR:$rs1, GPR:$rs2, cv_tuimm5:$imm5)>;

let Predicates = [HasVendorXCVmac] in {
  def : PatCoreVMacGprGprGpr<"mac", "MAC">;
  def : PatCoreVMacGprGprGpr<"msu", "MSU">;

  def : PatCoreVMacGprGprUimm5<"muluN", "MULUN">;
  def : PatCoreVMacGprGprUimm5<"mulhhuN", "MULHHUN">;
  def : PatCoreVMacGprGprUimm5<"mulsN", "MULSN">;
  def : PatCoreVMacGprGprUimm5<"mulhhsN", "MULHHSN">;
  def : PatCoreVMacGprGprUimm5<"muluRN", "MULURN">;
  def : PatCoreVMacGprGprUimm5<"mulhhuRN", "MULHHURN">;
  def : PatCoreVMacGprGprUimm5<"mulsRN", "MULSRN">;
  def : PatCoreVMacGprGprUimm5<"mulhhsRN", "MULHHSRN">;

  def : PatCoreVMacGprGprGprUimm5<"macuN", "MACUN">;
  def : PatCoreVMacGprGprGprUimm5<"machhuN", "MACHHUN">;
  def : PatCoreVMacGprGprGprUimm5<"macsN", "MACSN">;
  def : PatCoreVMacGprGprGprUimm5<"machhsN", "MACHHSN">;
  def : PatCoreVMacGprGprGprUimm5<"macuRN", "MACURN">;
  def : PatCoreVMacGprGprGprUimm5<"machhuRN", "MACHHURN">;
  def : PatCoreVMacGprGprGprUimm5<"macsRN", "MACSRN">;
  def : PatCoreVMacGprGprGprUimm5<"machhsRN", "MACHHSRN">;
}

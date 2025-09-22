//===------ RISCVInstrInfoZicfiss.td - RISC-V Zicfiss -*- tablegen -*------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Instruction class templates
//===----------------------------------------------------------------------===//

class RVC_SSInst<bits<5> rs1val, RegisterClass reg_class, string opcodestr> :
  RVInst16<(outs), (ins reg_class:$rs1), opcodestr, "$rs1", [], InstFormatOther> {
  let Inst{15-13} = 0b011;
  let Inst{12} = 0;
  let Inst{11-7} = rs1val;
  let Inst{6-2} = 0b00000;
  let Inst{1-0} = 0b01;
  let DecoderMethod = "decodeCSSPushPopchk";
}

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

let Predicates = [HasStdExtZicfiss] in {
let Uses = [SSP], Defs = [SSP], hasSideEffects = 0, mayLoad = 1, mayStore = 0 in
def SSPOPCHK : RVInstI<0b100, OPC_SYSTEM, (outs), (ins GPRX1X5:$rs1), "sspopchk",
                       "$rs1"> {
  let rd = 0;
  let imm12 = 0b110011011100;
} // Uses = [SSP],  Defs = [SSP], hasSideEffects = 0, mayLoad = 1, mayStore = 0

let Uses = [SSP], hasSideEffects = 0, mayLoad = 0, mayStore = 0 in {
def SSRDP : RVInstI<0b100, OPC_SYSTEM, (outs GPRNoX0:$rd), (ins), "ssrdp", "$rd"> {
  let imm12 = 0b110011011100;
  let rs1 = 0b00000;
}
} // Uses = [SSP], hasSideEffects = 0, mayLoad = 0, mayStore = 0

let Uses = [SSP], Defs = [SSP], hasSideEffects = 0, mayLoad = 0, mayStore = 1 in
def SSPUSH : RVInstR<0b1100111, 0b100, OPC_SYSTEM, (outs), (ins GPRX1X5:$rs2),
                     "sspush", "$rs2"> {
  let rd = 0b00000;
  let rs1 = 0b00000;
}
} // Predicates = [HasStdExtZicfiss]

let Predicates = [HasStdExtZicfiss, HasStdExtZcmop],
    DecoderNamespace = "Zicfiss" in {
let Uses = [SSP], Defs = [SSP], hasSideEffects = 0, mayLoad = 0, mayStore = 1 in
def C_SSPUSH : RVC_SSInst<0b00001, GPRX1, "c.sspush">;

let Uses = [SSP], Defs = [SSP], hasSideEffects = 0, mayLoad = 1, mayStore = 0 in
def C_SSPOPCHK : RVC_SSInst<0b00101, GPRX5, "c.sspopchk">;
} // Predicates = [HasStdExtZicfiss, HasStdExtZcmop]

let Predicates = [HasStdExtZicfiss] in
defm SSAMOSWAP_W  : AMO_rr_aq_rl<0b01001, 0b010, "ssamoswap.w">;

let Predicates = [HasStdExtZicfiss, IsRV64] in
defm SSAMOSWAP_D  : AMO_rr_aq_rl<0b01001, 0b011, "ssamoswap.d">;

//===----------------------------------------------------------------------===/
// Compress Instruction tablegen backend.
//===----------------------------------------------------------------------===//

let Predicates = [HasStdExtZicfiss, HasStdExtZcmop] in {
def : CompressPat<(SSPUSH X1), (C_SSPUSH X1)>;
def : CompressPat<(SSPOPCHK X5), (C_SSPOPCHK X5)>;
} // Predicates = [HasStdExtZicfiss, HasStdExtZcmop]

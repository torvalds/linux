//===-- RISCVInstrInfoXTHead.td ----------------------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the vendor extensions defined by T-Head of Alibaba.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// T-HEAD specific DAG Nodes.
//===----------------------------------------------------------------------===//

def SDT_LoadPair : SDTypeProfile<2, 2, [SDTCisSameAs<0, 1>,
                                        SDTCisSameAs<1, 3>,
                                        SDTCisPtrTy<2>,
                                        SDTCisVT<3, XLenVT>]>;
def SDT_StorePair : SDTypeProfile<0, 4, [SDTCisSameAs<0, 1>,
                                         SDTCisSameAs<1, 3>,
                                         SDTCisPtrTy<2>,
                                         SDTCisVT<3, XLenVT>]>;

def th_lwud : SDNode<"RISCVISD::TH_LWUD", SDT_LoadPair,
                     [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;
def th_lwd : SDNode<"RISCVISD::TH_LWD", SDT_LoadPair,
                    [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;
def th_ldd : SDNode<"RISCVISD::TH_LDD", SDT_LoadPair,
                    [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;
def th_swd : SDNode<"RISCVISD::TH_SWD", SDT_StorePair,
                    [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;
def th_sdd : SDNode<"RISCVISD::TH_SDD", SDT_StorePair,
                    [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;

//===----------------------------------------------------------------------===//
// Instruction class templates
//===----------------------------------------------------------------------===//

class THInstVdotVV<bits<6> funct6, RISCVVFormat opv, dag outs, dag ins,
                   string opcodestr, string argstr>
    : RVInstVV<funct6, opv, outs, ins, opcodestr, argstr> {
  let Inst{26} = 0;
  let Inst{6-0} = OPC_CUSTOM_0.Value;
  let DecoderNamespace = "XTHeadVdot";
}

class THInstVdotVX<bits<6> funct6, RISCVVFormat opv, dag outs, dag ins,
                   string opcodestr, string argstr>
    : RVInstVX<funct6, opv, outs, ins, opcodestr, argstr> {
  let Inst{26} = 1;
  let Inst{6-0} = OPC_CUSTOM_0.Value;
  let DecoderNamespace = "XTHeadVdot";
}

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in {
// op vd, vs1, vs2, vm (reverse the order of vs1 and vs2)
class THVdotALUrVV<bits<6> funct6, RISCVVFormat opv, string opcodestr,
                   bit EarlyClobber>
    : THInstVdotVV<funct6, opv, (outs VR:$vd_wb),
                   (ins VR:$vd, VR:$vs1, VR:$vs2, VMaskOp:$vm),
                   opcodestr, "$vd, $vs1, $vs2$vm"> {
  let Constraints = !if(EarlyClobber, "@earlyclobber $vd_wb, $vd = $vd_wb",
                                      "$vd = $vd_wb");
}

// op vd, rs1, vs2, vm (reverse the order of rs1 and vs2)
class THVdotALUrVX<bits<6> funct6, RISCVVFormat opv, string opcodestr,
                   bit EarlyClobber>
    : THInstVdotVX<funct6, opv, (outs VR:$vd_wb),
                   (ins VR:$vd, GPR:$rs1, VR:$vs2, VMaskOp:$vm),
                   opcodestr, "$vd, $rs1, $vs2$vm"> {
  let Constraints = !if(EarlyClobber, "@earlyclobber $vd_wb, $vd = $vd_wb",
                                      "$vd = $vd_wb");
}
} // hasSideEffects = 0, mayLoad = 0, mayStore = 0

let Predicates = [HasVendorXTHeadBa], DecoderNamespace = "XTHeadBa",
    hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
class THShiftALU_rri<bits<3> funct3, string opcodestr>
    : RVInstRBase<funct3, OPC_CUSTOM_0, (outs GPR:$rd),
                  (ins GPR:$rs1, GPR:$rs2, uimm2:$uimm2),
                  opcodestr, "$rd, $rs1, $rs2, $uimm2"> {
  bits<2> uimm2;
  let Inst{31-27} = 0;
  let Inst{26-25} = uimm2;
}

let Predicates = [HasVendorXTHeadBb], DecoderNamespace = "XTHeadBb",
    hasSideEffects = 0, mayLoad = 0, mayStore = 0 in {
class THShift_ri<bits<5> funct5, bits<3> funct3, string opcodestr>
    : RVInstIShift<funct5, funct3, OPC_CUSTOM_0, (outs GPR:$rd),
                   (ins GPR:$rs1, uimmlog2xlen:$shamt),
                   opcodestr, "$rd, $rs1, $shamt">;

class THBitfieldExtract_rii<bits<3> funct3, string opcodestr>
    : RVInstIBase<funct3, OPC_CUSTOM_0, (outs GPR:$rd),
                  (ins GPR:$rs1, uimmlog2xlen:$msb, uimmlog2xlen:$lsb),
                  opcodestr, "$rd, $rs1, $msb, $lsb"> {
  bits<6> msb;
  bits<6> lsb;
  let Inst{31-26} = msb;
  let Inst{25-20} = lsb;
}

class THRev_r<bits<5> funct5, bits<2> funct2, string opcodestr>
    : RVInstIUnary<{funct5, funct2, 0b00000}, 0b001, OPC_CUSTOM_0,
                   (outs GPR:$rd), (ins GPR:$rs1), opcodestr, "$rd, $rs1">;
}

let Predicates = [HasVendorXTHeadBb, IsRV64], DecoderNamespace = "XTHeadBb",
    hasSideEffects = 0, mayLoad = 0, mayStore = 0 in
class THShiftW_ri<bits<7> funct7, bits<3> funct3, string opcodestr>
    : RVInstIShiftW<funct7, funct3, OPC_CUSTOM_0, (outs GPR:$rd),
                    (ins GPR:$rs1, uimm5:$shamt),
                    opcodestr, "$rd, $rs1, $shamt">;

let Predicates = [HasVendorXTHeadCondMov], DecoderNamespace = "XTHeadCondMov",
    hasSideEffects = 0, mayLoad = 0, mayStore = 0, isCommutable = 1 in
class THCondMov_rr<bits<7> funct7, string opcodestr>
    : RVInstR<funct7, 0b001, OPC_CUSTOM_0, (outs GPR:$rd_wb),
              (ins GPR:$rd, GPR:$rs1, GPR:$rs2),
              opcodestr, "$rd, $rs1, $rs2"> {
  let Constraints = "$rd_wb = $rd";
}

let Predicates = [HasVendorXTHeadMac], DecoderNamespace = "XTHeadMac",
    hasSideEffects = 0, mayLoad = 0, mayStore = 0, isCommutable = 1 in
class THMulAccumulate_rr<bits<7> funct7, string opcodestr>
    : RVInstR<funct7, 0b001, OPC_CUSTOM_0, (outs GPR:$rd_wb),
              (ins GPR:$rd, GPR:$rs1, GPR:$rs2),
              opcodestr, "$rd, $rs1, $rs2"> {
  let Constraints = "$rd_wb = $rd";
}

let Predicates = [HasVendorXTHeadMemPair], DecoderNamespace = "XTHeadMemPair",
    hasSideEffects = 0, mayLoad = 1, mayStore = 0 in
class THLoadPair<bits<5> funct5, string opcodestr>
  : RVInstRBase<0b100, OPC_CUSTOM_0,
                (outs GPR:$rd, GPR:$rs2),
                (ins GPR:$rs1, uimm2:$uimm2, uimm7:$const3or4),
                 opcodestr, "$rd, $rs2, (${rs1}), $uimm2, $const3or4"> {
  bits<2> uimm2;
  let Inst{31-27} = funct5;
  let Inst{26-25} = uimm2;
  let DecoderMethod = "decodeXTHeadMemPair";
  let Constraints = "@earlyclobber $rd,@earlyclobber $rs2";
}

let Predicates = [HasVendorXTHeadMemPair], DecoderNamespace = "XTHeadMemPair",
    hasSideEffects = 0, mayLoad = 0, mayStore = 1 in
class THStorePair<bits<5> funct5, string opcodestr>
  : RVInstRBase<0b101, OPC_CUSTOM_0, (outs),
              (ins GPR:$rd, GPR:$rs2, GPR:$rs1, uimm2:$uimm2, uimm7:$const3or4),
              opcodestr, "$rd, $rs2, (${rs1}), $uimm2, $const3or4"> {
  bits<2> uimm2;
  let Inst{31-27} = funct5;
  let Inst{26-25} = uimm2;
  let DecoderMethod = "decodeXTHeadMemPair";
}

let hasSideEffects = 1, mayLoad = 0, mayStore = 0 in
class THCacheInst_r<bits<5> funct5, string opcodestr>
    : RVInstR<0b0000001, 0, OPC_CUSTOM_0, (outs), (ins GPR:$rs1),
              opcodestr, "$rs1"> {
  let rd = 0;
  let rs2 = funct5;
}

let hasSideEffects = 1, mayLoad = 0, mayStore = 0 in
class THCacheInst_rr<bits<7> funct7, string opcodestr>
    : RVInstR<funct7, 0, OPC_CUSTOM_0, (outs), (ins GPR:$rs1, GPR:$rs2),
      opcodestr, "$rs1, $rs2"> {
  let rd = 0;
}

let hasSideEffects = 1, mayLoad = 0, mayStore = 0 in
class THCacheInst_void<bits<5> funct5, string opcodestr>
    : RVInstR<0b0000000, 0, OPC_CUSTOM_0, (outs), (ins), opcodestr, ""> {
  let rd = 0;
  let rs1 = 0;
  let rs2 = funct5;
}

let hasSideEffects = 0, mayLoad = 1, mayStore = 0 in {
class THLoadIndexed<RegisterClass Ty, bits<5> funct5, string opcodestr>
    : RVInstRBase<!if(!eq(Ty, GPR), 0b100, 0b110), OPC_CUSTOM_0,
                  (outs Ty:$rd), (ins GPR:$rs1, GPR:$rs2, uimm2:$uimm2),
                  opcodestr, "$rd, $rs1, $rs2, $uimm2"> {
  bits<2> uimm2;
  let Inst{31-27} = funct5;
  let Inst{26-25} = uimm2;
}

class THLoadUpdate<bits<5> funct5, string opcodestr>
    : RVInstIBase<0b100, OPC_CUSTOM_0, (outs GPR:$rd, GPR:$rs1_wb),
                  (ins GPR:$rs1, simm5:$simm5, uimm2:$uimm2),
                  opcodestr, "$rd, (${rs1}), $simm5, $uimm2"> {
  bits<5> simm5;
  bits<2> uimm2;
  let Inst{31-27} = funct5;
  let Inst{26-25} = uimm2;
  let Inst{24-20} = simm5;
  let Constraints = "@earlyclobber $rd, $rs1_wb = $rs1";
}
}

let hasSideEffects = 0, mayLoad = 0, mayStore = 1 in {
class THStoreIndexed<RegisterClass StTy, bits<5> funct5, string opcodestr>
    : RVInstRBase<!if(!eq(StTy, GPR), 0b101, 0b111), OPC_CUSTOM_0,
                  (outs), (ins StTy:$rd, GPR:$rs1, GPR:$rs2, uimm2:$uimm2),
                  opcodestr, "$rd, $rs1, $rs2, $uimm2"> {
  bits<2> uimm2;
  let Inst{31-27} = funct5;
  let Inst{26-25} = uimm2;
}

class THStoreUpdate<bits<5> funct5, string opcodestr>
    : RVInstIBase<0b101, OPC_CUSTOM_0, (outs GPR:$rs1_up),
                  (ins GPR:$rd, GPR:$rs1, simm5:$simm5, uimm2:$uimm2),
                  opcodestr, "$rd, (${rs1}), $simm5, $uimm2"> {
  bits<5> simm5;
  bits<2> uimm2;
  let Inst{31-27} = funct5;
  let Inst{26-25} = uimm2;
  let Inst{24-20} = simm5;
  let Constraints = "$rs1_up = $rs1";
}
}

//===----------------------------------------------------------------------===//
// Combination of instruction classes.
// Use these multiclasses to define instructions more easily.
//===----------------------------------------------------------------------===//

multiclass THVdotVMAQA_VX<string opcodestr, bits<6> funct6> {
  let RVVConstraint = WidenV in
  def _VX : THVdotALUrVX<funct6, OPMVX, opcodestr # ".vx", EarlyClobber=1>;
}

multiclass THVdotVMAQA<string opcodestr, bits<6> funct6>
    : THVdotVMAQA_VX<opcodestr, funct6> {
  let RVVConstraint = WidenV in
  def _VV   : THVdotALUrVV<funct6, OPMVX, opcodestr # ".vv", EarlyClobber=1>;
}

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

let Predicates = [HasVendorXTHeadBa] in
def TH_ADDSL : THShiftALU_rri<0b001, "th.addsl">,
               Sched<[WriteSHXADD, ReadSHXADD, ReadSHXADD]>;

let Predicates = [HasVendorXTHeadBb] in {
def TH_SRRI : THShift_ri<0b00010, 0b001, "th.srri">;
def TH_EXT : THBitfieldExtract_rii<0b010, "th.ext">;
def TH_EXTU : THBitfieldExtract_rii<0b011, "th.extu">;
def TH_FF0 : THRev_r<0b10000, 0b10, "th.ff0">;
def TH_FF1 : THRev_r<0b10000, 0b11, "th.ff1">;
def TH_REV : THRev_r<0b10000, 0b01, "th.rev">;
def TH_TSTNBZ : THRev_r<0b10000, 0b00, "th.tstnbz">;
} // Predicates = [HasVendorXTHeadBb]

let Predicates = [HasVendorXTHeadBb, IsRV64], IsSignExtendingOpW = 1 in {
def TH_SRRIW : THShiftW_ri<0b0001010, 0b001, "th.srriw">;
def TH_REVW : THRev_r<0b10010, 0b00, "th.revw">;
} // Predicates = [HasVendorXTHeadBb, IsRV64]

let Predicates = [HasVendorXTHeadBs], DecoderNamespace = "XTHeadBs",
    IsSignExtendingOpW = 1 in
def TH_TST : RVBShift_ri<0b10001, 0b001, OPC_CUSTOM_0, "th.tst">,
             Sched<[WriteSingleBitImm, ReadSingleBitImm]>;

let Predicates = [HasVendorXTHeadCondMov] in {
def TH_MVEQZ : THCondMov_rr<0b0100000, "th.mveqz">;
def TH_MVNEZ : THCondMov_rr<0b0100001, "th.mvnez">;
} // Predicates = [HasVendorXTHeadCondMov]

let Predicates = [HasVendorXTHeadMac] in {
def TH_MULA : THMulAccumulate_rr<0b0010000, "th.mula">;
def TH_MULS : THMulAccumulate_rr<0b0010001, "th.muls">;
}  // Predicates = [HasVendorXTHeadMac]

let Predicates = [HasVendorXTHeadMac], IsSignExtendingOpW = 1 in {
def TH_MULAH : THMulAccumulate_rr<0b0010100, "th.mulah">;
def TH_MULSH : THMulAccumulate_rr<0b0010101, "th.mulsh">;
} // Predicates = [HasVendorXTHeadMac], IsSignExtendingOpW = 1

let Predicates = [HasVendorXTHeadMac, IsRV64], IsSignExtendingOpW = 1 in {
def TH_MULAW : THMulAccumulate_rr<0b0010010, "th.mulaw">;
def TH_MULSW : THMulAccumulate_rr<0b0010011, "th.mulsw">;
} // Predicates = [HasVendorXTHeadMac, IsRV64]

let Predicates = [HasVendorXTHeadMemPair] in {
def TH_LWUD : THLoadPair<0b11110, "th.lwud">,
              Sched<[WriteLDW, WriteLDW, ReadMemBase]>;
def TH_SWD  : THStorePair<0b11100, "th.swd">,
              Sched<[WriteSTW, WriteSTW, ReadStoreData, ReadMemBase]>;
let IsSignExtendingOpW = 1 in
def TH_LWD  : THLoadPair<0b11100, "th.lwd">,
              Sched<[WriteLDW, WriteLDW, ReadMemBase]>;
}

let Predicates = [HasVendorXTHeadMemPair, IsRV64] in {
def TH_LDD : THLoadPair<0b11111, "th.ldd">,
             Sched<[WriteLDD, WriteLDD, ReadMemBase]>;
def TH_SDD : THStorePair<0b11111, "th.sdd">,
             Sched<[WriteSTD, WriteSTD, ReadStoreData, ReadMemBase]>;
}

let Predicates = [HasVendorXTHeadMemIdx], DecoderNamespace = "XTHeadMemIdx" in {
// T-Head Load/Store + Update instructions.
def TH_LBIA : THLoadUpdate<0b00011, "th.lbia">,
              Sched<[WriteLDB, ReadMemBase]>;
def TH_LBIB : THLoadUpdate<0b00001, "th.lbib">,
              Sched<[WriteLDB, ReadMemBase]>;
def TH_LBUIA : THLoadUpdate<0b10011, "th.lbuia">,
               Sched<[WriteLDB, ReadMemBase]>;
def TH_LBUIB : THLoadUpdate<0b10001, "th.lbuib">,
               Sched<[WriteLDB, ReadMemBase]>;

def TH_LHIA : THLoadUpdate<0b00111, "th.lhia">,
              Sched<[WriteLDH, ReadMemBase]>;
def TH_LHIB : THLoadUpdate<0b00101, "th.lhib">,
              Sched<[WriteLDH, ReadMemBase]>;
def TH_LHUIA : THLoadUpdate<0b10111, "th.lhuia">,
               Sched<[WriteLDH, ReadMemBase]>;
def TH_LHUIB : THLoadUpdate<0b10101, "th.lhuib">,
               Sched<[WriteLDH, ReadMemBase]>;

def TH_LWIA : THLoadUpdate<0b01011, "th.lwia">,
              Sched<[WriteLDW, ReadMemBase]>;
def TH_LWIB : THLoadUpdate<0b01001, "th.lwib">,
              Sched<[WriteLDW, ReadMemBase]>;

def TH_SBIA : THStoreUpdate<0b00011, "th.sbia">,
              Sched<[WriteSTB, ReadStoreData, ReadMemBase]>;
def TH_SBIB : THStoreUpdate<0b00001, "th.sbib">,
              Sched<[WriteSTB, ReadStoreData, ReadMemBase]>;

def TH_SHIA : THStoreUpdate<0b00111, "th.shia">,
              Sched<[WriteSTH, ReadStoreData, ReadMemBase]>;
def TH_SHIB : THStoreUpdate<0b00101, "th.shib">,
              Sched<[WriteSTH, ReadStoreData, ReadMemBase]>;

def TH_SWIA : THStoreUpdate<0b01011, "th.swia">,
              Sched<[WriteSTW, ReadStoreData, ReadMemBase]>;
def TH_SWIB : THStoreUpdate<0b01001, "th.swib">,
              Sched<[WriteSTW, ReadStoreData, ReadMemBase]>;

// T-Head Load/Store Indexed instructions.
def TH_LRB : THLoadIndexed<GPR, 0b00000, "th.lrb">,
             Sched<[WriteLDB, ReadMemBase]>;
def TH_LRBU : THLoadIndexed<GPR, 0b10000, "th.lrbu">,
              Sched<[WriteLDB, ReadMemBase]>;
def TH_LURB : THLoadIndexed<GPR, 0b00010, "th.lurb">,
              Sched<[WriteLDB, ReadMemBase]>;
def TH_LURBU : THLoadIndexed<GPR, 0b10010, "th.lurbu">,
               Sched<[WriteLDB, ReadMemBase]>;

def TH_LRH : THLoadIndexed<GPR, 0b00100, "th.lrh">,
             Sched<[WriteLDH, ReadMemBase]>;
def TH_LRHU : THLoadIndexed<GPR, 0b10100, "th.lrhu">,
              Sched<[WriteLDH, ReadMemBase]>;
def TH_LURH : THLoadIndexed<GPR, 0b00110, "th.lurh">,
              Sched<[WriteLDB, ReadMemBase]>;
def TH_LURHU : THLoadIndexed<GPR, 0b10110, "th.lurhu">,
               Sched<[WriteLDB, ReadMemBase]>;

def TH_LRW : THLoadIndexed<GPR, 0b01000, "th.lrw">,
             Sched<[WriteLDW, ReadMemBase]>;
def TH_LURW : THLoadIndexed<GPR, 0b01010, "th.lurw">,
              Sched<[WriteLDB, ReadMemBase]>;

def TH_SRB : THStoreIndexed<GPR, 0b00000, "th.srb">,
             Sched<[WriteSTB, ReadStoreData, ReadMemBase]>;
def TH_SURB : THStoreIndexed<GPR, 0b00010, "th.surb">,
              Sched<[WriteLDB, ReadMemBase]>;

def TH_SRH : THStoreIndexed<GPR, 0b00100, "th.srh">,
             Sched<[WriteSTH, ReadStoreData, ReadMemBase]>;
def TH_SURH : THStoreIndexed<GPR, 0b00110, "th.surh">,
              Sched<[WriteLDB, ReadMemBase]>;

def TH_SRW : THStoreIndexed<GPR, 0b01000, "th.srw">,
             Sched<[WriteSTW, ReadStoreData, ReadMemBase]>;
def TH_SURW : THStoreIndexed<GPR, 0b01010, "th.surw">,
              Sched<[WriteLDB, ReadMemBase]>;
}

let Predicates = [HasVendorXTHeadMemIdx, IsRV64], DecoderNamespace = "XTHeadMemIdx" in {
// T-Head Load/Store + Update instructions.
def TH_LWUIA : THLoadUpdate<0b11011, "th.lwuia">,
               Sched<[WriteLDH, ReadMemBase]>;
def TH_LWUIB : THLoadUpdate<0b11001, "th.lwuib">,
               Sched<[WriteLDH, ReadMemBase]>;

def TH_LDIA : THLoadUpdate<0b01111, "th.ldia">,
              Sched<[WriteLDW, ReadMemBase]>;
def TH_LDIB : THLoadUpdate<0b01101, "th.ldib">,
              Sched<[WriteLDW, ReadMemBase]>;

def TH_SDIA : THStoreUpdate<0b01111, "th.sdia">,
              Sched<[WriteSTW, ReadStoreData, ReadMemBase]>;
def TH_SDIB : THStoreUpdate<0b01101, "th.sdib">,
              Sched<[WriteSTW, ReadStoreData, ReadMemBase]>;

// T-Head Load/Store Indexed instructions.
def TH_LRWU : THLoadIndexed<GPR, 0b11000, "th.lrwu">,
              Sched<[WriteLDW, ReadMemBase]>;
def TH_LURWU : THLoadIndexed<GPR, 0b11010, "th.lurwu">,
               Sched<[WriteLDB, ReadMemBase]>;

def TH_LRD : THLoadIndexed<GPR, 0b01100, "th.lrd">,
             Sched<[WriteLDW, ReadMemBase]>;
def TH_LURD : THLoadIndexed<GPR, 0b01110, "th.lurd">,
              Sched<[WriteLDB, ReadMemBase]>;

def TH_SRD : THStoreIndexed<GPR, 0b01100, "th.srd">,
             Sched<[WriteSTW, ReadStoreData, ReadMemBase]>;
def TH_SURD : THStoreIndexed<GPR, 0b01110, "th.surd">,
              Sched<[WriteLDB, ReadMemBase]>;
}

// T-Head Load/Store Indexed instructions for floating point registers.

let Predicates = [HasVendorXTHeadFMemIdx, HasStdExtF],
    DecoderNamespace = "XTHeadFMemIdx" in {
def TH_FLRW : THLoadIndexed<FPR32, 0b01000, "th.flrw">,
              Sched<[WriteFLD32, ReadFMemBase]>;
def TH_FSRW : THStoreIndexed<FPR32, 0b01000, "th.fsrw">,
              Sched<[WriteFST32, ReadFStoreData, ReadFMemBase]>;
}

let Predicates = [HasVendorXTHeadFMemIdx, HasStdExtD],
    DecoderNamespace = "XTHeadFMemIdx" in {
def TH_FLRD : THLoadIndexed<FPR64, 0b01100, "th.flrd">,
              Sched<[WriteFLD64, ReadFMemBase]>;
def TH_FSRD : THStoreIndexed<FPR64, 0b01100, "th.fsrd">,
              Sched<[WriteFST64, ReadFStoreData, ReadFMemBase]>;
}

let Predicates = [HasVendorXTHeadFMemIdx, HasStdExtF, IsRV64],
    DecoderNamespace = "XTHeadFMemIdx" in {
def TH_FLURW : THLoadIndexed<FPR32, 0b01010, "th.flurw">,
               Sched<[WriteFLD32, ReadFMemBase]>;
def TH_FSURW : THStoreIndexed<FPR32, 0b01010, "th.fsurw">,
               Sched<[WriteFST32, ReadFStoreData, ReadFMemBase]>;
}

let Predicates = [HasVendorXTHeadFMemIdx, HasStdExtD, IsRV64],
    DecoderNamespace = "XTHeadFMemIdx" in {
def TH_FLURD : THLoadIndexed<FPR64, 0b01110, "th.flurd">,
               Sched<[WriteFLD64, ReadFMemBase]>;
def TH_FSURD : THStoreIndexed<FPR64, 0b01110, "th.fsurd">,
               Sched<[WriteFST64, ReadFStoreData, ReadFMemBase]>;
}

let Predicates = [HasVendorXTHeadVdot] in {
defm THVdotVMAQA      : THVdotVMAQA<"th.vmaqa",     0b100000>;
defm THVdotVMAQAU     : THVdotVMAQA<"th.vmaqau",    0b100010>;
defm THVdotVMAQASU    : THVdotVMAQA<"th.vmaqasu",   0b100100>;
defm THVdotVMAQAUS    : THVdotVMAQA_VX<"th.vmaqaus",0b100110>;
}

// Associate LMUL with tablegen records of register classes.
def THVdotV_M1  : LMULInfo<0b000,  8,   VR, VR,   VR,   VR,   VR, "M1">;
def THVdotV_M2  : LMULInfo<0b001, 16, VRM2, VRM2, VR,   VR,   VR, "M2">;
def THVdotV_M4  : LMULInfo<0b010, 32, VRM4, VRM4, VRM2, VR,   VR, "M4">;
def THVdotV_M8  : LMULInfo<0b011, 64, VRM8, VRM8, VRM4, VRM2, VR, "M8">;

defvar MxListTHVdot = [V_MF2, THVdotV_M1, THVdotV_M2, THVdotV_M4, THVdotV_M8];

defset list<VTypeInfoToWide> AllQuadWidenableInt8NoVLMulVectors = {
  def : VTypeInfoToWide<VI8MF2,  VI32MF2>;
  def : VTypeInfoToWide<VI8M1,   VI32M1>;
  def : VTypeInfoToWide<VI8M2,   VI32M2>;
  def : VTypeInfoToWide<VI8M4,   VI32M4>;
  def : VTypeInfoToWide<VI8M8,   VI32M8>;
}

//===----------------------------------------------------------------------===//
// Combination of instruction classes.
// Use these multiclasses to define instructions more easily.
//===----------------------------------------------------------------------===//

multiclass VPseudoVMAQA_VV_VX {
  foreach m = MxListTHVdot in {
    // TODO: Add Sched
    defm "" : VPseudoTernaryW_VV<m>;
    defm "" : VPseudoTernaryW_VX<m>;
  }
}

multiclass VPseudoVMAQA_VX {
  foreach m = MxListTHVdot in {
    // TODO: Add Sched
    defm "" : VPseudoTernaryW_VX<m>;
  }
}

multiclass VPatTernaryVMAQA_VV<string intrinsic, string instruction,
                               list<VTypeInfoToWide> vtilist> {
  foreach vtiToWti = vtilist in {
    defvar vti = vtiToWti.Vti;
    defvar wti = vtiToWti.Wti;
    defm : VPatTernaryWithPolicy<intrinsic, instruction, "VV",
                                 wti.Vector, vti.Vector, vti.Vector,
                                 vti.Mask, wti.Log2SEW, vti.LMul,
                                 wti.RegClass, vti.RegClass, vti.RegClass>;
  }
}

multiclass VPatTernaryVMAQA_VX<string intrinsic, string instruction,
                               list<VTypeInfoToWide> vtilist> {
  foreach vtiToWti = vtilist in {
    defvar vti = vtiToWti.Vti;
    defvar wti = vtiToWti.Wti;
    defm : VPatTernaryWithPolicy<intrinsic, instruction,
                                 "V"#vti.ScalarSuffix,
                                 wti.Vector, vti.Scalar, vti.Vector,
                                 vti.Mask, wti.Log2SEW, vti.LMul,
                                 wti.RegClass, vti.ScalarRegClass, vti.RegClass>;
  }
}

multiclass VPatTernaryVMAQA_VV_VX<string intrinsic, string instruction,
                                  list<VTypeInfoToWide> vtilist>
    : VPatTernaryVMAQA_VV<intrinsic, instruction, vtilist>,
      VPatTernaryVMAQA_VX<intrinsic, instruction, vtilist>;

//===----------------------------------------------------------------------===//
// Pseudo-instructions and codegen patterns
//===----------------------------------------------------------------------===//

let Predicates = [HasVendorXTHeadBa] in {
def : Pat<(add (XLenVT GPR:$rs1), (shl GPR:$rs2, uimm2:$uimm2)),
          (TH_ADDSL GPR:$rs1, GPR:$rs2, uimm2:$uimm2)>;
def : Pat<(XLenVT (riscv_shl_add GPR:$rs1, uimm2:$uimm2, GPR:$rs2)),
          (TH_ADDSL GPR:$rs2, GPR:$rs1, uimm2:$uimm2)>;

// Reuse complex patterns from StdExtZba
def : Pat<(add_non_imm12 sh1add_op:$rs1, (XLenVT GPR:$rs2)),
          (TH_ADDSL GPR:$rs2, sh1add_op:$rs1, 1)>;
def : Pat<(add_non_imm12 sh2add_op:$rs1, (XLenVT GPR:$rs2)),
          (TH_ADDSL GPR:$rs2, sh2add_op:$rs1, 2)>;
def : Pat<(add_non_imm12 sh3add_op:$rs1, (XLenVT GPR:$rs2)),
          (TH_ADDSL GPR:$rs2, sh3add_op:$rs1, 3)>;

def : Pat<(add (XLenVT GPR:$r), CSImm12MulBy4:$i),
          (TH_ADDSL GPR:$r, (XLenVT (ADDI (XLenVT X0), (SimmShiftRightBy2XForm CSImm12MulBy4:$i))), 2)>;
def : Pat<(add (XLenVT GPR:$r), CSImm12MulBy8:$i),
          (TH_ADDSL GPR:$r, (XLenVT (ADDI (XLenVT X0), (SimmShiftRightBy3XForm CSImm12MulBy8:$i))), 3)>;

def : Pat<(mul_const_oneuse GPR:$r, (XLenVT 200)),
          (SLLI (XLenVT (TH_ADDSL (XLenVT (TH_ADDSL GPR:$r, GPR:$r, 2)),
                                  (XLenVT (TH_ADDSL GPR:$r, GPR:$r, 2)), 2)), 3)>;
} // Predicates = [HasVendorXTHeadBa]

let Predicates = [HasVendorXTHeadBb] in {
def : PatGprImm<rotr, TH_SRRI, uimmlog2xlen>;
// There's no encoding for a rotate-left-immediate in X-THead-Bb, as
// it can be implemented with th.srri by negating the immediate.
def : Pat<(rotl (XLenVT GPR:$rs1), uimmlog2xlen:$shamt),
          (TH_SRRI GPR:$rs1, (ImmSubFromXLen uimmlog2xlen:$shamt))>;
def : Pat<(sext_inreg (XLenVT GPR:$rs1), i32), (TH_EXT GPR:$rs1, 31, 0)>;
def : Pat<(sext_inreg (XLenVT GPR:$rs1), i16), (TH_EXT GPR:$rs1, 15, 0)>;
def : Pat<(sext_inreg (XLenVT GPR:$rs1), i8), (TH_EXT GPR:$rs1, 7, 0)>;
def : Pat<(sext_inreg (XLenVT GPR:$rs1), i1), (TH_EXT GPR:$rs1, 0, 0)>;
def : PatGpr<ctlz, TH_FF1>;
def : Pat<(XLenVT (ctlz (xor (XLenVT GPR:$rs1), -1))), (TH_FF0 GPR:$rs1)>;
def : PatGpr<bswap, TH_REV>;
} // Predicates = [HasVendorXTHeadBb]

let Predicates = [HasVendorXTHeadBb, IsRV64] in {
def : PatGprImm<riscv_rorw, TH_SRRIW, uimm5>;
def : Pat<(riscv_rolw GPR:$rs1, uimm5:$rs2),
          (TH_SRRIW GPR:$rs1, (ImmSubFrom32 uimm5:$rs2))>;
def : Pat<(sra (bswap i64:$rs1), (i64 32)),
          (TH_REVW i64:$rs1)>;
def : Pat<(binop_allwusers<srl> (bswap i64:$rs1), (i64 32)),
          (TH_REVW i64:$rs1)>;
def : Pat<(riscv_clzw i64:$rs1),
          (TH_FF0 (i64 (SLLI (i64 (XORI i64:$rs1, -1)), 32)))>;
} // Predicates = [HasVendorXTHeadBb, IsRV64]

let Predicates = [HasVendorXTHeadBs] in {
def : Pat<(and (srl (XLenVT GPR:$rs1), uimmlog2xlen:$shamt), 1),
          (TH_TST GPR:$rs1, uimmlog2xlen:$shamt)>;
def : Pat<(XLenVT (seteq (and (XLenVT GPR:$rs1), SingleBitSetMask:$mask), 0)),
          (TH_TST (XLenVT (XORI GPR:$rs1, -1)), SingleBitSetMask:$mask)>;
} // Predicates = [HasVendorXTHeadBs]

let Predicates = [HasVendorXTHeadCondMov] in {
def : Pat<(select (XLenVT GPR:$cond), (XLenVT GPR:$a), (XLenVT GPR:$b)),
          (TH_MVEQZ GPR:$a, GPR:$b, GPR:$cond)>;
def : Pat<(select (XLenVT GPR:$cond), (XLenVT GPR:$a), (XLenVT 0)),
          (TH_MVEQZ GPR:$a, (XLenVT X0), GPR:$cond)>;
def : Pat<(select (XLenVT GPR:$cond), (XLenVT 0), (XLenVT GPR:$b)),
          (TH_MVNEZ GPR:$b, (XLenVT X0), GPR:$cond)>;

def : Pat<(select (riscv_seteq (XLenVT GPR:$cond)), (XLenVT GPR:$a), (XLenVT GPR:$b)),
          (TH_MVNEZ GPR:$a, GPR:$b, GPR:$cond)>;
def : Pat<(select (riscv_setne (XLenVT GPR:$cond)), (XLenVT GPR:$a), (XLenVT GPR:$b)),
          (TH_MVEQZ GPR:$a, GPR:$b, GPR:$cond)>;
def : Pat<(select (riscv_seteq (XLenVT GPR:$cond)), (XLenVT GPR:$a), (XLenVT 0)),
          (TH_MVNEZ GPR:$a, (XLenVT X0), GPR:$cond)>;
def : Pat<(select (riscv_setne (XLenVT GPR:$cond)), (XLenVT GPR:$a), (XLenVT 0)),
          (TH_MVEQZ GPR:$a, (XLenVT X0), GPR:$cond)>;
def : Pat<(select (riscv_seteq (XLenVT GPR:$cond)), (XLenVT 0), (XLenVT GPR:$b)),
          (TH_MVEQZ GPR:$b, (XLenVT X0), GPR:$cond)>;
def : Pat<(select (riscv_setne (XLenVT GPR:$cond)),  (XLenVT 0), (XLenVT GPR:$b)),
          (TH_MVNEZ GPR:$b, (XLenVT X0), GPR:$cond)>;
} // Predicates = [HasVendorXTHeadCondMov]

let Predicates = [HasVendorXTHeadMac] in {
def : Pat<(add GPR:$rd, (mul (XLenVT GPR:$rs1), (XLenVT GPR:$rs2))),
          (TH_MULA GPR:$rd, GPR:$rs1, GPR:$rs2)>;
def : Pat<(sub GPR:$rd, (mul (XLenVT GPR:$rs1), (XLenVT GPR:$rs2))),
          (TH_MULS GPR:$rd, GPR:$rs1, GPR:$rs2)>;
} // Predicates = [HasVendorXTHeadMac]

let Predicates = [HasVendorXTHeadMac, IsRV64] in {
// mulaw, mulsw are available only in RV64.
def : Pat<(binop_allwusers<add> GPR:$rd, (mul GPR:$rs1, GPR:$rs2)),
          (TH_MULAW GPR:$rd, GPR:$rs1, GPR:$rs2)>;
def : Pat<(binop_allwusers<sub> GPR:$rd, (mul GPR:$rs1, GPR:$rs2)),
          (TH_MULSW GPR:$rd, GPR:$rs1, GPR:$rs2)>;
// mulah, mulsh produce a sign-extended result.
def : Pat<(binop_allwusers<add> GPR:$rd, (mul
            (sexti16 (i64 GPR:$rs1)),
            (sexti16 (i64 GPR:$rs2)))),
          (TH_MULAH GPR:$rd, GPR:$rs1, GPR:$rs2)>;
def : Pat<(binop_allwusers<sub> GPR:$rd, (mul
            (sexti16 (i64 GPR:$rs1)),
            (sexti16 (i64 GPR:$rs2)))),
          (TH_MULSH GPR:$rd, GPR:$rs1, GPR:$rs2)>;
} // Predicates = [HasVendorXTHeadMac, IsRV64]

let Predicates = [HasVendorXTHeadMac, IsRV32] in {
def : Pat<(i32 (add GPR:$rd, (mul (sexti16 (i32 GPR:$rs1)),
                                  (sexti16 (i32 GPR:$rs2))))),
          (TH_MULAH GPR:$rd, GPR:$rs1, GPR:$rs2)>;
def : Pat<(i32 (sub GPR:$rd, (mul (sexti16 (i32 GPR:$rs1)),
                                  (sexti16 (i32 GPR:$rs2))))),
          (TH_MULSH GPR:$rd, GPR:$rs1, GPR:$rs2)>;
} // Predicates = [HasVendorXTHeadMac, IsRV32]

defm PseudoTHVdotVMAQA      : VPseudoVMAQA_VV_VX;
defm PseudoTHVdotVMAQAU     : VPseudoVMAQA_VV_VX;
defm PseudoTHVdotVMAQASU    : VPseudoVMAQA_VV_VX;
defm PseudoTHVdotVMAQAUS    : VPseudoVMAQA_VX;

let Predicates = [HasVendorXTHeadVdot] in {
defm : VPatTernaryVMAQA_VV_VX<"int_riscv_th_vmaqa",  "PseudoTHVdotVMAQA",
                              AllQuadWidenableInt8NoVLMulVectors>;
defm : VPatTernaryVMAQA_VV_VX<"int_riscv_th_vmaqau", "PseudoTHVdotVMAQAU",
                              AllQuadWidenableInt8NoVLMulVectors>;
defm : VPatTernaryVMAQA_VV_VX<"int_riscv_th_vmaqasu","PseudoTHVdotVMAQASU",
                              AllQuadWidenableInt8NoVLMulVectors>;
defm : VPatTernaryVMAQA_VX<"int_riscv_th_vmaqaus",   "PseudoTHVdotVMAQAUS",
                           AllQuadWidenableInt8NoVLMulVectors>;
}

def uimm2_3_XFORM : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant((N->getZExtValue() >> 3) & 0x3,
                                   SDLoc(N), Subtarget->getXLenVT());
}]>;

def uimm2_3 : Operand<XLenVT>, ImmLeaf<XLenVT, [{
  return isShiftedUInt<2, 3>(Imm);
}], uimm2_3_XFORM>;

def uimm2_4_XFORM : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant((N->getZExtValue() >> 4) & 0x3,
                                   SDLoc(N), Subtarget->getXLenVT());
}]>;

def uimm2_4 : Operand<XLenVT>, ImmLeaf<XLenVT, [{
  return isShiftedUInt<2, 4>(Imm);
}], uimm2_4_XFORM>;

let Predicates = [HasVendorXTHeadMemPair, IsRV64] in {
def : Pat<(th_lwud i64:$rs1, uimm2_3:$uimm2_3), (TH_LWUD i64:$rs1, uimm2_3:$uimm2_3, 3)>;
def : Pat<(th_ldd i64:$rs1, uimm2_4:$uimm2_4), (TH_LDD i64:$rs1, uimm2_4:$uimm2_4, 4)>;

def : Pat<(th_sdd i64:$rd1, i64:$rd2, i64:$rs1, uimm2_4:$uimm2_4),
          (TH_SDD i64:$rd1, i64:$rd2, i64:$rs1, uimm2_4:$uimm2_4, 4)>;
}

let Predicates = [HasVendorXTHeadMemPair] in {
  def : Pat<(th_lwd GPR:$rs1, uimm2_3:$uimm2_3), (TH_LWD GPR:$rs1, uimm2_3:$uimm2_3, 3)>;
  def : Pat<(th_swd GPR:$rd1, GPR:$rd2, GPR:$rs1, uimm2_3:$uimm2_3),
            (TH_SWD GPR:$rd1, GPR:$rd2, GPR:$rs1, uimm2_3:$uimm2_3, 3)>;
}

let Predicates = [HasVendorXTHeadCmo], DecoderNamespace = "XTHeadCmo" in {
def TH_DCACHE_CSW    : THCacheInst_r<0b00001, "th.dcache.csw">;
def TH_DCACHE_ISW    : THCacheInst_r<0b00010, "th.dcache.isw">;
def TH_DCACHE_CISW   : THCacheInst_r<0b00011, "th.dcache.cisw">;
def TH_DCACHE_CVAL1  : THCacheInst_r<0b00100, "th.dcache.cval1">;
def TH_DCACHE_CVA    : THCacheInst_r<0b00101, "th.dcache.cva">;
def TH_DCACHE_IVA    : THCacheInst_r<0b00110, "th.dcache.iva">;
def TH_DCACHE_CIVA   : THCacheInst_r<0b00111, "th.dcache.civa">;
def TH_DCACHE_CPAL1  : THCacheInst_r<0b01000, "th.dcache.cpal1">;
def TH_DCACHE_CPA    : THCacheInst_r<0b01001, "th.dcache.cpa">;
def TH_DCACHE_IPA    : THCacheInst_r<0b01010, "th.dcache.ipa">;
def TH_DCACHE_CIPA   : THCacheInst_r<0b01011, "th.dcache.cipa">;
def TH_ICACHE_IVA    : THCacheInst_r<0b10000, "th.icache.iva">;
def TH_ICACHE_IPA    : THCacheInst_r<0b11000, "th.icache.ipa">;

def TH_DCACHE_CALL   : THCacheInst_void<0b00001, "th.dcache.call">;
def TH_DCACHE_IALL   : THCacheInst_void<0b00010, "th.dcache.iall">;
def TH_DCACHE_CIALL  : THCacheInst_void<0b00011, "th.dcache.ciall">;
def TH_ICACHE_IALL   : THCacheInst_void<0b10000, "th.icache.iall">;
def TH_ICACHE_IALLS  : THCacheInst_void<0b10001, "th.icache.ialls">;
def TH_L2CACHE_CALL  : THCacheInst_void<0b10101, "th.l2cache.call">;
def TH_L2CACHE_IALL  : THCacheInst_void<0b10110, "th.l2cache.iall">;
def TH_L2CACHE_CIALL : THCacheInst_void<0b10111, "th.l2cache.ciall">;
}

let Predicates = [HasVendorXTHeadSync], DecoderNamespace = "XTHeadSync" in {
def TH_SFENCE_VMAS : THCacheInst_rr<0b0000010, "th.sfence.vmas">;
def TH_SYNC        : THCacheInst_void<0b11000, "th.sync">;
def TH_SYNC_S      : THCacheInst_void<0b11001, "th.sync.s">;
def TH_SYNC_I      : THCacheInst_void<0b11010, "th.sync.i">;
def TH_SYNC_IS     : THCacheInst_void<0b11011, "th.sync.is">;
}

def AddrRegRegScale : ComplexPattern<iPTR, 3, "SelectAddrRegRegScale<3>">;
def AddrRegZextRegScale
    : ComplexPattern<i64, 3, "SelectAddrRegZextRegScale<3, 32>",
                     [], [], 10>;

multiclass LdIdxPat<PatFrag LoadOp, RVInst Inst, ValueType vt = XLenVT> {
def : Pat<(vt (LoadOp (AddrRegRegScale (XLenVT GPR:$rs1), (XLenVT GPR:$rs2), uimm2:$uimm2))),
          (Inst GPR:$rs1, GPR:$rs2, uimm2:$uimm2)>;
}

multiclass LdZextIdxPat<PatFrag LoadOp, RVInst Inst, ValueType vt = i64> {
def : Pat<(vt (LoadOp (AddrRegZextRegScale (i64 GPR:$rs1), (i64 GPR:$rs2), uimm2:$uimm2))),
          (Inst GPR:$rs1, GPR:$rs2, uimm2:$uimm2)>;
}

multiclass StIdxPat<PatFrag StoreOp, RVInst Inst, RegisterClass StTy,
                    ValueType vt = XLenVT> {
def : Pat<(StoreOp (vt StTy:$rd),
            (AddrRegRegScale (XLenVT GPR:$rs1), (XLenVT GPR:$rs2), uimm2:$uimm2)),
          (Inst StTy:$rd, GPR:$rs1, GPR:$rs2, uimm2:$uimm2)>;
}

multiclass StZextIdxPat<PatFrag StoreOp, RVInst Inst, RegisterClass StTy,
                        ValueType vt = i64> {
def : Pat<(StoreOp (vt StTy:$rd),
            (AddrRegZextRegScale (i64 GPR:$rs1), (i64 GPR:$rs2), uimm2:$uimm2)),
          (Inst StTy:$rd, GPR:$rs1, GPR:$rs2, uimm2:$uimm2)>;
}

let Predicates = [HasVendorXTHeadMemIdx] in {
defm : LdIdxPat<extloadi8, TH_LRB>;
defm : LdIdxPat<sextloadi8, TH_LRB>;
defm : LdIdxPat<zextloadi8, TH_LRBU>;

defm : LdIdxPat<extloadi16, TH_LRH>;
defm : LdIdxPat<sextloadi16, TH_LRH>;
defm : LdIdxPat<zextloadi16, TH_LRHU>;

defm : StIdxPat<truncstorei8, TH_SRB, GPR>;
defm : StIdxPat<truncstorei16, TH_SRH, GPR>;

defm : LdIdxPat<load, TH_LRW, i32>;
defm : StIdxPat<store, TH_SRW, GPR, i32>;
}

let Predicates = [HasVendorXTHeadMemIdx, IsRV64] in {
defm : LdZextIdxPat<extloadi8, TH_LURB>;
defm : LdZextIdxPat<sextloadi8, TH_LURB>;
defm : LdZextIdxPat<zextloadi8, TH_LURBU>;

defm : LdZextIdxPat<extloadi16, TH_LURH>;
defm : LdZextIdxPat<sextloadi16, TH_LURH>;
defm : LdZextIdxPat<zextloadi16, TH_LURHU>;

defm : LdIdxPat<extloadi32, TH_LRW, i64>;
defm : LdIdxPat<sextloadi32, TH_LRW, i64>;
defm : LdIdxPat<zextloadi32, TH_LRWU, i64>;

defm : LdZextIdxPat<extloadi32, TH_LURW>;
defm : LdZextIdxPat<sextloadi32, TH_LURW>;
defm : LdZextIdxPat<zextloadi32, TH_LURWU>;

defm : LdIdxPat<load, TH_LRD, i64>;
defm : LdZextIdxPat<load, TH_LURD>;

defm : StZextIdxPat<truncstorei8, TH_SURB, GPR>;
defm : StZextIdxPat<truncstorei16, TH_SURH, GPR>;
defm : StIdxPat<truncstorei32, TH_SRW, GPR, i64>;
defm : StZextIdxPat<truncstorei32, TH_SURW, GPR, i64>;
defm : StIdxPat<store, TH_SRD, GPR, i64>;
defm : StZextIdxPat<store, TH_SURD, GPR>;
}

let Predicates = [HasVendorXTHeadFMemIdx, HasStdExtF] in {
defm : LdIdxPat<load, TH_FLRW, f32>;
defm : StIdxPat<store, TH_FSRW, FPR32, f32>;
}

let Predicates = [HasVendorXTHeadFMemIdx, HasStdExtD] in {
defm : LdIdxPat<load, TH_FLRD, f64>;
defm : StIdxPat<store, TH_FSRD, FPR64, f64>;
}

let Predicates = [HasVendorXTHeadFMemIdx, HasStdExtF, IsRV64] in {
defm : LdZextIdxPat<load, TH_FLURW, f32>;
defm : StZextIdxPat<store, TH_FSURW, FPR32, f32>;
}

let Predicates = [HasVendorXTHeadFMemIdx, HasStdExtD, IsRV64] in {
defm : LdZextIdxPat<load, TH_FLURD, f64>;
defm : StZextIdxPat<store, TH_FSURD, FPR64, f64>;
}

def simm5shl2 : ComplexPattern<XLenVT, 2, "selectSimm5Shl2">;

multiclass StoreUpdatePat<PatFrag st, Instruction Inst, ValueType vt = XLenVT> {
def : Pat<(st (vt GPR:$rd), GPR:$rs1, (simm5shl2 simm5:$simm5, uimm2:$uimm2)),
          (Inst GPR:$rd, GPR:$rs1, simm5:$simm5, uimm2:$uimm2)>;
}

let Predicates = [HasVendorXTHeadMemIdx] in {
defm : StoreUpdatePat<post_truncsti8, TH_SBIA>;
defm : StoreUpdatePat<pre_truncsti8, TH_SBIB>;
defm : StoreUpdatePat<post_truncsti16, TH_SHIA>;
defm : StoreUpdatePat<pre_truncsti16, TH_SHIB>;

defm : StoreUpdatePat<post_store, TH_SWIA, i32>;
defm : StoreUpdatePat<pre_store, TH_SWIB, i32>;
}

let Predicates = [HasVendorXTHeadMemIdx, IsRV64] in {
defm : StoreUpdatePat<post_truncsti32, TH_SWIA, i64>;
defm : StoreUpdatePat<pre_truncsti32, TH_SWIB, i64>;
defm : StoreUpdatePat<post_store, TH_SDIA, i64>;
defm : StoreUpdatePat<pre_store, TH_SDIB, i64>;
}

//===----------------------------------------------------------------------===//
// Experimental RV64 i32 legalization patterns.
//===----------------------------------------------------------------------===//

let Predicates = [HasVendorXTHeadMemIdx, IsRV64] in {
defm : StoreUpdatePat<post_truncsti8, TH_SBIA, i32>;
defm : StoreUpdatePat<pre_truncsti8, TH_SBIB, i32>;
defm : StoreUpdatePat<post_truncsti16, TH_SHIA, i32>;
defm : StoreUpdatePat<pre_truncsti16, TH_SHIB, i32>;

defm : StIdxPat<truncstorei8, TH_SRB, GPR, i32>;
defm : StIdxPat<truncstorei16, TH_SRH, GPR, i32>;

defm : StZextIdxPat<truncstorei8, TH_SURB, GPR, i32>;
defm : StZextIdxPat<truncstorei16, TH_SURH, GPR, i32>;
defm : StZextIdxPat<store, TH_SURW, GPR, i32>;
}

let Predicates = [HasVendorXTHeadCondMov, IsRV64] in {
def : Pat<(select (XLenVT GPR:$cond), (i32 GPR:$a), (i32 GPR:$b)),
          (TH_MVEQZ GPR:$a, GPR:$b, GPR:$cond)>;
def : Pat<(select (XLenVT GPR:$cond), (i32 GPR:$a), (i32 0)),
          (TH_MVEQZ GPR:$a, (XLenVT X0), GPR:$cond)>;
def : Pat<(select (XLenVT GPR:$cond), (i32 0), (i32 GPR:$b)),
          (TH_MVNEZ GPR:$b, (XLenVT X0), GPR:$cond)>;

def : Pat<(select (riscv_seteq (XLenVT GPR:$cond)), (i32 GPR:$a), (i32 GPR:$b)),
          (TH_MVNEZ GPR:$a, GPR:$b, GPR:$cond)>;
def : Pat<(select (riscv_setne (XLenVT GPR:$cond)), (i32 GPR:$a), (i32 GPR:$b)),
          (TH_MVEQZ GPR:$a, GPR:$b, GPR:$cond)>;
def : Pat<(select (riscv_seteq (XLenVT GPR:$cond)), (i32 GPR:$a), (i32 0)),
          (TH_MVNEZ GPR:$a, (XLenVT X0), GPR:$cond)>;
def : Pat<(select (riscv_setne (XLenVT GPR:$cond)), (i32 GPR:$a), (i32 0)),
          (TH_MVEQZ GPR:$a, (XLenVT X0), GPR:$cond)>;
def : Pat<(select (riscv_seteq (XLenVT GPR:$cond)), (i32 0), (i32 GPR:$b)),
          (TH_MVEQZ GPR:$b, (XLenVT X0), GPR:$cond)>;
def : Pat<(select (riscv_setne (XLenVT GPR:$cond)),  (i32 0), (i32 GPR:$b)),
          (TH_MVNEZ GPR:$b, (XLenVT X0), GPR:$cond)>;
} // Predicates = [HasVendorXTHeadCondMov]

let Predicates = [HasVendorXTHeadMac, IsRV64] in {
// mulaw, mulsw are available only in RV64.
def : Pat<(i32 (add GPR:$rd, (mul GPR:$rs1, GPR:$rs2))),
          (TH_MULAW GPR:$rd, GPR:$rs1, GPR:$rs2)>;
def : Pat<(i32 (sub GPR:$rd, (mul GPR:$rs1, GPR:$rs2))),
          (TH_MULSW GPR:$rd, GPR:$rs1, GPR:$rs2)>;
// mulah, mulsh produce a sign-extended result.
def : Pat<(i32 (add GPR:$rd,
                    (mul (sexti16i32 (i32 GPR:$rs1)),
                         (sexti16i32 (i32 GPR:$rs2))))),
          (TH_MULAH GPR:$rd, GPR:$rs1, GPR:$rs2)>;
def : Pat<(i32 (sub GPR:$rd,
                    (mul (sexti16i32 (i32 GPR:$rs1)),
                         (sexti16i32 (i32 GPR:$rs2))))),
          (TH_MULSH GPR:$rd, GPR:$rs1, GPR:$rs2)>;
}

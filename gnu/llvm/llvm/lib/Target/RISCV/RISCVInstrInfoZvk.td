//===-- RISCVInstrInfoZvk.td - RISC-V 'Zvk' instructions ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the RISC-V instructions from the standard 'Zvk',
// Vector Cryptography Instructions extension, version Release 1.0.0.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Operand and SDNode transformation definitions.
//===----------------------------------------------------------------------===//

def tuimm5 : RISCVOp, TImmLeaf<XLenVT, [{return isUInt<5>(Imm);}]>;

//===----------------------------------------------------------------------===//
// Instruction class templates
//===----------------------------------------------------------------------===//

let hasSideEffects = 0, mayLoad = 0, mayStore = 0 in {
multiclass VCLMUL_MV_V_X<string opcodestr, bits<6> funct6> {
  def V  : VALUVV<funct6, OPMVV, opcodestr # "." # "vv">,
           SchedBinaryMC<"WriteVCLMULV", "ReadVCLMULV", "ReadVCLMULV">;
  def X  : VALUVX<funct6, OPMVX, opcodestr # "." # "vx">,
           SchedBinaryMC<"WriteVCLMULX", "ReadVCLMULV", "ReadVCLMULX">;
}

class RVInstIVI_VROR<bits<6> funct6, dag outs, dag ins, string opcodestr,
                     string argstr>
    : RVInst<outs, ins, opcodestr, argstr, [], InstFormatR> {
  bits<5> vs2;
  bits<6> imm;
  bits<5> vd;
  bit vm;

  let Inst{31-27} = funct6{5-1};
  let Inst{26} = imm{5};
  let Inst{25} = vm;
  let Inst{24-20} = vs2;
  let Inst{19-15} = imm{4-0};
  let Inst{14-12} = OPIVI.Value;
  let Inst{11-7} = vd;
  let Inst{6-0} = OPC_OP_V.Value;

  let Uses = [VTYPE, VL];
  let RVVConstraint = VMConstraint;
}

multiclass VROR_IV_V_X_I<string opcodestr, bits<6> funct6>
    : VALU_IV_V_X<opcodestr, funct6> {
  def I : RVInstIVI_VROR<funct6, (outs VR:$vd),
              (ins VR:$vs2, uimm6:$imm, VMaskOp:$vm),
              opcodestr # ".vi", "$vd, $vs2, $imm$vm">,
          SchedUnaryMC<"WriteVRotI", "ReadVRotV">;
}

// op vd, vs2, vs1
class PALUVVNoVm<bits<6> funct6, RISCVVFormat opv, string opcodestr>
    : VALUVVNoVm<funct6, opv, opcodestr> {
  let Inst{6-0} = OPC_OP_VE.Value;
}

// op vd, vs2, vs1
class PALUVVNoVmTernary<bits<6> funct6, RISCVVFormat opv, string opcodestr>
    : RVInstVV<funct6, opv, (outs VR:$vd_wb),
               (ins VR:$vd, VR:$vs2, VR:$vs1),
               opcodestr, "$vd, $vs2, $vs1"> {
  let Constraints = "$vd = $vd_wb";
  let vm = 1;
  let Inst{6-0} = OPC_OP_VE.Value;
}

// op vd, vs2, imm
class PALUVINoVm<bits<6> funct6, string opcodestr, Operand optype>
    : VALUVINoVm<funct6, opcodestr, optype> {
  let Inst{6-0} = OPC_OP_VE.Value;
  let Inst{14-12} = OPMVV.Value;
}

// op vd, vs2, imm where vd is also a source regardless of tail policy
class PALUVINoVmBinary<bits<6> funct6, string opcodestr, Operand optype>
    : RVInstIVI<funct6, (outs VR:$vd_wb),
                (ins VR:$vd, VR:$vs2, optype:$imm),
                opcodestr, "$vd, $vs2, $imm"> {
  let Constraints = "$vd = $vd_wb";
  let vm = 1;
  let Inst{6-0} = OPC_OP_VE.Value;
  let Inst{14-12} = OPMVV.Value;
}

// op vd, vs2 (use vs1 as instruction encoding) where vd is also a source
// regardless of tail policy
class PALUVs2NoVmBinary<bits<6> funct6, bits<5> vs1, RISCVVFormat opv,
                        string opcodestr>
    : RVInstV<funct6, vs1, opv, (outs VR:$vd_wb), (ins VR:$vd, VR:$vs2),
              opcodestr, "$vd, $vs2"> {
  let Constraints = "$vd = $vd_wb";
  let vm = 1;
  let Inst{6-0} = OPC_OP_VE.Value;
}

multiclass VAES_MV_V_S<bits<6> funct6_vv, bits<6> funct6_vs, bits<5> vs1,
                         RISCVVFormat opv, string opcodestr> {
  let RVVConstraint = NoConstraint in
  def NAME # _VV : PALUVs2NoVmBinary<funct6_vv, vs1, opv, opcodestr # ".vv">,
                   SchedBinaryMC<"WriteVAESMVV", "ReadVAESMVV", "ReadVAESMVV">;
  let RVVConstraint = VS2Constraint in
  def NAME # _VS : PALUVs2NoVmBinary<funct6_vs, vs1, opv, opcodestr # ".vs">,
                   SchedBinaryMC<"WriteVAESMVV", "ReadVAESMVV", "ReadVAESMVV">;
}
} // hasSideEffects = 0, mayLoad = 0, mayStore = 0

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

let Predicates = [HasStdExtZvbb] in {
  def  VBREV_V  : VALUVs2<0b010010, 0b01010, OPMVV, "vbrev.v">;
  def  VCLZ_V   : VALUVs2<0b010010, 0b01100, OPMVV, "vclz.v">;
  def  VCPOP_V  : VALUVs2<0b010010, 0b01110, OPMVV, "vcpop.v">;
  def  VCTZ_V   : VALUVs2<0b010010, 0b01101, OPMVV, "vctz.v">;
  let Constraints = "@earlyclobber $vd", RVVConstraint = WidenV in
  defm VWSLL_V  : VSHT_IV_V_X_I<"vwsll", 0b110101>;
} // Predicates = [HasStdExtZvbb]

let Predicates = [HasStdExtZvbc] in {
  defm VCLMUL_V  : VCLMUL_MV_V_X<"vclmul", 0b001100>;
  defm VCLMULH_V : VCLMUL_MV_V_X<"vclmulh", 0b001101>;
} // Predicates = [HasStdExtZvbc]

let Predicates = [HasStdExtZvkb] in {
  defm VANDN_V  : VALU_IV_V_X<"vandn", 0b000001>;
  def  VBREV8_V : VALUVs2<0b010010, 0b01000, OPMVV, "vbrev8.v">;
  def  VREV8_V  : VALUVs2<0b010010, 0b01001, OPMVV, "vrev8.v">;
  defm VROL_V   : VALU_IV_V_X<"vrol", 0b010101>;
  defm VROR_V   : VROR_IV_V_X_I<"vror", 0b010100>;
} // Predicates = [HasStdExtZvkb]

let Predicates = [HasStdExtZvkg], RVVConstraint = NoConstraint in {
  def VGHSH_VV : PALUVVNoVmTernary<0b101100, OPMVV, "vghsh.vv">,
                 SchedTernaryMC<"WriteVGHSHV", "ReadVGHSHV", "ReadVGHSHV",
                                "ReadVGHSHV">;
  def VGMUL_VV : PALUVs2NoVmBinary<0b101000, 0b10001, OPMVV, "vgmul.vv">,
                 SchedBinaryMC<"WriteVGMULV", "ReadVGMULV", "ReadVGMULV">;
} // Predicates = [HasStdExtZvkg]

let Predicates = [HasStdExtZvknhaOrZvknhb], RVVConstraint = Sha2Constraint in {
  def VSHA2CH_VV : PALUVVNoVmTernary<0b101110, OPMVV, "vsha2ch.vv">,
                   SchedTernaryMC<"WriteVSHA2CHV", "ReadVSHA2CHV", "ReadVSHA2CHV",
                                  "ReadVSHA2CHV">;
  def VSHA2CL_VV : PALUVVNoVmTernary<0b101111, OPMVV, "vsha2cl.vv">,
                   SchedTernaryMC<"WriteVSHA2CLV", "ReadVSHA2CLV", "ReadVSHA2CLV",
                                  "ReadVSHA2CLV">;
  def VSHA2MS_VV : PALUVVNoVmTernary<0b101101, OPMVV, "vsha2ms.vv">,
                   SchedTernaryMC<"WriteVSHA2MSV", "ReadVSHA2MSV", "ReadVSHA2MSV",
                                  "ReadVSHA2MSV">;
} // Predicates = [HasStdExtZvknhaOrZvknhb]

let Predicates = [HasStdExtZvkned] in {
  defm VAESDF     : VAES_MV_V_S<0b101000, 0b101001, 0b00001, OPMVV, "vaesdf">;
  defm VAESDM     : VAES_MV_V_S<0b101000, 0b101001, 0b00000, OPMVV, "vaesdm">;
  defm VAESEF     : VAES_MV_V_S<0b101000, 0b101001, 0b00011, OPMVV, "vaesef">;
  defm VAESEM     : VAES_MV_V_S<0b101000, 0b101001, 0b00010, OPMVV, "vaesem">;
  def  VAESKF1_VI : PALUVINoVm<0b100010, "vaeskf1.vi", uimm5>,
                    SchedUnaryMC<"WriteVAESKF1V", "ReadVAESKF1V">;
  def  VAESKF2_VI : PALUVINoVmBinary<0b101010, "vaeskf2.vi", uimm5>,
                    SchedBinaryMC<"WriteVAESKF2V", "ReadVAESKF2V", "ReadVAESKF2V">;
  let RVVConstraint = VS2Constraint in
  def  VAESZ_VS   : PALUVs2NoVmBinary<0b101001, 0b00111, OPMVV, "vaesz.vs">,
                    SchedBinaryMC<"WriteVAESZV", "ReadVAESZV", "ReadVAESZV">;
} // Predicates = [HasStdExtZvkned]

let Predicates = [HasStdExtZvksed] in {
  let RVVConstraint = NoConstraint in
  def  VSM4K_VI : PALUVINoVm<0b100001, "vsm4k.vi", uimm5>,
                  SchedUnaryMC<"WriteVSM4KV", "ReadVSM4KV">;
  defm VSM4R    : VAES_MV_V_S<0b101000, 0b101001, 0b10000, OPMVV, "vsm4r">;
} // Predicates = [HasStdExtZvksed]

let Predicates = [HasStdExtZvksh], RVVConstraint = VS2Constraint in {
  def VSM3C_VI  : PALUVINoVmBinary<0b101011, "vsm3c.vi", uimm5>,
                  SchedBinaryMC<"WriteVSM3CV", "ReadVSM3CV", "ReadVSM3CV">;
  def VSM3ME_VV : PALUVVNoVm<0b100000, OPMVV, "vsm3me.vv">,
                  SchedUnaryMC<"WriteVSM3MEV", "ReadVSM3MEV">;
} // Predicates = [HasStdExtZvksh]

//===----------------------------------------------------------------------===//
// Pseudo instructions
//===----------------------------------------------------------------------===//

defvar I32IntegerVectors = !filter(vti, AllIntegerVectors, !eq(vti.SEW, 32));
defvar I32I64IntegerVectors = !filter(vti, AllIntegerVectors,
                                      !or(!eq(vti.SEW, 32), !eq(vti.SEW, 64)));

class ZvkI32IntegerVectors<string vd_lmul> {
  list<VTypeInfo> vs2_types = !cond(!eq(vd_lmul, "M8")  : !filter(vti, I32IntegerVectors, !le(vti.LMul.octuple, 32)),
                                    !eq(vd_lmul, "M4")  : !filter(vti, I32IntegerVectors, !le(vti.LMul.octuple, 32)),
                                    !eq(vd_lmul, "M2")  : !filter(vti, I32IntegerVectors, !le(vti.LMul.octuple, 16)),
                                    !eq(vd_lmul, "M1")  : !filter(vti, I32IntegerVectors, !le(vti.LMul.octuple, 8)),
                                    !eq(vd_lmul, "MF2")  : !filter(vti, I32IntegerVectors, !le(vti.LMul.octuple, 4)),
                                    !eq(vd_lmul, "MF4")  : !filter(vti, I32IntegerVectors, !le(vti.LMul.octuple, 2)),
                                    !eq(vd_lmul, "MF8")  : !filter(vti, I32IntegerVectors, !le(vti.LMul.octuple, 1)));
}

class ZvkMxSet<string vd_lmul> {
  list<LMULInfo> vs2_lmuls = !cond(!eq(vd_lmul, "M8")  : [V_MF8, V_MF4, V_MF2, V_M1, V_M2, V_M4],
                                   !eq(vd_lmul, "M4")  : [V_MF8, V_MF4, V_MF2, V_M1, V_M2, V_M4],
                                   !eq(vd_lmul, "M2")  : [V_MF8, V_MF4, V_MF2, V_M1, V_M2],
                                   !eq(vd_lmul, "M1")  : [V_MF8, V_MF4, V_MF2, V_M1],
                                   !eq(vd_lmul, "MF2") : [V_MF8, V_MF4, V_MF2],
                                   !eq(vd_lmul, "MF4") : [V_MF8, V_MF4],
                                   !eq(vd_lmul, "MF8") : [V_MF8]);
}

class VPseudoBinaryNoMask_Zvk<DAGOperand RetClass, VReg OpClass> :
      Pseudo<(outs RetClass:$rd_wb),
        (ins RetClass:$rd, OpClass:$rs2, AVL:$vl, ixlenimm:$sew, ixlenimm:$policy), []>,
        RISCVVPseudo {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = "$rd_wb = $rd";
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
  let BaseInstr = !cast<Instruction>(PseudoToVInst<NAME>.VInst);
}

class VPseudoTernaryNoMask_Zvk<VReg RetClass,
                               VReg Op1Class,
                               DAGOperand Op2Class> :
        Pseudo<(outs RetClass:$rd_wb),
               (ins RetClass:$rd, Op1Class:$rs2, Op2Class:$rs1,
                    AVL:$vl, ixlenimm:$sew, ixlenimm:$policy), []>,
        RISCVVPseudo {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Constraints = "$rd_wb = $rd";
  let HasVLOp = 1;
  let HasSEWOp = 1;
  let HasVecPolicyOp = 1;
  let BaseInstr = !cast<Instruction>(PseudoToVInst<NAME>.VInst);
}

multiclass VPseudoBinaryNoMaskPolicy_Zvk<VReg RetClass,
                                         VReg Op1Class,
                                         DAGOperand Op2Class,
                                         LMULInfo MInfo,
                                         string Constraint = ""> {
  let VLMul = MInfo.value in {
    def "_" # MInfo.MX : VPseudoBinaryNoMaskPolicy<RetClass, Op1Class, Op2Class,
                                           Constraint>;
  }
}

multiclass VPseudoTernaryNoMask_Zvk<VReg RetClass,
                                    VReg Op1Class,
                                    DAGOperand Op2Class,
                                    LMULInfo MInfo> {
  let VLMul = MInfo.value in
    def "_" # MInfo.MX : VPseudoTernaryNoMask_Zvk<RetClass, Op1Class, Op2Class>;
}

multiclass VPseudoBinaryV_V_NoMask_Zvk<LMULInfo m> {
  let VLMul = m.value in {
    def "_VV_" # m.MX : VPseudoBinaryNoMask_Zvk<m.vrclass, m.vrclass>;
  }
}

multiclass VPseudoBinaryV_S_NoMask_Zvk<LMULInfo m> {
  let VLMul = m.value in
    foreach vs2_lmul = ZvkMxSet<m.MX>.vs2_lmuls in
      def "_VS_" # m.MX # "_" # vs2_lmul.MX : VPseudoBinaryNoMask_Zvk<m.vrclass, vs2_lmul.vrclass>;
}

multiclass VPseudoVGMUL {
  foreach m = MxListVF4 in {
    defvar mx = m.MX;
    defm "" : VPseudoBinaryV_V_NoMask_Zvk<m>,
              SchedBinary<"WriteVGMULV", "ReadVGMULV", "ReadVGMULV", mx>;
  }
}

multiclass VPseudoVAESMV {
  foreach m = MxListVF4 in {
    defvar mx = m.MX;
    defm "" : VPseudoBinaryV_V_NoMask_Zvk<m>,
              SchedBinary<"WriteVAESMVV", "ReadVAESMVV", "ReadVAESMVV", mx>;
    defm "" : VPseudoBinaryV_S_NoMask_Zvk<m>,
              SchedBinary<"WriteVAESMVV", "ReadVAESMVV", "ReadVAESMVV", mx>;

  }
}

multiclass VPseudoVSM4R {
  foreach m = MxListVF4 in {
    defvar mx = m.MX;
    defm "" : VPseudoBinaryV_V_NoMask_Zvk<m>,
              SchedBinary<"WriteVSM4RV", "ReadVSM4RV", "ReadVSM4RV", mx>;
    defm "" : VPseudoBinaryV_S_NoMask_Zvk<m>,
              SchedBinary<"WriteVSM4RV", "ReadVSM4RV", "ReadVSM4RV", mx>;

  }
}

multiclass VPseudoVGHSH {
  foreach m = MxListVF4 in {
    defvar mx = m.MX;
    defm _VV : VPseudoTernaryNoMask_Zvk<m.vrclass, m.vrclass, m.vrclass, m>,
               SchedTernary<"WriteVGHSHV", "ReadVGHSHV", "ReadVGHSHV",
                            "ReadVGHSHV", mx>;
  }
}

multiclass VPseudoVSHA2CH {
  foreach m = MxListVF4 in {
    defvar mx = m.MX;
    defm _VV : VPseudoTernaryNoMask_Zvk<m.vrclass, m.vrclass, m.vrclass, m>,
               SchedTernary<"WriteVSHA2CHV", "ReadVSHA2CHV", "ReadVSHA2CHV",
                            "ReadVSHA2CHV", mx>;
  }
}

multiclass VPseudoVSHA2CL {
  foreach m = MxListVF4 in {
    defvar mx = m.MX;
    defm _VV : VPseudoTernaryNoMask_Zvk<m.vrclass, m.vrclass, m.vrclass, m>,
               SchedTernary<"WriteVSHA2CLV", "ReadVSHA2CLV", "ReadVSHA2CLV",
                            "ReadVSHA2CLV", mx>;
  }
}

multiclass VPseudoVSHA2MS {
  foreach m = MxListVF4 in {
    defvar mx = m.MX;
    defm _VV : VPseudoTernaryNoMask_Zvk<m.vrclass, m.vrclass, m.vrclass, m>,
               SchedTernary<"WriteVSHA2MSV", "ReadVSHA2MSV", "ReadVSHA2MSV",
                            "ReadVSHA2MSV", mx>;
  }
}

multiclass VPseudoVAESKF1 {
  foreach m = MxListVF4 in {
    defvar mx = m.MX;
    defm _VI : VPseudoBinaryNoMaskPolicy_Zvk<m.vrclass, m.vrclass, uimm5, m>,
               SchedBinary<"WriteVAESKF1V", "ReadVAESKF1V", "ReadVAESKF1V", mx,
                           forceMergeOpRead=true>;
  }
}

multiclass VPseudoVAESKF2 {
  foreach m = MxListVF4 in {
    defvar mx = m.MX;
    defm _VI : VPseudoTernaryNoMask_Zvk<m.vrclass, m.vrclass, uimm5, m>,
               SchedTernary<"WriteVAESKF2V", "ReadVAESKF2V", "ReadVAESKF2V",
                            "ReadVAESKF2V", mx>;
  }
}

multiclass VPseudoVAESZ {
  foreach m = MxListVF4 in {
    defvar mx = m.MX;
    defm "" : VPseudoBinaryV_S_NoMask_Zvk<m>,
              SchedBinary<"WriteVAESZV", "ReadVAESZV", "ReadVAESZV", mx>;
  }
}

multiclass VPseudoVSM3C {
  foreach m = MxListVF4 in {
    defvar mx = m.MX;
    defm _VI : VPseudoTernaryNoMask_Zvk<m.vrclass, m.vrclass, uimm5, m>,
               SchedTernary<"WriteVSM3CV", "ReadVSM3CV", "ReadVSM3CV",
                            "ReadVSM3CV", mx>;
  }
}

multiclass VPseudoVSM4K {
  foreach m = MxListVF4 in {
    defvar mx = m.MX;
    defm _VI : VPseudoBinaryNoMaskPolicy_Zvk<m.vrclass, m.vrclass, uimm5, m>,
               SchedBinary<"WriteVSM4KV", "ReadVSM4KV", "ReadVSM4KV", mx,
                           forceMergeOpRead=true>;
  }
}

multiclass VPseudoVSM3ME {
  foreach m = MxListVF4 in {
    defvar mx = m.MX;
    defm _VV : VPseudoBinaryNoMaskPolicy_Zvk<m.vrclass, m.vrclass, m.vrclass, m>,
               SchedBinary<"WriteVSM3MEV", "ReadVSM3MEV", "ReadVSM3MEV", mx,
                           forceMergeOpRead=true>;
  }
}

multiclass VPseudoVCLMUL_VV_VX {
  foreach m = MxList in {
    defvar mx = m.MX;
    defm "" : VPseudoBinaryV_VV<m>,
              SchedBinary<"WriteVCLMULV", "ReadVCLMULV", "ReadVCLMULV", mx,
                          forceMergeOpRead=true>;
    defm "" : VPseudoBinaryV_VX<m>,
              SchedBinary<"WriteVCLMULX", "ReadVCLMULV", "ReadVCLMULX", mx,
                          forceMergeOpRead=true>;
  }
}

multiclass VPseudoUnaryV_V<LMULInfo m> {
  let VLMul = m.value in {
    defvar suffix = "_V_" # m.MX;
    def suffix : VPseudoUnaryNoMask<m.vrclass, m.vrclass>;
    def suffix # "_MASK" : VPseudoUnaryMask<m.vrclass, m.vrclass>,
                                            RISCVMaskedPseudo<MaskIdx=2>;
  }
}

multiclass VPseudoVBREV {
  foreach m = MxList in {
    defvar mx = m.MX;
    defm "" : VPseudoUnaryV_V<m>,
              SchedUnary<"WriteVBREVV", "ReadVBREVV", mx, forceMergeOpRead=true>;
  }
}

multiclass VPseudoVCLZ {
  foreach m = MxList in {
    defvar mx = m.MX;
    defm "" : VPseudoUnaryV_V<m>,
              SchedUnary<"WriteVCLZV", "ReadVCLZV", mx, forceMergeOpRead=true>;
  }
}

multiclass VPseudoVCTZ {
  foreach m = MxList in {
    defvar mx = m.MX;
    defm "" : VPseudoUnaryV_V<m>,
              SchedUnary<"WriteVCTZV", "ReadVCTZV", mx, forceMergeOpRead=true>;
  }
}

multiclass VPseudoVCPOP {
  foreach m = MxList in {
    defvar mx = m.MX;
    defm "" : VPseudoUnaryV_V<m>,
              SchedUnary<"WriteVCPOPV", "ReadVCPOPV", mx, forceMergeOpRead=true>;
  }
}

multiclass VPseudoVWSLL {
  foreach m = MxListW in {
    defvar mx = m.MX;
    defm "" : VPseudoBinaryW_VV<m>,
              SchedBinary<"WriteVWSLLV", "ReadVWSLLV", "ReadVWSLLV", mx,
                          forceMergeOpRead=true>;
    defm "" : VPseudoBinaryW_VX<m>,
              SchedBinary<"WriteVWSLLX", "ReadVWSLLV", "ReadVWSLLX", mx,
                          forceMergeOpRead=true>;
    defm "" : VPseudoBinaryW_VI<uimm5, m>,
              SchedUnary<"WriteVWSLLI", "ReadVWSLLV", mx,
                         forceMergeOpRead=true>;
  }
}

multiclass VPseudoVANDN {
 foreach m = MxList in {
    defm "" : VPseudoBinaryV_VV<m>,
              SchedBinary<"WriteVIALUV", "ReadVIALUV", "ReadVIALUV", m.MX,
                          forceMergeOpRead=true>;
    defm "" : VPseudoBinaryV_VX<m>,
              SchedBinary<"WriteVIALUX", "ReadVIALUV", "ReadVIALUX", m.MX,
                          forceMergeOpRead=true>;
  }
}

multiclass VPseudoVBREV8 {
  foreach m = MxList in {
    defvar mx = m.MX;
    defm "" : VPseudoUnaryV_V<m>,
              SchedUnary<"WriteVBREV8V", "ReadVBREV8V", mx, forceMergeOpRead=true>;
  }
}

multiclass VPseudoVREV8 {
  foreach m = MxList in {
    defvar mx = m.MX;
    defm "" : VPseudoUnaryV_V<m>,
              SchedUnary<"WriteVREV8V", "ReadVREV8V", mx, forceMergeOpRead=true>;
  }
}

multiclass VPseudoVROT_VV_VX {
 foreach m = MxList in {
    defm "" : VPseudoBinaryV_VV<m>,
              SchedBinary<"WriteVRotV", "ReadVRotV", "ReadVRotV", m.MX,
                          forceMergeOpRead=true>;
    defm "" : VPseudoBinaryV_VX<m>,
              SchedBinary<"WriteVRotX", "ReadVRotV", "ReadVRotX", m.MX,
                          forceMergeOpRead=true>;
  }
}

multiclass VPseudoVROT_VV_VX_VI
   : VPseudoVROT_VV_VX {
  foreach m = MxList in {
    defm "" : VPseudoBinaryV_VI<uimm6, m>,
              SchedUnary<"WriteVRotI", "ReadVRotV", m.MX,
                         forceMergeOpRead=true>;
  }
}

let Predicates = [HasStdExtZvbb] in {
  defm PseudoVBREV  : VPseudoVBREV;
  defm PseudoVCLZ   : VPseudoVCLZ;
  defm PseudoVCTZ   : VPseudoVCTZ;
  defm PseudoVCPOP  : VPseudoVCPOP;
  defm PseudoVWSLL : VPseudoVWSLL;
} // Predicates = [HasStdExtZvbb]

let Predicates = [HasStdExtZvbc] in {
  defm PseudoVCLMUL  : VPseudoVCLMUL_VV_VX;
  defm PseudoVCLMULH : VPseudoVCLMUL_VV_VX;
} // Predicates = [HasStdExtZvbc]

let Predicates = [HasStdExtZvkb] in {
  defm PseudoVANDN  : VPseudoVANDN;
  defm PseudoVBREV8 : VPseudoVBREV8;
  defm PseudoVREV8  : VPseudoVREV8;
  defm PseudoVROL   : VPseudoVROT_VV_VX;
  defm PseudoVROR   : VPseudoVROT_VV_VX_VI;
} // Predicates = [HasStdExtZvkb]

let Predicates = [HasStdExtZvkg] in {
  defm PseudoVGHSH : VPseudoVGHSH;
  defm PseudoVGMUL : VPseudoVGMUL;
} // Predicates = [HasStdExtZvkg]

let Predicates = [HasStdExtZvkned] in {
  defm PseudoVAESDF  : VPseudoVAESMV;
  defm PseudoVAESDM  : VPseudoVAESMV;
  defm PseudoVAESEF  : VPseudoVAESMV;
  defm PseudoVAESEM  : VPseudoVAESMV;
  defm PseudoVAESKF1 : VPseudoVAESKF1;
  defm PseudoVAESKF2 : VPseudoVAESKF2;
  defm PseudoVAESZ   : VPseudoVAESZ;
} // Predicates = [HasStdExtZvkned]

let Predicates = [HasStdExtZvknhaOrZvknhb] in {
  defm PseudoVSHA2CH : VPseudoVSHA2CH;
  defm PseudoVSHA2CL : VPseudoVSHA2CL;
  defm PseudoVSHA2MS : VPseudoVSHA2MS;
} // Predicates = [HasStdExtZvknhaOrZvknhb]

let Predicates = [HasStdExtZvksed] in {
  defm PseudoVSM4K : VPseudoVSM4K;
  defm PseudoVSM4R : VPseudoVSM4R;
} // Predicates = [HasStdExtZvksed]

let Predicates = [HasStdExtZvksh] in {
  defm PseudoVSM3C  : VPseudoVSM3C;
  defm PseudoVSM3ME : VPseudoVSM3ME;
} // Predicates = [HasStdExtZvksh]

//===----------------------------------------------------------------------===//
// SDNode patterns
//===----------------------------------------------------------------------===//

multiclass VPatUnarySDNode_V<SDPatternOperator op, string instruction_name,
                             Predicate predicate = HasStdExtZvbb> {
  foreach vti = AllIntegerVectors in {
    let Predicates = !listconcat([predicate],
                                 GetVTypePredicates<vti>.Predicates) in {
      def : Pat<(vti.Vector (op (vti.Vector vti.RegClass:$rs1))),
                (!cast<Instruction>(instruction_name#"_V_"#vti.LMul.MX)
                   (vti.Vector (IMPLICIT_DEF)),
                   vti.RegClass:$rs1,
                   vti.AVL, vti.Log2SEW, TA_MA)>;
    }
  }
}

// Helpers for detecting splats since we preprocess splat_vector to vmv.v.x
// This should match the logic in RISCVDAGToDAGISel::selectVSplat
def riscv_splat_vector : PatFrag<(ops node:$rs1),
                                 (riscv_vmv_v_x_vl undef, node:$rs1, srcvalue)>;
def riscv_vnot : PatFrag<(ops node:$rs1), (xor node:$rs1,
                                               (riscv_splat_vector -1))>;

foreach vti = AllIntegerVectors in {
  let Predicates = !listconcat([HasStdExtZvkb],
                               GetVTypePredicates<vti>.Predicates) in {
    def : Pat<(vti.Vector (and (riscv_vnot vti.RegClass:$rs1),
                               vti.RegClass:$rs2)),
              (!cast<Instruction>("PseudoVANDN_VV_"#vti.LMul.MX)
                 (vti.Vector (IMPLICIT_DEF)),
                 vti.RegClass:$rs2,
                 vti.RegClass:$rs1,
                 vti.AVL, vti.Log2SEW, TA_MA)>;
    def : Pat<(vti.Vector (and (riscv_splat_vector
                                 (not vti.ScalarRegClass:$rs1)),
                               vti.RegClass:$rs2)),
              (!cast<Instruction>("PseudoVANDN_VX_"#vti.LMul.MX)
                 (vti.Vector (IMPLICIT_DEF)),
                 vti.RegClass:$rs2,
                 vti.ScalarRegClass:$rs1,
                 vti.AVL, vti.Log2SEW, TA_MA)>;
  }
}

defm : VPatUnarySDNode_V<bitreverse, "PseudoVBREV">;
defm : VPatUnarySDNode_V<bswap, "PseudoVREV8", HasStdExtZvkb>;
defm : VPatUnarySDNode_V<ctlz, "PseudoVCLZ">;
defm : VPatUnarySDNode_V<cttz, "PseudoVCTZ">;
defm : VPatUnarySDNode_V<ctpop, "PseudoVCPOP">;

defm : VPatBinarySDNode_VV_VX<rotl, "PseudoVROL">;

// Invert the immediate and mask it to SEW for readability.
def InvRot8Imm : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(0x7 & (64 - N->getZExtValue()), SDLoc(N),
                                   N->getValueType(0));
}]>;
def InvRot16Imm : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(0xf & (64 - N->getZExtValue()), SDLoc(N),
                                   N->getValueType(0));
}]>;
def InvRot32Imm : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(0x1f & (64 - N->getZExtValue()), SDLoc(N),
                                   N->getValueType(0));
}]>;
def InvRot64Imm : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(0x3f & (64 - N->getZExtValue()), SDLoc(N),
                                   N->getValueType(0));
}]>;

// Although there is no vrol.vi, an immediate rotate left can be achieved by
// negating the immediate in vror.vi
foreach vti = AllIntegerVectors in {
  let Predicates = !listconcat([HasStdExtZvkb],
                               GetVTypePredicates<vti>.Predicates) in {
    def : Pat<(vti.Vector (rotl vti.RegClass:$rs2,
                                (vti.Vector (SplatPat_uimm6 uimm6:$rs1)))),
              (!cast<Instruction>("PseudoVROR_VI_"#vti.LMul.MX)
                 (vti.Vector (IMPLICIT_DEF)),
                 vti.RegClass:$rs2,
                 (!cast<SDNodeXForm>("InvRot" # vti.SEW # "Imm") uimm6:$rs1),
                 vti.AVL, vti.Log2SEW, TA_MA)>;
  }
}
defm : VPatBinarySDNode_VV_VX_VI<rotr, "PseudoVROR", uimm6>;

foreach vtiToWti = AllWidenableIntVectors in {
  defvar vti = vtiToWti.Vti;
  defvar wti = vtiToWti.Wti;
  let Predicates = !listconcat([HasStdExtZvbb],
                               GetVTypePredicates<vti>.Predicates,
                               GetVTypePredicates<wti>.Predicates) in {
    def : Pat<(shl (wti.Vector (zext_oneuse (vti.Vector vti.RegClass:$rs2))),
                   (wti.Vector (ext_oneuse (vti.Vector vti.RegClass:$rs1)))),
              (!cast<Instruction>("PseudoVWSLL_VV_"#vti.LMul.MX)
                 (wti.Vector (IMPLICIT_DEF)),
                 vti.RegClass:$rs2, vti.RegClass:$rs1,
                 vti.AVL, vti.Log2SEW, TA_MA)>;

    def : Pat<(shl (wti.Vector (zext_oneuse (vti.Vector vti.RegClass:$rs2))),
                   (wti.Vector (Low8BitsSplatPat (XLenVT GPR:$rs1)))),
              (!cast<Instruction>("PseudoVWSLL_VX_"#vti.LMul.MX)
                 (wti.Vector (IMPLICIT_DEF)),
                 vti.RegClass:$rs2, GPR:$rs1,
                 vti.AVL, vti.Log2SEW, TA_MA)>;

    def : Pat<(shl (wti.Vector (zext_oneuse (vti.Vector vti.RegClass:$rs2))),
                   (wti.Vector (SplatPat_uimm5 uimm5:$rs1))),
              (!cast<Instruction>("PseudoVWSLL_VI_"#vti.LMul.MX)
                 (wti.Vector (IMPLICIT_DEF)),
                 vti.RegClass:$rs2, uimm5:$rs1,
                 vti.AVL, vti.Log2SEW, TA_MA)>;
  }
}

//===----------------------------------------------------------------------===//
// VL patterns
//===----------------------------------------------------------------------===//

multiclass VPatUnaryVL_V<SDPatternOperator op, string instruction_name,
                         Predicate predicate = HasStdExtZvbb> {
  foreach vti = AllIntegerVectors in {
    let Predicates = !listconcat([predicate],
                                 GetVTypePredicates<vti>.Predicates) in {
      def : Pat<(vti.Vector (op (vti.Vector vti.RegClass:$rs1),
                                (vti.Vector vti.RegClass:$merge),
                                (vti.Mask V0),
                                VLOpFrag)),
                (!cast<Instruction>(instruction_name#"_V_"#vti.LMul.MX#"_MASK")
                   vti.RegClass:$merge,
                   vti.RegClass:$rs1,
                   (vti.Mask V0),
                   GPR:$vl,
                   vti.Log2SEW,
                   TAIL_AGNOSTIC)>;
    }
  }
}

foreach vti = AllIntegerVectors in {
  let Predicates = !listconcat([HasStdExtZvkb],
                               GetVTypePredicates<vti>.Predicates) in {
    def : Pat<(vti.Vector (riscv_and_vl (riscv_xor_vl
                                           (vti.Vector vti.RegClass:$rs1),
                                           (riscv_splat_vector -1),
                                           (vti.Vector vti.RegClass:$merge),
                                           (vti.Mask V0),
                                           VLOpFrag),
                                        (vti.Vector vti.RegClass:$rs2),
                                        (vti.Vector vti.RegClass:$merge),
                                        (vti.Mask V0),
                                        VLOpFrag)),
              (!cast<Instruction>("PseudoVANDN_VV_"#vti.LMul.MX#"_MASK")
                 vti.RegClass:$merge,
                 vti.RegClass:$rs2,
                 vti.RegClass:$rs1,
                 (vti.Mask V0),
                 GPR:$vl,
                 vti.Log2SEW,
                 TAIL_AGNOSTIC)>;

    def : Pat<(vti.Vector (riscv_and_vl (riscv_splat_vector
                                           (not vti.ScalarRegClass:$rs1)),
                                        (vti.Vector vti.RegClass:$rs2),
                                        (vti.Vector vti.RegClass:$merge),
                                        (vti.Mask V0),
                                        VLOpFrag)),
              (!cast<Instruction>("PseudoVANDN_VX_"#vti.LMul.MX#"_MASK")
                 vti.RegClass:$merge,
                 vti.RegClass:$rs2,
                 vti.ScalarRegClass:$rs1,
                 (vti.Mask V0),
                 GPR:$vl,
                 vti.Log2SEW,
                 TAIL_AGNOSTIC)>;
  }
}

defm : VPatUnaryVL_V<riscv_bitreverse_vl, "PseudoVBREV">;
defm : VPatUnaryVL_V<riscv_bswap_vl, "PseudoVREV8", HasStdExtZvkb>;
defm : VPatUnaryVL_V<riscv_ctlz_vl, "PseudoVCLZ">;
defm : VPatUnaryVL_V<riscv_cttz_vl, "PseudoVCTZ">;
defm : VPatUnaryVL_V<riscv_ctpop_vl, "PseudoVCPOP">;

defm : VPatBinaryVL_VV_VX<riscv_rotl_vl, "PseudoVROL">;
// Although there is no vrol.vi, an immediate rotate left can be achieved by
// negating the immediate in vror.vi
foreach vti = AllIntegerVectors in {
  let Predicates = !listconcat([HasStdExtZvkb],
                               GetVTypePredicates<vti>.Predicates) in {
    def : Pat<(riscv_rotl_vl vti.RegClass:$rs2,
                             (vti.Vector (SplatPat_uimm6 uimm6:$rs1)),
                             (vti.Vector vti.RegClass:$merge),
                             (vti.Mask V0), VLOpFrag),
              (!cast<Instruction>("PseudoVROR_VI_"#vti.LMul.MX#"_MASK")
                 vti.RegClass:$merge,
                 vti.RegClass:$rs2,
                 (!cast<SDNodeXForm>("InvRot" # vti.SEW # "Imm") uimm6:$rs1),
                 (vti.Mask V0), GPR:$vl, vti.Log2SEW, TAIL_AGNOSTIC)>;
  }
}
defm : VPatBinaryVL_VV_VX_VI<riscv_rotr_vl, "PseudoVROR", uimm6>;

foreach vtiToWti = AllWidenableIntVectors in {
  defvar vti = vtiToWti.Vti;
  defvar wti = vtiToWti.Wti;
  let Predicates = !listconcat([HasStdExtZvbb],
                               GetVTypePredicates<vti>.Predicates,
                               GetVTypePredicates<wti>.Predicates) in {
    def : Pat<(riscv_shl_vl
                 (wti.Vector (zext_oneuse (vti.Vector vti.RegClass:$rs2))),
                 (wti.Vector (ext_oneuse (vti.Vector vti.RegClass:$rs1))),
                 (wti.Vector wti.RegClass:$merge),
                 (vti.Mask V0), VLOpFrag),
              (!cast<Instruction>("PseudoVWSLL_VV_"#vti.LMul.MX#"_MASK")
                 wti.RegClass:$merge, vti.RegClass:$rs2, vti.RegClass:$rs1,
                 (vti.Mask V0), GPR:$vl, vti.Log2SEW, TAIL_AGNOSTIC)>;

    def : Pat<(riscv_shl_vl
                 (wti.Vector (riscv_zext_vl_oneuse
                                (vti.Vector vti.RegClass:$rs2),
                                (vti.Mask V0), VLOpFrag)),
                 (wti.Vector (riscv_ext_vl_oneuse
                                (vti.Vector vti.RegClass:$rs1),
                                (vti.Mask V0), VLOpFrag)),
                 (wti.Vector wti.RegClass:$merge),
                 (vti.Mask V0), VLOpFrag),
              (!cast<Instruction>("PseudoVWSLL_VV_"#vti.LMul.MX#"_MASK")
                 wti.RegClass:$merge, vti.RegClass:$rs2, vti.RegClass:$rs1,
                 (vti.Mask V0), GPR:$vl, vti.Log2SEW, TAIL_AGNOSTIC)>;

    def : Pat<(riscv_shl_vl
                 (wti.Vector (zext_oneuse (vti.Vector vti.RegClass:$rs2))),
                 (wti.Vector (Low8BitsSplatPat (XLenVT GPR:$rs1))),
                 (wti.Vector wti.RegClass:$merge),
                 (vti.Mask V0), VLOpFrag),
              (!cast<Instruction>("PseudoVWSLL_VX_"#vti.LMul.MX#"_MASK")
                 wti.RegClass:$merge, vti.RegClass:$rs2, GPR:$rs1,
                 (vti.Mask V0), GPR:$vl, vti.Log2SEW, TAIL_AGNOSTIC)>;

    def : Pat<(riscv_shl_vl
                 (wti.Vector (riscv_zext_vl_oneuse
                                (vti.Vector vti.RegClass:$rs2),
                                (vti.Mask V0), VLOpFrag)),
                 (wti.Vector (Low8BitsSplatPat (XLenVT GPR:$rs1))),
                 (wti.Vector wti.RegClass:$merge),
                 (vti.Mask V0), VLOpFrag),
              (!cast<Instruction>("PseudoVWSLL_VX_"#vti.LMul.MX#"_MASK")
                 wti.RegClass:$merge, vti.RegClass:$rs2, GPR:$rs1,
                 (vti.Mask V0), GPR:$vl, vti.Log2SEW, TAIL_AGNOSTIC)>;

    def : Pat<(riscv_shl_vl
                 (wti.Vector (zext_oneuse (vti.Vector vti.RegClass:$rs2))),
                 (wti.Vector (SplatPat_uimm5 uimm5:$rs1)),
                 (wti.Vector wti.RegClass:$merge),
                 (vti.Mask V0), VLOpFrag),
              (!cast<Instruction>("PseudoVWSLL_VI_"#vti.LMul.MX#"_MASK")
                 wti.RegClass:$merge, vti.RegClass:$rs2, uimm5:$rs1,
                 (vti.Mask V0), GPR:$vl, vti.Log2SEW, TAIL_AGNOSTIC)>;

    def : Pat<(riscv_shl_vl
                 (wti.Vector (riscv_zext_vl_oneuse
                                (vti.Vector vti.RegClass:$rs2),
                                (vti.Mask V0), VLOpFrag)),
                 (wti.Vector (SplatPat_uimm5 uimm5:$rs1)),
                 (wti.Vector wti.RegClass:$merge),
                 (vti.Mask V0), VLOpFrag),
              (!cast<Instruction>("PseudoVWSLL_VI_"#vti.LMul.MX#"_MASK")
                 wti.RegClass:$merge, vti.RegClass:$rs2, uimm5:$rs1,
                 (vti.Mask V0), GPR:$vl, vti.Log2SEW, TAIL_AGNOSTIC)>;

    def : Pat<(riscv_vwsll_vl
                 (vti.Vector vti.RegClass:$rs2),
                 (vti.Vector vti.RegClass:$rs1),
                 (wti.Vector wti.RegClass:$merge),
                 (vti.Mask V0), VLOpFrag),
              (!cast<Instruction>("PseudoVWSLL_VV_"#vti.LMul.MX#"_MASK")
                 wti.RegClass:$merge, vti.RegClass:$rs2, vti.RegClass:$rs1,
                 (vti.Mask V0), GPR:$vl, vti.Log2SEW, TAIL_AGNOSTIC)>;

    def : Pat<(riscv_vwsll_vl
                 (vti.Vector vti.RegClass:$rs2),
                 (vti.Vector (Low8BitsSplatPat (XLenVT GPR:$rs1))),
                 (wti.Vector wti.RegClass:$merge),
                 (vti.Mask V0), VLOpFrag),
              (!cast<Instruction>("PseudoVWSLL_VX_"#vti.LMul.MX#"_MASK")
                 wti.RegClass:$merge, vti.RegClass:$rs2, GPR:$rs1,
                 (vti.Mask V0), GPR:$vl, vti.Log2SEW, TAIL_AGNOSTIC)>;

    def : Pat<(riscv_vwsll_vl
                 (vti.Vector vti.RegClass:$rs2),
                 (vti.Vector (SplatPat_uimm5 uimm5:$rs1)),
                 (wti.Vector wti.RegClass:$merge),
                 (vti.Mask V0), VLOpFrag),
              (!cast<Instruction>("PseudoVWSLL_VI_"#vti.LMul.MX#"_MASK")
                 wti.RegClass:$merge, vti.RegClass:$rs2, uimm5:$rs1,
                 (vti.Mask V0), GPR:$vl, vti.Log2SEW, TAIL_AGNOSTIC)>;
  }
}

//===----------------------------------------------------------------------===//
// Codegen patterns
//===----------------------------------------------------------------------===//

class VPatUnaryNoMask_Zvk<string intrinsic_name,
                          string inst,
                          string kind,
                          ValueType result_type,
                          ValueType op2_type,
                          int sew,
                          LMULInfo vlmul,
                          VReg result_reg_class,
                          VReg op2_reg_class> :
  Pat<(result_type (!cast<Intrinsic>(intrinsic_name)
                   (result_type result_reg_class:$rd),
                   (op2_type op2_reg_class:$rs2),
                   VLOpFrag, (XLenVT timm:$policy))),
                   (!cast<Instruction>(inst#"_"#kind#"_"#vlmul.MX)
                   (result_type result_reg_class:$rd),
                   (op2_type op2_reg_class:$rs2),
                   GPR:$vl, sew, (XLenVT timm:$policy))>;

class VPatUnaryNoMask_VS_Zvk<string intrinsic_name,
                             string inst,
                             string kind,
                             ValueType result_type,
                             ValueType op2_type,
                             int sew,
                             LMULInfo vlmul,
                             LMULInfo vs2_lmul,
                             VReg result_reg_class,
                             VReg op2_reg_class> :
  Pat<(result_type (!cast<Intrinsic>(intrinsic_name)
                   (result_type result_reg_class:$rd),
                   (op2_type op2_reg_class:$rs2),
                   VLOpFrag, (XLenVT timm:$policy))),
                   (!cast<Instruction>(inst#"_"#kind#"_"#vlmul.MX#"_"#vs2_lmul.MX)
                   (result_type result_reg_class:$rd),
                   (op2_type op2_reg_class:$rs2),
                   GPR:$vl, sew, (XLenVT timm:$policy))>;

multiclass VPatUnaryV_V_NoMask_Zvk<string intrinsic, string instruction,
                                   list<VTypeInfo> vtilist> {
  foreach vti = vtilist in
    def : VPatUnaryNoMask_Zvk<intrinsic # "_vv", instruction, "VV",
                          vti.Vector, vti.Vector, vti.Log2SEW,
                          vti.LMul, vti.RegClass, vti.RegClass>;
}

multiclass VPatUnaryV_S_NoMaskVectorCrypto<string intrinsic, string instruction,
                                           list<VTypeInfo> vtilist> {
  foreach vti = vtilist in
    foreach vti_vs2 = ZvkI32IntegerVectors<vti.LMul.MX>.vs2_types in
      def : VPatUnaryNoMask_VS_Zvk<intrinsic # "_vs", instruction, "VS",
                            vti.Vector, vti_vs2.Vector, vti.Log2SEW,
                            vti.LMul, vti_vs2.LMul, vti.RegClass, vti_vs2.RegClass>;
}

multiclass VPatUnaryV_V_S_NoMask_Zvk<string intrinsic, string instruction,
                                     list<VTypeInfo> vtilist> {
  defm : VPatUnaryV_V_NoMask_Zvk<intrinsic, instruction, vtilist>;
  defm : VPatUnaryV_S_NoMaskVectorCrypto<intrinsic, instruction, vtilist>;
}

multiclass VPatBinaryV_VV_NoMask<string intrinsic, string instruction,
                                 list<VTypeInfo> vtilist> {
  foreach vti = vtilist in
    def : VPatTernaryNoMaskWithPolicy<intrinsic, instruction, "VV",
                                      vti.Vector, vti.Vector, vti.Vector,
                                      vti.Log2SEW, vti.LMul, vti.RegClass,
                                      vti.RegClass, vti.RegClass>;
}

multiclass VPatBinaryV_VI_NoMask<string intrinsic, string instruction,
                                 list<VTypeInfo> vtilist,
                                 Operand imm_type = tuimm5> {
  foreach vti = vtilist in
    def : VPatTernaryNoMaskWithPolicy<intrinsic, instruction, "VI",
                                      vti.Vector, vti.Vector, XLenVT,
                                      vti.Log2SEW, vti.LMul, vti.RegClass,
                                      vti.RegClass, imm_type>;
}

multiclass VPatBinaryV_VI_NoMaskTU<string intrinsic, string instruction,
                                   list<VTypeInfo> vtilist,
                                   Operand imm_type = tuimm5> {
  foreach vti = vtilist in
    def : VPatBinaryNoMaskTU<intrinsic, instruction # "_VI_" # vti.LMul.MX,
                             vti.Vector, vti.Vector, XLenVT, vti.Log2SEW,
                             vti.RegClass, vti.RegClass, imm_type>;
}

multiclass VPatBinaryV_VV_NoMaskTU<string intrinsic, string instruction,
                                   list<VTypeInfo> vtilist> {
  foreach vti = vtilist in
    def : VPatBinaryNoMaskTU<intrinsic, instruction # "_VV_" # vti.LMul.MX,
                             vti.Vector, vti.Vector, vti.Vector, vti.Log2SEW,
                             vti.RegClass, vti.RegClass, vti.RegClass>;
}

multiclass VPatBinaryV_VX_VROTATE<string intrinsic, string instruction,
                                  list<VTypeInfo> vtilist, bit isSEWAware = 0> {
  foreach vti = vtilist in {
    defvar kind = "V"#vti.ScalarSuffix;
    let Predicates = GetVTypePredicates<vti>.Predicates in
    defm : VPatBinary<intrinsic,
                      !if(isSEWAware,
                          instruction#"_"#kind#"_"#vti.LMul.MX#"_E"#vti.SEW,
                          instruction#"_"#kind#"_"#vti.LMul.MX),
                      vti.Vector, vti.Vector, XLenVT, vti.Mask,
                      vti.Log2SEW, vti.RegClass,
                      vti.RegClass, vti.ScalarRegClass>;
  }
}

multiclass VPatBinaryV_VI_VROL<string intrinsic, string instruction,
                               list<VTypeInfo> vtilist, bit isSEWAware = 0> {
  foreach vti = vtilist in {
    defvar Intr = !cast<Intrinsic>(intrinsic);
    defvar Pseudo = !cast<Instruction>(
        !if(isSEWAware, instruction#"_VI_"#vti.LMul.MX#"_E"#vti.SEW,
                        instruction#"_VI_"#vti.LMul.MX));
    let Predicates = GetVTypePredicates<vti>.Predicates in
    def : Pat<(vti.Vector (Intr (vti.Vector vti.RegClass:$merge),
                          (vti.Vector vti.RegClass:$rs2),
                          (XLenVT uimm6:$rs1),
                          VLOpFrag)),
                          (Pseudo (vti.Vector vti.RegClass:$merge),
                          (vti.Vector vti.RegClass:$rs2),
                          (InvRot64Imm uimm6:$rs1),
                          GPR:$vl, vti.Log2SEW, TU_MU)>;

    defvar IntrMask = !cast<Intrinsic>(intrinsic#"_mask");
    defvar PseudoMask = !cast<Instruction>(
        !if(isSEWAware, instruction#"_VI_"#vti.LMul.MX#"_E"#vti.SEW#"_MASK",
                        instruction#"_VI_"#vti.LMul.MX#"_MASK"));
    let Predicates = GetVTypePredicates<vti>.Predicates in
    def : Pat<(vti.Vector (IntrMask (vti.Vector vti.RegClass:$merge),
                          (vti.Vector vti.RegClass:$rs2),
                          (XLenVT uimm6:$rs1),
                          (vti.Mask V0),
                          VLOpFrag, (XLenVT timm:$policy))),
                          (PseudoMask (vti.Vector vti.RegClass:$merge),
                          (vti.Vector vti.RegClass:$rs2),
                          (InvRot64Imm uimm6:$rs1),
                          (vti.Mask V0),
                          GPR:$vl, vti.Log2SEW, (XLenVT timm:$policy))>;
    }
}

multiclass VPatBinaryV_VV_VX_VROL<string intrinsic, string instruction,
                                  string instruction2, list<VTypeInfo> vtilist>
    : VPatBinaryV_VV<intrinsic, instruction, vtilist>,
      VPatBinaryV_VX_VROTATE<intrinsic, instruction, vtilist>,
      VPatBinaryV_VI_VROL<intrinsic, instruction2, vtilist>;

multiclass VPatBinaryV_VV_VX_VI_VROR<string intrinsic, string instruction,
                                     list<VTypeInfo> vtilist>
    : VPatBinaryV_VV<intrinsic, instruction, vtilist>,
      VPatBinaryV_VX_VROTATE<intrinsic, instruction, vtilist>,
      VPatBinaryV_VI<intrinsic, instruction, vtilist, uimm6>;

multiclass VPatBinaryW_VV_VX_VI_VWSLL<string intrinsic, string instruction,
                                      list<VTypeInfoToWide> vtilist>
    : VPatBinaryW_VV<intrinsic, instruction, vtilist> {
  foreach VtiToWti = vtilist in {
    defvar Vti = VtiToWti.Vti;
    defvar Wti = VtiToWti.Wti;
    defvar kind = "V"#Vti.ScalarSuffix;
    let Predicates = !listconcat(GetVTypePredicates<Vti>.Predicates,
                                 GetVTypePredicates<Wti>.Predicates) in {
      defm : VPatBinary<intrinsic, instruction#"_"#kind#"_"#Vti.LMul.MX,
                        Wti.Vector, Vti.Vector, XLenVT, Vti.Mask,
                        Vti.Log2SEW, Wti.RegClass,
                        Vti.RegClass, Vti.ScalarRegClass>;
      defm : VPatBinary<intrinsic, instruction # "_VI_" # Vti.LMul.MX,
                        Wti.Vector, Vti.Vector, XLenVT, Vti.Mask,
                        Vti.Log2SEW, Wti.RegClass,
                        Vti.RegClass, uimm5>;
    }
  }
}

let Predicates = [HasStdExtZvbb] in {
  defm : VPatUnaryV_V<"int_riscv_vbrev", "PseudoVBREV", AllIntegerVectors>;
  defm : VPatUnaryV_V<"int_riscv_vclz", "PseudoVCLZ", AllIntegerVectors>;
  defm : VPatUnaryV_V<"int_riscv_vctz", "PseudoVCTZ", AllIntegerVectors>;
  defm : VPatUnaryV_V<"int_riscv_vcpopv", "PseudoVCPOP", AllIntegerVectors>;
  defm : VPatBinaryW_VV_VX_VI_VWSLL<"int_riscv_vwsll", "PseudoVWSLL", AllWidenableIntVectors>;
} // Predicates = [HasStdExtZvbb]

let Predicates = [HasStdExtZvbc] in {
  defm : VPatBinaryV_VV_VX<"int_riscv_vclmul", "PseudoVCLMUL", I64IntegerVectors>;
  defm : VPatBinaryV_VV_VX<"int_riscv_vclmulh", "PseudoVCLMULH", I64IntegerVectors>;
} // Predicates = [HasStdExtZvbc]

let Predicates = [HasStdExtZvkb] in {
  defm : VPatBinaryV_VV_VX<"int_riscv_vandn", "PseudoVANDN", AllIntegerVectors>;
  defm : VPatUnaryV_V<"int_riscv_vbrev8", "PseudoVBREV8", AllIntegerVectors>;
  defm : VPatUnaryV_V<"int_riscv_vrev8", "PseudoVREV8", AllIntegerVectors>;
  defm : VPatBinaryV_VV_VX_VROL<"int_riscv_vrol", "PseudoVROL", "PseudoVROR", AllIntegerVectors>;
  defm : VPatBinaryV_VV_VX_VI_VROR<"int_riscv_vror", "PseudoVROR", AllIntegerVectors>;
} // Predicates = [HasStdExtZvkb]

let Predicates = [HasStdExtZvkg] in {
  defm : VPatBinaryV_VV_NoMask<"int_riscv_vghsh", "PseudoVGHSH", I32IntegerVectors>;
  defm : VPatUnaryV_V_NoMask_Zvk<"int_riscv_vgmul", "PseudoVGMUL", I32IntegerVectors>;
} // Predicates = [HasStdExtZvkg]

let Predicates = [HasStdExtZvkned] in {
  defm : VPatUnaryV_V_S_NoMask_Zvk<"int_riscv_vaesdf", "PseudoVAESDF", I32IntegerVectors>;
  defm : VPatUnaryV_V_S_NoMask_Zvk<"int_riscv_vaesdm", "PseudoVAESDM", I32IntegerVectors>;
  defm : VPatUnaryV_V_S_NoMask_Zvk<"int_riscv_vaesef", "PseudoVAESEF", I32IntegerVectors>;
  defm : VPatUnaryV_V_S_NoMask_Zvk<"int_riscv_vaesem", "PseudoVAESEM", I32IntegerVectors>;
  defm : VPatBinaryV_VI_NoMaskTU<"int_riscv_vaeskf1", "PseudoVAESKF1", I32IntegerVectors>;
  defm : VPatBinaryV_VI_NoMask<"int_riscv_vaeskf2", "PseudoVAESKF2", I32IntegerVectors>;
  defm : VPatUnaryV_S_NoMaskVectorCrypto<"int_riscv_vaesz", "PseudoVAESZ", I32IntegerVectors>;
} // Predicates = [HasStdExtZvkned]

let Predicates = [HasStdExtZvknha] in {
  defm : VPatBinaryV_VV_NoMask<"int_riscv_vsha2ch", "PseudoVSHA2CH", I32IntegerVectors>;
  defm : VPatBinaryV_VV_NoMask<"int_riscv_vsha2cl", "PseudoVSHA2CH", I32IntegerVectors>;
  defm : VPatBinaryV_VV_NoMask<"int_riscv_vsha2ms", "PseudoVSHA2MS", I32IntegerVectors>;
} // Predicates = [HasStdExtZvknha]

let Predicates = [HasStdExtZvknhb] in {
  defm : VPatBinaryV_VV_NoMask<"int_riscv_vsha2ch", "PseudoVSHA2CH", I32I64IntegerVectors>;
  defm : VPatBinaryV_VV_NoMask<"int_riscv_vsha2cl", "PseudoVSHA2CH", I32I64IntegerVectors>;
  defm : VPatBinaryV_VV_NoMask<"int_riscv_vsha2ms", "PseudoVSHA2MS", I32I64IntegerVectors>;
} // Predicates = [HasStdExtZvknhb]

let Predicates = [HasStdExtZvksed] in {
  defm : VPatBinaryV_VI_NoMaskTU<"int_riscv_vsm4k", "PseudoVSM4K", I32IntegerVectors>;
  defm : VPatUnaryV_V_S_NoMask_Zvk<"int_riscv_vsm4r", "PseudoVSM4R", I32IntegerVectors>;
} // Predicates = [HasStdExtZvksed]

let Predicates = [HasStdExtZvksh] in {
  defm : VPatBinaryV_VI_NoMask<"int_riscv_vsm3c", "PseudoVSM3C", I32IntegerVectors>;
  defm : VPatBinaryV_VV_NoMaskTU<"int_riscv_vsm3me", "PseudoVSM3ME", I32IntegerVectors>;
} // Predicates = [HasStdExtZvksh]

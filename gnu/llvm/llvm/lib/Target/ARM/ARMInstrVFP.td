//===-- ARMInstrVFP.td - VFP support for ARM ---------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the ARM VFP instruction set.
//
//===----------------------------------------------------------------------===//

def SDT_CMPFP0  : SDTypeProfile<0, 1, [SDTCisFP<0>]>;
def SDT_VMOVDRR : SDTypeProfile<1, 2, [SDTCisVT<0, f64>, SDTCisVT<1, i32>,
                                       SDTCisSameAs<1, 2>]>;
def SDT_VMOVRRD : SDTypeProfile<2, 1, [SDTCisVT<0, i32>, SDTCisSameAs<0, 1>,
                                       SDTCisVT<2, f64>]>;

def SDT_VMOVSR : SDTypeProfile<1, 1, [SDTCisVT<0, f32>, SDTCisVT<1, i32>]>;

def arm_fmstat : SDNode<"ARMISD::FMSTAT",  SDTNone, [SDNPInGlue, SDNPOutGlue]>;
def arm_cmpfp  : SDNode<"ARMISD::CMPFP",   SDT_ARMCmp, [SDNPOutGlue]>;
def arm_cmpfp0 : SDNode<"ARMISD::CMPFPw0", SDT_CMPFP0, [SDNPOutGlue]>;
def arm_cmpfpe : SDNode<"ARMISD::CMPFPE",  SDT_ARMCmp, [SDNPOutGlue]>;
def arm_cmpfpe0: SDNode<"ARMISD::CMPFPEw0",SDT_CMPFP0, [SDNPOutGlue]>;
def arm_fmdrr  : SDNode<"ARMISD::VMOVDRR", SDT_VMOVDRR>;
def arm_fmrrd  : SDNode<"ARMISD::VMOVRRD", SDT_VMOVRRD>;
def arm_vmovsr  : SDNode<"ARMISD::VMOVSR", SDT_VMOVSR>;

def SDT_VMOVhr : SDTypeProfile<1, 1, [SDTCisFP<0>, SDTCisVT<1, i32>] >;
def SDT_VMOVrh : SDTypeProfile<1, 1, [SDTCisVT<0, i32>, SDTCisFP<1>] >;
def arm_vmovhr : SDNode<"ARMISD::VMOVhr", SDT_VMOVhr>;
def arm_vmovrh : SDNode<"ARMISD::VMOVrh", SDT_VMOVrh>;

//===----------------------------------------------------------------------===//
// Operand Definitions.
//

// 8-bit floating-point immediate encodings.
def FPImmOperand : AsmOperandClass {
  let Name = "FPImm";
  let ParserMethod = "parseFPImm";
}

def vfp_f16imm : Operand<f16>,
                 PatLeaf<(f16 fpimm), [{
      return ARM_AM::getFP16Imm(N->getValueAPF()) != -1;
    }], SDNodeXForm<fpimm, [{
      APFloat InVal = N->getValueAPF();
      uint32_t enc = ARM_AM::getFP16Imm(InVal);
      return CurDAG->getTargetConstant(enc, SDLoc(N), MVT::i32);
    }]>> {
  let PrintMethod = "printFPImmOperand";
  let ParserMatchClass = FPImmOperand;
}

def vfp_f32f16imm_xform : SDNodeXForm<fpimm, [{
      APFloat InVal = N->getValueAPF();
      uint32_t enc = ARM_AM::getFP32FP16Imm(InVal);
      return CurDAG->getTargetConstant(enc, SDLoc(N), MVT::i32);
    }]>;

def vfp_f32f16imm : PatLeaf<(f32 fpimm), [{
      return ARM_AM::getFP32FP16Imm(N->getValueAPF()) != -1;
    }], vfp_f32f16imm_xform>;

def vfp_f32imm_xform : SDNodeXForm<fpimm, [{
      APFloat InVal = N->getValueAPF();
      uint32_t enc = ARM_AM::getFP32Imm(InVal);
      return CurDAG->getTargetConstant(enc, SDLoc(N), MVT::i32);
    }]>;

def gi_vfp_f32imm : GICustomOperandRenderer<"renderVFPF32Imm">,
                    GISDNodeXFormEquiv<vfp_f32imm_xform>;

def vfp_f32imm : Operand<f32>,
                 PatLeaf<(f32 fpimm), [{
      return ARM_AM::getFP32Imm(N->getValueAPF()) != -1;
    }], vfp_f32imm_xform> {
  let PrintMethod = "printFPImmOperand";
  let ParserMatchClass = FPImmOperand;
  let GISelPredicateCode = [{
      const auto &MO = MI.getOperand(1);
      if (!MO.isFPImm())
        return false;
      return ARM_AM::getFP32Imm(MO.getFPImm()->getValueAPF()) != -1;
    }];
}

def vfp_f64imm_xform : SDNodeXForm<fpimm, [{
      APFloat InVal = N->getValueAPF();
      uint32_t enc = ARM_AM::getFP64Imm(InVal);
      return CurDAG->getTargetConstant(enc, SDLoc(N), MVT::i32);
    }]>;

def gi_vfp_f64imm : GICustomOperandRenderer<"renderVFPF64Imm">,
                    GISDNodeXFormEquiv<vfp_f64imm_xform>;

def vfp_f64imm : Operand<f64>,
                 PatLeaf<(f64 fpimm), [{
      return ARM_AM::getFP64Imm(N->getValueAPF()) != -1;
    }], vfp_f64imm_xform> {
  let PrintMethod = "printFPImmOperand";
  let ParserMatchClass = FPImmOperand;
  let GISelPredicateCode = [{
      const auto &MO = MI.getOperand(1);
      if (!MO.isFPImm())
        return false;
      return ARM_AM::getFP64Imm(MO.getFPImm()->getValueAPF()) != -1;
    }];
}

def alignedload16 : PatFrag<(ops node:$ptr), (load node:$ptr), [{
  return cast<LoadSDNode>(N)->getAlign() >= 2;
}]>;

def alignedload32 : PatFrag<(ops node:$ptr), (load node:$ptr), [{
  return cast<LoadSDNode>(N)->getAlign() >= 4;
}]>;

def alignedstore16 : PatFrag<(ops node:$val, node:$ptr),
                             (store node:$val, node:$ptr), [{
  return cast<StoreSDNode>(N)->getAlign() >= 2;
}]>;

def alignedstore32 : PatFrag<(ops node:$val, node:$ptr),
                             (store node:$val, node:$ptr), [{
  return cast<StoreSDNode>(N)->getAlign() >= 4;
}]>;

// The VCVT to/from fixed-point instructions encode the 'fbits' operand
// (the number of fixed bits) differently than it appears in the assembly
// source. It's encoded as "Size - fbits" where Size is the size of the
// fixed-point representation (32 or 16) and fbits is the value appearing
// in the assembly source, an integer in [0,16] or (0,32], depending on size.
def fbits32_asm_operand : AsmOperandClass { let Name = "FBits32"; }
def fbits32 : Operand<i32> {
  let PrintMethod = "printFBits32";
  let ParserMatchClass = fbits32_asm_operand;
}

def fbits16_asm_operand : AsmOperandClass { let Name = "FBits16"; }
def fbits16 : Operand<i32> {
  let PrintMethod = "printFBits16";
  let ParserMatchClass = fbits16_asm_operand;
}

//===----------------------------------------------------------------------===//
//  Load / store Instructions.
//

let canFoldAsLoad = 1, isReMaterializable = 1 in {

def VLDRD : ADI5<0b1101, 0b01, (outs DPR:$Dd), (ins addrmode5:$addr),
                 IIC_fpLoad64, "vldr", "\t$Dd, $addr",
                 [(set DPR:$Dd, (f64 (alignedload32 addrmode5:$addr)))]>,
            Requires<[HasFPRegs]>;

def VLDRS : ASI5<0b1101, 0b01, (outs SPR:$Sd), (ins addrmode5:$addr),
                 IIC_fpLoad32, "vldr", "\t$Sd, $addr",
                 [(set SPR:$Sd, (alignedload32 addrmode5:$addr))]>,
            Requires<[HasFPRegs]> {
  // Some single precision VFP instructions may be executed on both NEON and VFP
  // pipelines.
  let D = VFPNeonDomain;
}

let isUnpredicable = 1 in
def VLDRH : AHI5<0b1101, 0b01, (outs HPR:$Sd), (ins addrmode5fp16:$addr),
                 IIC_fpLoad16, "vldr", ".16\t$Sd, $addr",
                 [(set HPR:$Sd, (f16 (alignedload16 addrmode5fp16:$addr)))]>,
            Requires<[HasFPRegs16]>;

} // End of 'let canFoldAsLoad = 1, isReMaterializable = 1 in'

def : Pat<(bf16 (alignedload16 addrmode5fp16:$addr)),
          (VLDRH addrmode5fp16:$addr)> {
  let Predicates = [HasFPRegs16];
}
def : Pat<(bf16 (alignedload16 addrmode3:$addr)),
          (COPY_TO_REGCLASS (LDRH addrmode3:$addr), HPR)> {
  let Predicates = [HasNoFPRegs16, IsARM];
}
def : Pat<(bf16 (alignedload16 t2addrmode_imm12:$addr)),
          (COPY_TO_REGCLASS (t2LDRHi12 t2addrmode_imm12:$addr), HPR)> {
  let Predicates = [HasNoFPRegs16, IsThumb];
}

def VSTRD : ADI5<0b1101, 0b00, (outs), (ins DPR:$Dd, addrmode5:$addr),
                 IIC_fpStore64, "vstr", "\t$Dd, $addr",
                 [(alignedstore32 (f64 DPR:$Dd), addrmode5:$addr)]>,
            Requires<[HasFPRegs]>;

def VSTRS : ASI5<0b1101, 0b00, (outs), (ins SPR:$Sd, addrmode5:$addr),
                 IIC_fpStore32, "vstr", "\t$Sd, $addr",
                 [(alignedstore32 SPR:$Sd, addrmode5:$addr)]>,
            Requires<[HasFPRegs]> {
  // Some single precision VFP instructions may be executed on both NEON and VFP
  // pipelines.
  let D = VFPNeonDomain;
}

let isUnpredicable = 1 in
def VSTRH : AHI5<0b1101, 0b00, (outs), (ins HPR:$Sd, addrmode5fp16:$addr),
                 IIC_fpStore16, "vstr", ".16\t$Sd, $addr",
                 [(alignedstore16 (f16 HPR:$Sd), addrmode5fp16:$addr)]>,
            Requires<[HasFPRegs16]>;

def : Pat<(alignedstore16 (bf16 HPR:$Sd), addrmode5fp16:$addr),
          (VSTRH (bf16 HPR:$Sd), addrmode5fp16:$addr)> {
  let Predicates = [HasFPRegs16];
}
def : Pat<(alignedstore16 (bf16 HPR:$Sd), addrmode3:$addr),
          (STRH (COPY_TO_REGCLASS $Sd, GPR), addrmode3:$addr)> {
  let Predicates = [HasNoFPRegs16, IsARM];
}
def : Pat<(alignedstore16 (bf16 HPR:$Sd), t2addrmode_imm12:$addr),
          (t2STRHi12 (COPY_TO_REGCLASS $Sd, GPR), t2addrmode_imm12:$addr)> {
  let Predicates = [HasNoFPRegs16, IsThumb];
}

//===----------------------------------------------------------------------===//
//  Load / store multiple Instructions.
//

multiclass vfp_ldst_mult<string asm, bit L_bit,
                         InstrItinClass itin, InstrItinClass itin_upd> {
  let Predicates = [HasFPRegs] in {
  // Double Precision
  def DIA :
    AXDI4<(outs), (ins GPR:$Rn, pred:$p, dpr_reglist:$regs, variable_ops),
          IndexModeNone, itin,
          !strconcat(asm, "ia${p}\t$Rn, $regs"), "", []> {
    let Inst{24-23} = 0b01;       // Increment After
    let Inst{21}    = 0;          // No writeback
    let Inst{20}    = L_bit;
  }
  def DIA_UPD :
    AXDI4<(outs GPR:$wb), (ins GPR:$Rn, pred:$p, dpr_reglist:$regs,
                               variable_ops),
          IndexModeUpd, itin_upd,
          !strconcat(asm, "ia${p}\t$Rn!, $regs"), "$Rn = $wb", []> {
    let Inst{24-23} = 0b01;       // Increment After
    let Inst{21}    = 1;          // Writeback
    let Inst{20}    = L_bit;
  }
  def DDB_UPD :
    AXDI4<(outs GPR:$wb), (ins GPR:$Rn, pred:$p, dpr_reglist:$regs,
                               variable_ops),
          IndexModeUpd, itin_upd,
          !strconcat(asm, "db${p}\t$Rn!, $regs"), "$Rn = $wb", []> {
    let Inst{24-23} = 0b10;       // Decrement Before
    let Inst{21}    = 1;          // Writeback
    let Inst{20}    = L_bit;
  }

  // Single Precision
  def SIA :
    AXSI4<(outs), (ins GPR:$Rn, pred:$p, spr_reglist:$regs, variable_ops),
          IndexModeNone, itin,
          !strconcat(asm, "ia${p}\t$Rn, $regs"), "", []> {
    let Inst{24-23} = 0b01;       // Increment After
    let Inst{21}    = 0;          // No writeback
    let Inst{20}    = L_bit;

    // Some single precision VFP instructions may be executed on both NEON and
    // VFP pipelines.
    let D = VFPNeonDomain;
  }
  def SIA_UPD :
    AXSI4<(outs GPR:$wb), (ins GPR:$Rn, pred:$p, spr_reglist:$regs,
                               variable_ops),
          IndexModeUpd, itin_upd,
          !strconcat(asm, "ia${p}\t$Rn!, $regs"), "$Rn = $wb", []> {
    let Inst{24-23} = 0b01;       // Increment After
    let Inst{21}    = 1;          // Writeback
    let Inst{20}    = L_bit;

    // Some single precision VFP instructions may be executed on both NEON and
    // VFP pipelines.
    let D = VFPNeonDomain;
  }
  def SDB_UPD :
    AXSI4<(outs GPR:$wb), (ins GPR:$Rn, pred:$p, spr_reglist:$regs,
                               variable_ops),
          IndexModeUpd, itin_upd,
          !strconcat(asm, "db${p}\t$Rn!, $regs"), "$Rn = $wb", []> {
    let Inst{24-23} = 0b10;       // Decrement Before
    let Inst{21}    = 1;          // Writeback
    let Inst{20}    = L_bit;

    // Some single precision VFP instructions may be executed on both NEON and
    // VFP pipelines.
    let D = VFPNeonDomain;
  }
  }
}

let hasSideEffects = 0 in {

let mayLoad = 1, hasExtraDefRegAllocReq = 1 in
defm VLDM : vfp_ldst_mult<"vldm", 1, IIC_fpLoad_m, IIC_fpLoad_mu>;

let mayStore = 1, hasExtraSrcRegAllocReq = 1 in
defm VSTM : vfp_ldst_mult<"vstm", 0, IIC_fpStore_m, IIC_fpStore_mu>;

} // hasSideEffects

def : MnemonicAlias<"vldm", "vldmia">;
def : MnemonicAlias<"vstm", "vstmia">;


//===----------------------------------------------------------------------===//
//  Lazy load / store multiple Instructions
//
// VLLDM and VLSTM:
// 2 encoding options:
// T1 (bit 7 is 0):
// T1 takes an optional dpr_reglist, must be '{d0-d15}' (exactly)
// T1 require v8-M.Main, secure state, target with 16 D registers (or with no D registers - NOP)
// T2 (bit 7 is 1):
// T2 takes a mandatory dpr_reglist, must be '{d0-d31}' (exactly)
// T2 require v8.1-M.Main, secure state, target with 16/32 D registers (or with no D registers - NOP)
// (source: Arm v8-M ARM, DDI0553B.v ID16122022)

def VLLDM : AXSI4FR<"vlldm${p}\t$Rn, $regs", 0, 1>,
            Requires<[HasV8MMainline, Has8MSecExt]> {
    let Defs = [VPR, FPSCR, FPSCR_NZCV, D0, D1, D2, D3, D4, D5, D6, D7, D8, D9, D10, D11, D12, D13, D14, D15];
    let DecoderMethod = "DecodeLazyLoadStoreMul";
}
// T1: assembly does not contains the register list.
def : InstAlias<"vlldm${p}\t$Rn", (VLLDM GPRnopc:$Rn, pred:$p, 0)>,
                Requires<[HasV8MMainline, Has8MSecExt]>;
// T2: assembly must contains the register list.
// The register list has no effect on the encoding, it is for assembly/disassembly purposes only.
def VLLDM_T2 : AXSI4FR<"vlldm${p}\t$Rn, $regs", 1, 1>,
            Requires<[HasV8_1MMainline, Has8MSecExt]> {
    let Defs = [VPR, FPSCR, FPSCR_NZCV, D0,  D1,  D2,  D3,  D4,  D5,  D6,  D7,  D8,  D9,  D10, D11, D12, D13, D14, D15,
                                        D16, D17, D18, D19, D20, D21, D22, D23, D24, D25, D26, D27, D28, D29, D30, D31];
    let DecoderMethod = "DecodeLazyLoadStoreMul";
}
// T1: assembly contains the register list.
// The register list has no effect on the encoding, it is for assembly/disassembly purposes only.
def VLSTM : AXSI4FR<"vlstm${p}\t$Rn, $regs", 0, 0>,
            Requires<[HasV8MMainline, Has8MSecExt]> {
    let Defs = [VPR, FPSCR, FPSCR_NZCV];
    let Uses = [VPR, FPSCR, FPSCR_NZCV, D0, D1, D2, D3, D4, D5, D6, D7, D8, D9, D10, D11, D12, D13, D14, D15];
    let DecoderMethod = "DecodeLazyLoadStoreMul";
}
// T1: assembly does not contain the register list.
def : InstAlias<"vlstm${p}\t$Rn", (VLSTM GPRnopc:$Rn, pred:$p, 0)>,
                Requires<[HasV8MMainline, Has8MSecExt]>;
// T2: assembly must contain the register list.
// The register list has no effect on the encoding, it is for assembly/disassembly purposes only.
def VLSTM_T2 : AXSI4FR<"vlstm${p}\t$Rn, $regs", 1, 0>,
            Requires<[HasV8_1MMainline, Has8MSecExt]> {
    let Defs = [VPR, FPSCR, FPSCR_NZCV];
    let Uses = [VPR, FPSCR, FPSCR_NZCV, D0,  D1,  D2,  D3,  D4,  D5,  D6,  D7,  D8,  D9,  D10, D11, D12, D13, D14, D15,
                                        D16, D17, D18, D19, D20, D21, D22, D23, D24, D25, D26, D27, D28, D29, D30, D31];
    let DecoderMethod = "DecodeLazyLoadStoreMul";
}

def : InstAlias<"vpush${p} $r", (VSTMDDB_UPD SP, pred:$p, dpr_reglist:$r), 0>,
                Requires<[HasFPRegs]>;
def : InstAlias<"vpush${p} $r", (VSTMSDB_UPD SP, pred:$p, spr_reglist:$r), 0>,
                Requires<[HasFPRegs]>;
def : InstAlias<"vpop${p} $r",  (VLDMDIA_UPD SP, pred:$p, dpr_reglist:$r), 0>,
                Requires<[HasFPRegs]>;
def : InstAlias<"vpop${p} $r",  (VLDMSIA_UPD SP, pred:$p, spr_reglist:$r), 0>,
                Requires<[HasFPRegs]>;
defm : VFPDTAnyInstAlias<"vpush${p}", "$r",
                         (VSTMSDB_UPD SP, pred:$p, spr_reglist:$r)>;
defm : VFPDTAnyInstAlias<"vpush${p}", "$r",
                         (VSTMDDB_UPD SP, pred:$p, dpr_reglist:$r)>;
defm : VFPDTAnyInstAlias<"vpop${p}", "$r",
                         (VLDMSIA_UPD SP, pred:$p, spr_reglist:$r)>;
defm : VFPDTAnyInstAlias<"vpop${p}", "$r",
                         (VLDMDIA_UPD SP, pred:$p, dpr_reglist:$r)>;

// FLDMX, FSTMX - Load and store multiple unknown precision registers for
// pre-armv6 cores.
// These instruction are deprecated so we don't want them to get selected.
// However, there is no UAL syntax for them, so we keep them around for
// (dis)assembly only.
multiclass vfp_ldstx_mult<string asm, bit L_bit> {
  let Predicates = [HasFPRegs], hasNoSchedulingInfo = 1 in {
  // Unknown precision
  def XIA :
    AXXI4<(outs), (ins GPR:$Rn, pred:$p, dpr_reglist:$regs, variable_ops),
          IndexModeNone, !strconcat(asm, "iax${p}\t$Rn, $regs"), "", []> {
    let Inst{24-23} = 0b01;       // Increment After
    let Inst{21}    = 0;          // No writeback
    let Inst{20}    = L_bit;
  }
  def XIA_UPD :
    AXXI4<(outs GPR:$wb), (ins GPR:$Rn, pred:$p, dpr_reglist:$regs, variable_ops),
          IndexModeUpd, !strconcat(asm, "iax${p}\t$Rn!, $regs"), "$Rn = $wb", []> {
    let Inst{24-23} = 0b01;         // Increment After
    let Inst{21}    = 1;            // Writeback
    let Inst{20}    = L_bit;
  }
  def XDB_UPD :
    AXXI4<(outs GPR:$wb), (ins GPR:$Rn, pred:$p, dpr_reglist:$regs, variable_ops),
          IndexModeUpd, !strconcat(asm, "dbx${p}\t$Rn!, $regs"), "$Rn = $wb", []> {
    let Inst{24-23} = 0b10;         // Decrement Before
    let Inst{21}    = 1;            // Writeback
    let Inst{20}    = L_bit;
  }
  }
}

defm FLDM : vfp_ldstx_mult<"fldm", 1>;
defm FSTM : vfp_ldstx_mult<"fstm", 0>;

def : VFP2MnemonicAlias<"fldmeax", "fldmdbx">;
def : VFP2MnemonicAlias<"fldmfdx", "fldmiax">;

def : VFP2MnemonicAlias<"fstmeax", "fstmiax">;
def : VFP2MnemonicAlias<"fstmfdx", "fstmdbx">;

//===----------------------------------------------------------------------===//
// FP Binary Operations.
//

let TwoOperandAliasConstraint = "$Dn = $Dd" in
def VADDD  : ADbI<0b11100, 0b11, 0, 0,
                  (outs DPR:$Dd), (ins DPR:$Dn, DPR:$Dm),
                  IIC_fpALU64, "vadd", ".f64\t$Dd, $Dn, $Dm",
                  [(set DPR:$Dd, (fadd DPR:$Dn, (f64 DPR:$Dm)))]>,
             Sched<[WriteFPALU64]>;

let TwoOperandAliasConstraint = "$Sn = $Sd" in
def VADDS  : ASbIn<0b11100, 0b11, 0, 0,
                   (outs SPR:$Sd), (ins SPR:$Sn, SPR:$Sm),
                   IIC_fpALU32, "vadd", ".f32\t$Sd, $Sn, $Sm",
                   [(set SPR:$Sd, (fadd SPR:$Sn, SPR:$Sm))]>,
             Sched<[WriteFPALU32]> {
  // Some single precision VFP instructions may be executed on both NEON and
  // VFP pipelines on A8.
  let D = VFPNeonA8Domain;
}

let TwoOperandAliasConstraint = "$Sn = $Sd" in
def VADDH  : AHbI<0b11100, 0b11, 0, 0,
                  (outs HPR:$Sd), (ins HPR:$Sn, HPR:$Sm),
                  IIC_fpALU16, "vadd", ".f16\t$Sd, $Sn, $Sm",
                  [(set (f16 HPR:$Sd), (fadd (f16 HPR:$Sn), (f16 HPR:$Sm)))]>,
             Sched<[WriteFPALU32]>;

let TwoOperandAliasConstraint = "$Dn = $Dd" in
def VSUBD  : ADbI<0b11100, 0b11, 1, 0,
                  (outs DPR:$Dd), (ins DPR:$Dn, DPR:$Dm),
                  IIC_fpALU64, "vsub", ".f64\t$Dd, $Dn, $Dm",
                  [(set DPR:$Dd, (fsub DPR:$Dn, (f64 DPR:$Dm)))]>,
             Sched<[WriteFPALU64]>;

let TwoOperandAliasConstraint = "$Sn = $Sd" in
def VSUBS  : ASbIn<0b11100, 0b11, 1, 0,
                   (outs SPR:$Sd), (ins SPR:$Sn, SPR:$Sm),
                   IIC_fpALU32, "vsub", ".f32\t$Sd, $Sn, $Sm",
                   [(set SPR:$Sd, (fsub SPR:$Sn, SPR:$Sm))]>,
             Sched<[WriteFPALU32]>{
  // Some single precision VFP instructions may be executed on both NEON and
  // VFP pipelines on A8.
  let D = VFPNeonA8Domain;
}

let TwoOperandAliasConstraint = "$Sn = $Sd" in
def VSUBH  : AHbI<0b11100, 0b11, 1, 0,
                  (outs HPR:$Sd), (ins HPR:$Sn, HPR:$Sm),
                  IIC_fpALU16, "vsub", ".f16\t$Sd, $Sn, $Sm",
                  [(set (f16 HPR:$Sd), (fsub (f16 HPR:$Sn), (f16 HPR:$Sm)))]>,
            Sched<[WriteFPALU32]>;

let TwoOperandAliasConstraint = "$Dn = $Dd" in
def VDIVD  : ADbI<0b11101, 0b00, 0, 0,
                  (outs DPR:$Dd), (ins DPR:$Dn, DPR:$Dm),
                  IIC_fpDIV64, "vdiv", ".f64\t$Dd, $Dn, $Dm",
                  [(set DPR:$Dd, (fdiv DPR:$Dn, (f64 DPR:$Dm)))]>,
             Sched<[WriteFPDIV64]>;

let TwoOperandAliasConstraint = "$Sn = $Sd" in
def VDIVS  : ASbI<0b11101, 0b00, 0, 0,
                  (outs SPR:$Sd), (ins SPR:$Sn, SPR:$Sm),
                  IIC_fpDIV32, "vdiv", ".f32\t$Sd, $Sn, $Sm",
                  [(set SPR:$Sd, (fdiv SPR:$Sn, SPR:$Sm))]>,
             Sched<[WriteFPDIV32]>;

let TwoOperandAliasConstraint = "$Sn = $Sd" in
def VDIVH  : AHbI<0b11101, 0b00, 0, 0,
                  (outs HPR:$Sd), (ins HPR:$Sn, HPR:$Sm),
                  IIC_fpDIV16, "vdiv", ".f16\t$Sd, $Sn, $Sm",
                  [(set (f16 HPR:$Sd), (fdiv (f16 HPR:$Sn), (f16 HPR:$Sm)))]>,
             Sched<[WriteFPDIV32]>;

let TwoOperandAliasConstraint = "$Dn = $Dd" in
def VMULD  : ADbI<0b11100, 0b10, 0, 0,
                  (outs DPR:$Dd), (ins DPR:$Dn, DPR:$Dm),
                  IIC_fpMUL64, "vmul", ".f64\t$Dd, $Dn, $Dm",
                  [(set DPR:$Dd, (fmul DPR:$Dn, (f64 DPR:$Dm)))]>,
             Sched<[WriteFPMUL64, ReadFPMUL, ReadFPMUL]>;

let TwoOperandAliasConstraint = "$Sn = $Sd" in
def VMULS  : ASbIn<0b11100, 0b10, 0, 0,
                   (outs SPR:$Sd), (ins SPR:$Sn, SPR:$Sm),
                   IIC_fpMUL32, "vmul", ".f32\t$Sd, $Sn, $Sm",
                   [(set SPR:$Sd, (fmul SPR:$Sn, SPR:$Sm))]>,
            Sched<[WriteFPMUL32, ReadFPMUL, ReadFPMUL]> {
  // Some single precision VFP instructions may be executed on both NEON and
  // VFP pipelines on A8.
  let D = VFPNeonA8Domain;
}

let TwoOperandAliasConstraint = "$Sn = $Sd" in
def VMULH  : AHbI<0b11100, 0b10, 0, 0,
                  (outs HPR:$Sd), (ins HPR:$Sn, HPR:$Sm),
                  IIC_fpMUL16, "vmul", ".f16\t$Sd, $Sn, $Sm",
                  [(set (f16 HPR:$Sd), (fmul (f16 HPR:$Sn), (f16 HPR:$Sm)))]>,
             Sched<[WriteFPMUL32, ReadFPMUL, ReadFPMUL]>;

let TwoOperandAliasConstraint = "$Dn = $Dd" in
def VNMULD : ADbI<0b11100, 0b10, 1, 0,
                  (outs DPR:$Dd), (ins DPR:$Dn, DPR:$Dm),
                  IIC_fpMUL64, "vnmul", ".f64\t$Dd, $Dn, $Dm",
                  [(set DPR:$Dd, (fneg (fmul DPR:$Dn, (f64 DPR:$Dm))))]>,
             Sched<[WriteFPMUL64, ReadFPMUL, ReadFPMUL]>;

let TwoOperandAliasConstraint = "$Sn = $Sd" in
def VNMULS : ASbI<0b11100, 0b10, 1, 0,
                  (outs SPR:$Sd), (ins SPR:$Sn, SPR:$Sm),
                  IIC_fpMUL32, "vnmul", ".f32\t$Sd, $Sn, $Sm",
                  [(set SPR:$Sd, (fneg (fmul SPR:$Sn, SPR:$Sm)))]>,
            Sched<[WriteFPMUL32, ReadFPMUL, ReadFPMUL]> {
  // Some single precision VFP instructions may be executed on both NEON and
  // VFP pipelines on A8.
  let D = VFPNeonA8Domain;
}

let TwoOperandAliasConstraint = "$Sn = $Sd" in
def VNMULH : AHbI<0b11100, 0b10, 1, 0,
                  (outs HPR:$Sd), (ins HPR:$Sn, HPR:$Sm),
                  IIC_fpMUL16, "vnmul", ".f16\t$Sd, $Sn, $Sm",
                  [(set (f16 HPR:$Sd), (fneg (fmul (f16 HPR:$Sn), (f16 HPR:$Sm))))]>,
             Sched<[WriteFPMUL32, ReadFPMUL, ReadFPMUL]>;

multiclass vsel_inst<string op, bits<2> opc, int CC> {
  let DecoderNamespace = "VFPV8", PostEncoderMethod = "",
      Uses = [CPSR], AddedComplexity = 4, isUnpredicable = 1 in {
    def H : AHbInp<0b11100, opc, 0,
                   (outs HPR:$Sd), (ins HPR:$Sn, HPR:$Sm),
                   NoItinerary, !strconcat("vsel", op, ".f16\t$Sd, $Sn, $Sm"),
                   [(set (f16 HPR:$Sd), (ARMcmov (f16 HPR:$Sm), (f16 HPR:$Sn), CC))]>,
                   Requires<[HasFullFP16]>;

    def S : ASbInp<0b11100, opc, 0,
                   (outs SPR:$Sd), (ins SPR:$Sn, SPR:$Sm),
                   NoItinerary, !strconcat("vsel", op, ".f32\t$Sd, $Sn, $Sm"),
                   [(set SPR:$Sd, (ARMcmov SPR:$Sm, SPR:$Sn, CC))]>,
                   Requires<[HasFPARMv8]>;

    def D : ADbInp<0b11100, opc, 0,
                   (outs DPR:$Dd), (ins DPR:$Dn, DPR:$Dm),
                   NoItinerary, !strconcat("vsel", op, ".f64\t$Dd, $Dn, $Dm"),
                   [(set DPR:$Dd, (ARMcmov (f64 DPR:$Dm), (f64 DPR:$Dn), CC))]>,
                   Requires<[HasFPARMv8, HasDPVFP]>;
  }
}

// The CC constants here match ARMCC::CondCodes.
defm VSELGT : vsel_inst<"gt", 0b11, 12>;
defm VSELGE : vsel_inst<"ge", 0b10, 10>;
defm VSELEQ : vsel_inst<"eq", 0b00, 0>;
defm VSELVS : vsel_inst<"vs", 0b01, 6>;

multiclass vmaxmin_inst<string op, bit opc, SDNode SD> {
  let DecoderNamespace = "VFPV8", PostEncoderMethod = "",
      isUnpredicable = 1 in {
    def H : AHbInp<0b11101, 0b00, opc,
                   (outs HPR:$Sd), (ins HPR:$Sn, HPR:$Sm),
                   NoItinerary, !strconcat(op, ".f16\t$Sd, $Sn, $Sm"),
                   [(set (f16 HPR:$Sd), (SD (f16 HPR:$Sn), (f16 HPR:$Sm)))]>,
                   Requires<[HasFullFP16]>;

    def S : ASbInp<0b11101, 0b00, opc,
                   (outs SPR:$Sd), (ins SPR:$Sn, SPR:$Sm),
                   NoItinerary, !strconcat(op, ".f32\t$Sd, $Sn, $Sm"),
                   [(set SPR:$Sd, (SD SPR:$Sn, SPR:$Sm))]>,
                   Requires<[HasFPARMv8]>;

    def D : ADbInp<0b11101, 0b00, opc,
                   (outs DPR:$Dd), (ins DPR:$Dn, DPR:$Dm),
                   NoItinerary, !strconcat(op, ".f64\t$Dd, $Dn, $Dm"),
                   [(set DPR:$Dd, (f64 (SD (f64 DPR:$Dn), (f64 DPR:$Dm))))]>,
                   Requires<[HasFPARMv8, HasDPVFP]>;
  }
}

defm VFP_VMAXNM : vmaxmin_inst<"vmaxnm", 0, fmaxnum>;
defm VFP_VMINNM : vmaxmin_inst<"vminnm", 1, fminnum>;

// Match reassociated forms only if not sign dependent rounding.
def : Pat<(fmul (fneg DPR:$a), (f64 DPR:$b)),
          (VNMULD DPR:$a, DPR:$b)>,
          Requires<[NoHonorSignDependentRounding,HasDPVFP]>;
def : Pat<(fmul (fneg SPR:$a), SPR:$b),
          (VNMULS SPR:$a, SPR:$b)>, Requires<[NoHonorSignDependentRounding]>;

// These are encoded as unary instructions.
let Defs = [FPSCR_NZCV] in {
def VCMPED : ADuI<0b11101, 0b11, 0b0100, 0b11, 0,
                  (outs), (ins DPR:$Dd, DPR:$Dm),
                  IIC_fpCMP64, "vcmpe", ".f64\t$Dd, $Dm", "",
                  [(arm_cmpfpe DPR:$Dd, (f64 DPR:$Dm))]>;

def VCMPES : ASuI<0b11101, 0b11, 0b0100, 0b11, 0,
                  (outs), (ins SPR:$Sd, SPR:$Sm),
                  IIC_fpCMP32, "vcmpe", ".f32\t$Sd, $Sm", "",
                  [(arm_cmpfpe SPR:$Sd, SPR:$Sm)]> {
  // Some single precision VFP instructions may be executed on both NEON and
  // VFP pipelines on A8.
  let D = VFPNeonA8Domain;
}

def VCMPEH : AHuI<0b11101, 0b11, 0b0100, 0b11, 0,
                  (outs), (ins HPR:$Sd, HPR:$Sm),
                  IIC_fpCMP16, "vcmpe", ".f16\t$Sd, $Sm",
                  [(arm_cmpfpe (f16 HPR:$Sd), (f16 HPR:$Sm))]>;

def VCMPD  : ADuI<0b11101, 0b11, 0b0100, 0b01, 0,
                  (outs), (ins DPR:$Dd, DPR:$Dm),
                  IIC_fpCMP64, "vcmp", ".f64\t$Dd, $Dm", "",
                  [(arm_cmpfp DPR:$Dd, (f64 DPR:$Dm))]>;

def VCMPS  : ASuI<0b11101, 0b11, 0b0100, 0b01, 0,
                  (outs), (ins SPR:$Sd, SPR:$Sm),
                  IIC_fpCMP32, "vcmp", ".f32\t$Sd, $Sm", "",
                  [(arm_cmpfp SPR:$Sd, SPR:$Sm)]> {
  // Some single precision VFP instructions may be executed on both NEON and
  // VFP pipelines on A8.
  let D = VFPNeonA8Domain;
}

def VCMPH  : AHuI<0b11101, 0b11, 0b0100, 0b01, 0,
                  (outs), (ins HPR:$Sd, HPR:$Sm),
                  IIC_fpCMP16, "vcmp", ".f16\t$Sd, $Sm",
                  [(arm_cmpfp (f16 HPR:$Sd), (f16 HPR:$Sm))]>;
} // Defs = [FPSCR_NZCV]

//===----------------------------------------------------------------------===//
// FP Unary Operations.
//

def VABSD  : ADuI<0b11101, 0b11, 0b0000, 0b11, 0,
                  (outs DPR:$Dd), (ins DPR:$Dm),
                  IIC_fpUNA64, "vabs", ".f64\t$Dd, $Dm", "",
                  [(set DPR:$Dd, (fabs (f64 DPR:$Dm)))]>;

def VABSS  : ASuIn<0b11101, 0b11, 0b0000, 0b11, 0,
                   (outs SPR:$Sd), (ins SPR:$Sm),
                   IIC_fpUNA32, "vabs", ".f32\t$Sd, $Sm",
                   [(set SPR:$Sd, (fabs SPR:$Sm))]> {
  // Some single precision VFP instructions may be executed on both NEON and
  // VFP pipelines on A8.
  let D = VFPNeonA8Domain;
}

def VABSH  : AHuI<0b11101, 0b11, 0b0000, 0b11, 0,
                   (outs HPR:$Sd), (ins HPR:$Sm),
                   IIC_fpUNA16, "vabs", ".f16\t$Sd, $Sm",
                   [(set (f16 HPR:$Sd), (fabs (f16 HPR:$Sm)))]>;

let Defs = [FPSCR_NZCV] in {
def VCMPEZD : ADuI<0b11101, 0b11, 0b0101, 0b11, 0,
                   (outs), (ins DPR:$Dd),
                   IIC_fpCMP64, "vcmpe", ".f64\t$Dd, #0", "",
                   [(arm_cmpfpe0 (f64 DPR:$Dd))]> {
  let Inst{3-0} = 0b0000;
  let Inst{5}   = 0;
}

def VCMPEZS : ASuI<0b11101, 0b11, 0b0101, 0b11, 0,
                   (outs), (ins SPR:$Sd),
                   IIC_fpCMP32, "vcmpe", ".f32\t$Sd, #0", "",
                   [(arm_cmpfpe0 SPR:$Sd)]> {
  let Inst{3-0} = 0b0000;
  let Inst{5}   = 0;

  // Some single precision VFP instructions may be executed on both NEON and
  // VFP pipelines on A8.
  let D = VFPNeonA8Domain;
}

def VCMPEZH : AHuI<0b11101, 0b11, 0b0101, 0b11, 0,
                   (outs), (ins HPR:$Sd),
                   IIC_fpCMP16, "vcmpe", ".f16\t$Sd, #0",
                   [(arm_cmpfpe0 (f16 HPR:$Sd))]> {
  let Inst{3-0} = 0b0000;
  let Inst{5}   = 0;
}

def VCMPZD  : ADuI<0b11101, 0b11, 0b0101, 0b01, 0,
                   (outs), (ins DPR:$Dd),
                   IIC_fpCMP64, "vcmp", ".f64\t$Dd, #0", "",
                   [(arm_cmpfp0 (f64 DPR:$Dd))]> {
  let Inst{3-0} = 0b0000;
  let Inst{5}   = 0;
}

def VCMPZS  : ASuI<0b11101, 0b11, 0b0101, 0b01, 0,
                   (outs), (ins SPR:$Sd),
                   IIC_fpCMP32, "vcmp", ".f32\t$Sd, #0", "",
                   [(arm_cmpfp0 SPR:$Sd)]> {
  let Inst{3-0} = 0b0000;
  let Inst{5}   = 0;

  // Some single precision VFP instructions may be executed on both NEON and
  // VFP pipelines on A8.
  let D = VFPNeonA8Domain;
}

def VCMPZH  : AHuI<0b11101, 0b11, 0b0101, 0b01, 0,
                   (outs), (ins HPR:$Sd),
                   IIC_fpCMP16, "vcmp", ".f16\t$Sd, #0",
                   [(arm_cmpfp0 (f16 HPR:$Sd))]> {
  let Inst{3-0} = 0b0000;
  let Inst{5}   = 0;
}
} // Defs = [FPSCR_NZCV]

def VCVTDS  : ASuI<0b11101, 0b11, 0b0111, 0b11, 0,
                   (outs DPR:$Dd), (ins SPR:$Sm),
                   IIC_fpCVTDS, "vcvt", ".f64.f32\t$Dd, $Sm", "",
                   [(set DPR:$Dd, (fpextend SPR:$Sm))]>,
             Sched<[WriteFPCVT]> {
  // Instruction operands.
  bits<5> Dd;
  bits<5> Sm;

  // Encode instruction operands.
  let Inst{3-0}   = Sm{4-1};
  let Inst{5}     = Sm{0};
  let Inst{15-12} = Dd{3-0};
  let Inst{22}    = Dd{4};

  let Predicates = [HasVFP2, HasDPVFP];
  let hasSideEffects = 0;
}

// Special case encoding: bits 11-8 is 0b1011.
def VCVTSD  : VFPAI<(outs SPR:$Sd), (ins DPR:$Dm), VFPUnaryFrm,
                    IIC_fpCVTSD, "vcvt", ".f32.f64\t$Sd, $Dm", "",
                    [(set SPR:$Sd, (fpround DPR:$Dm))]>,
              Sched<[WriteFPCVT]> {
  // Instruction operands.
  bits<5> Sd;
  bits<5> Dm;

  // Encode instruction operands.
  let Inst{3-0}   = Dm{3-0};
  let Inst{5}     = Dm{4};
  let Inst{15-12} = Sd{4-1};
  let Inst{22}    = Sd{0};

  let Inst{27-23} = 0b11101;
  let Inst{21-16} = 0b110111;
  let Inst{11-8}  = 0b1011;
  let Inst{7-6}   = 0b11;
  let Inst{4}     = 0;

  let Predicates = [HasVFP2, HasDPVFP];
  let hasSideEffects = 0;
}

// Between half, single and double-precision.
let hasSideEffects = 0 in
def VCVTBHS: ASuI<0b11101, 0b11, 0b0010, 0b01, 0, (outs SPR:$Sd), (ins SPR:$Sm),
                 /* FIXME */ IIC_fpCVTSH, "vcvtb", ".f32.f16\t$Sd, $Sm", "",
                 [/* Intentionally left blank, see patterns below */]>,
                 Requires<[HasFP16]>,
             Sched<[WriteFPCVT]>;

def : FP16Pat<(f32 (fpextend (f16 HPR:$Sm))),
              (VCVTBHS (COPY_TO_REGCLASS (f16 HPR:$Sm), SPR))>;
def : FP16Pat<(f16_to_fp GPR:$a),
              (VCVTBHS (COPY_TO_REGCLASS GPR:$a, SPR))>;

let hasSideEffects = 0 in
def VCVTBSH: ASuI<0b11101, 0b11, 0b0011, 0b01, 0, (outs SPR:$Sd), (ins SPR:$Sda, SPR:$Sm),
                 /* FIXME */ IIC_fpCVTHS, "vcvtb", ".f16.f32\t$Sd, $Sm", "$Sd = $Sda",
                 [/* Intentionally left blank, see patterns below */]>,
                 Requires<[HasFP16]>,
             Sched<[WriteFPCVT]>;

def : FP16Pat<(f16 (fpround SPR:$Sm)),
              (COPY_TO_REGCLASS (VCVTBSH (IMPLICIT_DEF), SPR:$Sm), HPR)>;
def : FP16Pat<(fp_to_f16 SPR:$a),
              (i32 (COPY_TO_REGCLASS (VCVTBSH (IMPLICIT_DEF), SPR:$a), GPR))>;
def : FP16Pat<(insertelt (v8f16 MQPR:$src1), (f16 (fpround (f32 SPR:$src2))), imm_even:$lane),
              (v8f16 (INSERT_SUBREG (v8f16 MQPR:$src1),
                                    (VCVTBSH (EXTRACT_SUBREG (v8f16 MQPR:$src1), (SSubReg_f16_reg imm:$lane)),
                                             SPR:$src2),
                                    (SSubReg_f16_reg imm:$lane)))>;
def : FP16Pat<(insertelt (v4f16 DPR:$src1), (f16 (fpround (f32 SPR:$src2))), imm_even:$lane),
              (v4f16 (INSERT_SUBREG (v4f16 DPR:$src1),
                                    (VCVTBSH (EXTRACT_SUBREG (v4f16 DPR:$src1), (SSubReg_f16_reg imm:$lane)),
                                             SPR:$src2),
                                    (SSubReg_f16_reg imm:$lane)))>;

let hasSideEffects = 0 in
def VCVTTHS: ASuI<0b11101, 0b11, 0b0010, 0b11, 0, (outs SPR:$Sd), (ins SPR:$Sm),
                 /* FIXME */ IIC_fpCVTSH, "vcvtt", ".f32.f16\t$Sd, $Sm", "",
                 [/* Intentionally left blank, see patterns below */]>,
                 Requires<[HasFP16]>,
             Sched<[WriteFPCVT]>;

def : FP16Pat<(f32 (fpextend (extractelt (v8f16 MQPR:$src), imm_odd:$lane))),
              (VCVTTHS (EXTRACT_SUBREG MQPR:$src, (SSubReg_f16_reg imm_odd:$lane)))>;
def : FP16Pat<(f32 (fpextend (extractelt (v4f16 DPR:$src), imm_odd:$lane))),
              (VCVTTHS (EXTRACT_SUBREG
                (v2f32 (COPY_TO_REGCLASS (v4f16 DPR:$src), DPR_VFP2)),
                (SSubReg_f16_reg imm_odd:$lane)))>;

let hasSideEffects = 0 in
def VCVTTSH: ASuI<0b11101, 0b11, 0b0011, 0b11, 0, (outs SPR:$Sd), (ins SPR:$Sda, SPR:$Sm),
                 /* FIXME */ IIC_fpCVTHS, "vcvtt", ".f16.f32\t$Sd, $Sm", "$Sd = $Sda",
                 [/* Intentionally left blank, see patterns below */]>,
                 Requires<[HasFP16]>,
            Sched<[WriteFPCVT]>;

def : FP16Pat<(insertelt (v8f16 MQPR:$src1), (f16 (fpround (f32 SPR:$src2))), imm_odd:$lane),
              (v8f16 (INSERT_SUBREG (v8f16 MQPR:$src1),
                                    (VCVTTSH (EXTRACT_SUBREG (v8f16 MQPR:$src1), (SSubReg_f16_reg imm:$lane)),
                                             SPR:$src2),
                                    (SSubReg_f16_reg imm:$lane)))>;
def : FP16Pat<(insertelt (v4f16 DPR:$src1), (f16 (fpround (f32 SPR:$src2))), imm_odd:$lane),
              (v4f16 (INSERT_SUBREG (v4f16 DPR:$src1),
                                    (VCVTTSH (EXTRACT_SUBREG (v4f16 DPR:$src1), (SSubReg_f16_reg imm:$lane)),
                                             SPR:$src2),
                                    (SSubReg_f16_reg imm:$lane)))>;

def VCVTBHD : ADuI<0b11101, 0b11, 0b0010, 0b01, 0,
                   (outs DPR:$Dd), (ins SPR:$Sm),
                   NoItinerary, "vcvtb", ".f64.f16\t$Dd, $Sm", "",
                   [/* Intentionally left blank, see patterns below */]>,
                   Requires<[HasFPARMv8, HasDPVFP]>,
              Sched<[WriteFPCVT]> {
  // Instruction operands.
  bits<5> Sm;

  // Encode instruction operands.
  let Inst{3-0} = Sm{4-1};
  let Inst{5}   = Sm{0};

  let hasSideEffects = 0;
}

def : FullFP16Pat<(f64 (fpextend (f16 HPR:$Sm))),
                  (VCVTBHD (COPY_TO_REGCLASS (f16 HPR:$Sm), SPR))>,
                  Requires<[HasFPARMv8, HasDPVFP]>;
def : FP16Pat<(f64 (f16_to_fp GPR:$a)),
              (VCVTBHD (COPY_TO_REGCLASS GPR:$a, SPR))>,
              Requires<[HasFPARMv8, HasDPVFP]>;

def VCVTBDH : ADuI<0b11101, 0b11, 0b0011, 0b01, 0,
                   (outs SPR:$Sd), (ins SPR:$Sda, DPR:$Dm),
                   NoItinerary, "vcvtb", ".f16.f64\t$Sd, $Dm", "$Sd = $Sda",
                   [/* Intentionally left blank, see patterns below */]>,
                   Requires<[HasFPARMv8, HasDPVFP]> {
  // Instruction operands.
  bits<5> Sd;
  bits<5> Dm;

  // Encode instruction operands.
  let Inst{3-0}     = Dm{3-0};
  let Inst{5}       = Dm{4};
  let Inst{15-12}   = Sd{4-1};
  let Inst{22}      = Sd{0};

  let hasSideEffects = 0;
}

def : FullFP16Pat<(f16 (fpround DPR:$Dm)),
                  (COPY_TO_REGCLASS (VCVTBDH (IMPLICIT_DEF), DPR:$Dm), HPR)>,
                  Requires<[HasFPARMv8, HasDPVFP]>;
def : FP16Pat<(fp_to_f16 (f64 DPR:$a)),
              (i32 (COPY_TO_REGCLASS (VCVTBDH (IMPLICIT_DEF), DPR:$a), GPR))>,
                   Requires<[HasFPARMv8, HasDPVFP]>;

def VCVTTHD : ADuI<0b11101, 0b11, 0b0010, 0b11, 0,
                   (outs DPR:$Dd), (ins SPR:$Sm),
                   NoItinerary, "vcvtt", ".f64.f16\t$Dd, $Sm", "",
                   []>, Requires<[HasFPARMv8, HasDPVFP]> {
  // Instruction operands.
  bits<5> Sm;

  // Encode instruction operands.
  let Inst{3-0} = Sm{4-1};
  let Inst{5}   = Sm{0};

  let hasSideEffects = 0;
}

def VCVTTDH : ADuI<0b11101, 0b11, 0b0011, 0b11, 0,
                   (outs SPR:$Sd), (ins SPR:$Sda, DPR:$Dm),
                   NoItinerary, "vcvtt", ".f16.f64\t$Sd, $Dm", "$Sd = $Sda",
                   []>, Requires<[HasFPARMv8, HasDPVFP]> {
  // Instruction operands.
  bits<5> Sd;
  bits<5> Dm;

  // Encode instruction operands.
  let Inst{15-12} = Sd{4-1};
  let Inst{22}    = Sd{0};
  let Inst{3-0}   = Dm{3-0};
  let Inst{5}     = Dm{4};

  let hasSideEffects = 0;
}

multiclass vcvt_inst<string opc, bits<2> rm,
                     SDPatternOperator node = null_frag> {
  let PostEncoderMethod = "", DecoderNamespace = "VFPV8", hasSideEffects = 0 in {
    def SH : AHuInp<0b11101, 0b11, 0b1100, 0b11, 0,
                    (outs SPR:$Sd), (ins HPR:$Sm),
                    NoItinerary, !strconcat("vcvt", opc, ".s32.f16\t$Sd, $Sm"),
                    []>,
                    Requires<[HasFullFP16]> {
      let Inst{17-16} = rm;
    }

    def UH : AHuInp<0b11101, 0b11, 0b1100, 0b01, 0,
                    (outs SPR:$Sd), (ins HPR:$Sm),
                    NoItinerary, !strconcat("vcvt", opc, ".u32.f16\t$Sd, $Sm"),
                    []>,
                    Requires<[HasFullFP16]> {
      let Inst{17-16} = rm;
    }

    def SS : ASuInp<0b11101, 0b11, 0b1100, 0b11, 0,
                    (outs SPR:$Sd), (ins SPR:$Sm),
                    NoItinerary, !strconcat("vcvt", opc, ".s32.f32\t$Sd, $Sm"),
                    []>,
                    Requires<[HasFPARMv8]> {
      let Inst{17-16} = rm;
    }

    def US : ASuInp<0b11101, 0b11, 0b1100, 0b01, 0,
                    (outs SPR:$Sd), (ins SPR:$Sm),
                    NoItinerary, !strconcat("vcvt", opc, ".u32.f32\t$Sd, $Sm"),
                    []>,
                    Requires<[HasFPARMv8]> {
      let Inst{17-16} = rm;
    }

    def SD : ASuInp<0b11101, 0b11, 0b1100, 0b11, 0,
                    (outs SPR:$Sd), (ins DPR:$Dm),
                    NoItinerary, !strconcat("vcvt", opc, ".s32.f64\t$Sd, $Dm"),
                    []>,
                    Requires<[HasFPARMv8, HasDPVFP]> {
      bits<5> Dm;

      let Inst{17-16} = rm;

      // Encode instruction operands.
      let Inst{3-0} = Dm{3-0};
      let Inst{5}   = Dm{4};
      let Inst{8} = 1;
    }

    def UD : ASuInp<0b11101, 0b11, 0b1100, 0b01, 0,
                    (outs SPR:$Sd), (ins DPR:$Dm),
                    NoItinerary, !strconcat("vcvt", opc, ".u32.f64\t$Sd, $Dm"),
                    []>,
                    Requires<[HasFPARMv8, HasDPVFP]> {
      bits<5> Dm;

      let Inst{17-16} = rm;

      // Encode instruction operands
      let Inst{3-0}  = Dm{3-0};
      let Inst{5}    = Dm{4};
      let Inst{8} = 1;
    }
  }

  let Predicates = [HasFPARMv8] in {
    let Predicates = [HasFullFP16] in {
    def : Pat<(i32 (fp_to_sint (node (f16 HPR:$a)))),
              (COPY_TO_REGCLASS
                (!cast<Instruction>(NAME#"SH") (f16 HPR:$a)),
                GPR)>;

    def : Pat<(i32 (fp_to_uint (node (f16 HPR:$a)))),
              (COPY_TO_REGCLASS
                (!cast<Instruction>(NAME#"UH") (f16 HPR:$a)),
                GPR)>;
    }
    def : Pat<(i32 (fp_to_sint (node SPR:$a))),
              (COPY_TO_REGCLASS
                (!cast<Instruction>(NAME#"SS") SPR:$a),
                GPR)>;
    def : Pat<(i32 (fp_to_uint (node SPR:$a))),
              (COPY_TO_REGCLASS
                (!cast<Instruction>(NAME#"US") SPR:$a),
                GPR)>;
  }
  let Predicates = [HasFPARMv8, HasDPVFP] in {
    def : Pat<(i32 (fp_to_sint (node (f64 DPR:$a)))),
              (COPY_TO_REGCLASS
                (!cast<Instruction>(NAME#"SD") DPR:$a),
                GPR)>;
    def : Pat<(i32 (fp_to_uint (node (f64 DPR:$a)))),
              (COPY_TO_REGCLASS
                (!cast<Instruction>(NAME#"UD") DPR:$a),
                GPR)>;
  }
}

defm VCVTA : vcvt_inst<"a", 0b00, fround>;
defm VCVTN : vcvt_inst<"n", 0b01>;
defm VCVTP : vcvt_inst<"p", 0b10, fceil>;
defm VCVTM : vcvt_inst<"m", 0b11, ffloor>;

def VNEGD  : ADuI<0b11101, 0b11, 0b0001, 0b01, 0,
                  (outs DPR:$Dd), (ins DPR:$Dm),
                  IIC_fpUNA64, "vneg", ".f64\t$Dd, $Dm", "",
                  [(set DPR:$Dd, (fneg (f64 DPR:$Dm)))]>;

def VNEGS  : ASuIn<0b11101, 0b11, 0b0001, 0b01, 0,
                   (outs SPR:$Sd), (ins SPR:$Sm),
                   IIC_fpUNA32, "vneg", ".f32\t$Sd, $Sm",
                   [(set SPR:$Sd, (fneg SPR:$Sm))]> {
  // Some single precision VFP instructions may be executed on both NEON and
  // VFP pipelines on A8.
  let D = VFPNeonA8Domain;
}

def VNEGH  : AHuI<0b11101, 0b11, 0b0001, 0b01, 0,
                  (outs HPR:$Sd), (ins HPR:$Sm),
                  IIC_fpUNA16, "vneg", ".f16\t$Sd, $Sm",
                  [(set (f16 HPR:$Sd), (fneg (f16 HPR:$Sm)))]>;

multiclass vrint_inst_zrx<string opc, bit op, bit op2, SDPatternOperator node> {
  def H : AHuI<0b11101, 0b11, 0b0110, 0b11, 0,
               (outs HPR:$Sd), (ins HPR:$Sm),
               NoItinerary, !strconcat("vrint", opc), ".f16\t$Sd, $Sm",
               [(set (f16 HPR:$Sd), (node (f16 HPR:$Sm)))]>,
               Requires<[HasFullFP16]> {
    let Inst{7} = op2;
    let Inst{16} = op;
  }

  def S : ASuI<0b11101, 0b11, 0b0110, 0b11, 0,
               (outs SPR:$Sd), (ins SPR:$Sm),
               NoItinerary, !strconcat("vrint", opc), ".f32\t$Sd, $Sm", "",
               [(set (f32 SPR:$Sd), (node (f32 SPR:$Sm)))]>,
               Requires<[HasFPARMv8]> {
    let Inst{7} = op2;
    let Inst{16} = op;
  }
  def D : ADuI<0b11101, 0b11, 0b0110, 0b11, 0,
                (outs DPR:$Dd), (ins DPR:$Dm),
                NoItinerary, !strconcat("vrint", opc), ".f64\t$Dd, $Dm", "",
                [(set (f64 DPR:$Dd), (node (f64 DPR:$Dm)))]>,
                Requires<[HasFPARMv8, HasDPVFP]> {
    let Inst{7} = op2;
    let Inst{16} = op;
  }

  def : InstAlias<!strconcat("vrint", opc, "$p.f16.f16\t$Sd, $Sm"),
                  (!cast<Instruction>(NAME#"H") SPR:$Sd, SPR:$Sm, pred:$p), 0>,
        Requires<[HasFullFP16]>;
  def : InstAlias<!strconcat("vrint", opc, "$p.f32.f32\t$Sd, $Sm"),
                  (!cast<Instruction>(NAME#"S") SPR:$Sd, SPR:$Sm, pred:$p), 0>,
        Requires<[HasFPARMv8]>;
  def : InstAlias<!strconcat("vrint", opc, "$p.f64.f64\t$Dd, $Dm"),
                  (!cast<Instruction>(NAME#"D") DPR:$Dd, DPR:$Dm, pred:$p), 0>,
        Requires<[HasFPARMv8,HasDPVFP]>;
}

defm VRINTZ : vrint_inst_zrx<"z", 0, 1, ftrunc>;
defm VRINTR : vrint_inst_zrx<"r", 0, 0, fnearbyint>;
defm VRINTX : vrint_inst_zrx<"x", 1, 0, frint>;

multiclass vrint_inst_anpm<string opc, bits<2> rm,
                           SDPatternOperator node = null_frag> {
  let PostEncoderMethod = "", DecoderNamespace = "VFPV8",
      isUnpredicable = 1 in {
    def H : AHuInp<0b11101, 0b11, 0b1000, 0b01, 0,
                   (outs HPR:$Sd), (ins HPR:$Sm),
                   NoItinerary, !strconcat("vrint", opc, ".f16\t$Sd, $Sm"),
                   [(set (f16 HPR:$Sd), (node (f16 HPR:$Sm)))]>,
                   Requires<[HasFullFP16]> {
      let Inst{17-16} = rm;
    }
    def S : ASuInp<0b11101, 0b11, 0b1000, 0b01, 0,
                   (outs SPR:$Sd), (ins SPR:$Sm),
                   NoItinerary, !strconcat("vrint", opc, ".f32\t$Sd, $Sm"),
                   [(set (f32 SPR:$Sd), (node (f32 SPR:$Sm)))]>,
                   Requires<[HasFPARMv8]> {
      let Inst{17-16} = rm;
    }
    def D : ADuInp<0b11101, 0b11, 0b1000, 0b01, 0,
                   (outs DPR:$Dd), (ins DPR:$Dm),
                   NoItinerary, !strconcat("vrint", opc, ".f64\t$Dd, $Dm"),
                   [(set (f64 DPR:$Dd), (node (f64 DPR:$Dm)))]>,
                   Requires<[HasFPARMv8, HasDPVFP]> {
      let Inst{17-16} = rm;
    }
  }

  def : InstAlias<!strconcat("vrint", opc, ".f16.f16\t$Sd, $Sm"),
                  (!cast<Instruction>(NAME#"H") HPR:$Sd, HPR:$Sm), 0>,
        Requires<[HasFullFP16]>;
  def : InstAlias<!strconcat("vrint", opc, ".f32.f32\t$Sd, $Sm"),
                  (!cast<Instruction>(NAME#"S") SPR:$Sd, SPR:$Sm), 0>,
        Requires<[HasFPARMv8]>;
  def : InstAlias<!strconcat("vrint", opc, ".f64.f64\t$Dd, $Dm"),
                  (!cast<Instruction>(NAME#"D") DPR:$Dd, DPR:$Dm), 0>,
        Requires<[HasFPARMv8,HasDPVFP]>;
}

defm VRINTA : vrint_inst_anpm<"a", 0b00, fround>;
defm VRINTN : vrint_inst_anpm<"n", 0b01, int_arm_neon_vrintn>;
defm VRINTP : vrint_inst_anpm<"p", 0b10, fceil>;
defm VRINTM : vrint_inst_anpm<"m", 0b11, ffloor>;

def VSQRTD : ADuI<0b11101, 0b11, 0b0001, 0b11, 0,
                  (outs DPR:$Dd), (ins DPR:$Dm),
                  IIC_fpSQRT64, "vsqrt", ".f64\t$Dd, $Dm", "",
                  [(set DPR:$Dd, (fsqrt (f64 DPR:$Dm)))]>,
             Sched<[WriteFPSQRT64]>;

def VSQRTS : ASuI<0b11101, 0b11, 0b0001, 0b11, 0,
                  (outs SPR:$Sd), (ins SPR:$Sm),
                  IIC_fpSQRT32, "vsqrt", ".f32\t$Sd, $Sm", "",
                  [(set SPR:$Sd, (fsqrt SPR:$Sm))]>,
             Sched<[WriteFPSQRT32]>;

def VSQRTH : AHuI<0b11101, 0b11, 0b0001, 0b11, 0,
                  (outs HPR:$Sd), (ins HPR:$Sm),
                  IIC_fpSQRT16, "vsqrt", ".f16\t$Sd, $Sm",
                  [(set (f16 HPR:$Sd), (fsqrt (f16 HPR:$Sm)))]>;

let hasSideEffects = 0 in {
let isMoveReg = 1 in {
def VMOVD  : ADuI<0b11101, 0b11, 0b0000, 0b01, 0,
                  (outs DPR:$Dd), (ins DPR:$Dm),
                  IIC_fpUNA64, "vmov", ".f64\t$Dd, $Dm", "", []>,
             Requires<[HasFPRegs64]>;

def VMOVS  : ASuI<0b11101, 0b11, 0b0000, 0b01, 0,
                  (outs SPR:$Sd), (ins SPR:$Sm),
                  IIC_fpUNA32, "vmov", ".f32\t$Sd, $Sm", "", []>,
             Requires<[HasFPRegs]>;
} // isMoveReg

let PostEncoderMethod = "", DecoderNamespace = "VFPV8", isUnpredicable = 1 in {
def VMOVH  : ASuInp<0b11101, 0b11, 0b0000, 0b01, 0,
                  (outs SPR:$Sd), (ins SPR:$Sm),
                  IIC_fpUNA16, "vmovx.f16\t$Sd, $Sm", []>,
             Requires<[HasFullFP16]>;

def VINSH  : ASuInp<0b11101, 0b11, 0b0000, 0b11, 0,
                  (outs SPR:$Sd), (ins SPR:$Sda, SPR:$Sm),
                  IIC_fpUNA16, "vins.f16\t$Sd, $Sm", []>,
             Requires<[HasFullFP16]> {
  let Constraints = "$Sd = $Sda";
}

} // PostEncoderMethod
} // hasSideEffects

//===----------------------------------------------------------------------===//
// FP <-> GPR Copies.  Int <-> FP Conversions.
//

let isMoveReg = 1 in {
def VMOVRS : AVConv2I<0b11100001, 0b1010,
                      (outs GPR:$Rt), (ins SPR:$Sn),
                      IIC_fpMOVSI, "vmov", "\t$Rt, $Sn",
                      [(set GPR:$Rt, (bitconvert SPR:$Sn))]>,
             Requires<[HasFPRegs]>,
             Sched<[WriteFPMOV]> {
  // Instruction operands.
  bits<4> Rt;
  bits<5> Sn;

  // Encode instruction operands.
  let Inst{19-16} = Sn{4-1};
  let Inst{7}     = Sn{0};
  let Inst{15-12} = Rt;

  let Inst{6-5}   = 0b00;
  let Inst{3-0}   = 0b0000;

  // Some single precision VFP instructions may be executed on both NEON and VFP
  // pipelines.
  let D = VFPNeonDomain;
}

// Bitcast i32 -> f32.  NEON prefers to use VMOVDRR.
def VMOVSR : AVConv4I<0b11100000, 0b1010,
                      (outs SPR:$Sn), (ins GPR:$Rt),
                      IIC_fpMOVIS, "vmov", "\t$Sn, $Rt",
                      [(set SPR:$Sn, (bitconvert GPR:$Rt))]>,
             Requires<[HasFPRegs, UseVMOVSR]>,
             Sched<[WriteFPMOV]> {
  // Instruction operands.
  bits<5> Sn;
  bits<4> Rt;

  // Encode instruction operands.
  let Inst{19-16} = Sn{4-1};
  let Inst{7}     = Sn{0};
  let Inst{15-12} = Rt;

  let Inst{6-5}   = 0b00;
  let Inst{3-0}   = 0b0000;

  // Some single precision VFP instructions may be executed on both NEON and VFP
  // pipelines.
  let D = VFPNeonDomain;
}
} // isMoveReg
def : Pat<(arm_vmovsr GPR:$Rt), (VMOVSR GPR:$Rt)>, Requires<[HasFPRegs, UseVMOVSR]>;

let hasSideEffects = 0 in {
def VMOVRRD  : AVConv3I<0b11000101, 0b1011,
                        (outs GPR:$Rt, GPR:$Rt2), (ins DPR:$Dm),
                        IIC_fpMOVDI, "vmov", "\t$Rt, $Rt2, $Dm",
                 [(set GPR:$Rt, GPR:$Rt2, (arm_fmrrd DPR:$Dm))]>,
               Requires<[HasFPRegs]>,
               Sched<[WriteFPMOV]> {
  // Instruction operands.
  bits<5> Dm;
  bits<4> Rt;
  bits<4> Rt2;

  // Encode instruction operands.
  let Inst{3-0}   = Dm{3-0};
  let Inst{5}     = Dm{4};
  let Inst{15-12} = Rt;
  let Inst{19-16} = Rt2;

  let Inst{7-6} = 0b00;

  // Some single precision VFP instructions may be executed on both NEON and VFP
  // pipelines.
  let D = VFPNeonDomain;

  // This instruction is equivalent to
  // $Rt = EXTRACT_SUBREG $Dm, ssub_0
  // $Rt2 = EXTRACT_SUBREG $Dm, ssub_1
  let isExtractSubreg = 1;
}

def VMOVRRS  : AVConv3I<0b11000101, 0b1010,
                      (outs GPR:$Rt, GPR:$Rt2), (ins SPR:$src1, SPR:$src2),
                 IIC_fpMOVDI, "vmov", "\t$Rt, $Rt2, $src1, $src2",
                 [/* For disassembly only; pattern left blank */]>,
               Requires<[HasFPRegs]>,
               Sched<[WriteFPMOV]> {
  bits<5> src1;
  bits<4> Rt;
  bits<4> Rt2;

  // Encode instruction operands.
  let Inst{3-0}   = src1{4-1};
  let Inst{5}     = src1{0};
  let Inst{15-12} = Rt;
  let Inst{19-16} = Rt2;

  let Inst{7-6} = 0b00;

  // Some single precision VFP instructions may be executed on both NEON and VFP
  // pipelines.
  let D = VFPNeonDomain;
  let DecoderMethod = "DecodeVMOVRRS";
}
} // hasSideEffects

// FMDHR: GPR -> SPR
// FMDLR: GPR -> SPR

def VMOVDRR : AVConv5I<0b11000100, 0b1011,
                      (outs DPR:$Dm), (ins GPR:$Rt, GPR:$Rt2),
                      IIC_fpMOVID, "vmov", "\t$Dm, $Rt, $Rt2",
                      [(set DPR:$Dm, (arm_fmdrr GPR:$Rt, GPR:$Rt2))]>,
              Requires<[HasFPRegs]>,
              Sched<[WriteFPMOV]> {
  // Instruction operands.
  bits<5> Dm;
  bits<4> Rt;
  bits<4> Rt2;

  // Encode instruction operands.
  let Inst{3-0}   = Dm{3-0};
  let Inst{5}     = Dm{4};
  let Inst{15-12} = Rt;
  let Inst{19-16} = Rt2;

  let Inst{7-6}   = 0b00;

  // Some single precision VFP instructions may be executed on both NEON and VFP
  // pipelines.
  let D = VFPNeonDomain;

  // This instruction is equivalent to
  // $Dm = REG_SEQUENCE $Rt, ssub_0, $Rt2, ssub_1
  let isRegSequence = 1;
}

// Hoist an fabs or a fneg of a value coming from integer registers
// and do the fabs/fneg on the integer value. This is never a lose
// and could enable the conversion to float to be removed completely.
def : Pat<(fabs (arm_fmdrr GPR:$Rl, GPR:$Rh)),
          (VMOVDRR GPR:$Rl, (BFC GPR:$Rh, (i32 0x7FFFFFFF)))>,
      Requires<[IsARM, HasV6T2]>;
def : Pat<(fabs (arm_fmdrr GPR:$Rl, GPR:$Rh)),
          (VMOVDRR GPR:$Rl, (t2BFC GPR:$Rh, (i32 0x7FFFFFFF)))>,
      Requires<[IsThumb2, HasV6T2]>;
def : Pat<(fneg (arm_fmdrr GPR:$Rl, GPR:$Rh)),
          (VMOVDRR GPR:$Rl, (EORri GPR:$Rh, (i32 0x80000000)))>,
      Requires<[IsARM]>;
def : Pat<(fneg (arm_fmdrr GPR:$Rl, GPR:$Rh)),
          (VMOVDRR GPR:$Rl, (t2EORri GPR:$Rh, (i32 0x80000000)))>,
      Requires<[IsThumb2]>;

let hasSideEffects = 0 in
def VMOVSRR : AVConv5I<0b11000100, 0b1010,
                     (outs SPR:$dst1, SPR:$dst2), (ins GPR:$src1, GPR:$src2),
                IIC_fpMOVID, "vmov", "\t$dst1, $dst2, $src1, $src2",
                [/* For disassembly only; pattern left blank */]>,
              Requires<[HasFPRegs]>,
              Sched<[WriteFPMOV]> {
  // Instruction operands.
  bits<5> dst1;
  bits<4> src1;
  bits<4> src2;

  // Encode instruction operands.
  let Inst{3-0}   = dst1{4-1};
  let Inst{5}     = dst1{0};
  let Inst{15-12} = src1;
  let Inst{19-16} = src2;

  let Inst{7-6} = 0b00;

  // Some single precision VFP instructions may be executed on both NEON and VFP
  // pipelines.
  let D = VFPNeonDomain;

  let DecoderMethod = "DecodeVMOVSRR";
}

// Move H->R, clearing top 16 bits
def VMOVRH : AVConv2I<0b11100001, 0b1001,
                      (outs rGPR:$Rt), (ins HPR:$Sn),
                      IIC_fpMOVSI, "vmov", ".f16\t$Rt, $Sn",
                      []>,
             Requires<[HasFPRegs16]>,
             Sched<[WriteFPMOV]> {
  // Instruction operands.
  bits<4> Rt;
  bits<5> Sn;

  // Encode instruction operands.
  let Inst{19-16} = Sn{4-1};
  let Inst{7}     = Sn{0};
  let Inst{15-12} = Rt;

  let Inst{6-5}   = 0b00;
  let Inst{3-0}   = 0b0000;

  let isUnpredicable = 1;
}

// Move R->H, clearing top 16 bits
def VMOVHR : AVConv4I<0b11100000, 0b1001,
                      (outs HPR:$Sn), (ins rGPR:$Rt),
                      IIC_fpMOVIS, "vmov", ".f16\t$Sn, $Rt",
                      []>,
             Requires<[HasFPRegs16]>,
             Sched<[WriteFPMOV]> {
  // Instruction operands.
  bits<5> Sn;
  bits<4> Rt;

  // Encode instruction operands.
  let Inst{19-16} = Sn{4-1};
  let Inst{7}     = Sn{0};
  let Inst{15-12} = Rt;

  let Inst{6-5}   = 0b00;
  let Inst{3-0}   = 0b0000;

  let isUnpredicable = 1;
}

def : FPRegs16Pat<(arm_vmovrh (f16 HPR:$Sn)), (VMOVRH (f16 HPR:$Sn))>;
def : FPRegs16Pat<(arm_vmovrh (bf16 HPR:$Sn)), (VMOVRH (bf16 HPR:$Sn))>;
def : FPRegs16Pat<(f16 (arm_vmovhr rGPR:$Rt)), (VMOVHR rGPR:$Rt)>;
def : FPRegs16Pat<(bf16 (arm_vmovhr rGPR:$Rt)), (VMOVHR rGPR:$Rt)>;

// FMRDH: SPR -> GPR
// FMRDL: SPR -> GPR
// FMRRS: SPR -> GPR
// FMRX:  SPR system reg -> GPR
// FMSRR: GPR -> SPR
// FMXR:  GPR -> VFP system reg


// Int -> FP:

class AVConv1IDs_Encode<bits<5> opcod1, bits<2> opcod2, bits<4> opcod3,
                        bits<4> opcod4, dag oops, dag iops,
                        InstrItinClass itin, string opc, string asm,
                        list<dag> pattern>
  : AVConv1I<opcod1, opcod2, opcod3, opcod4, oops, iops, itin, opc, asm,
             pattern> {
  // Instruction operands.
  bits<5> Dd;
  bits<5> Sm;

  // Encode instruction operands.
  let Inst{3-0}   = Sm{4-1};
  let Inst{5}     = Sm{0};
  let Inst{15-12} = Dd{3-0};
  let Inst{22}    = Dd{4};

  let Predicates = [HasVFP2, HasDPVFP];
  let hasSideEffects = 0;
}

class AVConv1InSs_Encode<bits<5> opcod1, bits<2> opcod2, bits<4> opcod3,
                         bits<4> opcod4, dag oops, dag iops,InstrItinClass itin,
                         string opc, string asm, list<dag> pattern>
  : AVConv1In<opcod1, opcod2, opcod3, opcod4, oops, iops, itin, opc, asm,
              pattern> {
  // Instruction operands.
  bits<5> Sd;
  bits<5> Sm;

  // Encode instruction operands.
  let Inst{3-0}   = Sm{4-1};
  let Inst{5}     = Sm{0};
  let Inst{15-12} = Sd{4-1};
  let Inst{22}    = Sd{0};

  let hasSideEffects = 0;
}

class AVConv1IHs_Encode<bits<5> opcod1, bits<2> opcod2, bits<4> opcod3,
                        bits<4> opcod4, dag oops, dag iops,
                        InstrItinClass itin, string opc, string asm,
                        list<dag> pattern>
  : AVConv1I<opcod1, opcod2, opcod3, opcod4, oops, iops, itin, opc, asm,
             pattern> {
  // Instruction operands.
  bits<5> Sd;
  bits<5> Sm;

  // Encode instruction operands.
  let Inst{3-0}   = Sm{4-1};
  let Inst{5}     = Sm{0};
  let Inst{15-12} = Sd{4-1};
  let Inst{22}    = Sd{0};

  let Predicates = [HasFullFP16];
  let hasSideEffects = 0;
}

def VSITOD : AVConv1IDs_Encode<0b11101, 0b11, 0b1000, 0b1011,
                               (outs DPR:$Dd), (ins SPR:$Sm),
                               IIC_fpCVTID, "vcvt", ".f64.s32\t$Dd, $Sm",
                               []>,
             Sched<[WriteFPCVT]> {
  let Inst{7} = 1; // s32
}

let Predicates=[HasVFP2, HasDPVFP] in {
  def : VFPPat<(f64 (sint_to_fp GPR:$a)),
               (VSITOD (COPY_TO_REGCLASS GPR:$a, SPR))>;

  def : VFPPat<(f64 (sint_to_fp (i32 (alignedload32 addrmode5:$a)))),
               (VSITOD (VLDRS addrmode5:$a))>;
}

def VSITOS : AVConv1InSs_Encode<0b11101, 0b11, 0b1000, 0b1010,
                                (outs SPR:$Sd),(ins SPR:$Sm),
                                IIC_fpCVTIS, "vcvt", ".f32.s32\t$Sd, $Sm",
                                []>,
             Sched<[WriteFPCVT]> {
  let Inst{7} = 1; // s32

  // Some single precision VFP instructions may be executed on both NEON and
  // VFP pipelines on A8.
  let D = VFPNeonA8Domain;
}

def : VFPNoNEONPat<(f32 (sint_to_fp GPR:$a)),
                   (VSITOS (COPY_TO_REGCLASS GPR:$a, SPR))>;

def : VFPNoNEONPat<(f32 (sint_to_fp (i32 (alignedload32 addrmode5:$a)))),
                   (VSITOS (VLDRS addrmode5:$a))>;

def VSITOH : AVConv1IHs_Encode<0b11101, 0b11, 0b1000, 0b1001,
                               (outs HPR:$Sd), (ins SPR:$Sm),
                               IIC_fpCVTIH, "vcvt", ".f16.s32\t$Sd, $Sm",
                               []>,
             Sched<[WriteFPCVT]> {
  let Inst{7} = 1; // s32
  let isUnpredicable = 1;
}

def : VFPNoNEONPat<(f16 (sint_to_fp GPR:$a)),
                   (VSITOH (COPY_TO_REGCLASS GPR:$a, SPR))>;

def VUITOD : AVConv1IDs_Encode<0b11101, 0b11, 0b1000, 0b1011,
                               (outs DPR:$Dd), (ins SPR:$Sm),
                               IIC_fpCVTID, "vcvt", ".f64.u32\t$Dd, $Sm",
                               []>,
             Sched<[WriteFPCVT]> {
  let Inst{7} = 0; // u32
}

let Predicates=[HasVFP2, HasDPVFP] in {
  def : VFPPat<(f64 (uint_to_fp GPR:$a)),
               (VUITOD (COPY_TO_REGCLASS GPR:$a, SPR))>;

  def : VFPPat<(f64 (uint_to_fp (i32 (alignedload32 addrmode5:$a)))),
               (VUITOD (VLDRS addrmode5:$a))>;
}

def VUITOS : AVConv1InSs_Encode<0b11101, 0b11, 0b1000, 0b1010,
                                (outs SPR:$Sd), (ins SPR:$Sm),
                                IIC_fpCVTIS, "vcvt", ".f32.u32\t$Sd, $Sm",
                                []>,
             Sched<[WriteFPCVT]> {
  let Inst{7} = 0; // u32

  // Some single precision VFP instructions may be executed on both NEON and
  // VFP pipelines on A8.
  let D = VFPNeonA8Domain;
}

def : VFPNoNEONPat<(f32 (uint_to_fp GPR:$a)),
                   (VUITOS (COPY_TO_REGCLASS GPR:$a, SPR))>;

def : VFPNoNEONPat<(f32 (uint_to_fp (i32 (alignedload32 addrmode5:$a)))),
                   (VUITOS (VLDRS addrmode5:$a))>;

def VUITOH : AVConv1IHs_Encode<0b11101, 0b11, 0b1000, 0b1001,
                                (outs HPR:$Sd), (ins SPR:$Sm),
                                IIC_fpCVTIH, "vcvt", ".f16.u32\t$Sd, $Sm",
                                []>,
             Sched<[WriteFPCVT]> {
  let Inst{7} = 0; // u32
  let isUnpredicable = 1;
}

def : VFPNoNEONPat<(f16 (uint_to_fp GPR:$a)),
                   (VUITOH (COPY_TO_REGCLASS GPR:$a, SPR))>;

// FP -> Int:

class AVConv1IsD_Encode<bits<5> opcod1, bits<2> opcod2, bits<4> opcod3,
                        bits<4> opcod4, dag oops, dag iops,
                        InstrItinClass itin, string opc, string asm,
                        list<dag> pattern>
  : AVConv1I<opcod1, opcod2, opcod3, opcod4, oops, iops, itin, opc, asm,
             pattern> {
  // Instruction operands.
  bits<5> Sd;
  bits<5> Dm;

  // Encode instruction operands.
  let Inst{3-0}   = Dm{3-0};
  let Inst{5}     = Dm{4};
  let Inst{15-12} = Sd{4-1};
  let Inst{22}    = Sd{0};

  let Predicates = [HasVFP2, HasDPVFP];
  let hasSideEffects = 0;
}

class AVConv1InsS_Encode<bits<5> opcod1, bits<2> opcod2, bits<4> opcod3,
                         bits<4> opcod4, dag oops, dag iops,
                         InstrItinClass itin, string opc, string asm,
                         list<dag> pattern>
  : AVConv1In<opcod1, opcod2, opcod3, opcod4, oops, iops, itin, opc, asm,
              pattern> {
  // Instruction operands.
  bits<5> Sd;
  bits<5> Sm;

  // Encode instruction operands.
  let Inst{3-0}   = Sm{4-1};
  let Inst{5}     = Sm{0};
  let Inst{15-12} = Sd{4-1};
  let Inst{22}    = Sd{0};

  let hasSideEffects = 0;
}

class AVConv1IsH_Encode<bits<5> opcod1, bits<2> opcod2, bits<4> opcod3,
                         bits<4> opcod4, dag oops, dag iops,
                         InstrItinClass itin, string opc, string asm,
                         list<dag> pattern>
  : AVConv1I<opcod1, opcod2, opcod3, opcod4, oops, iops, itin, opc, asm,
              pattern> {
  // Instruction operands.
  bits<5> Sd;
  bits<5> Sm;

  // Encode instruction operands.
  let Inst{3-0}   = Sm{4-1};
  let Inst{5}     = Sm{0};
  let Inst{15-12} = Sd{4-1};
  let Inst{22}    = Sd{0};

  let Predicates = [HasFullFP16];
  let hasSideEffects = 0;
}

// Always set Z bit in the instruction, i.e. "round towards zero" variants.
def VTOSIZD : AVConv1IsD_Encode<0b11101, 0b11, 0b1101, 0b1011,
                                (outs SPR:$Sd), (ins DPR:$Dm),
                                IIC_fpCVTDI, "vcvt", ".s32.f64\t$Sd, $Dm",
                                []>,
              Sched<[WriteFPCVT]> {
  let Inst{7} = 1; // Z bit
}

let Predicates=[HasVFP2, HasDPVFP] in {
  def : VFPPat<(i32 (fp_to_sint (f64 DPR:$a))),
               (COPY_TO_REGCLASS (VTOSIZD DPR:$a), GPR)>;
  def : VFPPat<(i32 (fp_to_sint_sat (f64 DPR:$a), i32)),
               (COPY_TO_REGCLASS (VTOSIZD DPR:$a), GPR)>;

  def : VFPPat<(alignedstore32 (i32 (fp_to_sint (f64 DPR:$a))), addrmode5:$ptr),
               (VSTRS (VTOSIZD DPR:$a), addrmode5:$ptr)>;
  def : VFPPat<(alignedstore32 (i32 (fp_to_sint_sat (f64 DPR:$a), i32)), addrmode5:$ptr),
               (VSTRS (VTOSIZD DPR:$a), addrmode5:$ptr)>;
}

def VTOSIZS : AVConv1InsS_Encode<0b11101, 0b11, 0b1101, 0b1010,
                                 (outs SPR:$Sd), (ins SPR:$Sm),
                                 IIC_fpCVTSI, "vcvt", ".s32.f32\t$Sd, $Sm",
                                 []>,
              Sched<[WriteFPCVT]> {
  let Inst{7} = 1; // Z bit

  // Some single precision VFP instructions may be executed on both NEON and
  // VFP pipelines on A8.
  let D = VFPNeonA8Domain;
}

def : VFPNoNEONPat<(i32 (fp_to_sint SPR:$a)),
                   (COPY_TO_REGCLASS (VTOSIZS SPR:$a), GPR)>;
def : VFPPat<(i32 (fp_to_sint_sat SPR:$a, i32)),
             (COPY_TO_REGCLASS (VTOSIZS SPR:$a), GPR)>;

def : VFPNoNEONPat<(alignedstore32 (i32 (fp_to_sint (f32 SPR:$a))),
                                   addrmode5:$ptr),
                   (VSTRS (VTOSIZS SPR:$a), addrmode5:$ptr)>;
def : VFPPat<(alignedstore32 (i32 (fp_to_sint_sat (f32 SPR:$a), i32)),
                                   addrmode5:$ptr),
             (VSTRS (VTOSIZS SPR:$a), addrmode5:$ptr)>;

def VTOSIZH : AVConv1IsH_Encode<0b11101, 0b11, 0b1101, 0b1001,
                                 (outs SPR:$Sd), (ins HPR:$Sm),
                                 IIC_fpCVTHI, "vcvt", ".s32.f16\t$Sd, $Sm",
                                 []>,
              Sched<[WriteFPCVT]> {
  let Inst{7} = 1; // Z bit
  let isUnpredicable = 1;
}

def : VFPNoNEONPat<(i32 (fp_to_sint (f16 HPR:$a))),
                   (COPY_TO_REGCLASS (VTOSIZH (f16 HPR:$a)), GPR)>;
def : VFPPat<(i32 (fp_to_sint_sat (f16 HPR:$a), i32)),
             (COPY_TO_REGCLASS (VTOSIZH (f16 HPR:$a)), GPR)>;

def VTOUIZD : AVConv1IsD_Encode<0b11101, 0b11, 0b1100, 0b1011,
                               (outs SPR:$Sd), (ins DPR:$Dm),
                               IIC_fpCVTDI, "vcvt", ".u32.f64\t$Sd, $Dm",
                               []>,
              Sched<[WriteFPCVT]> {
  let Inst{7} = 1; // Z bit
}

let Predicates=[HasVFP2, HasDPVFP] in {
  def : VFPPat<(i32 (fp_to_uint (f64 DPR:$a))),
               (COPY_TO_REGCLASS (VTOUIZD DPR:$a), GPR)>;
  def : VFPPat<(i32 (fp_to_uint_sat (f64 DPR:$a), i32)),
               (COPY_TO_REGCLASS (VTOUIZD DPR:$a), GPR)>;

  def : VFPPat<(alignedstore32 (i32 (fp_to_uint (f64 DPR:$a))), addrmode5:$ptr),
               (VSTRS (VTOUIZD DPR:$a), addrmode5:$ptr)>;
  def : VFPPat<(alignedstore32 (i32 (fp_to_uint_sat (f64 DPR:$a), i32)), addrmode5:$ptr),
               (VSTRS (VTOUIZD DPR:$a), addrmode5:$ptr)>;
}

def VTOUIZS : AVConv1InsS_Encode<0b11101, 0b11, 0b1100, 0b1010,
                                 (outs SPR:$Sd), (ins SPR:$Sm),
                                 IIC_fpCVTSI, "vcvt", ".u32.f32\t$Sd, $Sm",
                                 []>,
              Sched<[WriteFPCVT]> {
  let Inst{7} = 1; // Z bit

  // Some single precision VFP instructions may be executed on both NEON and
  // VFP pipelines on A8.
  let D = VFPNeonA8Domain;
}

def : VFPNoNEONPat<(i32 (fp_to_uint SPR:$a)),
                   (COPY_TO_REGCLASS (VTOUIZS SPR:$a), GPR)>;
def : VFPPat<(i32 (fp_to_uint_sat SPR:$a, i32)),
             (COPY_TO_REGCLASS (VTOUIZS SPR:$a), GPR)>;

def : VFPNoNEONPat<(alignedstore32 (i32 (fp_to_uint (f32 SPR:$a))),
                                   addrmode5:$ptr),
                  (VSTRS (VTOUIZS SPR:$a), addrmode5:$ptr)>;
def : VFPPat<(alignedstore32 (i32 (fp_to_uint_sat (f32 SPR:$a), i32)),
                                   addrmode5:$ptr),
             (VSTRS (VTOUIZS SPR:$a), addrmode5:$ptr)>;

def VTOUIZH : AVConv1IsH_Encode<0b11101, 0b11, 0b1100, 0b1001,
                                 (outs SPR:$Sd), (ins HPR:$Sm),
                                 IIC_fpCVTHI, "vcvt", ".u32.f16\t$Sd, $Sm",
                                 []>,
              Sched<[WriteFPCVT]> {
  let Inst{7} = 1; // Z bit
  let isUnpredicable = 1;
}

def : VFPNoNEONPat<(i32 (fp_to_uint (f16 HPR:$a))),
                   (COPY_TO_REGCLASS (VTOUIZH (f16 HPR:$a)), GPR)>;
def : VFPPat<(i32 (fp_to_uint_sat (f16 HPR:$a), i32)),
             (COPY_TO_REGCLASS (VTOUIZH (f16 HPR:$a)), GPR)>;

// And the Z bit '0' variants, i.e. use the rounding mode specified by FPSCR.
let Uses = [FPSCR] in {
def VTOSIRD : AVConv1IsD_Encode<0b11101, 0b11, 0b1101, 0b1011,
                                (outs SPR:$Sd), (ins DPR:$Dm),
                                IIC_fpCVTDI, "vcvtr", ".s32.f64\t$Sd, $Dm",
                                [(set SPR:$Sd, (int_arm_vcvtr (f64 DPR:$Dm)))]>,
              Sched<[WriteFPCVT]> {
  let Inst{7} = 0; // Z bit
}

def VTOSIRS : AVConv1InsS_Encode<0b11101, 0b11, 0b1101, 0b1010,
                                 (outs SPR:$Sd), (ins SPR:$Sm),
                                 IIC_fpCVTSI, "vcvtr", ".s32.f32\t$Sd, $Sm",
                                 [(set SPR:$Sd, (int_arm_vcvtr SPR:$Sm))]>,
              Sched<[WriteFPCVT]> {
  let Inst{7} = 0; // Z bit
}

def VTOSIRH : AVConv1IsH_Encode<0b11101, 0b11, 0b1101, 0b1001,
                                 (outs SPR:$Sd), (ins SPR:$Sm),
                                 IIC_fpCVTHI, "vcvtr", ".s32.f16\t$Sd, $Sm",
                                 []>,
              Sched<[WriteFPCVT]> {
  let Inst{7} = 0; // Z bit
  let isUnpredicable = 1;
}

def VTOUIRD : AVConv1IsD_Encode<0b11101, 0b11, 0b1100, 0b1011,
                                (outs SPR:$Sd), (ins DPR:$Dm),
                                IIC_fpCVTDI, "vcvtr", ".u32.f64\t$Sd, $Dm",
                                [(set SPR:$Sd, (int_arm_vcvtru(f64 DPR:$Dm)))]>,
              Sched<[WriteFPCVT]> {
  let Inst{7} = 0; // Z bit
}

def VTOUIRS : AVConv1InsS_Encode<0b11101, 0b11, 0b1100, 0b1010,
                                 (outs SPR:$Sd), (ins SPR:$Sm),
                                 IIC_fpCVTSI, "vcvtr", ".u32.f32\t$Sd, $Sm",
                                 [(set SPR:$Sd, (int_arm_vcvtru SPR:$Sm))]>,
              Sched<[WriteFPCVT]> {
  let Inst{7} = 0; // Z bit
}

def VTOUIRH : AVConv1IsH_Encode<0b11101, 0b11, 0b1100, 0b1001,
                                 (outs SPR:$Sd), (ins SPR:$Sm),
                                 IIC_fpCVTHI, "vcvtr", ".u32.f16\t$Sd, $Sm",
                                 []>,
              Sched<[WriteFPCVT]> {
  let Inst{7} = 0; // Z bit
  let isUnpredicable = 1;
}
}

// v8.3-a Javascript Convert to Signed fixed-point
def VJCVT : AVConv1IsD_Encode<0b11101, 0b11, 0b1001, 0b1011,
                                (outs SPR:$Sd), (ins DPR:$Dm),
                                IIC_fpCVTDI, "vjcvt", ".s32.f64\t$Sd, $Dm",
                                []>,
            Requires<[HasFPARMv8, HasV8_3a]> {
  let Inst{7} = 1; // Z bit
}

// Convert between floating-point and fixed-point
// Data type for fixed-point naming convention:
//   S16 (U=0, sx=0) -> SH
//   U16 (U=1, sx=0) -> UH
//   S32 (U=0, sx=1) -> SL
//   U32 (U=1, sx=1) -> UL

let Constraints = "$a = $dst" in {

// FP to Fixed-Point:

// Single Precision register
class AVConv1XInsS_Encode<bits<5> op1, bits<2> op2, bits<4> op3, bits<4> op4,
                          bit op5, dag oops, dag iops, InstrItinClass itin,
                          string opc, string asm, list<dag> pattern>
  : AVConv1XI<op1, op2, op3, op4, op5, oops, iops, itin, opc, asm, pattern> {
  bits<5> dst;
  // if dp_operation then UInt(D:Vd) else UInt(Vd:D);
  let Inst{22} = dst{0};
  let Inst{15-12} = dst{4-1};

  let hasSideEffects = 0;
}

// Double Precision register
class AVConv1XInsD_Encode<bits<5> op1, bits<2> op2, bits<4> op3, bits<4> op4,
                          bit op5, dag oops, dag iops, InstrItinClass itin,
                          string opc, string asm, list<dag> pattern>
  : AVConv1XI<op1, op2, op3, op4, op5, oops, iops, itin, opc, asm, pattern> {
  bits<5> dst;
  // if dp_operation then UInt(D:Vd) else UInt(Vd:D);
  let Inst{22} = dst{4};
  let Inst{15-12} = dst{3-0};

  let hasSideEffects = 0;
  let Predicates = [HasVFP2, HasDPVFP];
}

let isUnpredicable = 1 in {

def VTOSHH : AVConv1XInsS_Encode<0b11101, 0b11, 0b1110, 0b1001, 0,
                       (outs SPR:$dst), (ins SPR:$a, fbits16:$fbits),
                 IIC_fpCVTHI, "vcvt", ".s16.f16\t$dst, $a, $fbits", []>,
             Requires<[HasFullFP16]>,
             Sched<[WriteFPCVT]>;

def VTOUHH : AVConv1XInsS_Encode<0b11101, 0b11, 0b1111, 0b1001, 0,
                       (outs SPR:$dst), (ins SPR:$a, fbits16:$fbits),
                 IIC_fpCVTHI, "vcvt", ".u16.f16\t$dst, $a, $fbits", []>,
             Requires<[HasFullFP16]>,
             Sched<[WriteFPCVT]>;

def VTOSLH : AVConv1XInsS_Encode<0b11101, 0b11, 0b1110, 0b1001, 1,
                       (outs SPR:$dst), (ins SPR:$a, fbits32:$fbits),
                 IIC_fpCVTHI, "vcvt", ".s32.f16\t$dst, $a, $fbits", []>,
             Requires<[HasFullFP16]>,
             Sched<[WriteFPCVT]>;

def VTOULH : AVConv1XInsS_Encode<0b11101, 0b11, 0b1111, 0b1001, 1,
                       (outs SPR:$dst), (ins SPR:$a, fbits32:$fbits),
                 IIC_fpCVTHI, "vcvt", ".u32.f16\t$dst, $a, $fbits", []>,
             Requires<[HasFullFP16]>,
             Sched<[WriteFPCVT]>;

} // End of 'let isUnpredicable = 1 in'

def VTOSHS : AVConv1XInsS_Encode<0b11101, 0b11, 0b1110, 0b1010, 0,
                       (outs SPR:$dst), (ins SPR:$a, fbits16:$fbits),
                 IIC_fpCVTSI, "vcvt", ".s16.f32\t$dst, $a, $fbits", []>,
             Sched<[WriteFPCVT]> {
  // Some single precision VFP instructions may be executed on both NEON and
  // VFP pipelines on A8.
  let D = VFPNeonA8Domain;
}

def VTOUHS : AVConv1XInsS_Encode<0b11101, 0b11, 0b1111, 0b1010, 0,
                       (outs SPR:$dst), (ins SPR:$a, fbits16:$fbits),
                 IIC_fpCVTSI, "vcvt", ".u16.f32\t$dst, $a, $fbits", []>,
             Sched<[WriteFPCVT]> {
  // Some single precision VFP instructions may be executed on both NEON and
  // VFP pipelines on A8.
  let D = VFPNeonA8Domain;
}

def VTOSLS : AVConv1XInsS_Encode<0b11101, 0b11, 0b1110, 0b1010, 1,
                       (outs SPR:$dst), (ins SPR:$a, fbits32:$fbits),
                 IIC_fpCVTSI, "vcvt", ".s32.f32\t$dst, $a, $fbits", []>,
             Sched<[WriteFPCVT]> {
  // Some single precision VFP instructions may be executed on both NEON and
  // VFP pipelines on A8.
  let D = VFPNeonA8Domain;
}

def VTOULS : AVConv1XInsS_Encode<0b11101, 0b11, 0b1111, 0b1010, 1,
                       (outs SPR:$dst), (ins SPR:$a, fbits32:$fbits),
                 IIC_fpCVTSI, "vcvt", ".u32.f32\t$dst, $a, $fbits", []>,
             Sched<[WriteFPCVT]> {
  // Some single precision VFP instructions may be executed on both NEON and
  // VFP pipelines on A8.
  let D = VFPNeonA8Domain;
}

def VTOSHD : AVConv1XInsD_Encode<0b11101, 0b11, 0b1110, 0b1011, 0,
                       (outs DPR:$dst), (ins DPR:$a, fbits16:$fbits),
                 IIC_fpCVTDI, "vcvt", ".s16.f64\t$dst, $a, $fbits", []>,
             Sched<[WriteFPCVT]>;

def VTOUHD : AVConv1XInsD_Encode<0b11101, 0b11, 0b1111, 0b1011, 0,
                       (outs DPR:$dst), (ins DPR:$a, fbits16:$fbits),
                 IIC_fpCVTDI, "vcvt", ".u16.f64\t$dst, $a, $fbits", []>,
             Sched<[WriteFPCVT]>;

def VTOSLD : AVConv1XInsD_Encode<0b11101, 0b11, 0b1110, 0b1011, 1,
                       (outs DPR:$dst), (ins DPR:$a, fbits32:$fbits),
                 IIC_fpCVTDI, "vcvt", ".s32.f64\t$dst, $a, $fbits", []>,
             Sched<[WriteFPCVT]>;

def VTOULD : AVConv1XInsD_Encode<0b11101, 0b11, 0b1111, 0b1011, 1,
                       (outs DPR:$dst), (ins DPR:$a, fbits32:$fbits),
                 IIC_fpCVTDI, "vcvt", ".u32.f64\t$dst, $a, $fbits", []>,
             Sched<[WriteFPCVT]>;

// Fixed-Point to FP:

let isUnpredicable = 1 in {

def VSHTOH : AVConv1XInsS_Encode<0b11101, 0b11, 0b1010, 0b1001, 0,
                       (outs SPR:$dst), (ins SPR:$a, fbits16:$fbits),
                 IIC_fpCVTIH, "vcvt", ".f16.s16\t$dst, $a, $fbits", []>,
             Requires<[HasFullFP16]>,
             Sched<[WriteFPCVT]>;

def VUHTOH : AVConv1XInsS_Encode<0b11101, 0b11, 0b1011, 0b1001, 0,
                       (outs SPR:$dst), (ins SPR:$a, fbits16:$fbits),
                 IIC_fpCVTIH, "vcvt", ".f16.u16\t$dst, $a, $fbits", []>,
             Requires<[HasFullFP16]>,
             Sched<[WriteFPCVT]>;

def VSLTOH : AVConv1XInsS_Encode<0b11101, 0b11, 0b1010, 0b1001, 1,
                       (outs SPR:$dst), (ins SPR:$a, fbits32:$fbits),
                 IIC_fpCVTIH, "vcvt", ".f16.s32\t$dst, $a, $fbits", []>,
             Requires<[HasFullFP16]>,
             Sched<[WriteFPCVT]>;

def VULTOH : AVConv1XInsS_Encode<0b11101, 0b11, 0b1011, 0b1001, 1,
                       (outs SPR:$dst), (ins SPR:$a, fbits32:$fbits),
                 IIC_fpCVTIH, "vcvt", ".f16.u32\t$dst, $a, $fbits", []>,
             Requires<[HasFullFP16]>,
             Sched<[WriteFPCVT]>;

} // End of 'let isUnpredicable = 1 in'

def VSHTOS : AVConv1XInsS_Encode<0b11101, 0b11, 0b1010, 0b1010, 0,
                       (outs SPR:$dst), (ins SPR:$a, fbits16:$fbits),
                 IIC_fpCVTIS, "vcvt", ".f32.s16\t$dst, $a, $fbits", []>,
             Sched<[WriteFPCVT]> {
  // Some single precision VFP instructions may be executed on both NEON and
  // VFP pipelines on A8.
  let D = VFPNeonA8Domain;
}

def VUHTOS : AVConv1XInsS_Encode<0b11101, 0b11, 0b1011, 0b1010, 0,
                       (outs SPR:$dst), (ins SPR:$a, fbits16:$fbits),
                 IIC_fpCVTIS, "vcvt", ".f32.u16\t$dst, $a, $fbits", []>,
             Sched<[WriteFPCVT]> {
  // Some single precision VFP instructions may be executed on both NEON and
  // VFP pipelines on A8.
  let D = VFPNeonA8Domain;
}

def VSLTOS : AVConv1XInsS_Encode<0b11101, 0b11, 0b1010, 0b1010, 1,
                       (outs SPR:$dst), (ins SPR:$a, fbits32:$fbits),
                 IIC_fpCVTIS, "vcvt", ".f32.s32\t$dst, $a, $fbits", []>,
             Sched<[WriteFPCVT]> {
  // Some single precision VFP instructions may be executed on both NEON and
  // VFP pipelines on A8.
  let D = VFPNeonA8Domain;
}

def VULTOS : AVConv1XInsS_Encode<0b11101, 0b11, 0b1011, 0b1010, 1,
                       (outs SPR:$dst), (ins SPR:$a, fbits32:$fbits),
                 IIC_fpCVTIS, "vcvt", ".f32.u32\t$dst, $a, $fbits", []>,
             Sched<[WriteFPCVT]> {
  // Some single precision VFP instructions may be executed on both NEON and
  // VFP pipelines on A8.
  let D = VFPNeonA8Domain;
}

def VSHTOD : AVConv1XInsD_Encode<0b11101, 0b11, 0b1010, 0b1011, 0,
                       (outs DPR:$dst), (ins DPR:$a, fbits16:$fbits),
                 IIC_fpCVTID, "vcvt", ".f64.s16\t$dst, $a, $fbits", []>,
             Sched<[WriteFPCVT]>;

def VUHTOD : AVConv1XInsD_Encode<0b11101, 0b11, 0b1011, 0b1011, 0,
                       (outs DPR:$dst), (ins DPR:$a, fbits16:$fbits),
                 IIC_fpCVTID, "vcvt", ".f64.u16\t$dst, $a, $fbits", []>,
             Sched<[WriteFPCVT]>;

def VSLTOD : AVConv1XInsD_Encode<0b11101, 0b11, 0b1010, 0b1011, 1,
                       (outs DPR:$dst), (ins DPR:$a, fbits32:$fbits),
                 IIC_fpCVTID, "vcvt", ".f64.s32\t$dst, $a, $fbits", []>,
             Sched<[WriteFPCVT]>;

def VULTOD : AVConv1XInsD_Encode<0b11101, 0b11, 0b1011, 0b1011, 1,
                       (outs DPR:$dst), (ins DPR:$a, fbits32:$fbits),
                 IIC_fpCVTID, "vcvt", ".f64.u32\t$dst, $a, $fbits", []>,
             Sched<[WriteFPCVT]>;

} // End of 'let Constraints = "$a = $dst" in'

// BFloat16  - Single precision, unary, predicated
class BF16_VCVT<string opc, bits<2> op7_6>
   : VFPAI<(outs SPR:$Sd), (ins SPR:$dst, SPR:$Sm),
           VFPUnaryFrm, NoItinerary,
           opc, ".bf16.f32\t$Sd, $Sm", "", []>,
      RegConstraint<"$dst = $Sd">,
      Requires<[HasBF16]>,
     Sched<[]> {
  bits<5> Sd;
  bits<5> Sm;

  // Encode instruction operands.
  let Inst{3-0}   = Sm{4-1};
  let Inst{5}     = Sm{0};
  let Inst{15-12} = Sd{4-1};
  let Inst{22}    = Sd{0};

  let Inst{27-23} = 0b11101; // opcode1
  let Inst{21-20} = 0b11;    // opcode2
  let Inst{19-16} = 0b0011;  // opcode3
  let Inst{11-8}  = 0b1001;
  let Inst{7-6}   = op7_6;
  let Inst{4}     = 0;

  let DecoderNamespace = "VFPV8";
  let hasSideEffects = 0;
}

def BF16_VCVTB : BF16_VCVT<"vcvtb", 0b01>;
def BF16_VCVTT : BF16_VCVT<"vcvtt", 0b11>;

//===----------------------------------------------------------------------===//
// FP Multiply-Accumulate Operations.
//

def VMLAD : ADbI<0b11100, 0b00, 0, 0,
                 (outs DPR:$Dd), (ins DPR:$Ddin, DPR:$Dn, DPR:$Dm),
                 IIC_fpMAC64, "vmla", ".f64\t$Dd, $Dn, $Dm",
                 [(set DPR:$Dd, (fadd_mlx (fmul_su DPR:$Dn, DPR:$Dm),
                                          (f64 DPR:$Ddin)))]>,
              RegConstraint<"$Ddin = $Dd">,
              Requires<[HasVFP2,HasDPVFP,UseFPVMLx]>,
              Sched<[WriteFPMAC64, ReadFPMAC, ReadFPMUL, ReadFPMUL]>;

def VMLAS : ASbIn<0b11100, 0b00, 0, 0,
                  (outs SPR:$Sd), (ins SPR:$Sdin, SPR:$Sn, SPR:$Sm),
                  IIC_fpMAC32, "vmla", ".f32\t$Sd, $Sn, $Sm",
                  [(set SPR:$Sd, (fadd_mlx (fmul_su SPR:$Sn, SPR:$Sm),
                                           SPR:$Sdin))]>,
              RegConstraint<"$Sdin = $Sd">,
              Requires<[HasVFP2,DontUseNEONForFP,UseFPVMLx]>,
              Sched<[WriteFPMAC32, ReadFPMAC, ReadFPMUL, ReadFPMUL]> {
  // Some single precision VFP instructions may be executed on both NEON and
  // VFP pipelines on A8.
  let D = VFPNeonA8Domain;
}

def VMLAH : AHbI<0b11100, 0b00, 0, 0,
                  (outs HPR:$Sd), (ins HPR:$Sdin, HPR:$Sn, HPR:$Sm),
                  IIC_fpMAC16, "vmla", ".f16\t$Sd, $Sn, $Sm",
                  [(set (f16 HPR:$Sd), (fadd_mlx (fmul_su (f16 HPR:$Sn), (f16 HPR:$Sm)),
                                           (f16 HPR:$Sdin)))]>,
              RegConstraint<"$Sdin = $Sd">,
              Requires<[HasFullFP16,UseFPVMLx]>;

def : Pat<(fadd_mlx DPR:$dstin, (fmul_su DPR:$a, (f64 DPR:$b))),
          (VMLAD DPR:$dstin, DPR:$a, DPR:$b)>,
          Requires<[HasVFP2,HasDPVFP,UseFPVMLx]>;
def : Pat<(fadd_mlx SPR:$dstin, (fmul_su SPR:$a, SPR:$b)),
          (VMLAS SPR:$dstin, SPR:$a, SPR:$b)>,
          Requires<[HasVFP2,DontUseNEONForFP, UseFPVMLx]>;
def : Pat<(fadd_mlx HPR:$dstin, (fmul_su (f16 HPR:$a), HPR:$b)),
          (VMLAH HPR:$dstin, (f16 HPR:$a), HPR:$b)>,
          Requires<[HasFullFP16,DontUseNEONForFP, UseFPVMLx]>;


def VMLSD : ADbI<0b11100, 0b00, 1, 0,
                 (outs DPR:$Dd), (ins DPR:$Ddin, DPR:$Dn, DPR:$Dm),
                 IIC_fpMAC64, "vmls", ".f64\t$Dd, $Dn, $Dm",
                 [(set DPR:$Dd, (fadd_mlx (fneg (fmul_su DPR:$Dn,DPR:$Dm)),
                                          (f64 DPR:$Ddin)))]>,
              RegConstraint<"$Ddin = $Dd">,
              Requires<[HasVFP2,HasDPVFP,UseFPVMLx]>,
              Sched<[WriteFPMAC64, ReadFPMAC, ReadFPMUL, ReadFPMUL]>;

def VMLSS : ASbIn<0b11100, 0b00, 1, 0,
                  (outs SPR:$Sd), (ins SPR:$Sdin, SPR:$Sn, SPR:$Sm),
                  IIC_fpMAC32, "vmls", ".f32\t$Sd, $Sn, $Sm",
                  [(set SPR:$Sd, (fadd_mlx (fneg (fmul_su SPR:$Sn, SPR:$Sm)),
                                           SPR:$Sdin))]>,
              RegConstraint<"$Sdin = $Sd">,
              Requires<[HasVFP2,DontUseNEONForFP,UseFPVMLx]>,
              Sched<[WriteFPMAC32, ReadFPMAC, ReadFPMUL, ReadFPMUL]> {
  // Some single precision VFP instructions may be executed on both NEON and
  // VFP pipelines on A8.
  let D = VFPNeonA8Domain;
}

def VMLSH : AHbI<0b11100, 0b00, 1, 0,
                  (outs HPR:$Sd), (ins HPR:$Sdin, HPR:$Sn, HPR:$Sm),
                  IIC_fpMAC16, "vmls", ".f16\t$Sd, $Sn, $Sm",
                  [(set (f16 HPR:$Sd), (fadd_mlx (fneg (fmul_su (f16 HPR:$Sn), (f16 HPR:$Sm))),
                                           (f16 HPR:$Sdin)))]>,
              RegConstraint<"$Sdin = $Sd">,
              Requires<[HasFullFP16,UseFPVMLx]>;

def : Pat<(fsub_mlx DPR:$dstin, (fmul_su DPR:$a, (f64 DPR:$b))),
          (VMLSD DPR:$dstin, DPR:$a, DPR:$b)>,
          Requires<[HasVFP2,HasDPVFP,UseFPVMLx]>;
def : Pat<(fsub_mlx SPR:$dstin, (fmul_su SPR:$a, SPR:$b)),
          (VMLSS SPR:$dstin, SPR:$a, SPR:$b)>,
          Requires<[HasVFP2,DontUseNEONForFP,UseFPVMLx]>;
def : Pat<(fsub_mlx HPR:$dstin, (fmul_su (f16 HPR:$a), HPR:$b)),
          (VMLSH HPR:$dstin, (f16 HPR:$a), HPR:$b)>,
          Requires<[HasFullFP16,DontUseNEONForFP,UseFPVMLx]>;

def VNMLAD : ADbI<0b11100, 0b01, 1, 0,
                  (outs DPR:$Dd), (ins DPR:$Ddin, DPR:$Dn, DPR:$Dm),
                  IIC_fpMAC64, "vnmla", ".f64\t$Dd, $Dn, $Dm",
                  [(set DPR:$Dd,(fsub_mlx (fneg (fmul_su DPR:$Dn,DPR:$Dm)),
                                          (f64 DPR:$Ddin)))]>,
                RegConstraint<"$Ddin = $Dd">,
                Requires<[HasVFP2,HasDPVFP,UseFPVMLx]>,
                Sched<[WriteFPMAC64, ReadFPMAC, ReadFPMUL, ReadFPMUL]>;

def VNMLAS : ASbI<0b11100, 0b01, 1, 0,
                  (outs SPR:$Sd), (ins SPR:$Sdin, SPR:$Sn, SPR:$Sm),
                  IIC_fpMAC32, "vnmla", ".f32\t$Sd, $Sn, $Sm",
                  [(set SPR:$Sd, (fsub_mlx (fneg (fmul_su SPR:$Sn, SPR:$Sm)),
                                           SPR:$Sdin))]>,
                RegConstraint<"$Sdin = $Sd">,
                Requires<[HasVFP2,DontUseNEONForFP,UseFPVMLx]>,
                Sched<[WriteFPMAC32, ReadFPMAC, ReadFPMUL, ReadFPMUL]> {
  // Some single precision VFP instructions may be executed on both NEON and
  // VFP pipelines on A8.
  let D = VFPNeonA8Domain;
}

def VNMLAH : AHbI<0b11100, 0b01, 1, 0,
                  (outs HPR:$Sd), (ins HPR:$Sdin, HPR:$Sn, HPR:$Sm),
                  IIC_fpMAC16, "vnmla", ".f16\t$Sd, $Sn, $Sm",
                  [(set (f16 HPR:$Sd), (fsub_mlx (fneg (fmul_su (f16 HPR:$Sn), (f16 HPR:$Sm))),
                                           (f16 HPR:$Sdin)))]>,
                RegConstraint<"$Sdin = $Sd">,
                Requires<[HasFullFP16,UseFPVMLx]>;

// (-(a * b) - dst) -> -(dst + (a * b))
def : Pat<(fsub_mlx (fneg (fmul_su DPR:$a, (f64 DPR:$b))), DPR:$dstin),
          (VNMLAD DPR:$dstin, DPR:$a, DPR:$b)>,
          Requires<[HasVFP2,HasDPVFP,UseFPVMLx]>;
def : Pat<(fsub_mlx (fneg (fmul_su SPR:$a, SPR:$b)), SPR:$dstin),
          (VNMLAS SPR:$dstin, SPR:$a, SPR:$b)>,
          Requires<[HasVFP2,DontUseNEONForFP,UseFPVMLx]>;
def : Pat<(fsub_mlx (fneg (fmul_su (f16 HPR:$a), HPR:$b)), HPR:$dstin),
          (VNMLAH HPR:$dstin, (f16 HPR:$a), HPR:$b)>,
          Requires<[HasFullFP16,DontUseNEONForFP,UseFPVMLx]>;

// (-dst - (a * b)) -> -(dst + (a * b))
def : Pat<(fsub_mlx (fneg DPR:$dstin), (fmul_su DPR:$a, (f64 DPR:$b))),
          (VNMLAD DPR:$dstin, DPR:$a, DPR:$b)>,
          Requires<[HasVFP2,HasDPVFP,UseFPVMLx]>;
def : Pat<(fsub_mlx (fneg SPR:$dstin), (fmul_su SPR:$a, SPR:$b)),
          (VNMLAS SPR:$dstin, SPR:$a, SPR:$b)>,
          Requires<[HasVFP2,DontUseNEONForFP,UseFPVMLx]>;
def : Pat<(fsub_mlx (fneg HPR:$dstin), (fmul_su (f16 HPR:$a), HPR:$b)),
          (VNMLAH HPR:$dstin, (f16 HPR:$a), HPR:$b)>,
          Requires<[HasFullFP16,DontUseNEONForFP,UseFPVMLx]>;

def VNMLSD : ADbI<0b11100, 0b01, 0, 0,
                  (outs DPR:$Dd), (ins DPR:$Ddin, DPR:$Dn, DPR:$Dm),
                  IIC_fpMAC64, "vnmls", ".f64\t$Dd, $Dn, $Dm",
                  [(set DPR:$Dd, (fsub_mlx (fmul_su DPR:$Dn, DPR:$Dm),
                                           (f64 DPR:$Ddin)))]>,
               RegConstraint<"$Ddin = $Dd">,
               Requires<[HasVFP2,HasDPVFP,UseFPVMLx]>,
               Sched<[WriteFPMAC64, ReadFPMAC, ReadFPMUL, ReadFPMUL]>;

def VNMLSS : ASbI<0b11100, 0b01, 0, 0,
                  (outs SPR:$Sd), (ins SPR:$Sdin, SPR:$Sn, SPR:$Sm),
                  IIC_fpMAC32, "vnmls", ".f32\t$Sd, $Sn, $Sm",
             [(set SPR:$Sd, (fsub_mlx (fmul_su SPR:$Sn, SPR:$Sm), SPR:$Sdin))]>,
                         RegConstraint<"$Sdin = $Sd">,
                Requires<[HasVFP2,DontUseNEONForFP,UseFPVMLx]>,
             Sched<[WriteFPMAC32, ReadFPMAC, ReadFPMUL, ReadFPMUL]> {
  // Some single precision VFP instructions may be executed on both NEON and
  // VFP pipelines on A8.
  let D = VFPNeonA8Domain;
}

def VNMLSH : AHbI<0b11100, 0b01, 0, 0,
                  (outs HPR:$Sd), (ins HPR:$Sdin, HPR:$Sn, HPR:$Sm),
                  IIC_fpMAC16, "vnmls", ".f16\t$Sd, $Sn, $Sm",
             [(set (f16 HPR:$Sd), (fsub_mlx (fmul_su (f16 HPR:$Sn), (f16 HPR:$Sm)), (f16 HPR:$Sdin)))]>,
                         RegConstraint<"$Sdin = $Sd">,
                Requires<[HasFullFP16,UseFPVMLx]>;

def : Pat<(fsub_mlx (fmul_su DPR:$a, (f64 DPR:$b)), DPR:$dstin),
          (VNMLSD DPR:$dstin, DPR:$a, DPR:$b)>,
          Requires<[HasVFP2,HasDPVFP,UseFPVMLx]>;
def : Pat<(fsub_mlx (fmul_su SPR:$a, SPR:$b), SPR:$dstin),
          (VNMLSS SPR:$dstin, SPR:$a, SPR:$b)>,
          Requires<[HasVFP2,DontUseNEONForFP,UseFPVMLx]>;
def : Pat<(fsub_mlx (fmul_su (f16 HPR:$a), HPR:$b), HPR:$dstin),
          (VNMLSH HPR:$dstin, (f16 HPR:$a), HPR:$b)>,
          Requires<[HasFullFP16,DontUseNEONForFP,UseFPVMLx]>;

//===----------------------------------------------------------------------===//
// Fused FP Multiply-Accumulate Operations.
//
def VFMAD : ADbI<0b11101, 0b10, 0, 0,
                 (outs DPR:$Dd), (ins DPR:$Ddin, DPR:$Dn, DPR:$Dm),
                 IIC_fpFMAC64, "vfma", ".f64\t$Dd, $Dn, $Dm",
                 [(set DPR:$Dd, (fadd_mlx (fmul_su DPR:$Dn, DPR:$Dm),
                                          (f64 DPR:$Ddin)))]>,
              RegConstraint<"$Ddin = $Dd">,
              Requires<[HasVFP4,HasDPVFP,UseFusedMAC]>,
            Sched<[WriteFPMAC64, ReadFPMAC, ReadFPMUL, ReadFPMUL]>;

def VFMAS : ASbIn<0b11101, 0b10, 0, 0,
                  (outs SPR:$Sd), (ins SPR:$Sdin, SPR:$Sn, SPR:$Sm),
                  IIC_fpFMAC32, "vfma", ".f32\t$Sd, $Sn, $Sm",
                  [(set SPR:$Sd, (fadd_mlx (fmul_su SPR:$Sn, SPR:$Sm),
                                           SPR:$Sdin))]>,
              RegConstraint<"$Sdin = $Sd">,
              Requires<[HasVFP4,DontUseNEONForFP,UseFusedMAC]>,
            Sched<[WriteFPMAC32, ReadFPMAC, ReadFPMUL, ReadFPMUL]> {
  // Some single precision VFP instructions may be executed on both NEON and
  // VFP pipelines.
}

def VFMAH : AHbI<0b11101, 0b10, 0, 0,
                  (outs HPR:$Sd), (ins HPR:$Sdin, HPR:$Sn, HPR:$Sm),
                  IIC_fpFMAC16, "vfma", ".f16\t$Sd, $Sn, $Sm",
                  [(set (f16 HPR:$Sd), (fadd_mlx (fmul_su (f16 HPR:$Sn), (f16 HPR:$Sm)),
                                           (f16 HPR:$Sdin)))]>,
              RegConstraint<"$Sdin = $Sd">,
              Requires<[HasFullFP16,UseFusedMAC]>,
            Sched<[WriteFPMAC32, ReadFPMAC, ReadFPMUL, ReadFPMUL]>;

def : Pat<(fadd_mlx DPR:$dstin, (fmul_su DPR:$a, (f64 DPR:$b))),
          (VFMAD DPR:$dstin, DPR:$a, DPR:$b)>,
          Requires<[HasVFP4,HasDPVFP,UseFusedMAC]>;
def : Pat<(fadd_mlx SPR:$dstin, (fmul_su SPR:$a, SPR:$b)),
          (VFMAS SPR:$dstin, SPR:$a, SPR:$b)>,
          Requires<[HasVFP4,DontUseNEONForFP,UseFusedMAC]>;
def : Pat<(fadd_mlx HPR:$dstin, (fmul_su (f16 HPR:$a), HPR:$b)),
          (VFMAH HPR:$dstin, (f16 HPR:$a), HPR:$b)>,
          Requires<[HasFullFP16,DontUseNEONForFP,UseFusedMAC]>;

// Match @llvm.fma.* intrinsics
// (fma x, y, z) -> (vfms z, x, y)
def : Pat<(f64 (fma DPR:$Dn, DPR:$Dm, DPR:$Ddin)),
          (VFMAD DPR:$Ddin, DPR:$Dn, DPR:$Dm)>,
      Requires<[HasVFP4,HasDPVFP]>;
def : Pat<(f32 (fma SPR:$Sn, SPR:$Sm, SPR:$Sdin)),
          (VFMAS SPR:$Sdin, SPR:$Sn, SPR:$Sm)>,
      Requires<[HasVFP4]>;
def : Pat<(f16 (fma HPR:$Sn, HPR:$Sm, (f16 HPR:$Sdin))),
          (VFMAH (f16 HPR:$Sdin), (f16 HPR:$Sn), (f16 HPR:$Sm))>,
      Requires<[HasFullFP16]>;

def VFMSD : ADbI<0b11101, 0b10, 1, 0,
                 (outs DPR:$Dd), (ins DPR:$Ddin, DPR:$Dn, DPR:$Dm),
                 IIC_fpFMAC64, "vfms", ".f64\t$Dd, $Dn, $Dm",
                 [(set DPR:$Dd, (fadd_mlx (fneg (fmul_su DPR:$Dn,DPR:$Dm)),
                                          (f64 DPR:$Ddin)))]>,
              RegConstraint<"$Ddin = $Dd">,
              Requires<[HasVFP4,HasDPVFP,UseFusedMAC]>,
              Sched<[WriteFPMAC64, ReadFPMAC, ReadFPMUL, ReadFPMUL]>;

def VFMSS : ASbIn<0b11101, 0b10, 1, 0,
                  (outs SPR:$Sd), (ins SPR:$Sdin, SPR:$Sn, SPR:$Sm),
                  IIC_fpFMAC32, "vfms", ".f32\t$Sd, $Sn, $Sm",
                  [(set SPR:$Sd, (fadd_mlx (fneg (fmul_su SPR:$Sn, SPR:$Sm)),
                                           SPR:$Sdin))]>,
              RegConstraint<"$Sdin = $Sd">,
              Requires<[HasVFP4,DontUseNEONForFP,UseFusedMAC]>,
              Sched<[WriteFPMAC32, ReadFPMAC, ReadFPMUL, ReadFPMUL]> {
  // Some single precision VFP instructions may be executed on both NEON and
  // VFP pipelines.
}

def VFMSH : AHbI<0b11101, 0b10, 1, 0,
                  (outs HPR:$Sd), (ins HPR:$Sdin, HPR:$Sn, HPR:$Sm),
                  IIC_fpFMAC16, "vfms", ".f16\t$Sd, $Sn, $Sm",
                  [(set (f16 HPR:$Sd), (fadd_mlx (fneg (fmul_su (f16 HPR:$Sn), (f16 HPR:$Sm))),
                                           (f16 HPR:$Sdin)))]>,
              RegConstraint<"$Sdin = $Sd">,
              Requires<[HasFullFP16,UseFusedMAC]>,
              Sched<[WriteFPMAC32, ReadFPMAC, ReadFPMUL, ReadFPMUL]>;

def : Pat<(fsub_mlx DPR:$dstin, (fmul_su DPR:$a, (f64 DPR:$b))),
          (VFMSD DPR:$dstin, DPR:$a, DPR:$b)>,
          Requires<[HasVFP4,HasDPVFP,UseFusedMAC]>;
def : Pat<(fsub_mlx SPR:$dstin, (fmul_su SPR:$a, SPR:$b)),
          (VFMSS SPR:$dstin, SPR:$a, SPR:$b)>,
          Requires<[HasVFP4,DontUseNEONForFP,UseFusedMAC]>;
def : Pat<(fsub_mlx HPR:$dstin, (fmul_su (f16 HPR:$a), HPR:$b)),
          (VFMSH HPR:$dstin, (f16 HPR:$a), HPR:$b)>,
          Requires<[HasFullFP16,DontUseNEONForFP,UseFusedMAC]>;

// Match @llvm.fma.* intrinsics
// (fma (fneg x), y, z) -> (vfms z, x, y)
def : Pat<(f64 (fma (fneg DPR:$Dn), DPR:$Dm, DPR:$Ddin)),
          (VFMSD DPR:$Ddin, DPR:$Dn, DPR:$Dm)>,
      Requires<[HasVFP4,HasDPVFP]>;
def : Pat<(f32 (fma (fneg SPR:$Sn), SPR:$Sm, SPR:$Sdin)),
          (VFMSS SPR:$Sdin, SPR:$Sn, SPR:$Sm)>,
      Requires<[HasVFP4]>;
def : Pat<(f16 (fma (fneg (f16 HPR:$Sn)), (f16 HPR:$Sm), (f16 HPR:$Sdin))),
          (VFMSH (f16 HPR:$Sdin), (f16 HPR:$Sn), (f16 HPR:$Sm))>,
      Requires<[HasFullFP16]>;

def VFNMAD : ADbI<0b11101, 0b01, 1, 0,
                  (outs DPR:$Dd), (ins DPR:$Ddin, DPR:$Dn, DPR:$Dm),
                  IIC_fpFMAC64, "vfnma", ".f64\t$Dd, $Dn, $Dm",
                  [(set DPR:$Dd,(fsub_mlx (fneg (fmul_su DPR:$Dn,DPR:$Dm)),
                                          (f64 DPR:$Ddin)))]>,
                RegConstraint<"$Ddin = $Dd">,
                Requires<[HasVFP4,HasDPVFP,UseFusedMAC]>,
                Sched<[WriteFPMAC64, ReadFPMAC, ReadFPMUL, ReadFPMUL]>;

def VFNMAS : ASbI<0b11101, 0b01, 1, 0,
                  (outs SPR:$Sd), (ins SPR:$Sdin, SPR:$Sn, SPR:$Sm),
                  IIC_fpFMAC32, "vfnma", ".f32\t$Sd, $Sn, $Sm",
                  [(set SPR:$Sd, (fsub_mlx (fneg (fmul_su SPR:$Sn, SPR:$Sm)),
                                           SPR:$Sdin))]>,
                RegConstraint<"$Sdin = $Sd">,
                Requires<[HasVFP4,DontUseNEONForFP,UseFusedMAC]>,
                Sched<[WriteFPMAC32, ReadFPMAC, ReadFPMUL, ReadFPMUL]> {
  // Some single precision VFP instructions may be executed on both NEON and
  // VFP pipelines.
}

def VFNMAH : AHbI<0b11101, 0b01, 1, 0,
                  (outs HPR:$Sd), (ins HPR:$Sdin, HPR:$Sn, HPR:$Sm),
                  IIC_fpFMAC16, "vfnma", ".f16\t$Sd, $Sn, $Sm",
                  [(set (f16 HPR:$Sd), (fsub_mlx (fneg (fmul_su (f16 HPR:$Sn), (f16 HPR:$Sm))),
                                           (f16 HPR:$Sdin)))]>,
                RegConstraint<"$Sdin = $Sd">,
                Requires<[HasFullFP16,UseFusedMAC]>,
                Sched<[WriteFPMAC32, ReadFPMAC, ReadFPMUL, ReadFPMUL]>;

def : Pat<(fsub_mlx (fneg (fmul_su DPR:$a, (f64 DPR:$b))), DPR:$dstin),
          (VFNMAD DPR:$dstin, DPR:$a, DPR:$b)>,
          Requires<[HasVFP4,HasDPVFP,UseFusedMAC]>;
def : Pat<(fsub_mlx (fneg (fmul_su SPR:$a, SPR:$b)), SPR:$dstin),
          (VFNMAS SPR:$dstin, SPR:$a, SPR:$b)>,
          Requires<[HasVFP4,DontUseNEONForFP,UseFusedMAC]>;

// Match @llvm.fma.* intrinsics
// (fneg (fma x, y, z)) -> (vfnma z, x, y)
def : Pat<(fneg (fma (f64 DPR:$Dn), (f64 DPR:$Dm), (f64 DPR:$Ddin))),
          (VFNMAD DPR:$Ddin, DPR:$Dn, DPR:$Dm)>,
      Requires<[HasVFP4,HasDPVFP]>;
def : Pat<(fneg (fma (f32 SPR:$Sn), (f32 SPR:$Sm), (f32 SPR:$Sdin))),
          (VFNMAS SPR:$Sdin, SPR:$Sn, SPR:$Sm)>,
      Requires<[HasVFP4]>;
def : Pat<(fneg (fma (f16 HPR:$Sn), (f16 HPR:$Sm), (f16 (f16 HPR:$Sdin)))),
          (VFNMAH (f16 HPR:$Sdin), (f16 HPR:$Sn), (f16 HPR:$Sm))>,
      Requires<[HasFullFP16]>;
// (fma (fneg x), y, (fneg z)) -> (vfnma z, x, y)
def : Pat<(f64 (fma (fneg DPR:$Dn), DPR:$Dm, (fneg DPR:$Ddin))),
          (VFNMAD DPR:$Ddin, DPR:$Dn, DPR:$Dm)>,
      Requires<[HasVFP4,HasDPVFP]>;
def : Pat<(f32 (fma (fneg SPR:$Sn), SPR:$Sm, (fneg SPR:$Sdin))),
          (VFNMAS SPR:$Sdin, SPR:$Sn, SPR:$Sm)>,
      Requires<[HasVFP4]>;
def : Pat<(f16 (fma (fneg (f16 HPR:$Sn)), (f16 HPR:$Sm), (fneg (f16 HPR:$Sdin)))),
          (VFNMAH (f16 HPR:$Sdin), (f16 HPR:$Sn), (f16 HPR:$Sm))>,
      Requires<[HasFullFP16]>;

def VFNMSD : ADbI<0b11101, 0b01, 0, 0,
                  (outs DPR:$Dd), (ins DPR:$Ddin, DPR:$Dn, DPR:$Dm),
                  IIC_fpFMAC64, "vfnms", ".f64\t$Dd, $Dn, $Dm",
                  [(set DPR:$Dd, (fsub_mlx (fmul_su DPR:$Dn, DPR:$Dm),
                                           (f64 DPR:$Ddin)))]>,
               RegConstraint<"$Ddin = $Dd">,
               Requires<[HasVFP4,HasDPVFP,UseFusedMAC]>,
               Sched<[WriteFPMAC64, ReadFPMAC, ReadFPMUL, ReadFPMUL]>;

def VFNMSS : ASbI<0b11101, 0b01, 0, 0,
                  (outs SPR:$Sd), (ins SPR:$Sdin, SPR:$Sn, SPR:$Sm),
                  IIC_fpFMAC32, "vfnms", ".f32\t$Sd, $Sn, $Sm",
             [(set SPR:$Sd, (fsub_mlx (fmul_su SPR:$Sn, SPR:$Sm), SPR:$Sdin))]>,
                         RegConstraint<"$Sdin = $Sd">,
                  Requires<[HasVFP4,DontUseNEONForFP,UseFusedMAC]>,
                  Sched<[WriteFPMAC32, ReadFPMAC, ReadFPMUL, ReadFPMUL]> {
  // Some single precision VFP instructions may be executed on both NEON and
  // VFP pipelines.
}

def VFNMSH : AHbI<0b11101, 0b01, 0, 0,
                  (outs HPR:$Sd), (ins HPR:$Sdin, HPR:$Sn, HPR:$Sm),
                  IIC_fpFMAC16, "vfnms", ".f16\t$Sd, $Sn, $Sm",
             [(set (f16 HPR:$Sd), (fsub_mlx (fmul_su (f16 HPR:$Sn), (f16 HPR:$Sm)), (f16 HPR:$Sdin)))]>,
                         RegConstraint<"$Sdin = $Sd">,
                  Requires<[HasFullFP16,UseFusedMAC]>,
                  Sched<[WriteFPMAC32, ReadFPMAC, ReadFPMUL, ReadFPMUL]>;

def : Pat<(fsub_mlx (fmul_su DPR:$a, (f64 DPR:$b)), DPR:$dstin),
          (VFNMSD DPR:$dstin, DPR:$a, DPR:$b)>,
          Requires<[HasVFP4,HasDPVFP,UseFusedMAC]>;
def : Pat<(fsub_mlx (fmul_su SPR:$a, SPR:$b), SPR:$dstin),
          (VFNMSS SPR:$dstin, SPR:$a, SPR:$b)>,
          Requires<[HasVFP4,DontUseNEONForFP,UseFusedMAC]>;

// Match @llvm.fma.* intrinsics

// (fma x, y, (fneg z)) -> (vfnms z, x, y))
def : Pat<(f64 (fma DPR:$Dn, DPR:$Dm, (fneg DPR:$Ddin))),
          (VFNMSD DPR:$Ddin, DPR:$Dn, DPR:$Dm)>,
      Requires<[HasVFP4,HasDPVFP]>;
def : Pat<(f32 (fma SPR:$Sn, SPR:$Sm, (fneg SPR:$Sdin))),
          (VFNMSS SPR:$Sdin, SPR:$Sn, SPR:$Sm)>,
      Requires<[HasVFP4]>;
def : Pat<(f16 (fma (f16 HPR:$Sn), (f16 HPR:$Sm), (fneg (f16 HPR:$Sdin)))),
          (VFNMSH (f16 HPR:$Sdin), (f16 HPR:$Sn), (f16 HPR:$Sm))>,
      Requires<[HasFullFP16]>;
// (fneg (fma (fneg x), y, z)) -> (vfnms z, x, y)
def : Pat<(fneg (f64 (fma (fneg DPR:$Dn), DPR:$Dm, DPR:$Ddin))),
          (VFNMSD DPR:$Ddin, DPR:$Dn, DPR:$Dm)>,
      Requires<[HasVFP4,HasDPVFP]>;
def : Pat<(fneg (f32 (fma (fneg SPR:$Sn), SPR:$Sm, SPR:$Sdin))),
          (VFNMSS SPR:$Sdin, SPR:$Sn, SPR:$Sm)>,
      Requires<[HasVFP4]>;
def : Pat<(fneg (f16 (fma (fneg (f16 HPR:$Sn)), (f16 HPR:$Sm), (f16 HPR:$Sdin)))),
          (VFNMSH (f16 HPR:$Sdin), (f16 HPR:$Sn), (f16 HPR:$Sm))>,
      Requires<[HasFullFP16]>;

//===----------------------------------------------------------------------===//
// FP Conditional moves.
//

let hasSideEffects = 0 in {
def VMOVDcc  : PseudoInst<(outs DPR:$Dd), (ins DPR:$Dn, DPR:$Dm, cmovpred:$p),
                    IIC_fpUNA64,
                    [(set (f64 DPR:$Dd),
                          (ARMcmov DPR:$Dn, DPR:$Dm, cmovpred:$p))]>,
               RegConstraint<"$Dn = $Dd">, Requires<[HasFPRegs64]>;

def VMOVScc  : PseudoInst<(outs SPR:$Sd), (ins SPR:$Sn, SPR:$Sm, cmovpred:$p),
                    IIC_fpUNA32,
                    [(set (f32 SPR:$Sd),
                          (ARMcmov SPR:$Sn, SPR:$Sm, cmovpred:$p))]>,
               RegConstraint<"$Sn = $Sd">, Requires<[HasFPRegs]>;

def VMOVHcc  : PseudoInst<(outs HPR:$Sd), (ins HPR:$Sn, HPR:$Sm, cmovpred:$p),
                    IIC_fpUNA16,
                    [(set (f16 HPR:$Sd),
                          (ARMcmov (f16 HPR:$Sn), (f16 HPR:$Sm), cmovpred:$p))]>,
               RegConstraint<"$Sd = $Sn">, Requires<[HasFPRegs]>;
} // hasSideEffects

//===----------------------------------------------------------------------===//
// Move from VFP System Register to ARM core register.
//

class MovFromVFP<bits<4> opc19_16, dag oops, dag iops, string opc, string asm,
                 list<dag> pattern>:
  VFPAI<oops, iops, VFPMiscFrm, IIC_fpSTAT, opc, asm, "", pattern> {

  // Instruction operand.
  bits<4> Rt;

  let Inst{27-20} = 0b11101111;
  let Inst{19-16} = opc19_16;
  let Inst{15-12} = Rt;
  let Inst{11-8}  = 0b1010;
  let Inst{7}     = 0;
  let Inst{6-5}   = 0b00;
  let Inst{4}     = 1;
  let Inst{3-0}   = 0b0000;
  let Unpredictable{7-5} = 0b111;
  let Unpredictable{3-0} = 0b1111;
}

let DecoderMethod = "DecodeForVMRSandVMSR" in {
 // APSR is the application level alias of CPSR. This FPSCR N, Z, C, V flags
 // to APSR.
 let Defs = [CPSR], Uses = [FPSCR_NZCV], Predicates = [HasFPRegs],
     Rt = 0b1111 /* apsr_nzcv */ in
 def FMSTAT : MovFromVFP<0b0001 /* fpscr */, (outs), (ins),
                         "vmrs", "\tAPSR_nzcv, fpscr", [(arm_fmstat)]>;

 // Application level FPSCR -> GPR
 let hasSideEffects = 1, Uses = [FPSCR], Predicates = [HasFPRegs] in
 def VMRS :  MovFromVFP<0b0001 /* fpscr */, (outs GPRnopc:$Rt), (ins),
                        "vmrs", "\t$Rt, fpscr",
                        [(set GPRnopc:$Rt, (int_arm_get_fpscr))]>;

 // System level FPEXC, FPSID -> GPR
 let Uses = [FPSCR] in {
   def VMRS_FPEXC : MovFromVFP<0b1000 /* fpexc */, (outs GPRnopc:$Rt), (ins),
                               "vmrs", "\t$Rt, fpexc", []>;
   def VMRS_FPSID : MovFromVFP<0b0000 /* fpsid */, (outs GPRnopc:$Rt), (ins),
                               "vmrs", "\t$Rt, fpsid", []>;
   def VMRS_MVFR0 : MovFromVFP<0b0111 /* mvfr0 */, (outs GPRnopc:$Rt), (ins),
                              "vmrs", "\t$Rt, mvfr0", []>;
   def VMRS_MVFR1 : MovFromVFP<0b0110 /* mvfr1 */, (outs GPRnopc:$Rt), (ins),
                               "vmrs", "\t$Rt, mvfr1", []>;
   let Predicates = [HasFPARMv8] in {
     def VMRS_MVFR2 : MovFromVFP<0b0101 /* mvfr2 */, (outs GPRnopc:$Rt), (ins),
                                 "vmrs", "\t$Rt, mvfr2", []>;
   }
   def VMRS_FPINST : MovFromVFP<0b1001 /* fpinst */, (outs GPRnopc:$Rt), (ins),
                                "vmrs", "\t$Rt, fpinst", []>;
   def VMRS_FPINST2 : MovFromVFP<0b1010 /* fpinst2 */, (outs GPRnopc:$Rt),
                                 (ins), "vmrs", "\t$Rt, fpinst2", []>;
   let Predicates = [HasV8_1MMainline, HasFPRegs] in {
     // System level FPSCR_NZCVQC -> GPR
     def VMRS_FPSCR_NZCVQC
       : MovFromVFP<0b0010 /* fpscr_nzcvqc */,
                    (outs GPR:$Rt), (ins cl_FPSCR_NZCV:$fpscr_in),
                    "vmrs", "\t$Rt, fpscr_nzcvqc", []>;
   }
 }
 let Predicates = [HasV8_1MMainline, Has8MSecExt] in {
   // System level FPSCR -> GPR, with context saving for security extensions
   def VMRS_FPCXTNS : MovFromVFP<0b1110 /* fpcxtns */, (outs GPR:$Rt), (ins),
                                 "vmrs", "\t$Rt, fpcxtns", []>;
 }
 let Predicates = [HasV8_1MMainline, Has8MSecExt] in {
   // System level FPSCR -> GPR, with context saving for security extensions
   def VMRS_FPCXTS : MovFromVFP<0b1111 /* fpcxts */, (outs GPR:$Rt), (ins),
                                "vmrs", "\t$Rt, fpcxts", []>;
 }

 let Predicates = [HasV8_1MMainline, HasMVEInt] in {
   // System level VPR/P0 -> GPR
   let Uses = [VPR] in
   def VMRS_VPR : MovFromVFP<0b1100 /* vpr */, (outs GPR:$Rt), (ins),
                             "vmrs", "\t$Rt, vpr", []>;

   def VMRS_P0  : MovFromVFP<0b1101 /* p0 */, (outs GPR:$Rt), (ins VCCR:$cond),
                             "vmrs", "\t$Rt, p0", []>;
 }
}

//===----------------------------------------------------------------------===//
// Move from ARM core register to VFP System Register.
//

class MovToVFP<bits<4> opc19_16, dag oops, dag iops, string opc, string asm,
               list<dag> pattern>:
  VFPAI<oops, iops, VFPMiscFrm, IIC_fpSTAT, opc, asm, "", pattern> {

  // Instruction operand.
  bits<4> Rt;

  let Inst{27-20} = 0b11101110;
  let Inst{19-16} = opc19_16;
  let Inst{15-12} = Rt;
  let Inst{11-8}  = 0b1010;
  let Inst{7}     = 0;
  let Inst{6-5}   = 0b00;
  let Inst{4}     = 1;
  let Inst{3-0}   = 0b0000;
  let Predicates = [HasVFP2];
  let Unpredictable{7-5} = 0b111;
  let Unpredictable{3-0} = 0b1111;
}

let DecoderMethod = "DecodeForVMRSandVMSR" in {
 let Defs = [FPSCR] in {
   let Predicates = [HasFPRegs] in
   // Application level GPR -> FPSCR
   def VMSR : MovToVFP<0b0001 /* fpscr */, (outs), (ins GPRnopc:$Rt),
                       "vmsr", "\tfpscr, $Rt",
                       [(int_arm_set_fpscr GPRnopc:$Rt)]>;
   // System level GPR -> FPEXC
   def VMSR_FPEXC : MovToVFP<0b1000 /* fpexc */, (outs), (ins GPRnopc:$Rt),
                               "vmsr", "\tfpexc, $Rt", []>;
   // System level GPR -> FPSID
   def VMSR_FPSID : MovToVFP<0b0000 /* fpsid */, (outs), (ins GPRnopc:$Rt),
                             "vmsr", "\tfpsid, $Rt", []>;
   def VMSR_FPINST : MovToVFP<0b1001 /* fpinst */, (outs), (ins GPRnopc:$Rt),
                              "vmsr", "\tfpinst, $Rt", []>;
   def VMSR_FPINST2 : MovToVFP<0b1010 /* fpinst2 */, (outs), (ins GPRnopc:$Rt),
                               "vmsr", "\tfpinst2, $Rt", []>;
 }
 let Predicates = [HasV8_1MMainline, Has8MSecExt] in {
   // System level GPR -> FPSCR with context saving for security extensions
   def VMSR_FPCXTNS : MovToVFP<0b1110 /* fpcxtns */, (outs), (ins GPR:$Rt),
                               "vmsr", "\tfpcxtns, $Rt", []>;
 }
 let Predicates = [HasV8_1MMainline, Has8MSecExt] in {
   // System level GPR -> FPSCR with context saving for security extensions
   def VMSR_FPCXTS : MovToVFP<0b1111 /* fpcxts */, (outs), (ins GPR:$Rt),
                              "vmsr", "\tfpcxts, $Rt", []>;
 }
 let Predicates = [HasV8_1MMainline, HasFPRegs] in {
   // System level GPR -> FPSCR_NZCVQC
   def VMSR_FPSCR_NZCVQC
     : MovToVFP<0b0010 /* fpscr_nzcvqc */,
                (outs cl_FPSCR_NZCV:$fpscr_out), (ins GPR:$Rt),
                "vmsr", "\tfpscr_nzcvqc, $Rt", []>;
 }

 let Predicates = [HasV8_1MMainline, HasMVEInt] in {
   // System level GPR -> VPR/P0
   let Defs = [VPR] in
   def VMSR_VPR : MovToVFP<0b1100 /* vpr */, (outs), (ins GPR:$Rt),
                           "vmsr", "\tvpr, $Rt", []>;

   def VMSR_P0  : MovToVFP<0b1101 /* p0 */, (outs VCCR:$cond), (ins GPR:$Rt),
                           "vmsr", "\tp0, $Rt", []>;
 }
}

//===----------------------------------------------------------------------===//
// Misc.
//

// Materialize FP immediates. VFP3 only.
let isReMaterializable = 1 in {
def FCONSTD : VFPAI<(outs DPR:$Dd), (ins vfp_f64imm:$imm),
                    VFPMiscFrm, IIC_fpUNA64,
                    "vmov", ".f64\t$Dd, $imm", "",
                    [(set DPR:$Dd, vfp_f64imm:$imm)]>,
              Requires<[HasVFP3,HasDPVFP]> {
  bits<5> Dd;
  bits<8> imm;

  let Inst{27-23} = 0b11101;
  let Inst{22}    = Dd{4};
  let Inst{21-20} = 0b11;
  let Inst{19-16} = imm{7-4};
  let Inst{15-12} = Dd{3-0};
  let Inst{11-9}  = 0b101;
  let Inst{8}     = 1;          // Double precision.
  let Inst{7-4}   = 0b0000;
  let Inst{3-0}   = imm{3-0};
}

def FCONSTS : VFPAI<(outs SPR:$Sd), (ins vfp_f32imm:$imm),
                     VFPMiscFrm, IIC_fpUNA32,
                     "vmov", ".f32\t$Sd, $imm", "",
                     [(set SPR:$Sd, vfp_f32imm:$imm)]>, Requires<[HasVFP3]> {
  bits<5> Sd;
  bits<8> imm;

  let Inst{27-23} = 0b11101;
  let Inst{22}    = Sd{0};
  let Inst{21-20} = 0b11;
  let Inst{19-16} = imm{7-4};
  let Inst{15-12} = Sd{4-1};
  let Inst{11-9}  = 0b101;
  let Inst{8}     = 0;          // Single precision.
  let Inst{7-4}   = 0b0000;
  let Inst{3-0}   = imm{3-0};
}

def FCONSTH : VFPAI<(outs HPR:$Sd), (ins vfp_f16imm:$imm),
                     VFPMiscFrm, IIC_fpUNA16,
                     "vmov", ".f16\t$Sd, $imm", "",
                     [(set (f16 HPR:$Sd), vfp_f16imm:$imm)]>,
              Requires<[HasFullFP16]> {
  bits<5> Sd;
  bits<8> imm;

  let Inst{27-23} = 0b11101;
  let Inst{22}    = Sd{0};
  let Inst{21-20} = 0b11;
  let Inst{19-16} = imm{7-4};
  let Inst{15-12} = Sd{4-1};
  let Inst{11-8}  = 0b1001;     // Half precision
  let Inst{7-4}   = 0b0000;
  let Inst{3-0}   = imm{3-0};

  let isUnpredicable = 1;
}
}

def : Pat<(f32 (vfp_f32f16imm:$imm)),
          (f32 (COPY_TO_REGCLASS (f16 (FCONSTH (vfp_f32f16imm_xform (f32 $imm)))), SPR))> {
  let Predicates = [HasFullFP16];
}

// Floating-point environment management.
def : Pat<(get_fpenv), (VMRS)>;
def : Pat<(set_fpenv GPRnopc:$Rt), (VMSR GPRnopc:$Rt)>;
def : Pat<(reset_fpenv), (VMSR (MOVi 0))>, Requires<[IsARM]>;
def : Pat<(reset_fpenv), (VMSR (tMOVi8 0))>, Requires<[IsThumb]>;
def : Pat<(get_fpmode), (VMRS)>;

//===----------------------------------------------------------------------===//
// Assembler aliases.
//
// A few mnemonic aliases for pre-unifixed syntax. We don't guarantee to
// support them all, but supporting at least some of the basics is
// good to be friendly.
def : VFP2MnemonicAlias<"flds", "vldr">;
def : VFP2MnemonicAlias<"fldd", "vldr">;
def : VFP2MnemonicAlias<"fmrs", "vmov">;
def : VFP2MnemonicAlias<"fmsr", "vmov">;
def : VFP2MnemonicAlias<"fsqrts", "vsqrt">;
def : VFP2MnemonicAlias<"fsqrtd", "vsqrt">;
def : VFP2MnemonicAlias<"fadds", "vadd.f32">;
def : VFP2MnemonicAlias<"faddd", "vadd.f64">;
def : VFP2MnemonicAlias<"fmrdd", "vmov">;
def : VFP2MnemonicAlias<"fmrds", "vmov">;
def : VFP2MnemonicAlias<"fmrrd", "vmov">;
def : VFP2MnemonicAlias<"fmdrr", "vmov">;
def : VFP2MnemonicAlias<"fmuls", "vmul.f32">;
def : VFP2MnemonicAlias<"fmuld", "vmul.f64">;
def : VFP2MnemonicAlias<"fnegs", "vneg.f32">;
def : VFP2MnemonicAlias<"fnegd", "vneg.f64">;
def : VFP2MnemonicAlias<"ftosizd", "vcvt.s32.f64">;
def : VFP2MnemonicAlias<"ftosid", "vcvtr.s32.f64">;
def : VFP2MnemonicAlias<"ftosizs", "vcvt.s32.f32">;
def : VFP2MnemonicAlias<"ftosis", "vcvtr.s32.f32">;
def : VFP2MnemonicAlias<"ftouizd", "vcvt.u32.f64">;
def : VFP2MnemonicAlias<"ftouid", "vcvtr.u32.f64">;
def : VFP2MnemonicAlias<"ftouizs", "vcvt.u32.f32">;
def : VFP2MnemonicAlias<"ftouis", "vcvtr.u32.f32">;
def : VFP2MnemonicAlias<"fsitod", "vcvt.f64.s32">;
def : VFP2MnemonicAlias<"fsitos", "vcvt.f32.s32">;
def : VFP2MnemonicAlias<"fuitod", "vcvt.f64.u32">;
def : VFP2MnemonicAlias<"fuitos", "vcvt.f32.u32">;
def : VFP2MnemonicAlias<"fsts", "vstr">;
def : VFP2MnemonicAlias<"fstd", "vstr">;
def : VFP2MnemonicAlias<"fmacd", "vmla.f64">;
def : VFP2MnemonicAlias<"fmacs", "vmla.f32">;
def : VFP2MnemonicAlias<"fcpys", "vmov.f32">;
def : VFP2MnemonicAlias<"fcpyd", "vmov.f64">;
def : VFP2MnemonicAlias<"fcmps", "vcmp.f32">;
def : VFP2MnemonicAlias<"fcmpd", "vcmp.f64">;
def : VFP2MnemonicAlias<"fdivs", "vdiv.f32">;
def : VFP2MnemonicAlias<"fdivd", "vdiv.f64">;
def : VFP2MnemonicAlias<"fmrx", "vmrs">;
def : VFP2MnemonicAlias<"fmxr", "vmsr">;

// Be friendly and accept the old form of zero-compare
def : VFP2DPInstAlias<"fcmpzd${p} $val", (VCMPZD DPR:$val, pred:$p)>;
def : VFP2InstAlias<"fcmpzs${p} $val", (VCMPZS SPR:$val, pred:$p)>;


def : InstAlias<"fmstat${p}", (FMSTAT pred:$p), 0>, Requires<[HasFPRegs]>;
def : VFP2InstAlias<"fadds${p} $Sd, $Sn, $Sm",
                    (VADDS SPR:$Sd, SPR:$Sn, SPR:$Sm, pred:$p)>;
def : VFP2DPInstAlias<"faddd${p} $Dd, $Dn, $Dm",
                      (VADDD DPR:$Dd, DPR:$Dn, DPR:$Dm, pred:$p)>;
def : VFP2InstAlias<"fsubs${p} $Sd, $Sn, $Sm",
                    (VSUBS SPR:$Sd, SPR:$Sn, SPR:$Sm, pred:$p)>;
def : VFP2DPInstAlias<"fsubd${p} $Dd, $Dn, $Dm",
                      (VSUBD DPR:$Dd, DPR:$Dn, DPR:$Dm, pred:$p)>;

// No need for the size suffix on VSQRT. It's implied by the register classes.
def : VFP2InstAlias<"vsqrt${p} $Sd, $Sm", (VSQRTS SPR:$Sd, SPR:$Sm, pred:$p)>;
def : VFP2DPInstAlias<"vsqrt${p} $Dd, $Dm", (VSQRTD DPR:$Dd, DPR:$Dm, pred:$p)>;

// VLDR/VSTR accept an optional type suffix.
def : VFP2InstAlias<"vldr${p}.32 $Sd, $addr",
                    (VLDRS SPR:$Sd, addrmode5:$addr, pred:$p)>;
def : VFP2InstAlias<"vstr${p}.32 $Sd, $addr",
                    (VSTRS SPR:$Sd, addrmode5:$addr, pred:$p)>;
def : VFP2InstAlias<"vldr${p}.64 $Dd, $addr",
                    (VLDRD DPR:$Dd, addrmode5:$addr, pred:$p)>;
def : VFP2InstAlias<"vstr${p}.64 $Dd, $addr",
                    (VSTRD DPR:$Dd, addrmode5:$addr, pred:$p)>;

// VMOV can accept optional 32-bit or less data type suffix suffix.
def : VFP2InstAlias<"vmov${p}.8 $Rt, $Sn",
                    (VMOVRS GPR:$Rt, SPR:$Sn, pred:$p)>;
def : VFP2InstAlias<"vmov${p}.16 $Rt, $Sn",
                    (VMOVRS GPR:$Rt, SPR:$Sn, pred:$p)>;
def : VFP2InstAlias<"vmov${p}.32 $Rt, $Sn",
                    (VMOVRS GPR:$Rt, SPR:$Sn, pred:$p)>;
def : VFP2InstAlias<"vmov${p}.8 $Sn, $Rt",
                    (VMOVSR SPR:$Sn, GPR:$Rt, pred:$p)>;
def : VFP2InstAlias<"vmov${p}.16 $Sn, $Rt",
                    (VMOVSR SPR:$Sn, GPR:$Rt, pred:$p)>;
def : VFP2InstAlias<"vmov${p}.32 $Sn, $Rt",
                    (VMOVSR SPR:$Sn, GPR:$Rt, pred:$p)>;

def : VFP2InstAlias<"vmov${p}.f64 $Rt, $Rt2, $Dn",
                    (VMOVRRD GPR:$Rt, GPR:$Rt2, DPR:$Dn, pred:$p)>;
def : VFP2InstAlias<"vmov${p}.f64 $Dn, $Rt, $Rt2",
                    (VMOVDRR DPR:$Dn, GPR:$Rt, GPR:$Rt2, pred:$p)>;

// VMOVS doesn't need the .f32 to disambiguate from the NEON encoding the way
// VMOVD does.
def : VFP2InstAlias<"vmov${p} $Sd, $Sm",
                    (VMOVS SPR:$Sd, SPR:$Sm, pred:$p)>;

// FCONSTD/FCONSTS alias for vmov.f64/vmov.f32
// These aliases provide added functionality over vmov.f instructions by
// allowing users to write assembly containing encoded floating point constants
// (e.g. #0x70 vs #1.0).  Without these alises there is no way for the
// assembler to accept encoded fp constants (but the equivalent fp-literal is
// accepted directly by vmovf).
def : VFP3InstAlias<"fconstd${p} $Dd, $val",
                    (FCONSTD DPR:$Dd, vfp_f64imm:$val, pred:$p)>;
def : VFP3InstAlias<"fconsts${p} $Sd, $val",
                    (FCONSTS SPR:$Sd, vfp_f32imm:$val, pred:$p)>;

def VSCCLRMD : VFPXI<(outs), (ins pred:$p, fp_dreglist_with_vpr:$regs, variable_ops),
                      AddrModeNone, 4, IndexModeNone, VFPMiscFrm, NoItinerary,
                      "vscclrm{$p}\t$regs", "", []>, Sched<[]> {
  bits<13> regs;
  let Inst{31-23} = 0b111011001;
  let Inst{22} = regs{12};
  let Inst{21-16} = 0b011111;
  let Inst{15-12} = regs{11-8};
  let Inst{11-8} = 0b1011;
  let Inst{7-1} = regs{7-1};
  let Inst{0} = 0;

  let DecoderMethod = "DecodeVSCCLRM";

  list<Predicate> Predicates = [HasV8_1MMainline, Has8MSecExt];
}

def VSCCLRMS : VFPXI<(outs), (ins pred:$p, fp_sreglist_with_vpr:$regs, variable_ops),
                      AddrModeNone, 4, IndexModeNone, VFPMiscFrm, NoItinerary,
                      "vscclrm{$p}\t$regs", "", []>, Sched<[]> {
  bits<13> regs;
  let Inst{31-23} = 0b111011001;
  let Inst{22} = regs{8};
  let Inst{21-16} = 0b011111;
  let Inst{15-12} = regs{12-9};
  let Inst{11-8} = 0b1010;
  let Inst{7-0} = regs{7-0};

  let DecoderMethod = "DecodeVSCCLRM";

  list<Predicate> Predicates = [HasV8_1MMainline, Has8MSecExt];
}

//===----------------------------------------------------------------------===//
// Store VFP System Register to memory.
//

class vfp_vstrldr<bit opc, bit P, bit W, bits<4> SysReg, string sysreg,
                  dag oops, dag iops, IndexMode im, string Dest, string cstr>
    : VFPI<oops, iops, AddrModeT2_i7s4, 4, im, VFPLdStFrm, IIC_fpSTAT,
           !if(opc,"vldr","vstr"), !strconcat("\t", sysreg, ", ", Dest), cstr, []>,
      Sched<[]> {
  bits<12> addr;
  let Inst{27-25} = 0b110;
  let Inst{24} = P;
  let Inst{23} = addr{7};
  let Inst{22} = SysReg{3};
  let Inst{21} = W;
  let Inst{20} = opc;
  let Inst{19-16} = addr{11-8};
  let Inst{15-13} = SysReg{2-0};
  let Inst{12-7} = 0b011111;
  let Inst{6-0} = addr{6-0};
  list<Predicate> Predicates = [HasFPRegs, HasV8_1MMainline];
  let mayLoad = opc;
  let mayStore = !if(opc, 0b0, 0b1);
  let hasSideEffects = 1;
}

multiclass vfp_vstrldr_sysreg<bit opc, bits<4> SysReg, string sysreg,
                              dag oops=(outs), dag iops=(ins)> {
  def _off :
    vfp_vstrldr<opc, 1, 0, SysReg, sysreg,
                oops, !con(iops, (ins t2addrmode_imm7s4:$addr)),
                IndexModePost, "$addr", "" > {
    let DecoderMethod = "DecodeVSTRVLDR_SYSREG<false>";
  }

  def _pre :
    vfp_vstrldr<opc, 1, 1, SysReg, sysreg,
                !con(oops, (outs GPRnopc:$wb)),
                !con(iops, (ins t2addrmode_imm7s4_pre:$addr)),
                IndexModePre, "$addr!", "$addr.base = $wb"> {
    let DecoderMethod = "DecodeVSTRVLDR_SYSREG<true>";
  }

  def _post :
    vfp_vstrldr<opc, 0, 1, SysReg, sysreg,
                !con(oops, (outs GPRnopc:$wb)),
                !con(iops, (ins t2_addr_offset_none:$Rn,
                                t2am_imm7s4_offset:$addr)),
                IndexModePost, "$Rn$addr", "$Rn.base = $wb"> {
   bits<4> Rn;
   let Inst{19-16} = Rn{3-0};
   let DecoderMethod = "DecodeVSTRVLDR_SYSREG<true>";
 }
}

let Defs = [FPSCR] in {
  defm VSTR_FPSCR          : vfp_vstrldr_sysreg<0b0,0b0001, "fpscr">;
  defm VSTR_FPSCR_NZCVQC   : vfp_vstrldr_sysreg<0b0,0b0010, "fpscr_nzcvqc">;

  let Predicates = [HasV8_1MMainline, Has8MSecExt] in {
    defm VSTR_FPCXTNS      : vfp_vstrldr_sysreg<0b0,0b1110, "fpcxtns">;
    defm VSTR_FPCXTS       : vfp_vstrldr_sysreg<0b0,0b1111, "fpcxts">;
  }
}

let Predicates = [HasV8_1MMainline, HasMVEInt] in {
  let Uses = [VPR] in {
    defm VSTR_VPR          : vfp_vstrldr_sysreg<0b0,0b1100, "vpr">;
  }
  defm VSTR_P0             : vfp_vstrldr_sysreg<0b0,0b1101, "p0",
                                                (outs), (ins VCCR:$P0)>;

  let Defs = [VPR] in {
    defm VLDR_VPR          : vfp_vstrldr_sysreg<0b1,0b1100, "vpr">;
  }
  defm VLDR_P0             : vfp_vstrldr_sysreg<0b1,0b1101, "p0",
                                                (outs VCCR:$P0), (ins)>;
}

let Uses = [FPSCR] in {
  defm VLDR_FPSCR          : vfp_vstrldr_sysreg<0b1,0b0001, "fpscr">;
  defm VLDR_FPSCR_NZCVQC   : vfp_vstrldr_sysreg<0b1,0b0010, "fpscr_nzcvqc">;

  let Predicates = [HasV8_1MMainline, Has8MSecExt] in {
    defm VLDR_FPCXTNS      : vfp_vstrldr_sysreg<0b1,0b1110, "fpcxtns">;
    defm VLDR_FPCXTS       : vfp_vstrldr_sysreg<0b1,0b1111, "fpcxts">;
  }
}

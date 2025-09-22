//===-- VOPDInstructions.td - Vector Instruction Definitions --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Encodings
//===----------------------------------------------------------------------===//

class VOPDe<bits<4> opX, bits<5> opY> : Enc64 {
  bits<9> src0X;
  bits<8> vsrc1X;
  bits<8> vdstX;
  bits<9> src0Y;
  bits<8> vsrc1Y;
  bits<8> vdstY;

  let Inst{8-0} = src0X;
  let Inst{16-9} = vsrc1X;
  let Inst{21-17} = opY;
  let Inst{25-22} = opX;
  let Inst{31-26} = 0x32; // encoding
  let Inst{40-32} = src0Y;
  let Inst{48-41} = vsrc1Y;
  let Inst{55-49} = vdstY{7-1};
  let Inst{63-56} = vdstX;
}

class VOPD_MADKe<bits<4> opX, bits<5> opY> : Enc96 {
  bits<9> src0X;
  bits<8> vsrc1X;
  bits<8> vdstX;
  bits<9> src0Y;
  bits<8> vsrc1Y;
  bits<8> vdstY;
  bits<32> imm;

  let Inst{8-0} = src0X;
  let Inst{16-9} = vsrc1X;
  let Inst{21-17} = opY;
  let Inst{25-22} = opX;
  let Inst{31-26} = 0x32; // encoding
  let Inst{40-32} = src0Y;
  let Inst{48-41} = vsrc1Y;
  let Inst{55-49} = vdstY{7-1};
  let Inst{63-56} = vdstX;
  let Inst{95-64} = imm;
}

//===----------------------------------------------------------------------===//
// VOPD classes
//===----------------------------------------------------------------------===//


class GFXGenD<GFXGen Gen, list<string> DXPseudos, list<string> DYPseudos,
              Predicate subtargetPred = Gen.AssemblerPredicate> :
    GFXGen<Gen.AssemblerPredicate, Gen.DecoderNamespace, Gen.Suffix,
           Gen.Subtarget> {
  list<string> VOPDXPseudos = DXPseudos;
  list<string> VOPDYPseudos = DYPseudos;
  Predicate SubtargetPredicate = subtargetPred;
}

class VOPD_Base<dag outs, dag ins, string asm, VOP_Pseudo VDX, VOP_Pseudo VDY,
                VOPD_Component XasVC, VOPD_Component YasVC, GFXGenD Gen>
    : VOPAnyCommon<outs, ins, asm, []>,
      VOP<NAME>,
      SIMCInstr<NAME, Gen.Subtarget> {
  // Fields for table indexing
  Instruction Opcode = !cast<Instruction>(NAME);
  bits<5> OpX = XasVC.VOPDOp;
  bits<5> OpY = YasVC.VOPDOp;
  bits<4> SubTgt = Gen.Subtarget;

  let VALU = 1;

  let DecoderNamespace = Gen.DecoderNamespace;
  let AssemblerPredicate = Gen.AssemblerPredicate;
  let WaveSizePredicate = isWave32;
  let isCodeGenOnly = 0;
  let SubtargetPredicate = Gen.SubtargetPredicate;
  let AsmMatchConverter  = "cvtVOPD";
  let Size = 8;
  let ReadsModeReg = !or(VDX.ReadsModeReg, VDY.ReadsModeReg);
  let mayRaiseFPException = ReadsModeReg;

  // V_DUAL_FMAC and V_DUAL_DOT2ACC_F32_F16 need a dummy src2 tied to dst for
  // passes to track its uses. Its presence does not affect VOPD formation rules
  // because the rules for src2 and dst are the same. src2X and src2Y should not
  // be encoded.
  bit hasSrc2AccX = !or(!eq(VDX.Mnemonic, "v_fmac_f32"), !eq(VDX.Mnemonic, "v_dot2c_f32_f16"));
  bit hasSrc2AccY = !or(!eq(VDY.Mnemonic, "v_fmac_f32"), !eq(VDY.Mnemonic, "v_dot2c_f32_f16"));
  string ConstraintsX = !if(hasSrc2AccX, "$src2X = $vdstX", "");
  string ConstraintsY = !if(hasSrc2AccY, "$src2Y = $vdstY", "");
  let Constraints =
      ConstraintsX # !if(!and(hasSrc2AccX, hasSrc2AccY), ", ", "") # ConstraintsY;
  string DisableEncodingX = !if(hasSrc2AccX, "$src2X", "");
  string DisableEncodingY = !if(hasSrc2AccY, "$src2Y", "");
  let DisableEncoding =
      DisableEncodingX # !if(!and(hasSrc2AccX, hasSrc2AccY), ", ", "") # DisableEncodingY;

  let Uses = RegListUnion<VDX.Uses, VDY.Uses>.ret;
  let Defs = RegListUnion<VDX.Defs, VDY.Defs>.ret;
  let SchedRW = !listconcat(VDX.SchedRW, VDY.SchedRW);
}

class VOPD<dag outs, dag ins, string asm, VOP_Pseudo VDX, VOP_Pseudo VDY,
           VOPD_Component XasVC, VOPD_Component YasVC, GFXGenD Gen>
    : VOPD_Base<outs, ins, asm, VDX, VDY, XasVC, YasVC, Gen>,
      VOPDe<XasVC.VOPDOp{3-0}, YasVC.VOPDOp> {
  let Inst{16-9} = !if (!eq(VDX.Mnemonic, "v_mov_b32"), 0x0, vsrc1X);
  let Inst{48-41} = !if (!eq(VDY.Mnemonic, "v_mov_b32"), 0x0, vsrc1Y);
}

class VOPD_MADK<dag outs, dag ins, string asm, VOP_Pseudo VDX, VOP_Pseudo VDY,
                VOPD_Component XasVC, VOPD_Component YasVC, GFXGenD Gen>
    : VOPD_Base<outs, ins, asm, VDX, VDY, XasVC, YasVC, Gen>,
      VOPD_MADKe<XasVC.VOPDOp{3-0}, YasVC.VOPDOp> {
  let Inst{16-9} = !if (!eq(VDX.Mnemonic, "v_mov_b32"), 0x0, vsrc1X);
  let Inst{48-41} = !if (!eq(VDY.Mnemonic, "v_mov_b32"), 0x0, vsrc1Y);
  let Size = 12;
  let FixedSize = 1;
}

// V_DUAL_DOT2ACC_F32_BF16 is a legal instruction, but V_DOT2ACC_F32_BF16 is
// not. V_DUAL_DOT2C_F32_BF16 is a legal instruction on GFX12, but
// V_DOT2C_F32_F16_e32 is not. Since we generate the DUAL form by converting
// from the normal form we will never generate them.
defvar VOPDPseudosCommon = [
  "V_FMAC_F32_e32", "V_FMAAK_F32", "V_FMAMK_F32", "V_MUL_F32_e32",
  "V_ADD_F32_e32", "V_SUB_F32_e32", "V_SUBREV_F32_e32", "V_MUL_LEGACY_F32_e32",
  "V_MOV_B32_e32", "V_CNDMASK_B32_e32", "V_MAX_F32_e32", "V_MIN_F32_e32"
];
defvar VOPDPseudosGFX11 = ["V_DOT2C_F32_F16_e32"];
defvar VOPDYOnlyPseudosCommon = ["V_ADD_U32_e32", "V_LSHLREV_B32_e32",
                                 "V_AND_B32_e32"];

defvar VOPDXPseudosGFX11 = !listconcat(VOPDPseudosCommon, VOPDPseudosGFX11);
defvar VOPDXPseudosGFX12 = VOPDPseudosCommon;
defvar VOPDYPseudosGFX11 = !listconcat(VOPDXPseudosGFX11, VOPDYOnlyPseudosCommon);
defvar VOPDYPseudosGFX12 = !listconcat(VOPDXPseudosGFX12, VOPDYOnlyPseudosCommon);

def GFX11GenD : GFXGenD<GFX11Gen, VOPDXPseudosGFX11, VOPDYPseudosGFX11>;
def GFX12GenD : GFXGenD<GFX12Gen, VOPDXPseudosGFX12, VOPDYPseudosGFX12>;


def VOPDDstYOperand : RegisterOperand<VGPR_32, "printRegularOperand"> {
  let DecoderMethod = "decodeOperandVOPDDstY";
}

class getRenamed<string VOPDName, GFXGen Gen> {
  string ret = !if(!eq(Gen.Subtarget, GFX12Gen.Subtarget),
                   !if(!eq(VOPDName, "v_dual_max_f32"),
                       "v_dual_max_num_f32",
                       !if(!eq(VOPDName, "v_dual_min_f32"),
                           "v_dual_min_num_f32",
                           VOPDName)),
                   VOPDName);
}

foreach Gen = [GFX11GenD, GFX12GenD] in {
  foreach x = Gen.VOPDXPseudos in {
    foreach y = Gen.VOPDYPseudos in {
      defvar xInst = !cast<VOP_Pseudo>(x);
      defvar yInst = !cast<VOP_Pseudo>(y);
      defvar XasVC = !cast<VOPD_Component>(x);
      defvar YasVC = !cast<VOPD_Component>(y);
      defvar xAsmName = getRenamed<XasVC.VOPDName, Gen>.ret;
      defvar yAsmName = getRenamed<YasVC.VOPDName, Gen>.ret;
      defvar isMADK = !or(!eq(x, "V_FMAAK_F32"), !eq(x, "V_FMAMK_F32"),
                          !eq(y, "V_FMAAK_F32"), !eq(y, "V_FMAMK_F32"));
      // If X or Y is MADK (have a mandatory immediate), all src operands which
      // may contain an optional literal must use the VSrc_*_Deferred operand
      // type. Optional literal operands in MADK VOPD components always use this
      // operand form. If Both X and Y are MADK, the mandatory literal of X
      // additionally must use an alternate operand format which defers to the
      // 'real' Y literal
      defvar isOpXMADK = !or(!eq(x, "V_FMAAK_F32"), !eq(x, "V_FMAMK_F32"));
      defvar isOpYMADK = !or(!eq(y, "V_FMAAK_F32"), !eq(y, "V_FMAMK_F32"));
      defvar OpName = "V_DUAL_" # !substr(x,2) # "_X_" # !substr(y,2) # Gen.Suffix;
      defvar outs = (outs VGPRSrc_32:$vdstX, VOPDDstYOperand:$vdstY);
      if !or(isOpXMADK, isOpYMADK) then {
        if !and(isOpXMADK, isOpYMADK) then {
          defvar X_MADK_Pfl = !cast<VOP_MADK_Base>(xInst.Pfl);
          defvar ins = !con(xInst.Pfl.InsVOPDXDeferred, yInst.Pfl.InsVOPDY);
          defvar asm = xAsmName #" "# X_MADK_Pfl.AsmVOPDXDeferred #" :: "# yAsmName #" "# yInst.Pfl.AsmVOPDY;
          def OpName : VOPD_MADK<outs, ins, asm, xInst, yInst, XasVC, YasVC, Gen>;
        } else {
          defvar asm = xAsmName #" "# xInst.Pfl.AsmVOPDX #" :: "# yAsmName #" "# yInst.Pfl.AsmVOPDY;
          if isOpXMADK then {
            assert !not(isOpYMADK), "Expected only OpX as MADK";
            defvar ins = !con(xInst.Pfl.InsVOPDX, yInst.Pfl.InsVOPDYDeferred);
            def OpName : VOPD_MADK<outs, ins, asm, xInst, yInst, XasVC, YasVC, Gen>;
          } else {
            assert !not(isOpXMADK), "Expected only OpY as MADK";
            defvar ins = !con(xInst.Pfl.InsVOPDXDeferred, yInst.Pfl.InsVOPDY);
            def OpName : VOPD_MADK<outs, ins, asm, xInst, yInst, XasVC, YasVC, Gen>;
          }
        }
      } else {
        defvar ins = !con(xInst.Pfl.InsVOPDX, yInst.Pfl.InsVOPDY);
        defvar asm = xAsmName #" "# xInst.Pfl.AsmVOPDX #" :: "# yAsmName #" "# yInst.Pfl.AsmVOPDY;
        def OpName : VOPD<outs, ins, asm, xInst, yInst, XasVC, YasVC, Gen>;
      }
    }
  }
}


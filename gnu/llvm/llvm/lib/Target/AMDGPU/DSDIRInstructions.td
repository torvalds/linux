//===-- DSDIRInstructions.td - LDS/VDS Direct Instruction Definitions -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// LDSDIR/VDSDIR encoding (LDSDIR is gfx11, VDSDIR is gfx12+)
//===----------------------------------------------------------------------===//

class LDSDIRe<bits<2> op, bit is_direct> : Enc32 {
  // encoding fields
  bits<2> attrchan;
  bits<6> attr;
  bits<4> waitvdst;
  bits<8> vdst;

  // encoding
  let Inst{31-24} = 0xce; // encoding
  let Inst{23-22} = 0x0; // reserved
  let Inst{21-20} = op;
  let Inst{19-16} = waitvdst;
  let Inst{15-10} = !if(is_direct, ?, attr);
  let Inst{9-8} = !if(is_direct, ?, attrchan);
  let Inst{7-0} = vdst;
}

class VDSDIRe<bits<2> op, bit is_direct> : Enc32 {
  // encoding fields
  bits<2> attrchan;
  bits<6> attr;
  bits<4> waitvdst;
  bits<8> vdst;
  bits<1> waitvsrc;

  // encoding
  let Inst{31-24} = 0xce; // encoding
  let Inst{23} = waitvsrc;
  let Inst{22} = 0x0; // reserved
  let Inst{21-20} = op;
  let Inst{19-16} = waitvdst;
  let Inst{15-10} = !if(is_direct, ?, attr);
  let Inst{9-8} = !if(is_direct, ?, attrchan);
  let Inst{7-0} = vdst;
}

//===----------------------------------------------------------------------===//
// LDSDIR/VDSDIR Classes
//===----------------------------------------------------------------------===//

class LDSDIR_getIns<bit direct> {
  dag ret = !if(direct,
    (ins WaitVDST:$waitvdst),
    (ins InterpAttr:$attr, InterpAttrChan:$attrchan, WaitVDST:$waitvdst)
  );
}

class VDSDIR_getIns<bit direct> {
  dag ret = !if(direct,
    (ins WaitVAVDst:$waitvdst, WaitVMVSrc:$waitvsrc),
    (ins InterpAttr:$attr, InterpAttrChan:$attrchan, WaitVAVDst:$waitvdst,
         WaitVMVSrc:$waitvsrc)
  );
}

class DSDIR_Common<string opName, string asm = "", dag ins, bit direct> :
  InstSI<(outs VGPR_32:$vdst), ins, asm> {
  let LDSDIR = 1;
  let EXP_CNT = 1;

  let hasSideEffects = 0;
  let mayLoad = 1;
  let mayStore = 0;
  let maybeAtomic = 0;

  string Mnemonic = opName;
  let UseNamedOperandTable = 1;

  let Uses = [M0, EXEC];
  let DisableWQM = 0;
  let SchedRW = [WriteLDS];

  bit is_direct;
  let is_direct = direct;
}

class DSDIR_Pseudo<string opName, dag ins, bit direct> :
  DSDIR_Common<opName, "", ins, direct>,
  SIMCInstr<opName, SIEncodingFamily.NONE> {
  let isPseudo = 1;
  let isCodeGenOnly = 1;
}

class LDSDIR_getAsm<bit direct> {
  string ret = !if(direct,
    " $vdst$waitvdst",
    " $vdst, $attr$attrchan$waitvdst"
  );
}

class VDSDIR_getAsm<bit direct> {
  string ret = !if(direct,
    " $vdst$waitvdst$waitvsrc",
    " $vdst, $attr$attrchan$waitvdst$waitvsrc"
  );
}

class DSDIR_Real<DSDIR_Pseudo lds, dag ins, string asm, int subtarget> :
  DSDIR_Common<lds.Mnemonic,
               lds.Mnemonic # asm,
               ins,
               lds.is_direct>,
  SIMCInstr <lds.PseudoInstr, subtarget> {
  let isPseudo = 0;
  let isCodeGenOnly = 0;

  // copy SubtargetPredicate from pseudo.
  let SubtargetPredicate = lds.SubtargetPredicate;
}

//===----------------------------------------------------------------------===//
// LDS/VDS Direct Instructions
//===----------------------------------------------------------------------===//

let SubtargetPredicate = isGFX11Only in {

def LDS_DIRECT_LOAD : DSDIR_Pseudo<"lds_direct_load", LDSDIR_getIns<1>.ret, 1>;
def LDS_PARAM_LOAD : DSDIR_Pseudo<"lds_param_load", LDSDIR_getIns<0>.ret, 0>;

def : GCNPat <
  (f32 (int_amdgcn_lds_direct_load M0)),
  (LDS_DIRECT_LOAD 0)
>;

def : GCNPat <
  (f32 (int_amdgcn_lds_param_load timm:$attrchan, timm:$attr, M0)),
  (LDS_PARAM_LOAD timm:$attr, timm:$attrchan, 0)
>;

} // End SubtargetPredicate = isGFX11Only

let SubtargetPredicate = isGFX12Plus in {

def DS_DIRECT_LOAD : DSDIR_Pseudo<"ds_direct_load", VDSDIR_getIns<1>.ret, 1>;
def DS_PARAM_LOAD : DSDIR_Pseudo<"ds_param_load", VDSDIR_getIns<0>.ret, 0>;

def : GCNPat <
  (f32 (int_amdgcn_lds_direct_load M0)),
  (DS_DIRECT_LOAD 0, 1)
>;

def : GCNPat <
  (f32 (int_amdgcn_lds_param_load timm:$attrchan, timm:$attr, M0)),
  (DS_PARAM_LOAD timm:$attr, timm:$attrchan, 0, 1)
>;

} // End SubtargetPredicate = isGFX12Only

//===----------------------------------------------------------------------===//
// GFX11
//===----------------------------------------------------------------------===//

multiclass DSDIR_Real_gfx11<bits<2> op> {
  defvar lds = !cast<DSDIR_Pseudo>(NAME);
  def _gfx11 : DSDIR_Real<lds, lds.InOperandList,
                          LDSDIR_getAsm<lds.is_direct>.ret,
                          SIEncodingFamily.GFX11>,
               LDSDIRe<op, lds.is_direct> {
    let AssemblerPredicate = isGFX11Only;
    let DecoderNamespace = "GFX11";
  }
}

defm LDS_PARAM_LOAD : DSDIR_Real_gfx11<0x0>;
defm LDS_DIRECT_LOAD : DSDIR_Real_gfx11<0x1>;

//===----------------------------------------------------------------------===//
// GFX12+
//===----------------------------------------------------------------------===//

multiclass DSDIR_Real_gfx12<bits<2> op> {
  defvar lds = !cast<DSDIR_Pseudo>(NAME);
  def _gfx12 : DSDIR_Real<lds, lds.InOperandList,
                          VDSDIR_getAsm<lds.is_direct>.ret,
                          SIEncodingFamily.GFX12>,
               VDSDIRe<op, lds.is_direct> {
    let AssemblerPredicate = isGFX12Plus;
    let DecoderNamespace = "GFX12";
  }
}

defm DS_PARAM_LOAD : DSDIR_Real_gfx12<0x0>;
defm DS_DIRECT_LOAD : DSDIR_Real_gfx12<0x1>;

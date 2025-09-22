//===-- DSInstructions.td - DS Instruction Definitions --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

class DS_Pseudo <string opName, dag outs, dag ins, string asmOps, list<dag> pattern=[]> :
  InstSI <outs, ins, "", pattern>,
  SIMCInstr <opName, SIEncodingFamily.NONE> {

  let LGKM_CNT = 1;
  let DS = 1;
  let GWS = 0;
  let Size = 8;
  let UseNamedOperandTable = 1;

  // Most instruction load and store data, so set this as the default.
  let mayLoad = 1;
  let mayStore = 1;

  let hasSideEffects = 0;
  let SchedRW = [WriteLDS];

  let isPseudo = 1;
  let isCodeGenOnly = 1;

  string Mnemonic = opName;
  string AsmOperands = asmOps;

  // Well these bits a kind of hack because it would be more natural
  // to test "outs" and "ins" dags for the presence of particular operands
  bits<1> has_vdst = 1;
  bits<1> has_addr = 1;
  bits<1> has_data0 = 1;
  bits<1> has_data1 = 1;

  bits<1> has_gws_data0 = 0; // data0 is encoded as addr

  bits<1> has_offset  = 1; // has "offset" that should be split to offset0,1
  bits<1> has_offset0 = 1;
  bits<1> has_offset1 = 1;

  bits<1> has_gds = 1;
  bits<1> gdsValue = 0; // if has_gds == 0 set gds to this value

  bits<1> has_m0_read = 1;

  let Uses = !if(has_m0_read, [M0, EXEC], [EXEC]);
}

class DS_Real <DS_Pseudo ps, string opName = ps.Mnemonic> :
  InstSI <ps.OutOperandList, ps.InOperandList, opName # ps.AsmOperands>,
  Enc64 {

  let isPseudo = 0;
  let isCodeGenOnly = 0;
  let LGKM_CNT = 1;
  let DS = 1;
  let UseNamedOperandTable = 1;

  // copy relevant pseudo op flags
  let GWS                = ps.GWS;
  let SubtargetPredicate = ps.SubtargetPredicate;
  let WaveSizePredicate  = ps.WaveSizePredicate;
  let OtherPredicates    = ps.OtherPredicates;
  let TSFlags            = ps.TSFlags;
  let SchedRW            = ps.SchedRW;
  let mayLoad            = ps.mayLoad;
  let mayStore           = ps.mayStore;
  let IsAtomicRet        = ps.IsAtomicRet;
  let IsAtomicNoRet      = ps.IsAtomicNoRet;
  let Uses               = ps.Uses;
  let Defs               = ps.Defs;

  let Constraints = ps.Constraints;
  let DisableEncoding = ps.DisableEncoding;

  // encoding fields
  bits<10> vdst;
  bits<1> gds;
  bits<8> addr;
  bits<10> data0;
  bits<10> data1;
  bits<8> offset0;
  bits<8> offset1;

  bits<16> offset;
  let offset0 = !if(ps.has_offset, offset{7-0}, ?);
  let offset1 = !if(ps.has_offset, offset{15-8}, ?);

  bits<1> acc = !if(ps.has_vdst, vdst{9},
                    !if(!or(ps.has_data0, ps.has_gws_data0), data0{9}, 0));
}

// DS Pseudo instructions

class DS_0A1D_NORET<string opName, RegisterClass rc = VGPR_32>
: DS_Pseudo<opName,
  (outs),
  (ins getLdStRegisterOperand<rc>.ret:$data0, Offset:$offset, gds:$gds),
  " $data0$offset$gds"> {

  let has_addr = 0;
  let has_data1 = 0;
  let has_vdst = 0;
}

class DS_1A1D_NORET<string opName, RegisterClass rc = VGPR_32>
: DS_Pseudo<opName,
  (outs),
  (ins VGPR_32:$addr, getLdStRegisterOperand<rc>.ret:$data0, Offset:$offset, gds:$gds),
  " $addr, $data0$offset$gds"> {

  let has_data1 = 0;
  let has_vdst = 0;
  let IsAtomicNoRet = 1;
}

multiclass DS_1A1D_NORET_mc<string opName, RegisterClass rc = VGPR_32> {
  def "" : DS_1A1D_NORET<opName, rc>;

  let has_m0_read = 0 in {
    def _gfx9 : DS_1A1D_NORET<opName, rc>;
  }
}

multiclass DS_1A1D_NORET_mc_gfx9<string opName, RegisterClass rc = VGPR_32> {
  let has_m0_read = 0 in {
    def "" : DS_1A1D_NORET<opName, rc>;
  }
}

class DS_1A2D_NORET<string opName, RegisterClass rc = VGPR_32,
                    RegisterOperand data_op = getLdStRegisterOperand<rc>.ret>
: DS_Pseudo<opName,
  (outs),
  (ins VGPR_32:$addr, data_op:$data0, data_op:$data1, Offset:$offset, gds:$gds),
  " $addr, $data0, $data1$offset$gds"> {

  let has_vdst = 0;
  let IsAtomicNoRet = 1;
}

multiclass DS_1A2D_NORET_mc<string opName, RegisterClass rc = VGPR_32> {
  def "" : DS_1A2D_NORET<opName, rc>;

  let has_m0_read = 0 in {
    def _gfx9 : DS_1A2D_NORET<opName, rc>;
  }
}

class DS_1A2D_Off8_NORET <string opName, RegisterClass rc = VGPR_32,
                          RegisterOperand data_op = getLdStRegisterOperand<rc>.ret>
: DS_Pseudo<opName,
  (outs),
  (ins VGPR_32:$addr, data_op:$data0, data_op:$data1,
       Offset0:$offset0, Offset1:$offset1, gds:$gds),
  " $addr, $data0, $data1$offset0$offset1$gds"> {

  let has_vdst = 0;
  let has_offset = 0;
}

multiclass DS_1A2D_Off8_NORET_mc <string opName, RegisterClass rc = VGPR_32> {
  def "" : DS_1A2D_Off8_NORET<opName, rc>;

  let has_m0_read = 0 in {
    def _gfx9 : DS_1A2D_Off8_NORET<opName, rc>;
  }
}

class DS_0A1D_RET_GDS<string opName, RegisterClass rc = VGPR_32, RegisterClass src = rc,
                  RegisterOperand dst_op = getLdStRegisterOperand<rc>.ret,
                  RegisterOperand src_op = getLdStRegisterOperand<src>.ret>
: DS_Pseudo<opName,
  (outs dst_op:$vdst),
  (ins src_op:$data0, Offset:$offset),
  " $vdst, $data0$offset gds"> {

  let has_addr = 0;
  let has_data1 = 0;
  let has_gds = 0;
  let gdsValue = 1;
  let hasSideEffects = 1;
}

class DS_1A1D_RET <string opName, RegisterClass rc = VGPR_32,
                  RegisterOperand data_op = getLdStRegisterOperand<rc>.ret>
: DS_Pseudo<opName,
  (outs data_op:$vdst),
  (ins VGPR_32:$addr, data_op:$data0, Offset:$offset, gds:$gds),
  " $vdst, $addr, $data0$offset$gds"> {

  let hasPostISelHook = 1;
  let has_data1 = 0;
  let IsAtomicRet = 1;
}

multiclass DS_1A1D_RET_mc <string opName, RegisterClass rc = VGPR_32> {
  def "" : DS_1A1D_RET<opName, rc>;

  let has_m0_read = 0 in {
    def _gfx9 : DS_1A1D_RET<opName, rc>;
  }
}

multiclass DS_1A1D_RET_mc_gfx9 <string opName, RegisterClass rc = VGPR_32> {
  let has_m0_read = 0 in {
    def "" : DS_1A1D_RET<opName, rc>;
  }
}

class DS_1A2D_RET<string opName,
                  RegisterClass rc = VGPR_32,
                  RegisterClass src = rc,
                  RegisterOperand dst_op = getLdStRegisterOperand<rc>.ret,
                  RegisterOperand src_op = getLdStRegisterOperand<src>.ret>
: DS_Pseudo<opName,
  (outs dst_op:$vdst),
  (ins VGPR_32:$addr, src_op:$data0, src_op:$data1, Offset:$offset, gds:$gds),
  " $vdst, $addr, $data0, $data1$offset$gds"> {

  let hasPostISelHook = 1;
  let IsAtomicRet = 1;
}

multiclass DS_1A2D_RET_mc<string opName,
                          RegisterClass rc = VGPR_32,
                          RegisterClass src = rc> {
  def "" : DS_1A2D_RET<opName, rc, src>;

  let has_m0_read = 0 in {
    def _gfx9 : DS_1A2D_RET<opName, rc, src>;
  }
}

class DS_1A2D_Off8_RET<string opName,
                       RegisterClass rc = VGPR_32,
                       RegisterClass src = rc,
                       RegisterOperand dst_op = getLdStRegisterOperand<rc>.ret,
                       RegisterOperand src_op = getLdStRegisterOperand<src>.ret>
: DS_Pseudo<opName,
  (outs dst_op:$vdst),
  (ins VGPR_32:$addr, src_op:$data0, src_op:$data1, Offset0:$offset0, Offset1:$offset1, gds:$gds),
  " $vdst, $addr, $data0, $data1$offset0$offset1$gds"> {

  let has_offset = 0;
  let hasPostISelHook = 1;
}

multiclass DS_1A2D_Off8_RET_mc<string opName,
                               RegisterClass rc = VGPR_32,
                               RegisterClass src = rc> {
  def "" : DS_1A2D_Off8_RET<opName, rc, src>;

  let has_m0_read = 0 in {
    def _gfx9 : DS_1A2D_Off8_RET<opName, rc, src>;
  }
}

class DS_BVH_STACK<string opName>
: DS_Pseudo<opName,
  (outs getLdStRegisterOperand<VGPR_32>.ret:$vdst, VGPR_32:$addr),
  (ins VGPR_32:$addr_in, getLdStRegisterOperand<VGPR_32>.ret:$data0, VReg_128:$data1, Offset:$offset),
  " $vdst, $addr, $data0, $data1$offset"> {
  let Constraints = "$addr = $addr_in";
  let DisableEncoding = "$addr_in";
  let has_gds = 0;
  let gdsValue = 0;
  // TODO: Use MMOs in the LDS address space instead of hasSideEffects = 1.
  let hasSideEffects = 1;
  let SchedRW = [WriteLDS, WriteLDS];
}

class DS_1A_RET<string opName, RegisterClass rc = VGPR_32, bit HasTiedOutput = 0, Operand ofs = Offset,
                RegisterOperand data_op = getLdStRegisterOperand<rc>.ret>
: DS_Pseudo<opName,
  (outs data_op:$vdst),
  !if(HasTiedOutput,
    (ins VGPR_32:$addr, ofs:$offset, gds:$gds, data_op:$vdst_in),
    (ins VGPR_32:$addr, ofs:$offset, gds:$gds)),
  " $vdst, $addr$offset$gds"> {
  let Constraints = !if(HasTiedOutput, "$vdst = $vdst_in", "");
  let DisableEncoding = !if(HasTiedOutput, "$vdst_in", "");
  let has_data0 = 0;
  let has_data1 = 0;
}

multiclass DS_1A_RET_mc<string opName, RegisterClass rc = VGPR_32, bit HasTiedOutput = 0, Operand ofs = Offset> {
  def "" : DS_1A_RET<opName, rc, HasTiedOutput, ofs>;

  let has_m0_read = 0 in {
    def _gfx9 : DS_1A_RET<opName, rc, HasTiedOutput, ofs>;
  }
}

class DS_1A_RET_Tied<string opName, RegisterClass rc = VGPR_32> :
  DS_1A_RET<opName, rc, 1>;

class DS_1A_Off8_RET <string opName, RegisterClass rc = VGPR_32>
: DS_Pseudo<opName,
  (outs getLdStRegisterOperand<rc>.ret:$vdst),
  (ins VGPR_32:$addr, Offset0:$offset0, Offset1:$offset1, gds:$gds),
  " $vdst, $addr$offset0$offset1$gds"> {

  let has_offset = 0;
  let has_data0 = 0;
  let has_data1 = 0;
}

multiclass DS_1A_Off8_RET_mc <string opName, RegisterClass rc = VGPR_32> {
  def "" : DS_1A_Off8_RET<opName, rc>;

  let has_m0_read = 0 in {
    def _gfx9 : DS_1A_Off8_RET<opName, rc>;
  }
}

class DS_1A_RET_GDS <string opName> : DS_Pseudo<opName,
  (outs getLdStRegisterOperand<VGPR_32>.ret:$vdst),
  (ins VGPR_32:$addr, Offset:$offset),
  " $vdst, $addr$offset gds"> {

  let has_data0 = 0;
  let has_data1 = 0;
  let has_gds = 0;
  let gdsValue = 1;
}

class DS_0A_RET <string opName> : DS_Pseudo<opName,
  (outs getLdStRegisterOperand<VGPR_32>.ret:$vdst),
  (ins Offset:$offset, gds:$gds),
  " $vdst$offset$gds"> {

  let mayLoad = 1;
  let mayStore = 1;

  let has_addr = 0;
  let has_data0 = 0;
  let has_data1 = 0;
}

class DS_1A <string opName> : DS_Pseudo<opName,
  (outs),
  (ins VGPR_32:$addr, Offset:$offset, gds:$gds),
  " $addr$offset$gds"> {

  let mayLoad = 1;
  let mayStore = 1;

  let has_vdst = 0;
  let has_data0 = 0;
  let has_data1 = 0;
}

multiclass DS_1A_mc <string opName> {
  def "" : DS_1A<opName>;

  let has_m0_read = 0 in {
    def _gfx9 : DS_1A<opName>;
  }
}


class DS_GWS <string opName, dag ins, string asmOps>
: DS_Pseudo<opName, (outs), ins, asmOps> {
  let GWS = 1;

  let has_vdst  = 0;
  let has_addr  = 0;
  let has_data0 = 0;
  let has_data1 = 0;

  let has_gds   = 0;
  let gdsValue  = 1;
}

class DS_GWS_0D <string opName>
: DS_GWS<opName,
  (ins Offset:$offset), "$offset gds"> {
  let hasSideEffects = 1;
}

class DS_GWS_1D <string opName>
: DS_GWS<opName,
  (ins getLdStRegisterOperand<VGPR_32>.ret:$data0, Offset:$offset),
  " $data0$offset gds"> {

  let has_gws_data0 = 1;
  let hasSideEffects = 1;
}

class DS_VOID <string opName> : DS_Pseudo<opName,
  (outs), (ins), ""> {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 1;
  let UseNamedOperandTable = 0;

  let has_vdst = 0;
  let has_addr = 0;
  let has_data0 = 0;
  let has_data1 = 0;
  let has_offset = 0;
  let has_offset0 = 0;
  let has_offset1 = 0;
  let has_gds = 0;
}

class DS_1A1D_PERMUTE <string opName, SDPatternOperator node = null_frag,
                       RegisterOperand data_op = getLdStRegisterOperand<VGPR_32>.ret>
: DS_Pseudo<opName,
  (outs data_op:$vdst),
  (ins VGPR_32:$addr, data_op:$data0, Offset:$offset),
  " $vdst, $addr, $data0$offset",
  [(set i32:$vdst,
   (node (DS1Addr1Offset i32:$addr, i32:$offset), i32:$data0))] > {

  let mayLoad = 0;
  let mayStore = 0;
  let isConvergent = 1;

  let has_data1 = 0;
  let has_gds = 0;
}

class DSAtomicRetPat<DS_Pseudo inst, ValueType vt, PatFrag frag, int complexity = 0,
  bit gds=0> : GCNPat <(frag (DS1Addr1Offset i32:$ptr, i32:$offset), vt:$value),
  (inst $ptr, getVregSrcForVT<vt>.ret:$value, Offset:$offset, (i1 gds))> {
  let AddedComplexity = complexity;
}

defm DS_ADD_U32       : DS_1A1D_NORET_mc<"ds_add_u32">;
defm DS_SUB_U32       : DS_1A1D_NORET_mc<"ds_sub_u32">;
defm DS_RSUB_U32      : DS_1A1D_NORET_mc<"ds_rsub_u32">;
defm DS_INC_U32       : DS_1A1D_NORET_mc<"ds_inc_u32">;
defm DS_DEC_U32       : DS_1A1D_NORET_mc<"ds_dec_u32">;
defm DS_MIN_I32       : DS_1A1D_NORET_mc<"ds_min_i32">;
defm DS_MAX_I32       : DS_1A1D_NORET_mc<"ds_max_i32">;
defm DS_MIN_U32       : DS_1A1D_NORET_mc<"ds_min_u32">;
defm DS_MAX_U32       : DS_1A1D_NORET_mc<"ds_max_u32">;
defm DS_AND_B32       : DS_1A1D_NORET_mc<"ds_and_b32">;
defm DS_OR_B32        : DS_1A1D_NORET_mc<"ds_or_b32">;
defm DS_XOR_B32       : DS_1A1D_NORET_mc<"ds_xor_b32">;

let SubtargetPredicate = HasLDSFPAtomicAddF32 in {
defm DS_ADD_F32       : DS_1A1D_NORET_mc<"ds_add_f32">;
}

defm DS_MIN_F32       : DS_1A1D_NORET_mc<"ds_min_f32">;
defm DS_MAX_F32       : DS_1A1D_NORET_mc<"ds_max_f32">;

let mayLoad = 0 in {
defm DS_WRITE_B8      : DS_1A1D_NORET_mc<"ds_write_b8">;
defm DS_WRITE_B16     : DS_1A1D_NORET_mc<"ds_write_b16">;
defm DS_WRITE_B32     : DS_1A1D_NORET_mc<"ds_write_b32">;
defm DS_WRITE2_B32    : DS_1A2D_Off8_NORET_mc<"ds_write2_b32">;
defm DS_WRITE2ST64_B32: DS_1A2D_Off8_NORET_mc<"ds_write2st64_b32">;


let has_m0_read = 0 in {

let SubtargetPredicate = HasD16LoadStore in {
def DS_WRITE_B8_D16_HI  : DS_1A1D_NORET<"ds_write_b8_d16_hi">;
def DS_WRITE_B16_D16_HI : DS_1A1D_NORET<"ds_write_b16_d16_hi">;
}

} // End has_m0_read = 0

let SubtargetPredicate = HasDSAddTid in {
def DS_WRITE_ADDTID_B32 : DS_0A1D_NORET<"ds_write_addtid_b32">;
}

} // End mayLoad = 0

let SubtargetPredicate = HasLdsAtomicAddF64 in {
  defm DS_ADD_F64     : DS_1A1D_NORET_mc_gfx9<"ds_add_f64", VReg_64>;
  defm DS_ADD_RTN_F64 : DS_1A1D_RET_mc_gfx9<"ds_add_rtn_f64", VReg_64>;
} // End SubtargetPredicate = HasLdsAtomicAddF64

let SubtargetPredicate = HasAtomicDsPkAdd16Insts in {
  defm DS_PK_ADD_F16      : DS_1A1D_NORET_mc<"ds_pk_add_f16">;
  defm DS_PK_ADD_RTN_F16  : DS_1A1D_RET_mc<"ds_pk_add_rtn_f16", VGPR_32>;
  defm DS_PK_ADD_BF16     : DS_1A1D_NORET_mc<"ds_pk_add_bf16">;
  defm DS_PK_ADD_RTN_BF16 : DS_1A1D_RET_mc<"ds_pk_add_rtn_bf16", VGPR_32>;
} // End SubtargetPredicate = HasAtomicDsPkAdd16Insts

defm DS_CMPSTORE_B32     : DS_1A2D_NORET_mc<"ds_cmpstore_b32">;
defm DS_CMPSTORE_F32     : DS_1A2D_NORET_mc<"ds_cmpstore_f32">;
defm DS_CMPSTORE_B64     : DS_1A2D_NORET_mc<"ds_cmpstore_b64", VReg_64>;
defm DS_CMPSTORE_F64     : DS_1A2D_NORET_mc<"ds_cmpstore_f64", VReg_64>;
defm DS_CMPSTORE_RTN_B32 : DS_1A2D_RET_mc<"ds_cmpstore_rtn_b32", VGPR_32>;
defm DS_CMPSTORE_RTN_F32 : DS_1A2D_RET_mc<"ds_cmpstore_rtn_f32", VGPR_32>;
defm DS_CMPSTORE_RTN_B64  : DS_1A2D_RET_mc<"ds_cmpstore_rtn_b64", VReg_64>;
defm DS_CMPSTORE_RTN_F64  : DS_1A2D_RET_mc<"ds_cmpstore_rtn_f64", VReg_64>;

defm DS_MSKOR_B32     : DS_1A2D_NORET_mc<"ds_mskor_b32">;
defm DS_CMPST_B32     : DS_1A2D_NORET_mc<"ds_cmpst_b32">;
defm DS_CMPST_F32     : DS_1A2D_NORET_mc<"ds_cmpst_f32">;

defm DS_ADD_U64       : DS_1A1D_NORET_mc<"ds_add_u64", VReg_64>;
defm DS_SUB_U64       : DS_1A1D_NORET_mc<"ds_sub_u64", VReg_64>;
defm DS_RSUB_U64      : DS_1A1D_NORET_mc<"ds_rsub_u64", VReg_64>;
defm DS_INC_U64       : DS_1A1D_NORET_mc<"ds_inc_u64", VReg_64>;
defm DS_DEC_U64       : DS_1A1D_NORET_mc<"ds_dec_u64", VReg_64>;
defm DS_MIN_I64       : DS_1A1D_NORET_mc<"ds_min_i64", VReg_64>;
defm DS_MAX_I64       : DS_1A1D_NORET_mc<"ds_max_i64", VReg_64>;
defm DS_MIN_U64       : DS_1A1D_NORET_mc<"ds_min_u64", VReg_64>;
defm DS_MAX_U64       : DS_1A1D_NORET_mc<"ds_max_u64", VReg_64>;
defm DS_AND_B64       : DS_1A1D_NORET_mc<"ds_and_b64", VReg_64>;
defm DS_OR_B64        : DS_1A1D_NORET_mc<"ds_or_b64", VReg_64>;
defm DS_XOR_B64       : DS_1A1D_NORET_mc<"ds_xor_b64", VReg_64>;
defm DS_MSKOR_B64     : DS_1A2D_NORET_mc<"ds_mskor_b64", VReg_64>;
let mayLoad = 0 in {
defm DS_WRITE_B64     : DS_1A1D_NORET_mc<"ds_write_b64", VReg_64>;
defm DS_WRITE2_B64    : DS_1A2D_Off8_NORET_mc<"ds_write2_b64", VReg_64>;
defm DS_WRITE2ST64_B64: DS_1A2D_Off8_NORET_mc<"ds_write2st64_b64", VReg_64>;
}
defm DS_CMPST_B64     : DS_1A2D_NORET_mc<"ds_cmpst_b64", VReg_64>;
defm DS_CMPST_F64     : DS_1A2D_NORET_mc<"ds_cmpst_f64", VReg_64>;
defm DS_MIN_F64       : DS_1A1D_NORET_mc<"ds_min_f64", VReg_64>;
defm DS_MAX_F64       : DS_1A1D_NORET_mc<"ds_max_f64", VReg_64>;

defm DS_ADD_RTN_U32   : DS_1A1D_RET_mc<"ds_add_rtn_u32", VGPR_32>;

let SubtargetPredicate = HasLDSFPAtomicAddF32 in {
defm DS_ADD_RTN_F32   : DS_1A1D_RET_mc<"ds_add_rtn_f32", VGPR_32>;
}
defm DS_SUB_RTN_U32   : DS_1A1D_RET_mc<"ds_sub_rtn_u32", VGPR_32>;
defm DS_RSUB_RTN_U32  : DS_1A1D_RET_mc<"ds_rsub_rtn_u32", VGPR_32>;
defm DS_INC_RTN_U32   : DS_1A1D_RET_mc<"ds_inc_rtn_u32", VGPR_32>;
defm DS_DEC_RTN_U32   : DS_1A1D_RET_mc<"ds_dec_rtn_u32", VGPR_32>;
defm DS_MIN_RTN_I32   : DS_1A1D_RET_mc<"ds_min_rtn_i32", VGPR_32>;
defm DS_MAX_RTN_I32   : DS_1A1D_RET_mc<"ds_max_rtn_i32", VGPR_32>;
defm DS_MIN_RTN_U32   : DS_1A1D_RET_mc<"ds_min_rtn_u32", VGPR_32>;
defm DS_MAX_RTN_U32   : DS_1A1D_RET_mc<"ds_max_rtn_u32", VGPR_32>;
defm DS_AND_RTN_B32   : DS_1A1D_RET_mc<"ds_and_rtn_b32", VGPR_32>;
defm DS_OR_RTN_B32    : DS_1A1D_RET_mc<"ds_or_rtn_b32", VGPR_32>;
defm DS_XOR_RTN_B32   : DS_1A1D_RET_mc<"ds_xor_rtn_b32", VGPR_32>;
defm DS_MSKOR_RTN_B32 : DS_1A2D_RET_mc<"ds_mskor_rtn_b32", VGPR_32>;
defm DS_CMPST_RTN_B32 : DS_1A2D_RET_mc<"ds_cmpst_rtn_b32", VGPR_32>;
defm DS_CMPST_RTN_F32 : DS_1A2D_RET_mc<"ds_cmpst_rtn_f32", VGPR_32>;
defm DS_MIN_RTN_F32   : DS_1A1D_RET_mc<"ds_min_rtn_f32", VGPR_32>;
defm DS_MAX_RTN_F32   : DS_1A1D_RET_mc<"ds_max_rtn_f32", VGPR_32>;

defm DS_WRXCHG_RTN_B32 : DS_1A1D_RET_mc<"ds_wrxchg_rtn_b32">;
defm DS_WRXCHG2_RTN_B32 : DS_1A2D_Off8_RET_mc<"ds_wrxchg2_rtn_b32", VReg_64, VGPR_32>;
defm DS_WRXCHG2ST64_RTN_B32 : DS_1A2D_Off8_RET_mc<"ds_wrxchg2st64_rtn_b32", VReg_64, VGPR_32>;

defm DS_ADD_RTN_U64  : DS_1A1D_RET_mc<"ds_add_rtn_u64", VReg_64>;
defm DS_SUB_RTN_U64  : DS_1A1D_RET_mc<"ds_sub_rtn_u64", VReg_64>;
defm DS_RSUB_RTN_U64  : DS_1A1D_RET_mc<"ds_rsub_rtn_u64", VReg_64>;
defm DS_INC_RTN_U64   : DS_1A1D_RET_mc<"ds_inc_rtn_u64", VReg_64>;
defm DS_DEC_RTN_U64   : DS_1A1D_RET_mc<"ds_dec_rtn_u64", VReg_64>;
defm DS_MIN_RTN_I64    : DS_1A1D_RET_mc<"ds_min_rtn_i64", VReg_64>;
defm DS_MAX_RTN_I64    : DS_1A1D_RET_mc<"ds_max_rtn_i64", VReg_64>;
defm DS_MIN_RTN_U64   : DS_1A1D_RET_mc<"ds_min_rtn_u64", VReg_64>;
defm DS_MAX_RTN_U64   : DS_1A1D_RET_mc<"ds_max_rtn_u64", VReg_64>;
defm DS_AND_RTN_B64    : DS_1A1D_RET_mc<"ds_and_rtn_b64", VReg_64>;
defm DS_OR_RTN_B64     : DS_1A1D_RET_mc<"ds_or_rtn_b64", VReg_64>;
defm DS_XOR_RTN_B64    : DS_1A1D_RET_mc<"ds_xor_rtn_b64", VReg_64>;
defm DS_MSKOR_RTN_B64  : DS_1A2D_RET_mc<"ds_mskor_rtn_b64", VReg_64>;
defm DS_CMPST_RTN_B64  : DS_1A2D_RET_mc<"ds_cmpst_rtn_b64", VReg_64>;
defm DS_CMPST_RTN_F64  : DS_1A2D_RET_mc<"ds_cmpst_rtn_f64", VReg_64>;
defm DS_MIN_RTN_F64    : DS_1A1D_RET_mc<"ds_min_rtn_f64", VReg_64>;
defm DS_MAX_RTN_F64    : DS_1A1D_RET_mc<"ds_max_rtn_f64", VReg_64>;

defm DS_WRXCHG_RTN_B64 : DS_1A1D_RET_mc<"ds_wrxchg_rtn_b64", VReg_64>;
defm DS_WRXCHG2_RTN_B64 : DS_1A2D_Off8_RET_mc<"ds_wrxchg2_rtn_b64", VReg_128, VReg_64>;
defm DS_WRXCHG2ST64_RTN_B64 : DS_1A2D_Off8_RET_mc<"ds_wrxchg2st64_rtn_b64", VReg_128, VReg_64>;

let isConvergent = 1, usesCustomInserter = 1 in {
def DS_GWS_INIT       : DS_GWS_1D<"ds_gws_init"> {
  let mayLoad = 0;
}
def DS_GWS_SEMA_V     : DS_GWS_0D<"ds_gws_sema_v">;
def DS_GWS_SEMA_BR    : DS_GWS_1D<"ds_gws_sema_br">;
def DS_GWS_SEMA_P     : DS_GWS_0D<"ds_gws_sema_p">;
def DS_GWS_BARRIER    : DS_GWS_1D<"ds_gws_barrier">;
}

let SubtargetPredicate = HasDsSrc2Insts in {
def DS_ADD_SRC2_U32   : DS_1A<"ds_add_src2_u32">;
def DS_SUB_SRC2_U32   : DS_1A<"ds_sub_src2_u32">;
def DS_RSUB_SRC2_U32  : DS_1A<"ds_rsub_src2_u32">;
def DS_INC_SRC2_U32   : DS_1A<"ds_inc_src2_u32">;
def DS_DEC_SRC2_U32   : DS_1A<"ds_dec_src2_u32">;
def DS_MIN_SRC2_I32   : DS_1A<"ds_min_src2_i32">;
def DS_MAX_SRC2_I32   : DS_1A<"ds_max_src2_i32">;
def DS_MIN_SRC2_U32   : DS_1A<"ds_min_src2_u32">;
def DS_MAX_SRC2_U32   : DS_1A<"ds_max_src2_u32">;
def DS_AND_SRC2_B32   : DS_1A<"ds_and_src2_b32">;
def DS_OR_SRC2_B32    : DS_1A<"ds_or_src2_b32">;
def DS_XOR_SRC2_B32   : DS_1A<"ds_xor_src2_b32">;
def DS_MIN_SRC2_F32   : DS_1A<"ds_min_src2_f32">;
def DS_MAX_SRC2_F32   : DS_1A<"ds_max_src2_f32">;

def DS_ADD_SRC2_U64   : DS_1A<"ds_add_src2_u64">;
def DS_SUB_SRC2_U64   : DS_1A<"ds_sub_src2_u64">;
def DS_RSUB_SRC2_U64  : DS_1A<"ds_rsub_src2_u64">;
def DS_INC_SRC2_U64   : DS_1A<"ds_inc_src2_u64">;
def DS_DEC_SRC2_U64   : DS_1A<"ds_dec_src2_u64">;
def DS_MIN_SRC2_I64   : DS_1A<"ds_min_src2_i64">;
def DS_MAX_SRC2_I64   : DS_1A<"ds_max_src2_i64">;
def DS_MIN_SRC2_U64   : DS_1A<"ds_min_src2_u64">;
def DS_MAX_SRC2_U64   : DS_1A<"ds_max_src2_u64">;
def DS_AND_SRC2_B64   : DS_1A<"ds_and_src2_b64">;
def DS_OR_SRC2_B64    : DS_1A<"ds_or_src2_b64">;
def DS_XOR_SRC2_B64   : DS_1A<"ds_xor_src2_b64">;
def DS_MIN_SRC2_F64   : DS_1A<"ds_min_src2_f64">;
def DS_MAX_SRC2_F64   : DS_1A<"ds_max_src2_f64">;

def DS_WRITE_SRC2_B32 : DS_1A<"ds_write_src2_b32">;
def DS_WRITE_SRC2_B64 : DS_1A<"ds_write_src2_b64">;
} // End SubtargetPredicate = HasDsSrc2Insts

let Uses = [EXEC], mayLoad = 0, mayStore = 0, isConvergent = 1 in {
def DS_SWIZZLE_B32 : DS_1A_RET <"ds_swizzle_b32", VGPR_32, 0, Swizzle>;
}

let mayStore = 0 in {
defm DS_READ_I8      : DS_1A_RET_mc<"ds_read_i8">;
defm DS_READ_U8      : DS_1A_RET_mc<"ds_read_u8">;
defm DS_READ_I16     : DS_1A_RET_mc<"ds_read_i16">;
defm DS_READ_U16     : DS_1A_RET_mc<"ds_read_u16">;
defm DS_READ_B32     : DS_1A_RET_mc<"ds_read_b32">;
defm DS_READ_B64     : DS_1A_RET_mc<"ds_read_b64", VReg_64>;

defm DS_READ2_B32    : DS_1A_Off8_RET_mc<"ds_read2_b32", VReg_64>;
defm DS_READ2ST64_B32: DS_1A_Off8_RET_mc<"ds_read2st64_b32", VReg_64>;

defm DS_READ2_B64    : DS_1A_Off8_RET_mc<"ds_read2_b64", VReg_128>;
defm DS_READ2ST64_B64: DS_1A_Off8_RET_mc<"ds_read2st64_b64", VReg_128>;

let has_m0_read = 0 in {
let SubtargetPredicate = HasD16LoadStore, TiedSourceNotRead = 1 in {
def DS_READ_U8_D16     : DS_1A_RET_Tied<"ds_read_u8_d16">;
def DS_READ_U8_D16_HI  : DS_1A_RET_Tied<"ds_read_u8_d16_hi">;
def DS_READ_I8_D16     : DS_1A_RET_Tied<"ds_read_i8_d16">;
def DS_READ_I8_D16_HI  : DS_1A_RET_Tied<"ds_read_i8_d16_hi">;
def DS_READ_U16_D16    : DS_1A_RET_Tied<"ds_read_u16_d16">;
def DS_READ_U16_D16_HI : DS_1A_RET_Tied<"ds_read_u16_d16_hi">;
}
} // End has_m0_read = 0

let SubtargetPredicate = HasDSAddTid in {
def DS_READ_ADDTID_B32 : DS_0A_RET<"ds_read_addtid_b32">;
}

} // End mayStore = 0

def DS_CONSUME       : DS_0A_RET<"ds_consume">;
def DS_APPEND        : DS_0A_RET<"ds_append">;

let SubtargetPredicate = isNotGFX90APlus in
def DS_ORDERED_COUNT : DS_1A_RET_GDS<"ds_ordered_count">;

//===----------------------------------------------------------------------===//
// Instruction definitions for CI and newer.
//===----------------------------------------------------------------------===//

let SubtargetPredicate = isGFX7Plus in {

defm DS_WRAP_RTN_B32 : DS_1A2D_RET_mc<"ds_wrap_rtn_b32", VGPR_32>;
defm DS_CONDXCHG32_RTN_B64 : DS_1A1D_RET_mc<"ds_condxchg32_rtn_b64", VReg_64>;

let isConvergent = 1, usesCustomInserter = 1 in {
def DS_GWS_SEMA_RELEASE_ALL : DS_GWS_0D<"ds_gws_sema_release_all">;
}

let mayStore = 0 in {
defm DS_READ_B96 : DS_1A_RET_mc<"ds_read_b96", VReg_96>;
defm DS_READ_B128: DS_1A_RET_mc<"ds_read_b128", VReg_128>;
} // End mayStore = 0

let mayLoad = 0 in {
defm DS_WRITE_B96 : DS_1A1D_NORET_mc<"ds_write_b96", VReg_96>;
defm DS_WRITE_B128 : DS_1A1D_NORET_mc<"ds_write_b128", VReg_128>;
} // End mayLoad = 0

def DS_NOP : DS_VOID<"ds_nop">;

} // let SubtargetPredicate = isGFX7Plus

//===----------------------------------------------------------------------===//
// Instruction definitions for VI and newer.
//===----------------------------------------------------------------------===//

let SubtargetPredicate = isGFX8Plus in {

let Uses = [EXEC] in {
def DS_PERMUTE_B32  : DS_1A1D_PERMUTE <"ds_permute_b32",
                                       int_amdgcn_ds_permute>;
def DS_BPERMUTE_B32 : DS_1A1D_PERMUTE <"ds_bpermute_b32",
                                       int_amdgcn_ds_bpermute>;
}

} // let SubtargetPredicate = isGFX8Plus

let SubtargetPredicate = HasLDSFPAtomicAddF32, OtherPredicates = [HasDsSrc2Insts] in {
def DS_ADD_SRC2_F32 : DS_1A<"ds_add_src2_f32">;
}


//===----------------------------------------------------------------------===//
// Instruction definitions for GFX11.
//===----------------------------------------------------------------------===//

let SubtargetPredicate = isGFX11Only in {

def DS_ADD_GS_REG_RTN : DS_0A1D_RET_GDS<"ds_add_gs_reg_rtn", VReg_64, VGPR_32>;
def DS_SUB_GS_REG_RTN : DS_0A1D_RET_GDS<"ds_sub_gs_reg_rtn", VReg_64, VGPR_32>;

} // let SubtargetPredicate = isGFX11Only

let SubtargetPredicate = isGFX11Plus in {

let OtherPredicates = [HasImageInsts] in
def DS_BVH_STACK_RTN_B32 : DS_BVH_STACK<"ds_bvh_stack_rtn_b32">;

} // let SubtargetPredicate = isGFX11Plus

//===----------------------------------------------------------------------===//
// Instruction definitions for GFX12 and newer.
//===----------------------------------------------------------------------===//

let SubtargetPredicate = isGFX12Plus in {

defm DS_COND_SUB_U32      : DS_1A1D_NORET_mc<"ds_cond_sub_u32">;
defm DS_COND_SUB_RTN_U32  : DS_1A1D_RET_mc<"ds_cond_sub_rtn_u32", VGPR_32>;
defm DS_SUB_CLAMP_U32     : DS_1A1D_NORET_mc<"ds_sub_clamp_u32">;
defm DS_SUB_CLAMP_RTN_U32 : DS_1A1D_RET_mc<"ds_sub_clamp_rtn_u32", VGPR_32>;

multiclass DSAtomicRetNoRetPatIntrinsic_mc<DS_Pseudo inst, DS_Pseudo noRetInst,
                                  ValueType vt, string frag> {
  def : DSAtomicRetPat<inst, vt,
                        !cast<PatFrag>(frag#"_local_addrspace")>;

  let OtherPredicates = [HasAtomicCSubNoRtnInsts] in
    def : DSAtomicRetPat<noRetInst, vt,
                          !cast<PatFrag>(frag#"_noret_local_addrspace"), /* complexity */ 1>;
}

defm : DSAtomicRetNoRetPatIntrinsic_mc<DS_COND_SUB_RTN_U32, DS_COND_SUB_U32, i32, "int_amdgcn_atomic_cond_sub_u32">;
} // let SubtargetPredicate = isGFX12Plus

//===----------------------------------------------------------------------===//
// DS Patterns
//===----------------------------------------------------------------------===//

def : GCNPat <
  (int_amdgcn_ds_swizzle i32:$src, timm:$offset16),
  (DS_SWIZZLE_B32 VGPR_32:$src, (as_i16timm $offset16), (i1 0))
>;

class DSReadPat <DS_Pseudo inst, ValueType vt, PatFrag frag, int gds=0> : GCNPat <
  (vt (frag (DS1Addr1Offset i32:$ptr, i32:$offset))),
  (inst $ptr, Offset:$offset, (i1 gds))
>;

multiclass DSReadPat_mc<DS_Pseudo inst, ValueType vt, string frag> {

  let OtherPredicates = [LDSRequiresM0Init] in {
    def : DSReadPat<inst, vt, !cast<PatFrag>(frag#"_m0")>;
  }

  let OtherPredicates = [NotLDSRequiresM0Init] in {
    def : DSReadPat<!cast<DS_Pseudo>(!cast<string>(inst)#"_gfx9"), vt, !cast<PatFrag>(frag)>;
  }
}

class DSReadPat_D16 <DS_Pseudo inst, PatFrag frag, ValueType vt> : GCNPat <
  (frag (DS1Addr1Offset i32:$ptr, i32:$offset), vt:$in),
  (inst $ptr, Offset:$offset, (i1 0), $in)
>;

defm : DSReadPat_mc <DS_READ_I8, i32, "sextloadi8_local">;
defm : DSReadPat_mc <DS_READ_I8,  i16, "sextloadi8_local">;
defm : DSReadPat_mc <DS_READ_U8,  i32, "extloadi8_local">;
defm : DSReadPat_mc <DS_READ_U8,  i32, "zextloadi8_local">;
defm : DSReadPat_mc <DS_READ_U8,  i16, "extloadi8_local">;
defm : DSReadPat_mc <DS_READ_U8,  i16, "zextloadi8_local">;
defm : DSReadPat_mc <DS_READ_I16, i32, "sextloadi16_local">;
defm : DSReadPat_mc <DS_READ_I16, i32, "sextloadi16_local">;
defm : DSReadPat_mc <DS_READ_U16, i32, "extloadi16_local">;
defm : DSReadPat_mc <DS_READ_U16, i32, "zextloadi16_local">;
defm : DSReadPat_mc <DS_READ_U16, i16, "load_local">;

foreach vt = Reg32Types.types in {
defm : DSReadPat_mc <DS_READ_B32, vt, "load_local">;
}

defm : DSReadPat_mc <DS_READ_U8, i16, "atomic_load_8_local">;
defm : DSReadPat_mc <DS_READ_U8, i32, "atomic_load_8_local">;
defm : DSReadPat_mc <DS_READ_U16, i16, "atomic_load_16_local">;
defm : DSReadPat_mc <DS_READ_U16, i32, "atomic_load_16_local">;
defm : DSReadPat_mc <DS_READ_B32, i32, "atomic_load_32_local">;
defm : DSReadPat_mc <DS_READ_B64, i64, "atomic_load_64_local">;

let OtherPredicates = [D16PreservesUnusedBits] in {
def : DSReadPat_D16<DS_READ_U16_D16_HI, load_d16_hi_local, v2i16>;
def : DSReadPat_D16<DS_READ_U16_D16_HI, load_d16_hi_local, v2f16>;
def : DSReadPat_D16<DS_READ_U8_D16_HI, az_extloadi8_d16_hi_local, v2i16>;
def : DSReadPat_D16<DS_READ_U8_D16_HI, az_extloadi8_d16_hi_local, v2f16>;
def : DSReadPat_D16<DS_READ_I8_D16_HI, sextloadi8_d16_hi_local, v2i16>;
def : DSReadPat_D16<DS_READ_I8_D16_HI, sextloadi8_d16_hi_local, v2f16>;

def : DSReadPat_D16<DS_READ_U16_D16, load_d16_lo_local, v2i16>;
def : DSReadPat_D16<DS_READ_U16_D16, load_d16_lo_local, v2f16>;
def : DSReadPat_D16<DS_READ_U8_D16, az_extloadi8_d16_lo_local, v2i16>;
def : DSReadPat_D16<DS_READ_U8_D16, az_extloadi8_d16_lo_local, v2f16>;
def : DSReadPat_D16<DS_READ_I8_D16, sextloadi8_d16_lo_local, v2i16>;
def : DSReadPat_D16<DS_READ_I8_D16, sextloadi8_d16_lo_local, v2f16>;
}

class DSWritePat <DS_Pseudo inst, ValueType vt, PatFrag frag, int gds=0> : GCNPat <
  (frag vt:$value, (DS1Addr1Offset i32:$ptr, i32:$offset)),
  (inst $ptr, getVregSrcForVT<vt>.ret:$value, Offset:$offset, (i1 gds))
>;

multiclass DSWritePat_mc <DS_Pseudo inst, ValueType vt, string frag> {
  let OtherPredicates = [LDSRequiresM0Init] in {
    def : DSWritePat<inst, vt, !cast<PatFrag>(frag#"_m0")>;
  }

  let OtherPredicates = [NotLDSRequiresM0Init] in {
    def : DSWritePat<!cast<DS_Pseudo>(!cast<string>(inst)#"_gfx9"), vt, !cast<PatFrag>(frag)>;
  }
}

defm : DSWritePat_mc <DS_WRITE_B8, i32, "truncstorei8_local">;
defm : DSWritePat_mc <DS_WRITE_B16, i32, "truncstorei16_local">;
defm : DSWritePat_mc <DS_WRITE_B8, i16, "truncstorei8_local">;
defm : DSWritePat_mc <DS_WRITE_B16, i16, "store_local">;

foreach vt = Reg32Types.types in {
defm : DSWritePat_mc <DS_WRITE_B32, vt, "store_local">;
}

defm : DSWritePat_mc <DS_WRITE_B8, i16, "atomic_store_8_local">;
defm : DSWritePat_mc <DS_WRITE_B8, i32, "atomic_store_8_local">;
defm : DSWritePat_mc <DS_WRITE_B16, i16, "atomic_store_16_local">;
defm : DSWritePat_mc <DS_WRITE_B16, i32, "atomic_store_16_local">;
defm : DSWritePat_mc <DS_WRITE_B32, i32, "atomic_store_32_local">;
defm : DSWritePat_mc <DS_WRITE_B64, i64, "atomic_store_64_local">;

let OtherPredicates = [HasD16LoadStore] in {
def : DSWritePat <DS_WRITE_B16_D16_HI, i32, store_hi16_local>;
def : DSWritePat <DS_WRITE_B8_D16_HI, i32, truncstorei8_hi16_local>;
}

class DS64Bit4ByteAlignedReadPat<DS_Pseudo inst, ValueType vt, PatFrag frag> : GCNPat <
  (vt:$value (frag (DS64Bit4ByteAligned i32:$ptr, i8:$offset0, i8:$offset1))),
  (inst $ptr, $offset0, $offset1, (i1 0))
>;

class DS64Bit4ByteAlignedWritePat<DS_Pseudo inst, ValueType vt, PatFrag frag> : GCNPat<
  (frag vt:$value, (DS64Bit4ByteAligned i32:$ptr, i8:$offset0, i8:$offset1)),
  (inst $ptr, (i32 (EXTRACT_SUBREG VReg_64:$value, sub0)),
              (i32 (EXTRACT_SUBREG VReg_64:$value, sub1)), $offset0, $offset1,
              (i1 0))
>;

class DS128Bit8ByteAlignedReadPat<DS_Pseudo inst, ValueType vt, PatFrag frag> : GCNPat <
  (vt:$value (frag (DS128Bit8ByteAligned i32:$ptr, i8:$offset0, i8:$offset1))),
  (inst $ptr, $offset0, $offset1, (i1 0))
>;

class DS128Bit8ByteAlignedWritePat<DS_Pseudo inst, ValueType vt, PatFrag frag> : GCNPat<
  (frag vt:$value, (DS128Bit8ByteAligned i32:$ptr, i8:$offset0, i8:$offset1)),
  (inst $ptr, (i64 (EXTRACT_SUBREG VReg_128:$value, sub0_sub1)),
              (i64 (EXTRACT_SUBREG VReg_128:$value, sub2_sub3)), $offset0, $offset1,
              (i1 0))
>;

multiclass DS64Bit4ByteAlignedPat_mc<ValueType vt> {
  let OtherPredicates = [LDSRequiresM0Init, isGFX7Plus] in {
    def : DS64Bit4ByteAlignedReadPat<DS_READ2_B32, vt, load_local_m0>;
    def : DS64Bit4ByteAlignedWritePat<DS_WRITE2_B32, vt, store_local_m0>;
  }

  let OtherPredicates = [NotLDSRequiresM0Init] in {
    def : DS64Bit4ByteAlignedReadPat<DS_READ2_B32_gfx9, vt, load_local>;
    def : DS64Bit4ByteAlignedWritePat<DS_WRITE2_B32_gfx9, vt, store_local>;
  }
}

multiclass DS128Bit8ByteAlignedPat_mc<ValueType vt> {
  let OtherPredicates = [LDSRequiresM0Init, isGFX7Plus] in {
    def : DS128Bit8ByteAlignedReadPat<DS_READ2_B64, vt, load_local_m0>;
    def : DS128Bit8ByteAlignedWritePat<DS_WRITE2_B64, vt, store_local_m0>;
  }

  let OtherPredicates = [NotLDSRequiresM0Init] in {
    def : DS128Bit8ByteAlignedReadPat<DS_READ2_B64_gfx9, vt, load_local>;
    def : DS128Bit8ByteAlignedWritePat<DS_WRITE2_B64_gfx9, vt, store_local>;
  }
}

// v2i32 loads are split into i32 loads on SI during lowering, due to a bug
// related to bounds checking.
foreach vt = VReg_64.RegTypes in {
defm : DS64Bit4ByteAlignedPat_mc<vt>;
}

foreach vt = VReg_128.RegTypes in {
defm : DS128Bit8ByteAlignedPat_mc<vt>;
}

// Prefer ds_read over ds_read2 and ds_write over ds_write2, all other things
// being equal, because it has a larger immediate offset range.
let AddedComplexity = 100 in {

foreach vt = VReg_64.RegTypes in {
defm : DSReadPat_mc <DS_READ_B64, vt, "load_align8_local">;
defm : DSWritePat_mc <DS_WRITE_B64, vt, "store_align8_local">;
}

let SubtargetPredicate = isGFX7Plus in {

foreach vt = VReg_96.RegTypes in {
defm : DSReadPat_mc <DS_READ_B96, vt, "load_align16_local">;
defm : DSWritePat_mc <DS_WRITE_B96, vt, "store_align16_local">;
}

foreach vt = VReg_128.RegTypes in {
defm : DSReadPat_mc <DS_READ_B128, vt, "load_align16_local">;
defm : DSWritePat_mc <DS_WRITE_B128, vt, "store_align16_local">;
}

let SubtargetPredicate = HasUnalignedAccessMode in {

// Select 64 bit loads and stores aligned less than 4 as a single ds_read_b64/
// ds_write_b64 instruction as this is faster than ds_read2_b32/ds_write2_b32
// which would be used otherwise. In this case a b32 access would still be
// misaligned, but we will have 2 of them.
foreach vt = VReg_64.RegTypes in {
defm : DSReadPat_mc <DS_READ_B64, vt, "load_align_less_than_4_local">;
defm : DSWritePat_mc <DS_WRITE_B64, vt, "store_align_less_than_4_local">;
}

// Selection will split most of the unaligned 3 dword accesses due to performance
// reasons when beneficial. Keep these two patterns for the rest of the cases.
foreach vt = VReg_96.RegTypes in {
defm : DSReadPat_mc <DS_READ_B96, vt, "load_local">;
defm : DSWritePat_mc <DS_WRITE_B96, vt, "store_local">;
}

// Select 128 bit loads and stores aligned less than 4 as a single ds_read_b128/
// ds_write_b128 instruction as this is faster than ds_read2_b64/ds_write2_b64
// which would be used otherwise. In this case a b64 access would still be
// misaligned, but we will have 2 of them.
foreach vt = VReg_128.RegTypes in {
defm : DSReadPat_mc <DS_READ_B128, vt, "load_align_less_than_4_local">;
defm : DSWritePat_mc <DS_WRITE_B128, vt, "store_align_less_than_4_local">;
}

} // End SubtargetPredicate = HasUnalignedAccessMode

} // End SubtargetPredicate = isGFX7Plus

} // End AddedComplexity = 100

multiclass DSAtomicRetPat_mc<DS_Pseudo inst, ValueType vt, string frag> {
  let OtherPredicates = [LDSRequiresM0Init] in {
    def : DSAtomicRetPat<inst, vt, !cast<PatFrag>(frag#"_local_m0_"#vt)>;
  }

  let OtherPredicates = [NotLDSRequiresM0Init] in {
    def : DSAtomicRetPat<!cast<DS_Pseudo>(!cast<string>(inst)#"_gfx9"), vt,
                         !cast<PatFrag>(frag#"_local_"#vt)>;
  }

  let OtherPredicates = [HasGDS] in {
    def : DSAtomicRetPat<inst, vt, !cast<PatFrag>(frag#"_region_m0_"#vt),
                         /* complexity */ 0, /* gds */ 1>;
  }
}

multiclass DSAtomicRetNoRetPat_mc<DS_Pseudo inst, DS_Pseudo noRetInst,
                                  ValueType vt, string frag> {
  let OtherPredicates = [LDSRequiresM0Init] in {
    def : DSAtomicRetPat<inst, vt,
                         !cast<PatFrag>(frag#"_local_m0_"#vt)>;
    def : DSAtomicRetPat<noRetInst, vt,
                         !cast<PatFrag>(frag#"_local_m0_noret_"#vt), /* complexity */ 1>;
  }

  let OtherPredicates = [NotLDSRequiresM0Init] in {
    def : DSAtomicRetPat<!cast<DS_Pseudo>(!cast<string>(inst)#"_gfx9"), vt,
                         !cast<PatFrag>(frag#"_local_"#vt)>;
    def : DSAtomicRetPat<!cast<DS_Pseudo>(!cast<string>(noRetInst)#"_gfx9"), vt,
                         !cast<PatFrag>(frag#"_local_noret_"#vt), /* complexity */ 1>;
  }

  let OtherPredicates = [HasGDS] in {
    def : DSAtomicRetPat<inst, vt,
                         !cast<PatFrag>(frag#"_region_m0_"#vt),
                         /* complexity */ 0, /* gds */ 1>;
    def : DSAtomicRetPat<noRetInst, vt,
                         !cast<PatFrag>(frag#"_region_m0_noret_"#vt),
                         /* complexity */ 1, /* gds */ 1>;
  }
}



let SubtargetPredicate = isGFX6GFX7GFX8GFX9GFX10 in {
// Caution, the order of src and cmp is the *opposite* of the BUFFER_ATOMIC_CMPSWAP opcode.
class DSAtomicCmpXChgSwapped<DS_Pseudo inst, ValueType vt, PatFrag frag,
  int complexity = 0, bit gds=0> : GCNPat<
  (frag (DS1Addr1Offset i32:$ptr, i32:$offset), vt:$cmp, vt:$swap),
  (inst $ptr, getVregSrcForVT<vt>.ret:$cmp, getVregSrcForVT<vt>.ret:$swap, Offset:$offset, (i1 gds))> {
  let AddedComplexity = complexity;
}

multiclass DSAtomicCmpXChgSwapped_mc<DS_Pseudo inst, DS_Pseudo noRetInst, ValueType vt,
                                     string frag> {
  let OtherPredicates = [LDSRequiresM0Init] in {
    def : DSAtomicCmpXChgSwapped<inst, vt, !cast<PatFrag>(frag#"_local_m0_"#vt)>;
    def : DSAtomicCmpXChgSwapped<noRetInst, vt, !cast<PatFrag>(frag#"_local_m0_noret_"#vt),
                                 /* complexity */ 1>;
  }

  let OtherPredicates = [NotLDSRequiresM0Init] in {
    def : DSAtomicCmpXChgSwapped<!cast<DS_Pseudo>(!cast<string>(inst)#"_gfx9"), vt,
                                 !cast<PatFrag>(frag#"_local_"#vt)>;
    def : DSAtomicCmpXChgSwapped<!cast<DS_Pseudo>(!cast<string>(noRetInst)#"_gfx9"), vt,
                                 !cast<PatFrag>(frag#"_local_noret_"#vt),
                                 /* complexity */ 1>;
  }

  let OtherPredicates = [HasGDS] in {
    def : DSAtomicCmpXChgSwapped<inst, vt, !cast<PatFrag>(frag#"_region_m0_"#vt),
                                 /* complexity */ 0, /* gds */ 1>;
    def : DSAtomicCmpXChgSwapped<noRetInst, vt, !cast<PatFrag>(frag#"_region_m0_noret_"#vt),
                                 /* complexity */ 1, /* gds */ 1>;
  }
}
} // End SubtargetPredicate = isGFX6GFX7GFX8GFX9GFX10

let SubtargetPredicate = isGFX11Plus in {
// The order of src and cmp agrees with the BUFFER_ATOMIC_CMPSWAP opcode.
class DSAtomicCmpXChg<DS_Pseudo inst, ValueType vt, PatFrag frag,
  int complexity = 0, bit gds=0> : GCNPat<
  (frag (DS1Addr1Offset i32:$ptr, i32:$offset), vt:$cmp, vt:$swap),
  (inst $ptr, getVregSrcForVT<vt>.ret:$swap, getVregSrcForVT<vt>.ret:$cmp, Offset:$offset, (i1 gds))> {
  let AddedComplexity = complexity;
}

multiclass DSAtomicCmpXChg_mc<DS_Pseudo inst, DS_Pseudo noRetInst, ValueType vt, string frag> {

  def : DSAtomicCmpXChg<!cast<DS_Pseudo>(!cast<string>(inst)#"_gfx9"), vt,
                        !cast<PatFrag>(frag#"_local_"#vt)>;
  def : DSAtomicCmpXChg<!cast<DS_Pseudo>(!cast<string>(noRetInst)#"_gfx9"), vt,
                        !cast<PatFrag>(frag#"_local_noret_"#vt), /* complexity */ 1>;

  let OtherPredicates = [HasGDS] in {
    def : DSAtomicCmpXChg<inst, vt, !cast<PatFrag>(frag#"_region_m0_"#vt),
                          /* complexity */ 0, /* gds */ 1>;
    def : DSAtomicCmpXChg<noRetInst, vt, !cast<PatFrag>(frag#"_region_m0_noret_"#vt),
                          /* complexity */ 1, /* gds */ 1>;
  }
}
} // End SubtargetPredicate = isGFX11Plus

// 32-bit atomics.
defm : DSAtomicRetPat_mc<DS_WRXCHG_RTN_B32, i32, "atomic_swap">;
defm : DSAtomicRetNoRetPat_mc<DS_ADD_RTN_U32, DS_ADD_U32, i32, "atomic_load_add">;
defm : DSAtomicRetNoRetPat_mc<DS_SUB_RTN_U32, DS_SUB_U32, i32, "atomic_load_sub">;
defm : DSAtomicRetNoRetPat_mc<DS_INC_RTN_U32, DS_INC_U32, i32, "atomic_load_uinc_wrap">;
defm : DSAtomicRetNoRetPat_mc<DS_DEC_RTN_U32, DS_DEC_U32, i32, "atomic_load_udec_wrap">;
defm : DSAtomicRetNoRetPat_mc<DS_AND_RTN_B32, DS_AND_B32, i32, "atomic_load_and">;
defm : DSAtomicRetNoRetPat_mc<DS_OR_RTN_B32, DS_OR_B32, i32, "atomic_load_or">;
defm : DSAtomicRetNoRetPat_mc<DS_XOR_RTN_B32, DS_XOR_B32, i32, "atomic_load_xor">;
defm : DSAtomicRetNoRetPat_mc<DS_MIN_RTN_I32, DS_MIN_I32, i32, "atomic_load_min">;
defm : DSAtomicRetNoRetPat_mc<DS_MAX_RTN_I32, DS_MAX_I32, i32, "atomic_load_max">;
defm : DSAtomicRetNoRetPat_mc<DS_MIN_RTN_U32, DS_MIN_U32, i32, "atomic_load_umin">;
defm : DSAtomicRetNoRetPat_mc<DS_MAX_RTN_U32, DS_MAX_U32, i32, "atomic_load_umax">;
defm : DSAtomicRetNoRetPat_mc<DS_MIN_RTN_F32, DS_MIN_F32, f32, "atomic_load_fmin">;
defm : DSAtomicRetNoRetPat_mc<DS_MAX_RTN_F32, DS_MAX_F32, f32, "atomic_load_fmax">;


let SubtargetPredicate = HasAtomicDsPkAdd16Insts in {
defm : DSAtomicRetNoRetPat_mc<DS_PK_ADD_RTN_F16, DS_PK_ADD_F16, v2f16, "atomic_load_fadd">;
defm : DSAtomicRetNoRetPat_mc<DS_PK_ADD_RTN_BF16, DS_PK_ADD_BF16, v2bf16, "atomic_load_fadd">;
}

let SubtargetPredicate = isGFX6GFX7GFX8GFX9GFX10 in {
defm : DSAtomicCmpXChgSwapped_mc<DS_CMPST_RTN_B32, DS_CMPST_B32, i32, "atomic_cmp_swap">;
}

let SubtargetPredicate = isGFX11Plus in {
defm : DSAtomicCmpXChg_mc<DS_CMPSTORE_RTN_B32, DS_CMPSTORE_B32, i32, "atomic_cmp_swap">;
}

let SubtargetPredicate = HasLDSFPAtomicAddF32 in {
defm : DSAtomicRetNoRetPat_mc<DS_ADD_RTN_F32, DS_ADD_F32, f32, "atomic_load_fadd">;
}

// 64-bit atomics.
defm : DSAtomicRetPat_mc<DS_WRXCHG_RTN_B64, i64, "atomic_swap">;
defm : DSAtomicRetNoRetPat_mc<DS_ADD_RTN_U64, DS_ADD_U64, i64, "atomic_load_add">;
defm : DSAtomicRetNoRetPat_mc<DS_SUB_RTN_U64, DS_SUB_U64, i64, "atomic_load_sub">;
defm : DSAtomicRetNoRetPat_mc<DS_INC_RTN_U64, DS_INC_U64, i64, "atomic_load_uinc_wrap">;
defm : DSAtomicRetNoRetPat_mc<DS_DEC_RTN_U64, DS_DEC_U64, i64, "atomic_load_udec_wrap">;
defm : DSAtomicRetNoRetPat_mc<DS_AND_RTN_B64, DS_AND_B64, i64, "atomic_load_and">;
defm : DSAtomicRetNoRetPat_mc<DS_OR_RTN_B64, DS_OR_B64, i64, "atomic_load_or">;
defm : DSAtomicRetNoRetPat_mc<DS_XOR_RTN_B64, DS_XOR_B64, i64, "atomic_load_xor">;
defm : DSAtomicRetNoRetPat_mc<DS_MIN_RTN_I64, DS_MIN_I64, i64, "atomic_load_min">;
defm : DSAtomicRetNoRetPat_mc<DS_MAX_RTN_I64, DS_MAX_I64, i64, "atomic_load_max">;
defm : DSAtomicRetNoRetPat_mc<DS_MIN_RTN_U64, DS_MIN_U64, i64, "atomic_load_umin">;
defm : DSAtomicRetNoRetPat_mc<DS_MAX_RTN_U64, DS_MAX_U64, i64, "atomic_load_umax">;
defm : DSAtomicRetNoRetPat_mc<DS_MIN_RTN_F64, DS_MIN_F64, f64, "atomic_load_fmin">;
defm : DSAtomicRetNoRetPat_mc<DS_MAX_RTN_F64, DS_MAX_F64, f64, "atomic_load_fmax">;

let SubtargetPredicate = isGFX6GFX7GFX8GFX9GFX10 in {
defm : DSAtomicCmpXChgSwapped_mc<DS_CMPST_RTN_B64, DS_CMPST_B64, i64, "atomic_cmp_swap">;
} // End SubtargetPredicate = isGFX6GFX7GFX8GFX9GFX10

let SubtargetPredicate = isGFX11Plus in {
defm : DSAtomicCmpXChg_mc<DS_CMPSTORE_RTN_B64, DS_CMPSTORE_B64, i64, "atomic_cmp_swap">;
} // End SubtargetPredicate = isGFX11Plus

let SubtargetPredicate = HasLdsAtomicAddF64 in {
def : DSAtomicRetPat<DS_ADD_RTN_F64, f64, atomic_load_fadd_local_f64>;
let AddedComplexity = 1 in
def : DSAtomicRetPat<DS_ADD_F64, f64, atomic_load_fadd_local_noret_f64>;

class DSAtomicRetPatIntrinsic<DS_Pseudo inst, ValueType vt, PatFrag frag,
  bit gds=0> : GCNPat <
  (vt (frag (DS1Addr1Offset i32:$ptr, i32:$offset), vt:$value)),
  (inst $ptr, getVregSrcForVT<vt>.ret:$value, Offset:$offset, (i1 gds))> {
}

def : DSAtomicRetPatIntrinsic<DS_ADD_RTN_F64, f64, int_amdgcn_flat_atomic_fadd_local_addrspace>;
let AddedComplexity = 1 in
def : DSAtomicRetPatIntrinsic<DS_ADD_F64, f64, int_amdgcn_flat_atomic_fadd_noret_local_addrspace>;
}

let SubtargetPredicate = HasAtomicDsPkAdd16Insts in {
defm : DSAtomicRetNoRetPat_mc<DS_PK_ADD_RTN_F16, DS_PK_ADD_F16, v2f16, "atomic_load_fadd">;
} // End SubtargetPredicate = HasAtomicDsPkAdd16Insts

let OtherPredicates = [HasGDS] in
def : GCNPat <
  (SIds_ordered_count i32:$value, i16:$offset),
  (DS_ORDERED_COUNT $value, (as_i16imm $offset))
>;

def : GCNPat <
  (i64 (int_amdgcn_ds_add_gs_reg_rtn i32:$src, timm:$offset32)),
  (DS_ADD_GS_REG_RTN VGPR_32:$src, (as_i32timm $offset32))
>;

def : GCNPat <
  (i32 (int_amdgcn_ds_add_gs_reg_rtn i32:$src, timm:$offset32)),
  (EXTRACT_SUBREG
    (i64 (COPY_TO_REGCLASS
      (DS_ADD_GS_REG_RTN VGPR_32:$src, (as_i32timm $offset32)),
      VReg_64)),
    sub0)
>;

def : GCNPat <
  (i64 (int_amdgcn_ds_sub_gs_reg_rtn i32:$src, timm:$offset32)),
  (DS_SUB_GS_REG_RTN VGPR_32:$src, (as_i32timm $offset32))
>;

def : GCNPat <
  (i32 (int_amdgcn_ds_sub_gs_reg_rtn i32:$src, timm:$offset32)),
  (EXTRACT_SUBREG
    (i64 (COPY_TO_REGCLASS
      (DS_SUB_GS_REG_RTN VGPR_32:$src, (as_i32timm $offset32)),
      VReg_64)),
    sub0)
>;

//===----------------------------------------------------------------------===//
// Target-specific instruction encodings.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Base ENC_DS for GFX6, GFX7, GFX10, GFX11, GFX12.
//===----------------------------------------------------------------------===//

class Base_DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<bits<8> op, DS_Pseudo ps, int ef,
                                               string opName = ps.Mnemonic,
                                               bit hasGDS = true>
    : DS_Real<ps, opName>, SIMCInstr <ps.PseudoInstr, ef> {

  let Inst{7-0}   = !if(ps.has_offset0, offset0, 0);
  let Inst{15-8}  = !if(ps.has_offset1, offset1, 0);
  let Inst{17}    = !if(ps.has_gds, gds, ps.gdsValue);
  let Inst{25-18} = op;
  let Inst{31-26} = 0x36;
  let Inst{39-32} = !if(ps.has_addr, addr, !if(ps.has_gws_data0, data0{7-0}, 0));
  let Inst{47-40} = !if(ps.has_data0, data0{7-0}, 0);
  let Inst{55-48} = !if(ps.has_data1, data1{7-0}, 0);
  let Inst{63-56} = !if(ps.has_vdst, vdst{7-0}, 0);

  let gds = !if(hasGDS, ?, 0);
}

//===----------------------------------------------------------------------===//
// GFX12.
//===----------------------------------------------------------------------===//

multiclass DS_Real_gfx12<bits<8> op, string name = !tolower(NAME), bit needAlias = true> {
  defvar ps = !cast<DS_Pseudo>(NAME);
  let AssemblerPredicate = isGFX12Plus in {
    let DecoderNamespace = "GFX12" in
      def _gfx12 :
        Base_DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<op, ps, SIEncodingFamily.GFX12,
                                               name, /*hasGDS=*/false>;
    if !and(needAlias, !ne(ps.Mnemonic, name)) then
      def : AMDGPUMnemonicAlias<ps.Mnemonic, name>;
  } // End AssemblerPredicate
}

defm DS_MIN_F32           : DS_Real_gfx12<0x012, "ds_min_num_f32">;
defm DS_MAX_F32           : DS_Real_gfx12<0x013, "ds_max_num_f32">;
defm DS_MIN_RTN_F32       : DS_Real_gfx12<0x032, "ds_min_num_rtn_f32">;
defm DS_MAX_RTN_F32       : DS_Real_gfx12<0x033, "ds_max_num_rtn_f32">;
defm DS_MIN_F64           : DS_Real_gfx12<0x052, "ds_min_num_f64">;
defm DS_MAX_F64           : DS_Real_gfx12<0x053, "ds_max_num_f64">;
defm DS_MIN_RTN_F64       : DS_Real_gfx12<0x072, "ds_min_num_rtn_f64">;
defm DS_MAX_RTN_F64       : DS_Real_gfx12<0x073, "ds_max_num_rtn_f64">;
defm DS_COND_SUB_U32      : DS_Real_gfx12<0x098>;
defm DS_SUB_CLAMP_U32     : DS_Real_gfx12<0x099>;
defm DS_COND_SUB_RTN_U32  : DS_Real_gfx12<0x0a8>;
defm DS_SUB_CLAMP_RTN_U32 : DS_Real_gfx12<0x0a9>;
defm DS_PK_ADD_F16        : DS_Real_gfx12<0x09a>;
defm DS_PK_ADD_RTN_F16    : DS_Real_gfx12<0x0aa>;
defm DS_PK_ADD_BF16       : DS_Real_gfx12<0x09b>;
defm DS_PK_ADD_RTN_BF16   : DS_Real_gfx12<0x0ab>;

// New aliases added in GFX12 without renaming the instructions.
let AssemblerPredicate = isGFX12Plus in {
  def : AMDGPUMnemonicAlias<"ds_subrev_u32", "ds_rsub_u32">;
  def : AMDGPUMnemonicAlias<"ds_subrev_rtn_u32", "ds_rsub_rtn_u32">;
  def : AMDGPUMnemonicAlias<"ds_subrev_u64", "ds_rsub_u64">;
  def : AMDGPUMnemonicAlias<"ds_subrev_rtn_u64", "ds_rsub_rtn_u64">;
}

//===----------------------------------------------------------------------===//
// GFX11.
//===----------------------------------------------------------------------===//

multiclass DS_Real_gfx11<bits<8> op, string name = !tolower(NAME)> {
  defvar ps = !cast<DS_Pseudo>(NAME);
  let AssemblerPredicate = isGFX11Only in {
    let DecoderNamespace = "GFX11" in
      def _gfx11 :
        Base_DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<op, ps, SIEncodingFamily.GFX11,
                                               name>;
    if !ne(ps.Mnemonic, name) then
      def : AMDGPUMnemonicAlias<ps.Mnemonic, name>;
  } // End AssemblerPredicate
}

multiclass DS_Real_gfx11_gfx12<bits<8> op, string name = !tolower(NAME)>
  : DS_Real_gfx11<op, name>, DS_Real_gfx12<op, name>;

defm DS_WRITE_B32           : DS_Real_gfx11_gfx12<0x00d, "ds_store_b32">;
defm DS_WRITE2_B32          : DS_Real_gfx11_gfx12<0x00e, "ds_store_2addr_b32">;
defm DS_WRITE2ST64_B32      : DS_Real_gfx11_gfx12<0x00f, "ds_store_2addr_stride64_b32">;
defm DS_WRITE_B8            : DS_Real_gfx11_gfx12<0x01e, "ds_store_b8">;
defm DS_WRITE_B16           : DS_Real_gfx11_gfx12<0x01f, "ds_store_b16">;
defm DS_WRXCHG_RTN_B32      : DS_Real_gfx11_gfx12<0x02d, "ds_storexchg_rtn_b32">;
defm DS_WRXCHG2_RTN_B32     : DS_Real_gfx11_gfx12<0x02e, "ds_storexchg_2addr_rtn_b32">;
defm DS_WRXCHG2ST64_RTN_B32 : DS_Real_gfx11_gfx12<0x02f, "ds_storexchg_2addr_stride64_rtn_b32">;
defm DS_READ_B32            : DS_Real_gfx11_gfx12<0x036, "ds_load_b32">;
defm DS_READ2_B32           : DS_Real_gfx11_gfx12<0x037, "ds_load_2addr_b32">;
defm DS_READ2ST64_B32       : DS_Real_gfx11_gfx12<0x038, "ds_load_2addr_stride64_b32">;
defm DS_READ_I8             : DS_Real_gfx11_gfx12<0x039, "ds_load_i8">;
defm DS_READ_U8             : DS_Real_gfx11_gfx12<0x03a, "ds_load_u8">;
defm DS_READ_I16            : DS_Real_gfx11_gfx12<0x03b, "ds_load_i16">;
defm DS_READ_U16            : DS_Real_gfx11_gfx12<0x03c, "ds_load_u16">;
defm DS_WRITE_B64           : DS_Real_gfx11_gfx12<0x04d, "ds_store_b64">;
defm DS_WRITE2_B64          : DS_Real_gfx11_gfx12<0x04e, "ds_store_2addr_b64">;
defm DS_WRITE2ST64_B64      : DS_Real_gfx11_gfx12<0x04f, "ds_store_2addr_stride64_b64">;
defm DS_WRXCHG_RTN_B64      : DS_Real_gfx11_gfx12<0x06d, "ds_storexchg_rtn_b64">;
defm DS_WRXCHG2_RTN_B64     : DS_Real_gfx11_gfx12<0x06e, "ds_storexchg_2addr_rtn_b64">;
defm DS_WRXCHG2ST64_RTN_B64 : DS_Real_gfx11_gfx12<0x06f, "ds_storexchg_2addr_stride64_rtn_b64">;
defm DS_READ_B64            : DS_Real_gfx11_gfx12<0x076, "ds_load_b64">;
defm DS_READ2_B64           : DS_Real_gfx11_gfx12<0x077, "ds_load_2addr_b64">;
defm DS_READ2ST64_B64       : DS_Real_gfx11_gfx12<0x078, "ds_load_2addr_stride64_b64">;
defm DS_WRITE_B8_D16_HI     : DS_Real_gfx11_gfx12<0x0a0, "ds_store_b8_d16_hi">;
defm DS_WRITE_B16_D16_HI    : DS_Real_gfx11_gfx12<0x0a1, "ds_store_b16_d16_hi">;
defm DS_READ_U8_D16         : DS_Real_gfx11_gfx12<0x0a2, "ds_load_u8_d16">;
defm DS_READ_U8_D16_HI      : DS_Real_gfx11_gfx12<0x0a3, "ds_load_u8_d16_hi">;
defm DS_READ_I8_D16         : DS_Real_gfx11_gfx12<0x0a4, "ds_load_i8_d16">;
defm DS_READ_I8_D16_HI      : DS_Real_gfx11_gfx12<0x0a5, "ds_load_i8_d16_hi">;
defm DS_READ_U16_D16        : DS_Real_gfx11_gfx12<0x0a6, "ds_load_u16_d16">;
defm DS_READ_U16_D16_HI     : DS_Real_gfx11_gfx12<0x0a7, "ds_load_u16_d16_hi">;
defm DS_WRITE_ADDTID_B32    : DS_Real_gfx11_gfx12<0x0b0, "ds_store_addtid_b32">;
defm DS_READ_ADDTID_B32     : DS_Real_gfx11_gfx12<0x0b1, "ds_load_addtid_b32">;
defm DS_WRITE_B96           : DS_Real_gfx11_gfx12<0x0de, "ds_store_b96">;
defm DS_WRITE_B128          : DS_Real_gfx11_gfx12<0x0df, "ds_store_b128">;
defm DS_READ_B96            : DS_Real_gfx11_gfx12<0x0fe, "ds_load_b96">;
defm DS_READ_B128           : DS_Real_gfx11_gfx12<0x0ff, "ds_load_b128">;

// DS_CMPST_* are renamed to DS_CMPSTORE_* in GFX11, but also the data operands (src and cmp) are swapped
// comparing to pre-GFX11.
// Note: the mnemonic alias is not generated to avoid a potential ambiguity due to the semantics change.

defm DS_CMPSTORE_B32                     : DS_Real_gfx11_gfx12<0x010>;
defm DS_CMPSTORE_F32                     : DS_Real_gfx11<0x011>;
defm DS_CMPSTORE_RTN_B32                 : DS_Real_gfx11_gfx12<0x030>;
defm DS_CMPSTORE_RTN_F32                 : DS_Real_gfx11<0x031>;
defm DS_CMPSTORE_B64                     : DS_Real_gfx11_gfx12<0x050>;
defm DS_CMPSTORE_F64                     : DS_Real_gfx11<0x051>;
defm DS_CMPSTORE_RTN_B64                 : DS_Real_gfx11_gfx12<0x070>;
defm DS_CMPSTORE_RTN_F64                 : DS_Real_gfx11<0x071>;

defm DS_ADD_RTN_F32                      : DS_Real_gfx11_gfx12<0x079>;
defm DS_ADD_GS_REG_RTN                   : DS_Real_gfx11<0x07a>;
defm DS_SUB_GS_REG_RTN                   : DS_Real_gfx11<0x07b>;
defm DS_BVH_STACK_RTN_B32                : DS_Real_gfx11<0x0ad>;

//===----------------------------------------------------------------------===//
// GFX10.
//===----------------------------------------------------------------------===//

let AssemblerPredicate = isGFX10Only, DecoderNamespace = "GFX10" in {
  multiclass DS_Real_gfx10<bits<8> op>  {
    def _gfx10 : Base_DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<op,
      !cast<DS_Pseudo>(NAME), SIEncodingFamily.GFX10>;
  }
} // End AssemblerPredicate = isGFX10Only, DecoderNamespace = "GFX10"

defm DS_ADD_RTN_F32      : DS_Real_gfx10<0x055>;
defm DS_WRITE_B8_D16_HI  : DS_Real_gfx10<0x0a0>;
defm DS_WRITE_B16_D16_HI : DS_Real_gfx10<0x0a1>;
defm DS_READ_U8_D16      : DS_Real_gfx10<0x0a2>;
defm DS_READ_U8_D16_HI   : DS_Real_gfx10<0x0a3>;
defm DS_READ_I8_D16      : DS_Real_gfx10<0x0a4>;
defm DS_READ_I8_D16_HI   : DS_Real_gfx10<0x0a5>;
defm DS_READ_U16_D16     : DS_Real_gfx10<0x0a6>;
defm DS_READ_U16_D16_HI  : DS_Real_gfx10<0x0a7>;
defm DS_WRITE_ADDTID_B32 : DS_Real_gfx10<0x0b0>;
defm DS_READ_ADDTID_B32  : DS_Real_gfx10<0x0b1>;

//===----------------------------------------------------------------------===//
// GFX10, GFX11, GFX12.
//===----------------------------------------------------------------------===//

multiclass DS_Real_gfx10_gfx11_gfx12<bits<8> op> :
  DS_Real_gfx10<op>, DS_Real_gfx11<op>, DS_Real_gfx12<op>;

multiclass DS_Real_gfx10_gfx11<bits<8> op> :
  DS_Real_gfx10<op>, DS_Real_gfx11<op>;

defm DS_ADD_F32          : DS_Real_gfx10_gfx11_gfx12<0x015>;
defm DS_ADD_SRC2_F32     : DS_Real_gfx10<0x095>;
defm DS_PERMUTE_B32      : DS_Real_gfx10_gfx11_gfx12<0x0b2>;
defm DS_BPERMUTE_B32     : DS_Real_gfx10_gfx11_gfx12<0x0b3>;

//===----------------------------------------------------------------------===//
// GFX7, GFX10, GFX11, GFX12.
//===----------------------------------------------------------------------===//

let AssemblerPredicate = isGFX7Only, DecoderNamespace = "GFX7" in {
  multiclass DS_Real_gfx7<bits<8> op> {
    def _gfx7 : Base_DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<op,
      !cast<DS_Pseudo>(NAME), SIEncodingFamily.SI>;
  }
} // End AssemblerPredicate = isGFX7Only, DecoderNamespace = "GFX7"

multiclass DS_Real_gfx7_gfx10_gfx11_gfx12<bits<8> op> :
  DS_Real_gfx7<op>, DS_Real_gfx10_gfx11_gfx12<op>;

multiclass DS_Real_gfx7_gfx10_gfx11<bits<8> op> :
  DS_Real_gfx7<op>, DS_Real_gfx10_gfx11<op>;

multiclass DS_Real_gfx7_gfx10<bits<8> op> :
  DS_Real_gfx7<op>, DS_Real_gfx10<op>;

// FIXME-GFX7: Add tests when upstreaming this part.
defm DS_GWS_SEMA_RELEASE_ALL : DS_Real_gfx7_gfx10_gfx11<0x018>;
defm DS_WRAP_RTN_B32         : DS_Real_gfx7_gfx10_gfx11<0x034>;
defm DS_CONDXCHG32_RTN_B64   : DS_Real_gfx7_gfx10_gfx11_gfx12<0x07e>;
defm DS_WRITE_B96            : DS_Real_gfx7_gfx10<0x0de>;
defm DS_WRITE_B128           : DS_Real_gfx7_gfx10<0x0df>;
defm DS_READ_B96             : DS_Real_gfx7_gfx10<0x0fe>;
defm DS_READ_B128            : DS_Real_gfx7_gfx10<0x0ff>;

//===----------------------------------------------------------------------===//
// GFX6, GFX7, GFX10, GFX11.
//===----------------------------------------------------------------------===//

let AssemblerPredicate = isGFX6GFX7, DecoderNamespace = "GFX6GFX7" in {
  multiclass DS_Real_gfx6_gfx7<bits<8> op> {
    def _gfx6_gfx7 : Base_DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<op,
      !cast<DS_Pseudo>(NAME), SIEncodingFamily.SI>;
  }
} // End AssemblerPredicate = isGFX6GFX7, DecoderNamespace = "GFX6GFX7"

multiclass DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<bits<8> op> :
  DS_Real_gfx6_gfx7<op>, DS_Real_gfx10_gfx11_gfx12<op>;

multiclass DS_Real_gfx6_gfx7_gfx10_gfx11<bits<8> op> :
  DS_Real_gfx6_gfx7<op>, DS_Real_gfx10_gfx11<op>;

multiclass DS_Real_gfx6_gfx7_gfx10<bits<8> op> :
  DS_Real_gfx6_gfx7<op>, DS_Real_gfx10<op>;

defm DS_ADD_U32             : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x000>;
defm DS_SUB_U32             : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x001>;
defm DS_RSUB_U32            : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x002>;
defm DS_INC_U32             : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x003>;
defm DS_DEC_U32             : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x004>;
defm DS_MIN_I32             : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x005>;
defm DS_MAX_I32             : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x006>;
defm DS_MIN_U32             : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x007>;
defm DS_MAX_U32             : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x008>;
defm DS_AND_B32             : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x009>;
defm DS_OR_B32              : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x00a>;
defm DS_XOR_B32             : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x00b>;
defm DS_MSKOR_B32           : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x00c>;

defm DS_WRITE_B32           : DS_Real_gfx6_gfx7_gfx10<0x00d>;
defm DS_WRITE2_B32          : DS_Real_gfx6_gfx7_gfx10<0x00e>;
defm DS_WRITE2ST64_B32      : DS_Real_gfx6_gfx7_gfx10<0x00f>;
defm DS_CMPST_B32           : DS_Real_gfx6_gfx7_gfx10<0x010>;
defm DS_CMPST_F32           : DS_Real_gfx6_gfx7_gfx10<0x011>;

defm DS_MIN_F32             : DS_Real_gfx6_gfx7_gfx10_gfx11<0x012>;
defm DS_MAX_F32             : DS_Real_gfx6_gfx7_gfx10_gfx11<0x013>;
defm DS_NOP                 : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x014>;
defm DS_GWS_INIT            : DS_Real_gfx6_gfx7_gfx10_gfx11<0x019>;
defm DS_GWS_SEMA_V          : DS_Real_gfx6_gfx7_gfx10_gfx11<0x01a>;
defm DS_GWS_SEMA_BR         : DS_Real_gfx6_gfx7_gfx10_gfx11<0x01b>;
defm DS_GWS_SEMA_P          : DS_Real_gfx6_gfx7_gfx10_gfx11<0x01c>;
defm DS_GWS_BARRIER         : DS_Real_gfx6_gfx7_gfx10_gfx11<0x01d>;

defm DS_WRITE_B8            : DS_Real_gfx6_gfx7_gfx10<0x01e>;
defm DS_WRITE_B16           : DS_Real_gfx6_gfx7_gfx10<0x01f>;

defm DS_ADD_RTN_U32         : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x020>;
defm DS_SUB_RTN_U32         : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x021>;
defm DS_RSUB_RTN_U32        : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x022>;
defm DS_INC_RTN_U32         : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x023>;
defm DS_DEC_RTN_U32         : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x024>;
defm DS_MIN_RTN_I32         : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x025>;
defm DS_MAX_RTN_I32         : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x026>;
defm DS_MIN_RTN_U32         : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x027>;
defm DS_MAX_RTN_U32         : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x028>;
defm DS_AND_RTN_B32         : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x029>;
defm DS_OR_RTN_B32          : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x02a>;
defm DS_XOR_RTN_B32         : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x02b>;
defm DS_MSKOR_RTN_B32       : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x02c>;

defm DS_WRXCHG_RTN_B32      : DS_Real_gfx6_gfx7_gfx10<0x02d>;
defm DS_WRXCHG2_RTN_B32     : DS_Real_gfx6_gfx7_gfx10<0x02e>;
defm DS_WRXCHG2ST64_RTN_B32 : DS_Real_gfx6_gfx7_gfx10<0x02f>;
defm DS_CMPST_RTN_B32       : DS_Real_gfx6_gfx7_gfx10<0x030>;
defm DS_CMPST_RTN_F32       : DS_Real_gfx6_gfx7_gfx10<0x031>;

defm DS_MIN_RTN_F32         : DS_Real_gfx6_gfx7_gfx10_gfx11<0x032>;
defm DS_MAX_RTN_F32         : DS_Real_gfx6_gfx7_gfx10_gfx11<0x033>;
defm DS_SWIZZLE_B32         : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x035>;

defm DS_READ_B32            : DS_Real_gfx6_gfx7_gfx10<0x036>;
defm DS_READ2_B32           : DS_Real_gfx6_gfx7_gfx10<0x037>;
defm DS_READ2ST64_B32       : DS_Real_gfx6_gfx7_gfx10<0x038>;
defm DS_READ_I8             : DS_Real_gfx6_gfx7_gfx10<0x039>;
defm DS_READ_U8             : DS_Real_gfx6_gfx7_gfx10<0x03a>;
defm DS_READ_I16            : DS_Real_gfx6_gfx7_gfx10<0x03b>;
defm DS_READ_U16            : DS_Real_gfx6_gfx7_gfx10<0x03c>;

defm DS_CONSUME             : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x03d>;
defm DS_APPEND              : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x03e>;
defm DS_ORDERED_COUNT       : DS_Real_gfx6_gfx7_gfx10_gfx11<0x03f>;
defm DS_ADD_U64             : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x040>;
defm DS_SUB_U64             : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x041>;
defm DS_RSUB_U64            : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x042>;
defm DS_INC_U64             : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x043>;
defm DS_DEC_U64             : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x044>;
defm DS_MIN_I64             : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x045>;
defm DS_MAX_I64             : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x046>;
defm DS_MIN_U64             : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x047>;
defm DS_MAX_U64             : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x048>;
defm DS_AND_B64             : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x049>;
defm DS_OR_B64              : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x04a>;
defm DS_XOR_B64             : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x04b>;
defm DS_MSKOR_B64           : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x04c>;

defm DS_WRITE_B64           : DS_Real_gfx6_gfx7_gfx10<0x04d>;
defm DS_WRITE2_B64          : DS_Real_gfx6_gfx7_gfx10<0x04e>;
defm DS_WRITE2ST64_B64      : DS_Real_gfx6_gfx7_gfx10<0x04f>;
defm DS_CMPST_B64           : DS_Real_gfx6_gfx7_gfx10<0x050>;
defm DS_CMPST_F64           : DS_Real_gfx6_gfx7_gfx10<0x051>;

defm DS_MIN_F64             : DS_Real_gfx6_gfx7_gfx10_gfx11<0x052>;
defm DS_MAX_F64             : DS_Real_gfx6_gfx7_gfx10_gfx11<0x053>;
defm DS_ADD_RTN_U64         : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x060>;
defm DS_SUB_RTN_U64         : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x061>;
defm DS_RSUB_RTN_U64        : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x062>;
defm DS_INC_RTN_U64         : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x063>;
defm DS_DEC_RTN_U64         : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x064>;
defm DS_MIN_RTN_I64         : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x065>;
defm DS_MAX_RTN_I64         : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x066>;
defm DS_MIN_RTN_U64         : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x067>;
defm DS_MAX_RTN_U64         : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x068>;
defm DS_AND_RTN_B64         : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x069>;
defm DS_OR_RTN_B64          : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x06a>;
defm DS_XOR_RTN_B64         : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x06b>;
defm DS_MSKOR_RTN_B64       : DS_Real_gfx6_gfx7_gfx10_gfx11_gfx12<0x06c>;

defm DS_WRXCHG_RTN_B64      : DS_Real_gfx6_gfx7_gfx10<0x06d>;
defm DS_WRXCHG2_RTN_B64     : DS_Real_gfx6_gfx7_gfx10<0x06e>;
defm DS_WRXCHG2ST64_RTN_B64 : DS_Real_gfx6_gfx7_gfx10<0x06f>;
defm DS_CMPST_RTN_B64       : DS_Real_gfx6_gfx7_gfx10<0x070>;
defm DS_CMPST_RTN_F64       : DS_Real_gfx6_gfx7_gfx10<0x071>;

defm DS_MIN_RTN_F64         : DS_Real_gfx6_gfx7_gfx10_gfx11<0x072>;
defm DS_MAX_RTN_F64         : DS_Real_gfx6_gfx7_gfx10_gfx11<0x073>;

defm DS_READ_B64            : DS_Real_gfx6_gfx7_gfx10<0x076>;
defm DS_READ2_B64           : DS_Real_gfx6_gfx7_gfx10<0x077>;
defm DS_READ2ST64_B64       : DS_Real_gfx6_gfx7_gfx10<0x078>;
defm DS_ADD_SRC2_U32        : DS_Real_gfx6_gfx7_gfx10<0x080>;
defm DS_SUB_SRC2_U32        : DS_Real_gfx6_gfx7_gfx10<0x081>;
defm DS_RSUB_SRC2_U32       : DS_Real_gfx6_gfx7_gfx10<0x082>;
defm DS_INC_SRC2_U32        : DS_Real_gfx6_gfx7_gfx10<0x083>;
defm DS_DEC_SRC2_U32        : DS_Real_gfx6_gfx7_gfx10<0x084>;
defm DS_MIN_SRC2_I32        : DS_Real_gfx6_gfx7_gfx10<0x085>;
defm DS_MAX_SRC2_I32        : DS_Real_gfx6_gfx7_gfx10<0x086>;
defm DS_MIN_SRC2_U32        : DS_Real_gfx6_gfx7_gfx10<0x087>;
defm DS_MAX_SRC2_U32        : DS_Real_gfx6_gfx7_gfx10<0x088>;
defm DS_AND_SRC2_B32        : DS_Real_gfx6_gfx7_gfx10<0x089>;
defm DS_OR_SRC2_B32         : DS_Real_gfx6_gfx7_gfx10<0x08a>;
defm DS_XOR_SRC2_B32        : DS_Real_gfx6_gfx7_gfx10<0x08b>;
defm DS_WRITE_SRC2_B32      : DS_Real_gfx6_gfx7_gfx10<0x08d>;
defm DS_MIN_SRC2_F32        : DS_Real_gfx6_gfx7_gfx10<0x092>;
defm DS_MAX_SRC2_F32        : DS_Real_gfx6_gfx7_gfx10<0x093>;
defm DS_ADD_SRC2_U64        : DS_Real_gfx6_gfx7_gfx10<0x0c0>;
defm DS_SUB_SRC2_U64        : DS_Real_gfx6_gfx7_gfx10<0x0c1>;
defm DS_RSUB_SRC2_U64       : DS_Real_gfx6_gfx7_gfx10<0x0c2>;
defm DS_INC_SRC2_U64        : DS_Real_gfx6_gfx7_gfx10<0x0c3>;
defm DS_DEC_SRC2_U64        : DS_Real_gfx6_gfx7_gfx10<0x0c4>;
defm DS_MIN_SRC2_I64        : DS_Real_gfx6_gfx7_gfx10<0x0c5>;
defm DS_MAX_SRC2_I64        : DS_Real_gfx6_gfx7_gfx10<0x0c6>;
defm DS_MIN_SRC2_U64        : DS_Real_gfx6_gfx7_gfx10<0x0c7>;
defm DS_MAX_SRC2_U64        : DS_Real_gfx6_gfx7_gfx10<0x0c8>;
defm DS_AND_SRC2_B64        : DS_Real_gfx6_gfx7_gfx10<0x0c9>;
defm DS_OR_SRC2_B64         : DS_Real_gfx6_gfx7_gfx10<0x0ca>;
defm DS_XOR_SRC2_B64        : DS_Real_gfx6_gfx7_gfx10<0x0cb>;
defm DS_WRITE_SRC2_B64      : DS_Real_gfx6_gfx7_gfx10<0x0cd>;
defm DS_MIN_SRC2_F64        : DS_Real_gfx6_gfx7_gfx10<0x0d2>;
defm DS_MAX_SRC2_F64        : DS_Real_gfx6_gfx7_gfx10<0x0d3>;

//===----------------------------------------------------------------------===//
// GFX8, GFX9 (VI).
//===----------------------------------------------------------------------===//

class DS_Real_vi <bits<8> op, DS_Pseudo ps> :
  DS_Real <ps>,
  SIMCInstr <ps.PseudoInstr, SIEncodingFamily.VI> {
  let AssemblerPredicate = isGFX8GFX9;
  let DecoderNamespace = "GFX8";

  // encoding
  let Inst{7-0}   = !if(ps.has_offset0, offset0, 0);
  let Inst{15-8}  = !if(ps.has_offset1, offset1, 0);
  let Inst{16}    = !if(ps.has_gds, gds, ps.gdsValue);
  let Inst{24-17} = op;
  let Inst{25}    = acc;
  let Inst{31-26} = 0x36; // ds prefix
  let Inst{39-32} = !if(ps.has_addr, addr, !if(ps.has_gws_data0, data0{7-0}, 0));
  let Inst{47-40} = !if(ps.has_data0, data0{7-0}, 0);
  let Inst{55-48} = !if(ps.has_data1, data1{7-0}, 0);
  let Inst{63-56} = !if(ps.has_vdst, vdst{7-0}, 0);
}

def DS_ADD_U32_vi         : DS_Real_vi<0x0,  DS_ADD_U32>;
def DS_SUB_U32_vi         : DS_Real_vi<0x1,  DS_SUB_U32>;
def DS_RSUB_U32_vi        : DS_Real_vi<0x2,  DS_RSUB_U32>;
def DS_INC_U32_vi         : DS_Real_vi<0x3,  DS_INC_U32>;
def DS_DEC_U32_vi         : DS_Real_vi<0x4,  DS_DEC_U32>;
def DS_MIN_I32_vi         : DS_Real_vi<0x5,  DS_MIN_I32>;
def DS_MAX_I32_vi         : DS_Real_vi<0x6,  DS_MAX_I32>;
def DS_MIN_U32_vi         : DS_Real_vi<0x7,  DS_MIN_U32>;
def DS_MAX_U32_vi         : DS_Real_vi<0x8,  DS_MAX_U32>;
def DS_AND_B32_vi         : DS_Real_vi<0x9,  DS_AND_B32>;
def DS_OR_B32_vi          : DS_Real_vi<0xa,  DS_OR_B32>;
def DS_XOR_B32_vi         : DS_Real_vi<0xb,  DS_XOR_B32>;
def DS_MSKOR_B32_vi       : DS_Real_vi<0xc,  DS_MSKOR_B32>;
def DS_WRITE_B32_vi       : DS_Real_vi<0xd,  DS_WRITE_B32>;
def DS_WRITE2_B32_vi      : DS_Real_vi<0xe,  DS_WRITE2_B32>;
def DS_WRITE2ST64_B32_vi  : DS_Real_vi<0xf,  DS_WRITE2ST64_B32>;
def DS_CMPST_B32_vi       : DS_Real_vi<0x10, DS_CMPST_B32>;
def DS_CMPST_F32_vi       : DS_Real_vi<0x11, DS_CMPST_F32>;
def DS_MIN_F32_vi         : DS_Real_vi<0x12, DS_MIN_F32>;
def DS_MAX_F32_vi         : DS_Real_vi<0x13, DS_MAX_F32>;
def DS_NOP_vi             : DS_Real_vi<0x14, DS_NOP>;
def DS_ADD_F32_vi         : DS_Real_vi<0x15, DS_ADD_F32>;
def DS_GWS_INIT_vi        : DS_Real_vi<0x99, DS_GWS_INIT>;
def DS_GWS_SEMA_V_vi      : DS_Real_vi<0x9a, DS_GWS_SEMA_V>;
def DS_GWS_SEMA_BR_vi     : DS_Real_vi<0x9b, DS_GWS_SEMA_BR>;
def DS_GWS_SEMA_P_vi      : DS_Real_vi<0x9c, DS_GWS_SEMA_P>;
def DS_GWS_BARRIER_vi     : DS_Real_vi<0x9d, DS_GWS_BARRIER>;
def DS_WRITE_ADDTID_B32_vi : DS_Real_vi<0x1d, DS_WRITE_ADDTID_B32>;
def DS_WRITE_B8_vi        : DS_Real_vi<0x1e, DS_WRITE_B8>;
def DS_WRITE_B16_vi       : DS_Real_vi<0x1f, DS_WRITE_B16>;
def DS_ADD_RTN_U32_vi     : DS_Real_vi<0x20, DS_ADD_RTN_U32>;
def DS_SUB_RTN_U32_vi     : DS_Real_vi<0x21, DS_SUB_RTN_U32>;
def DS_RSUB_RTN_U32_vi    : DS_Real_vi<0x22, DS_RSUB_RTN_U32>;
def DS_INC_RTN_U32_vi     : DS_Real_vi<0x23, DS_INC_RTN_U32>;
def DS_DEC_RTN_U32_vi     : DS_Real_vi<0x24, DS_DEC_RTN_U32>;
def DS_MIN_RTN_I32_vi     : DS_Real_vi<0x25, DS_MIN_RTN_I32>;
def DS_MAX_RTN_I32_vi     : DS_Real_vi<0x26, DS_MAX_RTN_I32>;
def DS_MIN_RTN_U32_vi     : DS_Real_vi<0x27, DS_MIN_RTN_U32>;
def DS_MAX_RTN_U32_vi     : DS_Real_vi<0x28, DS_MAX_RTN_U32>;
def DS_AND_RTN_B32_vi     : DS_Real_vi<0x29, DS_AND_RTN_B32>;
def DS_OR_RTN_B32_vi      : DS_Real_vi<0x2a, DS_OR_RTN_B32>;
def DS_XOR_RTN_B32_vi     : DS_Real_vi<0x2b, DS_XOR_RTN_B32>;
def DS_MSKOR_RTN_B32_vi   : DS_Real_vi<0x2c, DS_MSKOR_RTN_B32>;
def DS_WRXCHG_RTN_B32_vi  : DS_Real_vi<0x2d, DS_WRXCHG_RTN_B32>;
def DS_WRXCHG2_RTN_B32_vi : DS_Real_vi<0x2e, DS_WRXCHG2_RTN_B32>;
def DS_WRXCHG2ST64_RTN_B32_vi : DS_Real_vi<0x2f, DS_WRXCHG2ST64_RTN_B32>;
def DS_CMPST_RTN_B32_vi   : DS_Real_vi<0x30, DS_CMPST_RTN_B32>;
def DS_CMPST_RTN_F32_vi   : DS_Real_vi<0x31, DS_CMPST_RTN_F32>;
def DS_MIN_RTN_F32_vi     : DS_Real_vi<0x32, DS_MIN_RTN_F32>;
def DS_MAX_RTN_F32_vi     : DS_Real_vi<0x33, DS_MAX_RTN_F32>;
def DS_WRAP_RTN_B32_vi    : DS_Real_vi<0x34, DS_WRAP_RTN_B32>;
def DS_ADD_RTN_F32_vi     : DS_Real_vi<0x35, DS_ADD_RTN_F32>;
def DS_READ_B32_vi        : DS_Real_vi<0x36, DS_READ_B32>;
def DS_READ2_B32_vi       : DS_Real_vi<0x37, DS_READ2_B32>;
def DS_READ2ST64_B32_vi   : DS_Real_vi<0x38, DS_READ2ST64_B32>;
def DS_READ_I8_vi         : DS_Real_vi<0x39, DS_READ_I8>;
def DS_READ_U8_vi         : DS_Real_vi<0x3a, DS_READ_U8>;
def DS_READ_I16_vi        : DS_Real_vi<0x3b, DS_READ_I16>;
def DS_READ_U16_vi        : DS_Real_vi<0x3c, DS_READ_U16>;
def DS_READ_ADDTID_B32_vi : DS_Real_vi<0xb6, DS_READ_ADDTID_B32>;
def DS_CONSUME_vi         : DS_Real_vi<0xbd, DS_CONSUME>;
def DS_APPEND_vi          : DS_Real_vi<0xbe, DS_APPEND>;
def DS_ORDERED_COUNT_vi   : DS_Real_vi<0xbf, DS_ORDERED_COUNT>;
def DS_SWIZZLE_B32_vi     : DS_Real_vi<0x3d, DS_SWIZZLE_B32>;
def DS_PERMUTE_B32_vi     : DS_Real_vi<0x3e, DS_PERMUTE_B32>;
def DS_BPERMUTE_B32_vi    : DS_Real_vi<0x3f, DS_BPERMUTE_B32>;

def DS_ADD_U64_vi         : DS_Real_vi<0x40, DS_ADD_U64>;
def DS_SUB_U64_vi         : DS_Real_vi<0x41, DS_SUB_U64>;
def DS_RSUB_U64_vi        : DS_Real_vi<0x42, DS_RSUB_U64>;
def DS_INC_U64_vi         : DS_Real_vi<0x43, DS_INC_U64>;
def DS_DEC_U64_vi         : DS_Real_vi<0x44, DS_DEC_U64>;
def DS_MIN_I64_vi         : DS_Real_vi<0x45, DS_MIN_I64>;
def DS_MAX_I64_vi         : DS_Real_vi<0x46, DS_MAX_I64>;
def DS_MIN_U64_vi         : DS_Real_vi<0x47, DS_MIN_U64>;
def DS_MAX_U64_vi         : DS_Real_vi<0x48, DS_MAX_U64>;
def DS_AND_B64_vi         : DS_Real_vi<0x49, DS_AND_B64>;
def DS_OR_B64_vi          : DS_Real_vi<0x4a, DS_OR_B64>;
def DS_XOR_B64_vi         : DS_Real_vi<0x4b, DS_XOR_B64>;
def DS_MSKOR_B64_vi       : DS_Real_vi<0x4c, DS_MSKOR_B64>;
def DS_WRITE_B64_vi       : DS_Real_vi<0x4d, DS_WRITE_B64>;
def DS_WRITE2_B64_vi      : DS_Real_vi<0x4E, DS_WRITE2_B64>;
def DS_WRITE2ST64_B64_vi  : DS_Real_vi<0x4f, DS_WRITE2ST64_B64>;
def DS_CMPST_B64_vi       : DS_Real_vi<0x50, DS_CMPST_B64>;
def DS_CMPST_F64_vi       : DS_Real_vi<0x51, DS_CMPST_F64>;
def DS_MIN_F64_vi         : DS_Real_vi<0x52, DS_MIN_F64>;
def DS_MAX_F64_vi         : DS_Real_vi<0x53, DS_MAX_F64>;

def DS_WRITE_B8_D16_HI_vi  : DS_Real_vi<0x54, DS_WRITE_B8_D16_HI>;
def DS_WRITE_B16_D16_HI_vi : DS_Real_vi<0x55, DS_WRITE_B16_D16_HI>;

def DS_READ_U8_D16_vi     : DS_Real_vi<0x56, DS_READ_U8_D16>;
def DS_READ_U8_D16_HI_vi  : DS_Real_vi<0x57, DS_READ_U8_D16_HI>;
def DS_READ_I8_D16_vi     : DS_Real_vi<0x58, DS_READ_I8_D16>;
def DS_READ_I8_D16_HI_vi  : DS_Real_vi<0x59, DS_READ_I8_D16_HI>;
def DS_READ_U16_D16_vi    : DS_Real_vi<0x5a, DS_READ_U16_D16>;
def DS_READ_U16_D16_HI_vi : DS_Real_vi<0x5b, DS_READ_U16_D16_HI>;

def DS_ADD_RTN_U64_vi     : DS_Real_vi<0x60, DS_ADD_RTN_U64>;
def DS_SUB_RTN_U64_vi     : DS_Real_vi<0x61, DS_SUB_RTN_U64>;
def DS_RSUB_RTN_U64_vi    : DS_Real_vi<0x62, DS_RSUB_RTN_U64>;
def DS_INC_RTN_U64_vi     : DS_Real_vi<0x63, DS_INC_RTN_U64>;
def DS_DEC_RTN_U64_vi     : DS_Real_vi<0x64, DS_DEC_RTN_U64>;
def DS_MIN_RTN_I64_vi     : DS_Real_vi<0x65, DS_MIN_RTN_I64>;
def DS_MAX_RTN_I64_vi     : DS_Real_vi<0x66, DS_MAX_RTN_I64>;
def DS_MIN_RTN_U64_vi     : DS_Real_vi<0x67, DS_MIN_RTN_U64>;
def DS_MAX_RTN_U64_vi     : DS_Real_vi<0x68, DS_MAX_RTN_U64>;
def DS_AND_RTN_B64_vi     : DS_Real_vi<0x69, DS_AND_RTN_B64>;
def DS_OR_RTN_B64_vi      : DS_Real_vi<0x6a, DS_OR_RTN_B64>;
def DS_XOR_RTN_B64_vi     : DS_Real_vi<0x6b, DS_XOR_RTN_B64>;
def DS_MSKOR_RTN_B64_vi   : DS_Real_vi<0x6c, DS_MSKOR_RTN_B64>;
def DS_WRXCHG_RTN_B64_vi  : DS_Real_vi<0x6d, DS_WRXCHG_RTN_B64>;
def DS_WRXCHG2_RTN_B64_vi : DS_Real_vi<0x6e, DS_WRXCHG2_RTN_B64>;
def DS_WRXCHG2ST64_RTN_B64_vi : DS_Real_vi<0x6f, DS_WRXCHG2ST64_RTN_B64>;
def DS_CONDXCHG32_RTN_B64_vi   : DS_Real_vi<0x7e, DS_CONDXCHG32_RTN_B64>;
def DS_GWS_SEMA_RELEASE_ALL_vi : DS_Real_vi<0x98, DS_GWS_SEMA_RELEASE_ALL>;
def DS_CMPST_RTN_B64_vi   : DS_Real_vi<0x70, DS_CMPST_RTN_B64>;
def DS_CMPST_RTN_F64_vi   : DS_Real_vi<0x71, DS_CMPST_RTN_F64>;
def DS_MIN_RTN_F64_vi     : DS_Real_vi<0x72, DS_MIN_RTN_F64>;
def DS_MAX_RTN_F64_vi     : DS_Real_vi<0x73, DS_MAX_RTN_F64>;

def DS_READ_B64_vi        : DS_Real_vi<0x76, DS_READ_B64>;
def DS_READ2_B64_vi       : DS_Real_vi<0x77, DS_READ2_B64>;
def DS_READ2ST64_B64_vi   : DS_Real_vi<0x78, DS_READ2ST64_B64>;

def DS_ADD_SRC2_U32_vi    : DS_Real_vi<0x80, DS_ADD_SRC2_U32>;
def DS_SUB_SRC2_U32_vi    : DS_Real_vi<0x81, DS_SUB_SRC2_U32>;
def DS_RSUB_SRC2_U32_vi   : DS_Real_vi<0x82, DS_RSUB_SRC2_U32>;
def DS_INC_SRC2_U32_vi    : DS_Real_vi<0x83, DS_INC_SRC2_U32>;
def DS_DEC_SRC2_U32_vi    : DS_Real_vi<0x84, DS_DEC_SRC2_U32>;
def DS_MIN_SRC2_I32_vi    : DS_Real_vi<0x85, DS_MIN_SRC2_I32>;
def DS_MAX_SRC2_I32_vi    : DS_Real_vi<0x86, DS_MAX_SRC2_I32>;
def DS_MIN_SRC2_U32_vi    : DS_Real_vi<0x87, DS_MIN_SRC2_U32>;
def DS_MAX_SRC2_U32_vi    : DS_Real_vi<0x88, DS_MAX_SRC2_U32>;
def DS_AND_SRC2_B32_vi    : DS_Real_vi<0x89, DS_AND_SRC2_B32>;
def DS_OR_SRC2_B32_vi     : DS_Real_vi<0x8a, DS_OR_SRC2_B32>;
def DS_XOR_SRC2_B32_vi    : DS_Real_vi<0x8b, DS_XOR_SRC2_B32>;
def DS_WRITE_SRC2_B32_vi  : DS_Real_vi<0x8d, DS_WRITE_SRC2_B32>;
def DS_MIN_SRC2_F32_vi    : DS_Real_vi<0x92, DS_MIN_SRC2_F32>;
def DS_MAX_SRC2_F32_vi    : DS_Real_vi<0x93, DS_MAX_SRC2_F32>;
def DS_ADD_SRC2_F32_vi    : DS_Real_vi<0x95, DS_ADD_SRC2_F32>;
def DS_ADD_SRC2_U64_vi    : DS_Real_vi<0xc0, DS_ADD_SRC2_U64>;
def DS_SUB_SRC2_U64_vi    : DS_Real_vi<0xc1, DS_SUB_SRC2_U64>;
def DS_RSUB_SRC2_U64_vi   : DS_Real_vi<0xc2, DS_RSUB_SRC2_U64>;
def DS_INC_SRC2_U64_vi    : DS_Real_vi<0xc3, DS_INC_SRC2_U64>;
def DS_DEC_SRC2_U64_vi    : DS_Real_vi<0xc4, DS_DEC_SRC2_U64>;
def DS_MIN_SRC2_I64_vi    : DS_Real_vi<0xc5, DS_MIN_SRC2_I64>;
def DS_MAX_SRC2_I64_vi    : DS_Real_vi<0xc6, DS_MAX_SRC2_I64>;
def DS_MIN_SRC2_U64_vi    : DS_Real_vi<0xc7, DS_MIN_SRC2_U64>;
def DS_MAX_SRC2_U64_vi    : DS_Real_vi<0xc8, DS_MAX_SRC2_U64>;
def DS_AND_SRC2_B64_vi    : DS_Real_vi<0xc9, DS_AND_SRC2_B64>;
def DS_OR_SRC2_B64_vi     : DS_Real_vi<0xca, DS_OR_SRC2_B64>;
def DS_XOR_SRC2_B64_vi    : DS_Real_vi<0xcb, DS_XOR_SRC2_B64>;
def DS_WRITE_SRC2_B64_vi  : DS_Real_vi<0xcd, DS_WRITE_SRC2_B64>;
def DS_MIN_SRC2_F64_vi    : DS_Real_vi<0xd2, DS_MIN_SRC2_F64>;
def DS_MAX_SRC2_F64_vi    : DS_Real_vi<0xd3, DS_MAX_SRC2_F64>;
def DS_WRITE_B96_vi       : DS_Real_vi<0xde, DS_WRITE_B96>;
def DS_WRITE_B128_vi      : DS_Real_vi<0xdf, DS_WRITE_B128>;
def DS_READ_B96_vi        : DS_Real_vi<0xfe, DS_READ_B96>;
def DS_READ_B128_vi       : DS_Real_vi<0xff, DS_READ_B128>;

// GFX90A+.
def DS_ADD_F64_vi     : DS_Real_vi<0x5c, DS_ADD_F64>;
def DS_ADD_RTN_F64_vi : DS_Real_vi<0x7c, DS_ADD_RTN_F64>;

// GFX940+.
def DS_PK_ADD_F16_vi     : DS_Real_vi<0x17, DS_PK_ADD_F16>;
def DS_PK_ADD_RTN_F16_vi : DS_Real_vi<0xb7, DS_PK_ADD_RTN_F16>;
def DS_PK_ADD_BF16_vi     : DS_Real_vi<0x18, DS_PK_ADD_BF16>;
def DS_PK_ADD_RTN_BF16_vi : DS_Real_vi<0xb8, DS_PK_ADD_RTN_BF16>;

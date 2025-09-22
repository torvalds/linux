//===-- R600Instructions.td - R600 Instruction defs  -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// TableGen definitions for instructions which are available on R600 family
// GPUs.
//
//===----------------------------------------------------------------------===//

include "R600InstrFormats.td"

// FIXME: Should not be arbitrarily split from other R600 inst classes.
class R600WrapperInst <dag outs, dag ins, string asm = "", list<dag> pattern = []> :
  AMDGPUInst<outs, ins, asm, pattern>, PredicateControl {
  let SubtargetPredicate = isR600toCayman;
  let Namespace = "R600";
}


class InstR600ISA <dag outs, dag ins, string asm, list<dag> pattern = []> :
    InstR600 <outs, ins, asm, pattern, NullALU> {

}

def MEMxi : Operand<iPTR> {
  let MIOperandInfo = (ops R600_TReg32_X:$ptr, i32imm:$index);
  let PrintMethod = "printMemOperand";
}

def MEMrr : Operand<iPTR> {
  let MIOperandInfo = (ops R600_Reg32:$ptr, R600_Reg32:$index);
}

// Operands for non-registers

class InstFlag<string PM = "printOperand", int Default = 0>
    : OperandWithDefaultOps <i32, (ops (i32 Default))> {
  let PrintMethod = PM;
}

// src_sel for ALU src operands, see also ALU_CONST, ALU_PARAM registers
def SEL : OperandWithDefaultOps <i32, (ops (i32 -1))>;
def BANK_SWIZZLE : OperandWithDefaultOps <i32, (ops (i32 0))> {
  let PrintMethod = "printBankSwizzle";
}

def LITERAL : InstFlag<"printLiteral">;

def WRITE : InstFlag <"printWrite", 1>;
def OMOD : InstFlag <"printOMOD">;
def REL : InstFlag <"printRel">;
def CLAMP : InstFlag <"printClamp">;
def NEG : InstFlag <"printNeg">;
def ABS : InstFlag <"printAbs">;
def UEM : InstFlag <"printUpdateExecMask">;
def UP : InstFlag <"printUpdatePred">;

// XXX: The r600g finalizer in Mesa expects last to be one in most cases.
// Once we start using the packetizer in this backend we should have this
// default to 0.
def LAST : InstFlag<"printLast", 1>;
def RSel : Operand<i32> {
  let PrintMethod = "printRSel";
}
def CT: Operand<i32> {
  let PrintMethod = "printCT";
}

def FRAMEri : Operand<iPTR> {
  let MIOperandInfo = (ops R600_Reg32:$ptr, i32imm:$index);
}

def ADDRVTX_READ : ComplexPattern<i32, 2, "SelectADDRVTX_READ", [], []>;
def ADDRGA_CONST_OFFSET : ComplexPattern<i32, 1, "SelectGlobalValueConstantOffset", [], []>;
def ADDRGA_VAR_OFFSET : ComplexPattern<i32, 2, "SelectGlobalValueVariableOffset", [], []>;
def ADDRIndirect : ComplexPattern<iPTR, 2, "SelectADDRIndirect", [], []>;


def R600_Pred : PredicateOperand<i32, (ops R600_Predicate),
                                     (ops PRED_SEL_OFF)>;

let isTerminator = 1, isReturn = 1, hasCtrlDep = 1,
    usesCustomInserter = 1, Namespace = "R600" in {
  def RETURN : ILFormat<(outs), (ins variable_ops),
    "RETURN", [(AMDGPUendpgm)]
  >;
}

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in {

// Class for instructions with only one source register.
// If you add new ins to this instruction, make sure they are listed before
// $literal, because the backend currently assumes that the last operand is
// a literal.  Also be sure to update the enum R600Op1OperandIndex::ROI in
// R600Defines.h, R600InstrInfo::buildDefaultInstruction(),
// and R600InstrInfo::getOperandIdx().
class R600_1OP <bits<11> inst, string opName, list<dag> pattern,
                InstrItinClass itin = AnyALU> :
    InstR600 <(outs R600_Reg32:$dst),
              (ins WRITE:$write, OMOD:$omod, REL:$dst_rel, CLAMP:$clamp,
                   R600_Reg32:$src0, NEG:$src0_neg, REL:$src0_rel, ABS:$src0_abs, SEL:$src0_sel,
                   LAST:$last, R600_Pred:$pred_sel, LITERAL:$literal,
                   BANK_SWIZZLE:$bank_swizzle),
              !strconcat("  ", opName,
                   "$clamp $last $dst$write$dst_rel$omod, "
                   "$src0_neg$src0_abs$src0$src0_abs$src0_rel, "
                   "$pred_sel $bank_swizzle"),
              pattern,
              itin>,
    R600ALU_Word0,
    R600ALU_Word1_OP2 <inst> {

  let src1 = 0;
  let src1_rel = 0;
  let src1_neg = 0;
  let src1_abs = 0;
  let update_exec_mask = 0;
  let update_pred = 0;
  let HasNativeOperands = 1;
  let Op1 = 1;
  let ALUInst = 1;
  let DisableEncoding = "$literal";
  let UseNamedOperandTable = 1;

  let Inst{31-0}  = Word0;
  let Inst{63-32} = Word1;
}

class R600_1OP_Helper <bits<11> inst, string opName, SDPatternOperator node,
                    InstrItinClass itin = AnyALU> :
    R600_1OP <inst, opName,
              [(set R600_Reg32:$dst, (node R600_Reg32:$src0))], itin
>;

// If you add or change the operands for R600_2OP instructions, you must
// also update the R600Op2OperandIndex::ROI enum in R600Defines.h,
// R600InstrInfo::buildDefaultInstruction(), and R600InstrInfo::getOperandIdx().
class R600_2OP <bits<11> inst, string opName, list<dag> pattern,
                InstrItinClass itin = AnyALU> :
  InstR600 <(outs R600_Reg32:$dst),
          (ins UEM:$update_exec_mask, UP:$update_pred, WRITE:$write,
               OMOD:$omod, REL:$dst_rel, CLAMP:$clamp,
               R600_Reg32:$src0, NEG:$src0_neg, REL:$src0_rel, ABS:$src0_abs, SEL:$src0_sel,
               R600_Reg32:$src1, NEG:$src1_neg, REL:$src1_rel, ABS:$src1_abs, SEL:$src1_sel,
               LAST:$last, R600_Pred:$pred_sel, LITERAL:$literal,
               BANK_SWIZZLE:$bank_swizzle),
          !strconcat("  ", opName,
                "$clamp $last $update_exec_mask$update_pred$dst$write$dst_rel$omod, "
                "$src0_neg$src0_abs$src0$src0_abs$src0_rel, "
                "$src1_neg$src1_abs$src1$src1_abs$src1_rel, "
                "$pred_sel $bank_swizzle"),
          pattern,
          itin>,
    R600ALU_Word0,
    R600ALU_Word1_OP2 <inst> {

  let HasNativeOperands = 1;
  let Op2 = 1;
  let ALUInst = 1;
  let DisableEncoding = "$literal";
  let UseNamedOperandTable = 1;

  let Inst{31-0}  = Word0;
  let Inst{63-32} = Word1;
}

class R600_2OP_Helper <bits<11> inst, string opName,
                       SDPatternOperator node = null_frag,
                       InstrItinClass itin = AnyALU> :
    R600_2OP <inst, opName,
              [(set R600_Reg32:$dst, (node R600_Reg32:$src0,
                                           R600_Reg32:$src1))], itin
>;

// If you add our change the operands for R600_3OP instructions, you must
// also update the R600Op3OperandIndex::ROI enum in R600Defines.h,
// R600InstrInfo::buildDefaultInstruction(), and
// R600InstrInfo::getOperandIdx().
class R600_3OP <bits<5> inst, string opName, list<dag> pattern,
                InstrItinClass itin = AnyALU> :
  InstR600 <(outs R600_Reg32:$dst),
          (ins REL:$dst_rel, CLAMP:$clamp,
               R600_Reg32:$src0, NEG:$src0_neg, REL:$src0_rel, SEL:$src0_sel,
               R600_Reg32:$src1, NEG:$src1_neg, REL:$src1_rel, SEL:$src1_sel,
               R600_Reg32:$src2, NEG:$src2_neg, REL:$src2_rel, SEL:$src2_sel,
               LAST:$last, R600_Pred:$pred_sel, LITERAL:$literal,
               BANK_SWIZZLE:$bank_swizzle),
          !strconcat("  ", opName, "$clamp $last $dst$dst_rel, "
                             "$src0_neg$src0$src0_rel, "
                             "$src1_neg$src1$src1_rel, "
                             "$src2_neg$src2$src2_rel, "
                             "$pred_sel"
                             "$bank_swizzle"),
          pattern,
          itin>,
    R600ALU_Word0,
    R600ALU_Word1_OP3<inst>{

  let HasNativeOperands = 1;
  let DisableEncoding = "$literal";
  let Op3 = 1;
  let UseNamedOperandTable = 1;
  let ALUInst = 1;

  let Inst{31-0}  = Word0;
  let Inst{63-32} = Word1;
}

} // End mayLoad = 1, mayStore = 0, hasSideEffects = 0

class EG_CF_RAT <bits <8> cfinst, bits <6> ratinst, bits<4> ratid, bits<4> mask,
                 dag outs, dag ins, string asm, list<dag> pattern> :
    InstR600ISA <outs, ins, asm, pattern>,
    CF_ALLOC_EXPORT_WORD0_RAT, CF_ALLOC_EXPORT_WORD1_BUF  {

  let rat_id = ratid;
  let rat_inst = ratinst;
  let rim         = 0;
  // XXX: Have a separate instruction for non-indexed writes.
  let type        = 1;
  let rw_rel      = 0;
  let elem_size   = 0;

  let array_size  = 0;
  let comp_mask   = mask;
  let burst_count = 0;
  let vpm         = 0;
  let cf_inst = cfinst;
  let mark        = 0;
  let barrier     = 1;

  let Inst{31-0} = Word0;
  let Inst{63-32} = Word1;
  let IsExport = 1;

}

class VTX_READ <string name, dag outs, list<dag> pattern>
    : InstR600ISA <outs, (ins MEMxi:$src_gpr, i8imm:$buffer_id), !strconcat("  ", name, ", #$buffer_id"), pattern>,
      VTX_WORD1_GPR {

  // Static fields
  let DST_REL = 0;
  // The docs say that if this bit is set, then DATA_FORMAT, NUM_FORMAT_ALL,
  // FORMAT_COMP_ALL, SRF_MODE_ALL, and ENDIAN_SWAP fields will be ignored,
  // however, based on my testing if USE_CONST_FIELDS is set, then all
  // these fields need to be set to 0.
  let USE_CONST_FIELDS = 0;
  let NUM_FORMAT_ALL = 1;
  let FORMAT_COMP_ALL = 0;
  let SRF_MODE_ALL = 0;

  let Inst{63-32} = Word1;
  // LLVM can only encode 64-bit instructions, so these fields are manually
  // encoded in R600CodeEmitter
  //
  // bits<16> OFFSET;
  // bits<2>  ENDIAN_SWAP = 0;
  // bits<1>  CONST_BUF_NO_STRIDE = 0;
  // bits<1>  MEGA_FETCH = 0;
  // bits<1>  ALT_CONST = 0;
  // bits<2>  BUFFER_INDEX_MODE = 0;

  // VTX_WORD2 (LLVM can only encode 64-bit instructions, so WORD2 encoding
  // is done in R600CodeEmitter
  //
  // Inst{79-64} = OFFSET;
  // Inst{81-80} = ENDIAN_SWAP;
  // Inst{82}    = CONST_BUF_NO_STRIDE;
  // Inst{83}    = MEGA_FETCH;
  // Inst{84}    = ALT_CONST;
  // Inst{86-85} = BUFFER_INDEX_MODE;
  // Inst{95-86} = 0; Reserved

  // VTX_WORD3 (Padding)
  //
  // Inst{127-96} = 0;

  let VTXInst = 1;
}

// Legacy.
def atomic_cmp_swap_global_noret : PatFrag<
  (ops node:$ptr, node:$cmp, node:$value),
  (atomic_cmp_swap node:$ptr, node:$cmp, node:$value),
  [{return cast<MemSDNode>(N)->getAddressSpace() == AMDGPUAS::GLOBAL_ADDRESS && (SDValue(N, 0).use_empty());}]>;

def atomic_cmp_swap_global_ret : PatFrag<
  (ops node:$ptr, node:$cmp, node:$value),
  (atomic_cmp_swap node:$ptr, node:$cmp, node:$value),
  [{return cast<MemSDNode>(N)->getAddressSpace() == AMDGPUAS::GLOBAL_ADDRESS && (!SDValue(N, 0).use_empty());}]>;

def mskor_global : PatFrag<(ops node:$val, node:$ptr),
                            (AMDGPUstore_mskor node:$val, node:$ptr), [{
  return cast<MemSDNode>(N)->getAddressSpace() == AMDGPUAS::GLOBAL_ADDRESS;
}]>;

// FIXME: These are deprecated
class AZExtLoadBase <SDPatternOperator ld_node>: PatFrag<(ops node:$ptr),
                                              (ld_node node:$ptr), [{
  LoadSDNode *L = cast<LoadSDNode>(N);
  return L->getExtensionType() == ISD::ZEXTLOAD ||
         L->getExtensionType() == ISD::EXTLOAD;
}]>;

def az_extload : AZExtLoadBase <unindexedload>;

def az_extloadi8 : PatFrag<(ops node:$ptr), (az_extload node:$ptr), [{
  return cast<LoadSDNode>(N)->getMemoryVT() == MVT::i8;
}]>;

def az_extloadi16 : PatFrag<(ops node:$ptr), (az_extload node:$ptr), [{
  return cast<LoadSDNode>(N)->getMemoryVT() == MVT::i16;
}]>;

def az_extloadi32 : PatFrag<(ops node:$ptr), (az_extload node:$ptr), [{
  return cast<LoadSDNode>(N)->getMemoryVT() == MVT::i32;
}]>;

let AddressSpaces = LoadAddress_local.AddrSpaces in {
def az_extloadi8_local : PatFrag<(ops node:$ptr), (az_extloadi8 node:$ptr)>;
def az_extloadi16_local : PatFrag<(ops node:$ptr), (az_extloadi16 node:$ptr)>;
}

class LoadParamFrag <PatFrag load_type> : PatFrag <
  (ops node:$ptr), (load_type node:$ptr),
  [{ return isConstantLoad(cast<LoadSDNode>(N), 0) ||
            (cast<LoadSDNode>(N)->getAddressSpace() == AMDGPUAS::PARAM_I_ADDRESS); }]
>;

def vtx_id3_az_extloadi8 : LoadParamFrag<az_extloadi8>;
def vtx_id3_az_extloadi16 : LoadParamFrag<az_extloadi16>;
def vtx_id3_load : LoadParamFrag<load>;

class LoadVtxId1 <PatFrag load> : PatFrag <
  (ops node:$ptr), (load node:$ptr), [{
  const MemSDNode *LD = cast<MemSDNode>(N);
  return LD->getAddressSpace() == AMDGPUAS::GLOBAL_ADDRESS ||
         (LD->getAddressSpace() == AMDGPUAS::CONSTANT_ADDRESS &&
           !isa<GlobalValue>(getUnderlyingObject(
           LD->getMemOperand()->getValue())));
}]>;

def vtx_id1_az_extloadi8 : LoadVtxId1 <az_extloadi8>;
def vtx_id1_az_extloadi16 : LoadVtxId1 <az_extloadi16>;
def vtx_id1_load : LoadVtxId1 <load>;

class LoadVtxId2 <PatFrag load> : PatFrag <
  (ops node:$ptr), (load node:$ptr), [{
  const MemSDNode *LD = cast<MemSDNode>(N);
  return LD->getAddressSpace() == AMDGPUAS::CONSTANT_ADDRESS &&
         isa<GlobalValue>(getUnderlyingObject(
         LD->getMemOperand()->getValue()));
}]>;

def vtx_id2_az_extloadi8 : LoadVtxId2 <az_extloadi8>;
def vtx_id2_az_extloadi16 : LoadVtxId2 <az_extloadi16>;
def vtx_id2_load : LoadVtxId2 <load>;

//===----------------------------------------------------------------------===//
// R600 SDNodes
//===----------------------------------------------------------------------===//

let Namespace = "R600" in {

def INTERP_PAIR_XY :  AMDGPUShaderInst <
  (outs R600_TReg32_X:$dst0, R600_TReg32_Y:$dst1),
  (ins i32imm:$src0, R600_TReg32_Y:$src1, R600_TReg32_X:$src2),
  "INTERP_PAIR_XY $src0 $src1 $src2 : $dst0 dst1",
  []>;

def INTERP_PAIR_ZW :  AMDGPUShaderInst <
  (outs R600_TReg32_Z:$dst0, R600_TReg32_W:$dst1),
  (ins i32imm:$src0, R600_TReg32_Y:$src1, R600_TReg32_X:$src2),
  "INTERP_PAIR_ZW $src0 $src1 $src2 : $dst0 dst1",
  []>;

}

def CONST_ADDRESS: SDNode<"AMDGPUISD::CONST_ADDRESS",
  SDTypeProfile<1, -1, [SDTCisInt<0>, SDTCisPtrTy<1>]>,
  [SDNPVariadic]
>;

def DOT4 : SDNode<"AMDGPUISD::DOT4",
  SDTypeProfile<1, 8, [SDTCisFP<0>, SDTCisVT<1, f32>, SDTCisVT<2, f32>,
      SDTCisVT<3, f32>, SDTCisVT<4, f32>, SDTCisVT<5, f32>,
      SDTCisVT<6, f32>, SDTCisVT<7, f32>, SDTCisVT<8, f32>]>,
  []
>;

def COS_HW : SDNode<"AMDGPUISD::COS_HW",
  SDTypeProfile<1, 1, [SDTCisFP<0>, SDTCisFP<1>]>
>;

def SIN_HW : SDNode<"AMDGPUISD::SIN_HW",
  SDTypeProfile<1, 1, [SDTCisFP<0>, SDTCisFP<1>]>
>;

def TEXTURE_FETCH_Type : SDTypeProfile<1, 19, [SDTCisFP<0>]>;

def TEXTURE_FETCH: SDNode<"AMDGPUISD::TEXTURE_FETCH", TEXTURE_FETCH_Type, []>;

multiclass TexPattern<bits<32> TextureOp, Instruction inst, ValueType vt = v4f32> {
def : R600Pat<(TEXTURE_FETCH (i32 TextureOp), vt:$SRC_GPR,
          (i32 imm:$srcx), (i32 imm:$srcy), (i32 imm:$srcz), (i32 imm:$srcw),
          (i32 imm:$offsetx), (i32 imm:$offsety), (i32 imm:$offsetz),
          (i32 imm:$DST_SEL_X), (i32 imm:$DST_SEL_Y), (i32 imm:$DST_SEL_Z),
          (i32 imm:$DST_SEL_W),
          (i32 imm:$RESOURCE_ID), (i32 imm:$SAMPLER_ID),
          (i32 imm:$COORD_TYPE_X), (i32 imm:$COORD_TYPE_Y), (i32 imm:$COORD_TYPE_Z),
          (i32 imm:$COORD_TYPE_W)),
          (inst R600_Reg128:$SRC_GPR,
          imm:$srcx, imm:$srcy, imm:$srcz, imm:$srcw,
          imm:$offsetx, imm:$offsety, imm:$offsetz,
          imm:$DST_SEL_X, imm:$DST_SEL_Y, imm:$DST_SEL_Z,
          imm:$DST_SEL_W,
          imm:$RESOURCE_ID, imm:$SAMPLER_ID,
          imm:$COORD_TYPE_X, imm:$COORD_TYPE_Y, imm:$COORD_TYPE_Z,
          imm:$COORD_TYPE_W)>;
}

//===----------------------------------------------------------------------===//
// Interpolation Instructions
//===----------------------------------------------------------------------===//

let Namespace = "R600" in {

def INTERP_VEC_LOAD :  AMDGPUShaderInst <
  (outs R600_Reg128:$dst),
  (ins i32imm:$src0),
  "INTERP_LOAD $src0 : $dst">;

}

def INTERP_XY : R600_2OP <0xD6, "INTERP_XY", []> {
  let bank_swizzle = 5;
}

def INTERP_ZW : R600_2OP <0xD7, "INTERP_ZW", []> {
  let bank_swizzle = 5;
}

def INTERP_LOAD_P0 : R600_1OP <0xE0, "INTERP_LOAD_P0", []>;

//===----------------------------------------------------------------------===//
// Export Instructions
//===----------------------------------------------------------------------===//

class ExportWord0 {
  field bits<32> Word0;

  bits<13> arraybase;
  bits<2> type;
  bits<7> gpr;
  bits<2> elem_size;

  let Word0{12-0} = arraybase;
  let Word0{14-13} = type;
  let Word0{21-15} = gpr;
  let Word0{22} = 0; // RW_REL
  let Word0{29-23} = 0; // INDEX_GPR
  let Word0{31-30} = elem_size;
}

class ExportSwzWord1 {
  field bits<32> Word1;

  bits<3> sw_x;
  bits<3> sw_y;
  bits<3> sw_z;
  bits<3> sw_w;
  bits<1> eop;
  bits<8> inst;

  let Word1{2-0} = sw_x;
  let Word1{5-3} = sw_y;
  let Word1{8-6} = sw_z;
  let Word1{11-9} = sw_w;
}

class ExportBufWord1 {
  field bits<32> Word1;

  bits<12> arraySize;
  bits<4> compMask;
  bits<1> eop;
  bits<8> inst;

  let Word1{11-0} = arraySize;
  let Word1{15-12} = compMask;
}

multiclass ExportPattern<Instruction ExportInst, bits<8> cf_inst> {
  def : R600Pat<(R600_EXPORT (v4f32 R600_Reg128:$src), (i32 imm:$base), (i32 imm:$type),
    (i32 imm:$swz_x), (i32 imm:$swz_y), (i32 imm:$swz_z), (i32 imm:$swz_w)),
        (ExportInst R600_Reg128:$src, imm:$type, imm:$base,
        imm:$swz_x, imm:$swz_y, imm:$swz_z, imm:$swz_w, cf_inst, 0)
  >;

}

multiclass SteamOutputExportPattern<Instruction ExportInst,
    bits<8> buf0inst, bits<8> buf1inst, bits<8> buf2inst, bits<8> buf3inst> {
// Stream0
  def : R600Pat<(int_r600_store_stream_output (v4f32 R600_Reg128:$src),
      (i32 imm:$arraybase), (i32 0), (i32 imm:$mask)),
      (ExportInst R600_Reg128:$src, 0, imm:$arraybase,
      4095, imm:$mask, buf0inst, 0)>;
// Stream1
  def : R600Pat<(int_r600_store_stream_output (v4f32 R600_Reg128:$src),
      (i32 imm:$arraybase), (i32 1), (i32 imm:$mask)),
      (ExportInst $src, 0, imm:$arraybase,
      4095, imm:$mask, buf1inst, 0)>;
// Stream2
  def : R600Pat<(int_r600_store_stream_output (v4f32 R600_Reg128:$src),
      (i32 imm:$arraybase), (i32 2), (i32 imm:$mask)),
      (ExportInst $src, 0, imm:$arraybase,
      4095, imm:$mask, buf2inst, 0)>;
// Stream3
  def : R600Pat<(int_r600_store_stream_output (v4f32 R600_Reg128:$src),
      (i32 imm:$arraybase), (i32 3), (i32 imm:$mask)),
      (ExportInst $src, 0, imm:$arraybase,
      4095, imm:$mask, buf3inst, 0)>;
}

// Export Instructions should not be duplicated by TailDuplication pass
// (which assumes that duplicable instruction are affected by exec mask)
let usesCustomInserter = 1, isNotDuplicable = 1 in {

class ExportSwzInst : InstR600ISA<(
    outs),
    (ins R600_Reg128:$gpr, i32imm:$type, i32imm:$arraybase,
    RSel:$sw_x, RSel:$sw_y, RSel:$sw_z, RSel:$sw_w, i32imm:$inst,
    i32imm:$eop),
    !strconcat("EXPORT", " $gpr.$sw_x$sw_y$sw_z$sw_w"),
    []>, ExportWord0, ExportSwzWord1 {
  let elem_size = 3;
  let Inst{31-0} = Word0;
  let Inst{63-32} = Word1;
  let IsExport = 1;
}

} // End usesCustomInserter = 1

class ExportBufInst : InstR600ISA<(
    outs),
    (ins R600_Reg128:$gpr, i32imm:$type, i32imm:$arraybase,
    i32imm:$arraySize, i32imm:$compMask, i32imm:$inst, i32imm:$eop),
    !strconcat("EXPORT", " $gpr"),
    []>, ExportWord0, ExportBufWord1 {
  let elem_size = 0;
  let Inst{31-0} = Word0;
  let Inst{63-32} = Word1;
  let IsExport = 1;
}

//===----------------------------------------------------------------------===//
// Control Flow Instructions
//===----------------------------------------------------------------------===//


def KCACHE : InstFlag<"printKCache">;

class ALU_CLAUSE<bits<4> inst, string OpName> : R600WrapperInst <(outs),
(ins i32imm:$ADDR, i32imm:$KCACHE_BANK0, i32imm:$KCACHE_BANK1,
KCACHE:$KCACHE_MODE0, KCACHE:$KCACHE_MODE1,
i32imm:$KCACHE_ADDR0, i32imm:$KCACHE_ADDR1,
i32imm:$COUNT, i32imm:$Enabled),
!strconcat(OpName, " $COUNT, @$ADDR, "
"KC0[$KCACHE_MODE0], KC1[$KCACHE_MODE1]"),
[] >, CF_ALU_WORD0, CF_ALU_WORD1 {
  field bits<64> Inst;

  let CF_INST = inst;
  let ALT_CONST = 0;
  let WHOLE_QUAD_MODE = 0;
  let BARRIER = 1;
  let isCodeGenOnly = 1;
  let UseNamedOperandTable = 1;

  let Inst{31-0} = Word0;
  let Inst{63-32} = Word1;
}

class CF_WORD0_R600 {
  field bits<32> Word0;

  bits<32> ADDR;

  let Word0 = ADDR;
}

class CF_CLAUSE_R600 <bits<7> inst, dag ins, string AsmPrint> : R600WrapperInst <(outs),
ins, AsmPrint, [] >, CF_WORD0_R600, CF_WORD1_R600 {
  field bits<64> Inst;
  bits<4> CNT;

  let CF_INST = inst;
  let BARRIER = 1;
  let CF_CONST = 0;
  let VALID_PIXEL_MODE = 0;
  let COND = 0;
  let COUNT = CNT{2-0};
  let CALL_COUNT = 0;
  let COUNT_3 = CNT{3};
  let END_OF_PROGRAM = 0;
  let WHOLE_QUAD_MODE = 0;

  let Inst{31-0} = Word0;
  let Inst{63-32} = Word1;
}

class CF_CLAUSE_EG <bits<8> inst, dag ins, string AsmPrint> : R600WrapperInst <(outs),
ins, AsmPrint, [] >, CF_WORD0_EG, CF_WORD1_EG {
  field bits<64> Inst;

  let CF_INST = inst;
  let BARRIER = 1;
  let JUMPTABLE_SEL = 0;
  let CF_CONST = 0;
  let VALID_PIXEL_MODE = 0;
  let COND = 0;
  let END_OF_PROGRAM = 0;

  let Inst{31-0} = Word0;
  let Inst{63-32} = Word1;
}

def CF_ALU : ALU_CLAUSE<8, "ALU">;
def CF_ALU_PUSH_BEFORE : ALU_CLAUSE<9, "ALU_PUSH_BEFORE">;
def CF_ALU_POP_AFTER : ALU_CLAUSE<10, "ALU_POP_AFTER">;
def CF_ALU_CONTINUE : ALU_CLAUSE<13, "ALU_CONTINUE">;
def CF_ALU_BREAK : ALU_CLAUSE<14, "ALU_BREAK">;
def CF_ALU_ELSE_AFTER : ALU_CLAUSE<15, "ALU_ELSE_AFTER">;

def FETCH_CLAUSE : R600WrapperInst <(outs),
(ins i32imm:$addr), "Fetch clause starting at $addr:", [] > {
  field bits<8> Inst;
  bits<8> addr;
  let Inst = addr;
  let isCodeGenOnly = 1;
}

def ALU_CLAUSE : R600WrapperInst <(outs),
(ins i32imm:$addr), "ALU clause starting at $addr:", [] > {
  field bits<8> Inst;
  bits<8> addr;
  let Inst = addr;
  let isCodeGenOnly = 1;
}

def LITERALS : R600WrapperInst <(outs),
(ins LITERAL:$literal1, LITERAL:$literal2), "$literal1, $literal2", [] > {
  let isCodeGenOnly = 1;

  field bits<64> Inst;
  bits<32> literal1;
  bits<32> literal2;

  let Inst{31-0} = literal1;
  let Inst{63-32} = literal2;
}

def PAD : R600WrapperInst <(outs), (ins), "PAD", [] > {
  field bits<64> Inst;
}

//===----------------------------------------------------------------------===//
// Common Instructions R600, R700, Evergreen, Cayman
//===----------------------------------------------------------------------===//

let isCodeGenOnly = 1, isPseudo = 1 in {

let Namespace = "R600", usesCustomInserter = 1  in {

class FABS <RegisterClass rc> : AMDGPUShaderInst <
  (outs rc:$dst),
  (ins rc:$src0),
  "FABS $dst, $src0",
  [(set f32:$dst, (fabs f32:$src0))]
>;

class FNEG <RegisterClass rc> : AMDGPUShaderInst <
  (outs rc:$dst),
  (ins rc:$src0),
  "FNEG $dst, $src0",
  [(set f32:$dst, (fneg f32:$src0))]
>;

} // usesCustomInserter = 1

multiclass RegisterLoadStore <RegisterClass dstClass, Operand addrClass,
                    ComplexPattern addrPat> {
let UseNamedOperandTable = 1 in {

  def RegisterLoad : AMDGPUShaderInst <
    (outs dstClass:$dst),
    (ins addrClass:$addr, i32imm:$chan),
    "RegisterLoad $dst, $addr",
    [(set i32:$dst, (AMDGPUregister_load addrPat:$addr, (i32 timm:$chan)))]
  > {
    let isRegisterLoad = 1;
  }

  def RegisterStore : AMDGPUShaderInst <
    (outs),
    (ins dstClass:$val, addrClass:$addr, i32imm:$chan),
    "RegisterStore $val, $addr",
    [(AMDGPUregister_store i32:$val, addrPat:$addr, (i32 timm:$chan))]
  > {
    let isRegisterStore = 1;
  }
}
}

} // End isCodeGenOnly = 1, isPseudo = 1


def ADD : R600_2OP_Helper <0x0, "ADD", fadd>;
// Non-IEEE MUL: 0 * anything = 0
def MUL : R600_2OP_Helper <0x1, "MUL NON-IEEE">;
def MUL_IEEE : R600_2OP_Helper <0x2, "MUL_IEEE", fmul>;
// TODO: Do these actually match the regular fmin/fmax behavior?
def MAX : R600_2OP_Helper <0x3, "MAX", AMDGPUfmax_legacy>;
def MIN : R600_2OP_Helper <0x4, "MIN", AMDGPUfmin_legacy>;
// According to https://msdn.microsoft.com/en-us/library/windows/desktop/cc308050%28v=vs.85%29.aspx
// DX10 min/max returns the other operand if one is NaN,
// this matches http://llvm.org/docs/LangRef.html#llvm-minnum-intrinsic
def MAX_DX10 : R600_2OP_Helper <0x5, "MAX_DX10", fmaxnum>;
def MIN_DX10 : R600_2OP_Helper <0x6, "MIN_DX10", fminnum>;

// For the SET* instructions there is a naming conflict in TargetSelectionDAG.td,
// so some of the instruction names don't match the asm string.
// XXX: Use the defs in TargetSelectionDAG.td instead of intrinsics.
def SETE : R600_2OP <
  0x08, "SETE",
  [(set f32:$dst, (selectcc f32:$src0, f32:$src1, FP_ONE, FP_ZERO, COND_OEQ))]
>;

def SGT : R600_2OP <
  0x09, "SETGT",
  [(set f32:$dst, (selectcc f32:$src0, f32:$src1, FP_ONE, FP_ZERO, COND_OGT))]
>;

def SGE : R600_2OP <
  0xA, "SETGE",
  [(set f32:$dst, (selectcc f32:$src0, f32:$src1, FP_ONE, FP_ZERO, COND_OGE))]
>;

def SNE : R600_2OP <
  0xB, "SETNE",
  [(set f32:$dst, (selectcc f32:$src0, f32:$src1, FP_ONE, FP_ZERO, COND_UNE_NE))]
>;

def SETE_DX10 : R600_2OP <
  0xC, "SETE_DX10",
  [(set i32:$dst, (selectcc f32:$src0, f32:$src1, -1, 0, COND_OEQ))]
>;

def SETGT_DX10 : R600_2OP <
  0xD, "SETGT_DX10",
  [(set i32:$dst, (selectcc f32:$src0, f32:$src1, -1, 0, COND_OGT))]
>;

def SETGE_DX10 : R600_2OP <
  0xE, "SETGE_DX10",
  [(set i32:$dst, (selectcc f32:$src0, f32:$src1, -1, 0, COND_OGE))]
>;

// FIXME: This should probably be COND_ONE
def SETNE_DX10 : R600_2OP <
  0xF, "SETNE_DX10",
  [(set i32:$dst, (selectcc f32:$src0, f32:$src1, -1, 0, COND_UNE_NE))]
>;

// FIXME: Need combine for AMDGPUfract
def FRACT : R600_1OP_Helper <0x10, "FRACT", AMDGPUfract>;
def TRUNC : R600_1OP_Helper <0x11, "TRUNC", ftrunc>;
def CEIL : R600_1OP_Helper <0x12, "CEIL", fceil>;
def RNDNE : R600_1OP_Helper <0x13, "RNDNE", froundeven>;
def FLOOR : R600_1OP_Helper <0x14, "FLOOR", ffloor>;

def MOV : R600_1OP <0x19, "MOV", []>;


// This is a hack to get rid of DUMMY_CHAIN nodes.
// Most DUMMY_CHAINs should be eliminated during legalization, but undef
// values can sneak in some to selection.
let isPseudo = 1, isCodeGenOnly = 1 in {
def DUMMY_CHAIN : R600WrapperInst <
  (outs),
  (ins),
  "DUMMY_CHAIN",
  [(R600dummy_chain)]
>;
} // end let isPseudo = 1, isCodeGenOnly = 1


let isPseudo = 1, isCodeGenOnly = 1, usesCustomInserter = 1 in {

class MOV_IMM <Operand immType> : R600WrapperInst <
  (outs R600_Reg32:$dst),
  (ins immType:$imm),
  "",
  []
> {
  let Namespace = "R600";
}

} // end let isPseudo = 1, isCodeGenOnly = 1, usesCustomInserter = 1

def MOV_IMM_I32 : MOV_IMM<i32imm>;
def : R600Pat <
  (imm:$val),
  (MOV_IMM_I32 imm:$val)
>;

def MOV_IMM_GLOBAL_ADDR : MOV_IMM<i32imm>;
def : R600Pat <
  (AMDGPUconstdata_ptr tglobaladdr:$addr),
  (MOV_IMM_GLOBAL_ADDR tglobaladdr:$addr)
>;


def MOV_IMM_F32 : MOV_IMM<f32imm>;
def : R600Pat <
  (fpimm:$val),
  (MOV_IMM_F32  fpimm:$val)
>;

def PRED_SETE : R600_2OP <0x20, "PRED_SETE", []>;
def PRED_SETGT : R600_2OP <0x21, "PRED_SETGT", []>;
def PRED_SETGE : R600_2OP <0x22, "PRED_SETGE", []>;
def PRED_SETNE : R600_2OP <0x23, "PRED_SETNE", []>;

let hasSideEffects = 1 in {

def KILLGT : R600_2OP <0x2D, "KILLGT", []>;

} // end hasSideEffects

def AND_INT : R600_2OP_Helper <0x30, "AND_INT", and>;
def OR_INT : R600_2OP_Helper <0x31, "OR_INT", or>;
def XOR_INT : R600_2OP_Helper <0x32, "XOR_INT", xor>;
def NOT_INT : R600_1OP_Helper <0x33, "NOT_INT", not>;
def ADD_INT : R600_2OP_Helper <0x34, "ADD_INT", add>;
def SUB_INT : R600_2OP_Helper <0x35, "SUB_INT", sub>;
def MAX_INT : R600_2OP_Helper <0x36, "MAX_INT", smax>;
def MIN_INT : R600_2OP_Helper <0x37, "MIN_INT", smin>;
def MAX_UINT : R600_2OP_Helper <0x38, "MAX_UINT", umax>;
def MIN_UINT : R600_2OP_Helper <0x39, "MIN_UINT", umin>;

def SETE_INT : R600_2OP <
  0x3A, "SETE_INT",
  [(set i32:$dst, (selectcc i32:$src0, i32:$src1, -1, 0, SETEQ))]
>;

def SETGT_INT : R600_2OP <
  0x3B, "SETGT_INT",
  [(set i32:$dst, (selectcc i32:$src0, i32:$src1, -1, 0, SETGT))]
>;

def SETGE_INT : R600_2OP <
  0x3C, "SETGE_INT",
  [(set i32:$dst, (selectcc i32:$src0, i32:$src1, -1, 0, SETGE))]
>;

def SETNE_INT : R600_2OP <
  0x3D, "SETNE_INT",
  [(set i32:$dst, (selectcc i32:$src0, i32:$src1, -1, 0, SETNE))]
>;

def SETGT_UINT : R600_2OP <
  0x3E, "SETGT_UINT",
  [(set i32:$dst, (selectcc i32:$src0, i32:$src1, -1, 0, SETUGT))]
>;

def SETGE_UINT : R600_2OP <
  0x3F, "SETGE_UINT",
  [(set i32:$dst, (selectcc i32:$src0, i32:$src1, -1, 0, SETUGE))]
>;

def PRED_SETE_INT : R600_2OP <0x42, "PRED_SETE_INT", []>;
def PRED_SETGT_INT : R600_2OP <0x43, "PRED_SETGE_INT", []>;
def PRED_SETGE_INT : R600_2OP <0x44, "PRED_SETGE_INT", []>;
def PRED_SETNE_INT : R600_2OP <0x45, "PRED_SETNE_INT", []>;

def CNDE_INT : R600_3OP <
  0x1C, "CNDE_INT",
  [(set i32:$dst, (selectcc i32:$src0, 0, i32:$src1, i32:$src2, COND_EQ))]
>;

def CNDGE_INT : R600_3OP <
  0x1E, "CNDGE_INT",
  [(set i32:$dst, (selectcc i32:$src0, 0, i32:$src1, i32:$src2, COND_SGE))]
>;

def CNDGT_INT : R600_3OP <
  0x1D, "CNDGT_INT",
  [(set i32:$dst, (selectcc i32:$src0, 0, i32:$src1, i32:$src2, COND_SGT))]
>;

//===----------------------------------------------------------------------===//
// Texture instructions
//===----------------------------------------------------------------------===//

let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in {

class R600_TEX <bits<11> inst, string opName> :
  InstR600 <(outs R600_Reg128:$DST_GPR),
          (ins R600_Reg128:$SRC_GPR,
          RSel:$srcx, RSel:$srcy, RSel:$srcz, RSel:$srcw,
          i32imm:$offsetx, i32imm:$offsety, i32imm:$offsetz,
          RSel:$DST_SEL_X, RSel:$DST_SEL_Y, RSel:$DST_SEL_Z, RSel:$DST_SEL_W,
          i32imm:$RESOURCE_ID, i32imm:$SAMPLER_ID,
          CT:$COORD_TYPE_X, CT:$COORD_TYPE_Y, CT:$COORD_TYPE_Z,
          CT:$COORD_TYPE_W),
          !strconcat("  ", opName,
          " $DST_GPR.$DST_SEL_X$DST_SEL_Y$DST_SEL_Z$DST_SEL_W, "
          "$SRC_GPR.$srcx$srcy$srcz$srcw "
          "RID:$RESOURCE_ID SID:$SAMPLER_ID "
          "CT:$COORD_TYPE_X$COORD_TYPE_Y$COORD_TYPE_Z$COORD_TYPE_W"),
          [],
          NullALU>, TEX_WORD0, TEX_WORD1, TEX_WORD2 {
  let Inst{31-0} = Word0;
  let Inst{63-32} = Word1;

  let TEX_INST = inst{4-0};
  let SRC_REL = 0;
  let DST_REL = 0;
  let LOD_BIAS = 0;

  let INST_MOD = 0;
  let FETCH_WHOLE_QUAD = 0;
  let ALT_CONST = 0;
  let SAMPLER_INDEX_MODE = 0;
  let RESOURCE_INDEX_MODE = 0;

  let TEXInst = 1;
}

} // End mayLoad = 0, mayStore = 0, hasSideEffects = 0



def TEX_SAMPLE : R600_TEX <0x10, "TEX_SAMPLE">;
def TEX_SAMPLE_C : R600_TEX <0x18, "TEX_SAMPLE_C">;
def TEX_SAMPLE_L : R600_TEX <0x11, "TEX_SAMPLE_L">;
def TEX_SAMPLE_C_L : R600_TEX <0x19, "TEX_SAMPLE_C_L">;
def TEX_SAMPLE_LB : R600_TEX <0x12, "TEX_SAMPLE_LB">;
def TEX_SAMPLE_C_LB : R600_TEX <0x1A, "TEX_SAMPLE_C_LB">;
def TEX_LD : R600_TEX <0x03, "TEX_LD">;
def TEX_LDPTR : R600_TEX <0x03, "TEX_LDPTR"> {
  let INST_MOD = 1;
}
def TEX_GET_TEXTURE_RESINFO : R600_TEX <0x04, "TEX_GET_TEXTURE_RESINFO">;
def TEX_GET_GRADIENTS_H : R600_TEX <0x07, "TEX_GET_GRADIENTS_H">;
def TEX_GET_GRADIENTS_V : R600_TEX <0x08, "TEX_GET_GRADIENTS_V">;
def TEX_SET_GRADIENTS_H : R600_TEX <0x0B, "TEX_SET_GRADIENTS_H">;
def TEX_SET_GRADIENTS_V : R600_TEX <0x0C, "TEX_SET_GRADIENTS_V">;
def TEX_SAMPLE_G : R600_TEX <0x14, "TEX_SAMPLE_G">;
def TEX_SAMPLE_C_G : R600_TEX <0x1C, "TEX_SAMPLE_C_G">;

defm : TexPattern<0, TEX_SAMPLE>;
defm : TexPattern<1, TEX_SAMPLE_C>;
defm : TexPattern<2, TEX_SAMPLE_L>;
defm : TexPattern<3, TEX_SAMPLE_C_L>;
defm : TexPattern<4, TEX_SAMPLE_LB>;
defm : TexPattern<5, TEX_SAMPLE_C_LB>;
defm : TexPattern<6, TEX_LD, v4i32>;
defm : TexPattern<7, TEX_GET_TEXTURE_RESINFO, v4i32>;
defm : TexPattern<8, TEX_GET_GRADIENTS_H>;
defm : TexPattern<9, TEX_GET_GRADIENTS_V>;
defm : TexPattern<10, TEX_LDPTR, v4i32>;

//===----------------------------------------------------------------------===//
// Helper classes for common instructions
//===----------------------------------------------------------------------===//

class MUL_LIT_Common <bits<5> inst> : R600_3OP <
  inst, "MUL_LIT",
  []
>;

class MULADD_Common <bits<5> inst> : R600_3OP <
  inst, "MULADD",
  []
>;

class MULADD_IEEE_Common <bits<5> inst> : R600_3OP <
  inst, "MULADD_IEEE",
  [(set f32:$dst, (any_fmad f32:$src0, f32:$src1, f32:$src2))]
>;

class FMA_Common <bits<5> inst> : R600_3OP <
  inst, "FMA",
  [(set f32:$dst, (fma f32:$src0, f32:$src1, f32:$src2))], VecALU
>
{
  let OtherPredicates = [FMA];
}

class CNDE_Common <bits<5> inst> : R600_3OP <
  inst, "CNDE",
  [(set f32:$dst, (selectcc f32:$src0, FP_ZERO, f32:$src1, f32:$src2, COND_OEQ))]
>;

class CNDGT_Common <bits<5> inst> : R600_3OP <
  inst, "CNDGT",
  [(set f32:$dst, (selectcc f32:$src0, FP_ZERO, f32:$src1, f32:$src2, COND_OGT))]
> {
  let Itinerary = VecALU;
}

class CNDGE_Common <bits<5> inst> : R600_3OP <
  inst, "CNDGE",
  [(set f32:$dst, (selectcc f32:$src0, FP_ZERO, f32:$src1, f32:$src2, COND_OGE))]
> {
  let Itinerary = VecALU;
}


let isCodeGenOnly = 1, isPseudo = 1, Namespace = "R600"  in {
class R600_VEC2OP<list<dag> pattern> : InstR600 <(outs R600_Reg32:$dst), (ins
// Slot X
   UEM:$update_exec_mask_X, UP:$update_pred_X, WRITE:$write_X,
   OMOD:$omod_X, REL:$dst_rel_X, CLAMP:$clamp_X,
   R600_TReg32_X:$src0_X, NEG:$src0_neg_X, REL:$src0_rel_X, ABS:$src0_abs_X, SEL:$src0_sel_X,
   R600_TReg32_X:$src1_X, NEG:$src1_neg_X, REL:$src1_rel_X, ABS:$src1_abs_X, SEL:$src1_sel_X,
   R600_Pred:$pred_sel_X,
// Slot Y
   UEM:$update_exec_mask_Y, UP:$update_pred_Y, WRITE:$write_Y,
   OMOD:$omod_Y, REL:$dst_rel_Y, CLAMP:$clamp_Y,
   R600_TReg32_Y:$src0_Y, NEG:$src0_neg_Y, REL:$src0_rel_Y, ABS:$src0_abs_Y, SEL:$src0_sel_Y,
   R600_TReg32_Y:$src1_Y, NEG:$src1_neg_Y, REL:$src1_rel_Y, ABS:$src1_abs_Y, SEL:$src1_sel_Y,
   R600_Pred:$pred_sel_Y,
// Slot Z
   UEM:$update_exec_mask_Z, UP:$update_pred_Z, WRITE:$write_Z,
   OMOD:$omod_Z, REL:$dst_rel_Z, CLAMP:$clamp_Z,
   R600_TReg32_Z:$src0_Z, NEG:$src0_neg_Z, REL:$src0_rel_Z, ABS:$src0_abs_Z, SEL:$src0_sel_Z,
   R600_TReg32_Z:$src1_Z, NEG:$src1_neg_Z, REL:$src1_rel_Z, ABS:$src1_abs_Z, SEL:$src1_sel_Z,
   R600_Pred:$pred_sel_Z,
// Slot W
   UEM:$update_exec_mask_W, UP:$update_pred_W, WRITE:$write_W,
   OMOD:$omod_W, REL:$dst_rel_W, CLAMP:$clamp_W,
   R600_TReg32_W:$src0_W, NEG:$src0_neg_W, REL:$src0_rel_W, ABS:$src0_abs_W, SEL:$src0_sel_W,
   R600_TReg32_W:$src1_W, NEG:$src1_neg_W, REL:$src1_rel_W, ABS:$src1_abs_W, SEL:$src1_sel_W,
   R600_Pred:$pred_sel_W,
   LITERAL:$literal0, LITERAL:$literal1),
  "",
  pattern,
  AnyALU> {

  let UseNamedOperandTable = 1;

}
}

def DOT_4 : R600_VEC2OP<[(set R600_Reg32:$dst, (DOT4
  R600_TReg32_X:$src0_X, R600_TReg32_X:$src1_X,
  R600_TReg32_Y:$src0_Y, R600_TReg32_Y:$src1_Y,
  R600_TReg32_Z:$src0_Z, R600_TReg32_Z:$src1_Z,
  R600_TReg32_W:$src0_W, R600_TReg32_W:$src1_W))]>;


class DOT4_Common <bits<11> inst> : R600_2OP <inst, "DOT4", []>;


let mayLoad = 0, mayStore = 0, hasSideEffects = 0 in {
multiclass CUBE_Common <bits<11> inst> {

  def _pseudo : InstR600 <
    (outs R600_Reg128:$dst),
    (ins R600_Reg128:$src0),
    "CUBE $dst $src0",
    [(set v4f32:$dst, (int_r600_cube v4f32:$src0))],
    VecALU
  > {
    let isPseudo = 1;
    let UseNamedOperandTable = 1;
  }

  def _real : R600_2OP <inst, "CUBE", []>;
}
} // End mayLoad = 0, mayStore = 0, hasSideEffects = 0

class EXP_IEEE_Common <bits<11> inst> : R600_1OP_Helper <
  inst, "EXP_IEEE", AMDGPUexp
> {
  let Itinerary = TransALU;
}

class FLT_TO_INT_Common <bits<11> inst> : R600_1OP_Helper <
  inst, "FLT_TO_INT", fp_to_sint
> {
  let Itinerary = TransALU;
}

class INT_TO_FLT_Common <bits<11> inst> : R600_1OP_Helper <
  inst, "INT_TO_FLT", sint_to_fp
> {
  let Itinerary = TransALU;
}

class FLT_TO_UINT_Common <bits<11> inst> : R600_1OP_Helper <
  inst, "FLT_TO_UINT", fp_to_uint
> {
  let Itinerary = TransALU;
}

class UINT_TO_FLT_Common <bits<11> inst> : R600_1OP_Helper <
  inst, "UINT_TO_FLT", uint_to_fp
> {
  let Itinerary = TransALU;
}

class LOG_CLAMPED_Common <bits<11> inst> : R600_1OP <
  inst, "LOG_CLAMPED", []
>;

class LOG_IEEE_Common <bits<11> inst> : R600_1OP_Helper <
  inst, "LOG_IEEE", AMDGPUlog
> {
  let Itinerary = TransALU;
}

class LSHL_Common <bits<11> inst> : R600_2OP_Helper <inst, "LSHL", shl>;
class LSHR_Common <bits<11> inst> : R600_2OP_Helper <inst, "LSHR", srl>;
class ASHR_Common <bits<11> inst> : R600_2OP_Helper <inst, "ASHR", sra>;
class MULHI_INT_Common <bits<11> inst> : R600_2OP_Helper <
  inst, "MULHI_INT", mulhs> {
  let Itinerary = TransALU;
}

class MULHI_INT24_Common <bits<11> inst> : R600_2OP_Helper <
  inst, "MULHI_INT24", AMDGPUmulhi_i24> {
  let Itinerary = VecALU;
}

class MULHI_UINT_Common <bits<11> inst> : R600_2OP_Helper <
  inst, "MULHI", mulhu> {
  let Itinerary = TransALU;
}

class MULHI_UINT24_Common <bits<11> inst> : R600_2OP_Helper <
  inst, "MULHI_UINT24", AMDGPUmulhi_u24> {
  let Itinerary = VecALU;
}

class MULLO_INT_Common <bits<11> inst> : R600_2OP_Helper <
  inst, "MULLO_INT", mul> {
  let Itinerary = TransALU;
}
class MULLO_UINT_Common <bits<11> inst> : R600_2OP <inst, "MULLO_UINT", []> {
  let Itinerary = TransALU;
}

class RECIP_CLAMPED_Common <bits<11> inst> : R600_1OP <
  inst, "RECIP_CLAMPED", []
> {
  let Itinerary = TransALU;
}

class RECIP_IEEE_Common <bits<11> inst> : R600_1OP <
  inst, "RECIP_IEEE", [(set f32:$dst, (AMDGPUrcp f32:$src0))]
> {
  let Itinerary = TransALU;
}

class RECIP_UINT_Common <bits<11> inst> : R600_1OP_Helper <
  inst, "RECIP_UINT", AMDGPUurecip
> {
  let Itinerary = TransALU;
}

// Clamped to maximum.
class RECIPSQRT_CLAMPED_Common <bits<11> inst> : R600_1OP_Helper <
  inst, "RECIPSQRT_CLAMPED", AMDGPUrsq_clamp
> {
  let Itinerary = TransALU;
}

class RECIPSQRT_IEEE_Common <bits<11> inst> : R600_1OP_Helper <
  inst, "RECIPSQRT_IEEE", AMDGPUrsq> {
  let Itinerary = TransALU;
}

// TODO: There is also RECIPSQRT_FF which clamps to zero.

class SIN_Common <bits<11> inst> : R600_1OP <
  inst, "SIN", [(set f32:$dst, (SIN_HW f32:$src0))]>{
  let Trig = 1;
  let Itinerary = TransALU;
}

class COS_Common <bits<11> inst> : R600_1OP <
  inst, "COS", [(set f32:$dst, (COS_HW f32:$src0))]> {
  let Trig = 1;
  let Itinerary = TransALU;
}

def FABS_R600 : FABS<R600_Reg32>;
def FNEG_R600 : FNEG<R600_Reg32>;

//===----------------------------------------------------------------------===//
// Helper patterns for complex intrinsics
//===----------------------------------------------------------------------===//

// FIXME: Should be predicated on unsafe fp math.
multiclass DIV_Common <InstR600 recip_ieee> {
def : R600Pat<
  (fdiv f32:$src0, f32:$src1),
  (MUL_IEEE $src0, (recip_ieee $src1))
>;

def : RcpPat<recip_ieee, f32>;
}

class SqrtPat<Instruction RsqInst, Instruction RecipInst> : R600Pat <
  (fsqrt f32:$src),
  (RecipInst (RsqInst $src))
>;

//===----------------------------------------------------------------------===//
// R600 / R700 Instructions
//===----------------------------------------------------------------------===//

let Predicates = [isR600] in {

  def MUL_LIT_r600 : MUL_LIT_Common<0x0C>;
  def MULADD_r600 : MULADD_Common<0x10>;
  def MULADD_IEEE_r600 : MULADD_IEEE_Common<0x14>;
  def CNDE_r600 : CNDE_Common<0x18>;
  def CNDGT_r600 : CNDGT_Common<0x19>;
  def CNDGE_r600 : CNDGE_Common<0x1A>;
  def DOT4_r600 : DOT4_Common<0x50>;
  defm CUBE_r600 : CUBE_Common<0x52>;
  def EXP_IEEE_r600 : EXP_IEEE_Common<0x61>;
  def LOG_CLAMPED_r600 : LOG_CLAMPED_Common<0x62>;
  def LOG_IEEE_r600 : LOG_IEEE_Common<0x63>;
  def RECIP_CLAMPED_r600 : RECIP_CLAMPED_Common<0x64>;
  def RECIP_IEEE_r600 : RECIP_IEEE_Common<0x66>;
  def RECIPSQRT_CLAMPED_r600 : RECIPSQRT_CLAMPED_Common<0x67>;
  def RECIPSQRT_IEEE_r600 : RECIPSQRT_IEEE_Common<0x69>;
  def FLT_TO_INT_r600 : FLT_TO_INT_Common<0x6b>;
  def INT_TO_FLT_r600 : INT_TO_FLT_Common<0x6c>;
  def FLT_TO_UINT_r600 : FLT_TO_UINT_Common<0x79>;
  def UINT_TO_FLT_r600 : UINT_TO_FLT_Common<0x6d>;
  def SIN_r600 : SIN_Common<0x6E>;
  def COS_r600 : COS_Common<0x6F>;
  def ASHR_r600 : ASHR_Common<0x70>;
  def LSHR_r600 : LSHR_Common<0x71>;
  def LSHL_r600 : LSHL_Common<0x72>;
  def MULLO_INT_r600 : MULLO_INT_Common<0x73>;
  def MULHI_INT_r600 : MULHI_INT_Common<0x74>;
  def MULLO_UINT_r600 : MULLO_UINT_Common<0x75>;
  def MULHI_UINT_r600 : MULHI_UINT_Common<0x76>;
  def RECIP_UINT_r600 : RECIP_UINT_Common <0x78>;

  defm DIV_r600 : DIV_Common<RECIP_IEEE_r600>;
  def : POW_Common <LOG_IEEE_r600, EXP_IEEE_r600, MUL>;

  def : SqrtPat<RECIPSQRT_IEEE_r600, RECIP_IEEE_r600>;

  def R600_ExportSwz : ExportSwzInst {
    let Word1{20-17} = 0; // BURST_COUNT
    let Word1{21} = eop;
    let Word1{22} = 0; // VALID_PIXEL_MODE
    let Word1{30-23} = inst;
    let Word1{31} = 1; // BARRIER
  }
  defm : ExportPattern<R600_ExportSwz, 39>;

  def R600_ExportBuf : ExportBufInst {
    let Word1{20-17} = 0; // BURST_COUNT
    let Word1{21} = eop;
    let Word1{22} = 0; // VALID_PIXEL_MODE
    let Word1{30-23} = inst;
    let Word1{31} = 1; // BARRIER
  }
  defm : SteamOutputExportPattern<R600_ExportBuf, 0x20, 0x21, 0x22, 0x23>;

  def CF_TC_R600 : CF_CLAUSE_R600<1, (ins i32imm:$ADDR, i32imm:$CNT),
  "TEX $CNT @$ADDR"> {
    let POP_COUNT = 0;
  }
  def CF_VC_R600 : CF_CLAUSE_R600<2, (ins i32imm:$ADDR, i32imm:$CNT),
  "VTX $CNT @$ADDR"> {
    let POP_COUNT = 0;
  }
  def WHILE_LOOP_R600 : CF_CLAUSE_R600<6, (ins i32imm:$ADDR),
  "LOOP_START_DX10 @$ADDR"> {
    let POP_COUNT = 0;
    let CNT = 0;
  }
  def END_LOOP_R600 : CF_CLAUSE_R600<5, (ins i32imm:$ADDR), "END_LOOP @$ADDR"> {
    let POP_COUNT = 0;
    let CNT = 0;
  }
  def LOOP_BREAK_R600 : CF_CLAUSE_R600<9, (ins i32imm:$ADDR),
  "LOOP_BREAK @$ADDR"> {
    let POP_COUNT = 0;
    let CNT = 0;
  }
  def CF_CONTINUE_R600 : CF_CLAUSE_R600<8, (ins i32imm:$ADDR),
  "CONTINUE @$ADDR"> {
    let POP_COUNT = 0;
    let CNT = 0;
  }
  def CF_JUMP_R600 : CF_CLAUSE_R600<10, (ins i32imm:$ADDR, i32imm:$POP_COUNT),
  "JUMP @$ADDR POP:$POP_COUNT"> {
    let CNT = 0;
  }
  def CF_PUSH_ELSE_R600 : CF_CLAUSE_R600<12, (ins i32imm:$ADDR),
  "PUSH_ELSE @$ADDR"> {
    let CNT = 0;
    let POP_COUNT = 0; // FIXME?
  }
  def CF_ELSE_R600 : CF_CLAUSE_R600<13, (ins i32imm:$ADDR, i32imm:$POP_COUNT),
  "ELSE @$ADDR POP:$POP_COUNT"> {
    let CNT = 0;
  }
  def CF_CALL_FS_R600 : CF_CLAUSE_R600<19, (ins), "CALL_FS"> {
    let ADDR = 0;
    let CNT = 0;
    let POP_COUNT = 0;
  }
  def POP_R600 : CF_CLAUSE_R600<14, (ins i32imm:$ADDR, i32imm:$POP_COUNT),
  "POP @$ADDR POP:$POP_COUNT"> {
    let CNT = 0;
  }
  def CF_END_R600 : CF_CLAUSE_R600<0, (ins), "CF_END"> {
    let CNT = 0;
    let POP_COUNT = 0;
    let ADDR = 0;
    let END_OF_PROGRAM = 1;
  }

}


//===----------------------------------------------------------------------===//
// Register loads and stores - for indirect addressing
//===----------------------------------------------------------------------===//

let Namespace = "R600" in {
defm R600_ : RegisterLoadStore <R600_Reg32, FRAMEri, ADDRIndirect>;
}

// Hardcode channel to 0
// NOTE: LSHR is not available here. LSHR is per family instruction
def : R600Pat <
  (i32 (load_private ADDRIndirect:$addr) ),
  (R600_RegisterLoad FRAMEri:$addr, (i32 0))
>;
def : R600Pat <
  (store_private i32:$val, ADDRIndirect:$addr),
  (R600_RegisterStore i32:$val, FRAMEri:$addr, (i32 0))
>;


//===----------------------------------------------------------------------===//
// Pseudo instructions
//===----------------------------------------------------------------------===//

let isPseudo = 1 in {

def PRED_X : InstR600 <
  (outs R600_Predicate_Bit:$dst),
  (ins R600_Reg32:$src0, i32imm:$src1, i32imm:$flags),
  "", [], NullALU> {
  let FlagOperandIdx = 3;
}

let isTerminator = 1, isBranch = 1 in {
def JUMP_COND : InstR600 <
          (outs),
          (ins brtarget:$target, R600_Predicate_Bit:$p),
          "JUMP $target ($p)",
          [], AnyALU
  >;

def JUMP : InstR600 <
          (outs),
          (ins brtarget:$target),
          "JUMP $target",
          [], AnyALU
  >
{
  let isPredicable = 1;
  let isBarrier = 1;
}

}  // End isTerminator = 1, isBranch = 1

let usesCustomInserter = 1 in {

let mayLoad = 0, mayStore = 0, hasSideEffects = 1 in {

def MASK_WRITE : InstR600 <
    (outs),
    (ins R600_Reg32:$src),
    "MASK_WRITE $src",
    [],
    NullALU
>;

} // End mayLoad = 0, mayStore = 0, hasSideEffects = 1


def TXD: InstR600 <
  (outs R600_Reg128:$dst),
  (ins R600_Reg128:$src0, R600_Reg128:$src1, R600_Reg128:$src2,
       i32imm:$resourceId, i32imm:$samplerId, i32imm:$textureTarget),
  "TXD $dst, $src0, $src1, $src2, $resourceId, $samplerId, $textureTarget", [],
  NullALU > {
  let TEXInst = 1;
}

def TXD_SHADOW: InstR600 <
  (outs R600_Reg128:$dst),
  (ins R600_Reg128:$src0, R600_Reg128:$src1, R600_Reg128:$src2,
       i32imm:$resourceId, i32imm:$samplerId, i32imm:$textureTarget),
  "TXD_SHADOW $dst, $src0, $src1, $src2, $resourceId, $samplerId, $textureTarget",
  [], NullALU> {
  let TEXInst = 1;
}
} // End isPseudo = 1
} // End usesCustomInserter = 1


//===----------------------------------------------------------------------===//
// Constant Buffer Addressing Support
//===----------------------------------------------------------------------===//

let usesCustomInserter = 1, isCodeGenOnly = 1, isPseudo = 1, Namespace = "R600"  in {
def CONST_COPY : Instruction {
  let OutOperandList = (outs R600_Reg32:$dst);
  let InOperandList = (ins i32imm:$src);
  let Pattern =
      [(set R600_Reg32:$dst, (CONST_ADDRESS ADDRGA_CONST_OFFSET:$src))];
  let AsmString = "CONST_COPY";
  let hasSideEffects = 0;
  let isAsCheapAsAMove = 1;
  let Itinerary = NullALU;
}
} // end usesCustomInserter = 1, isCodeGenOnly = 1, isPseudo = 1, Namespace = "AMDGPU"

def TEX_VTX_CONSTBUF :
  InstR600ISA <(outs R600_Reg128:$dst_gpr), (ins (MEMxi $src_gpr, $src_index):$src, i32imm:$buffer_id), "VTX_READ_eg $dst_gpr, $src",
      [(set v4i32:$dst_gpr, (CONST_ADDRESS ADDRGA_VAR_OFFSET:$src, (i32 imm:$buffer_id)))]>,
  VTX_WORD1_GPR, VTX_WORD0_eg {

  let VC_INST = 0;
  let FETCH_TYPE = 2;
  let FETCH_WHOLE_QUAD = 0;
  let SRC_REL = 0;
  let SRC_SEL_X = 0;
  let DST_REL = 0;
  let USE_CONST_FIELDS = 0;
  let NUM_FORMAT_ALL = 2;
  let FORMAT_COMP_ALL = 1;
  let SRF_MODE_ALL = 1;
  let MEGA_FETCH_COUNT = 16;
  let DST_SEL_X        = 0;
  let DST_SEL_Y        = 1;
  let DST_SEL_Z        = 2;
  let DST_SEL_W        = 3;
  let DATA_FORMAT      = 35;

  let Inst{31-0} = Word0;
  let Inst{63-32} = Word1;

// LLVM can only encode 64-bit instructions, so these fields are manually
// encoded in R600CodeEmitter
//
// bits<16> OFFSET;
// bits<2>  ENDIAN_SWAP = 0;
// bits<1>  CONST_BUF_NO_STRIDE = 0;
// bits<1>  MEGA_FETCH = 0;
// bits<1>  ALT_CONST = 0;
// bits<2>  BUFFER_INDEX_MODE = 0;



// VTX_WORD2 (LLVM can only encode 64-bit instructions, so WORD2 encoding
// is done in R600CodeEmitter
//
// Inst{79-64} = OFFSET;
// Inst{81-80} = ENDIAN_SWAP;
// Inst{82}    = CONST_BUF_NO_STRIDE;
// Inst{83}    = MEGA_FETCH;
// Inst{84}    = ALT_CONST;
// Inst{86-85} = BUFFER_INDEX_MODE;
// Inst{95-86} = 0; Reserved

// VTX_WORD3 (Padding)
//
// Inst{127-96} = 0;
  let VTXInst = 1;
}

def TEX_VTX_TEXBUF:
  InstR600ISA <(outs R600_Reg128:$dst_gpr), (ins (MEMxi $src_gpr, $src_index):$src, i32imm:$buffer_id), "TEX_VTX_EXPLICIT_READ $dst_gpr, $src">,
VTX_WORD1_GPR, VTX_WORD0_eg {

let VC_INST = 0;
let FETCH_TYPE = 2;
let FETCH_WHOLE_QUAD = 0;
let SRC_REL = 0;
let SRC_SEL_X = 0;
let DST_REL = 0;
let USE_CONST_FIELDS = 1;
let NUM_FORMAT_ALL = 0;
let FORMAT_COMP_ALL = 0;
let SRF_MODE_ALL = 1;
let MEGA_FETCH_COUNT = 16;
let DST_SEL_X        = 0;
let DST_SEL_Y        = 1;
let DST_SEL_Z        = 2;
let DST_SEL_W        = 3;
let DATA_FORMAT      = 0;

let Inst{31-0} = Word0;
let Inst{63-32} = Word1;

// LLVM can only encode 64-bit instructions, so these fields are manually
// encoded in R600CodeEmitter
//
// bits<16> OFFSET;
// bits<2>  ENDIAN_SWAP = 0;
// bits<1>  CONST_BUF_NO_STRIDE = 0;
// bits<1>  MEGA_FETCH = 0;
// bits<1>  ALT_CONST = 0;
// bits<2>  BUFFER_INDEX_MODE = 0;



// VTX_WORD2 (LLVM can only encode 64-bit instructions, so WORD2 encoding
// is done in R600CodeEmitter
//
// Inst{79-64} = OFFSET;
// Inst{81-80} = ENDIAN_SWAP;
// Inst{82}    = CONST_BUF_NO_STRIDE;
// Inst{83}    = MEGA_FETCH;
// Inst{84}    = ALT_CONST;
// Inst{86-85} = BUFFER_INDEX_MODE;
// Inst{95-86} = 0; Reserved

// VTX_WORD3 (Padding)
//
// Inst{127-96} = 0;
  let VTXInst = 1;
}

//===---------------------------------------------------------------------===//
// Flow and Program control Instructions
//===---------------------------------------------------------------------===//

multiclass BranchConditional<SDNode Op, RegisterClass rci, RegisterClass rcf> {
    def _i32 : ILFormat<(outs),
  (ins brtarget:$target, rci:$src0),
        "; i32 Pseudo branch instruction",
  [(Op bb:$target, (i32 rci:$src0))]>;
    def _f32 : ILFormat<(outs),
  (ins brtarget:$target, rcf:$src0),
        "; f32 Pseudo branch instruction",
  [(Op bb:$target, (f32 rcf:$src0))]>;
}

// Only scalar types should generate flow control
multiclass BranchInstr<string name> {
  def _i32 : ILFormat<(outs), (ins R600_Reg32:$src),
      !strconcat(name, " $src"), []>;
  def _f32 : ILFormat<(outs), (ins R600_Reg32:$src),
      !strconcat(name, " $src"), []>;
}
// Only scalar types should generate flow control
multiclass BranchInstr2<string name> {
  def _i32 : ILFormat<(outs), (ins R600_Reg32:$src0, R600_Reg32:$src1),
      !strconcat(name, " $src0, $src1"), []>;
  def _f32 : ILFormat<(outs), (ins R600_Reg32:$src0, R600_Reg32:$src1),
      !strconcat(name, " $src0, $src1"), []>;
}

//===---------------------------------------------------------------------===//
// Custom Inserter for Branches and returns, this eventually will be a
// separate pass
//===---------------------------------------------------------------------===//
let isTerminator = 1, usesCustomInserter = 1, isBranch = 1, isBarrier = 1,
    Namespace = "R600" in {
  def BRANCH : ILFormat<(outs), (ins brtarget:$target),
      "; Pseudo unconditional branch instruction",
      [(br bb:$target)]>;
  defm BRANCH_COND : BranchConditional<IL_brcond, R600_Reg32, R600_Reg32>;
}

//===----------------------------------------------------------------------===//
// Branch Instructions
//===----------------------------------------------------------------------===//

def IF_PREDICATE_SET  : ILFormat<(outs), (ins R600_Reg32:$src),
  "IF_PREDICATE_SET $src", []>;

let isTerminator=1 in {
  def BREAK       : ILFormat< (outs), (ins),
      "BREAK", []>;
  def CONTINUE    : ILFormat< (outs), (ins),
      "CONTINUE", []>;
  def DEFAULT     : ILFormat< (outs), (ins),
      "DEFAULT", []>;
  def ELSE        : ILFormat< (outs), (ins),
      "ELSE", []>;
  def ENDSWITCH   : ILFormat< (outs), (ins),
      "ENDSWITCH", []>;
  def ENDMAIN     : ILFormat< (outs), (ins),
      "ENDMAIN", []>;
  def END         : ILFormat< (outs), (ins),
      "END", []>;
  def ENDFUNC     : ILFormat< (outs), (ins),
      "ENDFUNC", []>;
  def ENDIF       : ILFormat< (outs), (ins),
      "ENDIF", []>;
  def WHILELOOP   : ILFormat< (outs), (ins),
      "WHILE", []>;
  def ENDLOOP     : ILFormat< (outs), (ins),
      "ENDLOOP", []>;
  def FUNC        : ILFormat< (outs), (ins),
      "FUNC", []>;
  def RETDYN      : ILFormat< (outs), (ins),
      "RET_DYN", []>;
  // This opcode has custom swizzle pattern encoded in Swizzle Encoder
  defm IF_LOGICALNZ  : BranchInstr<"IF_LOGICALNZ">;
  // This opcode has custom swizzle pattern encoded in Swizzle Encoder
  defm IF_LOGICALZ   : BranchInstr<"IF_LOGICALZ">;
  // This opcode has custom swizzle pattern encoded in Swizzle Encoder
  defm BREAK_LOGICALNZ : BranchInstr<"BREAK_LOGICALNZ">;
  // This opcode has custom swizzle pattern encoded in Swizzle Encoder
  defm BREAK_LOGICALZ : BranchInstr<"BREAK_LOGICALZ">;
  // This opcode has custom swizzle pattern encoded in Swizzle Encoder
  defm CONTINUE_LOGICALNZ : BranchInstr<"CONTINUE_LOGICALNZ">;
  // This opcode has custom swizzle pattern encoded in Swizzle Encoder
  defm CONTINUE_LOGICALZ : BranchInstr<"CONTINUE_LOGICALZ">;
  defm IFC         : BranchInstr2<"IFC">;
  defm BREAKC      : BranchInstr2<"BREAKC">;
  defm CONTINUEC   : BranchInstr2<"CONTINUEC">;
}

//===----------------------------------------------------------------------===//
// Indirect addressing pseudo instructions
//===----------------------------------------------------------------------===//

let isPseudo = 1 in {

class ExtractVertical <RegisterClass vec_rc> : InstR600 <
  (outs R600_Reg32:$dst),
  (ins vec_rc:$vec, R600_Reg32:$index), "",
  [],
  AnyALU
>;

let Constraints = "$dst = $vec" in {

class InsertVertical <RegisterClass vec_rc> : InstR600 <
  (outs vec_rc:$dst),
  (ins vec_rc:$vec, R600_Reg32:$value, R600_Reg32:$index), "",
  [],
  AnyALU
>;

} // End Constraints = "$dst = $vec"

} // End isPseudo = 1

def R600_EXTRACT_ELT_V2 : ExtractVertical <R600_Reg64Vertical>;
def R600_EXTRACT_ELT_V4 : ExtractVertical <R600_Reg128Vertical>;

def R600_INSERT_ELT_V2 : InsertVertical <R600_Reg64Vertical>;
def R600_INSERT_ELT_V4 : InsertVertical <R600_Reg128Vertical>;

class ExtractVerticalPat <Instruction inst, ValueType vec_ty,
                          ValueType scalar_ty> : R600Pat <
  (scalar_ty (extractelt vec_ty:$vec, i32:$index)),
  (inst $vec, $index)
>;

def : ExtractVerticalPat <R600_EXTRACT_ELT_V2, v2i32, i32>;
def : ExtractVerticalPat <R600_EXTRACT_ELT_V2, v2f32, f32>;
def : ExtractVerticalPat <R600_EXTRACT_ELT_V4, v4i32, i32>;
def : ExtractVerticalPat <R600_EXTRACT_ELT_V4, v4f32, f32>;

class InsertVerticalPat <Instruction inst, ValueType vec_ty,
                         ValueType scalar_ty> : R600Pat <
  (vec_ty (insertelt vec_ty:$vec, scalar_ty:$value, i32:$index)),
  (inst $vec, $value, $index)
>;

def : InsertVerticalPat <R600_INSERT_ELT_V2, v2i32, i32>;
def : InsertVerticalPat <R600_INSERT_ELT_V2, v2f32, f32>;
def : InsertVerticalPat <R600_INSERT_ELT_V4, v4i32, i32>;
def : InsertVerticalPat <R600_INSERT_ELT_V4, v4f32, f32>;

//===----------------------------------------------------------------------===//
// ISel Patterns
//===----------------------------------------------------------------------===//

let SubtargetPredicate = isR600toCayman in {

// CND*_INT Patterns for f32 True / False values

class CND_INT_f32 <InstR600 cnd, CondCode cc> : R600Pat <
  (selectcc i32:$src0, 0, f32:$src1, f32:$src2, cc),
  (cnd $src0, $src1, $src2)
>;

def : CND_INT_f32 <CNDE_INT,  SETEQ>;
def : CND_INT_f32 <CNDGT_INT, SETGT>;
def : CND_INT_f32 <CNDGE_INT, SETGE>;

//CNDGE_INT extra pattern
def : R600Pat <
  (selectcc i32:$src0, -1, i32:$src1, i32:$src2, COND_SGT),
  (CNDGE_INT $src0, $src1, $src2)
>;

// KIL Patterns
def KIL : R600Pat <
  (int_r600_kill f32:$src0),
  (MASK_WRITE (KILLGT (f32 ZERO), $src0))
>;

def : Extract_Element <f32, v4f32, 0, sub0>;
def : Extract_Element <f32, v4f32, 1, sub1>;
def : Extract_Element <f32, v4f32, 2, sub2>;
def : Extract_Element <f32, v4f32, 3, sub3>;

def : Insert_Element <f32, v4f32, 0, sub0>;
def : Insert_Element <f32, v4f32, 1, sub1>;
def : Insert_Element <f32, v4f32, 2, sub2>;
def : Insert_Element <f32, v4f32, 3, sub3>;

def : Extract_Element <i32, v4i32, 0, sub0>;
def : Extract_Element <i32, v4i32, 1, sub1>;
def : Extract_Element <i32, v4i32, 2, sub2>;
def : Extract_Element <i32, v4i32, 3, sub3>;

def : Insert_Element <i32, v4i32, 0, sub0>;
def : Insert_Element <i32, v4i32, 1, sub1>;
def : Insert_Element <i32, v4i32, 2, sub2>;
def : Insert_Element <i32, v4i32, 3, sub3>;

def : Extract_Element <f32, v2f32, 0, sub0>;
def : Extract_Element <f32, v2f32, 1, sub1>;

def : Insert_Element <f32, v2f32, 0, sub0>;
def : Insert_Element <f32, v2f32, 1, sub1>;

def : Extract_Element <i32, v2i32, 0, sub0>;
def : Extract_Element <i32, v2i32, 1, sub1>;

def : Insert_Element <i32, v2i32, 0, sub0>;
def : Insert_Element <i32, v2i32, 1, sub1>;

// bitconvert patterns

def : BitConvert <i32, f32, R600_Reg32>;
def : BitConvert <f32, i32, R600_Reg32>;
def : BitConvert <v2f32, v2i32, R600_Reg64>;
def : BitConvert <v2i32, v2f32, R600_Reg64>;
def : BitConvert <v4f32, v4i32, R600_Reg128>;
def : BitConvert <v4i32, v4f32, R600_Reg128>;

// DWORDADDR pattern
def : DwordAddrPat  <i32, R600_Reg32>;

} // End SubtargetPredicate = isR600toCayman

def getLDSNoRetOp : InstrMapping {
  let FilterClass = "R600_LDS_1A1D";
  let RowFields = ["BaseOp"];
  let ColFields = ["DisableEncoding"];
  let KeyCol = ["$dst"];
  let ValueCols = [[""""]];
}

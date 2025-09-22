//===-- SPIRVInstrInfo.td - Target Description for SPIR-V Target ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the SPIR-V instructions in TableGen format.
//
//===----------------------------------------------------------------------===//

include "SPIRVInstrFormats.td"
include "SPIRVSymbolicOperands.td"

// Codegen only metadata instructions
let isCodeGenOnly=1 in {
  def ASSIGN_TYPE: Pseudo<(outs ANYID:$dst_id), (ins ANYID:$src_id, TYPE:$src_ty)>;
  def DECL_TYPE: Pseudo<(outs ANYID:$dst_id), (ins ANYID:$src_id, TYPE:$src_ty)>;
  def GET_ID: Pseudo<(outs ID:$dst_id), (ins ANYID:$src)>;
  def GET_ID64: Pseudo<(outs ID64:$dst_id), (ins ANYID:$src)>;
  def GET_fID: Pseudo<(outs fID:$dst_id), (ins ANYID:$src)>;
  def GET_fID64: Pseudo<(outs fID64:$dst_id), (ins ANYID:$src)>;
  def GET_pID32: Pseudo<(outs pID32:$dst_id), (ins ANYID:$src)>;
  def GET_pID64: Pseudo<(outs pID64:$dst_id), (ins ANYID:$src)>;
  def GET_vID: Pseudo<(outs vID:$dst_id), (ins ANYID:$src)>;
  def GET_vfID: Pseudo<(outs vfID:$dst_id), (ins ANYID:$src)>;
  def GET_vpID32: Pseudo<(outs vpID32:$dst_id), (ins ANYID:$src)>;
  def GET_vpID64: Pseudo<(outs vpID64:$dst_id), (ins ANYID:$src)>;
}

def SPVTypeBin : SDTypeProfile<1, 2, []>;

def assigntype : SDNode<"SPIRVISD::AssignType", SPVTypeBin>;

def : GINodeEquiv<ASSIGN_TYPE, assigntype>;

class BinOp<string name, bits<16> opCode, list<dag> pattern=[]>
                : Op<opCode, (outs ANYID:$dst), (ins TYPE:$src_ty, ANYID:$src, ANYID:$src2),
                  "$dst = "#name#" $src_ty $src $src2", pattern>;

class BinOpTyped<string name, bits<16> opCode, RegisterClass CID, SDNode node>
                : Op<opCode, (outs ID:$dst), (ins TYPE:$src_ty, CID:$src, CID:$src2),
                  "$dst = "#name#" $src_ty $src $src2", [(set ID:$dst, (assigntype (node CID:$src, CID:$src2), TYPE:$src_ty))]>;

class TernOpTyped<string name, bits<16> opCode, RegisterClass CCond, RegisterClass CID, SDNode node>
                : Op<opCode, (outs ID:$dst), (ins TYPE:$src_ty, CCond:$cond, CID:$src1, CID:$src2),
                  "$dst = "#name#" $src_ty $cond $src1 $src2", [(set ID:$dst, (assigntype (node CCond:$cond, CID:$src1, CID:$src2), TYPE:$src_ty))]>;

multiclass BinOpTypedGen<string name, bits<16> opCode, SDNode node, bit genF = 0, bit genV = 0> {
  if genF then
    def S: BinOpTyped<name, opCode, fID, node>;
  else
    def S: BinOpTyped<name, opCode, ID, node>;
  if genV then {
    if genF then
      def V: BinOpTyped<name, opCode, vfID, node>;
    else
      def V: BinOpTyped<name, opCode, vID, node>;
  }
}

multiclass TernOpTypedGen<string name, bits<16> opCode, SDNode node, bit genP = 1, bit genI = 1, bit genF = 0, bit genV = 0> {
  if genF then {
    def SFSCond: TernOpTyped<name, opCode, ID, fID, node>;
    def SFVCond: TernOpTyped<name, opCode, vID, fID, node>;
  }
  if genI then {
    def SISCond: TernOpTyped<name, opCode, ID, ID, node>;
    def SIVCond: TernOpTyped<name, opCode, vID, ID, node>;
  }
  if genP then {
    def SPSCond32: TernOpTyped<name, opCode, ID, pID32, node>;
    def SPVCond32: TernOpTyped<name, opCode, vID, pID32, node>;
    def SPSCond64: TernOpTyped<name, opCode, ID, pID64, node>;
    def SPVCond64: TernOpTyped<name, opCode, vID, pID64, node>;
  }
  if genV then {
    if genF then {
      def VFSCond: TernOpTyped<name, opCode, ID, vfID, node>;
      def VFVCond: TernOpTyped<name, opCode, vID, vfID, node>;
    }
    if genI then {
      def VISCond: TernOpTyped<name, opCode, ID, vID, node>;
      def VIVCond: TernOpTyped<name, opCode, vID, vID, node>;
    }
    if genP then {
      def VPSCond32: TernOpTyped<name, opCode, ID, vpID32, node>;
      def VPVCond32: TernOpTyped<name, opCode, vID, vpID32, node>;
      def VPSCond64: TernOpTyped<name, opCode, ID, vpID64, node>;
      def VPVCond64: TernOpTyped<name, opCode, vID, vpID64, node>;
    }
  }
}

class UnOp<string name, bits<16> opCode, list<dag> pattern=[]>
                : Op<opCode, (outs ANYID:$dst), (ins TYPE:$type, ANYID:$src),
                  "$dst = "#name#" $type $src", pattern>;
class UnOpTyped<string name, bits<16> opCode, RegisterClass CID, SDNode node>
                : Op<opCode, (outs ID:$dst), (ins TYPE:$src_ty, CID:$src),
                  "$dst = "#name#" $src_ty $src", [(set ID:$dst, (assigntype (node CID:$src), TYPE:$src_ty))]>;

class SimpleOp<string name, bits<16> opCode>: Op<opCode, (outs), (ins), name>;

// 3.42.1 Miscellaneous Instructions

def OpNop: SimpleOp<"OpNop", 0>;
def OpUndef: Op<1, (outs ID:$res), (ins TYPE:$type), "$res = OpUndef $type">;
def OpSizeOf: Op<321, (outs ID:$res), (ins TYPE:$ty, ID:$ptr), "$res = OpSizeOf $ty $ptr">;

//  - SPV_KHR_expect_assume : Expect assume instructions
def OpAssumeTrueKHR: Op<5630, (outs), (ins ID:$cond), "OpAssumeTrueKHR $cond">;
def OpExpectKHR: Op<5631, (outs ID:$res), (ins TYPE:$ty, ID:$val, ID:$expected), "$res = OpExpectKHR $ty $val $expected">;

// 3.42.2 Debug Instructions

def OpSourceContinued: Op<2, (outs), (ins StringImm:$str, variable_ops),
                  "OpSourceContinued $str">;
def OpSource: Op<3, (outs), (ins SourceLanguage:$lang, i32imm:$version, variable_ops),
                  "OpSource $lang $version">;
def OpSourceExtension: Op<4, (outs), (ins StringImm:$extension, variable_ops),
                  "OpSourceExtension $extension">;
def OpName: Op<5, (outs), (ins ANY:$tar, StringImm:$name, variable_ops), "OpName $tar $name">;
def OpMemberName: Op<6, (outs), (ins TYPE:$ty, i32imm:$mem, StringImm:$name, variable_ops),
                  "OpMemberName $ty $mem $name">;
def OpString: Op<7, (outs ID:$r), (ins StringImm:$s, variable_ops), "$r = OpString $s">;
def OpLine: Op<8, (outs), (ins ID:$file, i32imm:$ln, i32imm:$col), "OpLine $file $ln $col">;
def OpNoLine: Op<317, (outs), (ins), "OpNoLine">;
def OpModuleProcessed: Op<330, (outs), (ins StringImm:$process, variable_ops),
                  "OpModuleProcessed $process">;

// 3.42.3 Annotation Instructions

def OpDecorate: Op<71, (outs), (ins ANY:$target, Decoration:$dec, variable_ops),
                  "OpDecorate $target $dec">;
def OpMemberDecorate: Op<72, (outs), (ins TYPE:$t, i32imm:$m, Decoration:$d, variable_ops),
                  "OpMemberDecorate $t $m $d">;

// TODO Currently some deprecated opcodes are missing: OpDecorationGroup,
// OpGroupDecorate and OpGroupMemberDecorate

def OpDecorateId: Op<332, (outs), (ins ANY:$target, Decoration:$dec, variable_ops),
                  "OpDecorateId $target $dec">;
def OpDecorateString: Op<5632, (outs), (ins ANY:$t, Decoration:$d, StringImm:$s, variable_ops),
                  "OpDecorateString $t $d $s">;
def OpMemberDecorateString: Op<5633, (outs),
                  (ins TYPE:$ty, i32imm:$mem, Decoration:$dec, StringImm:$str, variable_ops),
                  "OpMemberDecorateString $ty $mem $dec $str">;

// 3.42.4 Extension Instructions

def OpExtension: Op<10, (outs), (ins StringImm:$name, variable_ops), "OpExtension $name">;
def OpExtInstImport: Op<11, (outs ID:$res), (ins StringImm:$extInstsName, variable_ops),
                  "$res = OpExtInstImport $extInstsName">;
def OpExtInst: Op<12, (outs ID:$res), (ins TYPE:$ty, ID:$set, Extension:$inst, variable_ops),
                  "$res = OpExtInst $ty $set $inst">;

// 3.42.5 Mode-Setting Instructions

def OpMemoryModel: Op<14, (outs), (ins AddressingModel:$addr, MemoryModel:$mem),
                  "OpMemoryModel $addr $mem">;
def OpEntryPoint: Op<15, (outs),
                  (ins ExecutionModel:$model, ID:$entry, StringImm:$name, variable_ops),
                  "OpEntryPoint $model $entry $name">;
def OpExecutionMode: Op<16, (outs), (ins ID:$entry, ExecutionMode:$mode, variable_ops),
                  "OpExecutionMode $entry $mode">;
def OpCapability: Op<17, (outs), (ins Capability:$cap), "OpCapability $cap">;
def OpExecutionModeId: Op<331, (outs), (ins ID:$entry, ExecutionMode:$mode, variable_ops),
                  "OpExecutionModeId $entry $mode">;

// 3.42.6 Type-Declaration Instructions

def OpTypeVoid: Op<19, (outs TYPE:$type), (ins), "$type = OpTypeVoid">;
def OpTypeBool: Op<20, (outs TYPE:$type), (ins), "$type = OpTypeBool">;
def OpTypeInt: Op<21, (outs TYPE:$type), (ins i32imm:$width, i32imm:$signedness),
                  "$type = OpTypeInt $width $signedness">;
def OpTypeFloat: Op<22, (outs TYPE:$type), (ins i32imm:$width),
                  "$type = OpTypeFloat $width">;
def OpTypeVector: Op<23, (outs TYPE:$type), (ins TYPE:$compType, i32imm:$compCount),
                  "$type = OpTypeVector $compType $compCount">;
def OpTypeMatrix: Op<24, (outs TYPE:$type), (ins TYPE:$colType, i32imm:$colCount),
                  "$type = OpTypeMatrix $colType $colCount">;
def OpTypeImage: Op<25, (outs TYPE:$res), (ins TYPE:$sampTy, Dim:$dim, i32imm:$depth,
      i32imm:$arrayed, i32imm:$MS, i32imm:$sampled, ImageFormat:$imFormat, variable_ops),
                  "$res = OpTypeImage $sampTy $dim $depth $arrayed $MS $sampled $imFormat">;
def OpTypeSampler: Op<26, (outs TYPE:$res), (ins), "$res = OpTypeSampler">;
def OpTypeSampledImage: Op<27, (outs TYPE:$res), (ins TYPE:$imageType),
                  "$res = OpTypeSampledImage $imageType">;
def OpTypeArray: Op<28, (outs TYPE:$type), (ins TYPE:$elementType, ID:$length),
                  "$type = OpTypeArray $elementType $length">;
def OpTypeRuntimeArray: Op<29, (outs TYPE:$type), (ins TYPE:$elementType),
                  "$type = OpTypeRuntimeArray $elementType">;
def OpTypeStruct: Op<30, (outs TYPE:$res), (ins variable_ops), "$res = OpTypeStruct">;
def OpTypeOpaque: Op<31, (outs TYPE:$res), (ins StringImm:$name, variable_ops),
                  "$res = OpTypeOpaque $name">;
def OpTypePointer: Op<32, (outs TYPE:$res), (ins StorageClass:$storage, TYPE:$type),
                  "$res = OpTypePointer $storage $type">;
def OpTypeFunction: Op<33, (outs TYPE:$funcType), (ins TYPE:$returnType, variable_ops),
                  "$funcType = OpTypeFunction $returnType">;
def OpTypeEvent: Op<34, (outs TYPE:$res), (ins), "$res = OpTypeEvent">;
def OpTypeDeviceEvent: Op<35, (outs TYPE:$res), (ins), "$res = OpTypeDeviceEvent">;
def OpTypeReserveId: Op<36, (outs TYPE:$res), (ins), "$res = OpTypeReserveId">;
def OpTypeQueue: Op<37, (outs TYPE:$res), (ins), "$res = OpTypeQueue">;
def OpTypePipe: Op<38, (outs TYPE:$res), (ins AccessQualifier:$a), "$res = OpTypePipe $a">;
def OpTypeForwardPointer: Op<39, (outs), (ins TYPE:$ptrType, StorageClass:$storageClass),
                  "OpTypeForwardPointer $ptrType $storageClass">;
def OpTypePipeStorage: Op<322, (outs TYPE:$res), (ins), "$res = OpTypePipeStorage">;
def OpTypeNamedBarrier: Op<327, (outs TYPE:$res), (ins), "$res = OpTypeNamedBarrier">;
def OpTypeAccelerationStructureNV: Op<5341, (outs TYPE:$res), (ins),
                  "$res = OpTypeAccelerationStructureNV">;
def OpTypeCooperativeMatrixNV: Op<5358, (outs TYPE:$res),
                  (ins TYPE:$compType, ID:$scope, ID:$rows, ID:$cols),
                  "$res = OpTypeCooperativeMatrixNV $compType $scope $rows $cols">;
def OpTypeCooperativeMatrixKHR: Op<4456, (outs TYPE:$res),
                  (ins TYPE:$compType, ID:$scope, ID:$rows, ID:$cols, ID:$use),
                  "$res = OpTypeCooperativeMatrixKHR $compType $scope $rows $cols $use">;

// 3.42.7 Constant-Creation Instructions

def imm_to_i32 : SDNodeXForm<imm, [{
return CurDAG->getTargetConstant(
  N->getValueAP().bitcastToAPInt().getZExtValue(), SDLoc(N), MVT::i32);
}]>;

def fimm_to_i32 : SDNodeXForm<imm, [{
return CurDAG->getTargetConstant(
  N->getValueAPF().bitcastToAPInt().getZExtValue(), SDLoc(N), MVT::i32);
}]>;

def gi_bitcast_fimm_to_i32 : GICustomOperandRenderer<"renderFImm32">,
  GISDNodeXFormEquiv<fimm_to_i32>;

def gi_bitcast_imm_to_i32 : GICustomOperandRenderer<"renderImm32">,
  GISDNodeXFormEquiv<imm_to_i32>;

def PseudoConstI: IntImmLeaf<i32, [{ return Imm.getBitWidth() <= 32; }], imm_to_i32>;
def PseudoConstF: FPImmLeaf<f32, [{  return true; }], fimm_to_i32>;
def ConstPseudoTrue: IntImmLeaf<i32, [{ return Imm.getBitWidth() == 1 && Imm.getZExtValue() == 1; }]>;
def ConstPseudoFalse: IntImmLeaf<i32, [{ return Imm.getBitWidth() == 1 && Imm.getZExtValue() == 0; }]>;
def ConstPseudoNull: IntImmLeaf<i64, [{ return Imm.isZero(); }]>;

multiclass IntFPImm<bits<16> opCode, string name> {
  def I: Op<opCode, (outs ID:$dst), (ins TYPE:$type, ID:$src, variable_ops),
                  "$dst = "#name#" $type", [(set ID:$dst, (assigntype PseudoConstI:$src, TYPE:$type))]>;
  def F: Op<opCode, (outs ID:$dst), (ins TYPE:$type, fID:$src, variable_ops),
                  "$dst = "#name#" $type", [(set ID:$dst, (assigntype PseudoConstF:$src, TYPE:$type))]>;
}

def OpConstantTrue: Op<41, (outs ID:$dst), (ins TYPE:$src_ty), "$dst = OpConstantTrue $src_ty",
                      [(set ID:$dst, (assigntype ConstPseudoTrue, TYPE:$src_ty))]>;
def OpConstantFalse: Op<42, (outs ID:$dst), (ins TYPE:$src_ty), "$dst = OpConstantFalse $src_ty",
                      [(set ID:$dst, (assigntype ConstPseudoFalse, TYPE:$src_ty))]>;

defm OpConstant: IntFPImm<43, "OpConstant">;

def OpConstantComposite: Op<44, (outs ID:$res), (ins TYPE:$type, variable_ops),
                  "$res = OpConstantComposite $type">;
def OpConstantSampler: Op<45, (outs ID:$res),
                  (ins TYPE:$t, SamplerAddressingMode:$s, i32imm:$p, SamplerFilterMode:$f),
                  "$res = OpConstantSampler $t $s $p $f">;
def OpConstantNull: Op<46, (outs ID:$dst), (ins TYPE:$src_ty), "$dst = OpConstantNull $src_ty",
                      [(set ID:$dst, (assigntype ConstPseudoNull, TYPE:$src_ty))]>;

def OpSpecConstantTrue: Op<48, (outs ID:$r), (ins TYPE:$t), "$r = OpSpecConstantTrue $t">;
def OpSpecConstantFalse: Op<49, (outs ID:$r), (ins TYPE:$t), "$r = OpSpecConstantFalse $t">;
def OpSpecConstant: Op<50, (outs ID:$res), (ins TYPE:$type, i32imm:$imm, variable_ops),
                  "$res = OpSpecConstant $type $imm">;
def OpSpecConstantComposite: Op<51, (outs ID:$res), (ins TYPE:$type, variable_ops),
                  "$res = OpSpecConstantComposite $type">;
def OpSpecConstantOp: Op<52, (outs ID:$res), (ins TYPE:$t, i32imm:$c, ID:$o, variable_ops),
                  "$res = OpSpecConstantOp $t $c $o">;

// 3.42.8 Memory Instructions

def OpVariable: Op<59, (outs ID:$res), (ins TYPE:$type, StorageClass:$sc, variable_ops),
                  "$res = OpVariable $type $sc">;
def OpImageTexelPointer: Op<60, (outs ID:$res),
                  (ins TYPE:$resType, ID:$image, ID:$coord, ID:$sample),
                  "$res = OpImageTexelPointer $resType $image $coord $sample">;
def OpLoad: Op<61, (outs ID:$res), (ins TYPE:$resType, ID:$pointer, variable_ops),
                  "$res = OpLoad $resType $pointer">;
def OpStore: Op<62, (outs), (ins ID:$pointer, ID:$objectToStore, variable_ops),
                  "OpStore $pointer $objectToStore">;
def OpCopyMemory: Op<63, (outs), (ins ID:$dest, ID:$src, variable_ops),
                  "OpCopyMemory $dest $src">;
def OpCopyMemorySized: Op<64, (outs), (ins ID:$dest, ID:$src, ID:$size, variable_ops),
                  "OpCopyMemorySized $dest $src $size">;
def OpAccessChain: Op<65, (outs ID:$res), (ins TYPE:$type, ID:$base, variable_ops),
                  "$res = OpAccessChain $type $base">;
def OpInBoundsAccessChain: Op<66, (outs ID:$res),
                  (ins TYPE:$type, ID:$base, variable_ops),
                  "$res = OpInBoundsAccessChain $type $base">;
def OpPtrAccessChain: Op<67, (outs ID:$res),
                  (ins TYPE:$type, ID:$base, ID:$element, variable_ops),
                  "$res = OpPtrAccessChain $type $base $element">;
def OpArrayLength: Op<68, (outs ID:$res), (ins TYPE:$resTy, ID:$struct, i32imm:$arrayMember),
                  "$res = OpArrayLength $resTy $struct $arrayMember">;
def OpGenericPtrMemSemantics: Op<69, (outs ID:$res), (ins TYPE:$resType, ID:$pointer),
                  "$res = OpGenericPtrMemSemantics $resType $pointer">;
def OpInBoundsPtrAccessChain: Op<70, (outs ID:$res),
                  (ins TYPE:$type, ID:$base, ID:$element, variable_ops),
                  "$res = OpInBoundsPtrAccessChain $type $base $element">;
def OpPtrEqual: Op<401, (outs ID:$res), (ins TYPE:$resType, ID:$a, ID:$b),
                  "$res = OpPtrEqual $resType $a $b">;
def OpPtrNotEqual: Op<402, (outs ID:$res), (ins TYPE:$resType, ID:$a, ID:$b),
                  "$res = OpPtrNotEqual $resType $a $b">;
def OpPtrDiff: Op<403, (outs ID:$res), (ins TYPE:$resType, ID:$a, ID:$b),
                  "$res = OpPtrDiff $resType $a $b">;

// - SPV_INTEL_variable_length_array

def OpVariableLengthArrayINTEL: Op<5818, (outs ID:$res), (ins TYPE:$type, ID:$length),
                  "$res = OpVariableLengthArrayINTEL $type $length">;
def OpSaveMemoryINTEL: Op<5819, (outs ID:$res), (ins TYPE:$type),
                  "$res = OpSaveMemoryINTEL $type">;
def OpRestoreMemoryINTEL: Op<5820, (outs), (ins ID:$ptr),
                  "OpRestoreMemoryINTEL $ptr">;

// 3.42.9 Function Instructions

def OpFunction: Op<54, (outs ID:$func),
                  (ins TYPE:$resType, FunctionControl:$funcControl, TYPE:$funcType),
                  "$func = OpFunction $resType $funcControl $funcType">;
def OpFunctionParameter: Op<55, (outs ID:$arg), (ins TYPE:$type),
                  "$arg = OpFunctionParameter $type">;
def OpFunctionEnd: Op<56, (outs), (ins), "OpFunctionEnd"> {
  let isTerminator=1;
}
def OpFunctionCall: Op<57, (outs ID:$res), (ins TYPE:$resType, ID:$function, variable_ops),
                  "$res = OpFunctionCall $resType $function">;

// 3.42.10 Image Instructions

def OpSampledImage: BinOp<"OpSampledImage", 86>;

def OpImageSampleImplicitLod: Op<87, (outs ID:$res),
                  (ins TYPE:$type, ID:$sampledImage, ID:$coord, variable_ops),
                  "$res = OpImageSampleImplicitLod $type $sampledImage $coord">;
def OpImageSampleExplicitLod: Op<88, (outs ID:$res),
                  (ins TYPE:$ty, ID:$sImage, ID:$uv, ImageOperand:$op, ID:$i, variable_ops),
                  "$res = OpImageSampleExplicitLod $ty $sImage $uv $op $i">;

def OpImageSampleDrefImplicitLod: Op<89, (outs ID:$res),
                  (ins TYPE:$type, ID:$sampledImage, ID:$coord, ID:$dref, variable_ops),
                  "$res = OpImageSampleDrefImplicitLod $type $sampledImage $dref $coord">;
def OpImageSampleDrefExplicitLod: Op<90, (outs ID:$res),
                  (ins TYPE:$ty, ID:$im, ID:$uv, ID:$d, ImageOperand:$op, ID:$i, variable_ops),
                  "$res = OpImageSampleDrefExplicitLod $ty $im $uv $d $op $i">;

def OpImageSampleProjImplicitLod: Op<91, (outs ID:$res),
                  (ins TYPE:$type, ID:$sampledImage, ID:$coord, variable_ops),
                  "$res = OpImageSampleProjImplicitLod $type $sampledImage $coord">;
def OpImageSampleProjExplicitLod: Op<92, (outs ID:$res),
                  (ins TYPE:$ty, ID:$im, ID:$uv, ID:$d, ImageOperand:$op, ID:$i, variable_ops),
                  "$res = OpImageSampleProjExplicitLod $ty $im $uv $op $i">;

def OpImageSampleProjDrefImplicitLod: Op<93, (outs ID:$res),
                  (ins TYPE:$type, ID:$sampledImage, ID:$coord, ID:$dref, variable_ops),
                  "$res = OpImageSampleProjDrefImplicitLod $type $sampledImage $dref $coord">;
def OpImageSampleProjDrefExplicitLod: Op<94, (outs ID:$res),
                  (ins TYPE:$ty, ID:$im, ID:$uv, ID:$d, ImageOperand:$op, ID:$i, variable_ops),
                  "$res = OpImageSampleProjDrefExplicitLod $ty $im $uv $d $op $i">;

def OpImageFetch: Op<95, (outs ID:$res),
                  (ins TYPE:$type, ID:$image, ID:$coord, variable_ops),
                  "$res = OpImageFetch $type $image $coord">;
def OpImageGather: Op<96, (outs ID:$res),
                  (ins TYPE:$type, ID:$sampledImage, ID:$coord, ID:$component, variable_ops),
                  "$res = OpImageGather $type $sampledImage $coord $component">;
def OpImageDrefGather: Op<97, (outs ID:$res),
                  (ins TYPE:$type, ID:$sampledImage, ID:$coord, ID:$dref, variable_ops),
                  "$res = OpImageDrefGather $type $sampledImage $coord $dref">;

def OpImageRead: Op<98, (outs ID:$res),
                  (ins TYPE:$type, ID:$image, ID:$coord, variable_ops),
                  "$res = OpImageRead $type $image $coord">;
def OpImageWrite: Op<99, (outs), (ins ID:$image, ID:$coord, ID:$texel, variable_ops),
                  "OpImageWrite $image $coord $texel">;

def OpImage: UnOp<"OpImage", 100>;
def OpImageQueryFormat: UnOp<"OpImageQueryFormat", 101>;
def OpImageQueryOrder: UnOp<"OpImageQueryOrder", 102>;
def OpImageQuerySizeLod: BinOp<"OpImageQuerySizeLod", 103>;
def OpImageQuerySize: UnOp<"OpImageQuerySize", 104>;
def OpImageQueryLod: BinOp<"OpImageQueryLod", 105>;
def OpImageQueryLevels: UnOp<"OpImageQueryLevels", 106>;
def OpImageQuerySamples: UnOp<"OpImageQuerySamples", 107>;

def OpImageSparseSampleImplicitLod: Op<305, (outs ID:$res),
                  (ins TYPE:$type, ID:$sampledImage, ID:$coord, variable_ops),
                  "$res = OpImageSparseSampleImplicitLod $type $sampledImage $coord">;
def OpImageSparseSampleExplicitLod: Op<306, (outs ID:$res),
                  (ins TYPE:$ty, ID:$sImage, ID:$uv, ImageOperand:$op, ID:$i, variable_ops),
                  "$res = OpImageSparseSampleExplicitLod $ty $sImage $uv $op $i">;

def OpImageSparseSampleDrefImplicitLod: Op<307, (outs ID:$res),
                  (ins TYPE:$type, ID:$sampledImg, ID:$coord, ID:$dref, variable_ops),
                  "$res = OpImageSparseSampleDrefImplicitLod $type $sampledImg $dref $coord">;
def OpImageSparseSampleDrefExplicitLod: Op<308, (outs ID:$res),
                  (ins TYPE:$ty, ID:$im, ID:$uv, ID:$d, ImageOperand:$op, ID:$i, variable_ops),
                  "$res = OpImageSparseSampleDrefExplicitLod $ty $im $uv $d $op $i">;

def OpImageSparseSampleProjImplicitLod: Op<309, (outs ID:$res),
                  (ins TYPE:$type, ID:$sampledImage, ID:$coord, variable_ops),
                  "$res = OpImageSparseSampleProjImplicitLod $type $sampledImage $coord">;
def OpImageSparseSampleProjExplicitLod: Op<310, (outs ID:$res),
                  (ins TYPE:$ty, ID:$im, ID:$uv, ID:$d, ImageOperand:$op, ID:$i, variable_ops),
                  "$res = OpImageSparseSampleProjExplicitLod $ty $im $uv $op $i">;

def OpImageSparseSampleProjDrefImplicitLod: Op<311, (outs ID:$res),
                  (ins TYPE:$type, ID:$sImage, ID:$coord, ID:$dref, variable_ops),
                  "$res = OpImageSparseSampleProjDrefImplicitLod $type $sImage $dref $coord">;
def OpImageSparseSampleProjDrefExplicitLod: Op<312, (outs ID:$res),
                  (ins TYPE:$ty, ID:$im, ID:$uv, ID:$d, ImageOperand:$op, ID:$i, variable_ops),
                  "$res = OpImageSparseSampleProjDrefExplicitLod $ty $im $uv $d $op $i">;

def OpImageSparseFetch: Op<313, (outs ID:$res),
                  (ins TYPE:$type, ID:$image, ID:$coord, variable_ops),
                  "$res = OpImageSparseFetch $type $image $coord">;
def OpImageSparseGather: Op<314, (outs ID:$res),
                  (ins TYPE:$type, ID:$sampledImage, ID:$coord, ID:$component, variable_ops),
                  "$res = OpImageSparseGather $type $sampledImage $coord $component">;
def OpImageSparseDrefGather: Op<315, (outs ID:$res),
                  (ins TYPE:$type, ID:$sampledImage, ID:$coord, ID:$dref, variable_ops),
                  "$res = OpImageSparseDrefGather $type $sampledImage $coord $dref">;

def OpImageSparseTexelsResident: UnOp<"OpImageSparseTexelsResident", 316>;

def OpImageSparseRead: Op<320, (outs ID:$res),
                  (ins TYPE:$type, ID:$image, ID:$coord, variable_ops),
                  "$res = OpImageSparseRead $type $image $coord">;

def OpImageSampleFootprintNV: Op<5283, (outs ID:$res),
                  (ins TYPE:$ty, ID:$sImg, ID:$uv, ID:$granularity, ID:$coarse, variable_ops),
                  "$res = OpImageSampleFootprintNV $ty $sImg $uv $granularity $coarse">;

// 3.42.11 Conversion instructions

def OpConvertFToU : UnOp<"OpConvertFToU", 109>;
def OpConvertFToS : UnOp<"OpConvertFToS", 110>;
def OpConvertSToF : UnOp<"OpConvertSToF", 111>;
def OpConvertUToF : UnOp<"OpConvertUToF", 112>;

def OpUConvert : UnOp<"OpUConvert", 113>;
def OpSConvert : UnOp<"OpSConvert", 114>;
def OpFConvert : UnOp<"OpFConvert", 115>;

def OpQuantizeToF16 : UnOp<"OpQuantizeToF16", 116>;

def OpConvertPtrToU : UnOp<"OpConvertPtrToU", 117>;

def OpSatConvertSToU : UnOp<"OpSatConvertSToU", 118>;
def OpSatConvertUToS : UnOp<"OpSatConvertUToS", 119>;

def OpConvertUToPtr : UnOp<"OpConvertUToPtr", 120>;
def OpPtrCastToGeneric : UnOp<"OpPtrCastToGeneric", 121>;
def OpGenericCastToPtr : UnOp<"OpGenericCastToPtr", 122>;
def OpGenericCastToPtrExplicit : Op<123, (outs ID:$r), (ins TYPE:$t, ID:$p, StorageClass:$s),
                              "$r = OpGenericCastToPtrExplicit $t $p $s">;
def OpBitcast : UnOp<"OpBitcast", 124>;

// SPV_INTEL_usm_storage_classes
def OpPtrCastToCrossWorkgroupINTEL : UnOp<"OpPtrCastToCrossWorkgroupINTEL", 5934>;
def OpCrossWorkgroupCastToPtrINTEL : UnOp<"OpCrossWorkgroupCastToPtrINTEL", 5938>;

// SPV_INTEL_bfloat16_conversion
def OpConvertFToBF16INTEL : UnOp<"OpConvertFToBF16INTEL", 6116>;
def OpConvertBF16ToFINTEL : UnOp<"OpConvertBF16ToFINTEL", 6117>;

// 3.42.12 Composite Instructions

def OpVectorExtractDynamic: Op<77, (outs ID:$res), (ins TYPE:$type, vID:$vec, ID:$idx),
                  "$res = OpVectorExtractDynamic $type $vec $idx", [(set ID:$res, (assigntype (extractelt vID:$vec, ID:$idx), TYPE:$type))]>;

def OpVectorInsertDynamic: Op<78, (outs ID:$res), (ins TYPE:$ty, ID:$vec, ID:$comp, ID:$idx),
                  "$res = OpVectorInsertDynamic $ty $vec $comp $idx">;
def OpVectorShuffle: Op<79, (outs ID:$res), (ins TYPE:$ty, ID:$v1, ID:$v2, variable_ops),
                  "$res = OpVectorShuffle $ty $v1 $v2">;
def OpCompositeConstruct: Op<80, (outs ID:$res), (ins TYPE:$type, variable_ops),
                  "$res = OpCompositeConstruct $type">;
def OpCompositeExtract: Op<81, (outs ID:$res), (ins TYPE:$type, ID:$base, variable_ops),
                  "$res = OpCompositeExtract $type $base">;
def OpCompositeInsert: Op<82, (outs ID:$r), (ins TYPE:$ty, ID:$obj, ID:$base, variable_ops),
                  "$r = OpCompositeInsert $ty $obj $base">;
def OpCopyObject: UnOp<"OpCopyObject", 83>;
def OpTranspose: UnOp<"OpTranspose", 84>;
def OpCopyLogical: UnOp<"OpCopyLogical", 400>;

// 3.42.13 Arithmetic Instructions

def OpSNegate: UnOp<"OpSNegate", 126>;
def OpFNegate: UnOpTyped<"OpFNegate", 127, fID, fneg>;
def OpFNegateV: UnOpTyped<"OpFNegate", 127, vfID, fneg>;
defm OpIAdd: BinOpTypedGen<"OpIAdd", 128, add, 0, 1>;
defm OpFAdd: BinOpTypedGen<"OpFAdd", 129, fadd, 1, 1>;

defm OpISub: BinOpTypedGen<"OpISub", 130, sub, 0, 1>;
defm OpFSub: BinOpTypedGen<"OpFSub", 131, fsub, 1, 1>;

defm OpIMul: BinOpTypedGen<"OpIMul", 132, mul, 0, 1>;
defm OpFMul: BinOpTypedGen<"OpFMul", 133, fmul, 1, 1>;

defm OpUDiv: BinOpTypedGen<"OpUDiv", 134, udiv, 0, 1>;
defm OpSDiv: BinOpTypedGen<"OpSDiv", 135, sdiv, 0, 1>;
defm OpFDiv: BinOpTypedGen<"OpFDiv", 136, fdiv, 1, 1>;

defm OpUMod: BinOpTypedGen<"OpUMod", 137, urem, 0, 1>;
defm OpSRem: BinOpTypedGen<"OpSRem", 138, srem, 0, 1>;

def OpSMod: BinOp<"OpSMod", 139>;

defm OpFRem: BinOpTypedGen<"OpFRem", 140, frem, 1, 1>;
def OpFMod: BinOp<"OpFMod", 141>;

def OpVectorTimesScalar: BinOp<"OpVectorTimesScalar", 142>;
def OpMatrixTimesScalar: BinOp<"OpMatrixTimesScalar", 143>;
def OpVectorTimesMatrix: BinOp<"OpVectorTimesMatrix", 144>;
def OpMatrixTimesVector: BinOp<"OpMatrixTimesVector", 145>;
def OpMatrixTimesMatrix: BinOp<"OpMatrixTimesMatrix", 146>;

def OpOuterProduct: BinOp<"OpOuterProduct", 147>;
def OpDot: BinOp<"OpDot", 148>;

def OpIAddCarry: BinOpTyped<"OpIAddCarry", 149, ID, addc>;
def OpISubBorrow: BinOpTyped<"OpISubBorrow", 150, ID, subc>;
def OpUMulExtended: BinOp<"OpUMulExtended", 151>;
def OpSMulExtended: BinOp<"OpSMulExtended", 152>;

// 3.42.14 Bit Instructions

defm OpShiftRightLogical: BinOpTypedGen<"OpShiftRightLogical", 194, srl, 0, 1>;
defm OpShiftRightArithmetic: BinOpTypedGen<"OpShiftRightArithmetic", 195, sra, 0, 1>;
defm OpShiftLeftLogical: BinOpTypedGen<"OpShiftLeftLogical", 196, shl, 0, 1>;

defm OpBitwiseOr: BinOpTypedGen<"OpBitwiseOr", 197, or, 0, 1>;
defm OpBitwiseXor: BinOpTypedGen<"OpBitwiseXor", 198, xor, 0, 1>;
defm OpBitwiseAnd: BinOpTypedGen<"OpBitwiseAnd", 199, and, 0, 1>;
def OpNot: UnOp<"OpNot", 200>;

def OpBitFieldInsert: Op<201, (outs ID:$res),
                  (ins TYPE:$ty, ID:$base, ID:$insert, ID:$offset, ID:$count),
                  "$res = OpBitFieldInsert $ty $base $insert $offset $count">;
def OpBitFieldSExtract: Op<202, (outs ID:$res),
                  (ins TYPE:$ty, ID:$base, ID:$offset, ID:$count),
                  "$res = OpBitFieldSExtract $ty $base $offset $count">;
def OpBitFieldUExtract: Op<203, (outs ID:$res),
                  (ins TYPE:$ty, ID:$base, ID:$offset, ID:$count),
                  "$res = OpBitFieldUExtract $ty $base $offset $count">;
def OpBitReverse: Op<204, (outs ID:$r), (ins TYPE:$ty, ID:$b), "$r = OpBitReverse $ty $b">;
def OpBitCount: Op<205, (outs ID:$r), (ins TYPE:$ty, ID:$b), "$r = OpBitCount $ty $b">;

// 3.42.15 Relational and Logical Instructions

def OpAny: Op<154, (outs ID:$res), (ins TYPE:$ty, ID:$vec),
                  "$res = OpAny $ty $vec">;
def OpAll: Op<155, (outs ID:$res), (ins TYPE:$ty, ID:$vec),
                  "$res = OpAll $ty $vec">;

def OpIsNan: UnOp<"OpIsNan", 156>;
def OpIsInf: UnOp<"OpIsInf", 157>;
def OpIsFinite: UnOp<"OpIsFinite", 158>;
def OpIsNormal: UnOp<"OpIsNormal", 159>;
def OpSignBitSet: UnOp<"OpSignBitSet", 160>;

def OpLessOrGreater: BinOp<"OpLessOrGreater", 161>;
def OpOrdered: BinOp<"OpOrdered", 162>;
def OpUnordered: BinOp<"OpUnordered", 163>;

def OpLogicalEqual: BinOp<"OpLogicalEqual", 164>;
def OpLogicalNotEqual: BinOp<"OpLogicalNotEqual", 165>;
def OpLogicalOr: BinOp<"OpLogicalOr", 166>;
def OpLogicalAnd: BinOp<"OpLogicalAnd", 167>;
def OpLogicalNot: UnOp<"OpLogicalNot", 168>;

defm OpSelect: TernOpTypedGen<"OpSelect", 169, select, 1, 1, 1, 1>;

def OpIEqual: BinOp<"OpIEqual", 170>;
def OpINotEqual: BinOp<"OpINotEqual", 171>;

def OpUGreaterThan: BinOp<"OpUGreaterThan", 172>;
def OpSGreaterThan: BinOp<"OpSGreaterThan", 173>;
def OpUGreaterThanEqual: BinOp<"OpUGreaterThanEqual", 174>;
def OpSGreaterThanEqual: BinOp<"OpSGreaterThanEqual", 175>;
def OpULessThan: BinOp<"OpULessThan", 176>;
def OpSLessThan: BinOp<"OpSLessThan", 177>;
def OpULessThanEqual: BinOp<"OpULessThanEqual", 178>;
def OpSLessThanEqual: BinOp<"OpSLessThanEqual", 179>;

def OpFOrdEqual: BinOp<"OpFOrdEqual", 180>;
def OpFUnordEqual: BinOp<"OpFUnordEqual", 181>;
def OpFOrdNotEqual: BinOp<"OpFOrdNotEqual", 182>;
def OpFUnordNotEqual: BinOp<"OpFUnordNotEqual", 183>;

def OpFOrdLessThan: BinOp<"OpFOrdLessThan", 184>;
def OpFUnordLessThan: BinOp<"OpFUnordLessThan", 185>;
def OpFOrdGreaterThan: BinOp<"OpFOrdGreaterThan", 186>;
def OpFUnordGreaterThan: BinOp<"OpFUnordGreaterThan", 187>;

def OpFOrdLessThanEqual: BinOp<"OpFOrdLessThanEqual", 188>;
def OpFUnordLessThanEqual: BinOp<"OpFUnordLessThanEqual", 189>;
def OpFOrdGreaterThanEqual: BinOp<"OpFOrdGreaterThanEqual", 190>;
def OpFUnordGreaterThanEqual: BinOp<"OpFUnordGreaterThanEqual", 191>;

// 3.42.16 Derivative Instructions

def OpDPdx: UnOp<"OpDPdx", 207>;
def OpDPdy: UnOp<"OpDPdy", 208>;
def OpFwidth: UnOp<"OpFwidth", 209>;

def OpDPdxFine: UnOp<"OpDPdxFine", 210>;
def OpDPdyFine: UnOp<"OpDPdyFine", 211>;
def OpFwidthFine: UnOp<"OpFwidthFine", 212>;

def OpDPdxCoarse: UnOp<"OpDPdxCoarse", 213>;
def OpDPdyCoarse: UnOp<"OpDPdyCoarse", 214>;
def OpFwidthCoarse: UnOp<"OpFwidthCoarse", 215>;

// 3.42.17 Control-Flow Instructions

def OpPhi: Op<245, (outs ID:$res), (ins TYPE:$type, ID:$var0, ID:$block0, variable_ops),
                  "$res = OpPhi $type $var0 $block0">;
def OpLoopMerge: Op<246, (outs), (ins ID:$merge, ID:$continue, LoopControl:$lc, variable_ops),
                  "OpLoopMerge $merge $continue $lc">;
def OpSelectionMerge: Op<247, (outs), (ins ID:$merge, SelectionControl:$sc),
                  "OpSelectionMerge $merge $sc">;
def OpLabel: Op<248, (outs ID:$label), (ins), "$label = OpLabel">;
let isTerminator=1 in {
  def OpBranch: Op<249, (outs), (ins ID:$label), "OpBranch $label">;
  def OpBranchConditional: Op<250, (outs), (ins ID:$cond, ID:$true, ID:$false, variable_ops),
                  "OpBranchConditional $cond $true $false">;
  def OpSwitch: Op<251, (outs), (ins ID:$sel, ID:$dflt, variable_ops), "OpSwitch $sel $dflt">;
}
let isReturn = 1, hasDelaySlot=0, isBarrier = 0, isTerminator=1, isNotDuplicable = 1 in {
  def OpKill: SimpleOp<"OpKill", 252>;
  def OpReturn: SimpleOp<"OpReturn", 253>;
  def OpReturnValue: Op<254, (outs), (ins ANYID:$ret), "OpReturnValue $ret">;
  def OpUnreachable: SimpleOp<"OpUnreachable", 255>;
}
def OpLifetimeStart: Op<256, (outs), (ins ID:$ptr, i32imm:$sz), "OpLifetimeStart $ptr, $sz">;
def OpLifetimeStop: Op<257, (outs), (ins ID:$ptr, i32imm:$sz), "OpLifetimeStop $ptr, $sz">;

// 3.42.18 Atomic Instructions

class AtomicOp<string name, bits<16> opCode>: Op<opCode, (outs ID:$res),
                  (ins TYPE:$ty, ID:$ptr, ID:$sc, ID:$sem),
                  "$res = "#name#" $ty $ptr $sc $sem">;

class AtomicOpVal<string name, bits<16> opCode>: Op<opCode, (outs ID:$res),
                  (ins TYPE:$ty, ID:$ptr, ID:$sc, ID:$sem, ID:$val),
                  "$res = "#name#" $ty $ptr $sc $sem $val">;

def OpAtomicLoad: AtomicOp<"OpAtomicLoad", 227>;

def OpAtomicStore: Op<228, (outs), (ins ID:$ptr, ID:$sc, ID:$sem, ID:$val),
                  "OpAtomicStore $ptr $sc $sem $val">;
def OpAtomicExchange: Op<229, (outs ID:$res),
                  (ins TYPE:$ty, ID:$ptr, ID:$sc, ID:$sem, ID:$val),
                  "$res = OpAtomicExchange $ty $ptr $sc $sem $val">;
def OpAtomicCompareExchange: Op<230, (outs ID:$res),
                  (ins TYPE:$ty, ID:$ptr, ID:$sc, ID:$eq,
                   ID:$neq, ID:$val, ID:$cmp),
                  "$res = OpAtomicCompareExchange $ty $ptr $sc $eq $neq $val $cmp">;
def OpAtomicCompareExchangeWeak: Op<231, (outs ID:$res),
                   (ins TYPE:$ty, ID:$ptr, ID:$sc, ID:$eq,
                    ID:$neq, ID:$val, ID:$cmp),
                   "$res = OpAtomicCompareExchangeWeak $ty $ptr $sc $eq $neq $val $cmp">;

def OpAtomicIIncrement: AtomicOp<"OpAtomicIIncrement", 232>;
def OpAtomicIDecrement: AtomicOp<"OpAtomicIDecrement", 233>;

def OpAtomicIAdd: AtomicOpVal<"OpAtomicIAdd", 234>;
def OpAtomicISub: AtomicOpVal<"OpAtomicISub", 235>;

def OpAtomicSMin: AtomicOpVal<"OpAtomicSMin", 236>;
def OpAtomicUMin: AtomicOpVal<"OpAtomicUMin", 237>;
def OpAtomicSMax: AtomicOpVal<"OpAtomicSMax", 238>;
def OpAtomicUMax: AtomicOpVal<"OpAtomicUMax", 239>;

def OpAtomicAnd: AtomicOpVal<"OpAtomicAnd", 240>;
def OpAtomicOr: AtomicOpVal<"OpAtomicOr", 241>;
def OpAtomicXor: AtomicOpVal<"OpAtomicXor", 242>;

def OpAtomicFAddEXT: AtomicOpVal<"OpAtomicFAddEXT", 6035>;
def OpAtomicFMinEXT: AtomicOpVal<"OpAtomicFMinEXT", 5614>;
def OpAtomicFMaxEXT: AtomicOpVal<"OpAtomicFMaxEXT", 5615>;

def OpAtomicFlagTestAndSet: AtomicOp<"OpAtomicFlagTestAndSet", 318>;
def OpAtomicFlagClear: Op<319, (outs), (ins ID:$ptr, ID:$sc, ID:$sem),
                  "OpAtomicFlagClear $ptr $sc $sem">;

// 3.42.19 Primitive Instructions

def OpEmitVertex: SimpleOp<"OpEmitVertex", 218>;
def OpEndPrimitive: SimpleOp<"OpEndPrimitive", 219>;
def OpEmitStreamVertex: Op<220, (outs), (ins ID:$stream), "OpEmitStreamVertex $stream">;
def OpEndStreamPrimitive: Op<221, (outs), (ins ID:$stream), "OpEndStreamPrimitive $stream">;

// 3.42.20 Barrier Instructions

def OpControlBarrier: Op<224, (outs), (ins ID:$exec, ID:$mem, ID:$sem),
                  "OpControlBarrier $exec $mem $sem">;
def OpMemoryBarrier: Op<225, (outs), (ins ID:$mem, ID:$sem),
                  "OpMemoryBarrier $mem $sem">;
def OpNamedBarrierInitialize: UnOp<"OpNamedBarrierInitialize", 328>;
def OpMemoryNamedBarrier: Op<329, (outs), (ins ID:$barr, ID:$mem, ID:$sem),
                  "OpMemoryNamedBarrier $barr $mem $sem">;

// 3.42.21. Group and Subgroup Instructions

def OpGroupAsyncCopy: Op<259, (outs ID:$res), (ins TYPE:$ty, ID:$scope,
                  ID:$dst, ID:$src, ID:$nelts, ID:$stride, ID:$event),
                  "$res = OpGroupAsyncCopy $ty $scope $dst $src $nelts $stride $event">;
def OpGroupWaitEvents: Op<260, (outs), (ins ID:$scope, ID:$nelts, ID:$elist),
                  "OpGroupWaitEvents $scope $nelts $elist">;
def OpGroupAll: Op<261, (outs ID:$res), (ins TYPE:$ty, ID:$scope, ID:$pr),
                  "$res = OpGroupAll $ty $scope $pr">;
def OpGroupAny: Op<262, (outs ID:$res), (ins TYPE:$ty, ID:$scope, ID:$pr),
                  "$res = OpGroupAny $ty $scope $pr">;
def OpGroupBroadcast: Op<263, (outs ID:$res), (ins TYPE:$ty, ID:$scope,
                               ID:$val, ID:$id),
                  "$res = OpGroupBroadcast $ty $scope $val $id">;
class OpGroup<string name, bits<16> opCode>: Op<opCode, (outs ID:$res),
                  (ins TYPE:$ty, ID:$scope, GroupOperation:$groupOp, ID:$x),
                  "$res = OpGroup"#name#" $ty $scope $groupOp $x">;
def OpGroupIAdd: OpGroup<"IAdd", 264>;
def OpGroupFAdd: OpGroup<"FAdd", 265>;
def OpGroupFMin: OpGroup<"FMin", 266>;
def OpGroupUMin: OpGroup<"UMin", 267>;
def OpGroupSMin: OpGroup<"SMin", 268>;
def OpGroupFMax: OpGroup<"FMax", 269>;
def OpGroupUMax: OpGroup<"UMax", 270>;
def OpGroupSMax: OpGroup<"SMax", 271>;

// TODO: 3.42.22. Device-Side Enqueue Instructions
def OpEnqueueKernel: Op<292, (outs ID:$res), (ins TYPE:$type, ID:$queue, ID:$flags, ID:$NDR, ID:$nevents, ID:$wevents,
                                              ID:$revent, ID:$invoke, ID:$param, ID:$psize, ID:$palign, variable_ops),
                  "$res = OpEnqueueKernel $type $queue $flags $NDR $nevents $wevents $revent $invoke $param $psize $palign">;
def OpRetainEvent: Op<297, (outs), (ins ID:$event), "OpRetainEvent $event">;
def OpReleaseEvent: Op<298, (outs), (ins ID:$event), "OpReleaseEvent $event">;
def OpCreateUserEvent: Op<299, (outs ID:$res), (ins TYPE:$type),
                  "$res = OpCreateUserEvent $type">;
def OpIsValidEvent: Op<300, (outs ID:$res), (ins TYPE:$type, ID:$event),
                  "$res = OpIsValidEvent $type $event ">;
def OpSetUserEventStatus: Op<301, (outs), (ins ID:$event, ID:$status),
                  "OpSetUserEventStatus $event $status">;
def OpCaptureEventProfilingInfo: Op<302, (outs),
                  (ins ID:$event, ID:$info, ID:$value),
                  "OpCaptureEventProfilingInfo $event $info $value">;
def OpGetDefaultQueue: Op<303, (outs ID:$res), (ins TYPE:$type),
                  "$res = OpGetDefaultQueue $type">;
def OpBuildNDRange: Op<304, (outs ID:$res), (ins TYPE:$type, ID:$GWS, ID:$LWS, ID:$GWO),
                  "$res = OpBuildNDRange $type $GWS $LWS $GWO">;

// TODO: 3.42.23. Pipe Instructions

// 3.42.24. Non-Uniform Instructions

def OpGroupNonUniformElect: Op<333, (outs ID:$res), (ins TYPE:$ty, ID:$scope),
                  "$res = OpGroupNonUniformElect $ty $scope">;
class OpGroupNU3<string name, bits<16> opCode>: Op<opCode,
                  (outs ID:$res), (ins TYPE:$ty, ID:$scope, ID:$pred),
                  "$res = OpGroupNonUniform"#name#" $ty $scope $pred">;
class OpGroupNU4<string name, bits<16> opCode>: Op<opCode,
                  (outs ID:$res), (ins TYPE:$ty, ID:$scope, ID:$val, ID:$id),
                  "$res = OpGroupNonUniform"#name#" $ty $scope $val $id">;
def OpGroupNonUniformAll: OpGroupNU3<"All", 334>;
def OpGroupNonUniformAny: OpGroupNU3<"Any", 335>;
def OpGroupNonUniformAllEqual: OpGroupNU3<"AllEqual", 336>;
def OpGroupNonUniformBroadcast: OpGroupNU4<"Broadcast", 337>;
def OpGroupNonUniformBroadcastFirst: OpGroupNU3<"BroadcastFirst", 338>;
def OpGroupNonUniformBallot: OpGroupNU3<"Ballot", 339>;
def OpGroupNonUniformInverseBallot: OpGroupNU3<"InverseBallot", 340>;
def OpGroupNonUniformBallotBitExtract: OpGroupNU4<"BallotBitExtract", 341>;
def OpGroupNonUniformBallotBitCount: Op<342, (outs ID:$res),
                  (ins TYPE:$ty, ID:$scope, GroupOperation:$groupOp, ID:$val),
                  "$res = OpGroupNonUniformBallotBitCount "
                          "$ty $scope $groupOp $val">;
def OpGroupNonUniformBallotFindLSB: OpGroupNU3<"BallotFindLSB", 343>;
def OpGroupNonUniformBallotFindMSB: OpGroupNU3<"BallotFindMSB", 344>;
def OpGroupNonUniformShuffle: OpGroupNU4<"Shuffle", 345>;
def OpGroupNonUniformShuffleXor: OpGroupNU4<"ShuffleXor", 346>;
def OpGroupNonUniformShuffleUp: OpGroupNU4<"ShuffleUp", 347>;
def OpGroupNonUniformShuffleDown: OpGroupNU4<"ShuffleDown", 348>;
class OpGroupNUGroup<string name, bits<16> opCode>: Op<opCode, (outs ID:$res),
                  (ins TYPE:$ty, ID:$scope, GroupOperation:$groupOp,
                   ID:$val, variable_ops),
                  "$res = OpGroupNonUniform"#name#" $ty $scope $groupOp $val">;
def OpGroupNonUniformIAdd: OpGroupNUGroup<"IAdd", 349>;
def OpGroupNonUniformFAdd: OpGroupNUGroup<"FAdd", 350>;
def OpGroupNonUniformIMul: OpGroupNUGroup<"IMul", 351>;
def OpGroupNonUniformFMul: OpGroupNUGroup<"FMul", 352>;
def OpGroupNonUniformSMin: OpGroupNUGroup<"SMin", 353>;
def OpGroupNonUniformUMin: OpGroupNUGroup<"UMin", 354>;
def OpGroupNonUniformFMin: OpGroupNUGroup<"FMin", 355>;
def OpGroupNonUniformSMax: OpGroupNUGroup<"SMax", 356>;
def OpGroupNonUniformUMax: OpGroupNUGroup<"UMax", 357>;
def OpGroupNonUniformFMax: OpGroupNUGroup<"FMax", 358>;
def OpGroupNonUniformBitwiseAnd: OpGroupNUGroup<"BitwiseAnd", 359>;
def OpGroupNonUniformBitwiseOr: OpGroupNUGroup<"BitwiseOr", 360>;
def OpGroupNonUniformBitwiseXor: OpGroupNUGroup<"BitwiseXor", 361>;
def OpGroupNonUniformLogicalAnd: OpGroupNUGroup<"LogicalAnd", 362>;
def OpGroupNonUniformLogicalOr: OpGroupNUGroup<"LogicalOr", 363>;
def OpGroupNonUniformLogicalXor: OpGroupNUGroup<"LogicalXor", 364>;

// SPV_KHR_subgroup_rotate
def OpGroupNonUniformRotateKHR: Op<4431, (outs ID:$res),
                  (ins TYPE:$type, ID:$scope, ID:$value, ID:$delta, variable_ops),
                  "$res = OpGroupNonUniformRotateKHR $type $scope $value $delta">;

// SPV_KHR_shader_clock
def OpReadClockKHR: Op<5056, (outs ID:$res),
                  (ins TYPE:$type, ID:$scope),
                  "$res = OpReadClockKHR $type $scope">;

// 3.49.7, Constant-Creation Instructions

//  - SPV_INTEL_function_pointers
def OpConstantFunctionPointerINTEL: Op<5600, (outs ID:$res), (ins TYPE:$ty, ID:$fun), "$res = OpConstantFunctionPointerINTEL $ty $fun">;

// 3.49.9. Function Instructions

//  - SPV_INTEL_function_pointers
def OpFunctionPointerCallINTEL: Op<5601, (outs ID:$res), (ins TYPE:$ty, ID:$funPtr, variable_ops), "$res = OpFunctionPointerCallINTEL $ty $funPtr">;

// 3.49.21. Group and Subgroup Instructions

// - SPV_INTEL_subgroups
def OpSubgroupShuffleINTEL: Op<5571, (outs ID:$res), (ins TYPE:$type, ID:$data, ID:$invocationId),
                  "$res = OpSubgroupShuffleINTEL $type $data $invocationId">;
def OpSubgroupShuffleDownINTEL: Op<5572, (outs ID:$res), (ins TYPE:$type, ID:$current, ID:$next, ID:$delta),
                  "$res = OpSubgroupShuffleDownINTEL $type $current $next $delta">;
def OpSubgroupShuffleUpINTEL: Op<5573, (outs ID:$res), (ins TYPE:$type, ID:$previous, ID:$current, ID:$delta),
                  "$res = OpSubgroupShuffleUpINTEL $type $previous $current $delta">;
def OpSubgroupShuffleXorINTEL: Op<5574, (outs ID:$res), (ins TYPE:$type, ID:$data, ID:$value),
                  "$res = OpSubgroupShuffleXorINTEL $type $data $value">;
def OpSubgroupBlockReadINTEL: Op<5575, (outs ID:$res), (ins TYPE:$type, ID:$ptr),
                  "$res = OpSubgroupBlockReadINTEL $type $ptr">;
def OpSubgroupBlockWriteINTEL: Op<5576, (outs), (ins ID:$ptr, ID:$data),
                  "OpSubgroupBlockWriteINTEL $ptr $data">;
def OpSubgroupImageBlockReadINTEL: Op<5577, (outs ID:$res), (ins TYPE:$type, ID:$image, ID:$coordinate),
                  "$res = OpSubgroupImageBlockReadINTEL $type $image $coordinate">;
def OpSubgroupImageBlockWriteINTEL: Op<5578, (outs), (ins ID:$image, ID:$coordinate, ID:$data),
                  "OpSubgroupImageBlockWriteINTEL $image $coordinate $data">;

// - SPV_KHR_uniform_group_instructions
def OpGroupIMulKHR: Op<6401, (outs ID:$res), (ins TYPE:$type, ID:$scope, i32imm:$groupOp, ID:$value),
                  "$res = OpGroupIMulKHR $type $scope $groupOp $value">;
def OpGroupFMulKHR: Op<6402, (outs ID:$res), (ins TYPE:$type, ID:$scope, i32imm:$groupOp, ID:$value),
                  "$res = OpGroupFMulKHR $type $scope $groupOp $value">;
def OpGroupBitwiseAndKHR: Op<6403, (outs ID:$res), (ins TYPE:$type, ID:$scope, i32imm:$groupOp, ID:$value),
                  "$res = OpGroupBitwiseAndKHR $type $scope $groupOp $value">;
def OpGroupBitwiseOrKHR: Op<6404, (outs ID:$res), (ins TYPE:$type, ID:$scope, i32imm:$groupOp, ID:$value),
                  "$res = OpGroupBitwiseOrKHR $type $scope $groupOp $value">;
def OpGroupBitwiseXorKHR: Op<6405, (outs ID:$res), (ins TYPE:$type, ID:$scope, i32imm:$groupOp, ID:$value),
                  "$res = OpGroupBitwiseXorKHR $type $scope $groupOp $value">;
def OpGroupLogicalAndKHR: Op<6406, (outs ID:$res), (ins TYPE:$type, ID:$scope, i32imm:$groupOp, ID:$value),
                  "$res = OpGroupLogicalAndKHR $type $scope $groupOp $value">;
def OpGroupLogicalOrKHR: Op<6407, (outs ID:$res), (ins TYPE:$type, ID:$scope, i32imm:$groupOp, ID:$value),
                  "$res = OpGroupLogicalOrKHR $type $scope $groupOp $value">;
def OpGroupLogicalXorKHR: Op<6408, (outs ID:$res), (ins TYPE:$type, ID:$scope, i32imm:$groupOp, ID:$value),
                  "$res = OpGroupLogicalXorKHR $type $scope $groupOp $value">;

// Inline Assembly Instructions
def OpAsmTargetINTEL: Op<5609, (outs ID:$res), (ins StringImm:$str), "$res = OpAsmTargetINTEL $str">;
def OpAsmINTEL: Op<5610, (outs ID:$res), (ins TYPE:$type, TYPE:$asm_type, ID:$target,
                                          StringImm:$asm, StringImm:$constraints),
                  "$res = OpAsmINTEL $type $asm_type $target $asm">;
def OpAsmCallINTEL: Op<5611, (outs ID:$res), (ins TYPE:$type, ID:$asm, variable_ops),
                  "$res = OpAsmCallINTEL $type $asm">;

// SPV_KHR_cooperative_matrix
def OpCooperativeMatrixLoadKHR: Op<4457, (outs ID:$res),
                  (ins TYPE:$resType, ID:$pointer, ID:$memory_layout, variable_ops),
                  "$res = OpCooperativeMatrixLoadKHR $resType $pointer $memory_layout">;
def OpCooperativeMatrixStoreKHR: Op<4458, (outs),
                  (ins ID:$pointer, ID:$objectToStore, ID:$memory_layout, variable_ops),
                  "OpCooperativeMatrixStoreKHR $pointer $objectToStore $memory_layout">;
def OpCooperativeMatrixMulAddKHR: Op<4459, (outs ID:$res),
                  (ins TYPE:$type, ID:$A, ID:$B, ID:$C, variable_ops),
                  "$res = OpCooperativeMatrixMulAddKHR $type $A $B $C">;
def OpCooperativeMatrixLengthKHR: Op<4460, (outs ID:$res), (ins TYPE:$type, ID:$coop_matr_type),
                  "$res = OpCooperativeMatrixLengthKHR $type $coop_matr_type">;

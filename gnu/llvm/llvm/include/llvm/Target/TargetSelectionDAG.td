//===- TargetSelectionDAG.td - Common code for DAG isels ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the target-independent interfaces used by SelectionDAG
// instruction selection generators.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Selection DAG Type Constraint definitions.
//
// Note that the semantics of these constraints are hard coded into tblgen.  To
// modify or add constraints, you have to hack tblgen.
//

class SDTypeConstraint<int opnum> {
  int OperandNum = opnum;
}

// SDTCisVT - The specified operand has exactly this VT.
class SDTCisVT<int OpNum, ValueType vt> : SDTypeConstraint<OpNum> {
  ValueType VT = vt;
}

class SDTCisPtrTy<int OpNum> : SDTypeConstraint<OpNum>;

// SDTCisInt - The specified operand has integer type.
class SDTCisInt<int OpNum> : SDTypeConstraint<OpNum>;

// SDTCisFP - The specified operand has floating-point type.
class SDTCisFP<int OpNum> : SDTypeConstraint<OpNum>;

// SDTCisVec - The specified operand has a vector type.
class SDTCisVec<int OpNum> : SDTypeConstraint<OpNum>;

// SDTCisSameAs - The two specified operands have identical types.
class SDTCisSameAs<int OpNum, int OtherOp> : SDTypeConstraint<OpNum> {
  int OtherOperandNum = OtherOp;
}

// SDTCisVTSmallerThanOp - The specified operand is a VT SDNode, and its type is
// smaller than the 'Other' operand.
class SDTCisVTSmallerThanOp<int OpNum, int OtherOp> : SDTypeConstraint<OpNum> {
  int OtherOperandNum = OtherOp;
}

class SDTCisOpSmallerThanOp<int SmallOp, int BigOp> : SDTypeConstraint<SmallOp>{
  int BigOperandNum = BigOp;
}

/// SDTCisEltOfVec - This indicates that ThisOp is a scalar type of the same
/// type as the element type of OtherOp, which is a vector type.
class SDTCisEltOfVec<int ThisOp, int OtherOp>
  : SDTypeConstraint<ThisOp> {
  int OtherOpNum = OtherOp;
}

/// SDTCisSubVecOfVec - This indicates that ThisOp is a vector type
/// with length less that of OtherOp, which is a vector type.
class SDTCisSubVecOfVec<int ThisOp, int OtherOp>
  : SDTypeConstraint<ThisOp> {
  int OtherOpNum = OtherOp;
}

// SDTCVecEltisVT - The specified operand is vector type with element type
// of VT.
class SDTCVecEltisVT<int OpNum, ValueType vt> : SDTypeConstraint<OpNum> {
  ValueType VT = vt;
}

// SDTCisSameNumEltsAs - The two specified operands have identical number
// of elements.
class SDTCisSameNumEltsAs<int OpNum, int OtherOp> : SDTypeConstraint<OpNum> {
  int OtherOperandNum = OtherOp;
}

// SDTCisSameSizeAs - The two specified operands have identical size.
class SDTCisSameSizeAs<int OpNum, int OtherOp> : SDTypeConstraint<OpNum> {
  int OtherOperandNum = OtherOp;
}

//===----------------------------------------------------------------------===//
// Selection DAG Type Profile definitions.
//
// These use the constraints defined above to describe the type requirements of
// the various nodes.  These are not hard coded into tblgen, allowing targets to
// add their own if needed.
//

// SDTypeProfile - This profile describes the type requirements of a Selection
// DAG node.
class SDTypeProfile<int numresults, int numoperands,
                    list<SDTypeConstraint> constraints> {
  int NumResults = numresults;
  int NumOperands = numoperands;
  list<SDTypeConstraint> Constraints = constraints;
}

// Builtin profiles.
def SDTIntLeaf: SDTypeProfile<1, 0, [SDTCisInt<0>]>;         // for 'imm'.
def SDTFPLeaf : SDTypeProfile<1, 0, [SDTCisFP<0>]>;          // for 'fpimm'.
def SDTPtrLeaf: SDTypeProfile<1, 0, [SDTCisPtrTy<0>]>;       // for '&g'.
def SDTOther  : SDTypeProfile<1, 0, [SDTCisVT<0, OtherVT>]>; // for 'vt'.
def SDTUNDEF  : SDTypeProfile<1, 0, []>;                     // for 'undef'.
def SDTUnaryOp  : SDTypeProfile<1, 1, []>;                   // for bitconvert.

def SDTPtrAddOp : SDTypeProfile<1, 2, [     // ptradd
  SDTCisSameAs<0, 1>, SDTCisInt<2>, SDTCisPtrTy<1>
]>;
def SDTIntBinOp : SDTypeProfile<1, 2, [     // add, and, or, xor, udiv, etc.
  SDTCisSameAs<0, 1>, SDTCisSameAs<0, 2>, SDTCisInt<0>
]>;
def SDTIntShiftOp : SDTypeProfile<1, 2, [   // shl, sra, srl
  SDTCisSameAs<0, 1>, SDTCisInt<0>, SDTCisInt<2>
]>;
def SDTIntShiftDOp: SDTypeProfile<1, 3, [   // fshl, fshr
  SDTCisSameAs<0, 1>, SDTCisSameAs<0, 2>, SDTCisInt<0>, SDTCisInt<3>
]>;
def SDTIntSatNoShOp : SDTypeProfile<1, 2, [   // ssat with no shift
  SDTCisSameAs<0, 1>, SDTCisInt<2>
]>;
def SDTIntBinHiLoOp : SDTypeProfile<2, 2, [ // mulhi, mullo, sdivrem, udivrem
  SDTCisSameAs<0, 1>, SDTCisSameAs<0, 2>, SDTCisSameAs<0, 3>,SDTCisInt<0>
]>;
def SDTIntScaledBinOp : SDTypeProfile<1, 3, [  // smulfix, sdivfix, etc
  SDTCisSameAs<0, 1>, SDTCisSameAs<0, 2>, SDTCisInt<0>, SDTCisInt<3>
]>;

def SDTFPBinOp : SDTypeProfile<1, 2, [      // fadd, fmul, etc.
  SDTCisSameAs<0, 1>, SDTCisSameAs<0, 2>, SDTCisFP<0>
]>;
def SDTFPSignOp : SDTypeProfile<1, 2, [     // fcopysign.
  SDTCisSameAs<0, 1>, SDTCisFP<0>, SDTCisFP<2>
]>;
def SDTFPTernaryOp : SDTypeProfile<1, 3, [  // fmadd, fnmsub, etc.
  SDTCisSameAs<0, 1>, SDTCisSameAs<0, 2>, SDTCisSameAs<0, 3>, SDTCisFP<0>
]>;
def SDTIntUnaryOp : SDTypeProfile<1, 1, [ // bitreverse
  SDTCisSameAs<0, 1>, SDTCisInt<0>
]>;
def SDTIntBitCountUnaryOp : SDTypeProfile<1, 1, [   // ctlz, cttz
  SDTCisInt<0>, SDTCisInt<1>
]>;
def SDTIntExtendOp : SDTypeProfile<1, 1, [  // sext, zext, anyext
  SDTCisInt<0>, SDTCisInt<1>, SDTCisOpSmallerThanOp<1, 0>, SDTCisSameNumEltsAs<0, 1>
]>;
def SDTIntTruncOp  : SDTypeProfile<1, 1, [  // trunc
  SDTCisInt<0>, SDTCisInt<1>, SDTCisOpSmallerThanOp<0, 1>, SDTCisSameNumEltsAs<0, 1>
]>;
def SDTFPUnaryOp  : SDTypeProfile<1, 1, [   // fneg, fsqrt, etc
  SDTCisSameAs<0, 1>, SDTCisFP<0>
]>;
def SDTFPRoundOp  : SDTypeProfile<1, 1, [   // fpround
  SDTCisFP<0>, SDTCisFP<1>, SDTCisOpSmallerThanOp<0, 1>, SDTCisSameNumEltsAs<0, 1>
]>;
def SDTFPExtendOp  : SDTypeProfile<1, 1, [  // fpextend
  SDTCisFP<0>, SDTCisFP<1>, SDTCisOpSmallerThanOp<1, 0>, SDTCisSameNumEltsAs<0, 1>
]>;
def SDIsFPClassOp : SDTypeProfile<1, 2, [   // is_fpclass
  SDTCisInt<0>, SDTCisFP<1>, SDTCisInt<2>, SDTCisSameNumEltsAs<0, 1>
]>;
def SDTIntToFPOp : SDTypeProfile<1, 1, [    // [su]int_to_fp
  SDTCisFP<0>, SDTCisInt<1>, SDTCisSameNumEltsAs<0, 1>
]>;
def SDTFPToIntOp : SDTypeProfile<1, 1, [    // fp_to_[su]int
  SDTCisInt<0>, SDTCisFP<1>, SDTCisSameNumEltsAs<0, 1>
]>;
def SDTFPToIntSatOp : SDTypeProfile<1, 2, [    // fp_to_[su]int_sat
  SDTCisInt<0>, SDTCisFP<1>, SDTCisSameNumEltsAs<0, 1>, SDTCisVT<2, OtherVT>
]>;
def SDTFPExpOp : SDTypeProfile<1, 2, [      // ldexp
  SDTCisSameAs<0, 1>, SDTCisFP<0>, SDTCisInt<2>
]>;
def SDTGetFPStateOp : SDTypeProfile<1, 0, [ // get_fpenv, get_fpmode
  SDTCisInt<0>
]>;
def SDTSetFPStateOp : SDTypeProfile<0, 1, [ // set_fpenv, set_fpmode
  SDTCisInt<0>
]>;
def SDTExtInreg : SDTypeProfile<1, 2, [     // sext_inreg
  SDTCisSameAs<0, 1>, SDTCisInt<0>, SDTCisVT<2, OtherVT>,
  SDTCisVTSmallerThanOp<2, 1>
]>;
def SDTExtInvec : SDTypeProfile<1, 1, [     // sext_invec
  SDTCisInt<0>, SDTCisVec<0>, SDTCisInt<1>, SDTCisVec<1>,
  SDTCisOpSmallerThanOp<1, 0>
]>;
def SDTFreeze : SDTypeProfile<1, 1, [
  SDTCisSameAs<0, 1>
]>;

def SDTSetCC : SDTypeProfile<1, 3, [        // setcc
  SDTCisInt<0>, SDTCisSameAs<1, 2>, SDTCisVT<3, OtherVT>
]>;

def SDTSelect : SDTypeProfile<1, 3, [       // select
  SDTCisInt<1>, SDTCisSameAs<0, 2>, SDTCisSameAs<2, 3>
]>;

def SDTVSelect : SDTypeProfile<1, 3, [       // vselect
  SDTCisVec<0>, SDTCisInt<1>, SDTCisSameAs<0, 2>, SDTCisSameAs<2, 3>, SDTCisSameNumEltsAs<0, 1>
]>;

def SDTSelectCC : SDTypeProfile<1, 5, [     // select_cc
  SDTCisSameAs<1, 2>, SDTCisSameAs<3, 4>, SDTCisSameAs<0, 3>,
  SDTCisVT<5, OtherVT>
]>;

def SDTBr : SDTypeProfile<0, 1, [           // br
  SDTCisVT<0, OtherVT>
]>;

def SDTBrCC : SDTypeProfile<0, 4, [       // brcc
  SDTCisVT<0, OtherVT>, SDTCisSameAs<1, 2>, SDTCisVT<3, OtherVT>
]>;

def SDTBrcond : SDTypeProfile<0, 2, [       // brcond
  SDTCisInt<0>, SDTCisVT<1, OtherVT>
]>;

def SDTBrind : SDTypeProfile<0, 1, [        // brind
  SDTCisPtrTy<0>
]>;

def SDTCatchret : SDTypeProfile<0, 2, [     // catchret
  SDTCisVT<0, OtherVT>, SDTCisVT<1, OtherVT>
]>;

def SDTCleanupret : SDTypeProfile<0, 1, [   // cleanupret
  SDTCisVT<0, OtherVT>
]>;

def SDTNone : SDTypeProfile<0, 0, []>;      // ret, trap

def SDTUBSANTrap : SDTypeProfile<0, 1, []>;      // ubsantrap

def SDTLoad : SDTypeProfile<1, 1, [         // load
  SDTCisPtrTy<1>
]>;

def SDTStore : SDTypeProfile<0, 2, [        // store
  SDTCisPtrTy<1>
]>;

def SDTIStore : SDTypeProfile<1, 3, [       // indexed store
  SDTCisSameAs<0, 2>, SDTCisPtrTy<0>, SDTCisPtrTy<3>
]>;

def SDTMaskedStore: SDTypeProfile<0, 4, [       // masked store
  SDTCisVec<0>, SDTCisPtrTy<1>, SDTCisPtrTy<2>, SDTCisVec<3>, SDTCisSameNumEltsAs<0, 3>
]>;

def SDTMaskedLoad: SDTypeProfile<1, 4, [       // masked load
  SDTCisVec<0>, SDTCisPtrTy<1>, SDTCisPtrTy<2>, SDTCisVec<3>, SDTCisSameAs<0, 4>,
  SDTCisSameNumEltsAs<0, 3>
]>;

def SDTMaskedGather : SDTypeProfile<1, 4, [
  SDTCisVec<0>, SDTCisSameAs<0, 1>, SDTCisVec<2>, SDTCisPtrTy<3>, SDTCisVec<4>,
  SDTCisSameNumEltsAs<0, 2>, SDTCisSameNumEltsAs<0, 4>
]>;

def SDTMaskedScatter : SDTypeProfile<0, 4, [
  SDTCisVec<0>, SDTCisVec<1>, SDTCisPtrTy<2>, SDTCisVec<3>,
  SDTCisSameNumEltsAs<0, 1>, SDTCisSameNumEltsAs<0, 3>
]>;

def SDTVectorCompress : SDTypeProfile<1, 3, [
  SDTCisVec<0>, SDTCisSameAs<0, 1>,
  SDTCisVec<2>, SDTCisSameNumEltsAs<1, 2>,
  SDTCisSameAs<1, 3>
]>;

def SDTVecShuffle : SDTypeProfile<1, 2, [
  SDTCisSameAs<0, 1>, SDTCisSameAs<1, 2>
]>;
def SDTVecSlice : SDTypeProfile<1, 3, [     // vector splice
  SDTCisSameAs<0, 1>, SDTCisSameAs<1, 2>, SDTCisInt<3>
]>;
def SDTVecExtract : SDTypeProfile<1, 2, [   // vector extract
  SDTCisEltOfVec<0, 1>, SDTCisPtrTy<2>
]>;
def SDTVecInsert : SDTypeProfile<1, 3, [    // vector insert
  SDTCisEltOfVec<2, 1>, SDTCisSameAs<0, 1>, SDTCisPtrTy<3>
]>;
def SDTVecReduce : SDTypeProfile<1, 1, [    // vector reduction
  SDTCisInt<0>, SDTCisVec<1>
]>;
def SDTFPVecReduce : SDTypeProfile<1, 1, [  // FP vector reduction
  SDTCisFP<0>, SDTCisVec<1>
]>;

def SDTVecReverse : SDTypeProfile<1, 1, [  // vector reverse
  SDTCisVec<0>, SDTCisSameAs<0,1>
]>;

def SDTSubVecExtract : SDTypeProfile<1, 2, [// subvector extract
  SDTCisSubVecOfVec<0,1>, SDTCisInt<2>
]>;
def SDTSubVecInsert : SDTypeProfile<1, 3, [ // subvector insert
  SDTCisSubVecOfVec<2, 1>, SDTCisSameAs<0,1>, SDTCisInt<3>
]>;

def SDTPrefetch : SDTypeProfile<0, 4, [     // prefetch
  SDTCisPtrTy<0>, SDTCisSameAs<1, 2>, SDTCisSameAs<1, 3>, SDTCisInt<1>
]>;

def SDTAtomicFence : SDTypeProfile<0, 2, [
  SDTCisSameAs<0,1>, SDTCisPtrTy<0>
]>;
def SDTAtomic3 : SDTypeProfile<1, 3, [
  SDTCisSameAs<0,2>,  SDTCisSameAs<0,3>, SDTCisInt<0>, SDTCisPtrTy<1>
]>;
def SDTAtomic2 : SDTypeProfile<1, 2, [
  SDTCisSameAs<0,2>, SDTCisInt<0>, SDTCisPtrTy<1>
]>;

def SDTFPAtomic2 : SDTypeProfile<1, 2, [
  SDTCisSameAs<0,2>, SDTCisFP<0>, SDTCisPtrTy<1>
]>;

def SDTAtomicStore : SDTypeProfile<0, 2, [
  SDTCisInt<0>, SDTCisPtrTy<1>
]>;
def SDTAtomicLoad : SDTypeProfile<1, 1, [
  SDTCisPtrTy<1>
]>;

class SDCallSeqStart<list<SDTypeConstraint> constraints> :
        SDTypeProfile<0, 2, constraints>;
class SDCallSeqEnd<list<SDTypeConstraint> constraints> :
        SDTypeProfile<0, 2, constraints>;

//===----------------------------------------------------------------------===//
// Selection DAG Node definitions.
//
class SDNode<string opcode, SDTypeProfile typeprof,
             list<SDNodeProperty> props = [], string sdclass = "SDNode">
             : SDPatternOperator {
  string Opcode  = opcode;
  string SDClass = sdclass;
  let Properties = props;
  SDTypeProfile TypeProfile = typeprof;
}

// Special TableGen-recognized dag nodes
def set;
def implicit;
def node;
def srcvalue;

def imm        : SDNode<"ISD::Constant"  , SDTIntLeaf , [], "ConstantSDNode">;
def timm       : SDNode<"ISD::TargetConstant",SDTIntLeaf, [], "ConstantSDNode">;
def fpimm      : SDNode<"ISD::ConstantFP", SDTFPLeaf  , [], "ConstantFPSDNode">;
def vt         : SDNode<"ISD::VALUETYPE" , SDTOther   , [], "VTSDNode">;
def bb         : SDNode<"ISD::BasicBlock", SDTOther   , [], "BasicBlockSDNode">;
def cond       : SDNode<"ISD::CONDCODE"  , SDTOther   , [], "CondCodeSDNode">;
def undef      : SDNode<"ISD::UNDEF"     , SDTUNDEF   , []>;
def vscale     : SDNode<"ISD::VSCALE"    , SDTIntUnaryOp, []>;
def globaladdr : SDNode<"ISD::GlobalAddress",         SDTPtrLeaf, [],
                        "GlobalAddressSDNode">;
def tglobaladdr : SDNode<"ISD::TargetGlobalAddress",  SDTPtrLeaf, [],
                         "GlobalAddressSDNode">;
def globaltlsaddr : SDNode<"ISD::GlobalTLSAddress",         SDTPtrLeaf, [],
                          "GlobalAddressSDNode">;
def tglobaltlsaddr : SDNode<"ISD::TargetGlobalTLSAddress",  SDTPtrLeaf, [],
                           "GlobalAddressSDNode">;
def constpool   : SDNode<"ISD::ConstantPool",         SDTPtrLeaf, [],
                         "ConstantPoolSDNode">;
def tconstpool  : SDNode<"ISD::TargetConstantPool",   SDTPtrLeaf, [],
                         "ConstantPoolSDNode">;
def jumptable   : SDNode<"ISD::JumpTable",            SDTPtrLeaf, [],
                         "JumpTableSDNode">;
def tjumptable  : SDNode<"ISD::TargetJumpTable",      SDTPtrLeaf, [],
                         "JumpTableSDNode">;
def frameindex  : SDNode<"ISD::FrameIndex",           SDTPtrLeaf, [],
                         "FrameIndexSDNode">;
def tframeindex : SDNode<"ISD::TargetFrameIndex",     SDTPtrLeaf, [],
                         "FrameIndexSDNode">;
def externalsym : SDNode<"ISD::ExternalSymbol",       SDTPtrLeaf, [],
                         "ExternalSymbolSDNode">;
def texternalsym: SDNode<"ISD::TargetExternalSymbol", SDTPtrLeaf, [],
                         "ExternalSymbolSDNode">;
def mcsym: SDNode<"ISD::MCSymbol", SDTPtrLeaf, [], "MCSymbolSDNode">;
def blockaddress : SDNode<"ISD::BlockAddress",        SDTPtrLeaf, [],
                         "BlockAddressSDNode">;
def tblockaddress: SDNode<"ISD::TargetBlockAddress",  SDTPtrLeaf, [],
                         "BlockAddressSDNode">;

def add        : SDNode<"ISD::ADD"       , SDTIntBinOp   ,
                        [SDNPCommutative, SDNPAssociative]>;
def ptradd     : SDNode<"ISD::ADD"       , SDTPtrAddOp, []>;
def sub        : SDNode<"ISD::SUB"       , SDTIntBinOp>;
def mul        : SDNode<"ISD::MUL"       , SDTIntBinOp,
                        [SDNPCommutative, SDNPAssociative]>;
def mulhs      : SDNode<"ISD::MULHS"     , SDTIntBinOp, [SDNPCommutative]>;
def mulhu      : SDNode<"ISD::MULHU"     , SDTIntBinOp, [SDNPCommutative]>;
def avgfloors  : SDNode<"ISD::AVGFLOORS" , SDTIntBinOp, [SDNPCommutative]>;
def avgflooru  : SDNode<"ISD::AVGFLOORU" , SDTIntBinOp, [SDNPCommutative]>;
def avgceils   : SDNode<"ISD::AVGCEILS"  , SDTIntBinOp, [SDNPCommutative]>;
def avgceilu   : SDNode<"ISD::AVGCEILU"  , SDTIntBinOp, [SDNPCommutative]>;
def abds       : SDNode<"ISD::ABDS"      , SDTIntBinOp, [SDNPCommutative]>;
def abdu       : SDNode<"ISD::ABDU"      , SDTIntBinOp, [SDNPCommutative]>;
def smullohi   : SDNode<"ISD::SMUL_LOHI" , SDTIntBinHiLoOp, [SDNPCommutative]>;
def umullohi   : SDNode<"ISD::UMUL_LOHI" , SDTIntBinHiLoOp, [SDNPCommutative]>;
def sdiv       : SDNode<"ISD::SDIV"      , SDTIntBinOp>;
def udiv       : SDNode<"ISD::UDIV"      , SDTIntBinOp>;
def srem       : SDNode<"ISD::SREM"      , SDTIntBinOp>;
def urem       : SDNode<"ISD::UREM"      , SDTIntBinOp>;
def sdivrem    : SDNode<"ISD::SDIVREM"   , SDTIntBinHiLoOp>;
def udivrem    : SDNode<"ISD::UDIVREM"   , SDTIntBinHiLoOp>;
def srl        : SDNode<"ISD::SRL"       , SDTIntShiftOp>;
def sra        : SDNode<"ISD::SRA"       , SDTIntShiftOp>;
def shl        : SDNode<"ISD::SHL"       , SDTIntShiftOp>;
def rotl       : SDNode<"ISD::ROTL"      , SDTIntShiftOp>;
def rotr       : SDNode<"ISD::ROTR"      , SDTIntShiftOp>;
def fshl       : SDNode<"ISD::FSHL"      , SDTIntShiftDOp>;
def fshr       : SDNode<"ISD::FSHR"      , SDTIntShiftDOp>;
def and        : SDNode<"ISD::AND"       , SDTIntBinOp,
                        [SDNPCommutative, SDNPAssociative]>;
def or         : SDNode<"ISD::OR"        , SDTIntBinOp,
                        [SDNPCommutative, SDNPAssociative]>;
def xor        : SDNode<"ISD::XOR"       , SDTIntBinOp,
                        [SDNPCommutative, SDNPAssociative]>;
def addc       : SDNode<"ISD::ADDC"      , SDTIntBinOp,
                        [SDNPCommutative, SDNPOutGlue]>;
def adde       : SDNode<"ISD::ADDE"      , SDTIntBinOp,
                        [SDNPCommutative, SDNPOutGlue, SDNPInGlue]>;
def subc       : SDNode<"ISD::SUBC"      , SDTIntBinOp,
                        [SDNPOutGlue]>;
def sube       : SDNode<"ISD::SUBE"      , SDTIntBinOp,
                        [SDNPOutGlue, SDNPInGlue]>;
def smin       : SDNode<"ISD::SMIN"      , SDTIntBinOp,
                                  [SDNPCommutative, SDNPAssociative]>;
def smax       : SDNode<"ISD::SMAX"      , SDTIntBinOp,
                                  [SDNPCommutative, SDNPAssociative]>;
def umin       : SDNode<"ISD::UMIN"      , SDTIntBinOp,
                                  [SDNPCommutative, SDNPAssociative]>;
def umax       : SDNode<"ISD::UMAX"      , SDTIntBinOp,
                                  [SDNPCommutative, SDNPAssociative]>;

def scmp       : SDNode<"ISD::SCMP"      , SDTIntBinOp,
                                  []>;
def ucmp       : SDNode<"ISD::UCMP"      , SDTIntBinOp,
                                  []>;

def saddsat    : SDNode<"ISD::SADDSAT"   , SDTIntBinOp, [SDNPCommutative]>;
def uaddsat    : SDNode<"ISD::UADDSAT"   , SDTIntBinOp, [SDNPCommutative]>;
def ssubsat    : SDNode<"ISD::SSUBSAT"   , SDTIntBinOp>;
def usubsat    : SDNode<"ISD::USUBSAT"   , SDTIntBinOp>;
def sshlsat    : SDNode<"ISD::SSHLSAT"   , SDTIntBinOp>;
def ushlsat    : SDNode<"ISD::USHLSAT"   , SDTIntBinOp>;

def smulfix    : SDNode<"ISD::SMULFIX"   , SDTIntScaledBinOp, [SDNPCommutative]>;
def smulfixsat : SDNode<"ISD::SMULFIXSAT", SDTIntScaledBinOp, [SDNPCommutative]>;
def umulfix    : SDNode<"ISD::UMULFIX"   , SDTIntScaledBinOp, [SDNPCommutative]>;
def umulfixsat : SDNode<"ISD::UMULFIXSAT", SDTIntScaledBinOp, [SDNPCommutative]>;
def sdivfix    : SDNode<"ISD::SDIVFIX"   , SDTIntScaledBinOp>;
def sdivfixsat : SDNode<"ISD::SDIVFIXSAT", SDTIntScaledBinOp>;
def udivfix    : SDNode<"ISD::UDIVFIX"   , SDTIntScaledBinOp>;
def udivfixsat : SDNode<"ISD::UDIVFIXSAT", SDTIntScaledBinOp>;

def sext_inreg : SDNode<"ISD::SIGN_EXTEND_INREG", SDTExtInreg>;
def sext_invec : SDNode<"ISD::SIGN_EXTEND_VECTOR_INREG", SDTExtInvec>;
def zext_invec : SDNode<"ISD::ZERO_EXTEND_VECTOR_INREG", SDTExtInvec>;

def abs        : SDNode<"ISD::ABS"        , SDTIntUnaryOp>;
def bitreverse : SDNode<"ISD::BITREVERSE" , SDTIntUnaryOp>;
def bswap      : SDNode<"ISD::BSWAP"      , SDTIntUnaryOp>;
def ctlz       : SDNode<"ISD::CTLZ"       , SDTIntBitCountUnaryOp>;
def cttz       : SDNode<"ISD::CTTZ"       , SDTIntBitCountUnaryOp>;
def ctpop      : SDNode<"ISD::CTPOP"      , SDTIntBitCountUnaryOp>;
def ctlz_zero_undef : SDNode<"ISD::CTLZ_ZERO_UNDEF", SDTIntBitCountUnaryOp>;
def cttz_zero_undef : SDNode<"ISD::CTTZ_ZERO_UNDEF", SDTIntBitCountUnaryOp>;
def sext       : SDNode<"ISD::SIGN_EXTEND", SDTIntExtendOp>;
def zext       : SDNode<"ISD::ZERO_EXTEND", SDTIntExtendOp>;
def anyext     : SDNode<"ISD::ANY_EXTEND" , SDTIntExtendOp>;
def trunc      : SDNode<"ISD::TRUNCATE"   , SDTIntTruncOp>;
def bitconvert : SDNode<"ISD::BITCAST"    , SDTUnaryOp>;
def addrspacecast : SDNode<"ISD::ADDRSPACECAST", SDTUnaryOp>;
def freeze     : SDNode<"ISD::FREEZE"     , SDTFreeze>;
def extractelt : SDNode<"ISD::EXTRACT_VECTOR_ELT", SDTVecExtract>;
def insertelt  : SDNode<"ISD::INSERT_VECTOR_ELT", SDTVecInsert>;

def vecreduce_add  : SDNode<"ISD::VECREDUCE_ADD", SDTVecReduce>;
def vecreduce_smax  : SDNode<"ISD::VECREDUCE_SMAX", SDTVecReduce>;
def vecreduce_umax  : SDNode<"ISD::VECREDUCE_UMAX", SDTVecReduce>;
def vecreduce_smin  : SDNode<"ISD::VECREDUCE_SMIN", SDTVecReduce>;
def vecreduce_umin  : SDNode<"ISD::VECREDUCE_UMIN", SDTVecReduce>;
def vecreduce_fadd  : SDNode<"ISD::VECREDUCE_FADD", SDTFPVecReduce>;
def vecreduce_fmin  : SDNode<"ISD::VECREDUCE_FMIN", SDTFPVecReduce>;
def vecreduce_fmax  : SDNode<"ISD::VECREDUCE_FMAX", SDTFPVecReduce>;
def vecreduce_fminimum : SDNode<"ISD::VECREDUCE_FMINIMUM", SDTFPVecReduce>;
def vecreduce_fmaximum : SDNode<"ISD::VECREDUCE_FMAXIMUM", SDTFPVecReduce>;

def fadd       : SDNode<"ISD::FADD"       , SDTFPBinOp, [SDNPCommutative]>;
def fsub       : SDNode<"ISD::FSUB"       , SDTFPBinOp>;
def fmul       : SDNode<"ISD::FMUL"       , SDTFPBinOp, [SDNPCommutative]>;
def fdiv       : SDNode<"ISD::FDIV"       , SDTFPBinOp>;
def frem       : SDNode<"ISD::FREM"       , SDTFPBinOp>;
def fma        : SDNode<"ISD::FMA"        , SDTFPTernaryOp, [SDNPCommutative]>;
def fmad       : SDNode<"ISD::FMAD"       , SDTFPTernaryOp, [SDNPCommutative]>;
def fabs       : SDNode<"ISD::FABS"       , SDTFPUnaryOp>;
def fminnum    : SDNode<"ISD::FMINNUM"    , SDTFPBinOp,
                                  [SDNPCommutative, SDNPAssociative]>;
def fmaxnum    : SDNode<"ISD::FMAXNUM"    , SDTFPBinOp,
                                  [SDNPCommutative, SDNPAssociative]>;
def fminnum_ieee : SDNode<"ISD::FMINNUM_IEEE", SDTFPBinOp,
                          [SDNPCommutative]>;
def fmaxnum_ieee  : SDNode<"ISD::FMAXNUM_IEEE", SDTFPBinOp,
                           [SDNPCommutative]>;
def fminimum   : SDNode<"ISD::FMINIMUM"   , SDTFPBinOp,
                        [SDNPCommutative, SDNPAssociative]>;
def fmaximum   : SDNode<"ISD::FMAXIMUM"   , SDTFPBinOp,
                        [SDNPCommutative, SDNPAssociative]>;
def fgetsign   : SDNode<"ISD::FGETSIGN"   , SDTFPToIntOp>;
def fcanonicalize : SDNode<"ISD::FCANONICALIZE", SDTFPUnaryOp>;
def fneg       : SDNode<"ISD::FNEG"       , SDTFPUnaryOp>;
def fsqrt      : SDNode<"ISD::FSQRT"      , SDTFPUnaryOp>;
def fsin       : SDNode<"ISD::FSIN"       , SDTFPUnaryOp>;
def fcos       : SDNode<"ISD::FCOS"       , SDTFPUnaryOp>;
def ftan       : SDNode<"ISD::FTAN"       , SDTFPUnaryOp>;
def fasin      : SDNode<"ISD::FASIN"      , SDTFPUnaryOp>;
def facos      : SDNode<"ISD::FACOS"      , SDTFPUnaryOp>;
def fatan      : SDNode<"ISD::FATAN"      , SDTFPUnaryOp>;
def fsinh      : SDNode<"ISD::FSINH"      , SDTFPUnaryOp>;
def fcosh      : SDNode<"ISD::FCOSH"      , SDTFPUnaryOp>;
def ftanh      : SDNode<"ISD::FTANH"      , SDTFPUnaryOp>;
def fexp2      : SDNode<"ISD::FEXP2"      , SDTFPUnaryOp>;
def fexp10     : SDNode<"ISD::FEXP10"     , SDTFPUnaryOp>;
def fpow       : SDNode<"ISD::FPOW"       , SDTFPBinOp>;
def flog2      : SDNode<"ISD::FLOG2"      , SDTFPUnaryOp>;
def fldexp     : SDNode<"ISD::FLDEXP"     , SDTFPExpOp>;
def frint      : SDNode<"ISD::FRINT"      , SDTFPUnaryOp>;
def ftrunc     : SDNode<"ISD::FTRUNC"     , SDTFPUnaryOp>;
def fceil      : SDNode<"ISD::FCEIL"      , SDTFPUnaryOp>;
def ffloor     : SDNode<"ISD::FFLOOR"     , SDTFPUnaryOp>;
def fnearbyint : SDNode<"ISD::FNEARBYINT" , SDTFPUnaryOp>;
def fround     : SDNode<"ISD::FROUND"     , SDTFPUnaryOp>;
def froundeven : SDNode<"ISD::FROUNDEVEN" , SDTFPUnaryOp>;

def lround     : SDNode<"ISD::LROUND"     , SDTFPToIntOp>;
def llround    : SDNode<"ISD::LLROUND"    , SDTFPToIntOp>;
def lrint      : SDNode<"ISD::LRINT"      , SDTFPToIntOp>;
def llrint     : SDNode<"ISD::LLRINT"     , SDTFPToIntOp>;

def fpround    : SDNode<"ISD::FP_ROUND"   , SDTFPRoundOp>;
def fpextend   : SDNode<"ISD::FP_EXTEND"  , SDTFPExtendOp>;
def fcopysign  : SDNode<"ISD::FCOPYSIGN"  , SDTFPSignOp>;

def is_fpclass : SDNode<"ISD::IS_FPCLASS" , SDIsFPClassOp>;

def sint_to_fp : SDNode<"ISD::SINT_TO_FP" , SDTIntToFPOp>;
def uint_to_fp : SDNode<"ISD::UINT_TO_FP" , SDTIntToFPOp>;
def fp_to_sint : SDNode<"ISD::FP_TO_SINT" , SDTFPToIntOp>;
def fp_to_uint : SDNode<"ISD::FP_TO_UINT" , SDTFPToIntOp>;
def fp_to_sint_sat : SDNode<"ISD::FP_TO_SINT_SAT" , SDTFPToIntSatOp>;
def fp_to_uint_sat : SDNode<"ISD::FP_TO_UINT_SAT" , SDTFPToIntSatOp>;
def f16_to_fp  : SDNode<"ISD::FP16_TO_FP" , SDTIntToFPOp>;
def fp_to_f16  : SDNode<"ISD::FP_TO_FP16" , SDTFPToIntOp>;
def bf16_to_fp  : SDNode<"ISD::BF16_TO_FP" , SDTIntToFPOp>;
def fp_to_bf16  : SDNode<"ISD::FP_TO_BF16" , SDTFPToIntOp>;

def strict_fadd       : SDNode<"ISD::STRICT_FADD",
                               SDTFPBinOp, [SDNPHasChain, SDNPCommutative]>;
def strict_fsub       : SDNode<"ISD::STRICT_FSUB",
                               SDTFPBinOp, [SDNPHasChain]>;
def strict_fmul       : SDNode<"ISD::STRICT_FMUL",
                               SDTFPBinOp, [SDNPHasChain, SDNPCommutative]>;
def strict_fdiv       : SDNode<"ISD::STRICT_FDIV",
                               SDTFPBinOp, [SDNPHasChain]>;
def strict_frem       : SDNode<"ISD::STRICT_FREM",
                               SDTFPBinOp, [SDNPHasChain]>;
def strict_fma        : SDNode<"ISD::STRICT_FMA",
                               SDTFPTernaryOp, [SDNPHasChain, SDNPCommutative]>;
def strict_fsqrt      : SDNode<"ISD::STRICT_FSQRT",
                               SDTFPUnaryOp, [SDNPHasChain]>;
def strict_fsin       : SDNode<"ISD::STRICT_FSIN",
                               SDTFPUnaryOp, [SDNPHasChain]>;
def strict_fcos       : SDNode<"ISD::STRICT_FCOS",
                               SDTFPUnaryOp, [SDNPHasChain]>;
def strict_ftan       : SDNode<"ISD::STRICT_FTAN",
                               SDTFPUnaryOp, [SDNPHasChain]>;
def strict_fasin      : SDNode<"ISD::STRICT_FASIN",
                               SDTFPUnaryOp, [SDNPHasChain]>;
def strict_facos      : SDNode<"ISD::STRICT_FACOS",
                               SDTFPUnaryOp, [SDNPHasChain]>;
def strict_fatan      : SDNode<"ISD::STRICT_FATAN",
                               SDTFPUnaryOp, [SDNPHasChain]>;
def strict_fsinh      : SDNode<"ISD::STRICT_FSINH",
                               SDTFPUnaryOp, [SDNPHasChain]>;
def strict_fcosh      : SDNode<"ISD::STRICT_FCOSH",
                               SDTFPUnaryOp, [SDNPHasChain]>;
def strict_ftanh      : SDNode<"ISD::STRICT_FTANH",
                               SDTFPUnaryOp, [SDNPHasChain]>;
def strict_fexp2      : SDNode<"ISD::STRICT_FEXP2",
                               SDTFPUnaryOp, [SDNPHasChain]>;
def strict_fpow       : SDNode<"ISD::STRICT_FPOW",
                               SDTFPBinOp, [SDNPHasChain]>;
def strict_fldexp     : SDNode<"ISD::STRICT_FLDEXP",
                               SDTFPExpOp, [SDNPHasChain]>;
def strict_flog2      : SDNode<"ISD::STRICT_FLOG2",
                               SDTFPUnaryOp, [SDNPHasChain]>;
def strict_frint      : SDNode<"ISD::STRICT_FRINT",
                               SDTFPUnaryOp, [SDNPHasChain]>;
def strict_lrint      : SDNode<"ISD::STRICT_LRINT",
                               SDTFPToIntOp, [SDNPHasChain]>;
def strict_llrint     : SDNode<"ISD::STRICT_LLRINT",
                               SDTFPToIntOp, [SDNPHasChain]>;
def strict_fnearbyint : SDNode<"ISD::STRICT_FNEARBYINT",
                               SDTFPUnaryOp, [SDNPHasChain]>;
def strict_fceil      : SDNode<"ISD::STRICT_FCEIL",
                               SDTFPUnaryOp, [SDNPHasChain]>;
def strict_ffloor     : SDNode<"ISD::STRICT_FFLOOR",
                               SDTFPUnaryOp, [SDNPHasChain]>;
def strict_lround     : SDNode<"ISD::STRICT_LROUND",
                               SDTFPToIntOp, [SDNPHasChain]>;
def strict_llround    : SDNode<"ISD::STRICT_LLROUND",
                               SDTFPToIntOp, [SDNPHasChain]>;
def strict_fround     : SDNode<"ISD::STRICT_FROUND",
                               SDTFPUnaryOp, [SDNPHasChain]>;
def strict_froundeven : SDNode<"ISD::STRICT_FROUNDEVEN",
                               SDTFPUnaryOp, [SDNPHasChain]>;
def strict_ftrunc     : SDNode<"ISD::STRICT_FTRUNC",
                               SDTFPUnaryOp, [SDNPHasChain]>;
def strict_fminnum    : SDNode<"ISD::STRICT_FMINNUM",
                               SDTFPBinOp, [SDNPHasChain,
                                            SDNPCommutative, SDNPAssociative]>;
def strict_fmaxnum    : SDNode<"ISD::STRICT_FMAXNUM",
                               SDTFPBinOp, [SDNPHasChain,
                                            SDNPCommutative, SDNPAssociative]>;
def strict_fminimum   : SDNode<"ISD::STRICT_FMINIMUM",
                               SDTFPBinOp, [SDNPHasChain,
                                            SDNPCommutative, SDNPAssociative]>;
def strict_fmaximum   : SDNode<"ISD::STRICT_FMAXIMUM",
                               SDTFPBinOp, [SDNPHasChain,
                                            SDNPCommutative, SDNPAssociative]>;
def strict_fpround    : SDNode<"ISD::STRICT_FP_ROUND",
                               SDTFPRoundOp, [SDNPHasChain]>;
def strict_fpextend   : SDNode<"ISD::STRICT_FP_EXTEND",
                               SDTFPExtendOp, [SDNPHasChain]>;
def strict_fp_to_sint : SDNode<"ISD::STRICT_FP_TO_SINT",
                               SDTFPToIntOp, [SDNPHasChain]>;
def strict_fp_to_uint : SDNode<"ISD::STRICT_FP_TO_UINT",
                               SDTFPToIntOp, [SDNPHasChain]>;
def strict_sint_to_fp : SDNode<"ISD::STRICT_SINT_TO_FP",
                               SDTIntToFPOp, [SDNPHasChain]>;
def strict_uint_to_fp : SDNode<"ISD::STRICT_UINT_TO_FP",
                               SDTIntToFPOp, [SDNPHasChain]>;

def strict_f16_to_fp  : SDNode<"ISD::STRICT_FP16_TO_FP",
                               SDTIntToFPOp, [SDNPHasChain]>;
def strict_fp_to_f16  : SDNode<"ISD::STRICT_FP_TO_FP16",
                               SDTFPToIntOp, [SDNPHasChain]>;

def strict_bf16_to_fp  : SDNode<"ISD::STRICT_BF16_TO_FP",
                               SDTIntToFPOp, [SDNPHasChain]>;
def strict_fp_to_bf16  : SDNode<"ISD::STRICT_FP_TO_BF16",
                               SDTFPToIntOp, [SDNPHasChain]>;

def strict_fsetcc  : SDNode<"ISD::STRICT_FSETCC",  SDTSetCC, [SDNPHasChain]>;
def strict_fsetccs : SDNode<"ISD::STRICT_FSETCCS", SDTSetCC, [SDNPHasChain]>;

def get_fpenv      : SDNode<"ISD::GET_FPENV", SDTGetFPStateOp, [SDNPHasChain]>;
def set_fpenv      : SDNode<"ISD::SET_FPENV", SDTSetFPStateOp, [SDNPHasChain]>;
def reset_fpenv    : SDNode<"ISD::RESET_FPENV", SDTNone, [SDNPHasChain]>;
def get_fpmode     : SDNode<"ISD::GET_FPMODE", SDTGetFPStateOp, [SDNPHasChain]>;
def set_fpmode     : SDNode<"ISD::SET_FPMODE", SDTSetFPStateOp, [SDNPHasChain]>;
def reset_fpmode   : SDNode<"ISD::RESET_FPMODE", SDTNone, [SDNPHasChain]>;

def setcc      : SDNode<"ISD::SETCC"      , SDTSetCC>;
def select     : SDNode<"ISD::SELECT"     , SDTSelect>;
def vselect    : SDNode<"ISD::VSELECT"    , SDTVSelect>;
def selectcc   : SDNode<"ISD::SELECT_CC"  , SDTSelectCC>;

def brcc       : SDNode<"ISD::BR_CC"      , SDTBrCC,   [SDNPHasChain]>;
def brcond     : SDNode<"ISD::BRCOND"     , SDTBrcond, [SDNPHasChain]>;
def brind      : SDNode<"ISD::BRIND"      , SDTBrind,  [SDNPHasChain]>;
def br         : SDNode<"ISD::BR"         , SDTBr,     [SDNPHasChain]>;
def catchret   : SDNode<"ISD::CATCHRET"   , SDTCatchret,
                        [SDNPHasChain, SDNPSideEffect]>;
def cleanupret : SDNode<"ISD::CLEANUPRET" , SDTCleanupret, [SDNPHasChain]>;

def trap       : SDNode<"ISD::TRAP"       , SDTNone,
                        [SDNPHasChain, SDNPSideEffect]>;
def debugtrap  : SDNode<"ISD::DEBUGTRAP"  , SDTNone,
                        [SDNPHasChain, SDNPSideEffect]>;
def ubsantrap  : SDNode<"ISD::UBSANTRAP"  , SDTUBSANTrap,
                        [SDNPHasChain, SDNPSideEffect]>;

def prefetch   : SDNode<"ISD::PREFETCH"   , SDTPrefetch,
                        [SDNPHasChain, SDNPMayLoad, SDNPMayStore,
                         SDNPMemOperand]>;

def readcyclecounter : SDNode<"ISD::READCYCLECOUNTER", SDTIntLeaf,
                     [SDNPHasChain, SDNPSideEffect]>;

def readsteadycounter : SDNode<"ISD::READSTEADYCOUNTER", SDTIntLeaf,
                     [SDNPHasChain, SDNPSideEffect]>;

def membarrier : SDNode<"ISD::MEMBARRIER", SDTNone,
                        [SDNPHasChain, SDNPSideEffect]>;

def jump_table_debug_info : SDNode<"ISD::JUMP_TABLE_DEBUG_INFO", SDTNone,
                        [SDNPHasChain]>;

def atomic_fence : SDNode<"ISD::ATOMIC_FENCE" , SDTAtomicFence,
                          [SDNPHasChain, SDNPSideEffect]>;

def atomic_cmp_swap : SDNode<"ISD::ATOMIC_CMP_SWAP" , SDTAtomic3,
                    [SDNPHasChain, SDNPMayStore, SDNPMayLoad, SDNPMemOperand]>;
def atomic_load_add : SDNode<"ISD::ATOMIC_LOAD_ADD" , SDTAtomic2,
                    [SDNPHasChain, SDNPMayStore, SDNPMayLoad, SDNPMemOperand]>;
def atomic_swap     : SDNode<"ISD::ATOMIC_SWAP", SDTAtomic2,
                    [SDNPHasChain, SDNPMayStore, SDNPMayLoad, SDNPMemOperand]>;
def atomic_load_sub : SDNode<"ISD::ATOMIC_LOAD_SUB" , SDTAtomic2,
                    [SDNPHasChain, SDNPMayStore, SDNPMayLoad, SDNPMemOperand]>;
def atomic_load_and : SDNode<"ISD::ATOMIC_LOAD_AND" , SDTAtomic2,
                    [SDNPHasChain, SDNPMayStore, SDNPMayLoad, SDNPMemOperand]>;
def atomic_load_clr : SDNode<"ISD::ATOMIC_LOAD_CLR" , SDTAtomic2,
                    [SDNPHasChain, SDNPMayStore, SDNPMayLoad, SDNPMemOperand]>;
def atomic_load_or  : SDNode<"ISD::ATOMIC_LOAD_OR" , SDTAtomic2,
                    [SDNPHasChain, SDNPMayStore, SDNPMayLoad, SDNPMemOperand]>;
def atomic_load_xor : SDNode<"ISD::ATOMIC_LOAD_XOR" , SDTAtomic2,
                    [SDNPHasChain, SDNPMayStore, SDNPMayLoad, SDNPMemOperand]>;
def atomic_load_nand: SDNode<"ISD::ATOMIC_LOAD_NAND", SDTAtomic2,
                    [SDNPHasChain, SDNPMayStore, SDNPMayLoad, SDNPMemOperand]>;
def atomic_load_min : SDNode<"ISD::ATOMIC_LOAD_MIN", SDTAtomic2,
                    [SDNPHasChain, SDNPMayStore, SDNPMayLoad, SDNPMemOperand]>;
def atomic_load_max : SDNode<"ISD::ATOMIC_LOAD_MAX", SDTAtomic2,
                    [SDNPHasChain, SDNPMayStore, SDNPMayLoad, SDNPMemOperand]>;
def atomic_load_umin : SDNode<"ISD::ATOMIC_LOAD_UMIN", SDTAtomic2,
                    [SDNPHasChain, SDNPMayStore, SDNPMayLoad, SDNPMemOperand]>;
def atomic_load_umax : SDNode<"ISD::ATOMIC_LOAD_UMAX", SDTAtomic2,
                    [SDNPHasChain, SDNPMayStore, SDNPMayLoad, SDNPMemOperand]>;
def atomic_load_fadd : SDNode<"ISD::ATOMIC_LOAD_FADD" , SDTFPAtomic2,
                    [SDNPHasChain, SDNPMayStore, SDNPMayLoad, SDNPMemOperand]>;
def atomic_load_fsub : SDNode<"ISD::ATOMIC_LOAD_FSUB" , SDTFPAtomic2,
                    [SDNPHasChain, SDNPMayStore, SDNPMayLoad, SDNPMemOperand]>;
def atomic_load_fmax : SDNode<"ISD::ATOMIC_LOAD_FMAX", SDTFPAtomic2,
                    [SDNPHasChain, SDNPMayStore, SDNPMayLoad, SDNPMemOperand]>;
def atomic_load_fmin : SDNode<"ISD::ATOMIC_LOAD_FMIN", SDTFPAtomic2,
                    [SDNPHasChain, SDNPMayStore, SDNPMayLoad, SDNPMemOperand]>;
def atomic_load_uinc_wrap : SDNode<"ISD::ATOMIC_LOAD_UINC_WRAP", SDTAtomic2,
                    [SDNPHasChain, SDNPMayStore, SDNPMayLoad, SDNPMemOperand]>;
def atomic_load_udec_wrap : SDNode<"ISD::ATOMIC_LOAD_UDEC_WRAP", SDTAtomic2,
                    [SDNPHasChain, SDNPMayStore, SDNPMayLoad, SDNPMemOperand]>;

def atomic_load      : SDNode<"ISD::ATOMIC_LOAD", SDTAtomicLoad,
                    [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;
def atomic_store     : SDNode<"ISD::ATOMIC_STORE", SDTAtomicStore,
                    [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;

def masked_st    : SDNode<"ISD::MSTORE",  SDTMaskedStore,
                       [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;
def masked_ld    : SDNode<"ISD::MLOAD",  SDTMaskedLoad,
                       [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;

def masked_gather : SDNode<"ISD::MGATHER", SDTMaskedGather,
                           [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;

def masked_scatter : SDNode<"ISD::MSCATTER", SDTMaskedScatter,
                            [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;

def vector_compress : SDNode<"ISD::VECTOR_COMPRESS", SDTVectorCompress>;

// Do not use ld, st directly. Use load, extload, sextload, zextload, store,
// and truncst (see below).
def ld         : SDNode<"ISD::LOAD"       , SDTLoad,
                        [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;
def st         : SDNode<"ISD::STORE"      , SDTStore,
                        [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;
def ist        : SDNode<"ISD::STORE"      , SDTIStore,
                        [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;

def vector_shuffle : SDNode<"ISD::VECTOR_SHUFFLE", SDTVecShuffle, []>;
def vector_reverse : SDNode<"ISD::VECTOR_REVERSE", SDTVecReverse>;
def vector_splice : SDNode<"ISD::VECTOR_SPLICE", SDTVecSlice, []>;
def build_vector : SDNode<"ISD::BUILD_VECTOR", SDTypeProfile<1, -1, []>, []>;
def splat_vector : SDNode<"ISD::SPLAT_VECTOR", SDTypeProfile<1, 1, []>, []>;
def step_vector : SDNode<"ISD::STEP_VECTOR", SDTypeProfile<1, 1,
                       [SDTCisVec<0>, SDTCisInt<1>]>, []>;
def scalar_to_vector : SDNode<"ISD::SCALAR_TO_VECTOR", SDTypeProfile<1, 1, []>,
                              []>;

// vector_extract/vector_insert are deprecated. extractelt/insertelt
// are preferred.
def vector_extract : SDNode<"ISD::EXTRACT_VECTOR_ELT",
    SDTypeProfile<1, 2, [SDTCisPtrTy<2>]>, []>;
def vector_insert : SDNode<"ISD::INSERT_VECTOR_ELT",
    SDTypeProfile<1, 3, [SDTCisSameAs<0, 1>, SDTCisPtrTy<3>]>, []>;
def concat_vectors : SDNode<"ISD::CONCAT_VECTORS",
    SDTypeProfile<1, 2, [SDTCisSubVecOfVec<1, 0>, SDTCisSameAs<1, 2>]>,[]>;

// This operator does not do subvector type checking.  The ARM
// backend, at least, needs it.
def vector_extract_subvec : SDNode<"ISD::EXTRACT_SUBVECTOR",
    SDTypeProfile<1, 2, [SDTCisInt<2>, SDTCisVec<1>, SDTCisVec<0>]>,
    []>;
def vector_insert_subvec : SDNode<"ISD::INSERT_SUBVECTOR",
    SDTypeProfile<1, 3, [SDTCisVec<0>, SDTCisSameAs<0, 1>, SDTCisVec<2>, SDTCisInt<3>]>,
    []>;

// This operator does subvector type checking.
def extract_subvector : SDNode<"ISD::EXTRACT_SUBVECTOR", SDTSubVecExtract, []>;
def insert_subvector : SDNode<"ISD::INSERT_SUBVECTOR", SDTSubVecInsert, []>;

// Nodes for intrinsics, you should use the intrinsic itself and let tblgen use
// these internally.  Don't reference these directly.
def intrinsic_void : SDNode<"ISD::INTRINSIC_VOID",
                            SDTypeProfile<0, -1, [SDTCisPtrTy<0>]>,
                            [SDNPHasChain]>;
def intrinsic_w_chain : SDNode<"ISD::INTRINSIC_W_CHAIN",
                               SDTypeProfile<1, -1, [SDTCisPtrTy<1>]>,
                               [SDNPHasChain]>;
def intrinsic_wo_chain : SDNode<"ISD::INTRINSIC_WO_CHAIN",
                                SDTypeProfile<1, -1, [SDTCisPtrTy<1>]>, []>;

def SDT_assert : SDTypeProfile<1, 1,
  [SDTCisInt<0>, SDTCisInt<1>, SDTCisSameAs<1, 0>]>;
def assertsext : SDNode<"ISD::AssertSext", SDT_assert>;
def assertzext : SDNode<"ISD::AssertZext", SDT_assert>;
def assertalign : SDNode<"ISD::AssertAlign", SDT_assert>;

def convergencectrl_anchor : SDNode<"ISD::CONVERGENCECTRL_ANCHOR",
                                    SDTypeProfile<1, 0, [SDTCisVT<0,untyped>]>>;
def convergencectrl_entry  : SDNode<"ISD::CONVERGENCECTRL_ENTRY",
                                    SDTypeProfile<1, 0, [SDTCisVT<0,untyped>]>>;
def convergencectrl_loop   : SDNode<"ISD::CONVERGENCECTRL_LOOP",
                                    SDTypeProfile<1, 1,
                                      [SDTCisVT<0,untyped>, SDTCisVT<1,untyped>]>>;
def convergencectrl_glue   : SDNode<"ISD::CONVERGENCECTRL_GLUE",
                                    SDTypeProfile<0, 1, [SDTCisVT<0, untyped>]>>;

//===----------------------------------------------------------------------===//
// Selection DAG Condition Codes

class CondCode<string fcmpName = "", string icmpName = ""> {
  string ICmpPredicate = icmpName;
  string FCmpPredicate = fcmpName;
}

// ISD::CondCode enums, and mapping to CmpInst::Predicate names
def SETOEQ : CondCode<"FCMP_OEQ">;
def SETOGT : CondCode<"FCMP_OGT">;
def SETOGE : CondCode<"FCMP_OGE">;
def SETOLT : CondCode<"FCMP_OLT">;
def SETOLE : CondCode<"FCMP_OLE">;
def SETONE : CondCode<"FCMP_ONE">;
def SETO   : CondCode<"FCMP_ORD">;
def SETUO  : CondCode<"FCMP_UNO">;
def SETUEQ : CondCode<"FCMP_UEQ">;
def SETUGT : CondCode<"FCMP_UGT", "ICMP_UGT">;
def SETUGE : CondCode<"FCMP_UGE", "ICMP_UGE">;
def SETULT : CondCode<"FCMP_ULT", "ICMP_ULT">;
def SETULE : CondCode<"FCMP_ULE", "ICMP_ULE">;
def SETUNE : CondCode<"FCMP_UNE">;
def SETEQ : CondCode<"", "ICMP_EQ">;
def SETGT : CondCode<"", "ICMP_SGT">;
def SETGE : CondCode<"", "ICMP_SGE">;
def SETLT : CondCode<"", "ICMP_SLT">;
def SETLE : CondCode<"", "ICMP_SLE">;
def SETNE : CondCode<"", "ICMP_NE">;

//===----------------------------------------------------------------------===//
// Selection DAG Node Transformation Functions.
//
// This mechanism allows targets to manipulate nodes in the output DAG once a
// match has been formed.  This is typically used to manipulate immediate
// values.
//
class SDNodeXForm<SDNode opc, code xformFunction> {
  SDNode Opcode = opc;
  code XFormFunction = xformFunction;
}

def NOOP_SDNodeXForm : SDNodeXForm<imm, [{}]>;

//===----------------------------------------------------------------------===//
// Selection DAG Pattern Fragments.
//
// Pattern fragments are reusable chunks of dags that match specific things.
// They can take arguments and have C++ predicates that control whether they
// match.  They are intended to make the patterns for common instructions more
// compact and readable.
//

/// PatFrags - Represents a set of pattern fragments.  Each single fragment
/// can match something on the DAG, from a single node to multiple nested other
/// fragments.   The whole set of fragments matches if any of the single
/// fragments match.  This allows e.g. matching and "add with overflow" and
/// a regular "add" with the same fragment set.
///
class PatFrags<dag ops, list<dag> frags, code pred = [{}],
               SDNodeXForm xform = NOOP_SDNodeXForm> : SDPatternOperator {
  dag Operands = ops;
  list<dag> Fragments = frags;
  code PredicateCode = pred;
  code GISelPredicateCode = [{}];
  code ImmediateCode = [{}];
  SDNodeXForm OperandTransform = xform;

  // When this is set, the PredicateCode may refer to a constant Operands
  // vector which contains the captured nodes of the DAG, in the order listed
  // by the Operands field above.
  //
  // This is useful when Fragments involves associative / commutative
  // operators: a single piece of code can easily refer to all operands even
  // when re-associated / commuted variants of the fragment are matched.
  bit PredicateCodeUsesOperands = false;

  // Define a few pre-packaged predicates. This helps GlobalISel import
  // existing rules from SelectionDAG for many common cases.
  // They will be tested prior to the code in pred and must not be used in
  // ImmLeaf and its subclasses.

  // If set to true, a predicate is added that checks for the absence of use of
  // the first result.
  bit HasNoUse = ?;
  // If set to true, a predicate is added that checks for the sole use of
  // the first result.
  bit HasOneUse = ?;

  // Is the desired pre-packaged predicate for a load?
  bit IsLoad = ?;
  // Is the desired pre-packaged predicate for a store?
  bit IsStore = ?;
  // Is the desired pre-packaged predicate for an atomic?
  bit IsAtomic = ?;

  // cast<LoadSDNode>(N)->getAddressingMode() == ISD::UNINDEXED;
  // cast<StoreSDNode>(N)->getAddressingMode() == ISD::UNINDEXED;
  bit IsUnindexed = ?;

  // cast<LoadSDNode>(N)->getExtensionType() != ISD::NON_EXTLOAD
  bit IsNonExtLoad = ?;
  // cast<LoadSDNode>(N)->getExtensionType() == ISD::EXTLOAD;
  bit IsAnyExtLoad = ?;
  // cast<LoadSDNode>(N)->getExtensionType() == ISD::SEXTLOAD;
  bit IsSignExtLoad = ?;
  // cast<LoadSDNode>(N)->getExtensionType() == ISD::ZEXTLOAD;
  bit IsZeroExtLoad = ?;
  // !cast<StoreSDNode>(N)->isTruncatingStore();
  // cast<StoreSDNode>(N)->isTruncatingStore();
  bit IsTruncStore = ?;

  // cast<MemSDNode>(N)->getAddressSpace() ==
  // If this empty, accept any address space.
  list<int> AddressSpaces = ?;

  // cast<MemSDNode>(N)->getAlign() >=
  // If this is empty, accept any alignment.
  int MinAlignment = ?;

  // cast<AtomicSDNode>(N)->getOrdering() == AtomicOrdering::Monotonic
  bit IsAtomicOrderingMonotonic = ?;
  // cast<AtomicSDNode>(N)->getOrdering() == AtomicOrdering::Acquire
  bit IsAtomicOrderingAcquire = ?;
  // cast<AtomicSDNode>(N)->getOrdering() == AtomicOrdering::Release
  bit IsAtomicOrderingRelease = ?;
  // cast<AtomicSDNode>(N)->getOrdering() == AtomicOrdering::AcquireRelease
  bit IsAtomicOrderingAcquireRelease = ?;
  // cast<AtomicSDNode>(N)->getOrdering() == AtomicOrdering::SequentiallyConsistent
  bit IsAtomicOrderingSequentiallyConsistent = ?;

  // isAcquireOrStronger(cast<AtomicSDNode>(N)->getOrdering())
  // !isAcquireOrStronger(cast<AtomicSDNode>(N)->getOrdering())
  bit IsAtomicOrderingAcquireOrStronger = ?;

  // isReleaseOrStronger(cast<AtomicSDNode>(N)->getOrdering())
  // !isReleaseOrStronger(cast<AtomicSDNode>(N)->getOrdering())
  bit IsAtomicOrderingReleaseOrStronger = ?;

  // cast<LoadSDNode>(N)->getMemoryVT() == MVT::<VT>;
  // cast<StoreSDNode>(N)->getMemoryVT() == MVT::<VT>;
  ValueType MemoryVT = ?;
  // cast<LoadSDNode>(N)->getMemoryVT().getScalarType() == MVT::<VT>;
  // cast<StoreSDNode>(N)->getMemoryVT().getScalarType() == MVT::<VT>;
  ValueType ScalarMemoryVT = ?;
}

// Patterns and PatFrags can also subclass GISelFlags to set flags that affect
// how GlobalISel behaves when matching them.
class GISelFlags {
  bit GIIgnoreCopies = ?;
}

// PatFrag - A version of PatFrags matching only a single fragment.
class PatFrag<dag ops, dag frag, code pred = [{}],
              SDNodeXForm xform = NOOP_SDNodeXForm>
  : PatFrags<ops, [frag], pred, xform>;

// OutPatFrag is a pattern fragment that is used as part of an output pattern
// (not an input pattern). These do not have predicates or transforms, but are
// used to avoid repeated subexpressions in output patterns.
class OutPatFrag<dag ops, dag frag>
 : PatFrag<ops, frag, [{}], NOOP_SDNodeXForm>;

// PatLeaf's are pattern fragments that have no operands.  This is just a helper
// to define immediates and other common things concisely.
class PatLeaf<dag frag, code pred = [{}], SDNodeXForm xform = NOOP_SDNodeXForm>
 : PatFrag<(ops), frag, pred, xform>;


// ImmLeaf is a pattern fragment with a constraint on the immediate.  The
// constraint is a function that is run on the immediate (always with the value
// sign extended out to an int64_t) as Imm.  For example:
//
//  def immSExt8 : ImmLeaf<i16, [{ return (char)Imm == Imm; }]>;
//
// this is a more convenient form to match 'imm' nodes in than PatLeaf and also
// is preferred over using PatLeaf because it allows the code generator to
// reason more about the constraint.
//
// If FastIsel should ignore all instructions that have an operand of this type,
// the FastIselShouldIgnore flag can be set.  This is an optimization to reduce
// the code size of the generated fast instruction selector.
class ImmLeaf<ValueType vt, code pred, SDNodeXForm xform = NOOP_SDNodeXForm,
              SDNode ImmNode = imm>
  : PatFrag<(ops), (vt ImmNode), [{}], xform> {
  let ImmediateCode = pred;
  bit FastIselShouldIgnore = false;

  // Is the data type of the immediate an APInt?
  bit IsAPInt = false;

  // Is the data type of the immediate an APFloat?
  bit IsAPFloat = false;
}

// Convenience wrapper for ImmLeaf to use timm/TargetConstant instead
// of imm/Constant.
class TImmLeaf<ValueType vt, code pred, SDNodeXForm xform = NOOP_SDNodeXForm,
  SDNode ImmNode = timm> : ImmLeaf<vt, pred, xform, ImmNode>;

// An ImmLeaf except that Imm is an APInt. This is useful when you need to
// zero-extend the immediate instead of sign-extend it.
//
// Note that FastISel does not currently understand IntImmLeaf and will not
// generate code for rules that make use of it. As such, it does not make sense
// to replace ImmLeaf with IntImmLeaf. However, replacing PatLeaf with an
// IntImmLeaf will allow GlobalISel to import the rule.
class IntImmLeaf<ValueType vt, code pred, SDNodeXForm xform = NOOP_SDNodeXForm>
    : ImmLeaf<vt, pred, xform> {
  let IsAPInt = true;
  let FastIselShouldIgnore = true;
}

// An ImmLeaf except that Imm is an APFloat.
//
// Note that FastISel does not currently understand FPImmLeaf and will not
// generate code for rules that make use of it.
class FPImmLeaf<ValueType vt, code pred, SDNodeXForm xform = NOOP_SDNodeXForm>
  : ImmLeaf<vt, pred, xform, fpimm> {
  let IsAPFloat = true;
  let FastIselShouldIgnore = true;
}

// Leaf fragments.

def vtInt      : PatLeaf<(vt),  [{ return N->getVT().isInteger(); }]>;
def vtFP       : PatLeaf<(vt),  [{ return N->getVT().isFloatingPoint(); }]>;

// Use ISD::isConstantSplatVectorAllOnes or ISD::isConstantSplatVectorAllZeros
// to look for the corresponding build_vector or splat_vector. Will look through
// bitcasts and check for either opcode, except when used as a pattern root.
// When used as a pattern root, only fixed-length build_vector and scalable
// splat_vector are supported.
def immAllOnesV  : SDPatternOperator; // ISD::isConstantSplatVectorAllOnes
def immAllZerosV : SDPatternOperator; // ISD::isConstantSplatVectorAllZeros

// Other helper fragments.
def not  : PatFrag<(ops node:$in), (xor node:$in, -1)>;
def vnot : PatFrag<(ops node:$in), (xor node:$in, immAllOnesV)>;
def ineg : PatFrag<(ops node:$in), (sub 0, node:$in)>;

def zanyext : PatFrags<(ops node:$op),
                       [(zext node:$op),
                        (anyext node:$op)]>;

// null_frag - The null pattern operator is used in multiclass instantiations
// which accept an SDPatternOperator for use in matching patterns for internal
// definitions. When expanding a pattern, if the null fragment is referenced
// in the expansion, the pattern is discarded and it is as-if '[]' had been
// specified. This allows multiclasses to have the isel patterns be optional.
def null_frag : SDPatternOperator;

// load fragments.
def unindexedload : PatFrag<(ops node:$ptr), (ld node:$ptr)> {
  let IsLoad = true;
  let IsUnindexed = true;
}
def load : PatFrag<(ops node:$ptr), (unindexedload node:$ptr)> {
  let IsLoad = true;
  let IsNonExtLoad = true;
}

// extending load fragments.
def extload   : PatFrag<(ops node:$ptr), (unindexedload node:$ptr)> {
  let IsLoad = true;
  let IsAnyExtLoad = true;
}
def sextload  : PatFrag<(ops node:$ptr), (unindexedload node:$ptr)> {
  let IsLoad = true;
  let IsSignExtLoad = true;
}
def zextload  : PatFrag<(ops node:$ptr), (unindexedload node:$ptr)> {
  let IsLoad = true;
  let IsZeroExtLoad = true;
}

def extloadi1  : PatFrag<(ops node:$ptr), (extload node:$ptr)> {
  let IsLoad = true;
  let MemoryVT = i1;
}
def extloadi8  : PatFrag<(ops node:$ptr), (extload node:$ptr)> {
  let IsLoad = true;
  let MemoryVT = i8;
}
def extloadi16 : PatFrag<(ops node:$ptr), (extload node:$ptr)> {
  let IsLoad = true;
  let MemoryVT = i16;
}
def extloadi32 : PatFrag<(ops node:$ptr), (extload node:$ptr)> {
  let IsLoad = true;
  let MemoryVT = i32;
}
def extloadi64 : PatFrag<(ops node:$ptr), (extload node:$ptr)> {
  let IsLoad = true;
  let MemoryVT = i64;
}
def extloadf16 : PatFrag<(ops node:$ptr), (extload node:$ptr)> {
  let IsLoad = true;
  let MemoryVT = f16;
}
def extloadf32 : PatFrag<(ops node:$ptr), (extload node:$ptr)> {
  let IsLoad = true;
  let MemoryVT = f32;
}
def extloadf64 : PatFrag<(ops node:$ptr), (extload node:$ptr)> {
  let IsLoad = true;
  let MemoryVT = f64;
}

def sextloadi1  : PatFrag<(ops node:$ptr), (sextload node:$ptr)> {
  let IsLoad = true;
  let MemoryVT = i1;
}
def sextloadi8  : PatFrag<(ops node:$ptr), (sextload node:$ptr)> {
  let IsLoad = true;
  let MemoryVT = i8;
}
def sextloadi16 : PatFrag<(ops node:$ptr), (sextload node:$ptr)> {
  let IsLoad = true;
  let MemoryVT = i16;
}
def sextloadi32 : PatFrag<(ops node:$ptr), (sextload node:$ptr)> {
  let IsLoad = true;
  let MemoryVT = i32;
}
def sextloadi64 : PatFrag<(ops node:$ptr), (sextload node:$ptr)> {
  let IsLoad = true;
  let MemoryVT = i64;
}

def zextloadi1  : PatFrag<(ops node:$ptr), (zextload node:$ptr)> {
  let IsLoad = true;
  let MemoryVT = i1;
}
def zextloadi8  : PatFrag<(ops node:$ptr), (zextload node:$ptr)> {
  let IsLoad = true;
  let MemoryVT = i8;
}
def zextloadi16 : PatFrag<(ops node:$ptr), (zextload node:$ptr)> {
  let IsLoad = true;
  let MemoryVT = i16;
}
def zextloadi32 : PatFrag<(ops node:$ptr), (zextload node:$ptr)> {
  let IsLoad = true;
  let MemoryVT = i32;
}
def zextloadi64 : PatFrag<(ops node:$ptr), (zextload node:$ptr)> {
  let IsLoad = true;
  let MemoryVT = i64;
}

def extloadvi1  : PatFrag<(ops node:$ptr), (extload node:$ptr)> {
  let IsLoad = true;
  let ScalarMemoryVT = i1;
}
def extloadvi8  : PatFrag<(ops node:$ptr), (extload node:$ptr)> {
  let IsLoad = true;
  let ScalarMemoryVT = i8;
}
def extloadvi16 : PatFrag<(ops node:$ptr), (extload node:$ptr)> {
  let IsLoad = true;
  let ScalarMemoryVT = i16;
}
def extloadvi32 : PatFrag<(ops node:$ptr), (extload node:$ptr)> {
  let IsLoad = true;
  let ScalarMemoryVT = i32;
}
def extloadvf16 : PatFrag<(ops node:$ptr), (extload node:$ptr)> {
  let IsLoad = true;
  let ScalarMemoryVT = f16;
}
def extloadvf32 : PatFrag<(ops node:$ptr), (extload node:$ptr)> {
  let IsLoad = true;
  let ScalarMemoryVT = f32;
}
def extloadvf64 : PatFrag<(ops node:$ptr), (extload node:$ptr)> {
  let IsLoad = true;
  let ScalarMemoryVT = f64;
}

def sextloadvi1  : PatFrag<(ops node:$ptr), (sextload node:$ptr)> {
  let IsLoad = true;
  let ScalarMemoryVT = i1;
}
def sextloadvi8  : PatFrag<(ops node:$ptr), (sextload node:$ptr)> {
  let IsLoad = true;
  let ScalarMemoryVT = i8;
}
def sextloadvi16 : PatFrag<(ops node:$ptr), (sextload node:$ptr)> {
  let IsLoad = true;
  let ScalarMemoryVT = i16;
}
def sextloadvi32 : PatFrag<(ops node:$ptr), (sextload node:$ptr)> {
  let IsLoad = true;
  let ScalarMemoryVT = i32;
}

def zextloadvi1  : PatFrag<(ops node:$ptr), (zextload node:$ptr)> {
  let IsLoad = true;
  let ScalarMemoryVT = i1;
}
def zextloadvi8  : PatFrag<(ops node:$ptr), (zextload node:$ptr)> {
  let IsLoad = true;
  let ScalarMemoryVT = i8;
}
def zextloadvi16 : PatFrag<(ops node:$ptr), (zextload node:$ptr)> {
  let IsLoad = true;
  let ScalarMemoryVT = i16;
}
def zextloadvi32 : PatFrag<(ops node:$ptr), (zextload node:$ptr)> {
  let IsLoad = true;
  let ScalarMemoryVT = i32;
}

// store fragments.
def unindexedstore : PatFrag<(ops node:$val, node:$ptr),
                             (st node:$val, node:$ptr)> {
  let IsStore = true;
  let IsUnindexed = true;
}
def store : PatFrag<(ops node:$val, node:$ptr),
                    (unindexedstore node:$val, node:$ptr)> {
  let IsStore = true;
  let IsTruncStore = false;
}

// truncstore fragments.
def truncstore : PatFrag<(ops node:$val, node:$ptr),
                         (unindexedstore node:$val, node:$ptr)> {
  let IsStore = true;
  let IsTruncStore = true;
}
def truncstorei8 : PatFrag<(ops node:$val, node:$ptr),
                           (truncstore node:$val, node:$ptr)> {
  let IsStore = true;
  let MemoryVT = i8;
  let IsTruncStore = true;
}
def truncstorei16 : PatFrag<(ops node:$val, node:$ptr),
                            (truncstore node:$val, node:$ptr)> {
  let IsStore = true;
  let MemoryVT = i16;
  let IsTruncStore = true;
}
def truncstorei32 : PatFrag<(ops node:$val, node:$ptr),
                            (truncstore node:$val, node:$ptr)> {
  let IsStore = true;
  let MemoryVT = i32;
  let IsTruncStore = true;
}
def truncstorei64 : PatFrag<(ops node:$val, node:$ptr),
                            (truncstore node:$val, node:$ptr)> {
  let IsStore = true;
  let MemoryVT = i64;
  let IsTruncStore = true;
}
def truncstoref16 : PatFrag<(ops node:$val, node:$ptr),
                            (truncstore node:$val, node:$ptr)> {
  let IsStore = true;
  let MemoryVT = f16;
}
def truncstoref32 : PatFrag<(ops node:$val, node:$ptr),
                            (truncstore node:$val, node:$ptr)> {
  let IsStore = true;
  let MemoryVT = f32;
}
def truncstoref64 : PatFrag<(ops node:$val, node:$ptr),
                            (truncstore node:$val, node:$ptr)> {
  let IsStore = true;
  let MemoryVT = f64;
}

def truncstorevi8 : PatFrag<(ops node:$val, node:$ptr),
                            (truncstore node:$val, node:$ptr)> {
  let IsStore = true;
  let ScalarMemoryVT = i8;
}

def truncstorevi16 : PatFrag<(ops node:$val, node:$ptr),
                             (truncstore node:$val, node:$ptr)> {
  let IsStore = true;
  let ScalarMemoryVT = i16;
}

def truncstorevi32 : PatFrag<(ops node:$val, node:$ptr),
                             (truncstore node:$val, node:$ptr)> {
  let IsStore = true;
  let ScalarMemoryVT = i32;
}

// indexed store fragments.
def istore : PatFrag<(ops node:$val, node:$base, node:$offset),
                     (ist node:$val, node:$base, node:$offset)> {
  let IsStore = true;
  let IsTruncStore = false;
}

def pre_store : PatFrag<(ops node:$val, node:$base, node:$offset),
                        (istore node:$val, node:$base, node:$offset), [{
  ISD::MemIndexedMode AM = cast<StoreSDNode>(N)->getAddressingMode();
  return AM == ISD::PRE_INC || AM == ISD::PRE_DEC;
}]>;

def itruncstore : PatFrag<(ops node:$val, node:$base, node:$offset),
                          (ist node:$val, node:$base, node:$offset)> {
  let IsStore = true;
  let IsTruncStore = true;
}
def pre_truncst : PatFrag<(ops node:$val, node:$base, node:$offset),
                          (itruncstore node:$val, node:$base, node:$offset), [{
  ISD::MemIndexedMode AM = cast<StoreSDNode>(N)->getAddressingMode();
  return AM == ISD::PRE_INC || AM == ISD::PRE_DEC;
}]>;
def pre_truncsti1 : PatFrag<(ops node:$val, node:$base, node:$offset),
                            (pre_truncst node:$val, node:$base, node:$offset)> {
  let IsStore = true;
  let MemoryVT = i1;
}
def pre_truncsti8 : PatFrag<(ops node:$val, node:$base, node:$offset),
                            (pre_truncst node:$val, node:$base, node:$offset)> {
  let IsStore = true;
  let MemoryVT = i8;
}
def pre_truncsti16 : PatFrag<(ops node:$val, node:$base, node:$offset),
                             (pre_truncst node:$val, node:$base, node:$offset)> {
  let IsStore = true;
  let MemoryVT = i16;
}
def pre_truncsti32 : PatFrag<(ops node:$val, node:$base, node:$offset),
                             (pre_truncst node:$val, node:$base, node:$offset)> {
  let IsStore = true;
  let MemoryVT = i32;
}
def pre_truncstf32 : PatFrag<(ops node:$val, node:$base, node:$offset),
                             (pre_truncst node:$val, node:$base, node:$offset)> {
  let IsStore = true;
  let MemoryVT = f32;
}
def pre_truncstvi8 : PatFrag<(ops node:$val, node:$base, node:$offset),
                             (pre_truncst node:$val, node:$base, node:$offset)> {
  let IsStore = true;
  let ScalarMemoryVT = i8;
}
def pre_truncstvi16 : PatFrag<(ops node:$val, node:$base, node:$offset),
                              (pre_truncst node:$val, node:$base, node:$offset)> {
  let IsStore = true;
  let ScalarMemoryVT = i16;
}

def post_store : PatFrag<(ops node:$val, node:$ptr, node:$offset),
                         (istore node:$val, node:$ptr, node:$offset), [{
  ISD::MemIndexedMode AM = cast<StoreSDNode>(N)->getAddressingMode();
  return AM == ISD::POST_INC || AM == ISD::POST_DEC;
}]>;

def post_truncst : PatFrag<(ops node:$val, node:$base, node:$offset),
                           (itruncstore node:$val, node:$base, node:$offset), [{
  ISD::MemIndexedMode AM = cast<StoreSDNode>(N)->getAddressingMode();
  return AM == ISD::POST_INC || AM == ISD::POST_DEC;
}]>;
def post_truncsti1 : PatFrag<(ops node:$val, node:$base, node:$offset),
                             (post_truncst node:$val, node:$base, node:$offset)> {
  let IsStore = true;
  let MemoryVT = i1;
}
def post_truncsti8 : PatFrag<(ops node:$val, node:$base, node:$offset),
                             (post_truncst node:$val, node:$base, node:$offset)> {
  let IsStore = true;
  let MemoryVT = i8;
}
def post_truncsti16 : PatFrag<(ops node:$val, node:$base, node:$offset),
                              (post_truncst node:$val, node:$base, node:$offset)> {
  let IsStore = true;
  let MemoryVT = i16;
}
def post_truncsti32 : PatFrag<(ops node:$val, node:$base, node:$offset),
                              (post_truncst node:$val, node:$base, node:$offset)> {
  let IsStore = true;
  let MemoryVT = i32;
}
def post_truncstf32 : PatFrag<(ops node:$val, node:$base, node:$offset),
                              (post_truncst node:$val, node:$base, node:$offset)> {
  let IsStore = true;
  let MemoryVT = f32;
}
def post_truncstvi8 : PatFrag<(ops node:$val, node:$base, node:$offset),
                              (post_truncst node:$val, node:$base, node:$offset)> {
  let IsStore = true;
  let ScalarMemoryVT = i8;
}
def post_truncstvi16 : PatFrag<(ops node:$val, node:$base, node:$offset),
                               (post_truncst node:$val, node:$base, node:$offset)> {
  let IsStore = true;
  let ScalarMemoryVT = i16;
}

// A helper for matching undef or freeze undef
def undef_or_freeze_undef : PatFrags<(ops), [(undef), (freeze undef)]>;

// TODO: Split these into volatile and unordered flavors to enable
// selectively legal optimizations for each.  (See D66309)
def simple_load : PatFrag<(ops node:$ptr),
                          (load node:$ptr), [{
  return cast<LoadSDNode>(N)->isSimple();
}]>;
def simple_store : PatFrag<(ops node:$val, node:$ptr),
                           (store node:$val, node:$ptr), [{
  return cast<StoreSDNode>(N)->isSimple();
}]>;

// nontemporal store fragments.
def nontemporalstore : PatFrag<(ops node:$val, node:$ptr),
                               (store node:$val, node:$ptr), [{
  return cast<StoreSDNode>(N)->isNonTemporal();
}]>;

def alignednontemporalstore : PatFrag<(ops node:$val, node:$ptr),
                                      (nontemporalstore node:$val, node:$ptr), [{
  StoreSDNode *St = cast<StoreSDNode>(N);
  return St->getAlign() >= St->getMemoryVT().getStoreSize();
}]>;

def unalignednontemporalstore : PatFrag<(ops node:$val, node:$ptr),
                                        (nontemporalstore node:$val, node:$ptr), [{
  StoreSDNode *St = cast<StoreSDNode>(N);
  return St->getAlignment() < St->getMemoryVT().getStoreSize();
}]>;

// nontemporal load fragments.
def nontemporalload : PatFrag<(ops node:$ptr),
                               (load node:$ptr), [{
  return cast<LoadSDNode>(N)->isNonTemporal();
}]>;

def alignednontemporalload : PatFrag<(ops node:$ptr),
                                      (nontemporalload node:$ptr), [{
  LoadSDNode *Ld = cast<LoadSDNode>(N);
  return Ld->getAlign() >= Ld->getMemoryVT().getStoreSize();
}]>;

// setcc convenience fragments.
def setoeq : PatFrag<(ops node:$lhs, node:$rhs),
                     (setcc node:$lhs, node:$rhs, SETOEQ)>;
def setogt : PatFrag<(ops node:$lhs, node:$rhs),
                     (setcc node:$lhs, node:$rhs, SETOGT)>;
def setoge : PatFrag<(ops node:$lhs, node:$rhs),
                     (setcc node:$lhs, node:$rhs, SETOGE)>;
def setolt : PatFrag<(ops node:$lhs, node:$rhs),
                     (setcc node:$lhs, node:$rhs, SETOLT)>;
def setole : PatFrag<(ops node:$lhs, node:$rhs),
                     (setcc node:$lhs, node:$rhs, SETOLE)>;
def setone : PatFrag<(ops node:$lhs, node:$rhs),
                     (setcc node:$lhs, node:$rhs, SETONE)>;
def seto   : PatFrag<(ops node:$lhs, node:$rhs),
                     (setcc node:$lhs, node:$rhs, SETO)>;
def setuo  : PatFrag<(ops node:$lhs, node:$rhs),
                     (setcc node:$lhs, node:$rhs, SETUO)>;
def setueq : PatFrag<(ops node:$lhs, node:$rhs),
                     (setcc node:$lhs, node:$rhs, SETUEQ)>;
def setugt : PatFrag<(ops node:$lhs, node:$rhs),
                     (setcc node:$lhs, node:$rhs, SETUGT)>;
def setuge : PatFrag<(ops node:$lhs, node:$rhs),
                     (setcc node:$lhs, node:$rhs, SETUGE)>;
def setult : PatFrag<(ops node:$lhs, node:$rhs),
                     (setcc node:$lhs, node:$rhs, SETULT)>;
def setule : PatFrag<(ops node:$lhs, node:$rhs),
                     (setcc node:$lhs, node:$rhs, SETULE)>;
def setune : PatFrag<(ops node:$lhs, node:$rhs),
                     (setcc node:$lhs, node:$rhs, SETUNE)>;
def seteq  : PatFrag<(ops node:$lhs, node:$rhs),
                     (setcc node:$lhs, node:$rhs, SETEQ)>;
def setgt  : PatFrag<(ops node:$lhs, node:$rhs),
                     (setcc node:$lhs, node:$rhs, SETGT)>;
def setge  : PatFrag<(ops node:$lhs, node:$rhs),
                     (setcc node:$lhs, node:$rhs, SETGE)>;
def setlt  : PatFrag<(ops node:$lhs, node:$rhs),
                     (setcc node:$lhs, node:$rhs, SETLT)>;
def setle  : PatFrag<(ops node:$lhs, node:$rhs),
                     (setcc node:$lhs, node:$rhs, SETLE)>;
def setne  : PatFrag<(ops node:$lhs, node:$rhs),
                     (setcc node:$lhs, node:$rhs, SETNE)>;

// We don't have strict FP extended loads as single DAG nodes, but we can
// still provide convenience fragments to match those operations.
def strict_extloadf32 : PatFrag<(ops node:$ptr),
                                (strict_fpextend (f32 (load node:$ptr)))>;
def strict_extloadf64 : PatFrag<(ops node:$ptr),
                                (strict_fpextend (f64 (load node:$ptr)))>;

// Convenience fragments to match both strict and non-strict fp operations
def any_fadd       : PatFrags<(ops node:$lhs, node:$rhs),
                              [(strict_fadd node:$lhs, node:$rhs),
                               (fadd node:$lhs, node:$rhs)]>;
def any_fsub       : PatFrags<(ops node:$lhs, node:$rhs),
                              [(strict_fsub node:$lhs, node:$rhs),
                               (fsub node:$lhs, node:$rhs)]>;
def any_fmul       : PatFrags<(ops node:$lhs, node:$rhs),
                              [(strict_fmul node:$lhs, node:$rhs),
                               (fmul node:$lhs, node:$rhs)]>;
def any_fdiv       : PatFrags<(ops node:$lhs, node:$rhs),
                              [(strict_fdiv node:$lhs, node:$rhs),
                               (fdiv node:$lhs, node:$rhs)]>;
def any_frem       : PatFrags<(ops node:$lhs, node:$rhs),
                              [(strict_frem node:$lhs, node:$rhs),
                               (frem node:$lhs, node:$rhs)]>;
def any_fma        : PatFrags<(ops node:$src1, node:$src2, node:$src3),
                              [(strict_fma node:$src1, node:$src2, node:$src3),
                               (fma node:$src1, node:$src2, node:$src3)]>;
def any_fsqrt      : PatFrags<(ops node:$src),
                              [(strict_fsqrt node:$src),
                               (fsqrt node:$src)]>;
def any_fsin       : PatFrags<(ops node:$src),
                              [(strict_fsin node:$src),
                               (fsin node:$src)]>;
def any_fcos       : PatFrags<(ops node:$src),
                              [(strict_fcos node:$src),
                               (fcos node:$src)]>;
def any_ftan       : PatFrags<(ops node:$src),
                              [(strict_ftan node:$src),
                               (ftan node:$src)]>;
def any_fasin      : PatFrags<(ops node:$src),
                              [(strict_fasin node:$src),
                               (fasin node:$src)]>;
def any_facos      : PatFrags<(ops node:$src),
                              [(strict_facos node:$src),
                               (facos node:$src)]>;
def any_fatan      : PatFrags<(ops node:$src),
                              [(strict_fatan node:$src),
                               (fatan node:$src)]>;
def any_fsinh      : PatFrags<(ops node:$src),
                              [(strict_fsinh node:$src),
                               (fsinh node:$src)]>;
def any_fcosh      : PatFrags<(ops node:$src),
                              [(strict_fcosh node:$src),
                               (fcosh node:$src)]>;
def any_ftanh      : PatFrags<(ops node:$src),
                              [(strict_ftanh node:$src),
                               (ftanh node:$src)]>;
def any_fexp2      : PatFrags<(ops node:$src),
                              [(strict_fexp2 node:$src),
                               (fexp2 node:$src)]>;
def any_fpow       : PatFrags<(ops node:$lhs, node:$rhs),
                              [(strict_fpow node:$lhs, node:$rhs),
                               (fpow node:$lhs, node:$rhs)]>;
def any_fldexp      : PatFrags<(ops node:$lhs, node:$rhs),
                              [(strict_fldexp node:$lhs, node:$rhs),
                               (fldexp node:$lhs, node:$rhs)]>;
def any_flog2      : PatFrags<(ops node:$src),
                              [(strict_flog2 node:$src),
                               (flog2 node:$src)]>;
def any_frint      : PatFrags<(ops node:$src),
                              [(strict_frint node:$src),
                               (frint node:$src)]>;
def any_lrint      : PatFrags<(ops node:$src),
                              [(strict_lrint node:$src),
                               (lrint node:$src)]>;
def any_llrint     : PatFrags<(ops node:$src),
                              [(strict_llrint node:$src),
                               (llrint node:$src)]>;
def any_fnearbyint : PatFrags<(ops node:$src),
                              [(strict_fnearbyint node:$src),
                               (fnearbyint node:$src)]>;
def any_fceil      : PatFrags<(ops node:$src),
                              [(strict_fceil node:$src),
                               (fceil node:$src)]>;
def any_ffloor     : PatFrags<(ops node:$src),
                              [(strict_ffloor node:$src),
                               (ffloor node:$src)]>;
def any_lround     : PatFrags<(ops node:$src),
                              [(strict_lround node:$src),
                               (lround node:$src)]>;
def any_llround    : PatFrags<(ops node:$src),
                              [(strict_llround node:$src),
                               (llround node:$src)]>;
def any_fround     : PatFrags<(ops node:$src),
                              [(strict_fround node:$src),
                               (fround node:$src)]>;
def any_froundeven : PatFrags<(ops node:$src),
                              [(strict_froundeven node:$src),
                               (froundeven node:$src)]>;
def any_ftrunc     : PatFrags<(ops node:$src),
                              [(strict_ftrunc node:$src),
                               (ftrunc node:$src)]>;
def any_fmaxnum    : PatFrags<(ops node:$lhs, node:$rhs),
                              [(strict_fmaxnum node:$lhs, node:$rhs),
                               (fmaxnum node:$lhs, node:$rhs)]>;
def any_fminnum    : PatFrags<(ops node:$lhs, node:$rhs),
                              [(strict_fminnum node:$lhs, node:$rhs),
                               (fminnum node:$lhs, node:$rhs)]>;
def any_fmaximum   : PatFrags<(ops node:$lhs, node:$rhs),
                              [(strict_fmaximum node:$lhs, node:$rhs),
                               (fmaximum node:$lhs, node:$rhs)]>;
def any_fminimum   : PatFrags<(ops node:$lhs, node:$rhs),
                              [(strict_fminimum node:$lhs, node:$rhs),
                               (fminimum node:$lhs, node:$rhs)]>;
def any_fpround    : PatFrags<(ops node:$src),
                              [(strict_fpround node:$src),
                               (fpround node:$src)]>;
def any_fpextend   : PatFrags<(ops node:$src),
                              [(strict_fpextend node:$src),
                               (fpextend node:$src)]>;
def any_extloadf32 : PatFrags<(ops node:$ptr),
                              [(strict_extloadf32 node:$ptr),
                               (extloadf32 node:$ptr)]>;
def any_extloadf64 : PatFrags<(ops node:$ptr),
                              [(strict_extloadf64 node:$ptr),
                               (extloadf64 node:$ptr)]>;
def any_fp_to_sint : PatFrags<(ops node:$src),
                              [(strict_fp_to_sint node:$src),
                               (fp_to_sint node:$src)]>;
def any_fp_to_uint : PatFrags<(ops node:$src),
                              [(strict_fp_to_uint node:$src),
                               (fp_to_uint node:$src)]>;
def any_sint_to_fp : PatFrags<(ops node:$src),
                              [(strict_sint_to_fp node:$src),
                               (sint_to_fp node:$src)]>;
def any_uint_to_fp : PatFrags<(ops node:$src),
                              [(strict_uint_to_fp node:$src),
                               (uint_to_fp node:$src)]>;
def any_fsetcc : PatFrags<(ops node:$lhs, node:$rhs, node:$pred),
                          [(strict_fsetcc node:$lhs, node:$rhs, node:$pred),
                           (setcc node:$lhs, node:$rhs, node:$pred)]>;
def any_fsetccs : PatFrags<(ops node:$lhs, node:$rhs, node:$pred),
                          [(strict_fsetccs node:$lhs, node:$rhs, node:$pred),
                           (setcc node:$lhs, node:$rhs, node:$pred)]>;

def any_f16_to_fp : PatFrags<(ops node:$src),
                              [(f16_to_fp node:$src),
                               (strict_f16_to_fp node:$src)]>;
def any_fp_to_f16 : PatFrags<(ops node:$src),
                              [(fp_to_f16 node:$src),
                               (strict_fp_to_f16 node:$src)]>;
def any_bf16_to_fp : PatFrags<(ops node:$src),
                               [(bf16_to_fp node:$src),
                                (strict_bf16_to_fp node:$src)]>;
def any_fp_to_bf16 : PatFrags<(ops node:$src),
                               [(fp_to_bf16 node:$src),
                                (strict_fp_to_bf16 node:$src)]>;

multiclass binary_atomic_op_ord {
  def NAME#_monotonic : PatFrag<(ops node:$ptr, node:$val),
      (!cast<SDPatternOperator>(NAME) node:$ptr, node:$val)> {
    let IsAtomic = true;
    let IsAtomicOrderingMonotonic = true;
  }
  def NAME#_acquire : PatFrag<(ops node:$ptr, node:$val),
      (!cast<SDPatternOperator>(NAME) node:$ptr, node:$val)> {
    let IsAtomic = true;
    let IsAtomicOrderingAcquire = true;
  }
  def NAME#_release : PatFrag<(ops node:$ptr, node:$val),
      (!cast<SDPatternOperator>(NAME) node:$ptr, node:$val)> {
    let IsAtomic = true;
    let IsAtomicOrderingRelease = true;
  }
  def NAME#_acq_rel : PatFrag<(ops node:$ptr, node:$val),
      (!cast<SDPatternOperator>(NAME) node:$ptr, node:$val)> {
    let IsAtomic = true;
    let IsAtomicOrderingAcquireRelease = true;
  }
  def NAME#_seq_cst : PatFrag<(ops node:$ptr, node:$val),
      (!cast<SDPatternOperator>(NAME) node:$ptr, node:$val)> {
    let IsAtomic = true;
    let IsAtomicOrderingSequentiallyConsistent = true;
  }
}

multiclass ternary_atomic_op_ord {
  def NAME#_monotonic : PatFrag<(ops node:$ptr, node:$cmp, node:$val),
      (!cast<SDPatternOperator>(NAME) node:$ptr, node:$cmp, node:$val)> {
    let IsAtomic = true;
    let IsAtomicOrderingMonotonic = true;
  }
  def NAME#_acquire : PatFrag<(ops node:$ptr, node:$cmp, node:$val),
      (!cast<SDPatternOperator>(NAME) node:$ptr, node:$cmp, node:$val)> {
    let IsAtomic = true;
    let IsAtomicOrderingAcquire = true;
  }
  def NAME#_release : PatFrag<(ops node:$ptr, node:$cmp, node:$val),
      (!cast<SDPatternOperator>(NAME) node:$ptr, node:$cmp, node:$val)> {
    let IsAtomic = true;
    let IsAtomicOrderingRelease = true;
  }
  def NAME#_acq_rel : PatFrag<(ops node:$ptr, node:$cmp, node:$val),
      (!cast<SDPatternOperator>(NAME) node:$ptr, node:$cmp, node:$val)> {
    let IsAtomic = true;
    let IsAtomicOrderingAcquireRelease = true;
  }
  def NAME#_seq_cst : PatFrag<(ops node:$ptr, node:$cmp, node:$val),
      (!cast<SDPatternOperator>(NAME) node:$ptr, node:$cmp, node:$val)> {
    let IsAtomic = true;
    let IsAtomicOrderingSequentiallyConsistent = true;
  }
}

multiclass binary_atomic_op<SDNode atomic_op> {
  foreach vt = [ i8, i16, i32, i64 ] in {
    def _#vt : PatFrag<(ops node:$ptr, node:$val),
                       (atomic_op  node:$ptr, node:$val)> {
      let IsAtomic = true;
      let MemoryVT = vt;
    }

    defm NAME#_#vt  : binary_atomic_op_ord;
  }
}

multiclass binary_atomic_op_fp<SDNode atomic_op> {
  foreach vt = [ f16, bf16, v2f16, v2bf16, f32, f64 ] in {
    def _#vt : PatFrag<(ops node:$ptr, node:$val),
                       (atomic_op node:$ptr, node:$val)> {
      let IsAtomic = true;
      let MemoryVT = vt;
    }
  }
}

multiclass ternary_atomic_op<SDNode atomic_op> {
  foreach vt = [ i8, i16, i32, i64 ] in {
    def _#vt : PatFrag<(ops node:$ptr, node:$cmp, node:$val),
                       (atomic_op node:$ptr, node:$cmp, node:$val)> {
      let IsAtomic = true;
      let MemoryVT = vt;
    }

    defm NAME#_#vt  : ternary_atomic_op_ord;
  }
}

defm atomic_load_add  : binary_atomic_op<atomic_load_add>;
defm atomic_swap      : binary_atomic_op<atomic_swap>;
defm atomic_load_sub  : binary_atomic_op<atomic_load_sub>;
defm atomic_load_and  : binary_atomic_op<atomic_load_and>;
defm atomic_load_clr  : binary_atomic_op<atomic_load_clr>;
defm atomic_load_or   : binary_atomic_op<atomic_load_or>;
defm atomic_load_xor  : binary_atomic_op<atomic_load_xor>;
defm atomic_load_nand : binary_atomic_op<atomic_load_nand>;
defm atomic_load_min  : binary_atomic_op<atomic_load_min>;
defm atomic_load_max  : binary_atomic_op<atomic_load_max>;
defm atomic_load_umin : binary_atomic_op<atomic_load_umin>;
defm atomic_load_umax : binary_atomic_op<atomic_load_umax>;
defm atomic_cmp_swap  : ternary_atomic_op<atomic_cmp_swap>;

/// Atomic load which zeroes the excess high bits.
def atomic_load_zext :
  PatFrag<(ops node:$ptr), (atomic_load node:$ptr)> {
  let IsAtomic = true; // FIXME: Should be IsLoad and/or IsAtomic?
  let IsZeroExtLoad = true;
}

/// Atomic load which sign extends the excess high bits.
def atomic_load_sext :
  PatFrag<(ops node:$ptr), (atomic_load node:$ptr)> {
  let IsAtomic = true; // FIXME: Should be IsLoad and/or IsAtomic?
  let IsSignExtLoad = true;
}

def atomic_load_8 :
  PatFrag<(ops node:$ptr),
          (atomic_load node:$ptr)> {
  let IsAtomic = true;
  let MemoryVT = i8;
}

def atomic_load_16 :
  PatFrag<(ops node:$ptr),
          (atomic_load node:$ptr)> {
  let IsAtomic = true;
  let MemoryVT = i16;
}

def atomic_load_32 :
  PatFrag<(ops node:$ptr),
          (atomic_load node:$ptr)> {
  let IsAtomic = true;
  let MemoryVT = i32;
}
def atomic_load_64 :
  PatFrag<(ops node:$ptr),
          (atomic_load node:$ptr)> {
  let IsAtomic = true;
  let MemoryVT = i64;
}

def atomic_load_zext_8 :
  PatFrag<(ops node:$ptr), (atomic_load_zext node:$ptr)> {
  let IsAtomic = true; // FIXME: Should be IsLoad and/or IsAtomic?
  let MemoryVT = i8;
}

def atomic_load_zext_16 :
  PatFrag<(ops node:$ptr), (atomic_load_zext node:$ptr)> {
  let IsAtomic = true; // FIXME: Should be IsLoad and/or IsAtomic?
  let MemoryVT = i16;
}

def atomic_load_sext_8 :
  PatFrag<(ops node:$ptr), (atomic_load_sext node:$ptr)> {
  let IsAtomic = true; // FIXME: Should be IsLoad and/or IsAtomic?
  let MemoryVT = i8;
}

def atomic_load_sext_16 :
  PatFrag<(ops node:$ptr), (atomic_load_sext node:$ptr)> {
  let IsAtomic = true; // FIXME: Should be IsLoad and/or IsAtomic?
  let MemoryVT = i16;
}

// Atomic load which zeroes or anyextends the high bits.
def atomic_load_az_8 : PatFrags<(ops node:$op),
                                [(atomic_load_8 node:$op),
                                 (atomic_load_zext_8 node:$op)]>;

// Atomic load which zeroes or anyextends the high bits.
def atomic_load_az_16 : PatFrags<(ops node:$op),
                                 [(atomic_load_16 node:$op),
                                  (atomic_load_zext_16 node:$op)]>;

def nonext_masked_gather :
  PatFrag<(ops node:$def, node:$pred, node:$ptr, node:$idx),
          (masked_gather node:$def, node:$pred, node:$ptr, node:$idx), [{
  return cast<MaskedGatherSDNode>(N)->getExtensionType() == ISD::NON_EXTLOAD;
}]>;

// Any extending masked gather fragments.
def ext_masked_gather_i8 :
  PatFrag<(ops node:$def, node:$pred, node:$ptr, node:$idx),
          (masked_gather node:$def, node:$pred, node:$ptr, node:$idx), [{
  auto MGN = cast<MaskedGatherSDNode>(N);
  return MGN->getExtensionType() == ISD::EXTLOAD &&
         MGN->getMemoryVT().getScalarType() == MVT::i8;
}]>;
def ext_masked_gather_i16 :
  PatFrag<(ops node:$def, node:$pred, node:$ptr, node:$idx),
          (masked_gather node:$def, node:$pred, node:$ptr, node:$idx), [{
  auto MGN = cast<MaskedGatherSDNode>(N);
  return MGN->getExtensionType() == ISD::EXTLOAD &&
         MGN->getMemoryVT().getScalarType() == MVT::i16;
}]>;
def ext_masked_gather_i32 :
  PatFrag<(ops node:$def, node:$pred, node:$ptr, node:$idx),
          (masked_gather node:$def, node:$pred, node:$ptr, node:$idx), [{
  auto MGN = cast<MaskedGatherSDNode>(N);
  return MGN->getExtensionType() == ISD::EXTLOAD &&
         MGN->getMemoryVT().getScalarType() == MVT::i32;
}]>;

// Sign extending masked gather fragments.
def sext_masked_gather_i8 :
  PatFrag<(ops node:$def, node:$pred, node:$ptr, node:$idx),
          (masked_gather node:$def, node:$pred, node:$ptr, node:$idx), [{
  auto MGN = cast<MaskedGatherSDNode>(N);
  return MGN->getExtensionType() == ISD::SEXTLOAD &&
         MGN->getMemoryVT().getScalarType() == MVT::i8;
}]>;
def sext_masked_gather_i16 :
  PatFrag<(ops node:$def, node:$pred, node:$ptr, node:$idx),
          (masked_gather node:$def, node:$pred, node:$ptr, node:$idx), [{
  auto MGN = cast<MaskedGatherSDNode>(N);
  return MGN->getExtensionType() == ISD::SEXTLOAD &&
         MGN->getMemoryVT().getScalarType() == MVT::i16;
}]>;
def sext_masked_gather_i32 :
  PatFrag<(ops node:$def, node:$pred, node:$ptr, node:$idx),
          (masked_gather node:$def, node:$pred, node:$ptr, node:$idx), [{
  auto MGN = cast<MaskedGatherSDNode>(N);
  return MGN->getExtensionType() == ISD::SEXTLOAD &&
         MGN->getMemoryVT().getScalarType() == MVT::i32;
}]>;

// Zero extending masked gather fragments.
def zext_masked_gather_i8 :
  PatFrag<(ops node:$def, node:$pred, node:$ptr, node:$idx),
          (masked_gather node:$def, node:$pred, node:$ptr, node:$idx), [{
  auto MGN = cast<MaskedGatherSDNode>(N);
  return MGN->getExtensionType() == ISD::ZEXTLOAD &&
         MGN->getMemoryVT().getScalarType() == MVT::i8;
}]>;
def zext_masked_gather_i16 :
  PatFrag<(ops node:$def, node:$pred, node:$ptr, node:$idx),
          (masked_gather node:$def, node:$pred, node:$ptr, node:$idx), [{
  auto MGN = cast<MaskedGatherSDNode>(N);
  return MGN->getExtensionType() == ISD::ZEXTLOAD &&
         MGN->getMemoryVT().getScalarType() == MVT::i16;
}]>;
def zext_masked_gather_i32 :
  PatFrag<(ops node:$def, node:$pred, node:$ptr, node:$idx),
          (masked_gather node:$def, node:$pred, node:$ptr, node:$idx), [{
  auto MGN = cast<MaskedGatherSDNode>(N);
  return MGN->getExtensionType() == ISD::ZEXTLOAD &&
         MGN->getMemoryVT().getScalarType() == MVT::i32;
}]>;

// Any/Zero extending masked gather fragments.
def azext_masked_gather_i8 :
  PatFrags<(ops node:$def, node:$pred, node:$ptr, node:$idx),
           [(ext_masked_gather_i8 node:$def, node:$pred, node:$ptr, node:$idx),
            (zext_masked_gather_i8 node:$def, node:$pred, node:$ptr, node:$idx)]>;
def azext_masked_gather_i16 :
  PatFrags<(ops node:$def, node:$pred, node:$ptr, node:$idx),
           [(ext_masked_gather_i16 node:$def, node:$pred, node:$ptr, node:$idx),
            (zext_masked_gather_i16 node:$def, node:$pred, node:$ptr, node:$idx)]>;
def azext_masked_gather_i32 :
  PatFrags<(ops node:$def, node:$pred, node:$ptr, node:$idx),
           [(ext_masked_gather_i32 node:$def, node:$pred, node:$ptr, node:$idx),
            (zext_masked_gather_i32 node:$def, node:$pred, node:$ptr, node:$idx)]>;

def nontrunc_masked_scatter :
  PatFrag<(ops node:$val, node:$pred, node:$ptr, node:$idx),
          (masked_scatter node:$val, node:$pred, node:$ptr, node:$idx), [{
  return !cast<MaskedScatterSDNode>(N)->isTruncatingStore();
}]>;

// Truncating masked scatter fragments.
def trunc_masked_scatter_i8 :
  PatFrag<(ops node:$val, node:$pred, node:$ptr, node:$idx),
          (masked_scatter node:$val, node:$pred, node:$ptr, node:$idx), [{
  auto MSN = cast<MaskedScatterSDNode>(N);
  return MSN->isTruncatingStore() &&
         MSN->getMemoryVT().getScalarType() == MVT::i8;
}]>;
def trunc_masked_scatter_i16 :
  PatFrag<(ops node:$val, node:$pred, node:$ptr, node:$idx),
          (masked_scatter node:$val, node:$pred, node:$ptr, node:$idx), [{
  auto MSN = cast<MaskedScatterSDNode>(N);
  return MSN->isTruncatingStore() &&
         MSN->getMemoryVT().getScalarType() == MVT::i16;
}]>;
def trunc_masked_scatter_i32 :
  PatFrag<(ops node:$val, node:$pred, node:$ptr, node:$idx),
          (masked_scatter node:$val, node:$pred, node:$ptr, node:$idx), [{
  auto MSN = cast<MaskedScatterSDNode>(N);
  return MSN->isTruncatingStore() &&
         MSN->getMemoryVT().getScalarType() == MVT::i32;
}]>;


def atomic_store_8 :
  PatFrag<(ops node:$val, node:$ptr),
          (atomic_store node:$val, node:$ptr)> {
  let IsAtomic = true;
  let MemoryVT = i8;
}

def atomic_store_16 :
  PatFrag<(ops node:$val, node:$ptr),
          (atomic_store node:$val, node:$ptr)> {
  let IsAtomic = true;
  let MemoryVT = i16;
}

def atomic_store_32 :
  PatFrag<(ops node:$val, node:$ptr),
          (atomic_store node:$val, node:$ptr)> {
  let IsAtomic = true;
  let MemoryVT = i32;
}

def atomic_store_64 :
  PatFrag<(ops node:$val, node:$ptr),
          (atomic_store node:$val, node:$ptr)> {
  let IsAtomic = true;
  let MemoryVT = i64;
}

//===----------------------------------------------------------------------===//
// Selection DAG Pattern Support.
//
// Patterns are what are actually matched against by the target-flavored
// instruction selection DAG.  Instructions defined by the target implicitly
// define patterns in most cases, but patterns can also be explicitly added when
// an operation is defined by a sequence of instructions (e.g. loading a large
// immediate value on RISC targets that do not support immediates as large as
// their GPRs).
//

class Pattern<dag patternToMatch, list<dag> resultInstrs> {
  dag             PatternToMatch  = patternToMatch;
  list<dag>       ResultInstrs    = resultInstrs;
  list<Predicate> Predicates      = [];  // See class Instruction in Target.td.
  int             AddedComplexity = 0;   // See class Instruction in Target.td.
  bit           GISelShouldIgnore = 0;
}

// Pat - A simple (but common) form of a pattern, which produces a simple result
// not needing a full list.
class Pat<dag pattern, dag result> : Pattern<pattern, [result]>;

//===----------------------------------------------------------------------===//
// Complex pattern definitions.
//

// Complex patterns, e.g. X86 addressing mode, requires pattern matching code
// in C++. Ty is the type of return value; NumOperands is the number of operands
// returned by the select function; SelectFunc is the name of the function used
// to pattern match the max. pattern; RootNodes are the list of possible root nodes
// of the sub-dags to match.
// e.g. X86 addressing mode - def addr : ComplexPattern<iPTR, 4, "SelectAddr", [add]>;
//
class ComplexPattern<ValueType ty, int numops, string fn,
                     list<SDNode> roots = [], list<SDNodeProperty> props = [],
                     int complexity = -1> {
  ValueType Ty = ty;
  int NumOperands = numops;
  string SelectFunc = fn;
  list<SDNode> RootNodes = roots;
  list<SDNodeProperty> Properties = props;
  int Complexity = complexity;
}

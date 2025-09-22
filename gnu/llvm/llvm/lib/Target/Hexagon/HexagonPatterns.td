//===- HexagonPatterns.td - Selection Patterns for Hexagon -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Table of contents:
//     (0) Definitions
//     (1) Immediates
//     (2) Type casts
//     (3) Extend/truncate/saturate
//     (4) Logical
//     (5) Compare
//     (6) Select
//     (7) Insert/extract
//     (8) Shift/permute
//     (9) Arithmetic/bitwise
//    (10) Bit
//    (11) PIC
//    (12) Load
//    (13) Store
//    (14) Memop
//    (15) Call
//    (16) Branch
//    (17) Misc

// Guidelines (in no particular order):
// 1. Avoid relying on pattern ordering to give preference to one pattern
//    over another, prefer using AddedComplexity instead. The reason for
//    this is to avoid unintended conseqeuences (caused by altering the
//    order) when making changes. The current order of patterns in this
//    file obviously does play some role, but none of the ordering was
//    deliberately chosen (other than to create a logical structure of
//    this file). When making changes, adding AddedComplexity to existing
//    patterns may be needed.
// 2. Maintain the logical structure of the file, try to put new patterns
//    in designated sections.
// 3. Do not use A2_combinew instruction directly, use Combinew fragment
//    instead. It uses REG_SEQUENCE, which is more amenable to optimizations.
// 4. Most selection macros are based on PatFrags. For DAGs that involve
//    SDNodes, use pf1/pf2 to convert them to PatFrags. Use common frags
//    whenever possible (see the Definitions section). When adding new
//    macro, try to make is general to enable reuse across sections.
// 5. Compound instructions (e.g. Rx+Rs*Rt) are generated under the condition
//    that the nested operation has only one use. Having it separated in case
//    of multiple uses avoids duplication of (processor) work.
// 6. The v4 vector instructions (64-bit) are treated as core instructions,
//    for example, A2_vaddh is in the "arithmetic" section with A2_add.
// 7. When adding a pattern for an instruction with a constant-extendable
//    operand, allow all possible kinds of inputs for the immediate value
//    (see AnyImm/anyimm and their variants in the Definitions section).


// --(0) Definitions -----------------------------------------------------
//

// This complex pattern exists only to create a machine instruction operand
// of type "frame index". There doesn't seem to be a way to do that directly
// in the patterns.
def AddrFI: ComplexPattern<i32, 1, "SelectAddrFI", [frameindex], []>;

// These complex patterns are not strictly necessary, since global address
// folding will happen during DAG combining. For distinguishing between GA
// and GP, pat frags with HexagonCONST32 and HexagonCONST32_GP can be used.
def AddrGA: ComplexPattern<i32, 1, "SelectAddrGA", [], []>;
def AddrGP: ComplexPattern<i32, 1, "SelectAddrGP", [], []>;
def AnyImm: ComplexPattern<i32, 1, "SelectAnyImm", [], []>;
def AnyInt: ComplexPattern<i32, 1, "SelectAnyInt", [], []>;

// Global address or a constant being a multiple of 2^n.
def AnyImm0: ComplexPattern<i32, 1, "SelectAnyImm0", [], []>;
def AnyImm1: ComplexPattern<i32, 1, "SelectAnyImm1", [], []>;
def AnyImm2: ComplexPattern<i32, 1, "SelectAnyImm2", [], []>;
def AnyImm3: ComplexPattern<i32, 1, "SelectAnyImm3", [], []>;


// Type helper frags.
def V2I1:   PatLeaf<(v2i1    PredRegs:$R)>;
def V4I1:   PatLeaf<(v4i1    PredRegs:$R)>;
def V8I1:   PatLeaf<(v8i1    PredRegs:$R)>;
def V4I8:   PatLeaf<(v4i8    IntRegs:$R)>;
def V2I16:  PatLeaf<(v2i16   IntRegs:$R)>;

def V8I8:   PatLeaf<(v8i8    DoubleRegs:$R)>;
def V4I16:  PatLeaf<(v4i16   DoubleRegs:$R)>;
def V2I32:  PatLeaf<(v2i32   DoubleRegs:$R)>;

def SDTVecLeaf:
  SDTypeProfile<1, 0, [SDTCisVec<0>]>;
def SDTVecVecIntOp:
  SDTypeProfile<1, 3, [SDTCisVec<0>, SDTCisVec<1>, SDTCisSameAs<1,2>,
                       SDTCisVT<3,i32>]>;

def HexagonPTRUE:      SDNode<"HexagonISD::PTRUE",      SDTVecLeaf>;
def HexagonPFALSE:     SDNode<"HexagonISD::PFALSE",     SDTVecLeaf>;
def HexagonVALIGN:     SDNode<"HexagonISD::VALIGN",     SDTVecVecIntOp>;
def HexagonVALIGNADDR: SDNode<"HexagonISD::VALIGNADDR", SDTIntUnaryOp>;
def HexagonMULHUS:     SDNode<"HexagonISD::MULHUS",     SDTIntBinOp>;

def SDTSaturate:
  SDTypeProfile<1, 2, [SDTCisInt<0>, SDTCisInt<1>, SDTCisVT<2, OtherVT>]>;
def HexagonSSAT: SDNode<"HexagonISD::SSAT", SDTSaturate>;
def HexagonUSAT: SDNode<"HexagonISD::USAT", SDTSaturate>;

def ptrue:  PatFrag<(ops), (HexagonPTRUE)>;
def pfalse: PatFrag<(ops), (HexagonPFALSE)>;
def pnot:   PatFrag<(ops node:$Pu), (xor node:$Pu, ptrue)>;

def valign: PatFrag<(ops node:$Vt, node:$Vs, node:$Ru),
                    (HexagonVALIGN node:$Vt, node:$Vs, node:$Ru)>;
def valignaddr: PatFrag<(ops node:$Addr), (HexagonVALIGNADDR node:$Addr)>;

def ssat: PatFrag<(ops node:$V, node:$Ty), (HexagonSSAT node:$V, node:$Ty)>;
def usat: PatFrag<(ops node:$V, node:$Ty), (HexagonUSAT node:$V, node:$Ty)>;

// Pattern fragments to extract the low and high subregisters from a
// 64-bit value.
def LoReg: OutPatFrag<(ops node:$Rs), (EXTRACT_SUBREG $Rs, isub_lo)>;
def HiReg: OutPatFrag<(ops node:$Rs), (EXTRACT_SUBREG $Rs, isub_hi)>;

def IsOrAdd: PatFrag<(ops node:$A, node:$B), (or node:$A, node:$B), [{
  return isOrEquivalentToAdd(N);
}]>;

def IsPow2_32: PatLeaf<(i32 imm), [{
  uint32_t V = N->getZExtValue();
  return isPowerOf2_32(V);
}]>;

def IsPow2_64: PatLeaf<(i64 imm), [{
  uint64_t V = N->getZExtValue();
  return isPowerOf2_64(V);
}]>;

def IsNPow2_32: PatLeaf<(i32 imm), [{
  uint32_t NV = ~N->getZExtValue();
  return isPowerOf2_32(NV);
}]>;

def IsPow2_64L: PatLeaf<(i64 imm), [{
  uint64_t V = N->getZExtValue();
  return isPowerOf2_64(V) && Log2_64(V) < 32;
}]>;

def IsPow2_64H: PatLeaf<(i64 imm), [{
  uint64_t V = N->getZExtValue();
  return isPowerOf2_64(V) && Log2_64(V) >= 32;
}]>;

def IsNPow2_64L: PatLeaf<(i64 imm), [{
  uint64_t NV = ~N->getZExtValue();
  return isPowerOf2_64(NV) && Log2_64(NV) < 32;
}]>;

def IsNPow2_64H: PatLeaf<(i64 imm), [{
  uint64_t NV = ~N->getZExtValue();
  return isPowerOf2_64(NV) && Log2_64(NV) >= 32;
}]>;

class IsULE<int Width, int Arg>: PatLeaf<(i32 imm),
  "uint64_t V = N->getZExtValue();" #
  "return isUInt<" # Width # ">(V) && V <= " # Arg # ";"
>;

class IsUGT<int Width, int Arg>: PatLeaf<(i32 imm),
  "uint64_t V = N->getZExtValue();" #
  "return isUInt<" # Width # ">(V) && V > " # Arg # ";"
>;

def SDEC1: SDNodeXForm<imm, [{
  int32_t V = N->getSExtValue();
  return CurDAG->getTargetConstant(V-1, SDLoc(N), MVT::i32);
}]>;

def UDEC1: SDNodeXForm<imm, [{
  uint32_t V = N->getZExtValue();
  assert(V >= 1);
  return CurDAG->getTargetConstant(V-1, SDLoc(N), MVT::i32);
}]>;

def UDEC32: SDNodeXForm<imm, [{
  uint32_t V = N->getZExtValue();
  assert(V >= 32);
  return CurDAG->getTargetConstant(V-32, SDLoc(N), MVT::i32);
}]>;

class Subi<int From>: SDNodeXForm<imm,
  "int32_t V = " # From # " - N->getSExtValue();" #
  "return CurDAG->getTargetConstant(V, SDLoc(N), MVT::i32);"
>;

def Log2_32: SDNodeXForm<imm, [{
  uint32_t V = N->getZExtValue();
  return CurDAG->getTargetConstant(Log2_32(V), SDLoc(N), MVT::i32);
}]>;

def Log2_64: SDNodeXForm<imm, [{
  uint64_t V = N->getZExtValue();
  return CurDAG->getTargetConstant(Log2_64(V), SDLoc(N), MVT::i32);
}]>;

def LogN2_32: SDNodeXForm<imm, [{
  uint32_t NV = ~N->getZExtValue();
  return CurDAG->getTargetConstant(Log2_32(NV), SDLoc(N), MVT::i32);
}]>;

def LogN2_64: SDNodeXForm<imm, [{
  uint64_t NV = ~N->getZExtValue();
  return CurDAG->getTargetConstant(Log2_64(NV), SDLoc(N), MVT::i32);
}]>;

def NegImm8: SDNodeXForm<imm, [{
  int8_t NV = -N->getSExtValue();
  return CurDAG->getTargetConstant(NV, SDLoc(N), MVT::i32);
}]>;

def NegImm16: SDNodeXForm<imm, [{
  int16_t NV = -N->getSExtValue();
  return CurDAG->getTargetConstant(NV, SDLoc(N), MVT::i32);
}]>;

def NegImm32: SDNodeXForm<imm, [{
  int32_t NV = -N->getSExtValue();
  return CurDAG->getTargetConstant(NV, SDLoc(N), MVT::i32);
}]>;

def SplatB: SDNodeXForm<imm, [{
  uint32_t V = N->getZExtValue();
  assert(isUInt<8>(V) || V >> 8 == 0xFFFFFF);
  V &= 0xFF;
  uint32_t S = V << 24 | V << 16 | V << 8 | V;
  return CurDAG->getTargetConstant(S, SDLoc(N), MVT::i32);
}]>;

def SplatH: SDNodeXForm<imm, [{
  uint32_t V = N->getZExtValue();
  assert(isUInt<16>(V) || V >> 16 == 0xFFFF);
  V &= 0xFFFF;
  return CurDAG->getTargetConstant(V << 16 | V, SDLoc(N), MVT::i32);
}]>;


// Helpers for type promotions/contractions.
def I1toI32:  OutPatFrag<(ops node:$Rs), (C2_muxii (i1 $Rs), 1, 0)>;
def I32toI1:  OutPatFrag<(ops node:$Rs), (i1 (C2_cmpgtui (i32 $Rs), (i32 0)))>;
def ToZext64: OutPatFrag<(ops node:$Rs), (i64 (A4_combineir 0, (i32 $Rs)))>;
def ToSext64: OutPatFrag<(ops node:$Rs), (i64 (A2_sxtw (i32 $Rs)))>;
def ToAext64: OutPatFrag<(ops node:$Rs),
  (REG_SEQUENCE DoubleRegs, (i32 (IMPLICIT_DEF)), isub_hi, (i32 $Rs), isub_lo)>;

def Combinew: OutPatFrag<(ops node:$Rs, node:$Rt),
  (REG_SEQUENCE DoubleRegs, $Rs, isub_hi, $Rt, isub_lo)>;

def addrga: PatLeaf<(i32 AddrGA:$Addr)>;
def addrgp: PatLeaf<(i32 AddrGP:$Addr)>;
def anyimm: PatLeaf<(i32 AnyImm:$Imm)>;
def anyint: PatLeaf<(i32 AnyInt:$Imm)>;

// Global address or an aligned constant.
def anyimm0: PatLeaf<(i32 AnyImm0:$Addr)>;
def anyimm1: PatLeaf<(i32 AnyImm1:$Addr)>;
def anyimm2: PatLeaf<(i32 AnyImm2:$Addr)>;
def anyimm3: PatLeaf<(i32 AnyImm3:$Addr)>;

def f32ImmPred : PatLeaf<(f32 fpimm:$F)>;
def f64ImmPred : PatLeaf<(f64 fpimm:$F)>;
def f32zero: PatLeaf<(f32 fpimm:$F), [{
  return N->isExactlyValue(APFloat::getZero(APFloat::IEEEsingle(), false));
}]>;

// This complex pattern is really only to detect various forms of
// sign-extension i32->i64. The selected value will be of type i64
// whose low word is the value being extended. The high word is
// unspecified.
def Usxtw:  ComplexPattern<i64, 1, "DetectUseSxtw", [], []>;

def Aext64: PatFrag<(ops node:$Rs), (i64 (anyext node:$Rs))>;
def Zext64: PatFrag<(ops node:$Rs), (i64 (zext node:$Rs))>;
def Sext64: PatLeaf<(i64 Usxtw:$Rs)>;

def azext: PatFrags<(ops node:$Rs), [(zext node:$Rs), (anyext node:$Rs)]>;
def asext: PatFrags<(ops node:$Rs), [(sext node:$Rs), (anyext node:$Rs)]>;

def: Pat<(IsOrAdd (i32 AddrFI:$Rs), s32_0ImmPred:$off),
         (PS_fi (i32 AddrFI:$Rs), imm:$off)>;


// Converters from unary/binary SDNode to PatFrag.
class pf1<SDNode Op> : PatFrag<(ops node:$a), (Op node:$a)>;
class pf2<SDNode Op> : PatFrag<(ops node:$a, node:$b), (Op node:$a, node:$b)>;

class Not2<PatFrag P>
  : PatFrag<(ops node:$A, node:$B), (P node:$A, (not node:$B))>;
class VNot2<PatFrag P, PatFrag Not>
  : PatFrag<(ops node:$A, node:$B), (P node:$A, (Not node:$B))>;

// If there is a constant operand that feeds the and/or instruction,
// do not generate the compound instructions.
// It is not always profitable, as some times we end up with a transfer.
// Check the below example.
// ra = #65820; rb = lsr(rb, #8); rc ^= and (rb, ra)
// Instead this is preferable.
// ra = and (#65820, lsr(ra, #8)); rb = xor(rb, ra)
class Su_ni1<PatFrag Op>
  : PatFrag<Op.Operands, !head(Op.Fragments), [{
            if (hasOneUse(N)){
              // Check if Op1 is an immediate operand.
              SDValue Op1 = N->getOperand(1);
              return !isa<ConstantSDNode>(Op1);
            }
            return false;}],
            Op.OperandTransform>;

class Su<PatFrag Op>
  : PatFrag<Op.Operands, !head(Op.Fragments), [{ return hasOneUse(N); }],
            Op.OperandTransform>;

// Main selection macros.

class OpR_R_pat<InstHexagon MI, PatFrag Op, ValueType ResVT, PatFrag RegPred>
  : Pat<(ResVT (Op RegPred:$Rs)), (MI RegPred:$Rs)>;

class OpR_RI_pat<InstHexagon MI, PatFrag Op, ValueType ResType,
                 PatFrag RegPred, PatFrag ImmPred>
  : Pat<(ResType (Op RegPred:$Rs, ImmPred:$I)),
        (MI RegPred:$Rs, imm:$I)>;

class OpR_RR_pat<InstHexagon MI, PatFrag Op, ValueType ResType,
                 PatFrag RsPred, PatFrag RtPred = RsPred>
  : Pat<(ResType (Op RsPred:$Rs, RtPred:$Rt)),
        (MI RsPred:$Rs, RtPred:$Rt)>;

class AccRRI_pat<InstHexagon MI, PatFrag AccOp, PatFrag Op,
                 PatFrag RegPred, PatFrag ImmPred>
  : Pat<(AccOp RegPred:$Rx, (Op RegPred:$Rs, ImmPred:$I)),
        (MI RegPred:$Rx, RegPred:$Rs, imm:$I)>;

class AccRRR_pat<InstHexagon MI, PatFrag AccOp, PatFrag Op,
                 PatFrag RxPred, PatFrag RsPred, PatFrag RtPred>
  : Pat<(AccOp RxPred:$Rx, (Op RsPred:$Rs, RtPred:$Rt)),
        (MI RxPred:$Rx, RsPred:$Rs, RtPred:$Rt)>;

multiclass SelMinMax_pats<PatFrag CmpOp, PatFrag Val,
                          InstHexagon InstA, InstHexagon InstB> {
  def: Pat<(select (i1 (CmpOp Val:$A, Val:$B)), Val:$A, Val:$B),
           (InstA Val:$A, Val:$B)>;
  def: Pat<(select (i1 (CmpOp Val:$A, Val:$B)), Val:$B, Val:$A),
           (InstB Val:$A, Val:$B)>;
}

multiclass MinMax_pats<InstHexagon PickT, InstHexagon PickS,
                       SDPatternOperator Sel, SDPatternOperator CmpOp,
                       ValueType CmpType, PatFrag CmpPred> {
  def: Pat<(Sel (CmpType (CmpOp CmpPred:$Vs, CmpPred:$Vt)),
                CmpPred:$Vt, CmpPred:$Vs),
           (PickT CmpPred:$Vs, CmpPred:$Vt)>;
  def: Pat<(Sel (CmpType (CmpOp CmpPred:$Vs, CmpPred:$Vt)),
                CmpPred:$Vs, CmpPred:$Vt),
           (PickS CmpPred:$Vs, CmpPred:$Vt)>;
}

// Bitcasts between same-size vector types are no-ops, except for the
// actual type change.
multiclass NopCast_pat<ValueType Ty1, ValueType Ty2, RegisterClass RC> {
  def: Pat<(Ty1 (bitconvert (Ty2 RC:$Val))), (Ty1 RC:$Val)>;
  def: Pat<(Ty2 (bitconvert (Ty1 RC:$Val))), (Ty2 RC:$Val)>;
}

// Frags for commonly used SDNodes.
def Add: pf2<add>;    def And: pf2<and>;    def Sra: pf2<sra>;
def Sub: pf2<sub>;    def Or:  pf2<or>;     def Srl: pf2<srl>;
def Mul: pf2<mul>;    def Xor: pf2<xor>;    def Shl: pf2<shl>;

def Smin: pf2<smin>;  def Smax: pf2<smax>;
def Umin: pf2<umin>;  def Umax: pf2<umax>;

def Rol: pf2<rotl>;

def Fptosi: pf1<fp_to_sint>;
def Fptoui: pf1<fp_to_uint>;
def Sitofp: pf1<sint_to_fp>;
def Uitofp: pf1<uint_to_fp>;


// --(1) Immediate -------------------------------------------------------
//

def Imm64Lo: SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(int32_t (N->getSExtValue()),
                                   SDLoc(N), MVT::i32);
}]>;
def Imm64Hi: SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(int32_t (N->getSExtValue()>>32),
                                   SDLoc(N), MVT::i32);
}]>;


def SDTHexagonCONST32
  : SDTypeProfile<1, 1, [SDTCisVT<0, i32>, SDTCisVT<1, i32>, SDTCisPtrTy<0>]>;

def HexagonJT:          SDNode<"HexagonISD::JT",          SDTIntUnaryOp>;
def HexagonCP:          SDNode<"HexagonISD::CP",          SDTIntUnaryOp>;
def HexagonCONST32:     SDNode<"HexagonISD::CONST32",     SDTHexagonCONST32>;
def HexagonCONST32_GP:  SDNode<"HexagonISD::CONST32_GP",  SDTHexagonCONST32>;

def TruncI64ToI32: SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(N->getSExtValue(), SDLoc(N), MVT::i32);
}]>;

def: Pat<(s32_0ImmPred:$s16), (A2_tfrsi imm:$s16)>;
def: Pat<(s8_0Imm64Pred:$s8), (A2_tfrpi (TruncI64ToI32 $s8))>;

def: Pat<(HexagonCONST32    tglobaltlsaddr:$A), (A2_tfrsi imm:$A)>;
def: Pat<(HexagonCONST32    bbl:$A),            (A2_tfrsi imm:$A)>;
def: Pat<(HexagonCONST32    tglobaladdr:$A),    (A2_tfrsi imm:$A)>;
def: Pat<(HexagonCONST32_GP tblockaddress:$A),  (A2_tfrsi imm:$A)>;
def: Pat<(HexagonCONST32_GP tglobaladdr:$A),    (A2_tfrsi imm:$A)>;
def: Pat<(HexagonJT         tjumptable:$A),     (A2_tfrsi imm:$A)>;
def: Pat<(HexagonCP         tconstpool:$A),     (A2_tfrsi imm:$A)>;
// The HVX load patterns also match CP directly. Make sure that if
// the selection of this opcode changes, it's updated in all places.

def: Pat<(i1 0),        (PS_false)>;
def: Pat<(i1 1),        (PS_true)>;
def: Pat<(i64 imm:$v),  (CONST64 imm:$v)>,
     Requires<[UseSmallData,NotOptTinyCore]>;
def: Pat<(i64 imm:$v),
         (Combinew (A2_tfrsi (Imm64Hi $v)), (A2_tfrsi (Imm64Lo $v)))>;

def ftoi : SDNodeXForm<fpimm, [{
  APInt I = N->getValueAPF().bitcastToAPInt();
  return CurDAG->getTargetConstant(I.getZExtValue(), SDLoc(N),
                                   MVT::getIntegerVT(I.getBitWidth()));
}]>;

def: Pat<(f32ImmPred:$f), (A2_tfrsi (ftoi $f))>;
def: Pat<(f64ImmPred:$f), (CONST64  (ftoi $f))>;

def ToI32: OutPatFrag<(ops node:$V), (A2_tfrsi $V)>;

// --(2) Type cast -------------------------------------------------------
//

def: OpR_R_pat<F2_conv_sf2df,      pf1<fpextend>,   f64, F32>;
def: OpR_R_pat<F2_conv_df2sf,      pf1<fpround>,    f32, F64>;

def: OpR_R_pat<F2_conv_w2sf,       pf1<sint_to_fp>, f32, I32>;
def: OpR_R_pat<F2_conv_d2sf,       pf1<sint_to_fp>, f32, I64>;
def: OpR_R_pat<F2_conv_w2df,       pf1<sint_to_fp>, f64, I32>;
def: OpR_R_pat<F2_conv_d2df,       pf1<sint_to_fp>, f64, I64>;

def: OpR_R_pat<F2_conv_uw2sf,      pf1<uint_to_fp>, f32, I32>;
def: OpR_R_pat<F2_conv_ud2sf,      pf1<uint_to_fp>, f32, I64>;
def: OpR_R_pat<F2_conv_uw2df,      pf1<uint_to_fp>, f64, I32>;
def: OpR_R_pat<F2_conv_ud2df,      pf1<uint_to_fp>, f64, I64>;

def: OpR_R_pat<F2_conv_sf2w_chop,  pf1<fp_to_sint>, i32, F32>;
def: OpR_R_pat<F2_conv_df2w_chop,  pf1<fp_to_sint>, i32, F64>;
def: OpR_R_pat<F2_conv_sf2d_chop,  pf1<fp_to_sint>, i64, F32>;
def: OpR_R_pat<F2_conv_df2d_chop,  pf1<fp_to_sint>, i64, F64>;

def: OpR_R_pat<F2_conv_sf2uw_chop, pf1<fp_to_uint>, i32, F32>;
def: OpR_R_pat<F2_conv_df2uw_chop, pf1<fp_to_uint>, i32, F64>;
def: OpR_R_pat<F2_conv_sf2ud_chop, pf1<fp_to_uint>, i64, F32>;
def: OpR_R_pat<F2_conv_df2ud_chop, pf1<fp_to_uint>, i64, F64>;

// Bitcast is different than [fp|sint|uint]_to_[sint|uint|fp].
def: Pat<(i32 (bitconvert F32:$v)), (I32:$v)>;
def: Pat<(f32 (bitconvert I32:$v)), (F32:$v)>;
def: Pat<(i64 (bitconvert F64:$v)), (I64:$v)>;
def: Pat<(f64 (bitconvert I64:$v)), (F64:$v)>;

// Bit convert 32- and 64-bit types.
// All of these are bitcastable to one another: i32, v2i16, v4i8.
defm: NopCast_pat<i32,   v2i16, IntRegs>;
defm: NopCast_pat<i32,    v4i8, IntRegs>;
defm: NopCast_pat<v2i16,  v4i8, IntRegs>;
// All of these are bitcastable to one another: i64, v2i32, v4i16, v8i8.
defm: NopCast_pat<i64,   v2i32, DoubleRegs>;
defm: NopCast_pat<i64,   v4i16, DoubleRegs>;
defm: NopCast_pat<i64,    v8i8, DoubleRegs>;
defm: NopCast_pat<v2i32, v4i16, DoubleRegs>;
defm: NopCast_pat<v2i32,  v8i8, DoubleRegs>;
defm: NopCast_pat<v4i16,  v8i8, DoubleRegs>;


// --(3) Extend/truncate/saturate ----------------------------------------
//

def: Pat<(sext_inreg I32:$Rs, i8),  (A2_sxtb I32:$Rs)>;
def: Pat<(sext_inreg I32:$Rs, i16), (A2_sxth I32:$Rs)>;
def: Pat<(sext_inreg I64:$Rs, i32), (A2_sxtw (LoReg $Rs))>;
def: Pat<(sext_inreg I64:$Rs, i16), (A2_sxtw (A2_sxth (LoReg $Rs)))>;
def: Pat<(sext_inreg I64:$Rs, i8),  (A2_sxtw (A2_sxtb (LoReg $Rs)))>;

def: Pat<(i64 (sext I32:$Rs)), (A2_sxtw I32:$Rs)>;
def: Pat<(Zext64 I32:$Rs),     (ToZext64 $Rs)>;
def: Pat<(Aext64 I32:$Rs),     (ToZext64 $Rs)>;

def: Pat<(i32 (trunc I64:$Rs)), (LoReg $Rs)>;
def: Pat<(i1 (trunc I32:$Rs)),  (S2_tstbit_i I32:$Rs, 0)>;
def: Pat<(i1 (trunc I64:$Rs)),  (S2_tstbit_i (LoReg $Rs), 0)>;

let AddedComplexity = 20 in {
  def: Pat<(and I32:$Rs, 255),   (A2_zxtb I32:$Rs)>;
  def: Pat<(and I32:$Rs, 65535), (A2_zxth I32:$Rs)>;
}

// Extensions from i1 or vectors of i1.
def: Pat<(i32 (azext I1:$Pu)), (C2_muxii I1:$Pu, 1, 0)>;
def: Pat<(i64 (azext I1:$Pu)), (ToZext64 (C2_muxii I1:$Pu, 1, 0))>;
def: Pat<(i32  (sext I1:$Pu)), (C2_muxii I1:$Pu, -1, 0)>;
def: Pat<(i64  (sext I1:$Pu)), (Combinew (C2_muxii PredRegs:$Pu, -1, 0),
                                         (C2_muxii PredRegs:$Pu, -1, 0))>;

def: Pat<(v2i16 (sext V2I1:$Pu)), (S2_vtrunehb (C2_mask V2I1:$Pu))>;
def: Pat<(v2i32 (sext V2I1:$Pu)), (C2_mask V2I1:$Pu)>;
def: Pat<(v4i8  (sext V4I1:$Pu)), (S2_vtrunehb (C2_mask V4I1:$Pu))>;
def: Pat<(v4i16 (sext V4I1:$Pu)), (C2_mask V4I1:$Pu)>;
def: Pat<(v8i8  (sext V8I1:$Pu)), (C2_mask V8I1:$Pu)>;

def Vsplatpi: OutPatFrag<(ops node:$V),
                         (Combinew (A2_tfrsi $V), (A2_tfrsi $V))>;

def: Pat<(v2i16 (azext V2I1:$Pu)),
         (A2_andir (S2_vtrunehb (C2_mask V2I1:$Pu)), (i32 0x00010001))>;
def: Pat<(v2i32 (azext V2I1:$Pu)),
         (A2_andp (C2_mask V2I1:$Pu), (A2_combineii (i32 1), (i32 1)))>;
def: Pat<(v4i8 (azext V4I1:$Pu)),
         (A2_andir (S2_vtrunehb (C2_mask V4I1:$Pu)), (i32 0x01010101))>;
def: Pat<(v4i16 (azext V4I1:$Pu)),
         (A2_andp (C2_mask V4I1:$Pu), (Vsplatpi (i32 0x00010001)))>;
def: Pat<(v8i8 (azext V8I1:$Pu)),
         (A2_andp (C2_mask V8I1:$Pu), (Vsplatpi (i32 0x01010101)))>;

def: Pat<(v4i16 (azext  V4I8:$Rs)),  (S2_vzxtbh V4I8:$Rs)>;
def: Pat<(v2i32 (azext  V2I16:$Rs)), (S2_vzxthw V2I16:$Rs)>;
def: Pat<(v4i16 (sext   V4I8:$Rs)),  (S2_vsxtbh V4I8:$Rs)>;
def: Pat<(v2i32 (sext   V2I16:$Rs)), (S2_vsxthw V2I16:$Rs)>;

def: Pat<(v2i32 (sext_inreg V2I32:$Rs, v2i8)),
         (Combinew (A2_sxtb (HiReg $Rs)), (A2_sxtb (LoReg $Rs)))>;

def: Pat<(v2i32 (sext_inreg V2I32:$Rs, v2i16)),
         (Combinew (A2_sxth (HiReg $Rs)), (A2_sxth (LoReg $Rs)))>;

// Truncate: from vector B copy all 'E'ven 'B'yte elements:
// A[0] = B[0];  A[1] = B[2];  A[2] = B[4];  A[3] = B[6];
def: Pat<(v4i8 (trunc V4I16:$Rs)),
         (S2_vtrunehb V4I16:$Rs)>;

// Truncate: from vector B copy all 'O'dd 'B'yte elements:
// A[0] = B[1];  A[1] = B[3];  A[2] = B[5];  A[3] = B[7];
// S2_vtrunohb

// Truncate: from vectors B and C copy all 'E'ven 'H'alf-word elements:
// A[0] = B[0];  A[1] = B[2];  A[2] = C[0];  A[3] = C[2];
// S2_vtruneh

def: Pat<(v2i16 (trunc V2I32:$Rs)),
         (A2_combine_ll (HiReg $Rs), (LoReg $Rs))>;

// Truncate to vNi1
def: Pat<(v2i1 (trunc V2I32:$Rs)),
         (A4_vcmpweqi (A2_andp V2I32:$Rs, (A2_combineii (i32 1), (i32 1))),
                      (i32 1))>;
def: Pat<(v4i1 (trunc V4I16:$Rs)),
         (A4_vcmpheqi (Combinew (A2_andir (HiReg $Rs), (i32 0x00010001)),
                                (A2_andir (LoReg $Rs), (i32 0x00010001))),
                      (i32 1))>;
def: Pat<(v8i1 (trunc V8I8:$Rs)),
         (A4_vcmpbeqi (Combinew (A2_andir (HiReg $Rs), (i32 0x01010101)),
                                (A2_andir (LoReg $Rs), (i32 0x01010101))),
                      (i32 1))>;


// Saturation:
// Note: saturation assumes the same signed-ness for the input and the
// output.
def: Pat<(i32 (ssat I32:$Rs, i8)),  (A2_satb  I32:$Rs)>;
def: Pat<(i32 (ssat I32:$Rs, i16)), (A2_sath  I32:$Rs)>;
def: Pat<(i32 (ssat I64:$Rs, i32)), (A2_sat   I64:$Rs)>;
def: Pat<(i32 (usat I32:$Rs, i8)),  (A2_satub I32:$Rs)>;
def: Pat<(i32 (usat I32:$Rs, i16)), (A2_satuh I32:$Rs)>;
def: Pat<(i32 (usat I64:$Rs, i32)),
         (C2_mux (C2_cmpeqi (HiReg $Rs), (i32 0)), (LoReg $Rs), (i32 -1))>;

def: Pat<(v4i8  (ssat V4I16:$Rs, v4i8)),  (S2_vsathb  V4I16:$Rs)>;
def: Pat<(v2i16 (ssat V2I32:$Rs, v2i16)), (S2_vsatwh  V2I32:$Rs)>;
def: Pat<(v4i8  (usat V4I16:$Rs, v4i8)),  (S2_vsathub V4I16:$Rs)>;
def: Pat<(v2i16 (usat V2I32:$Rs, v2i16)), (S2_vsatwuh V2I32:$Rs)>;


// --(4) Logical ---------------------------------------------------------
//

def: Pat<(not I1:$Ps),      (C2_not I1:$Ps)>;
def: Pat<(pnot V2I1:$Ps),   (C2_not V2I1:$Ps)>;
def: Pat<(pnot V4I1:$Ps),   (C2_not V4I1:$Ps)>;
def: Pat<(pnot V8I1:$Ps),   (C2_not V8I1:$Ps)>;
def: Pat<(add I1:$Ps, -1),  (C2_not I1:$Ps)>;

def: OpR_RR_pat<C2_and,         And, i1, I1>;
def: OpR_RR_pat<C2_or,           Or, i1, I1>;
def: OpR_RR_pat<C2_xor,         Xor, i1, I1>;
def: OpR_RR_pat<C2_andn,  Not2<And>, i1, I1>;
def: OpR_RR_pat<C2_orn,    Not2<Or>, i1, I1>;

def: AccRRR_pat<C4_and_and,   And,       Su<And>, I1, I1, I1>;
def: AccRRR_pat<C4_and_or,    And,       Su< Or>, I1, I1, I1>;
def: AccRRR_pat<C4_or_and,     Or,       Su<And>, I1, I1, I1>;
def: AccRRR_pat<C4_or_or,      Or,       Su< Or>, I1, I1, I1>;
def: AccRRR_pat<C4_and_andn,  And, Su<Not2<And>>, I1, I1, I1>;
def: AccRRR_pat<C4_and_orn,   And, Su<Not2< Or>>, I1, I1, I1>;
def: AccRRR_pat<C4_or_andn,    Or, Su<Not2<And>>, I1, I1, I1>;
def: AccRRR_pat<C4_or_orn,     Or, Su<Not2< Or>>, I1, I1, I1>;

multiclass BoolvOpR_RR_pat<InstHexagon MI, PatFrag VOp> {
  def: OpR_RR_pat<MI, VOp, v2i1, V2I1>;
  def: OpR_RR_pat<MI, VOp, v4i1, V4I1>;
  def: OpR_RR_pat<MI, VOp, v8i1, V8I1>;
}

multiclass BoolvAccRRR_pat<InstHexagon MI, PatFrag AccOp, PatFrag VOp> {
  def: AccRRR_pat<MI, AccOp, VOp, V2I1, V2I1, V2I1>;
  def: AccRRR_pat<MI, AccOp, VOp, V4I1, V4I1, V4I1>;
  def: AccRRR_pat<MI, AccOp, VOp, V8I1, V8I1, V8I1>;
}

defm: BoolvOpR_RR_pat<C2_and,                    And>;
defm: BoolvOpR_RR_pat<C2_or,                      Or>;
defm: BoolvOpR_RR_pat<C2_xor,                    Xor>;
defm: BoolvOpR_RR_pat<C2_andn,      VNot2<And, pnot>>;
defm: BoolvOpR_RR_pat<C2_orn,       VNot2< Or, pnot>>;

// op(Ps, op(Pt, Pu))
defm: BoolvAccRRR_pat<C4_and_and,   And, Su<And>>;
defm: BoolvAccRRR_pat<C4_and_or,    And, Su<Or>>;
defm: BoolvAccRRR_pat<C4_or_and,    Or,  Su<And>>;
defm: BoolvAccRRR_pat<C4_or_or,     Or,  Su<Or>>;

// op(Ps, op(Pt, !Pu))
defm: BoolvAccRRR_pat<C4_and_andn,  And, Su<VNot2<And, pnot>>>;
defm: BoolvAccRRR_pat<C4_and_orn,   And, Su<VNot2< Or, pnot>>>;
defm: BoolvAccRRR_pat<C4_or_andn,   Or,  Su<VNot2<And, pnot>>>;
defm: BoolvAccRRR_pat<C4_or_orn,    Or,  Su<VNot2< Or, pnot>>>;


// --(5) Compare ---------------------------------------------------------
//

// Avoid negated comparisons, i.e. those of form "Pd = !cmp(...)".
// These cannot form compounds (e.g. J4_cmpeqi_tp0_jump_nt).

def: OpR_RI_pat<C2_cmpeqi,    seteq,          i1, I32,  anyimm>;
def: OpR_RI_pat<C2_cmpgti,    setgt,          i1, I32,  anyimm>;
def: OpR_RI_pat<C2_cmpgtui,   setugt,         i1, I32,  anyimm>;

def: Pat<(i1 (setge I32:$Rs, s32_0ImmPred:$s10)),
         (C2_cmpgti I32:$Rs, (SDEC1 imm:$s10))>;
def: Pat<(i1 (setuge I32:$Rs, u32_0ImmPred:$u9)),
         (C2_cmpgtui I32:$Rs, (UDEC1 imm:$u9))>;

def: Pat<(i1 (setlt I32:$Rs, s32_0ImmPred:$s10)),
         (C2_not (C2_cmpgti I32:$Rs, (SDEC1 imm:$s10)))>;
def: Pat<(i1 (setult I32:$Rs, u32_0ImmPred:$u9)),
         (C2_not (C2_cmpgtui I32:$Rs, (UDEC1 imm:$u9)))>;

// Patfrag to convert the usual comparison patfrags (e.g. setlt) to ones
// that reverse the order of the operands.
class RevCmp<PatFrag F>
  : PatFrag<(ops node:$rhs, node:$lhs), !head(F.Fragments), F.PredicateCode,
            F.OperandTransform>;

def: OpR_RR_pat<C2_cmpeq,     seteq,          i1,   I32>;
def: OpR_RR_pat<C2_cmpgt,     setgt,          i1,   I32>;
def: OpR_RR_pat<C2_cmpgtu,    setugt,         i1,   I32>;
def: OpR_RR_pat<C2_cmpgt,     RevCmp<setlt>,  i1,   I32>;
def: OpR_RR_pat<C2_cmpgtu,    RevCmp<setult>, i1,   I32>;
def: OpR_RR_pat<C2_cmpeqp,    seteq,          i1,   I64>;
def: OpR_RR_pat<C2_cmpgtp,    setgt,          i1,   I64>;
def: OpR_RR_pat<C2_cmpgtup,   setugt,         i1,   I64>;
def: OpR_RR_pat<C2_cmpgtp,    RevCmp<setlt>,  i1,   I64>;
def: OpR_RR_pat<C2_cmpgtup,   RevCmp<setult>, i1,   I64>;
def: OpR_RR_pat<A2_vcmpbeq,   seteq,          i1,   V8I8>;
def: OpR_RR_pat<A2_vcmpbeq,   seteq,          v8i1, V8I8>;
def: OpR_RR_pat<A4_vcmpbgt,   RevCmp<setlt>,  i1,   V8I8>;
def: OpR_RR_pat<A4_vcmpbgt,   RevCmp<setlt>,  v8i1, V8I8>;
def: OpR_RR_pat<A4_vcmpbgt,   setgt,          i1,   V8I8>;
def: OpR_RR_pat<A4_vcmpbgt,   setgt,          v8i1, V8I8>;
def: OpR_RR_pat<A2_vcmpbgtu,  RevCmp<setult>, i1,   V8I8>;
def: OpR_RR_pat<A2_vcmpbgtu,  RevCmp<setult>, v8i1, V8I8>;
def: OpR_RR_pat<A2_vcmpbgtu,  setugt,         i1,   V8I8>;
def: OpR_RR_pat<A2_vcmpbgtu,  setugt,         v8i1, V8I8>;
def: OpR_RR_pat<A2_vcmpheq,   seteq,          i1,   V4I16>;
def: OpR_RR_pat<A2_vcmpheq,   seteq,          v4i1, V4I16>;
def: OpR_RR_pat<A2_vcmphgt,   RevCmp<setlt>,  i1,   V4I16>;
def: OpR_RR_pat<A2_vcmphgt,   RevCmp<setlt>,  v4i1, V4I16>;
def: OpR_RR_pat<A2_vcmphgt,   setgt,          i1,   V4I16>;
def: OpR_RR_pat<A2_vcmphgt,   setgt,          v4i1, V4I16>;
def: OpR_RR_pat<A2_vcmphgtu,  RevCmp<setult>, i1,   V4I16>;
def: OpR_RR_pat<A2_vcmphgtu,  RevCmp<setult>, v4i1, V4I16>;
def: OpR_RR_pat<A2_vcmphgtu,  setugt,         i1,   V4I16>;
def: OpR_RR_pat<A2_vcmphgtu,  setugt,         v4i1, V4I16>;
def: OpR_RR_pat<A2_vcmpweq,   seteq,          i1,   V2I32>;
def: OpR_RR_pat<A2_vcmpweq,   seteq,          v2i1, V2I32>;
def: OpR_RR_pat<A2_vcmpwgt,   RevCmp<setlt>,  i1,   V2I32>;
def: OpR_RR_pat<A2_vcmpwgt,   RevCmp<setlt>,  v2i1, V2I32>;
def: OpR_RR_pat<A2_vcmpwgt,   setgt,          i1,   V2I32>;
def: OpR_RR_pat<A2_vcmpwgt,   setgt,          v2i1, V2I32>;
def: OpR_RR_pat<A2_vcmpwgtu,  RevCmp<setult>, i1,   V2I32>;
def: OpR_RR_pat<A2_vcmpwgtu,  RevCmp<setult>, v2i1, V2I32>;
def: OpR_RR_pat<A2_vcmpwgtu,  setugt,         i1,   V2I32>;
def: OpR_RR_pat<A2_vcmpwgtu,  setugt,         v2i1, V2I32>;

def: OpR_RR_pat<F2_sfcmpeq,   seteq,          i1, F32>;
def: OpR_RR_pat<F2_sfcmpgt,   setgt,          i1, F32>;
def: OpR_RR_pat<F2_sfcmpge,   setge,          i1, F32>;
def: OpR_RR_pat<F2_sfcmpeq,   setoeq,         i1, F32>;
def: OpR_RR_pat<F2_sfcmpgt,   setogt,         i1, F32>;
def: OpR_RR_pat<F2_sfcmpge,   setoge,         i1, F32>;
def: OpR_RR_pat<F2_sfcmpgt,   RevCmp<setolt>, i1, F32>;
def: OpR_RR_pat<F2_sfcmpge,   RevCmp<setole>, i1, F32>;
def: OpR_RR_pat<F2_sfcmpgt,   RevCmp<setlt>,  i1, F32>;
def: OpR_RR_pat<F2_sfcmpge,   RevCmp<setle>,  i1, F32>;
def: OpR_RR_pat<F2_sfcmpuo,   setuo,          i1, F32>;

def: OpR_RR_pat<F2_dfcmpeq,   seteq,          i1, F64>;
def: OpR_RR_pat<F2_dfcmpgt,   setgt,          i1, F64>;
def: OpR_RR_pat<F2_dfcmpge,   setge,          i1, F64>;
def: OpR_RR_pat<F2_dfcmpeq,   setoeq,         i1, F64>;
def: OpR_RR_pat<F2_dfcmpgt,   setogt,         i1, F64>;
def: OpR_RR_pat<F2_dfcmpge,   setoge,         i1, F64>;
def: OpR_RR_pat<F2_dfcmpgt,   RevCmp<setolt>, i1, F64>;
def: OpR_RR_pat<F2_dfcmpge,   RevCmp<setole>, i1, F64>;
def: OpR_RR_pat<F2_dfcmpgt,   RevCmp<setlt>,  i1, F64>;
def: OpR_RR_pat<F2_dfcmpge,   RevCmp<setle>,  i1, F64>;
def: OpR_RR_pat<F2_dfcmpuo,   setuo,          i1, F64>;

// Avoid C4_cmpneqi, C4_cmpltei, C4_cmplteui, since they cannot form compounds.

def: Pat<(i1 (setne I32:$Rs, anyimm:$u5)),
         (C2_not (C2_cmpeqi I32:$Rs, imm:$u5))>;
def: Pat<(i1 (setle I32:$Rs, anyimm:$u5)),
         (C2_not (C2_cmpgti I32:$Rs, imm:$u5))>;
def: Pat<(i1 (setule I32:$Rs, anyimm:$u5)),
         (C2_not (C2_cmpgtui I32:$Rs, imm:$u5))>;

class OpmR_RR_pat<PatFrag Output, PatFrag Op, ValueType ResType,
                  PatFrag RsPred, PatFrag RtPred = RsPred>
  : Pat<(ResType (Op RsPred:$Rs, RtPred:$Rt)),
        (Output RsPred:$Rs, RtPred:$Rt)>;

class Outn<InstHexagon MI>
  : OutPatFrag<(ops node:$Rs, node:$Rt),
               (C2_not (MI $Rs, $Rt))>;

def: OpmR_RR_pat<Outn<C2_cmpeq>,    setne,          i1,   I32>;
def: OpmR_RR_pat<Outn<C2_cmpgt>,    setle,          i1,   I32>;
def: OpmR_RR_pat<Outn<C2_cmpgtu>,   setule,         i1,   I32>;
def: OpmR_RR_pat<Outn<C2_cmpgt>,    RevCmp<setge>,  i1,   I32>;
def: OpmR_RR_pat<Outn<C2_cmpgtu>,   RevCmp<setuge>, i1,   I32>;
def: OpmR_RR_pat<Outn<C2_cmpeqp>,   setne,          i1,   I64>;
def: OpmR_RR_pat<Outn<C2_cmpgtp>,   setle,          i1,   I64>;
def: OpmR_RR_pat<Outn<C2_cmpgtup>,  setule,         i1,   I64>;
def: OpmR_RR_pat<Outn<C2_cmpgtp>,   RevCmp<setge>,  i1,   I64>;
def: OpmR_RR_pat<Outn<C2_cmpgtup>,  RevCmp<setuge>, i1,   I64>;
def: OpmR_RR_pat<Outn<A2_vcmpbeq>,  setne,          v8i1, V8I8>;
def: OpmR_RR_pat<Outn<A4_vcmpbgt>,  setle,          v8i1, V8I8>;
def: OpmR_RR_pat<Outn<A2_vcmpbgtu>, setule,         v8i1, V8I8>;
def: OpmR_RR_pat<Outn<A4_vcmpbgt>,  RevCmp<setge>,  v8i1, V8I8>;
def: OpmR_RR_pat<Outn<A2_vcmpbgtu>, RevCmp<setuge>, v8i1, V8I8>;
def: OpmR_RR_pat<Outn<A2_vcmpheq>,  setne,          v4i1, V4I16>;
def: OpmR_RR_pat<Outn<A2_vcmphgt>,  setle,          v4i1, V4I16>;
def: OpmR_RR_pat<Outn<A2_vcmphgtu>, setule,         v4i1, V4I16>;
def: OpmR_RR_pat<Outn<A2_vcmphgt>,  RevCmp<setge>,  v4i1, V4I16>;
def: OpmR_RR_pat<Outn<A2_vcmphgtu>, RevCmp<setuge>, v4i1, V4I16>;
def: OpmR_RR_pat<Outn<A2_vcmpweq>,  setne,          v2i1, V2I32>;
def: OpmR_RR_pat<Outn<A2_vcmpwgt>,  setle,          v2i1, V2I32>;
def: OpmR_RR_pat<Outn<A2_vcmpwgtu>, setule,         v2i1, V2I32>;
def: OpmR_RR_pat<Outn<A2_vcmpwgt>,  RevCmp<setge>,  v2i1, V2I32>;
def: OpmR_RR_pat<Outn<A2_vcmpwgtu>, RevCmp<setuge>, v2i1, V2I32>;

let AddedComplexity = 100 in {
  def: Pat<(i1 (seteq (and (xor I32:$Rs, I32:$Rt), 255), 0)),
           (A4_cmpbeq IntRegs:$Rs, IntRegs:$Rt)>;
  def: Pat<(i1 (setne (and (xor I32:$Rs, I32:$Rt), 255), 0)),
           (C2_not (A4_cmpbeq IntRegs:$Rs, IntRegs:$Rt))>;
  def: Pat<(i1 (seteq (and (xor I32:$Rs, I32:$Rt), 65535), 0)),
           (A4_cmpheq IntRegs:$Rs, IntRegs:$Rt)>;
  def: Pat<(i1 (setne (and (xor I32:$Rs, I32:$Rt), 65535), 0)),
           (C2_not (A4_cmpheq IntRegs:$Rs, IntRegs:$Rt))>;
}

// PatFrag for AsserZext which takes the original type as a parameter.
def SDTAssertZext: SDTypeProfile<1, 2, [SDTCisInt<0>, SDTCisSameAs<0,1>]>;
def AssertZextSD: SDNode<"ISD::AssertZext", SDTAssertZext>;
class AssertZext<ValueType T>: PatFrag<(ops node:$A), (AssertZextSD $A, T)>;

multiclass Cmpb_pat<InstHexagon MI, PatFrag Op, PatFrag AssertExt,
                      PatLeaf ImmPred, int Mask> {
  def: Pat<(i1 (Op (and I32:$Rs, Mask), ImmPred:$I)),
           (MI I32:$Rs, imm:$I)>;
  def: Pat<(i1 (Op (AssertExt I32:$Rs), ImmPred:$I)),
           (MI I32:$Rs, imm:$I)>;
}

multiclass CmpbN_pat<InstHexagon MI, PatFrag Op, PatFrag AssertExt,
                     PatLeaf ImmPred, int Mask> {
  def: Pat<(i1 (Op (and I32:$Rs, Mask), ImmPred:$I)),
           (C2_not (MI I32:$Rs, imm:$I))>;
  def: Pat<(i1 (Op (AssertExt I32:$Rs), ImmPred:$I)),
           (C2_not (MI I32:$Rs, imm:$I))>;
}

multiclass CmpbND_pat<InstHexagon MI, PatFrag Op, PatFrag AssertExt,
                      PatLeaf ImmPred, int Mask> {
  def: Pat<(i1 (Op (and I32:$Rs, Mask), ImmPred:$I)),
           (C2_not (MI I32:$Rs, (UDEC1 imm:$I)))>;
  def: Pat<(i1 (Op (AssertExt I32:$Rs), ImmPred:$I)),
           (C2_not (MI I32:$Rs, (UDEC1 imm:$I)))>;
}

let AddedComplexity = 200 in {
  defm: Cmpb_pat  <A4_cmpbeqi,  seteq,  AssertZext<i8>,  IsUGT<8,31>,  255>;
  defm: CmpbN_pat <A4_cmpbeqi,  setne,  AssertZext<i8>,  IsUGT<8,31>,  255>;
  defm: Cmpb_pat  <A4_cmpbgtui, setugt, AssertZext<i8>,  IsUGT<32,31>, 255>;
  defm: CmpbN_pat <A4_cmpbgtui, setule, AssertZext<i8>,  IsUGT<32,31>, 255>;
  defm: Cmpb_pat  <A4_cmphgtui, setugt, AssertZext<i16>, IsUGT<32,31>, 65535>;
  defm: CmpbN_pat <A4_cmphgtui, setule, AssertZext<i16>, IsUGT<32,31>, 65535>;
  defm: CmpbND_pat<A4_cmpbgtui, setult, AssertZext<i8>,  IsUGT<32,32>, 255>;
  defm: CmpbND_pat<A4_cmphgtui, setult, AssertZext<i16>, IsUGT<32,32>, 65535>;
}

def: Pat<(i32 (zext (i1 (seteq I32:$Rs, I32:$Rt)))),
         (A4_rcmpeq I32:$Rs, I32:$Rt)>;
def: Pat<(i32 (zext (i1 (setne I32:$Rs, I32:$Rt)))),
         (A4_rcmpneq I32:$Rs, I32:$Rt)>;
def: Pat<(i32 (zext (i1 (seteq I32:$Rs, anyimm:$s8)))),
         (A4_rcmpeqi I32:$Rs, imm:$s8)>;
def: Pat<(i32 (zext (i1 (setne I32:$Rs, anyimm:$s8)))),
         (A4_rcmpneqi I32:$Rs, imm:$s8)>;

def: Pat<(i1 (seteq I1:$Ps, (i1 -1))), (I1:$Ps)>;
def: Pat<(i1 (setne I1:$Ps, (i1 -1))), (C2_not I1:$Ps)>;
def: Pat<(i1 (seteq I1:$Ps, I1:$Pt)),  (C2_not (C2_xor I1:$Ps, I1:$Pt))>;
def: Pat<(i1 (setne I1:$Ps, I1:$Pt)),  (C2_xor I1:$Ps, I1:$Pt)>;

multiclass BoolE_pat<PatFrag OpPred, ValueType ResTy> {
  def: Pat<(ResTy (seteq OpPred:$Ps, OpPred:$Pt)), (C2_not (C2_xor $Ps, $Pt))>;
  def: Pat<(ResTy (setne OpPred:$Ps, OpPred:$Pt)), (C2_xor $Ps, $Pt)>;
}

defm: BoolE_pat<I1,   i1>;
defm: BoolE_pat<V2I1, v2i1>;
defm: BoolE_pat<V4I1, v4i1>;
defm: BoolE_pat<V8I1, v8i1>;

multiclass BoolL_pat<PatFrag OpPred, ValueType ResTy> {
  // Signed "true" == -1
  def: Pat<(ResTy (setlt  OpPred:$Ps, OpPred:$Pt)), (C2_andn $Ps, $Pt)>;
  def: Pat<(ResTy (setle  OpPred:$Ps, OpPred:$Pt)), (C2_orn  $Ps, $Pt)>;
  def: Pat<(ResTy (setult OpPred:$Ps, OpPred:$Pt)), (C2_andn $Pt, $Ps)>;
  def: Pat<(ResTy (setule OpPred:$Ps, OpPred:$Pt)), (C2_orn  $Pt, $Ps)>;
}

defm: BoolL_pat<I1,   i1>;
defm: BoolL_pat<V2I1, v2i1>;
defm: BoolL_pat<V4I1, v4i1>;
defm: BoolL_pat<V8I1, v8i1>;

// Floating-point comparisons with checks for ordered/unordered status.

class T3<InstHexagon MI1, InstHexagon MI2, InstHexagon MI3>
  : OutPatFrag<(ops node:$Rs, node:$Rt),
               (MI1 (MI2 $Rs, $Rt), (MI3 $Rs, $Rt))>;

class Cmpuf<InstHexagon MI>:  T3<C2_or,  F2_sfcmpuo, MI>;
class Cmpud<InstHexagon MI>:  T3<C2_or,  F2_dfcmpuo, MI>;

class Cmpufn<InstHexagon MI>: T3<C2_orn, F2_sfcmpuo, MI>;
class Cmpudn<InstHexagon MI>: T3<C2_orn, F2_dfcmpuo, MI>;

def: OpmR_RR_pat<Cmpuf<F2_sfcmpeq>,  setueq,         i1, F32>;
def: OpmR_RR_pat<Cmpuf<F2_sfcmpge>,  setuge,         i1, F32>;
def: OpmR_RR_pat<Cmpuf<F2_sfcmpgt>,  setugt,         i1, F32>;
def: OpmR_RR_pat<Cmpuf<F2_sfcmpge>,  RevCmp<setule>, i1, F32>;
def: OpmR_RR_pat<Cmpuf<F2_sfcmpgt>,  RevCmp<setult>, i1, F32>;
def: OpmR_RR_pat<Cmpufn<F2_sfcmpeq>, setune,         i1, F32>;

def: OpmR_RR_pat<Cmpud<F2_dfcmpeq>,  setueq,         i1, F64>;
def: OpmR_RR_pat<Cmpud<F2_dfcmpge>,  setuge,         i1, F64>;
def: OpmR_RR_pat<Cmpud<F2_dfcmpgt>,  setugt,         i1, F64>;
def: OpmR_RR_pat<Cmpud<F2_dfcmpge>,  RevCmp<setule>, i1, F64>;
def: OpmR_RR_pat<Cmpud<F2_dfcmpgt>,  RevCmp<setult>, i1, F64>;
def: OpmR_RR_pat<Cmpudn<F2_dfcmpeq>, setune,         i1, F64>;

def: OpmR_RR_pat<Outn<F2_sfcmpeq>, setone, i1, F32>;
def: OpmR_RR_pat<Outn<F2_sfcmpeq>, setne,  i1, F32>;

def: OpmR_RR_pat<Outn<F2_dfcmpeq>, setone, i1, F64>;
def: OpmR_RR_pat<Outn<F2_dfcmpeq>, setne,  i1, F64>;

def: OpmR_RR_pat<Outn<F2_sfcmpuo>, seto,   i1, F32>;
def: OpmR_RR_pat<Outn<F2_dfcmpuo>, seto,   i1, F64>;


// --(6) Select ----------------------------------------------------------
//

def: Pat<(select I1:$Pu, I32:$Rs, I32:$Rt),
         (C2_mux I1:$Pu, I32:$Rs, I32:$Rt)>;
def: Pat<(select I1:$Pu, v4i8:$Rs, v4i8:$Rt),
         (C2_mux I1:$Pu, v4i8:$Rs, v4i8:$Rt)>;
def: Pat<(select I1:$Pu, v2i16:$Rs, v2i16:$Rt),
         (C2_mux I1:$Pu, v2i16:$Rs, v2i16:$Rt)>;
def: Pat<(select I1:$Pu, anyimm:$s8, I32:$Rs),
         (C2_muxri I1:$Pu, imm:$s8, I32:$Rs)>;
def: Pat<(select I1:$Pu, I32:$Rs, anyimm:$s8),
         (C2_muxir I1:$Pu, I32:$Rs, imm:$s8)>;
def: Pat<(select I1:$Pu, anyimm:$s8, s8_0ImmPred:$S8),
         (C2_muxii I1:$Pu, imm:$s8, imm:$S8)>;

def: Pat<(select (not I1:$Pu), I32:$Rs, I32:$Rt),
         (C2_mux I1:$Pu, I32:$Rt, I32:$Rs)>;
def: Pat<(select (not I1:$Pu), s8_0ImmPred:$S8, anyimm:$s8),
         (C2_muxii I1:$Pu, imm:$s8, imm:$S8)>;
def: Pat<(select (not I1:$Pu), anyimm:$s8, I32:$Rs),
         (C2_muxir I1:$Pu, I32:$Rs, imm:$s8)>;
def: Pat<(select (not I1:$Pu), I32:$Rs, anyimm:$s8),
         (C2_muxri I1:$Pu, imm:$s8, I32:$Rs)>;

// Map from a 64-bit select to an emulated 64-bit mux.
// Hexagon does not support 64-bit MUXes; so emulate with combines.
def: Pat<(select I1:$Pu, I64:$Rs, I64:$Rt),
         (Combinew (C2_mux I1:$Pu, (HiReg $Rs), (HiReg $Rt)),
                   (C2_mux I1:$Pu, (LoReg $Rs), (LoReg $Rt)))>;

def: Pat<(select I1:$Pu, v2i32:$Rs, v2i32:$Rt),
         (Combinew (C2_mux I1:$Pu, (HiReg $Rs), (HiReg $Rt)),
                   (C2_mux I1:$Pu, (LoReg $Rs), (LoReg $Rt)))>;

def: Pat<(select I1:$Pu, F32:$Rs, f32ImmPred:$I),
         (C2_muxir I1:$Pu, F32:$Rs, (ftoi $I))>;
def: Pat<(select I1:$Pu, f32ImmPred:$I, F32:$Rt),
         (C2_muxri I1:$Pu, (ftoi $I), F32:$Rt)>;
def: Pat<(select I1:$Pu, F32:$Rs, F32:$Rt),
         (C2_mux I1:$Pu, F32:$Rs, F32:$Rt)>;
def: Pat<(select I1:$Pu, F64:$Rs, F64:$Rt),
         (Combinew (C2_mux I1:$Pu, (HiReg $Rs), (HiReg $Rt)),
                   (C2_mux I1:$Pu, (LoReg $Rs), (LoReg $Rt)))>;

def: Pat<(select (i1 (setult F32:$Ra, F32:$Rb)), F32:$Rs, F32:$Rt),
         (C2_mux (F2_sfcmpgt F32:$Rb, F32:$Ra), F32:$Rs, F32:$Rt)>;
def: Pat<(select (i1 (setult F64:$Ra, F64:$Rb)), F64:$Rs, F64:$Rt),
         (C2_vmux (F2_dfcmpgt F64:$Rb, F64:$Ra), F64:$Rs, F64:$Rt)>;

def: Pat<(select (not I1:$Pu), f32ImmPred:$I, F32:$Rs),
         (C2_muxir I1:$Pu, F32:$Rs, (ftoi $I))>;
def: Pat<(select (not I1:$Pu), F32:$Rt, f32ImmPred:$I),
         (C2_muxri I1:$Pu, (ftoi $I), F32:$Rt)>;

def: Pat<(vselect V8I1:$Pu, V8I8:$Rs, V8I8:$Rt),
         (C2_vmux V8I1:$Pu, V8I8:$Rs, V8I8:$Rt)>;
def: Pat<(vselect V4I1:$Pu, V4I16:$Rs, V4I16:$Rt),
         (C2_vmux V4I1:$Pu, V4I16:$Rs, V4I16:$Rt)>;
def: Pat<(vselect V2I1:$Pu, V2I32:$Rs, V2I32:$Rt),
         (C2_vmux V2I1:$Pu, V2I32:$Rs, V2I32:$Rt)>;

def: Pat<(vselect (pnot V8I1:$Pu), V8I8:$Rs, V8I8:$Rt),
         (C2_vmux V8I1:$Pu, V8I8:$Rt, V8I8:$Rs)>;
def: Pat<(vselect (pnot V4I1:$Pu), V4I16:$Rs, V4I16:$Rt),
         (C2_vmux V4I1:$Pu, V4I16:$Rt, V4I16:$Rs)>;
def: Pat<(vselect (pnot V2I1:$Pu), V2I32:$Rs, V2I32:$Rt),
         (C2_vmux V2I1:$Pu, V2I32:$Rt, V2I32:$Rs)>;


// From LegalizeDAG.cpp: (Pu ? Pv : Pw) <=> (Pu & Pv) | (!Pu & Pw).
def: Pat<(select I1:$Pu, I1:$Ps, I1:$Pt),
         (C4_or_andn (C2_and $Ps, $Pu), $Pt, $Pu)>;

def: Pat<(vselect V2I1:$Pu, V2I1:$Ps, V2I1:$Pt),
         (C4_or_andn (C2_and $Ps, $Pu), $Pt, $Pu)>;
def: Pat<(vselect V4I1:$Pu, V4I1:$Ps, V4I1:$Pt),
         (C4_or_andn (C2_and $Ps, $Pu), $Pt, $Pu)>;
def: Pat<(vselect V8I1:$Pu, V8I1:$Ps, V8I1:$Pt),
         (C4_or_andn (C2_and $Ps, $Pu), $Pt, $Pu)>;

def: Pat<(select I1:$Pu, V2I1:$Ps, V2I1:$Pt),
         (C2_tfrrp (C2_mux $Pu, (C2_tfrpr $Ps), (C2_tfrpr $Pt)))>;
def: Pat<(select I1:$Pu, V4I1:$Ps, V4I1:$Pt),
         (C2_tfrrp (C2_mux $Pu, (C2_tfrpr $Ps), (C2_tfrpr $Pt)))>;
def: Pat<(select I1:$Pu, V8I1:$Ps, V8I1:$Pt),
         (C2_tfrrp (C2_mux $Pu, (C2_tfrpr $Ps), (C2_tfrpr $Pt)))>;

def IsPosHalf : PatLeaf<(i32 IntRegs:$a), [{
  return isPositiveHalfWord(N);
}]>;

multiclass SelMinMax16_pats<PatFrag CmpOp, InstHexagon InstA,
                            InstHexagon InstB> {
  def: Pat<(sext_inreg (select (i1 (CmpOp IsPosHalf:$Rs, IsPosHalf:$Rt)),
                               IsPosHalf:$Rs, IsPosHalf:$Rt), i16),
           (InstA IntRegs:$Rs, IntRegs:$Rt)>;
  def: Pat<(sext_inreg (select (i1 (CmpOp IsPosHalf:$Rs, IsPosHalf:$Rt)),
                               IsPosHalf:$Rt, IsPosHalf:$Rs), i16),
           (InstB IntRegs:$Rs, IntRegs:$Rt)>;
}

let AddedComplexity = 200 in {
  defm: SelMinMax16_pats<setge,  A2_max,  A2_min>;
  defm: SelMinMax16_pats<setgt,  A2_max,  A2_min>;
  defm: SelMinMax16_pats<setle,  A2_min,  A2_max>;
  defm: SelMinMax16_pats<setlt,  A2_min,  A2_max>;
  defm: SelMinMax16_pats<setuge, A2_maxu, A2_minu>;
  defm: SelMinMax16_pats<setugt, A2_maxu, A2_minu>;
  defm: SelMinMax16_pats<setule, A2_minu, A2_maxu>;
  defm: SelMinMax16_pats<setult, A2_minu, A2_maxu>;
}

def: OpR_RR_pat<A2_min,   Smin, i32, I32, I32>;
def: OpR_RR_pat<A2_max,   Smax, i32, I32, I32>;
def: OpR_RR_pat<A2_minu,  Umin, i32, I32, I32>;
def: OpR_RR_pat<A2_maxu,  Umax, i32, I32, I32>;
def: OpR_RR_pat<A2_minp,  Smin, i64, I64, I64>;
def: OpR_RR_pat<A2_maxp,  Smax, i64, I64, I64>;
def: OpR_RR_pat<A2_minup, Umin, i64, I64, I64>;
def: OpR_RR_pat<A2_maxup, Umax, i64, I64, I64>;

let AddedComplexity = 100 in {
  defm: MinMax_pats<F2_sfmin, F2_sfmax, select, setogt, i1, F32>;
  defm: MinMax_pats<F2_sfmin, F2_sfmax, select, setoge, i1, F32>;
  defm: MinMax_pats<F2_sfmax, F2_sfmin, select, setolt, i1, F32>;
  defm: MinMax_pats<F2_sfmax, F2_sfmin, select, setole, i1, F32>;
}

let AddedComplexity = 100, Predicates = [HasV67] in {
  defm: MinMax_pats<F2_dfmin, F2_dfmax, select, setogt, i1, F64>;
  defm: MinMax_pats<F2_dfmin, F2_dfmax, select, setoge, i1, F64>;
  defm: MinMax_pats<F2_dfmax, F2_dfmin, select, setolt, i1, F64>;
  defm: MinMax_pats<F2_dfmax, F2_dfmin, select, setole, i1, F64>;
}

def: OpR_RR_pat<A2_vminb,  Smin, v8i8,  V8I8>;
def: OpR_RR_pat<A2_vmaxb,  Smax, v8i8,  V8I8>;
def: OpR_RR_pat<A2_vminub, Umin, v8i8,  V8I8>;
def: OpR_RR_pat<A2_vmaxub, Umax, v8i8,  V8I8>;

def: OpR_RR_pat<A2_vminh,  Smin, v4i16, V4I16>;
def: OpR_RR_pat<A2_vmaxh,  Smax, v4i16, V4I16>;
def: OpR_RR_pat<A2_vminuh, Umin, v4i16, V4I16>;
def: OpR_RR_pat<A2_vmaxuh, Umax, v4i16, V4I16>;

def: OpR_RR_pat<A2_vminw,  Smin, v2i32, V2I32>;
def: OpR_RR_pat<A2_vmaxw,  Smax, v2i32, V2I32>;
def: OpR_RR_pat<A2_vminuw, Umin, v2i32, V2I32>;
def: OpR_RR_pat<A2_vmaxuw, Umax, v2i32, V2I32>;

// --(7) Insert/extract --------------------------------------------------
//

def SDTHexagonINSERT:
  SDTypeProfile<1, 4, [SDTCisSameAs<0, 1>, SDTCisSameAs<0, 2>,
                       SDTCisInt<0>, SDTCisVT<3, i32>, SDTCisVT<4, i32>]>;
def HexagonINSERT:    SDNode<"HexagonISD::INSERT",   SDTHexagonINSERT>;

let AddedComplexity = 10 in {
  def: Pat<(HexagonINSERT I32:$Rs, I32:$Rt, u5_0ImmPred:$u1, u5_0ImmPred:$u2),
           (S2_insert I32:$Rs, I32:$Rt, imm:$u1, imm:$u2)>;
  def: Pat<(HexagonINSERT I64:$Rs, I64:$Rt, u6_0ImmPred:$u1, u6_0ImmPred:$u2),
           (S2_insertp I64:$Rs, I64:$Rt, imm:$u1, imm:$u2)>;
}
def: Pat<(HexagonINSERT I32:$Rs, I32:$Rt, I32:$Width, I32:$Off),
         (S2_insert_rp I32:$Rs, I32:$Rt, (Combinew $Width, $Off))>;
def: Pat<(HexagonINSERT I64:$Rs, I64:$Rt, I32:$Width, I32:$Off),
         (S2_insertp_rp I64:$Rs, I64:$Rt, (Combinew $Width, $Off))>;

def SDTHexagonEXTRACTU
  : SDTypeProfile<1, 3, [SDTCisSameAs<0, 1>, SDTCisInt<0>, SDTCisInt<1>,
                  SDTCisVT<2, i32>, SDTCisVT<3, i32>]>;
def HexagonEXTRACTU:   SDNode<"HexagonISD::EXTRACTU",   SDTHexagonEXTRACTU>;

let AddedComplexity = 10 in {
  def: Pat<(HexagonEXTRACTU I32:$Rs, u5_0ImmPred:$u5, u5_0ImmPred:$U5),
           (S2_extractu I32:$Rs, imm:$u5, imm:$U5)>;
  def: Pat<(HexagonEXTRACTU I64:$Rs, u6_0ImmPred:$u6, u6_0ImmPred:$U6),
           (S2_extractup I64:$Rs, imm:$u6, imm:$U6)>;
}
def: Pat<(HexagonEXTRACTU I32:$Rs, I32:$Width, I32:$Off),
         (S2_extractu_rp I32:$Rs, (Combinew $Width, $Off))>;
def: Pat<(HexagonEXTRACTU I64:$Rs, I32:$Width, I32:$Off),
         (S2_extractup_rp I64:$Rs, (Combinew $Width, $Off))>;

def: Pat<(v4i8  (splat_vector anyint:$V)), (ToI32 (SplatB $V))>;
def: Pat<(v2i16 (splat_vector anyint:$V)), (ToI32 (SplatH $V))>;
def: Pat<(v8i8  (splat_vector anyint:$V)),
          (Combinew (ToI32 (SplatB $V)), (ToI32 (SplatB $V)))>;
def: Pat<(v4i16 (splat_vector anyint:$V)),
          (Combinew (ToI32 (SplatH $V)), (ToI32 (SplatH $V)))>;
let AddedComplexity = 10 in
def: Pat<(v2i32 (splat_vector s8_0ImmPred:$s8)),
         (A2_combineii imm:$s8, imm:$s8)>;
def: Pat<(v2i32 (splat_vector anyimm:$V)), (Combinew (ToI32 $V), (ToI32 $V))>;

def: Pat<(v4i8  (splat_vector I32:$Rs)), (S2_vsplatrb I32:$Rs)>;
def: Pat<(v2i16 (splat_vector I32:$Rs)), (LoReg (S2_vsplatrh I32:$Rs))>;
def: Pat<(v4i16 (splat_vector I32:$Rs)), (S2_vsplatrh I32:$Rs)>;
def: Pat<(v2i32 (splat_vector I32:$Rs)), (Combinew I32:$Rs, I32:$Rs)>;

let AddedComplexity = 10 in
def: Pat<(v8i8 (splat_vector I32:$Rs)), (S6_vsplatrbp I32:$Rs)>,
     Requires<[HasV62]>;
def: Pat<(v8i8 (splat_vector I32:$Rs)),
         (Combinew (S2_vsplatrb I32:$Rs), (S2_vsplatrb I32:$Rs))>;

let AddedComplexity = 10 in {
  def: Pat<(sext_inreg (HexagonEXTRACTU I32:$Rs,  8, u5_0ImmPred:$U5),  i8),
           (S4_extract  I32:$Rs,  8, imm:$U5)>;
  def: Pat<(sext_inreg (HexagonEXTRACTU I32:$Rs, 16, u5_0ImmPred:$U5), i16),
           (S4_extract  I32:$Rs, 16, imm:$U5)>;
  def: Pat<(sext_inreg (HexagonEXTRACTU I64:$Rs,  8, u6_0ImmPred:$U6),  i8),
           (S4_extractp I64:$Rs,  8, imm:$U6)>;
  def: Pat<(sext_inreg (HexagonEXTRACTU I64:$Rs, 16, u6_0ImmPred:$U6), i16),
           (S4_extractp I64:$Rs, 16, imm:$U6)>;
  def: Pat<(sext_inreg (HexagonEXTRACTU I64:$Rs, 32, u6_0ImmPred:$U6), i32),
           (S4_extractp I64:$Rs, 32, imm:$U6)>;
}

def: Pat<(sext_inreg (HexagonEXTRACTU I32:$Rs,  8, I32:$Off),  i8),
         (S4_extract_rp  I32:$Rs, (Combinew (ToI32 8), I32:$Off))>;
def: Pat<(sext_inreg (HexagonEXTRACTU I32:$Rs, 16, I32:$Off), i16),
         (S4_extract_rp  I32:$Rs, (Combinew (ToI32 16), I32:$Off))>;
def: Pat<(sext_inreg (HexagonEXTRACTU I64:$Rs,  8, I32:$Off),  i8),
         (S4_extractp_rp I64:$Rs, (Combinew (ToI32 8), I32:$Off))>;
def: Pat<(sext_inreg (HexagonEXTRACTU I64:$Rs, 16, I32:$Off), i16),
         (S4_extractp_rp I64:$Rs, (Combinew (ToI32 16), I32:$Off))>;
def: Pat<(sext_inreg (HexagonEXTRACTU I64:$Rs, 32, I32:$Off), i32),
         (S4_extractp_rp I64:$Rs, (Combinew (ToI32 32), I32:$Off))>;


// --(8) Shift/permute ---------------------------------------------------
//

def SDTHexagonI64I32I32: SDTypeProfile<1, 2,
  [SDTCisVT<0, i64>, SDTCisVT<1, i32>, SDTCisSameAs<1, 2>]>;

def HexagonCOMBINE:  SDNode<"HexagonISD::COMBINE",  SDTHexagonI64I32I32>;

def: Pat<(HexagonCOMBINE I32:$Rs, I32:$Rt), (Combinew $Rs, $Rt)>;

// The complexity of the combines involving immediates should be greater
// than the complexity of the combine with two registers.
let AddedComplexity = 50 in {
  def: Pat<(HexagonCOMBINE I32:$Rs, anyimm:$s8),
           (A4_combineri IntRegs:$Rs, imm:$s8)>;
  def: Pat<(HexagonCOMBINE anyimm:$s8, I32:$Rs),
           (A4_combineir imm:$s8, IntRegs:$Rs)>;
}

// The complexity of the combine with two immediates should be greater than
// the complexity of a combine involving a register.
let AddedComplexity = 75 in {
  def: Pat<(HexagonCOMBINE s8_0ImmPred:$s8, anyimm:$u6),
           (A4_combineii imm:$s8, imm:$u6)>;
  def: Pat<(HexagonCOMBINE anyimm:$s8, s8_0ImmPred:$S8),
           (A2_combineii imm:$s8, imm:$S8)>;
}

def: Pat<(bswap I32:$Rs),  (A2_swiz I32:$Rs)>;
def: Pat<(bswap I64:$Rss), (Combinew (A2_swiz (LoReg $Rss)),
                                     (A2_swiz (HiReg $Rss)))>;

def: Pat<(bswap V2I16:$Rs), (A2_combine_lh (A2_swiz $Rs), (A2_swiz $Rs))>;
def: Pat<(bswap V2I32:$Rs), (Combinew (A2_swiz (HiReg $Rs)),
                                      (A2_swiz (LoReg $Rs)))>;
def: Pat<(bswap V4I16:$Rs), (A2_orp (S2_lsr_i_vh $Rs, 8),
                                    (S2_asl_i_vh $Rs, 8))>;

def: Pat<(shl s6_0ImmPred:$s6, I32:$Rt),  (S4_lsli imm:$s6, I32:$Rt)>;
def: Pat<(shl I32:$Rs, (i32 16)),         (A2_aslh I32:$Rs)>;
def: Pat<(sra I32:$Rs, (i32 16)),         (A2_asrh I32:$Rs)>;

def: OpR_RI_pat<S2_asr_i_r,  Sra, i32,   I32,   u5_0ImmPred>;
def: OpR_RI_pat<S2_lsr_i_r,  Srl, i32,   I32,   u5_0ImmPred>;
def: OpR_RI_pat<S2_asl_i_r,  Shl, i32,   I32,   u5_0ImmPred>;
def: OpR_RI_pat<S2_asr_i_p,  Sra, i64,   I64,   u6_0ImmPred>;
def: OpR_RI_pat<S2_lsr_i_p,  Srl, i64,   I64,   u6_0ImmPred>;
def: OpR_RI_pat<S2_asl_i_p,  Shl, i64,   I64,   u6_0ImmPred>;
def: OpR_RI_pat<S2_asr_i_vh, Sra, v4i16, V4I16, u4_0ImmPred>;
def: OpR_RI_pat<S2_lsr_i_vh, Srl, v4i16, V4I16, u4_0ImmPred>;
def: OpR_RI_pat<S2_asl_i_vh, Shl, v4i16, V4I16, u4_0ImmPred>;
def: OpR_RI_pat<S2_asr_i_vh, Sra, v2i32, V2I32, u5_0ImmPred>;
def: OpR_RI_pat<S2_lsr_i_vh, Srl, v2i32, V2I32, u5_0ImmPred>;
def: OpR_RI_pat<S2_asl_i_vh, Shl, v2i32, V2I32, u5_0ImmPred>;

def: OpR_RR_pat<S2_asr_r_r, Sra, i32, I32, I32>;
def: OpR_RR_pat<S2_lsr_r_r, Srl, i32, I32, I32>;
def: OpR_RR_pat<S2_asl_r_r, Shl, i32, I32, I32>;
def: OpR_RR_pat<S2_asr_r_p, Sra, i64, I64, I32>;
def: OpR_RR_pat<S2_lsr_r_p, Srl, i64, I64, I32>;
def: OpR_RR_pat<S2_asl_r_p, Shl, i64, I64, I32>;

// Funnel shifts.
def IsMul8_U3: PatLeaf<(i32 imm), [{
  uint64_t V = N->getZExtValue();
  return V % 8 == 0 && isUInt<3>(V / 8);
}]>;

def Divu8: SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(N->getZExtValue() / 8, SDLoc(N), MVT::i32);
}]>;

// Funnel shift-left.
def FShl32i: OutPatFrag<(ops node:$Rs, node:$Rt, node:$S),
  (HiReg (S2_asl_i_p (Combinew $Rs, $Rt), $S))>;
def FShl32r: OutPatFrag<(ops node:$Rs, node:$Rt, node:$Ru),
  (HiReg (S2_asl_r_p (Combinew $Rs, $Rt), $Ru))>;

def FShl64i: OutPatFrag<(ops node:$Rs, node:$Rt, node:$S),
  (S2_lsr_i_p_or (S2_asl_i_p $Rs, $S),  $Rt, (Subi<64> $S))>;
def FShl64r: OutPatFrag<(ops node:$Rs, node:$Rt, node:$Ru),
  (S2_lsr_r_p_or (S2_asl_r_p $Rs, $Ru), $Rt, (A2_subri 64, $Ru))>;

// Combined SDNodeXForm: (Divu8 (Subi<64> $S))
def Divu64_8: SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant((64 - N->getSExtValue()) / 8,
                                   SDLoc(N), MVT::i32);
}]>;

// Special cases:
let AddedComplexity = 100 in {
  def: Pat<(fshl I32:$Rs, I32:$Rt, (i32 16)),
           (A2_combine_lh I32:$Rs, I32:$Rt)>;
  def: Pat<(fshl I64:$Rs, I64:$Rt, IsMul8_U3:$S),
           (S2_valignib I64:$Rs, I64:$Rt, (Divu64_8 $S))>;
}

let Predicates = [HasV60], AddedComplexity = 50 in {
  def: OpR_RI_pat<S6_rol_i_r, Rol, i32, I32, u5_0ImmPred>;
  def: OpR_RI_pat<S6_rol_i_p, Rol, i64, I64, u6_0ImmPred>;
}
let AddedComplexity = 30 in {
  def: Pat<(rotl I32:$Rs, u5_0ImmPred:$S),          (FShl32i $Rs, $Rs, imm:$S)>;
  def: Pat<(rotl I64:$Rs, u6_0ImmPred:$S),          (FShl64i $Rs, $Rs, imm:$S)>;
  def: Pat<(fshl I32:$Rs, I32:$Rt, u5_0ImmPred:$S), (FShl32i $Rs, $Rt, imm:$S)>;
  def: Pat<(fshl I64:$Rs, I64:$Rt, u6_0ImmPred:$S), (FShl64i $Rs, $Rt, imm:$S)>;
}
def: Pat<(rotl I32:$Rs, I32:$Rt),           (FShl32r $Rs, $Rs, $Rt)>;
def: Pat<(rotl I64:$Rs, I32:$Rt),           (FShl64r $Rs, $Rs, $Rt)>;
def: Pat<(fshl I32:$Rs, I32:$Rt, I32:$Ru),  (FShl32r $Rs, $Rt, $Ru)>;
def: Pat<(fshl I64:$Rs, I64:$Rt, I32:$Ru),  (FShl64r $Rs, $Rt, $Ru)>;

// Funnel shift-right.
def FShr32i: OutPatFrag<(ops node:$Rs, node:$Rt, node:$S),
  (LoReg (S2_lsr_i_p (Combinew $Rs, $Rt), $S))>;
def FShr32r: OutPatFrag<(ops node:$Rs, node:$Rt, node:$Ru),
  (LoReg (S2_lsr_r_p (Combinew $Rs, $Rt), $Ru))>;

def FShr64i: OutPatFrag<(ops node:$Rs, node:$Rt, node:$S),
  (S2_asl_i_p_or (S2_lsr_i_p $Rt, $S),  $Rs, (Subi<64> $S))>;
def FShr64r: OutPatFrag<(ops node:$Rs, node:$Rt, node:$Ru),
  (S2_asl_r_p_or (S2_lsr_r_p $Rt, $Ru), $Rs, (A2_subri 64, $Ru))>;

// Special cases:
let AddedComplexity = 100 in {
  def: Pat<(fshr I32:$Rs, I32:$Rt, (i32 16)),
           (A2_combine_lh I32:$Rs, I32:$Rt)>;
  def: Pat<(fshr I64:$Rs, I64:$Rt, IsMul8_U3:$S),
           (S2_valignib I64:$Rs, I64:$Rt, (Divu8 $S))>;
}

let Predicates = [HasV60], AddedComplexity = 50 in {
  def: Pat<(rotr I32:$Rs, u5_0ImmPred:$S), (S6_rol_i_r I32:$Rs, (Subi<32> $S))>;
  def: Pat<(rotr I64:$Rs, u6_0ImmPred:$S), (S6_rol_i_p I64:$Rs, (Subi<64> $S))>;
}
let AddedComplexity = 30 in {
  def: Pat<(rotr I32:$Rs, u5_0ImmPred:$S),          (FShr32i $Rs, $Rs, imm:$S)>;
  def: Pat<(rotr I64:$Rs, u6_0ImmPred:$S),          (FShr64i $Rs, $Rs, imm:$S)>;
  def: Pat<(fshr I32:$Rs, I32:$Rt, u5_0ImmPred:$S), (FShr32i $Rs, $Rt, imm:$S)>;
  def: Pat<(fshr I64:$Rs, I64:$Rt, u6_0ImmPred:$S), (FShr64i $Rs, $Rt, imm:$S)>;
}
def: Pat<(rotr I32:$Rs, I32:$Rt),           (FShr32r $Rs, $Rs, $Rt)>;
def: Pat<(rotr I64:$Rs, I32:$Rt),           (FShr64r $Rs, $Rs, $Rt)>;
def: Pat<(fshr I32:$Rs, I32:$Rt, I32:$Ru),  (FShr32r $Rs, $Rt, $Ru)>;
def: Pat<(fshr I64:$Rs, I64:$Rt, I32:$Ru),  (FShr64r $Rs, $Rt, $Ru)>;


def: Pat<(sra (add (sra I32:$Rs, u5_0ImmPred:$u5), 1), (i32 1)),
         (S2_asr_i_r_rnd I32:$Rs, imm:$u5)>;
def: Pat<(sra (add (sra I64:$Rs, u6_0ImmPred:$u6), 1), (i32 1)),
         (S2_asr_i_p_rnd I64:$Rs, imm:$u6)>;

// Prefer S2_addasl_rrri over S2_asl_i_r_acc.
let AddedComplexity = 120 in
def: Pat<(add I32:$Rt, (shl I32:$Rs, u3_0ImmPred:$u3)),
         (S2_addasl_rrri IntRegs:$Rt, IntRegs:$Rs, imm:$u3)>;

let AddedComplexity = 100 in {
  def: AccRRI_pat<S2_asr_i_r_acc,   Add, Su<Sra>, I32, u5_0ImmPred>;
  def: AccRRI_pat<S2_asr_i_r_nac,   Sub, Su<Sra>, I32, u5_0ImmPred>;
  def: AccRRI_pat<S2_asr_i_r_and,   And, Su<Sra>, I32, u5_0ImmPred>;
  def: AccRRI_pat<S2_asr_i_r_or,    Or,  Su<Sra>, I32, u5_0ImmPred>;

  def: AccRRI_pat<S2_asr_i_p_acc,   Add, Su<Sra>, I64, u6_0ImmPred>;
  def: AccRRI_pat<S2_asr_i_p_nac,   Sub, Su<Sra>, I64, u6_0ImmPred>;
  def: AccRRI_pat<S2_asr_i_p_and,   And, Su<Sra>, I64, u6_0ImmPred>;
  def: AccRRI_pat<S2_asr_i_p_or,    Or,  Su<Sra>, I64, u6_0ImmPred>;

  def: AccRRI_pat<S2_lsr_i_r_acc,   Add, Su<Srl>, I32, u5_0ImmPred>;
  def: AccRRI_pat<S2_lsr_i_r_nac,   Sub, Su<Srl>, I32, u5_0ImmPred>;
  def: AccRRI_pat<S2_lsr_i_r_and,   And, Su<Srl>, I32, u5_0ImmPred>;
  def: AccRRI_pat<S2_lsr_i_r_or,    Or,  Su<Srl>, I32, u5_0ImmPred>;
  def: AccRRI_pat<S2_lsr_i_r_xacc,  Xor, Su<Srl>, I32, u5_0ImmPred>;

  def: AccRRI_pat<S2_lsr_i_p_acc,   Add, Su<Srl>, I64, u6_0ImmPred>;
  def: AccRRI_pat<S2_lsr_i_p_nac,   Sub, Su<Srl>, I64, u6_0ImmPred>;
  def: AccRRI_pat<S2_lsr_i_p_and,   And, Su<Srl>, I64, u6_0ImmPred>;
  def: AccRRI_pat<S2_lsr_i_p_or,    Or,  Su<Srl>, I64, u6_0ImmPred>;
  def: AccRRI_pat<S2_lsr_i_p_xacc,  Xor, Su<Srl>, I64, u6_0ImmPred>;

  def: AccRRI_pat<S2_asl_i_r_acc,   Add, Su<Shl>, I32, u5_0ImmPred>;
  def: AccRRI_pat<S2_asl_i_r_nac,   Sub, Su<Shl>, I32, u5_0ImmPred>;
  def: AccRRI_pat<S2_asl_i_r_and,   And, Su<Shl>, I32, u5_0ImmPred>;
  def: AccRRI_pat<S2_asl_i_r_or,    Or,  Su<Shl>, I32, u5_0ImmPred>;
  def: AccRRI_pat<S2_asl_i_r_xacc,  Xor, Su<Shl>, I32, u5_0ImmPred>;

  def: AccRRI_pat<S2_asl_i_p_acc,   Add, Su<Shl>, I64, u6_0ImmPred>;
  def: AccRRI_pat<S2_asl_i_p_nac,   Sub, Su<Shl>, I64, u6_0ImmPred>;
  def: AccRRI_pat<S2_asl_i_p_and,   And, Su<Shl>, I64, u6_0ImmPred>;
  def: AccRRI_pat<S2_asl_i_p_or,    Or,  Su<Shl>, I64, u6_0ImmPred>;
  def: AccRRI_pat<S2_asl_i_p_xacc,  Xor, Su<Shl>, I64, u6_0ImmPred>;

  let Predicates = [HasV60] in {
    def: AccRRI_pat<S6_rol_i_r_acc,   Add, Su<Rol>, I32, u5_0ImmPred>;
    def: AccRRI_pat<S6_rol_i_r_nac,   Sub, Su<Rol>, I32, u5_0ImmPred>;
    def: AccRRI_pat<S6_rol_i_r_and,   And, Su<Rol>, I32, u5_0ImmPred>;
    def: AccRRI_pat<S6_rol_i_r_or,    Or,  Su<Rol>, I32, u5_0ImmPred>;
    def: AccRRI_pat<S6_rol_i_r_xacc,  Xor, Su<Rol>, I32, u5_0ImmPred>;

    def: AccRRI_pat<S6_rol_i_p_acc,   Add, Su<Rol>, I64, u6_0ImmPred>;
    def: AccRRI_pat<S6_rol_i_p_nac,   Sub, Su<Rol>, I64, u6_0ImmPred>;
    def: AccRRI_pat<S6_rol_i_p_and,   And, Su<Rol>, I64, u6_0ImmPred>;
    def: AccRRI_pat<S6_rol_i_p_or,    Or,  Su<Rol>, I64, u6_0ImmPred>;
    def: AccRRI_pat<S6_rol_i_p_xacc,  Xor, Su<Rol>, I64, u6_0ImmPred>;
  }
}

let AddedComplexity = 100 in {
  def: AccRRR_pat<S2_asr_r_r_acc,   Add, Su<Sra>, I32, I32, I32>;
  def: AccRRR_pat<S2_asr_r_r_nac,   Sub, Su<Sra>, I32, I32, I32>;
  def: AccRRR_pat<S2_asr_r_r_and,   And, Su<Sra>, I32, I32, I32>;
  def: AccRRR_pat<S2_asr_r_r_or,    Or,  Su<Sra>, I32, I32, I32>;

  def: AccRRR_pat<S2_asr_r_p_acc,   Add, Su<Sra>, I64, I64, I32>;
  def: AccRRR_pat<S2_asr_r_p_nac,   Sub, Su<Sra>, I64, I64, I32>;
  def: AccRRR_pat<S2_asr_r_p_and,   And, Su<Sra>, I64, I64, I32>;
  def: AccRRR_pat<S2_asr_r_p_or,    Or,  Su<Sra>, I64, I64, I32>;
  def: AccRRR_pat<S2_asr_r_p_xor,   Xor, Su<Sra>, I64, I64, I32>;

  def: AccRRR_pat<S2_lsr_r_r_acc,   Add, Su<Srl>, I32, I32, I32>;
  def: AccRRR_pat<S2_lsr_r_r_nac,   Sub, Su<Srl>, I32, I32, I32>;
  def: AccRRR_pat<S2_lsr_r_r_and,   And, Su<Srl>, I32, I32, I32>;
  def: AccRRR_pat<S2_lsr_r_r_or,    Or,  Su<Srl>, I32, I32, I32>;

  def: AccRRR_pat<S2_lsr_r_p_acc,   Add, Su<Srl>, I64, I64, I32>;
  def: AccRRR_pat<S2_lsr_r_p_nac,   Sub, Su<Srl>, I64, I64, I32>;
  def: AccRRR_pat<S2_lsr_r_p_and,   And, Su<Srl>, I64, I64, I32>;
  def: AccRRR_pat<S2_lsr_r_p_or,    Or,  Su<Srl>, I64, I64, I32>;
  def: AccRRR_pat<S2_lsr_r_p_xor,   Xor, Su<Srl>, I64, I64, I32>;

  def: AccRRR_pat<S2_asl_r_r_acc,   Add, Su<Shl>, I32, I32, I32>;
  def: AccRRR_pat<S2_asl_r_r_nac,   Sub, Su<Shl>, I32, I32, I32>;
  def: AccRRR_pat<S2_asl_r_r_and,   And, Su<Shl>, I32, I32, I32>;
  def: AccRRR_pat<S2_asl_r_r_or,    Or,  Su<Shl>, I32, I32, I32>;

  def: AccRRR_pat<S2_asl_r_p_acc,   Add, Su<Shl>, I64, I64, I32>;
  def: AccRRR_pat<S2_asl_r_p_nac,   Sub, Su<Shl>, I64, I64, I32>;
  def: AccRRR_pat<S2_asl_r_p_and,   And, Su<Shl>, I64, I64, I32>;
  def: AccRRR_pat<S2_asl_r_p_or,    Or,  Su<Shl>, I64, I64, I32>;
  def: AccRRR_pat<S2_asl_r_p_xor,   Xor, Su<Shl>, I64, I64, I32>;
}


class OpshIRI_pat<InstHexagon MI, PatFrag Op, PatFrag ShOp,
                  PatFrag RegPred, PatFrag ImmPred>
  : Pat<(Op anyimm:$u8, (ShOp RegPred:$Rs, ImmPred:$U5)),
        (MI anyimm:$u8, RegPred:$Rs, imm:$U5)>;

let AddedComplexity = 200, Predicates = [UseCompound] in {
  def: OpshIRI_pat<S4_addi_asl_ri,  Add, Su<Shl>, I32, u5_0ImmPred>;
  def: OpshIRI_pat<S4_addi_lsr_ri,  Add, Su<Srl>, I32, u5_0ImmPred>;
  def: OpshIRI_pat<S4_subi_asl_ri,  Sub, Su<Shl>, I32, u5_0ImmPred>;
  def: OpshIRI_pat<S4_subi_lsr_ri,  Sub, Su<Srl>, I32, u5_0ImmPred>;
  def: OpshIRI_pat<S4_andi_asl_ri,  And, Su<Shl>, I32, u5_0ImmPred>;
  def: OpshIRI_pat<S4_andi_lsr_ri,  And, Su<Srl>, I32, u5_0ImmPred>;
  def: OpshIRI_pat<S4_ori_asl_ri,   Or,  Su<Shl>, I32, u5_0ImmPred>;
  def: OpshIRI_pat<S4_ori_lsr_ri,   Or,  Su<Srl>, I32, u5_0ImmPred>;
}

// Prefer this pattern to S2_asl_i_p_or for the special case of joining
// two 32-bit words into a 64-bit word.
let AddedComplexity = 200 in
def: Pat<(or (shl (Aext64 I32:$a), (i32 32)), (Zext64 I32:$b)),
         (Combinew I32:$a, I32:$b)>;

def: Pat<(or (or (or (shl (Zext64 (and I32:$b, (i32 65535))), (i32 16)),
                     (Zext64 (and I32:$a, (i32 65535)))),
                 (shl (Aext64 (and I32:$c, (i32 65535))), (i32 32))),
             (shl (Aext64 I32:$d), (i32 48))),
         (Combinew (A2_combine_ll I32:$d, I32:$c),
                   (A2_combine_ll I32:$b, I32:$a))>;

let AddedComplexity = 200 in {
  def: Pat<(or (shl I32:$Rt, (i32 16)), (and I32:$Rs, (i32 65535))),
           (A2_combine_ll I32:$Rt, I32:$Rs)>;
  def: Pat<(or (shl I32:$Rt, (i32 16)), (srl I32:$Rs, (i32 16))),
           (A2_combine_lh I32:$Rt, I32:$Rs)>;
  def: Pat<(or (and I32:$Rt, (i32 268431360)), (and I32:$Rs, (i32 65535))),
           (A2_combine_hl I32:$Rt, I32:$Rs)>;
  def: Pat<(or (and I32:$Rt, (i32 268431360)), (srl I32:$Rs, (i32 16))),
           (A2_combine_hh I32:$Rt, I32:$Rs)>;
}

def SDTHexagonVShift
  : SDTypeProfile<1, 2, [SDTCisSameAs<0, 1>, SDTCisVec<0>, SDTCisVT<2, i32>]>;

def HexagonVASL: SDNode<"HexagonISD::VASL", SDTHexagonVShift>;
def HexagonVASR: SDNode<"HexagonISD::VASR", SDTHexagonVShift>;
def HexagonVLSR: SDNode<"HexagonISD::VLSR", SDTHexagonVShift>;

// Funnel shifts with the shift amount module element bit width.
def HexagonMFSHL: SDNode<"HexagonISD::MFSHL", SDTIntShiftDOp>;
def HexagonMFSHR: SDNode<"HexagonISD::MFSHR", SDTIntShiftDOp>;

def: OpR_RI_pat<S2_asl_i_vw, pf2<HexagonVASL>, v2i32, V2I32, u5_0ImmPred>;
def: OpR_RI_pat<S2_asl_i_vh, pf2<HexagonVASL>, v4i16, V4I16, u4_0ImmPred>;
def: OpR_RI_pat<S2_asr_i_vw, pf2<HexagonVASR>, v2i32, V2I32, u5_0ImmPred>;
def: OpR_RI_pat<S2_asr_i_vh, pf2<HexagonVASR>, v4i16, V4I16, u4_0ImmPred>;
def: OpR_RI_pat<S2_lsr_i_vw, pf2<HexagonVLSR>, v2i32, V2I32, u5_0ImmPred>;
def: OpR_RI_pat<S2_lsr_i_vh, pf2<HexagonVLSR>, v4i16, V4I16, u4_0ImmPred>;

def: OpR_RR_pat<S2_asl_r_vw, pf2<HexagonVASL>, v2i32, V2I32, I32>;
def: OpR_RR_pat<S2_asl_r_vh, pf2<HexagonVASL>, v4i16, V4I16, I32>;
def: OpR_RR_pat<S2_asr_r_vw, pf2<HexagonVASR>, v2i32, V2I32, I32>;
def: OpR_RR_pat<S2_asr_r_vh, pf2<HexagonVASR>, v4i16, V4I16, I32>;
def: OpR_RR_pat<S2_lsr_r_vw, pf2<HexagonVLSR>, v2i32, V2I32, I32>;
def: OpR_RR_pat<S2_lsr_r_vh, pf2<HexagonVLSR>, v4i16, V4I16, I32>;

def: Pat<(sra V2I32:$b, (v2i32 (splat_vector u5_0ImmPred:$c))),
         (S2_asr_i_vw V2I32:$b, imm:$c)>;
def: Pat<(srl V2I32:$b, (v2i32 (splat_vector u5_0ImmPred:$c))),
         (S2_lsr_i_vw V2I32:$b, imm:$c)>;
def: Pat<(shl V2I32:$b, (v2i32 (splat_vector u5_0ImmPred:$c))),
         (S2_asl_i_vw V2I32:$b, imm:$c)>;
def: Pat<(sra V4I16:$b, (v4i16 (splat_vector u4_0ImmPred:$c))),
         (S2_asr_i_vh V4I16:$b, imm:$c)>;
def: Pat<(srl V4I16:$b, (v4i16 (splat_vector u4_0ImmPred:$c))),
         (S2_lsr_i_vh V4I16:$b, imm:$c)>;
def: Pat<(shl V4I16:$b, (v4i16 (splat_vector u4_0ImmPred:$c))),
         (S2_asl_i_vh V4I16:$b, imm:$c)>;

def: Pat<(HexagonVASR V2I16:$Rs, u4_0ImmPred:$S),
         (LoReg (S2_asr_i_vh (ToAext64 $Rs), imm:$S))>;
def: Pat<(HexagonVASL V2I16:$Rs, u4_0ImmPred:$S),
         (LoReg (S2_asl_i_vh (ToAext64 $Rs), imm:$S))>;
def: Pat<(HexagonVLSR V2I16:$Rs, u4_0ImmPred:$S),
         (LoReg (S2_lsr_i_vh (ToAext64 $Rs), imm:$S))>;
def: Pat<(HexagonVASR V2I16:$Rs, I32:$Rt),
         (LoReg (S2_asr_i_vh (ToAext64 $Rs), I32:$Rt))>;
def: Pat<(HexagonVASL V2I16:$Rs, I32:$Rt),
         (LoReg (S2_asl_i_vh (ToAext64 $Rs), I32:$Rt))>;
def: Pat<(HexagonVLSR V2I16:$Rs, I32:$Rt),
         (LoReg (S2_lsr_i_vh (ToAext64 $Rs), I32:$Rt))>;


// --(9) Arithmetic/bitwise ----------------------------------------------
//

def: Pat<(abs  I32:$Rs), (A2_abs   I32:$Rs)>;
def: Pat<(abs  I64:$Rs), (A2_absp  I64:$Rs)>;
def: Pat<(not  I32:$Rs), (A2_subri -1, I32:$Rs)>;
def: Pat<(not  I64:$Rs), (A2_notp  I64:$Rs)>;
def: Pat<(ineg I64:$Rs), (A2_negp  I64:$Rs)>;

def: Pat<(fabs F32:$Rs), (S2_clrbit_i    F32:$Rs, 31)>;
def: Pat<(fneg F32:$Rs), (S2_togglebit_i F32:$Rs, 31)>;

def: Pat<(fabs F64:$Rs),
         (Combinew (S2_clrbit_i (HiReg $Rs), 31),
                   (i32 (LoReg $Rs)))>;
def: Pat<(fneg F64:$Rs),
         (Combinew (S2_togglebit_i (HiReg $Rs), 31),
                   (i32 (LoReg $Rs)))>;

def: Pat<(add I32:$Rs, anyimm:$s16),   (A2_addi   I32:$Rs,  imm:$s16)>;
def: Pat<(or  I32:$Rs, anyimm:$s10),   (A2_orir   I32:$Rs,  imm:$s10)>;
def: Pat<(and I32:$Rs, anyimm:$s10),   (A2_andir  I32:$Rs,  imm:$s10)>;
def: Pat<(sub anyimm:$s10, I32:$Rs),   (A2_subri  imm:$s10, I32:$Rs)>;

def: OpR_RR_pat<A2_add,       Add,        i32,   I32>;
def: OpR_RR_pat<A2_sub,       Sub,        i32,   I32>;
def: OpR_RR_pat<A2_and,       And,        i32,   I32>;
def: OpR_RR_pat<A2_or,        Or,         i32,   I32>;
def: OpR_RR_pat<A2_xor,       Xor,        i32,   I32>;
def: OpR_RR_pat<A2_addp,      Add,        i64,   I64>;
def: OpR_RR_pat<A2_subp,      Sub,        i64,   I64>;
def: OpR_RR_pat<A2_andp,      And,        i64,   I64>;
def: OpR_RR_pat<A2_orp,       Or,         i64,   I64>;
def: OpR_RR_pat<A2_xorp,      Xor,        i64,   I64>;
def: OpR_RR_pat<A4_andnp,     Not2<And>,  i64,   I64>;
def: OpR_RR_pat<A4_ornp,      Not2<Or>,   i64,   I64>;

def: OpR_RR_pat<A2_svaddh,    Add,        v2i16, V2I16>;
def: OpR_RR_pat<A2_svsubh,    Sub,        v2i16, V2I16>;

def: OpR_RR_pat<A2_vaddub,    Add,        v8i8,  V8I8>;
def: OpR_RR_pat<A2_vaddh,     Add,        v4i16, V4I16>;
def: OpR_RR_pat<A2_vaddw,     Add,        v2i32, V2I32>;
def: OpR_RR_pat<A2_vsubub,    Sub,        v8i8,  V8I8>;
def: OpR_RR_pat<A2_vsubh,     Sub,        v4i16, V4I16>;
def: OpR_RR_pat<A2_vsubw,     Sub,        v2i32, V2I32>;

def: OpR_RR_pat<A2_and,       And,        v4i8,  V4I8>;
def: OpR_RR_pat<A2_xor,       Xor,        v4i8,  V4I8>;
def: OpR_RR_pat<A2_or,        Or,         v4i8,  V4I8>;
def: OpR_RR_pat<A2_and,       And,        v2i16, V2I16>;
def: OpR_RR_pat<A2_xor,       Xor,        v2i16, V2I16>;
def: OpR_RR_pat<A2_or,        Or,         v2i16, V2I16>;
def: OpR_RR_pat<A2_andp,      And,        v8i8,  V8I8>;
def: OpR_RR_pat<A2_orp,       Or,         v8i8,  V8I8>;
def: OpR_RR_pat<A2_xorp,      Xor,        v8i8,  V8I8>;
def: OpR_RR_pat<A2_andp,      And,        v4i16, V4I16>;
def: OpR_RR_pat<A2_orp,       Or,         v4i16, V4I16>;
def: OpR_RR_pat<A2_xorp,      Xor,        v4i16, V4I16>;
def: OpR_RR_pat<A2_andp,      And,        v2i32, V2I32>;
def: OpR_RR_pat<A2_orp,       Or,         v2i32, V2I32>;
def: OpR_RR_pat<A2_xorp,      Xor,        v2i32, V2I32>;

def: OpR_RR_pat<M2_mpyi,      Mul,        i32,   I32>;
def: OpR_RR_pat<M2_mpy_up,    pf2<mulhs>, i32,   I32>;
def: OpR_RR_pat<M2_mpyu_up,   pf2<mulhu>, i32,   I32>;
def: OpR_RI_pat<M2_mpysip,    Mul,        i32,   I32, u32_0ImmPred>;
def: OpR_RI_pat<M2_mpysmi,    Mul,        i32,   I32, s32_0ImmPred>;

// Arithmetic on predicates.
def: OpR_RR_pat<C2_xor,       Add,        i1,    I1>;
def: OpR_RR_pat<C2_xor,       Add,        v2i1,  V2I1>;
def: OpR_RR_pat<C2_xor,       Add,        v4i1,  V4I1>;
def: OpR_RR_pat<C2_xor,       Add,        v8i1,  V8I1>;
def: OpR_RR_pat<C2_xor,       Sub,        i1,    I1>;
def: OpR_RR_pat<C2_xor,       Sub,        v2i1,  V2I1>;
def: OpR_RR_pat<C2_xor,       Sub,        v4i1,  V4I1>;
def: OpR_RR_pat<C2_xor,       Sub,        v8i1,  V8I1>;
def: OpR_RR_pat<C2_and,       Mul,        i1,    I1>;
def: OpR_RR_pat<C2_and,       Mul,        v2i1,  V2I1>;
def: OpR_RR_pat<C2_and,       Mul,        v4i1,  V4I1>;
def: OpR_RR_pat<C2_and,       Mul,        v8i1,  V8I1>;

def: OpR_RR_pat<F2_sfadd,     pf2<fadd>,    f32, F32>;
def: OpR_RR_pat<F2_sfsub,     pf2<fsub>,    f32, F32>;
def: OpR_RR_pat<F2_sfmpy,     pf2<fmul>,    f32, F32>;
def: OpR_RR_pat<F2_sfmin,     pf2<fminnum>, f32, F32>;
def: OpR_RR_pat<F2_sfmax,     pf2<fmaxnum>, f32, F32>;

let Predicates = [HasV66] in {
  def: OpR_RR_pat<F2_dfadd,     pf2<fadd>,    f64, F64>;
  def: OpR_RR_pat<F2_dfsub,     pf2<fsub>,    f64, F64>;
}

def DfMpy: OutPatFrag<(ops node:$Rs, node:$Rt),
  (F2_dfmpyhh
    (F2_dfmpylh
      (F2_dfmpylh
        (F2_dfmpyll $Rs, $Rt),
      $Rs, $Rt),
    $Rt, $Rs),
  $Rs, $Rt)>;

let Predicates = [HasV67,UseUnsafeMath], AddedComplexity = 50 in {
  def: Pat<(fmul F64:$Rs, F64:$Rt), (DfMpy $Rs, $Rt)>;
}
let Predicates = [HasV67] in {
  def: OpR_RR_pat<F2_dfmin,     pf2<fminnum>, f64, F64>;
  def: OpR_RR_pat<F2_dfmax,     pf2<fmaxnum>, f64, F64>;

  def: Pat<(fmul F64:$Rs, F64:$Rt), (DfMpy (F2_dfmpyfix $Rs, $Rt),
                                           (F2_dfmpyfix $Rt, $Rs))>;
}

// In expressions like a0*b0 + a1*b1 + ..., prefer to generate multiply-add,
// over add-add with individual multiplies as inputs.
let AddedComplexity = 10 in {
  def: AccRRI_pat<M2_macsip,    Add, Su<Mul>, I32, u32_0ImmPred>;
  def: AccRRI_pat<M2_macsin,    Sub, Su<Mul>, I32, u32_0ImmPred>;
  def: AccRRR_pat<M2_maci,      Add, Su<Mul>, I32, I32, I32>;
  let Predicates = [HasV66] in
  def: AccRRR_pat<M2_mnaci,     Sub, Su<Mul>, I32, I32, I32>;
}

def: AccRRI_pat<M2_naccii,    Sub, Su<Add>, I32, s32_0ImmPred>;
def: AccRRI_pat<M2_accii,     Add, Su<Add>, I32, s32_0ImmPred>;
def: AccRRR_pat<M2_acci,      Add, Su<Add>, I32, I32, I32>;

// Mulh for vectors
//
def: Pat<(v2i32 (mulhu V2I32:$Rss, V2I32:$Rtt)),
         (Combinew (M2_mpyu_up (HiReg $Rss), (HiReg $Rtt)),
                   (M2_mpyu_up (LoReg $Rss), (LoReg $Rtt)))>;

def: Pat<(v2i32 (mulhs V2I32:$Rss, V2I32:$Rtt)),
         (Combinew (M2_mpy_up (HiReg $Rss), (HiReg $Rtt)),
                   (M2_mpy_up (LoReg $Rss), (LoReg $Rtt)))>;

def Mulhub4:
  OutPatFrag<(ops node:$Rs, node:$Rt), (S2_vtrunohb (M5_vmpybuu $Rs, $Rt))>;
def Mulhub8:
  OutPatFrag<(ops node:$Rss, node:$Rtt),
             (Combinew (Mulhub4 (HiReg $Rss), (HiReg $Rtt)),
                       (Mulhub4 (LoReg $Rss), (LoReg $Rtt)))>;

// (mux (x >= 0), 0, y)
def Negbytes8:
  OutPatFrag<(ops node:$Rss, node:$Rtt),
             (C2_vmux (A4_vcmpbgti $Rss, -1), (A2_tfrpi 0), $Rtt)>;

def: Pat<(v4i8 (mulhu  V4I8:$Rs,  V4I8:$Rt)), (Mulhub4  $Rs,  $Rt)>;
def: Pat<(v8i8 (mulhu V8I8:$Rss, V8I8:$Rtt)), (Mulhub8 $Rss, $Rtt)>;

// (Mulhs x, y) = (Mulhu x, y) - (x < 0 ? y : 0) - (y < 0 ? x : 0)
def Mulhsb8:
  OutPatFrag<(ops node:$Rss, node:$Rtt),
             (A2_vsubub (Mulhub8 $Rss, $Rtt),
                        (A2_vaddub (Negbytes8 $Rss, $Rtt),
                                   (Negbytes8 $Rtt, $Rss)))>;

def: Pat<(v4i8 (mulhs V4I8:$Rs, V4I8:$Rt)),
         (LoReg (Mulhsb8 (v8i8 (ToAext64 $Rs)), (v8i8 (ToAext64 $Rt))))>;
def: Pat<(v8i8 (mulhs V8I8:$Rss, V8I8:$Rtt)), (Mulhsb8 $Rss, $Rtt)>;

// v2i16 *s v2i16 -> v2i32
def Muli16:
  OutPatFrag<(ops node:$Rs, node:$Rt), (M2_vmpy2s_s0 $Rs, $Rt)>;

def Mulhsh2:
  OutPatFrag<(ops node:$Rs, node:$Rt),
             (A2_combine_hh (HiReg (Muli16 $Rs, $Rt)),
                            (LoReg (Muli16 $Rs, $Rt)))>;
def Mulhsh4:
  OutPatFrag<(ops node:$Rss, node:$Rtt),
             (Combinew (Mulhsh2 (HiReg $Rss), (HiReg $Rtt)),
                       (Mulhsh2 (LoReg $Rss), (LoReg $Rtt)))>;

def: Pat<(v2i16 (mulhs  V2I16:$Rs,  V2I16:$Rt)), (Mulhsh2  $Rs,  $Rt)>;
def: Pat<(v4i16 (mulhs V4I16:$Rss, V4I16:$Rtt)), (Mulhsh4 $Rss, $Rtt)>;

def: Pat<(v2i16 (mulhu V2I16:$Rs, V2I16:$Rt)),
  (A2_svaddh
     (Mulhsh2 $Rs, $Rt),
     (A2_svaddh (LoReg (A2_andp (Combinew $Rt, $Rs),
                                (S2_asr_i_vh (Combinew $Rs, $Rt), 15))),
                (HiReg (A2_andp (Combinew $Rt, $Rs),
                                (S2_asr_i_vh (Combinew $Rs, $Rt), 15)))))>;

def: Pat<(v4i16 (mulhu V4I16:$Rss, V4I16:$Rtt)),
         (A2_vaddh
           (Mulhsh4 $Rss, $Rtt),
           (A2_vaddh (A2_andp V4I16:$Rss, (S2_asr_i_vh $Rtt, 15)),
                     (A2_andp V4I16:$Rtt, (S2_asr_i_vh $Rss, 15))))>;


def: Pat<(ineg (mul I32:$Rs, u8_0ImmPred:$u8)),
         (M2_mpysin IntRegs:$Rs, imm:$u8)>;

def n8_0ImmPred: PatLeaf<(i32 imm), [{
  int64_t V = N->getSExtValue();
  return -255 <= V && V <= 0;
}]>;

// Change the sign of the immediate for Rd=-mpyi(Rs,#u8)
def: Pat<(mul I32:$Rs, n8_0ImmPred:$n8),
         (M2_mpysin I32:$Rs, (NegImm8 imm:$n8))>;

def: Pat<(add Sext64:$Rs, I64:$Rt),
         (A2_addsp (LoReg Sext64:$Rs), I64:$Rt)>;

def: AccRRR_pat<M4_and_and,   And, Su_ni1<And>,  I32,  I32,  I32>;
def: AccRRR_pat<M4_and_or,    And, Su_ni1<Or>,   I32,  I32,  I32>;
def: AccRRR_pat<M4_and_xor,   And, Su<Xor>,      I32,  I32,  I32>;
def: AccRRR_pat<M4_or_and,    Or,  Su_ni1<And>,  I32,  I32,  I32>;
def: AccRRR_pat<M4_or_or,     Or,  Su_ni1<Or>,   I32,  I32,  I32>;
def: AccRRR_pat<M4_or_xor,    Or,  Su<Xor>,      I32,  I32,  I32>;
def: AccRRR_pat<M4_xor_and,   Xor, Su_ni1<And>,  I32,  I32,  I32>;
def: AccRRR_pat<M4_xor_or,    Xor, Su_ni1<Or>,   I32,  I32,  I32>;
def: AccRRR_pat<M2_xor_xacc,  Xor, Su<Xor>,      I32,  I32,  I32>;
def: AccRRR_pat<M4_xor_xacc,  Xor, Su<Xor>,      I64,  I64,  I64>;

// For dags like (or (and (not _), _), (shl _, _)) where the "or" with
// one argument matches the patterns below, and with the other argument
// matches S2_asl_r_r_or, etc, prefer the patterns below.
let AddedComplexity = 110 in {  // greater than S2_asl_r_r_and/or/xor.
  def: AccRRR_pat<M4_and_andn,  And, Su<Not2<And>>, I32,  I32,  I32>;
  def: AccRRR_pat<M4_or_andn,   Or,  Su<Not2<And>>, I32,  I32,  I32>;
  def: AccRRR_pat<M4_xor_andn,  Xor, Su<Not2<And>>, I32,  I32,  I32>;
}

// S4_addaddi and S4_subaddi don't have tied operands, so give them
// a bit of preference.
let AddedComplexity = 30, Predicates = [UseCompound] in {
  def: Pat<(add I32:$Rs, (Su<Add> I32:$Ru, anyimm:$s6)),
           (S4_addaddi IntRegs:$Rs, IntRegs:$Ru, imm:$s6)>;
  def: Pat<(add anyimm:$s6, (Su<Add> I32:$Rs, I32:$Ru)),
           (S4_addaddi IntRegs:$Rs, IntRegs:$Ru, imm:$s6)>;
  def: Pat<(add I32:$Rs, (Su<Sub> anyimm:$s6, I32:$Ru)),
           (S4_subaddi IntRegs:$Rs, imm:$s6, IntRegs:$Ru)>;
  def: Pat<(sub (Su<Add> I32:$Rs, anyimm:$s6), I32:$Ru),
           (S4_subaddi IntRegs:$Rs, imm:$s6, IntRegs:$Ru)>;
  def: Pat<(add (Su<Sub> I32:$Rs, I32:$Ru), anyimm:$s6),
           (S4_subaddi IntRegs:$Rs, imm:$s6, IntRegs:$Ru)>;
}

let Predicates = [UseCompound] in
def: Pat<(or I32:$Ru, (Su<And> I32:$Rx, anyimm:$s10)),
         (S4_or_andix IntRegs:$Ru, IntRegs:$Rx, imm:$s10)>;

def: Pat<(or I32:$Rx, (Su<And> I32:$Rs, anyimm:$s10)),
         (S4_or_andi IntRegs:$Rx, IntRegs:$Rs, imm:$s10)>;
def: Pat<(or I32:$Rx, (Su<Or> I32:$Rs, anyimm:$s10)),
         (S4_or_ori IntRegs:$Rx, IntRegs:$Rs, imm:$s10)>;


def: Pat<(i32 (trunc (sra (Su<Mul> Sext64:$Rs, Sext64:$Rt), (i32 32)))),
         (M2_mpy_up (LoReg Sext64:$Rs), (LoReg Sext64:$Rt))>;
def: Pat<(i32 (trunc (srl (Su<Mul> Sext64:$Rs, Sext64:$Rt), (i32 32)))),
         (M2_mpy_up (LoReg Sext64:$Rs), (LoReg Sext64:$Rt))>;

def: Pat<(mul (Zext64 I32:$Rs), (Zext64 I32:$Rt)),
         (M2_dpmpyuu_s0 I32:$Rs, I32:$Rt)>;
def: Pat<(mul (Aext64 I32:$Rs), (Aext64 I32:$Rt)),
         (M2_dpmpyuu_s0 I32:$Rs, I32:$Rt)>;
def: Pat<(mul Sext64:$Rs, Sext64:$Rt),
         (M2_dpmpyss_s0 (LoReg Sext64:$Rs), (LoReg Sext64:$Rt))>;

def: Pat<(add I64:$Rx, (Su<Mul> Sext64:$Rs, Sext64:$Rt)),
         (M2_dpmpyss_acc_s0 I64:$Rx, (LoReg Sext64:$Rs), (LoReg Sext64:$Rt))>;
def: Pat<(sub I64:$Rx, (Su<Mul> Sext64:$Rs, Sext64:$Rt)),
         (M2_dpmpyss_nac_s0 I64:$Rx, (LoReg Sext64:$Rs), (LoReg Sext64:$Rt))>;
def: Pat<(add I64:$Rx, (Su<Mul> (Aext64 I32:$Rs), (Aext64 I32:$Rt))),
         (M2_dpmpyuu_acc_s0 I64:$Rx, I32:$Rs, I32:$Rt)>;
def: Pat<(add I64:$Rx, (Su<Mul> (Zext64 I32:$Rs), (Zext64 I32:$Rt))),
         (M2_dpmpyuu_acc_s0 I64:$Rx, I32:$Rs, I32:$Rt)>;
def: Pat<(sub I64:$Rx, (Su<Mul> (Aext64 I32:$Rs), (Aext64 I32:$Rt))),
         (M2_dpmpyuu_nac_s0 I64:$Rx, I32:$Rs, I32:$Rt)>;
def: Pat<(sub I64:$Rx, (Su<Mul> (Zext64 I32:$Rs), (Zext64 I32:$Rt))),
         (M2_dpmpyuu_nac_s0 I64:$Rx, I32:$Rs, I32:$Rt)>;

// Add halfword.
def: Pat<(sext_inreg (add I32:$Rt, I32:$Rs), i16),
         (A2_addh_l16_ll I32:$Rt, I32:$Rs)>;
def: Pat<(sra (add (shl I32:$Rt, (i32 16)), I32:$Rs), (i32 16)),
         (A2_addh_l16_hl I32:$Rt, I32:$Rs)>;
def: Pat<(shl (add I32:$Rt, I32:$Rs), (i32 16)),
         (A2_addh_h16_ll I32:$Rt, I32:$Rs)>;

// Subtract halfword.
def: Pat<(sext_inreg (sub I32:$Rt, I32:$Rs), i16),
         (A2_subh_l16_ll I32:$Rt, I32:$Rs)>;
def: Pat<(sra (add (shl I32:$Rt, (i32 16)), I32:$Rs), (i32 16)),
         (A2_addh_l16_hl I32:$Rt, I32:$Rs)>;
def: Pat<(shl (sub I32:$Rt, I32:$Rs), (i32 16)),
         (A2_subh_h16_ll I32:$Rt, I32:$Rs)>;

def: Pat<(mul I64:$Rss, I64:$Rtt),
         (Combinew
           (M2_maci (M2_maci (HiReg (M2_dpmpyuu_s0 (LoReg $Rss), (LoReg $Rtt))),
                             (LoReg $Rss),
                             (HiReg $Rtt)),
                    (LoReg $Rtt),
                    (HiReg $Rss)),
           (i32 (LoReg (M2_dpmpyuu_s0 (LoReg $Rss), (LoReg $Rtt)))))>;

def MulHU : OutPatFrag<(ops node:$Rss, node:$Rtt),
  (A2_addp
    (M2_dpmpyuu_acc_s0
      (S2_lsr_i_p
        (A2_addp
          (M2_dpmpyuu_acc_s0
            (S2_lsr_i_p (M2_dpmpyuu_s0 (LoReg $Rss), (LoReg $Rtt)), 32),
            (HiReg $Rss),
            (LoReg $Rtt)),
          (A4_combineir 0, (LoReg (M2_dpmpyuu_s0 (LoReg $Rss), (HiReg $Rtt))))),
        32),
      (HiReg $Rss),
      (HiReg $Rtt)),
    (S2_lsr_i_p (M2_dpmpyuu_s0 (LoReg $Rss), (HiReg $Rtt)), 32))>;

// Multiply 64-bit unsigned and use upper result.
def : Pat <(mulhu I64:$Rss, I64:$Rtt), (MulHU $Rss, $Rtt)>;

// Multiply 64-bit signed and use upper result.
//
// For two signed 64-bit integers A and B, let A' and B' denote A and B
// with the sign bit cleared. Then A = -2^63*s(A) + A', where s(A) is the
// sign bit of A (and identically for B). With this notation, the signed
// product A*B can be written as:
//   AB = (-2^63 s(A) + A') * (-2^63 s(B) + B')
//      = 2^126 s(A)s(B) - 2^63 [s(A)B'+s(B)A'] + A'B'
//      = 2^126 s(A)s(B) + 2^63 [s(A)B'+s(B)A'] + A'B' - 2*2^63 [s(A)B'+s(B)A']
//      = (unsigned product AB) - 2^64 [s(A)B'+s(B)A']

// Clear the sign bit in a 64-bit register.
def ClearSign : OutPatFrag<(ops node:$Rss),
  (Combinew (S2_clrbit_i (HiReg $Rss), 31), (i32 (LoReg $Rss)))>;

def : Pat <(mulhs I64:$Rss, I64:$Rtt),
  (A2_subp
    (MulHU $Rss, $Rtt),
    (A2_addp
      (A2_andp (S2_asr_i_p $Rss, 63), (ClearSign $Rtt)),
      (A2_andp (S2_asr_i_p $Rtt, 63), (ClearSign $Rss))))>;

// Prefer these instructions over M2_macsip/M2_macsin: the macsi* instructions
// will put the immediate addend into a register, while these instructions will
// use it directly. Such a construct does not appear in the middle of a gep,
// where M2_macsip would be preferable.
let AddedComplexity = 20, Predicates = [UseCompound] in {
  def: Pat<(add (Su<Mul> I32:$Rs, u6_0ImmPred:$U6), anyimm:$u6),
           (M4_mpyri_addi imm:$u6, IntRegs:$Rs, imm:$U6)>;
  def: Pat<(add (Su<Mul> I32:$Rs, I32:$Rt), anyimm:$u6),
           (M4_mpyrr_addi imm:$u6, IntRegs:$Rs, IntRegs:$Rt)>;
}

// Keep these instructions less preferable to M2_macsip/M2_macsin.
let Predicates = [UseCompound] in {
  def: Pat<(add I32:$Ru, (Su<Mul> I32:$Rs, u6_2ImmPred:$u6_2)),
           (M4_mpyri_addr_u2 IntRegs:$Ru, imm:$u6_2, IntRegs:$Rs)>;
  def: Pat<(add I32:$Ru, (Su<Mul> I32:$Rs, anyimm:$u6)),
           (M4_mpyri_addr IntRegs:$Ru, IntRegs:$Rs, imm:$u6)>;
  def: Pat<(add I32:$Ru, (Su<Mul> I32:$Ry, I32:$Rs)),
           (M4_mpyrr_addr IntRegs:$Ru, IntRegs:$Ry, IntRegs:$Rs)>;
}

def: Pat<(fma F32:$Rs, F32:$Rt, F32:$Rx),
         (F2_sffma F32:$Rx, F32:$Rs, F32:$Rt)>;
def: Pat<(fma (fneg F32:$Rs), F32:$Rt, F32:$Rx),
         (F2_sffms F32:$Rx, F32:$Rs, F32:$Rt)>;

def: Pat<(mul V2I32:$Rs, V2I32:$Rt),
         (PS_vmulw V2I32:$Rs, V2I32:$Rt)>;
def: Pat<(add V2I32:$Rx, (mul V2I32:$Rs, V2I32:$Rt)),
         (PS_vmulw_acc V2I32:$Rx, V2I32:$Rs, V2I32:$Rt)>;

// Add/subtract two v4i8: Hexagon does not have an insn for this one, so
// we use the double add v8i8, and use only the low part of the result.
def: Pat<(add V4I8:$Rs, V4I8:$Rt),
         (LoReg (A2_vaddub (ToAext64 $Rs), (ToAext64 $Rt)))>;
def: Pat<(sub V4I8:$Rs, V4I8:$Rt),
         (LoReg (A2_vsubub (ToAext64 $Rs), (ToAext64 $Rt)))>;

// Use M2_vmpy2s_s0 for half-word vector multiply. It multiplies two
// half-words, and saturates the result to a 32-bit value, except the
// saturation never happens (it can only occur with scaling).
def: Pat<(v2i16 (mul V2I16:$Rs, V2I16:$Rt)),
         (LoReg (S2_vtrunewh (IMPLICIT_DEF),
                             (M2_vmpy2s_s0 V2I16:$Rs, V2I16:$Rt)))>;
def: Pat<(v4i16 (mul V4I16:$Rs, V4I16:$Rt)),
         (S2_vtrunewh (M2_vmpy2s_s0 (HiReg $Rs), (HiReg $Rt)),
                      (M2_vmpy2s_s0 (LoReg $Rs), (LoReg $Rt)))>;

// Multiplies two v4i8 vectors.
def: Pat<(v4i8 (mul V4I8:$Rs, V4I8:$Rt)),
         (S2_vtrunehb (M5_vmpybuu V4I8:$Rs, V4I8:$Rt))>;

// Multiplies two v8i8 vectors.
def: Pat<(v8i8 (mul V8I8:$Rs, V8I8:$Rt)),
         (Combinew (S2_vtrunehb (M5_vmpybuu (HiReg $Rs), (HiReg $Rt))),
                   (S2_vtrunehb (M5_vmpybuu (LoReg $Rs), (LoReg $Rt))))>;


// --(10) Bit ------------------------------------------------------------
//

// Count leading zeros.
def: Pat<(i32 (ctlz I32:$Rs)),                (S2_cl0 I32:$Rs)>;
def: Pat<(i32 (trunc (ctlz I64:$Rss))),       (S2_cl0p I64:$Rss)>;

// Count trailing zeros.
def: Pat<(i32 (cttz I32:$Rs)),                (S2_ct0 I32:$Rs)>;
def: Pat<(i32 (trunc (cttz I64:$Rss))),       (S2_ct0p I64:$Rss)>;

// Count leading ones.
def: Pat<(i32 (ctlz (not I32:$Rs))),          (S2_cl1 I32:$Rs)>;
def: Pat<(i32 (trunc (ctlz (not I64:$Rss)))), (S2_cl1p I64:$Rss)>;

// Count trailing ones.
def: Pat<(i32 (cttz (not I32:$Rs))),           (S2_ct1 I32:$Rs)>;
def: Pat<(i32 (trunc (cttz (not I64:$Rss)))), (S2_ct1p I64:$Rss)>;

// Define leading/trailing patterns that require zero-extensions to 64 bits.
def: Pat<(i64 (ctlz I64:$Rss)),               (ToZext64 (S2_cl0p I64:$Rss))>;
def: Pat<(i64 (cttz I64:$Rss)),               (ToZext64 (S2_ct0p I64:$Rss))>;
def: Pat<(i64 (ctlz (not I64:$Rss))),         (ToZext64 (S2_cl1p I64:$Rss))>;
def: Pat<(i64 (cttz (not I64:$Rss))),         (ToZext64 (S2_ct1p I64:$Rss))>;

def: Pat<(i64 (ctpop I64:$Rss)),  (ToZext64 (S5_popcountp I64:$Rss))>;
def: Pat<(i32 (ctpop I32:$Rs)),   (S5_popcountp (A4_combineir 0, I32:$Rs))>;

def: Pat<(bitreverse I32:$Rs),    (S2_brev I32:$Rs)>;
def: Pat<(bitreverse I64:$Rss),   (S2_brevp I64:$Rss)>;

def: Pat<(bitreverse V4I8:$Rs),   (A2_swiz (S2_brev $Rs))>;
def: Pat<(bitreverse V8I8:$Rs),   (Combinew (A2_swiz (LoReg (S2_brevp $Rs))),
                                            (A2_swiz (HiReg (S2_brevp $Rs))))>;
def: Pat<(bitreverse V2I16:$Rs),  (A2_combine_lh (S2_brev $Rs),
                                                 (S2_brev $Rs))>;
def: Pat<(bitreverse V4I16:$Rs),
         (Combinew (A2_combine_lh (LoReg (S2_brevp $Rs)),
                                  (LoReg (S2_brevp $Rs))),
                   (A2_combine_lh (HiReg (S2_brevp $Rs)),
                                  (HiReg (S2_brevp $Rs))))>;
def: Pat<(bitreverse V2I32:$Rs),
         (Combinew (i32 (LoReg (S2_brevp $Rs))),
                   (i32 (HiReg (S2_brevp $Rs))))>;

let AddedComplexity = 20 in { // Complexity greater than and/or/xor
  def: Pat<(and I32:$Rs, IsNPow2_32:$V),
           (S2_clrbit_i IntRegs:$Rs, (LogN2_32 $V))>;
  def: Pat<(or I32:$Rs, IsPow2_32:$V),
           (S2_setbit_i IntRegs:$Rs, (Log2_32 $V))>;
  def: Pat<(xor I32:$Rs, IsPow2_32:$V),
           (S2_togglebit_i IntRegs:$Rs, (Log2_32 $V))>;

  def: Pat<(and I32:$Rs, (not (shl 1, I32:$Rt))),
           (S2_clrbit_r IntRegs:$Rs, IntRegs:$Rt)>;
  def: Pat<(or I32:$Rs, (shl 1, I32:$Rt)),
           (S2_setbit_r IntRegs:$Rs, IntRegs:$Rt)>;
  def: Pat<(xor I32:$Rs, (shl 1, I32:$Rt)),
           (S2_togglebit_r IntRegs:$Rs, IntRegs:$Rt)>;
}

// Clr/set/toggle bit for 64-bit values with immediate bit index.
let AddedComplexity = 20 in { // Complexity greater than and/or/xor
  def: Pat<(and I64:$Rss, IsNPow2_64L:$V),
           (Combinew (i32 (HiReg $Rss)),
                     (S2_clrbit_i (LoReg $Rss), (LogN2_64 $V)))>;
  def: Pat<(and I64:$Rss, IsNPow2_64H:$V),
           (Combinew (S2_clrbit_i (HiReg $Rss), (UDEC32 (i32 (LogN2_64 $V)))),
                     (i32 (LoReg $Rss)))>;

  def: Pat<(or I64:$Rss, IsPow2_64L:$V),
           (Combinew (i32 (HiReg $Rss)),
                     (S2_setbit_i (LoReg $Rss), (Log2_64 $V)))>;
  def: Pat<(or I64:$Rss, IsPow2_64H:$V),
           (Combinew (S2_setbit_i (HiReg $Rss), (UDEC32 (i32 (Log2_64 $V)))),
                     (i32 (LoReg $Rss)))>;

  def: Pat<(xor I64:$Rss, IsPow2_64L:$V),
           (Combinew (i32 (HiReg $Rss)),
                     (S2_togglebit_i (LoReg $Rss), (Log2_64 $V)))>;
  def: Pat<(xor I64:$Rss, IsPow2_64H:$V),
           (Combinew (S2_togglebit_i (HiReg $Rss), (UDEC32 (i32 (Log2_64 $V)))),
                     (i32 (LoReg $Rss)))>;
}


let AddedComplexity = 20 in { // Complexity greater than cmp reg-imm.
  def: Pat<(i1 (setne (and (shl 1, u5_0ImmPred:$u5), I32:$Rs), 0)),
           (S2_tstbit_i IntRegs:$Rs, imm:$u5)>;
  def: Pat<(i1 (setne (and (shl 1, I32:$Rt), I32:$Rs), 0)),
           (S2_tstbit_r IntRegs:$Rs, IntRegs:$Rt)>;
  def: Pat<(i1 (trunc I32:$Rs)),
           (S2_tstbit_i IntRegs:$Rs, 0)>;
  def: Pat<(i1 (trunc I64:$Rs)),
           (S2_tstbit_i (LoReg DoubleRegs:$Rs), 0)>;
}

def: Pat<(and (srl I32:$Rs, u5_0ImmPred:$u5), 1),
         (I1toI32 (S2_tstbit_i I32:$Rs, imm:$u5))>;
def: Pat<(and (srl I64:$Rss, IsULE<32,31>:$u6), 1),
         (ToZext64 (I1toI32 (S2_tstbit_i (LoReg $Rss), imm:$u6)))>;
def: Pat<(and (srl I64:$Rss, IsUGT<32,31>:$u6), 1),
         (ToZext64 (I1toI32 (S2_tstbit_i (HiReg $Rss), (UDEC32 $u6))))>;

def: Pat<(and (not (srl I32:$Rs, u5_0ImmPred:$u5)), 1),
         (I1toI32 (S4_ntstbit_i I32:$Rs, imm:$u5))>;
def: Pat<(and (not (srl I64:$Rss, IsULE<32,31>:$u6)), 1),
         (ToZext64 (I1toI32 (S4_ntstbit_i (LoReg $Rss), imm:$u6)))>;
def: Pat<(and (not (srl I64:$Rss, IsUGT<32,31>:$u6)), 1),
         (ToZext64 (I1toI32 (S4_ntstbit_i (HiReg $Rss), (UDEC32 $u6))))>;

let AddedComplexity = 20 in { // Complexity greater than compare reg-imm.
  def: Pat<(i1 (seteq (and I32:$Rs, u6_0ImmPred:$u6), 0)),
           (C2_bitsclri IntRegs:$Rs, imm:$u6)>;
  def: Pat<(i1 (seteq (and I32:$Rs, I32:$Rt), 0)),
           (C2_bitsclr IntRegs:$Rs, IntRegs:$Rt)>;
}

let AddedComplexity = 10 in   // Complexity greater than compare reg-reg.
def: Pat<(i1 (seteq (and I32:$Rs, I32:$Rt), IntRegs:$Rt)),
         (C2_bitsset IntRegs:$Rs, IntRegs:$Rt)>;

def SDTTestBit:
  SDTypeProfile<1, 2, [SDTCisVT<0, i1>, SDTCisVT<1, i32>, SDTCisVT<2, i32>]>;
def HexagonTSTBIT: SDNode<"HexagonISD::TSTBIT", SDTTestBit>;

def: Pat<(HexagonTSTBIT I32:$Rs, u5_0ImmPred:$u5),
         (S2_tstbit_i I32:$Rs, imm:$u5)>;
def: Pat<(HexagonTSTBIT I32:$Rs, I32:$Rt),
         (S2_tstbit_r I32:$Rs, I32:$Rt)>;

// Add extra complexity to prefer these instructions over bitsset/bitsclr.
// The reason is that tstbit/ntstbit can be folded into a compound instruction:
//   if ([!]tstbit(...)) jump ...
let AddedComplexity = 20 in {   // Complexity greater than cmp reg-imm.
  def: Pat<(i1 (seteq (and I32:$Rs, IsPow2_32:$u5), 0)),
           (S4_ntstbit_i I32:$Rs, (Log2_32 imm:$u5))>;
  def: Pat<(i1 (setne (and I32:$Rs, IsPow2_32:$u5), 0)),
           (S2_tstbit_i I32:$Rs, (Log2_32 imm:$u5))>;
  def: Pat<(i1 (seteq (and (shl 1, I32:$Rt), I32:$Rs), 0)),
           (S4_ntstbit_r I32:$Rs, I32:$Rt)>;
  def: Pat<(i1 (setne (and (shl 1, I32:$Rt), I32:$Rs), 0)),
           (S2_tstbit_r I32:$Rs, I32:$Rt)>;
}

def: Pat<(i1 (seteq (and I64:$Rs, IsPow2_64L:$u6), 0)),
         (S4_ntstbit_i (LoReg $Rs), (Log2_64 $u6))>;
def: Pat<(i1 (seteq (and I64:$Rs, IsPow2_64H:$u6), 0)),
         (S4_ntstbit_i (HiReg $Rs), (UDEC32 (i32 (Log2_64 $u6))))>;
def: Pat<(i1 (setne (and I64:$Rs, IsPow2_64L:$u6), 0)),
         (S2_tstbit_i (LoReg $Rs), (Log2_64 imm:$u6))>;
def: Pat<(i1 (setne (and I64:$Rs, IsPow2_64H:$u6), 0)),
         (S2_tstbit_i (HiReg $Rs), (UDEC32 (i32 (Log2_64 imm:$u6))))>;

// Do not increase complexity of these patterns. In the DAG, "cmp i8" may be
// represented as a compare against "value & 0xFF", which is an exact match
// for cmpb (same for cmph). The patterns below do not contain any additional
// complexity that would make them preferable, and if they were actually used
// instead of cmpb/cmph, they would result in a compare against register that
// is loaded with the byte/half mask (i.e. 0xFF or 0xFFFF).
def: Pat<(i1 (setne (and I32:$Rs, u6_0ImmPred:$u6), 0)),
         (C4_nbitsclri I32:$Rs, imm:$u6)>;
def: Pat<(i1 (setne (and I32:$Rs, I32:$Rt), 0)),
         (C4_nbitsclr I32:$Rs, I32:$Rt)>;
def: Pat<(i1 (setne (and I32:$Rs, I32:$Rt), I32:$Rt)),
         (C4_nbitsset I32:$Rs, I32:$Rt)>;

// Special patterns to address certain cases where the "top-down" matching
// algorithm would cause suboptimal selection.

let AddedComplexity = 100 in {
  // Avoid A4_rcmp[n]eqi in these cases:
  def: Pat<(i32 (zext (i1 (seteq (and (shl 1, I32:$Rt), I32:$Rs), 0)))),
           (I1toI32 (S4_ntstbit_r IntRegs:$Rs, IntRegs:$Rt))>;
  def: Pat<(i32 (zext (i1 (setne (and (shl 1, I32:$Rt), I32:$Rs), 0)))),
           (I1toI32 (S2_tstbit_r IntRegs:$Rs, IntRegs:$Rt))>;
  def: Pat<(i32 (zext (i1 (seteq (and I32:$Rs, IsPow2_32:$u5), 0)))),
           (I1toI32 (S4_ntstbit_i I32:$Rs, (Log2_32 imm:$u5)))>;
  def: Pat<(i32 (zext (i1 (setne (and I32:$Rs, IsPow2_32:$u5), 0)))),
           (I1toI32 (S2_tstbit_i I32:$Rs, (Log2_32 imm:$u5)))>;
  def: Pat<(i32 (zext (i1 (seteq (and (shl 1, I32:$Rt), I32:$Rs), 0)))),
           (I1toI32 (S4_ntstbit_r I32:$Rs, I32:$Rt))>;
  def: Pat<(i32 (zext (i1 (setne (and (shl 1, I32:$Rt), I32:$Rs), 0)))),
           (I1toI32 (S2_tstbit_r I32:$Rs, I32:$Rt))>;
}

// --(11) PIC ------------------------------------------------------------
//

def SDT_HexagonAtGot
  : SDTypeProfile<1, 3, [SDTCisVT<0, i32>, SDTCisVT<1, i32>, SDTCisVT<2, i32>]>;
def SDT_HexagonAtPcrel
  : SDTypeProfile<1, 1, [SDTCisVT<0, i32>, SDTCisVT<1, i32>]>;

// AT_GOT address-of-GOT, address-of-global, offset-in-global
def HexagonAtGot       : SDNode<"HexagonISD::AT_GOT", SDT_HexagonAtGot>;
// AT_PCREL address-of-global
def HexagonAtPcrel     : SDNode<"HexagonISD::AT_PCREL", SDT_HexagonAtPcrel>;

def: Pat<(HexagonAtGot I32:$got, I32:$addr, (i32 0)),
         (L2_loadri_io I32:$got, imm:$addr)>;
def: Pat<(HexagonAtGot I32:$got, I32:$addr, s30_2ImmPred:$off),
         (A2_addi (L2_loadri_io I32:$got, imm:$addr), imm:$off)>;
def: Pat<(HexagonAtPcrel I32:$addr),
         (C4_addipc imm:$addr)>;

// The HVX load patterns also match AT_PCREL directly. Make sure that
// if the selection of this opcode changes, it's updated in all places.


// --(12) Load -----------------------------------------------------------
//

def L1toI32:  OutPatFrag<(ops node:$Rs), (A2_subri 0, (i32 $Rs))>;
def L1toI64:  OutPatFrag<(ops node:$Rs), (ToSext64 (L1toI32 $Rs))>;

def extloadv2i8: PatFrag<(ops node:$ptr), (extload node:$ptr), [{
  return cast<LoadSDNode>(N)->getMemoryVT() == MVT::v2i8;
}]>;
def extloadv4i8: PatFrag<(ops node:$ptr), (extload node:$ptr), [{
  return cast<LoadSDNode>(N)->getMemoryVT() == MVT::v4i8;
}]>;

def zextloadv2i8: PatFrag<(ops node:$ptr), (zextload node:$ptr), [{
  return cast<LoadSDNode>(N)->getMemoryVT() == MVT::v2i8;
}]>;
def zextloadv4i8: PatFrag<(ops node:$ptr), (zextload node:$ptr), [{
  return cast<LoadSDNode>(N)->getMemoryVT() == MVT::v4i8;
}]>;

def sextloadv2i8: PatFrag<(ops node:$ptr), (sextload node:$ptr), [{
  return cast<LoadSDNode>(N)->getMemoryVT() == MVT::v2i8;
}]>;
def sextloadv4i8: PatFrag<(ops node:$ptr), (sextload node:$ptr), [{
  return cast<LoadSDNode>(N)->getMemoryVT() == MVT::v4i8;
}]>;

// Patterns to select load-indexed: Rs + Off.
// - frameindex [+ imm],
multiclass Loadxfi_pat<PatFrag Load, ValueType VT, PatLeaf ImmPred,
                       InstHexagon MI> {
  def: Pat<(VT (Load (add (i32 AddrFI:$fi), ImmPred:$Off))),
           (VT (MI AddrFI:$fi, imm:$Off))>;
  def: Pat<(VT (Load (IsOrAdd (i32 AddrFI:$fi), ImmPred:$Off))),
           (VT (MI AddrFI:$fi, imm:$Off))>;
  def: Pat<(VT (Load AddrFI:$fi)), (VT (MI AddrFI:$fi, 0))>;
}

// Patterns to select load-indexed: Rs + Off.
// - base reg [+ imm]
multiclass Loadxgi_pat<PatFrag Load, ValueType VT, PatLeaf ImmPred,
                       InstHexagon MI> {
  def: Pat<(VT (Load (add I32:$Rs, ImmPred:$Off))),
           (VT (MI IntRegs:$Rs, imm:$Off))>;
  def: Pat<(VT (Load (IsOrAdd I32:$Rs, ImmPred:$Off))),
           (VT (MI IntRegs:$Rs, imm:$Off))>;
  def: Pat<(VT (Load I32:$Rs)), (VT (MI IntRegs:$Rs, 0))>;
}

// Patterns to select load-indexed: Rs + Off. Combines Loadxfi + Loadxgi.
multiclass Loadxi_pat<PatFrag Load, ValueType VT, PatLeaf ImmPred,
                      InstHexagon MI> {
  defm: Loadxfi_pat<Load, VT, ImmPred, MI>;
  defm: Loadxgi_pat<Load, VT, ImmPred, MI>;
}

// Patterns to select load reg indexed: Rs + Off with a value modifier.
// - frameindex [+ imm]
multiclass Loadxfim_pat<PatFrag Load, ValueType VT, PatFrag ValueMod,
                        PatLeaf ImmPred, InstHexagon MI> {
  def: Pat<(VT (Load (add (i32 AddrFI:$fi), ImmPred:$Off))),
           (VT (ValueMod (MI AddrFI:$fi, imm:$Off)))>;
  def: Pat<(VT (Load (IsOrAdd (i32 AddrFI:$fi), ImmPred:$Off))),
           (VT (ValueMod (MI AddrFI:$fi, imm:$Off)))>;
  def: Pat<(VT (Load AddrFI:$fi)), (VT (ValueMod (MI AddrFI:$fi, 0)))>;
}

// Patterns to select load reg indexed: Rs + Off with a value modifier.
// - base reg [+ imm]
multiclass Loadxgim_pat<PatFrag Load, ValueType VT, PatFrag ValueMod,
                        PatLeaf ImmPred, InstHexagon MI> {
  def: Pat<(VT (Load (add I32:$Rs, ImmPred:$Off))),
           (VT (ValueMod (MI IntRegs:$Rs, imm:$Off)))>;
  def: Pat<(VT (Load (IsOrAdd I32:$Rs, ImmPred:$Off))),
           (VT (ValueMod (MI IntRegs:$Rs, imm:$Off)))>;
  def: Pat<(VT (Load I32:$Rs)), (VT (ValueMod (MI IntRegs:$Rs, 0)))>;
}

// Patterns to select load reg indexed: Rs + Off with a value modifier.
// Combines Loadxfim + Loadxgim.
multiclass Loadxim_pat<PatFrag Load, ValueType VT, PatFrag ValueMod,
                       PatLeaf ImmPred, InstHexagon MI> {
  defm: Loadxfim_pat<Load, VT, ValueMod, ImmPred, MI>;
  defm: Loadxgim_pat<Load, VT, ValueMod, ImmPred, MI>;
}

// Pattern to select load reg reg-indexed: Rs + Rt<<u2.
class Loadxr_shl_pat<PatFrag Load, ValueType VT, InstHexagon MI>
  : Pat<(VT (Load (add I32:$Rs, (i32 (shl I32:$Rt, u2_0ImmPred:$u2))))),
        (VT (MI IntRegs:$Rs, IntRegs:$Rt, imm:$u2))>;

// Pattern to select load reg reg-indexed: Rs + Rt<<0.
class Loadxr_add_pat<PatFrag Load, ValueType VT, InstHexagon MI>
  : Pat<(VT (Load (add I32:$Rs, I32:$Rt))),
        (VT (MI IntRegs:$Rs, IntRegs:$Rt, 0))>;

// Pattern to select load reg reg-indexed: Rs + Rt<<u2 with value modifier.
class Loadxrm_shl_pat<PatFrag Load, ValueType VT, PatFrag ValueMod,
                      InstHexagon MI>
  : Pat<(VT (Load (add I32:$Rs, (i32 (shl I32:$Rt, u2_0ImmPred:$u2))))),
        (VT (ValueMod (MI IntRegs:$Rs, IntRegs:$Rt, imm:$u2)))>;

// Pattern to select load reg reg-indexed: Rs + Rt<<0 with value modifier.
class Loadxrm_add_pat<PatFrag Load, ValueType VT, PatFrag ValueMod,
                      InstHexagon MI>
  : Pat<(VT (Load (add I32:$Rs, I32:$Rt))),
        (VT (ValueMod (MI IntRegs:$Rs, IntRegs:$Rt, 0)))>;

// Pattern to select load long-offset reg-indexed: Addr + Rt<<u2.
// Don't match for u2==0, instead use reg+imm for those cases.
class Loadxu_pat<PatFrag Load, ValueType VT, PatFrag ImmPred, InstHexagon MI>
  : Pat<(VT (Load (add (shl IntRegs:$Rt, u2_0ImmPred:$u2), ImmPred:$Addr))),
        (VT (MI IntRegs:$Rt, imm:$u2, ImmPred:$Addr))>;

class Loadxum_pat<PatFrag Load, ValueType VT, PatFrag ImmPred, PatFrag ValueMod,
                  InstHexagon MI>
  : Pat<(VT (Load (add (shl IntRegs:$Rt, u2_0ImmPred:$u2), ImmPred:$Addr))),
        (VT (ValueMod (MI IntRegs:$Rt, imm:$u2, ImmPred:$Addr)))>;

// Pattern to select load absolute.
class Loada_pat<PatFrag Load, ValueType VT, PatFrag Addr, InstHexagon MI>
  : Pat<(VT (Load Addr:$addr)), (MI Addr:$addr)>;

// Pattern to select load absolute with value modifier.
class Loadam_pat<PatFrag Load, ValueType VT, PatFrag Addr, PatFrag ValueMod,
                 InstHexagon MI>
  : Pat<(VT (Load Addr:$addr)), (ValueMod (MI Addr:$addr))>;


let AddedComplexity = 20 in {
  defm: Loadxi_pat<extloadi1,       i32,   anyimm0, L2_loadrub_io>;
  defm: Loadxi_pat<extloadi8,       i32,   anyimm0, L2_loadrub_io>;
  defm: Loadxi_pat<extloadi16,      i32,   anyimm1, L2_loadruh_io>;
  defm: Loadxi_pat<extloadv2i8,     v2i16, anyimm1, L2_loadbzw2_io>;
  defm: Loadxi_pat<extloadv4i8,     v4i16, anyimm2, L2_loadbzw4_io>;
  defm: Loadxi_pat<sextloadi8,      i32,   anyimm0, L2_loadrb_io>;
  defm: Loadxi_pat<sextloadi16,     i32,   anyimm1, L2_loadrh_io>;
  defm: Loadxi_pat<sextloadv2i8,    v2i16, anyimm1, L2_loadbsw2_io>;
  defm: Loadxi_pat<sextloadv4i8,    v4i16, anyimm2, L2_loadbsw4_io>;
  defm: Loadxi_pat<zextloadi1,      i32,   anyimm0, L2_loadrub_io>;
  defm: Loadxi_pat<zextloadi8,      i32,   anyimm0, L2_loadrub_io>;
  defm: Loadxi_pat<zextloadi16,     i32,   anyimm1, L2_loadruh_io>;
  defm: Loadxi_pat<zextloadv2i8,    v2i16, anyimm1, L2_loadbzw2_io>;
  defm: Loadxi_pat<zextloadv4i8,    v4i16, anyimm2, L2_loadbzw4_io>;
  defm: Loadxi_pat<load,            i32,   anyimm2, L2_loadri_io>;
  defm: Loadxi_pat<load,            v2i16, anyimm2, L2_loadri_io>;
  defm: Loadxi_pat<load,            v4i8,  anyimm2, L2_loadri_io>;
  defm: Loadxi_pat<load,            i64,   anyimm3, L2_loadrd_io>;
  defm: Loadxi_pat<load,            v2i32, anyimm3, L2_loadrd_io>;
  defm: Loadxi_pat<load,            v4i16, anyimm3, L2_loadrd_io>;
  defm: Loadxi_pat<load,            v8i8,  anyimm3, L2_loadrd_io>;
  defm: Loadxi_pat<load,            f32,   anyimm2, L2_loadri_io>;
  defm: Loadxi_pat<load,            f64,   anyimm3, L2_loadrd_io>;
  // No sextloadi1.

  defm: Loadxi_pat<atomic_load_8 ,  i32, anyimm0, L2_loadrub_io>;
  defm: Loadxi_pat<atomic_load_16,  i32, anyimm1, L2_loadruh_io>;
  defm: Loadxi_pat<atomic_load_32,  i32, anyimm2, L2_loadri_io>;
  defm: Loadxi_pat<atomic_load_64,  i64, anyimm3, L2_loadrd_io>;
}

let AddedComplexity = 30 in {
  // Loads of i1 are loading a byte, and the byte should be either 0 or 1.
  // It doesn't matter if it's sign- or zero-extended, so use zero-extension
  // everywhere.
  defm: Loadxim_pat<sextloadi1,   i32, L1toI32,  anyimm0, L2_loadrub_io>;
  defm: Loadxim_pat<extloadi1,    i64, ToAext64, anyimm0, L2_loadrub_io>;
  defm: Loadxim_pat<sextloadi1,   i64, L1toI64,  anyimm0, L2_loadrub_io>;
  defm: Loadxim_pat<zextloadi1,   i64, ToZext64, anyimm0, L2_loadrub_io>;

  defm: Loadxim_pat<extloadi8,    i64, ToAext64, anyimm0, L2_loadrub_io>;
  defm: Loadxim_pat<extloadi16,   i64, ToAext64, anyimm1, L2_loadruh_io>;
  defm: Loadxim_pat<extloadi32,   i64, ToAext64, anyimm2, L2_loadri_io>;
  defm: Loadxim_pat<zextloadi8,   i64, ToZext64, anyimm0, L2_loadrub_io>;
  defm: Loadxim_pat<zextloadi16,  i64, ToZext64, anyimm1, L2_loadruh_io>;
  defm: Loadxim_pat<zextloadi32,  i64, ToZext64, anyimm2, L2_loadri_io>;
  defm: Loadxim_pat<sextloadi8,   i64, ToSext64, anyimm0, L2_loadrb_io>;
  defm: Loadxim_pat<sextloadi16,  i64, ToSext64, anyimm1, L2_loadrh_io>;
  defm: Loadxim_pat<sextloadi32,  i64, ToSext64, anyimm2, L2_loadri_io>;
}

let AddedComplexity  = 60 in {
  def: Loadxu_pat<extloadi1,    i32,   anyimm0, L4_loadrub_ur>;
  def: Loadxu_pat<extloadi8,    i32,   anyimm0, L4_loadrub_ur>;
  def: Loadxu_pat<extloadi16,   i32,   anyimm1, L4_loadruh_ur>;
  def: Loadxu_pat<extloadv2i8,  v2i16, anyimm1, L4_loadbzw2_ur>;
  def: Loadxu_pat<extloadv4i8,  v4i16, anyimm2, L4_loadbzw4_ur>;
  def: Loadxu_pat<sextloadi8,   i32,   anyimm0, L4_loadrb_ur>;
  def: Loadxu_pat<sextloadi16,  i32,   anyimm1, L4_loadrh_ur>;
  def: Loadxu_pat<sextloadv2i8, v2i16, anyimm1, L4_loadbsw2_ur>;
  def: Loadxu_pat<sextloadv4i8, v4i16, anyimm2, L4_loadbsw4_ur>;
  def: Loadxu_pat<zextloadi1,   i32,   anyimm0, L4_loadrub_ur>;
  def: Loadxu_pat<zextloadi8,   i32,   anyimm0, L4_loadrub_ur>;
  def: Loadxu_pat<zextloadi16,  i32,   anyimm1, L4_loadruh_ur>;
  def: Loadxu_pat<zextloadv2i8, v2i16, anyimm1, L4_loadbzw2_ur>;
  def: Loadxu_pat<zextloadv4i8, v4i16, anyimm2, L4_loadbzw4_ur>;
  def: Loadxu_pat<load,         i32,   anyimm2, L4_loadri_ur>;
  def: Loadxu_pat<load,         v2i16, anyimm2, L4_loadri_ur>;
  def: Loadxu_pat<load,         v4i8,  anyimm2, L4_loadri_ur>;
  def: Loadxu_pat<load,         i64,   anyimm3, L4_loadrd_ur>;
  def: Loadxu_pat<load,         v2i32, anyimm3, L4_loadrd_ur>;
  def: Loadxu_pat<load,         v4i16, anyimm3, L4_loadrd_ur>;
  def: Loadxu_pat<load,         v8i8,  anyimm3, L4_loadrd_ur>;
  def: Loadxu_pat<load,         f32,   anyimm2, L4_loadri_ur>;
  def: Loadxu_pat<load,         f64,   anyimm3, L4_loadrd_ur>;

  def: Loadxum_pat<sextloadi1,  i32, anyimm0, L1toI32,  L4_loadrub_ur>;
  def: Loadxum_pat<extloadi1,   i64, anyimm0, ToAext64, L4_loadrub_ur>;
  def: Loadxum_pat<sextloadi1,  i64, anyimm0, L1toI64,  L4_loadrub_ur>;
  def: Loadxum_pat<zextloadi1,  i64, anyimm0, ToZext64, L4_loadrub_ur>;

  def: Loadxum_pat<sextloadi8,  i64, anyimm0, ToSext64, L4_loadrb_ur>;
  def: Loadxum_pat<zextloadi8,  i64, anyimm0, ToZext64, L4_loadrub_ur>;
  def: Loadxum_pat<extloadi8,   i64, anyimm0, ToAext64, L4_loadrub_ur>;
  def: Loadxum_pat<sextloadi16, i64, anyimm1, ToSext64, L4_loadrh_ur>;
  def: Loadxum_pat<zextloadi16, i64, anyimm1, ToZext64, L4_loadruh_ur>;
  def: Loadxum_pat<extloadi16,  i64, anyimm1, ToAext64, L4_loadruh_ur>;
  def: Loadxum_pat<sextloadi32, i64, anyimm2, ToSext64, L4_loadri_ur>;
  def: Loadxum_pat<zextloadi32, i64, anyimm2, ToZext64, L4_loadri_ur>;
  def: Loadxum_pat<extloadi32,  i64, anyimm2, ToAext64, L4_loadri_ur>;
}

let AddedComplexity = 40 in {
  def: Loadxr_shl_pat<extloadi1,     i32,   L4_loadrub_rr>;
  def: Loadxr_shl_pat<extloadi8,     i32,   L4_loadrub_rr>;
  def: Loadxr_shl_pat<zextloadi1,    i32,   L4_loadrub_rr>;
  def: Loadxr_shl_pat<zextloadi8,    i32,   L4_loadrub_rr>;
  def: Loadxr_shl_pat<sextloadi8,    i32,   L4_loadrb_rr>;
  def: Loadxr_shl_pat<extloadi16,    i32,   L4_loadruh_rr>;
  def: Loadxr_shl_pat<zextloadi16,   i32,   L4_loadruh_rr>;
  def: Loadxr_shl_pat<sextloadi16,   i32,   L4_loadrh_rr>;
  def: Loadxr_shl_pat<load,          i32,   L4_loadri_rr>;
  def: Loadxr_shl_pat<load,          v2i16, L4_loadri_rr>;
  def: Loadxr_shl_pat<load,          v4i8,  L4_loadri_rr>;
  def: Loadxr_shl_pat<load,          i64,   L4_loadrd_rr>;
  def: Loadxr_shl_pat<load,          v2i32, L4_loadrd_rr>;
  def: Loadxr_shl_pat<load,          v4i16, L4_loadrd_rr>;
  def: Loadxr_shl_pat<load,          v8i8,  L4_loadrd_rr>;
  def: Loadxr_shl_pat<load,          f32,   L4_loadri_rr>;
  def: Loadxr_shl_pat<load,          f64,   L4_loadrd_rr>;
}

let AddedComplexity = 20 in {
  def: Loadxr_add_pat<extloadi1,     i32,   L4_loadrub_rr>;
  def: Loadxr_add_pat<extloadi8,     i32,   L4_loadrub_rr>;
  def: Loadxr_add_pat<zextloadi8,    i32,   L4_loadrub_rr>;
  def: Loadxr_add_pat<zextloadi1,    i32,   L4_loadrub_rr>;
  def: Loadxr_add_pat<sextloadi8,    i32,   L4_loadrb_rr>;
  def: Loadxr_add_pat<extloadi16,    i32,   L4_loadruh_rr>;
  def: Loadxr_add_pat<zextloadi16,   i32,   L4_loadruh_rr>;
  def: Loadxr_add_pat<sextloadi16,   i32,   L4_loadrh_rr>;
  def: Loadxr_add_pat<load,          i32,   L4_loadri_rr>;
  def: Loadxr_add_pat<load,          v2i16, L4_loadri_rr>;
  def: Loadxr_add_pat<load,          v4i8,  L4_loadri_rr>;
  def: Loadxr_add_pat<load,          i64,   L4_loadrd_rr>;
  def: Loadxr_add_pat<load,          v2i32, L4_loadrd_rr>;
  def: Loadxr_add_pat<load,          v4i16, L4_loadrd_rr>;
  def: Loadxr_add_pat<load,          v8i8,  L4_loadrd_rr>;
  def: Loadxr_add_pat<load,          f32,   L4_loadri_rr>;
  def: Loadxr_add_pat<load,          f64,   L4_loadrd_rr>;
}

let AddedComplexity = 40 in {
  def: Loadxrm_shl_pat<sextloadi1,   i32, L1toI32,  L4_loadrub_rr>;
  def: Loadxrm_shl_pat<extloadi1,    i64, ToAext64, L4_loadrub_rr>;
  def: Loadxrm_shl_pat<sextloadi1,   i64, L1toI64,  L4_loadrub_rr>;
  def: Loadxrm_shl_pat<zextloadi1,   i64, ToZext64, L4_loadrub_rr>;

  def: Loadxrm_shl_pat<extloadi8,    i64, ToAext64, L4_loadrub_rr>;
  def: Loadxrm_shl_pat<zextloadi8,   i64, ToZext64, L4_loadrub_rr>;
  def: Loadxrm_shl_pat<sextloadi8,   i64, ToSext64, L4_loadrb_rr>;
  def: Loadxrm_shl_pat<extloadi16,   i64, ToAext64, L4_loadruh_rr>;
  def: Loadxrm_shl_pat<zextloadi16,  i64, ToZext64, L4_loadruh_rr>;
  def: Loadxrm_shl_pat<sextloadi16,  i64, ToSext64, L4_loadrh_rr>;
  def: Loadxrm_shl_pat<extloadi32,   i64, ToAext64, L4_loadri_rr>;
  def: Loadxrm_shl_pat<zextloadi32,  i64, ToZext64, L4_loadri_rr>;
  def: Loadxrm_shl_pat<sextloadi32,  i64, ToSext64, L4_loadri_rr>;
}

let AddedComplexity = 30 in {
  def: Loadxrm_add_pat<sextloadi1,   i32, L1toI32,  L4_loadrub_rr>;
  def: Loadxrm_add_pat<extloadi1,    i64, ToAext64, L4_loadrub_rr>;
  def: Loadxrm_add_pat<sextloadi1,   i64, L1toI64,  L4_loadrub_rr>;
  def: Loadxrm_add_pat<zextloadi1,   i64, ToZext64, L4_loadrub_rr>;

  def: Loadxrm_add_pat<extloadi8,    i64, ToAext64, L4_loadrub_rr>;
  def: Loadxrm_add_pat<zextloadi8,   i64, ToZext64, L4_loadrub_rr>;
  def: Loadxrm_add_pat<sextloadi8,   i64, ToSext64, L4_loadrb_rr>;
  def: Loadxrm_add_pat<extloadi16,   i64, ToAext64, L4_loadruh_rr>;
  def: Loadxrm_add_pat<zextloadi16,  i64, ToZext64, L4_loadruh_rr>;
  def: Loadxrm_add_pat<sextloadi16,  i64, ToSext64, L4_loadrh_rr>;
  def: Loadxrm_add_pat<extloadi32,   i64, ToAext64, L4_loadri_rr>;
  def: Loadxrm_add_pat<zextloadi32,  i64, ToZext64, L4_loadri_rr>;
  def: Loadxrm_add_pat<sextloadi32,  i64, ToSext64, L4_loadri_rr>;
}

// Absolute address

let AddedComplexity  = 60 in {
  def: Loada_pat<extloadi1,       i32,   anyimm0, PS_loadrubabs>;
  def: Loada_pat<zextloadi1,      i32,   anyimm0, PS_loadrubabs>;
  def: Loada_pat<extloadi8,       i32,   anyimm0, PS_loadrubabs>;
  def: Loada_pat<sextloadi8,      i32,   anyimm0, PS_loadrbabs>;
  def: Loada_pat<zextloadi8,      i32,   anyimm0, PS_loadrubabs>;
  def: Loada_pat<extloadi16,      i32,   anyimm1, PS_loadruhabs>;
  def: Loada_pat<sextloadi16,     i32,   anyimm1, PS_loadrhabs>;
  def: Loada_pat<zextloadi16,     i32,   anyimm1, PS_loadruhabs>;
  def: Loada_pat<load,            i32,   anyimm2, PS_loadriabs>;
  def: Loada_pat<load,            v2i16, anyimm2, PS_loadriabs>;
  def: Loada_pat<load,            v4i8,  anyimm2, PS_loadriabs>;
  def: Loada_pat<load,            i64,   anyimm3, PS_loadrdabs>;
  def: Loada_pat<load,            v2i32, anyimm3, PS_loadrdabs>;
  def: Loada_pat<load,            v4i16, anyimm3, PS_loadrdabs>;
  def: Loada_pat<load,            v8i8,  anyimm3, PS_loadrdabs>;
  def: Loada_pat<load,            f32,   anyimm2, PS_loadriabs>;
  def: Loada_pat<load,            f64,   anyimm3, PS_loadrdabs>;

  def: Loada_pat<atomic_load_8,   i32, anyimm0, PS_loadrubabs>;
  def: Loada_pat<atomic_load_16,  i32, anyimm1, PS_loadruhabs>;
  def: Loada_pat<atomic_load_32,  i32, anyimm2, PS_loadriabs>;
  def: Loada_pat<atomic_load_64,  i64, anyimm3, PS_loadrdabs>;
}

let AddedComplexity  = 30 in {
  def: Loadam_pat<load,           i1,  anyimm0, I32toI1,  PS_loadrubabs>;
  def: Loadam_pat<sextloadi1,     i32, anyimm0, L1toI32,  PS_loadrubabs>;
  def: Loadam_pat<extloadi1,      i64, anyimm0, ToZext64, PS_loadrubabs>;
  def: Loadam_pat<sextloadi1,     i64, anyimm0, L1toI64,  PS_loadrubabs>;
  def: Loadam_pat<zextloadi1,     i64, anyimm0, ToZext64, PS_loadrubabs>;

  def: Loadam_pat<extloadi8,      i64, anyimm0, ToAext64, PS_loadrubabs>;
  def: Loadam_pat<sextloadi8,     i64, anyimm0, ToSext64, PS_loadrbabs>;
  def: Loadam_pat<zextloadi8,     i64, anyimm0, ToZext64, PS_loadrubabs>;
  def: Loadam_pat<extloadi16,     i64, anyimm1, ToAext64, PS_loadruhabs>;
  def: Loadam_pat<sextloadi16,    i64, anyimm1, ToSext64, PS_loadrhabs>;
  def: Loadam_pat<zextloadi16,    i64, anyimm1, ToZext64, PS_loadruhabs>;
  def: Loadam_pat<extloadi32,     i64, anyimm2, ToAext64, PS_loadriabs>;
  def: Loadam_pat<sextloadi32,    i64, anyimm2, ToSext64, PS_loadriabs>;
  def: Loadam_pat<zextloadi32,    i64, anyimm2, ToZext64, PS_loadriabs>;
}

// GP-relative address

let AddedComplexity  = 100 in {
  def: Loada_pat<extloadi1,       i32,   addrgp,  L2_loadrubgp>;
  def: Loada_pat<zextloadi1,      i32,   addrgp,  L2_loadrubgp>;
  def: Loada_pat<extloadi8,       i32,   addrgp,  L2_loadrubgp>;
  def: Loada_pat<sextloadi8,      i32,   addrgp,  L2_loadrbgp>;
  def: Loada_pat<zextloadi8,      i32,   addrgp,  L2_loadrubgp>;
  def: Loada_pat<extloadi16,      i32,   addrgp,  L2_loadruhgp>;
  def: Loada_pat<sextloadi16,     i32,   addrgp,  L2_loadrhgp>;
  def: Loada_pat<zextloadi16,     i32,   addrgp,  L2_loadruhgp>;
  def: Loada_pat<load,            i32,   addrgp,  L2_loadrigp>;
  def: Loada_pat<load,            v2i16, addrgp,  L2_loadrigp>;
  def: Loada_pat<load,            v4i8,  addrgp,  L2_loadrigp>;
  def: Loada_pat<load,            i64,   addrgp,  L2_loadrdgp>;
  def: Loada_pat<load,            v2i32, addrgp,  L2_loadrdgp>;
  def: Loada_pat<load,            v4i16, addrgp,  L2_loadrdgp>;
  def: Loada_pat<load,            v8i8,  addrgp,  L2_loadrdgp>;
  def: Loada_pat<load,            f32,   addrgp,  L2_loadrigp>;
  def: Loada_pat<load,            f64,   addrgp,  L2_loadrdgp>;

  def: Loada_pat<atomic_load_8,   i32, addrgp,  L2_loadrubgp>;
  def: Loada_pat<atomic_load_16,  i32, addrgp,  L2_loadruhgp>;
  def: Loada_pat<atomic_load_32,  i32, addrgp,  L2_loadrigp>;
  def: Loada_pat<atomic_load_64,  i64, addrgp,  L2_loadrdgp>;
}

let AddedComplexity  = 70 in {
  def: Loadam_pat<sextloadi1,     i32, addrgp,  L1toI32,  L2_loadrubgp>;
  def: Loadam_pat<extloadi1,      i64, addrgp,  ToAext64, L2_loadrubgp>;
  def: Loadam_pat<sextloadi1,     i64, addrgp,  L1toI64,  L2_loadrubgp>;
  def: Loadam_pat<zextloadi1,     i64, addrgp,  ToZext64, L2_loadrubgp>;

  def: Loadam_pat<extloadi8,      i64, addrgp,  ToAext64, L2_loadrubgp>;
  def: Loadam_pat<sextloadi8,     i64, addrgp,  ToSext64, L2_loadrbgp>;
  def: Loadam_pat<zextloadi8,     i64, addrgp,  ToZext64, L2_loadrubgp>;
  def: Loadam_pat<extloadi16,     i64, addrgp,  ToAext64, L2_loadruhgp>;
  def: Loadam_pat<sextloadi16,    i64, addrgp,  ToSext64, L2_loadrhgp>;
  def: Loadam_pat<zextloadi16,    i64, addrgp,  ToZext64, L2_loadruhgp>;
  def: Loadam_pat<extloadi32,     i64, addrgp,  ToAext64, L2_loadrigp>;
  def: Loadam_pat<sextloadi32,    i64, addrgp,  ToSext64, L2_loadrigp>;
  def: Loadam_pat<zextloadi32,    i64, addrgp,  ToZext64, L2_loadrigp>;

  def: Loadam_pat<load,           i1,  addrgp,  I32toI1,  L2_loadrubgp>;
}

// Patterns for loads of i1:
def: Pat<(i1 (load AddrFI:$fi)),
         (C2_tfrrp (L2_loadrub_io AddrFI:$fi, 0))>;
def: Pat<(i1 (load (add I32:$Rs, anyimm0:$Off))),
         (C2_tfrrp (L2_loadrub_io IntRegs:$Rs, imm:$Off))>;
def: Pat<(i1 (load I32:$Rs)),
         (C2_tfrrp (L2_loadrub_io IntRegs:$Rs, 0))>;


// --(13) Store ----------------------------------------------------------
//

class Storepi_pat<PatFrag Store, PatFrag Value, PatFrag Offset, InstHexagon MI>
  : Pat<(Store Value:$Rt, I32:$Rx, Offset:$s4),
        (MI I32:$Rx, imm:$s4, Value:$Rt)>;

def: Storepi_pat<post_truncsti8,  I32, s4_0ImmPred, S2_storerb_pi>;
def: Storepi_pat<post_truncsti16, I32, s4_1ImmPred, S2_storerh_pi>;
def: Storepi_pat<post_store,      I32, s4_2ImmPred, S2_storeri_pi>;
def: Storepi_pat<post_store,      I64, s4_3ImmPred, S2_storerd_pi>;

// Patterns for generating stores, where the address takes different forms:
// - frameindex,
// - frameindex + offset,
// - base + offset,
// - simple (base address without offset).
// These would usually be used together (via Storexi_pat defined below), but
// in some cases one may want to apply different properties (such as
// AddedComplexity) to the individual patterns.
class Storexi_fi_pat<PatFrag Store, PatFrag Value, InstHexagon MI>
  : Pat<(Store Value:$Rs, AddrFI:$fi), (MI AddrFI:$fi, 0, Value:$Rs)>;

multiclass Storexi_fi_add_pat<PatFrag Store, PatFrag Value, PatFrag ImmPred,
                              InstHexagon MI> {
  def: Pat<(Store Value:$Rs, (add (i32 AddrFI:$fi), ImmPred:$Off)),
           (MI AddrFI:$fi, imm:$Off, Value:$Rs)>;
  def: Pat<(Store Value:$Rs, (IsOrAdd (i32 AddrFI:$fi), ImmPred:$Off)),
           (MI AddrFI:$fi, imm:$Off, Value:$Rs)>;
}

multiclass Storexi_add_pat<PatFrag Store, PatFrag Value, PatFrag ImmPred,
                           InstHexagon MI> {
  def: Pat<(Store Value:$Rt, (add I32:$Rs, ImmPred:$Off)),
           (MI IntRegs:$Rs, imm:$Off, Value:$Rt)>;
  def: Pat<(Store Value:$Rt, (IsOrAdd I32:$Rs, ImmPred:$Off)),
           (MI IntRegs:$Rs, imm:$Off, Value:$Rt)>;
}

class Storexi_base_pat<PatFrag Store, PatFrag Value, InstHexagon MI>
  : Pat<(Store Value:$Rt, I32:$Rs),
        (MI IntRegs:$Rs, 0, Value:$Rt)>;

// Patterns for generating stores, where the address takes different forms,
// and where the value being stored is transformed through the value modifier
// ValueMod.  The address forms are same as above.
class Storexim_fi_pat<PatFrag Store, PatFrag Value, PatFrag ValueMod,
                      InstHexagon MI>
  : Pat<(Store Value:$Rs, AddrFI:$fi),
        (MI AddrFI:$fi, 0, (ValueMod Value:$Rs))>;

multiclass Storexim_fi_add_pat<PatFrag Store, PatFrag Value, PatFrag ImmPred,
                               PatFrag ValueMod, InstHexagon MI> {
  def: Pat<(Store Value:$Rs, (add (i32 AddrFI:$fi), ImmPred:$Off)),
           (MI AddrFI:$fi, imm:$Off, (ValueMod Value:$Rs))>;
  def: Pat<(Store Value:$Rs, (IsOrAdd (i32 AddrFI:$fi), ImmPred:$Off)),
           (MI AddrFI:$fi, imm:$Off, (ValueMod Value:$Rs))>;
}

multiclass Storexim_add_pat<PatFrag Store, PatFrag Value, PatFrag ImmPred,
                            PatFrag ValueMod, InstHexagon MI> {
  def: Pat<(Store Value:$Rt, (add I32:$Rs, ImmPred:$Off)),
           (MI IntRegs:$Rs, imm:$Off, (ValueMod Value:$Rt))>;
  def: Pat<(Store Value:$Rt, (IsOrAdd I32:$Rs, ImmPred:$Off)),
           (MI IntRegs:$Rs, imm:$Off, (ValueMod Value:$Rt))>;
}

class Storexim_base_pat<PatFrag Store, PatFrag Value, PatFrag ValueMod,
                        InstHexagon MI>
  : Pat<(Store Value:$Rt, I32:$Rs),
        (MI IntRegs:$Rs, 0, (ValueMod Value:$Rt))>;

multiclass Storexi_pat<PatFrag Store, PatFrag Value, PatLeaf ImmPred,
                       InstHexagon MI> {
  defm: Storexi_fi_add_pat <Store, Value, ImmPred, MI>;
  def:  Storexi_fi_pat     <Store, Value,          MI>;
  defm: Storexi_add_pat    <Store, Value, ImmPred, MI>;
}

multiclass Storexim_pat<PatFrag Store, PatFrag Value, PatLeaf ImmPred,
                        PatFrag ValueMod, InstHexagon MI> {
  defm: Storexim_fi_add_pat <Store, Value, ImmPred, ValueMod, MI>;
  def:  Storexim_fi_pat     <Store, Value,          ValueMod, MI>;
  defm: Storexim_add_pat    <Store, Value, ImmPred, ValueMod, MI>;
}

// Reg<<S + Imm
class Storexu_shl_pat<PatFrag Store, PatFrag Value, PatFrag ImmPred, InstHexagon MI>
  : Pat<(Store Value:$Rt, (add (shl I32:$Ru, u2_0ImmPred:$u2), ImmPred:$A)),
        (MI IntRegs:$Ru, imm:$u2, ImmPred:$A, Value:$Rt)>;

// Reg<<S + Reg
class Storexr_shl_pat<PatFrag Store, PatFrag Value, InstHexagon MI>
  : Pat<(Store Value:$Ru, (add I32:$Rs, (shl I32:$Rt, u2_0ImmPred:$u2))),
        (MI IntRegs:$Rs, IntRegs:$Rt, imm:$u2, Value:$Ru)>;

// Reg + Reg
class Storexr_add_pat<PatFrag Store, PatFrag Value, InstHexagon MI>
  : Pat<(Store Value:$Ru, (add I32:$Rs, I32:$Rt)),
        (MI IntRegs:$Rs, IntRegs:$Rt, 0, Value:$Ru)>;

class Storea_pat<PatFrag Store, PatFrag Value, PatFrag Addr, InstHexagon MI>
  : Pat<(Store Value:$val, Addr:$addr), (MI Addr:$addr, Value:$val)>;

class Stoream_pat<PatFrag Store, PatFrag Value, PatFrag Addr, PatFrag ValueMod,
                  InstHexagon MI>
  : Pat<(Store Value:$val, Addr:$addr),
        (MI Addr:$addr, (ValueMod Value:$val))>;

def IMM_BYTE : SDNodeXForm<imm, [{
  // -1 can be represented as 255, etc.
  // assigning to a byte restores our desired signed value.
  int8_t imm = N->getSExtValue();
  return CurDAG->getTargetConstant(imm, SDLoc(N), MVT::i32);
}]>;

def IMM_HALF : SDNodeXForm<imm, [{
  // -1 can be represented as 65535, etc.
  // assigning to a short restores our desired signed value.
  int16_t imm = N->getSExtValue();
  return CurDAG->getTargetConstant(imm, SDLoc(N), MVT::i32);
}]>;

def IMM_WORD : SDNodeXForm<imm, [{
  // -1 can be represented as 4294967295, etc.
  // Currently, it's not doing this. But some optimization
  // might convert -1 to a large +ve number.
  // assigning to a word restores our desired signed value.
  int32_t imm = N->getSExtValue();
  return CurDAG->getTargetConstant(imm, SDLoc(N), MVT::i32);
}]>;

def ToImmByte : OutPatFrag<(ops node:$R), (IMM_BYTE $R)>;
def ToImmHalf : OutPatFrag<(ops node:$R), (IMM_HALF $R)>;
def ToImmWord : OutPatFrag<(ops node:$R), (IMM_WORD $R)>;

// Even though the offset is not extendable in the store-immediate, we
// can still generate the fi# in the base address. If the final offset
// is not valid for the instruction, we will replace it with a scratch
// register.
class SmallStackStore<PatFrag Store>
  : PatFrag<(ops node:$Val, node:$Addr), (Store node:$Val, node:$Addr), [{
  return isSmallStackStore(cast<StoreSDNode>(N));
}]>;

// This is the complement of SmallStackStore.
class LargeStackStore<PatFrag Store>
  : PatFrag<(ops node:$Val, node:$Addr), (Store node:$Val, node:$Addr), [{
  return !isSmallStackStore(cast<StoreSDNode>(N));
}]>;

// Preferred addressing modes for various combinations of stored value
// and address computation.
// For stores where the address and value are both immediates, prefer
// store-immediate. The reason is that the constant-extender optimization
// can replace store-immediate with a store-register, but there is nothing
// to generate a store-immediate out of a store-register.
//
//         C     R     F    F+C   R+C   R+R   R<<S+C   R<<S+R
// --+-------+-----+-----+------+-----+-----+--------+--------
// C |   imm | imm | imm |  imm | imm |  rr |     ur |     rr
// R |  abs* |  io |  io |   io |  io |  rr |     ur |     rr
//
// (*) Absolute or GP-relative.
//
// Note that any expression can be matched by Reg. In particular, an immediate
// can always be placed in a register, so patterns checking for Imm should
// have a higher priority than the ones involving Reg that could also match.
// For example, *(p+4) could become r1=#4; memw(r0+r1<<#0) instead of the
// preferred memw(r0+#4). Similarly Reg+Imm or Reg+Reg should be tried before
// Reg alone.
//
// The order in which the different combinations are tried:
//
//         C     F     R    F+C   R+C   R+R   R<<S+C   R<<S+R
// --+-------+-----+-----+------+-----+-----+--------+--------
// C |     1 |   6 |   - |    5 |   9 |   - |      - |      -
// R |     2 |   8 |  12 |    7 |  10 |  11 |      3 |      4


// First, match the unusual case of doubleword store into Reg+Imm4, i.e.
// a store where the offset Imm4 is a multiple of 4, but not of 8. This
// implies that Reg is also a proper multiple of 4. To still generate a
// doubleword store, add 4 to Reg, and subtract 4 from the offset.

def s30_2ProperPred  : PatLeaf<(i32 imm), [{
  int64_t v = (int64_t)N->getSExtValue();
  return isShiftedInt<30,2>(v) && !isShiftedInt<29,3>(v);
}]>;
def RoundTo8 : SDNodeXForm<imm, [{
  int32_t Imm = N->getSExtValue();
  return CurDAG->getTargetConstant(Imm & -8, SDLoc(N), MVT::i32);
}]>;

let AddedComplexity = 150 in
def: Pat<(store I64:$Ru, (add I32:$Rs, s30_2ProperPred:$Off)),
         (S2_storerd_io (A2_addi I32:$Rs, 4), (RoundTo8 $Off), I64:$Ru)>;

class Storexi_abs_pat<PatFrag Store, PatFrag Value, InstHexagon MI>
  : Pat<(Store Value:$val, anyimm:$addr),
        (MI (ToI32 $addr), 0, Value:$val)>;
class Storexim_abs_pat<PatFrag Store, PatFrag Value, PatFrag ValueMod,
                       InstHexagon MI>
  : Pat<(Store Value:$val, anyimm:$addr),
        (MI (ToI32 $addr), 0, (ValueMod Value:$val))>;

let AddedComplexity = 140 in {
  def: Storexim_abs_pat<truncstorei8,  anyint, ToImmByte, S4_storeirb_io>;
  def: Storexim_abs_pat<truncstorei16, anyint, ToImmHalf, S4_storeirh_io>;
  def: Storexim_abs_pat<store,         anyint, ToImmWord, S4_storeiri_io>;

  def: Storexi_abs_pat<truncstorei8,  anyimm, S4_storeirb_io>;
  def: Storexi_abs_pat<truncstorei16, anyimm, S4_storeirh_io>;
  def: Storexi_abs_pat<store,         anyimm, S4_storeiri_io>;
}

// GP-relative address
let AddedComplexity = 120 in {
  def: Storea_pat<truncstorei8,               I32, addrgp, S2_storerbgp>;
  def: Storea_pat<truncstorei16,              I32, addrgp, S2_storerhgp>;
  def: Storea_pat<store,                      I32, addrgp, S2_storerigp>;
  def: Storea_pat<store,                     V4I8, addrgp, S2_storerigp>;
  def: Storea_pat<store,                    V2I16, addrgp, S2_storerigp>;
  def: Storea_pat<store,                      I64, addrgp, S2_storerdgp>;
  def: Storea_pat<store,                     V8I8, addrgp, S2_storerdgp>;
  def: Storea_pat<store,                    V4I16, addrgp, S2_storerdgp>;
  def: Storea_pat<store,                    V2I32, addrgp, S2_storerdgp>;
  def: Storea_pat<store,                      F32, addrgp, S2_storerigp>;
  def: Storea_pat<store,                      F64, addrgp, S2_storerdgp>;
  def: Storea_pat<atomic_store_8,             I32, addrgp, S2_storerbgp>;
  def: Storea_pat<atomic_store_16,            I32, addrgp, S2_storerhgp>;
  def: Storea_pat<atomic_store_32,            I32, addrgp, S2_storerigp>;
  def: Storea_pat<atomic_store_32,           V4I8, addrgp, S2_storerigp>;
  def: Storea_pat<atomic_store_32,          V2I16, addrgp, S2_storerigp>;
  def: Storea_pat<atomic_store_64,            I64, addrgp, S2_storerdgp>;
  def: Storea_pat<atomic_store_64,           V8I8, addrgp, S2_storerdgp>;
  def: Storea_pat<atomic_store_64,          V4I16, addrgp, S2_storerdgp>;
  def: Storea_pat<atomic_store_64,          V2I32, addrgp, S2_storerdgp>;

  def: Stoream_pat<truncstorei8,  I64, addrgp, LoReg,    S2_storerbgp>;
  def: Stoream_pat<truncstorei16, I64, addrgp, LoReg,    S2_storerhgp>;
  def: Stoream_pat<truncstorei32, I64, addrgp, LoReg,    S2_storerigp>;
  def: Stoream_pat<store,         I1,  addrgp, I1toI32,  S2_storerbgp>;
}

// Absolute address
let AddedComplexity = 110 in {
  def: Storea_pat<truncstorei8,               I32, anyimm0, PS_storerbabs>;
  def: Storea_pat<truncstorei16,              I32, anyimm1, PS_storerhabs>;
  def: Storea_pat<store,                      I32, anyimm2, PS_storeriabs>;
  def: Storea_pat<store,                     V4I8, anyimm2, PS_storeriabs>;
  def: Storea_pat<store,                    V2I16, anyimm2, PS_storeriabs>;
  def: Storea_pat<store,                      I64, anyimm3, PS_storerdabs>;
  def: Storea_pat<store,                     V8I8, anyimm3, PS_storerdabs>;
  def: Storea_pat<store,                    V4I16, anyimm3, PS_storerdabs>;
  def: Storea_pat<store,                    V2I32, anyimm3, PS_storerdabs>;
  def: Storea_pat<store,                      F32, anyimm2, PS_storeriabs>;
  def: Storea_pat<store,                      F64, anyimm3, PS_storerdabs>;
  def: Storea_pat<atomic_store_8,             I32, anyimm0, PS_storerbabs>;
  def: Storea_pat<atomic_store_16,            I32, anyimm1, PS_storerhabs>;
  def: Storea_pat<atomic_store_32,            I32, anyimm2, PS_storeriabs>;
  def: Storea_pat<atomic_store_32,           V4I8, anyimm2, PS_storeriabs>;
  def: Storea_pat<atomic_store_32,          V2I16, anyimm2, PS_storeriabs>;
  def: Storea_pat<atomic_store_64,            I64, anyimm3, PS_storerdabs>;
  def: Storea_pat<atomic_store_64,           V8I8, anyimm3, PS_storerdabs>;
  def: Storea_pat<atomic_store_64,          V4I16, anyimm3, PS_storerdabs>;
  def: Storea_pat<atomic_store_64,          V2I32, anyimm3, PS_storerdabs>;

  def: Stoream_pat<truncstorei8,  I64, anyimm0, LoReg,    PS_storerbabs>;
  def: Stoream_pat<truncstorei16, I64, anyimm1, LoReg,    PS_storerhabs>;
  def: Stoream_pat<truncstorei32, I64, anyimm2, LoReg,    PS_storeriabs>;
  def: Stoream_pat<store,         I1,  anyimm0, I1toI32,  PS_storerbabs>;
}

// Reg<<S + Imm
let AddedComplexity = 100 in {
  def: Storexu_shl_pat<truncstorei8,    I32, anyimm0, S4_storerb_ur>;
  def: Storexu_shl_pat<truncstorei16,   I32, anyimm1, S4_storerh_ur>;
  def: Storexu_shl_pat<store,           I32, anyimm2, S4_storeri_ur>;
  def: Storexu_shl_pat<store,          V4I8, anyimm2, S4_storeri_ur>;
  def: Storexu_shl_pat<store,         V2I16, anyimm2, S4_storeri_ur>;
  def: Storexu_shl_pat<store,           I64, anyimm3, S4_storerd_ur>;
  def: Storexu_shl_pat<store,          V8I8, anyimm3, S4_storerd_ur>;
  def: Storexu_shl_pat<store,         V4I16, anyimm3, S4_storerd_ur>;
  def: Storexu_shl_pat<store,         V2I32, anyimm3, S4_storerd_ur>;
  def: Storexu_shl_pat<store,           F32, anyimm2, S4_storeri_ur>;
  def: Storexu_shl_pat<store,           F64, anyimm3, S4_storerd_ur>;

  def: Pat<(store I1:$Pu, (add (shl I32:$Rs, u2_0ImmPred:$u2), anyimm:$A)),
           (S4_storerb_ur IntRegs:$Rs, imm:$u2, imm:$A, (I1toI32 I1:$Pu))>;
}

// Reg<<S + Reg
let AddedComplexity = 90 in {
  def: Storexr_shl_pat<truncstorei8,    I32, S4_storerb_rr>;
  def: Storexr_shl_pat<truncstorei16,   I32, S4_storerh_rr>;
  def: Storexr_shl_pat<store,           I32, S4_storeri_rr>;
  def: Storexr_shl_pat<store,          V4I8, S4_storeri_rr>;
  def: Storexr_shl_pat<store,         V2I16, S4_storeri_rr>;
  def: Storexr_shl_pat<store,           I64, S4_storerd_rr>;
  def: Storexr_shl_pat<store,          V8I8, S4_storerd_rr>;
  def: Storexr_shl_pat<store,         V4I16, S4_storerd_rr>;
  def: Storexr_shl_pat<store,         V2I32, S4_storerd_rr>;
  def: Storexr_shl_pat<store,           F32, S4_storeri_rr>;
  def: Storexr_shl_pat<store,           F64, S4_storerd_rr>;

  def: Pat<(store I1:$Pu, (add (shl I32:$Rs, u2_0ImmPred:$u2), I32:$Rt)),
           (S4_storerb_rr IntRegs:$Rt, IntRegs:$Rs, imm:$u2, (I1toI32 I1:$Pu))>;
}

class SS_<PatFrag F> : SmallStackStore<F>;
class LS_<PatFrag F> : LargeStackStore<F>;

multiclass IMFA_<PatFrag S, PatFrag V, PatFrag O, PatFrag M, InstHexagon I> {
  defm: Storexim_fi_add_pat<S, V, O, M, I>;
}
multiclass IFA_<PatFrag S, PatFrag V, PatFrag O, InstHexagon I> {
  defm: Storexi_fi_add_pat<S, V, O, I>;
}

// Fi+Imm, store-immediate
let AddedComplexity = 80 in {
  defm: IMFA_<SS_<truncstorei8>,  anyint, u6_0ImmPred, ToImmByte, S4_storeirb_io>;
  defm: IMFA_<SS_<truncstorei16>, anyint, u6_1ImmPred, ToImmHalf, S4_storeirh_io>;
  defm: IMFA_<SS_<store>,         anyint, u6_2ImmPred, ToImmWord, S4_storeiri_io>;

  defm: IFA_<SS_<truncstorei8>,   anyimm, u6_0ImmPred, S4_storeirb_io>;
  defm: IFA_<SS_<truncstorei16>,  anyimm, u6_1ImmPred, S4_storeirh_io>;
  defm: IFA_<SS_<store>,          anyimm, u6_2ImmPred, S4_storeiri_io>;

  // For large-stack stores, generate store-register (prefer explicit Fi
  // in the address).
  defm: IMFA_<LS_<truncstorei8>,   anyimm, u6_0ImmPred, ToI32, S2_storerb_io>;
  defm: IMFA_<LS_<truncstorei16>,  anyimm, u6_1ImmPred, ToI32, S2_storerh_io>;
  defm: IMFA_<LS_<store>,          anyimm, u6_2ImmPred, ToI32, S2_storeri_io>;
}

// Fi, store-immediate
let AddedComplexity = 70 in {
  def: Storexim_fi_pat<SS_<truncstorei8>,  anyint, ToImmByte, S4_storeirb_io>;
  def: Storexim_fi_pat<SS_<truncstorei16>, anyint, ToImmHalf, S4_storeirh_io>;
  def: Storexim_fi_pat<SS_<store>,         anyint, ToImmWord, S4_storeiri_io>;

  def: Storexi_fi_pat<SS_<truncstorei8>,   anyimm, S4_storeirb_io>;
  def: Storexi_fi_pat<SS_<truncstorei16>,  anyimm, S4_storeirh_io>;
  def: Storexi_fi_pat<SS_<store>,          anyimm, S4_storeiri_io>;

  // For large-stack stores, generate store-register (prefer explicit Fi
  // in the address).
  def: Storexim_fi_pat<LS_<truncstorei8>,  anyimm, ToI32, S2_storerb_io>;
  def: Storexim_fi_pat<LS_<truncstorei16>, anyimm, ToI32, S2_storerh_io>;
  def: Storexim_fi_pat<LS_<store>,         anyimm, ToI32, S2_storeri_io>;
}

// Fi+Imm, Fi, store-register
let AddedComplexity = 60 in {
  defm: Storexi_fi_add_pat<truncstorei8,    I32, anyimm, S2_storerb_io>;
  defm: Storexi_fi_add_pat<truncstorei16,   I32, anyimm, S2_storerh_io>;
  defm: Storexi_fi_add_pat<store,           I32, anyimm, S2_storeri_io>;
  defm: Storexi_fi_add_pat<store,          V4I8, anyimm, S2_storeri_io>;
  defm: Storexi_fi_add_pat<store,         V2I16, anyimm, S2_storeri_io>;
  defm: Storexi_fi_add_pat<store,           I64, anyimm, S2_storerd_io>;
  defm: Storexi_fi_add_pat<store,          V8I8, anyimm, S2_storerd_io>;
  defm: Storexi_fi_add_pat<store,         V4I16, anyimm, S2_storerd_io>;
  defm: Storexi_fi_add_pat<store,         V2I32, anyimm, S2_storerd_io>;
  defm: Storexi_fi_add_pat<store,           F32, anyimm, S2_storeri_io>;
  defm: Storexi_fi_add_pat<store,           F64, anyimm, S2_storerd_io>;
  defm: Storexim_fi_add_pat<store, I1, anyimm, I1toI32, S2_storerb_io>;

  def: Storexi_fi_pat<truncstorei8,     I32, S2_storerb_io>;
  def: Storexi_fi_pat<truncstorei16,    I32, S2_storerh_io>;
  def: Storexi_fi_pat<store,            I32, S2_storeri_io>;
  def: Storexi_fi_pat<store,           V4I8, S2_storeri_io>;
  def: Storexi_fi_pat<store,          V2I16, S2_storeri_io>;
  def: Storexi_fi_pat<store,            I64, S2_storerd_io>;
  def: Storexi_fi_pat<store,           V8I8, S2_storerd_io>;
  def: Storexi_fi_pat<store,          V4I16, S2_storerd_io>;
  def: Storexi_fi_pat<store,          V2I32, S2_storerd_io>;
  def: Storexi_fi_pat<store,            F32, S2_storeri_io>;
  def: Storexi_fi_pat<store,            F64, S2_storerd_io>;
  def: Storexim_fi_pat<store, I1, I1toI32, S2_storerb_io>;
}


multiclass IMRA_<PatFrag S, PatFrag V, PatFrag O, PatFrag M, InstHexagon I> {
  defm: Storexim_add_pat<S, V, O, M, I>;
}
multiclass IRA_<PatFrag S, PatFrag V, PatFrag O, InstHexagon I> {
  defm: Storexi_add_pat<S, V, O, I>;
}

// Reg+Imm, store-immediate
let AddedComplexity = 50 in {
  defm: IMRA_<truncstorei8,   anyint, u6_0ImmPred, ToImmByte, S4_storeirb_io>;
  defm: IMRA_<truncstorei16,  anyint, u6_1ImmPred, ToImmHalf, S4_storeirh_io>;
  defm: IMRA_<store,          anyint, u6_2ImmPred, ToImmWord, S4_storeiri_io>;

  defm: IRA_<truncstorei8,    anyimm, u6_0ImmPred, S4_storeirb_io>;
  defm: IRA_<truncstorei16,   anyimm, u6_1ImmPred, S4_storeirh_io>;
  defm: IRA_<store,           anyimm, u6_2ImmPred, S4_storeiri_io>;
}

// Reg+Imm, store-register
let AddedComplexity = 40 in {
  defm: Storexi_pat<truncstorei8,     I32, anyimm0, S2_storerb_io>;
  defm: Storexi_pat<truncstorei16,    I32, anyimm1, S2_storerh_io>;
  defm: Storexi_pat<store,            I32, anyimm2, S2_storeri_io>;
  defm: Storexi_pat<store,           V4I8, anyimm2, S2_storeri_io>;
  defm: Storexi_pat<store,          V2I16, anyimm2, S2_storeri_io>;
  defm: Storexi_pat<store,            I64, anyimm3, S2_storerd_io>;
  defm: Storexi_pat<store,           V8I8, anyimm3, S2_storerd_io>;
  defm: Storexi_pat<store,          V4I16, anyimm3, S2_storerd_io>;
  defm: Storexi_pat<store,          V2I32, anyimm3, S2_storerd_io>;
  defm: Storexi_pat<store,            F32, anyimm2, S2_storeri_io>;
  defm: Storexi_pat<store,            F64, anyimm3, S2_storerd_io>;

  defm: Storexim_pat<truncstorei8,  I64, anyimm0, LoReg,   S2_storerb_io>;
  defm: Storexim_pat<truncstorei16, I64, anyimm1, LoReg,   S2_storerh_io>;
  defm: Storexim_pat<truncstorei32, I64, anyimm2, LoReg,   S2_storeri_io>;
  defm: Storexim_pat<store,         I1,  anyimm0, I1toI32, S2_storerb_io>;

  defm: Storexi_pat<atomic_store_8,     I32, anyimm0, S2_storerb_io>;
  defm: Storexi_pat<atomic_store_16,    I32, anyimm1, S2_storerh_io>;
  defm: Storexi_pat<atomic_store_32,    I32, anyimm2, S2_storeri_io>;
  defm: Storexi_pat<atomic_store_32,   V4I8, anyimm2, S2_storeri_io>;
  defm: Storexi_pat<atomic_store_32,  V2I16, anyimm2, S2_storeri_io>;
  defm: Storexi_pat<atomic_store_64,    I64, anyimm3, S2_storerd_io>;
  defm: Storexi_pat<atomic_store_64,   V8I8, anyimm3, S2_storerd_io>;
  defm: Storexi_pat<atomic_store_64,  V4I16, anyimm3, S2_storerd_io>;
  defm: Storexi_pat<atomic_store_64,  V2I32, anyimm3, S2_storerd_io>;
}

// Reg+Reg
let AddedComplexity = 30 in {
  def: Storexr_add_pat<truncstorei8,    I32, S4_storerb_rr>;
  def: Storexr_add_pat<truncstorei16,   I32, S4_storerh_rr>;
  def: Storexr_add_pat<store,           I32, S4_storeri_rr>;
  def: Storexr_add_pat<store,          V4I8, S4_storeri_rr>;
  def: Storexr_add_pat<store,         V2I16, S4_storeri_rr>;
  def: Storexr_add_pat<store,           I64, S4_storerd_rr>;
  def: Storexr_add_pat<store,          V8I8, S4_storerd_rr>;
  def: Storexr_add_pat<store,         V4I16, S4_storerd_rr>;
  def: Storexr_add_pat<store,         V2I32, S4_storerd_rr>;
  def: Storexr_add_pat<store,           F32, S4_storeri_rr>;
  def: Storexr_add_pat<store,           F64, S4_storerd_rr>;

  def: Pat<(store I1:$Pu, (add I32:$Rs, I32:$Rt)),
           (S4_storerb_rr IntRegs:$Rs, IntRegs:$Rt, 0, (I1toI32 I1:$Pu))>;
}

// Reg, store-immediate
let AddedComplexity = 20 in {
  def: Storexim_base_pat<truncstorei8,  anyint, ToImmByte, S4_storeirb_io>;
  def: Storexim_base_pat<truncstorei16, anyint, ToImmHalf, S4_storeirh_io>;
  def: Storexim_base_pat<store,         anyint, ToImmWord, S4_storeiri_io>;

  def: Storexi_base_pat<truncstorei8,   anyimm, S4_storeirb_io>;
  def: Storexi_base_pat<truncstorei16,  anyimm, S4_storeirh_io>;
  def: Storexi_base_pat<store,          anyimm, S4_storeiri_io>;
}

// Reg, store-register
let AddedComplexity = 10 in {
  def: Storexi_base_pat<truncstorei8,     I32, S2_storerb_io>;
  def: Storexi_base_pat<truncstorei16,    I32, S2_storerh_io>;
  def: Storexi_base_pat<store,            I32, S2_storeri_io>;
  def: Storexi_base_pat<store,           V4I8, S2_storeri_io>;
  def: Storexi_base_pat<store,          V2I16, S2_storeri_io>;
  def: Storexi_base_pat<store,            I64, S2_storerd_io>;
  def: Storexi_base_pat<store,           V8I8, S2_storerd_io>;
  def: Storexi_base_pat<store,          V4I16, S2_storerd_io>;
  def: Storexi_base_pat<store,          V2I32, S2_storerd_io>;
  def: Storexi_base_pat<store,            F32, S2_storeri_io>;
  def: Storexi_base_pat<store,            F64, S2_storerd_io>;

  def: Storexim_base_pat<truncstorei8,  I64, LoReg,   S2_storerb_io>;
  def: Storexim_base_pat<truncstorei16, I64, LoReg,   S2_storerh_io>;
  def: Storexim_base_pat<truncstorei32, I64, LoReg,   S2_storeri_io>;
  def: Storexim_base_pat<store,         I1,  I1toI32, S2_storerb_io>;

  def: Storexi_base_pat<atomic_store_8,     I32, S2_storerb_io>;
  def: Storexi_base_pat<atomic_store_16,    I32, S2_storerh_io>;
  def: Storexi_base_pat<atomic_store_32,    I32, S2_storeri_io>;
  def: Storexi_base_pat<atomic_store_32,   V4I8, S2_storeri_io>;
  def: Storexi_base_pat<atomic_store_32,  V2I16, S2_storeri_io>;
  def: Storexi_base_pat<atomic_store_64,    I64, S2_storerd_io>;
  def: Storexi_base_pat<atomic_store_64,   V8I8, S2_storerd_io>;
  def: Storexi_base_pat<atomic_store_64,  V4I16, S2_storerd_io>;
  def: Storexi_base_pat<atomic_store_64,  V2I32, S2_storerd_io>;
}


// --(14) Memop ----------------------------------------------------------
//

def m5_0Imm8Pred : PatLeaf<(i32 imm), [{
  int8_t V = N->getSExtValue();
  return -32 < V && V <= -1;
}]>;

def m5_0Imm16Pred : PatLeaf<(i32 imm), [{
  int16_t V = N->getSExtValue();
  return -32 < V && V <= -1;
}]>;

def m5_0ImmPred  : PatLeaf<(i32 imm), [{
  int64_t V = N->getSExtValue();
  return -31 <= V && V <= -1;
}]>;

def IsNPow2_8 : PatLeaf<(i32 imm), [{
  uint8_t NV = ~N->getZExtValue();
  return isPowerOf2_32(NV);
}]>;

def IsNPow2_16 : PatLeaf<(i32 imm), [{
  uint16_t NV = ~N->getZExtValue();
  return isPowerOf2_32(NV);
}]>;

def Log2_8 : SDNodeXForm<imm, [{
  uint8_t V = N->getZExtValue();
  return CurDAG->getTargetConstant(Log2_32(V), SDLoc(N), MVT::i32);
}]>;

def Log2_16 : SDNodeXForm<imm, [{
  uint16_t V = N->getZExtValue();
  return CurDAG->getTargetConstant(Log2_32(V), SDLoc(N), MVT::i32);
}]>;

def LogN2_8 : SDNodeXForm<imm, [{
  uint8_t NV = ~N->getZExtValue();
  return CurDAG->getTargetConstant(Log2_32(NV), SDLoc(N), MVT::i32);
}]>;

def LogN2_16 : SDNodeXForm<imm, [{
  uint16_t NV = ~N->getZExtValue();
  return CurDAG->getTargetConstant(Log2_32(NV), SDLoc(N), MVT::i32);
}]>;

def IdImm : SDNodeXForm<imm, [{ return SDValue(N, 0); }]>;

multiclass Memopxr_base_pat<PatFrag Load, PatFrag Store, SDNode Oper,
                            InstHexagon MI> {
  // Addr: i32
  def: Pat<(Store (Oper (Load I32:$Rs), I32:$A), I32:$Rs),
           (MI I32:$Rs, 0, I32:$A)>;
  // Addr: fi
  def: Pat<(Store (Oper (Load AddrFI:$Rs), I32:$A), AddrFI:$Rs),
           (MI AddrFI:$Rs, 0, I32:$A)>;
}

multiclass Memopxr_add_pat<PatFrag Load, PatFrag Store, PatFrag ImmPred,
                           SDNode Oper, InstHexagon MI> {
  // Addr: i32
  def: Pat<(Store (Oper (Load (add I32:$Rs, ImmPred:$Off)), I32:$A),
                  (add I32:$Rs, ImmPred:$Off)),
           (MI I32:$Rs, imm:$Off, I32:$A)>;
  def: Pat<(Store (Oper (Load (IsOrAdd I32:$Rs, ImmPred:$Off)), I32:$A),
                  (IsOrAdd I32:$Rs, ImmPred:$Off)),
           (MI I32:$Rs, imm:$Off, I32:$A)>;
  // Addr: fi
  def: Pat<(Store (Oper (Load (add AddrFI:$Rs, ImmPred:$Off)), I32:$A),
                  (add AddrFI:$Rs, ImmPred:$Off)),
           (MI AddrFI:$Rs, imm:$Off, I32:$A)>;
  def: Pat<(Store (Oper (Load (IsOrAdd AddrFI:$Rs, ImmPred:$Off)), I32:$A),
                  (IsOrAdd AddrFI:$Rs, ImmPred:$Off)),
           (MI AddrFI:$Rs, imm:$Off, I32:$A)>;
}

multiclass Memopxr_pat<PatFrag Load, PatFrag Store, PatFrag ImmPred,
                       SDNode Oper, InstHexagon MI> {
  let Predicates = [UseMEMOPS] in {
    defm: Memopxr_base_pat <Load, Store,          Oper, MI>;
    defm: Memopxr_add_pat  <Load, Store, ImmPred, Oper, MI>;
  }
}

let AddedComplexity = 200 in {
  // add reg
  defm: Memopxr_pat<extloadi8, truncstorei8, u6_0ImmPred, add,
        /*anyext*/  L4_add_memopb_io>;
  defm: Memopxr_pat<sextloadi8, truncstorei8, u6_0ImmPred, add,
        /*sext*/    L4_add_memopb_io>;
  defm: Memopxr_pat<zextloadi8, truncstorei8, u6_0ImmPred, add,
        /*zext*/    L4_add_memopb_io>;
  defm: Memopxr_pat<extloadi16, truncstorei16, u6_1ImmPred, add,
        /*anyext*/  L4_add_memoph_io>;
  defm: Memopxr_pat<sextloadi16, truncstorei16, u6_1ImmPred, add,
        /*sext*/    L4_add_memoph_io>;
  defm: Memopxr_pat<zextloadi16, truncstorei16, u6_1ImmPred, add,
        /*zext*/    L4_add_memoph_io>;
  defm: Memopxr_pat<load, store, u6_2ImmPred, add, L4_add_memopw_io>;

  // sub reg
  defm: Memopxr_pat<extloadi8, truncstorei8, u6_0ImmPred, sub,
        /*anyext*/  L4_sub_memopb_io>;
  defm: Memopxr_pat<sextloadi8, truncstorei8, u6_0ImmPred, sub,
        /*sext*/    L4_sub_memopb_io>;
  defm: Memopxr_pat<zextloadi8, truncstorei8, u6_0ImmPred, sub,
        /*zext*/    L4_sub_memopb_io>;
  defm: Memopxr_pat<extloadi16, truncstorei16, u6_1ImmPred, sub,
        /*anyext*/  L4_sub_memoph_io>;
  defm: Memopxr_pat<sextloadi16, truncstorei16, u6_1ImmPred, sub,
        /*sext*/    L4_sub_memoph_io>;
  defm: Memopxr_pat<zextloadi16, truncstorei16, u6_1ImmPred, sub,
        /*zext*/    L4_sub_memoph_io>;
  defm: Memopxr_pat<load, store, u6_2ImmPred, sub, L4_sub_memopw_io>;

  // and reg
  defm: Memopxr_pat<extloadi8, truncstorei8, u6_0ImmPred, and,
        /*anyext*/  L4_and_memopb_io>;
  defm: Memopxr_pat<sextloadi8, truncstorei8, u6_0ImmPred, and,
        /*sext*/    L4_and_memopb_io>;
  defm: Memopxr_pat<zextloadi8, truncstorei8, u6_0ImmPred, and,
        /*zext*/    L4_and_memopb_io>;
  defm: Memopxr_pat<extloadi16, truncstorei16, u6_1ImmPred, and,
        /*anyext*/  L4_and_memoph_io>;
  defm: Memopxr_pat<sextloadi16, truncstorei16, u6_1ImmPred, and,
        /*sext*/    L4_and_memoph_io>;
  defm: Memopxr_pat<zextloadi16, truncstorei16, u6_1ImmPred, and,
        /*zext*/    L4_and_memoph_io>;
  defm: Memopxr_pat<load, store, u6_2ImmPred, and, L4_and_memopw_io>;

  // or reg
  defm: Memopxr_pat<extloadi8, truncstorei8, u6_0ImmPred, or,
        /*anyext*/  L4_or_memopb_io>;
  defm: Memopxr_pat<sextloadi8, truncstorei8, u6_0ImmPred, or,
        /*sext*/    L4_or_memopb_io>;
  defm: Memopxr_pat<zextloadi8, truncstorei8, u6_0ImmPred, or,
        /*zext*/    L4_or_memopb_io>;
  defm: Memopxr_pat<extloadi16, truncstorei16, u6_1ImmPred, or,
        /*anyext*/  L4_or_memoph_io>;
  defm: Memopxr_pat<sextloadi16, truncstorei16, u6_1ImmPred, or,
        /*sext*/    L4_or_memoph_io>;
  defm: Memopxr_pat<zextloadi16, truncstorei16, u6_1ImmPred, or,
        /*zext*/    L4_or_memoph_io>;
  defm: Memopxr_pat<load, store, u6_2ImmPred, or, L4_or_memopw_io>;
}


multiclass Memopxi_base_pat<PatFrag Load, PatFrag Store, SDNode Oper,
                            PatFrag Arg, SDNodeXForm ArgMod, InstHexagon MI> {
  // Addr: i32
  def: Pat<(Store (Oper (Load I32:$Rs), Arg:$A), I32:$Rs),
           (MI I32:$Rs, 0, (ArgMod Arg:$A))>;
  // Addr: fi
  def: Pat<(Store (Oper (Load AddrFI:$Rs), Arg:$A), AddrFI:$Rs),
           (MI AddrFI:$Rs, 0, (ArgMod Arg:$A))>;
}

multiclass Memopxi_add_pat<PatFrag Load, PatFrag Store, PatFrag ImmPred,
                           SDNode Oper, PatFrag Arg, SDNodeXForm ArgMod,
                           InstHexagon MI> {
  // Addr: i32
  def: Pat<(Store (Oper (Load (add I32:$Rs, ImmPred:$Off)), Arg:$A),
                  (add I32:$Rs, ImmPred:$Off)),
           (MI I32:$Rs, imm:$Off, (ArgMod Arg:$A))>;
  def: Pat<(Store (Oper (Load (IsOrAdd I32:$Rs, ImmPred:$Off)), Arg:$A),
                  (IsOrAdd I32:$Rs, ImmPred:$Off)),
           (MI I32:$Rs, imm:$Off, (ArgMod Arg:$A))>;
  // Addr: fi
  def: Pat<(Store (Oper (Load (add AddrFI:$Rs, ImmPred:$Off)), Arg:$A),
                  (add AddrFI:$Rs, ImmPred:$Off)),
           (MI AddrFI:$Rs, imm:$Off, (ArgMod Arg:$A))>;
  def: Pat<(Store (Oper (Load (IsOrAdd AddrFI:$Rs, ImmPred:$Off)), Arg:$A),
                  (IsOrAdd AddrFI:$Rs, ImmPred:$Off)),
           (MI AddrFI:$Rs, imm:$Off, (ArgMod Arg:$A))>;
}

multiclass Memopxi_pat<PatFrag Load, PatFrag Store, PatFrag ImmPred,
                       SDNode Oper, PatFrag Arg, SDNodeXForm ArgMod,
                       InstHexagon MI> {
  let Predicates = [UseMEMOPS] in {
    defm: Memopxi_base_pat <Load, Store,          Oper, Arg, ArgMod, MI>;
    defm: Memopxi_add_pat  <Load, Store, ImmPred, Oper, Arg, ArgMod, MI>;
  }
}

let AddedComplexity = 220 in {
  // add imm
  defm: Memopxi_pat<extloadi8, truncstorei8, u6_0ImmPred, add, u5_0ImmPred,
        /*anyext*/  IdImm, L4_iadd_memopb_io>;
  defm: Memopxi_pat<sextloadi8, truncstorei8, u6_0ImmPred, add, u5_0ImmPred,
        /*sext*/    IdImm, L4_iadd_memopb_io>;
  defm: Memopxi_pat<zextloadi8, truncstorei8, u6_0ImmPred, add, u5_0ImmPred,
        /*zext*/    IdImm, L4_iadd_memopb_io>;
  defm: Memopxi_pat<extloadi16, truncstorei16, u6_1ImmPred, add, u5_0ImmPred,
        /*anyext*/  IdImm, L4_iadd_memoph_io>;
  defm: Memopxi_pat<extloadi16, truncstorei16, u6_1ImmPred, add, u5_0ImmPred,
        /*sext*/    IdImm, L4_iadd_memoph_io>;
  defm: Memopxi_pat<extloadi16, truncstorei16, u6_1ImmPred, add, u5_0ImmPred,
        /*zext*/    IdImm, L4_iadd_memoph_io>;
  defm: Memopxi_pat<load, store, u6_2ImmPred, add, u5_0ImmPred, IdImm,
                    L4_iadd_memopw_io>;
  defm: Memopxi_pat<extloadi8, truncstorei8, u6_0ImmPred, sub, m5_0Imm8Pred,
        /*anyext*/  NegImm8, L4_iadd_memopb_io>;
  defm: Memopxi_pat<sextloadi8, truncstorei8, u6_0ImmPred, sub, m5_0Imm8Pred,
        /*sext*/    NegImm8, L4_iadd_memopb_io>;
  defm: Memopxi_pat<zextloadi8, truncstorei8, u6_0ImmPred, sub, m5_0Imm8Pred,
        /*zext*/    NegImm8, L4_iadd_memopb_io>;
  defm: Memopxi_pat<extloadi16, truncstorei16, u6_1ImmPred, sub, m5_0Imm16Pred,
        /*anyext*/  NegImm16, L4_iadd_memoph_io>;
  defm: Memopxi_pat<sextloadi16, truncstorei16, u6_1ImmPred, sub, m5_0Imm16Pred,
        /*sext*/    NegImm16, L4_iadd_memoph_io>;
  defm: Memopxi_pat<zextloadi16, truncstorei16, u6_1ImmPred, sub, m5_0Imm16Pred,
        /*zext*/    NegImm16, L4_iadd_memoph_io>;
  defm: Memopxi_pat<load, store, u6_2ImmPred, sub, m5_0ImmPred, NegImm32,
                    L4_iadd_memopw_io>;

  // sub imm
  defm: Memopxi_pat<extloadi8, truncstorei8, u6_0ImmPred, sub, u5_0ImmPred,
        /*anyext*/  IdImm, L4_isub_memopb_io>;
  defm: Memopxi_pat<sextloadi8, truncstorei8, u6_0ImmPred, sub, u5_0ImmPred,
        /*sext*/    IdImm, L4_isub_memopb_io>;
  defm: Memopxi_pat<zextloadi8, truncstorei8, u6_0ImmPred, sub, u5_0ImmPred,
        /*zext*/    IdImm, L4_isub_memopb_io>;
  defm: Memopxi_pat<extloadi16, truncstorei16, u6_1ImmPred, sub, u5_0ImmPred,
        /*anyext*/  IdImm, L4_isub_memoph_io>;
  defm: Memopxi_pat<sextloadi16, truncstorei16, u6_1ImmPred, sub, u5_0ImmPred,
        /*sext*/    IdImm, L4_isub_memoph_io>;
  defm: Memopxi_pat<zextloadi16, truncstorei16, u6_1ImmPred, sub, u5_0ImmPred,
        /*zext*/    IdImm, L4_isub_memoph_io>;
  defm: Memopxi_pat<load, store, u6_2ImmPred, sub, u5_0ImmPred, IdImm,
                    L4_isub_memopw_io>;
  defm: Memopxi_pat<extloadi8, truncstorei8, u6_0ImmPred, add, m5_0Imm8Pred,
        /*anyext*/  NegImm8, L4_isub_memopb_io>;
  defm: Memopxi_pat<sextloadi8, truncstorei8, u6_0ImmPred, add, m5_0Imm8Pred,
        /*sext*/    NegImm8, L4_isub_memopb_io>;
  defm: Memopxi_pat<zextloadi8, truncstorei8, u6_0ImmPred, add, m5_0Imm8Pred,
        /*zext*/    NegImm8, L4_isub_memopb_io>;
  defm: Memopxi_pat<extloadi16, truncstorei16, u6_1ImmPred, add, m5_0Imm16Pred,
        /*anyext*/  NegImm16, L4_isub_memoph_io>;
  defm: Memopxi_pat<sextloadi16, truncstorei16, u6_1ImmPred, add, m5_0Imm16Pred,
        /*sext*/    NegImm16, L4_isub_memoph_io>;
  defm: Memopxi_pat<zextloadi16, truncstorei16, u6_1ImmPred, add, m5_0Imm16Pred,
        /*zext*/    NegImm16, L4_isub_memoph_io>;
  defm: Memopxi_pat<load, store, u6_2ImmPred, add, m5_0ImmPred, NegImm32,
                    L4_isub_memopw_io>;

  // clrbit imm
  defm: Memopxi_pat<extloadi8, truncstorei8, u6_0ImmPred, and, IsNPow2_8,
        /*anyext*/  LogN2_8, L4_iand_memopb_io>;
  defm: Memopxi_pat<sextloadi8, truncstorei8, u6_0ImmPred, and, IsNPow2_8,
        /*sext*/    LogN2_8, L4_iand_memopb_io>;
  defm: Memopxi_pat<zextloadi8, truncstorei8, u6_0ImmPred, and, IsNPow2_8,
        /*zext*/    LogN2_8, L4_iand_memopb_io>;
  defm: Memopxi_pat<extloadi16, truncstorei16, u6_1ImmPred, and, IsNPow2_16,
        /*anyext*/  LogN2_16, L4_iand_memoph_io>;
  defm: Memopxi_pat<sextloadi16, truncstorei16, u6_1ImmPred, and, IsNPow2_16,
        /*sext*/    LogN2_16, L4_iand_memoph_io>;
  defm: Memopxi_pat<zextloadi16, truncstorei16, u6_1ImmPred, and, IsNPow2_16,
        /*zext*/    LogN2_16, L4_iand_memoph_io>;
  defm: Memopxi_pat<load, store, u6_2ImmPred, and, IsNPow2_32,
		    LogN2_32, L4_iand_memopw_io>;

  // setbit imm
  defm: Memopxi_pat<extloadi8, truncstorei8, u6_0ImmPred, or, IsPow2_32,
        /*anyext*/  Log2_8, L4_ior_memopb_io>;
  defm: Memopxi_pat<sextloadi8, truncstorei8, u6_0ImmPred, or, IsPow2_32,
        /*sext*/    Log2_8, L4_ior_memopb_io>;
  defm: Memopxi_pat<zextloadi8, truncstorei8, u6_0ImmPred, or, IsPow2_32,
        /*zext*/    Log2_8, L4_ior_memopb_io>;
  defm: Memopxi_pat<extloadi16, truncstorei16, u6_1ImmPred, or, IsPow2_32,
        /*anyext*/  Log2_16, L4_ior_memoph_io>;
  defm: Memopxi_pat<sextloadi16, truncstorei16, u6_1ImmPred, or, IsPow2_32,
        /*sext*/    Log2_16, L4_ior_memoph_io>;
  defm: Memopxi_pat<zextloadi16, truncstorei16, u6_1ImmPred, or, IsPow2_32,
        /*zext*/    Log2_16, L4_ior_memoph_io>;
  defm: Memopxi_pat<load, store, u6_2ImmPred, or, IsPow2_32,
		    Log2_32, L4_ior_memopw_io>;
}


// --(15) Call -----------------------------------------------------------
//

// Pseudo instructions.
def SDT_SPCallSeqStart
  : SDCallSeqStart<[SDTCisVT<0, i32>, SDTCisVT<1, i32>]>;
def SDT_SPCallSeqEnd
  : SDCallSeqEnd<[SDTCisVT<0, i32>, SDTCisVT<1, i32>]>;

def callseq_start: SDNode<"ISD::CALLSEQ_START", SDT_SPCallSeqStart,
                          [SDNPHasChain, SDNPOutGlue]>;
def callseq_end:   SDNode<"ISD::CALLSEQ_END",   SDT_SPCallSeqEnd,
                          [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

def SDT_SPCall: SDTypeProfile<0, 1, [SDTCisVT<0, i32>]>;

def HexagonTCRet: SDNode<"HexagonISD::TC_RETURN", SDT_SPCall,
                         [SDNPHasChain,  SDNPOptInGlue, SDNPVariadic]>;
def callv3: SDNode<"HexagonISD::CALL", SDT_SPCall,
                   [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue, SDNPVariadic]>;
def callv3nr: SDNode<"HexagonISD::CALLnr", SDT_SPCall,
                     [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue, SDNPVariadic]>;

def: Pat<(callseq_start timm:$amt, timm:$amt2),
         (ADJCALLSTACKDOWN imm:$amt, imm:$amt2)>;
def: Pat<(callseq_end timm:$amt1, timm:$amt2),
         (ADJCALLSTACKUP imm:$amt1, imm:$amt2)>;

def: Pat<(HexagonTCRet tglobaladdr:$dst),   (PS_tailcall_i tglobaladdr:$dst)>;
def: Pat<(HexagonTCRet texternalsym:$dst),  (PS_tailcall_i texternalsym:$dst)>;
def: Pat<(HexagonTCRet I32:$dst),           (PS_tailcall_r I32:$dst)>;

def: Pat<(callv3 I32:$dst),                 (J2_callr I32:$dst)>;
def: Pat<(callv3 tglobaladdr:$dst),         (J2_call tglobaladdr:$dst)>;
def: Pat<(callv3 texternalsym:$dst),        (J2_call texternalsym:$dst)>;
def: Pat<(callv3 tglobaltlsaddr:$dst),      (J2_call tglobaltlsaddr:$dst)>;

def: Pat<(callv3nr I32:$dst),               (PS_callr_nr I32:$dst)>;
def: Pat<(callv3nr tglobaladdr:$dst),       (PS_call_nr tglobaladdr:$dst)>;
def: Pat<(callv3nr texternalsym:$dst),      (PS_call_nr texternalsym:$dst)>;

def retglue : SDNode<"HexagonISD::RET_GLUE", SDTNone,
                     [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def eh_return: SDNode<"HexagonISD::EH_RETURN", SDTNone, [SDNPHasChain]>;

def: Pat<(retglue),   (PS_jmpret (i32 R31))>;
def: Pat<(eh_return), (EH_RETURN_JMPR (i32 R31))>;


// --(16) Branch ---------------------------------------------------------
//

def: Pat<(br      bb:$dst),         (J2_jump  b30_2Imm:$dst)>;
def: Pat<(brind   I32:$dst),        (J2_jumpr I32:$dst)>;

def: Pat<(brcond I1:$Pu, bb:$dst),
         (J2_jumpt I1:$Pu, bb:$dst)>;
def: Pat<(brcond (not I1:$Pu), bb:$dst),
         (J2_jumpf I1:$Pu, bb:$dst)>;
def: Pat<(brcond (i1 (setne I1:$Pu, -1)), bb:$dst),
         (J2_jumpf I1:$Pu, bb:$dst)>;
def: Pat<(brcond (i1 (seteq I1:$Pu, 0)), bb:$dst),
         (J2_jumpf I1:$Pu, bb:$dst)>;
def: Pat<(brcond (i1 (setne I1:$Pu, 0)), bb:$dst),
         (J2_jumpt I1:$Pu, bb:$dst)>;


// --(17) Misc -----------------------------------------------------------


// Generate code of the form 'C2_muxii(cmpbgtui(Rdd, C-1),0,1)'
// for C code of the form r = (c>='0' && c<='9') ? 1 : 0.
// The isdigit transformation relies on two 'clever' aspects:
// 1) The data type is unsigned which allows us to eliminate a zero test after
//    biasing the expression by 48. We are depending on the representation of
//    the unsigned types, and semantics.
// 2) The front end has converted <= 9 into < 10 on entry to LLVM.
//
// For the C code:
//   retval = (c >= '0' && c <= '9') ? 1 : 0;
// The code is transformed upstream of llvm into
//   retval = (c-48) < 10 ? 1 : 0;

def u7_0PosImmPred : ImmLeaf<i32, [{
  // True if the immediate fits in an 7-bit unsigned field and is positive.
  return Imm > 0 && isUInt<7>(Imm);
}]>;

let AddedComplexity = 139 in
def: Pat<(i32 (zext (i1 (setult (and I32:$Rs, 255), u7_0PosImmPred:$u7)))),
         (C2_muxii (A4_cmpbgtui IntRegs:$Rs, (UDEC1 imm:$u7)), 0, 1)>;

let AddedComplexity = 100 in
def: Pat<(or (or (shl (HexagonINSERT (i32 (zextloadi8 (add I32:$b, 2))),
                                     (i32 (extloadi8  (add I32:$b, 3))),
                                     24, 8),
                      (i32 16)),
                 (shl (i32 (zextloadi8 (add I32:$b, 1))), (i32 8))),
             (zextloadi8 I32:$b)),
         (A2_swiz (L2_loadri_io I32:$b, 0))>;


// We need custom lowering of ISD::PREFETCH into HexagonISD::DCFETCH
// because the SDNode ISD::PREFETCH has properties MayLoad and MayStore.
// We don't really want either one here.
def SDTHexagonDCFETCH: SDTypeProfile<0, 2, [SDTCisPtrTy<0>,SDTCisInt<1>]>;
def HexagonDCFETCH: SDNode<"HexagonISD::DCFETCH", SDTHexagonDCFETCH,
                           [SDNPHasChain]>;

def: Pat<(HexagonDCFETCH IntRegs:$Rs, u11_3ImmPred:$u11_3),
         (Y2_dcfetchbo IntRegs:$Rs, imm:$u11_3)>;
def: Pat<(HexagonDCFETCH (i32 (add IntRegs:$Rs, u11_3ImmPred:$u11_3)), (i32 0)),
         (Y2_dcfetchbo IntRegs:$Rs, imm:$u11_3)>;

def SDTHexagonALLOCA
  : SDTypeProfile<1, 2, [SDTCisVT<0, i32>, SDTCisVT<1, i32>]>;
def HexagonALLOCA
  : SDNode<"HexagonISD::ALLOCA", SDTHexagonALLOCA, [SDNPHasChain]>;

def: Pat<(HexagonALLOCA I32:$Rs, (i32 imm:$A)),
         (PS_alloca IntRegs:$Rs, imm:$A)>;

def HexagonBARRIER: SDNode<"HexagonISD::BARRIER", SDTNone, [SDNPHasChain]>;
def: Pat<(HexagonBARRIER), (Y2_barrier)>;

def: Pat<(trap), (PS_crash)>;

// Read cycle counter.
def SDTInt64Leaf: SDTypeProfile<1, 0, [SDTCisVT<0, i64>]>;
def HexagonREADCYCLE: SDNode<"HexagonISD::READCYCLE", SDTInt64Leaf,
  [SDNPHasChain]>;

def: Pat<(HexagonREADCYCLE), (A4_tfrcpp UPCYCLE)>;

// Read time counter.
def HexagonREADTIMER: SDNode<"HexagonISD::READTIMER", SDTInt64Leaf,
  [SDNPHasChain]>;

def: Pat<(HexagonREADTIMER), (A4_tfrcpp UTIMER)>;

// The declared return value of the store-locked intrinsics is i32, but
// the instructions actually define i1. To avoid register copies from
// IntRegs to PredRegs and back, fold the entire pattern checking the
// result against true/false.
let AddedComplexity = 100 in {
  def: Pat<(i1 (setne (int_hexagon_S2_storew_locked I32:$Rs, I32:$Rt), 0)),
           (S2_storew_locked I32:$Rs, I32:$Rt)>;
  def: Pat<(i1 (seteq (int_hexagon_S2_storew_locked I32:$Rs, I32:$Rt), 0)),
           (C2_not (S2_storew_locked I32:$Rs, I32:$Rt))>;
  def: Pat<(i1 (setne (int_hexagon_S4_stored_locked I32:$Rs, I64:$Rt), 0)),
           (S4_stored_locked I32:$Rs, I64:$Rt)>;
  def: Pat<(i1 (seteq (int_hexagon_S4_stored_locked I32:$Rs, I64:$Rt), 0)),
           (C2_not (S4_stored_locked I32:$Rs, I64:$Rt))>;
}

def: Pat<(int_hexagon_instrprof_custom (HexagonAtPcrel tglobaladdr:$addr), u32_0ImmPred:$I),
         (PS_call_instrprof_custom tglobaladdr:$addr, imm:$I)>;

def: Pat<(int_hexagon_instrprof_custom (HexagonCONST32 tglobaladdr:$addr), u32_0ImmPred:$I),
         (PS_call_instrprof_custom tglobaladdr:$addr, imm:$I)>;

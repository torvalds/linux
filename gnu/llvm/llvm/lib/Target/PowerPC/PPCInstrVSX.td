//===- PPCInstrVSX.td - The PowerPC VSX Extension --*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the VSX extension to the PowerPC instruction set.
//
//===----------------------------------------------------------------------===//

// *********************************** NOTE ***********************************
// ** For POWER8 Little Endian, the VSX swap optimization relies on knowing  **
// ** which VMX and VSX instructions are lane-sensitive and which are not.   **
// ** A lane-sensitive instruction relies, implicitly or explicitly, on      **
// ** whether lanes are numbered from left to right.  An instruction like    **
// ** VADDFP is not lane-sensitive, because each lane of the result vector   **
// ** relies only on the corresponding lane of the source vectors.  However, **
// ** an instruction like VMULESB is lane-sensitive, because "even" and      **
// ** "odd" lanes are different for big-endian and little-endian numbering.  **
// **                                                                        **
// ** When adding new VMX and VSX instructions, please consider whether they **
// ** are lane-sensitive.  If so, they must be added to a switch statement   **
// ** in PPCVSXSwapRemoval::gatherVectorInstructions().                      **
// ****************************************************************************

// *********************************** NOTE ***********************************
// ** When adding new anonymous patterns to this file, please add them to    **
// ** the section titled Anonymous Patterns. Chances are that the existing   **
// ** predicate blocks already contain a combination of features that you    **
// ** are after. There is a list of blocks at the top of the section. If     **
// ** you definitely need a new combination of predicates, please add that   **
// ** combination to the list.                                               **
// ** File Structure:                                                        **
// ** - Custom PPCISD node definitions                                       **
// ** - Predicate definitions: predicates to specify the subtargets for      **
// **   which an instruction or pattern can be emitted.                      **
// ** - Instruction formats: classes instantiated by the instructions.       **
// **   These generally correspond to instruction formats in section 1.6 of  **
// **   the ISA document.                                                    **
// ** - Instruction definitions: the actual definitions of the instructions  **
// **   often including input patterns that they match.                      **
// ** - Helper DAG definitions: We define a number of dag objects to use as  **
// **   input or output patterns for consciseness of the code.               **
// ** - Anonymous patterns: input patterns that an instruction matches can   **
// **   often not be specified as part of the instruction definition, so an  **
// **   anonymous pattern must be specified mapping an input pattern to an   **
// **   output pattern. These are generally guarded by subtarget predicates. **
// ** - Instruction aliases: used to define extended mnemonics for assembly  **
// **   printing (for example: xxswapd for xxpermdi with 0x2 as the imm).    **
// ****************************************************************************

def SDT_PPCldvsxlh : SDTypeProfile<1, 1, [
  SDTCisVT<0, v4f32>, SDTCisPtrTy<1>
]>;

def SDT_PPCfpexth : SDTypeProfile<1, 2, [
  SDTCisVT<0, v2f64>, SDTCisVT<1, v4f32>, SDTCisPtrTy<2>
]>;

def SDT_PPCldsplat : SDTypeProfile<1, 1, [
  SDTCisVec<0>, SDTCisPtrTy<1>
]>;

// Little-endian-specific nodes.
def SDT_PPClxvd2x : SDTypeProfile<1, 1, [
  SDTCisVT<0, v2f64>, SDTCisPtrTy<1>
]>;
def SDT_PPCstxvd2x : SDTypeProfile<0, 2, [
  SDTCisVT<0, v2f64>, SDTCisPtrTy<1>
]>;
def SDT_PPCxxswapd : SDTypeProfile<1, 1, [
  SDTCisSameAs<0, 1>
]>;
def SDTVecConv : SDTypeProfile<1, 2, [
  SDTCisVec<0>, SDTCisVec<1>, SDTCisPtrTy<2>
]>;
def SDT_PPCld_vec_be : SDTypeProfile<1, 1, [
  SDTCisVec<0>, SDTCisPtrTy<1>
]>;
def SDT_PPCst_vec_be : SDTypeProfile<0, 2, [
  SDTCisVec<0>, SDTCisPtrTy<1>
]>;

def SDT_PPCxxperm : SDTypeProfile<1, 3, [
  SDTCisVT<0, v2f64>, SDTCisVT<1, v2f64>,
  SDTCisVT<2, v2f64>, SDTCisVT<3, v4i32>]>;
//--------------------------- Custom PPC nodes -------------------------------//
def PPClxvd2x  : SDNode<"PPCISD::LXVD2X", SDT_PPClxvd2x,
                        [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;
def PPCstxvd2x : SDNode<"PPCISD::STXVD2X", SDT_PPCstxvd2x,
                        [SDNPHasChain, SDNPMayStore]>;
def PPCld_vec_be  : SDNode<"PPCISD::LOAD_VEC_BE", SDT_PPCld_vec_be,
                        [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;
def PPCst_vec_be : SDNode<"PPCISD::STORE_VEC_BE", SDT_PPCst_vec_be,
                        [SDNPHasChain, SDNPMayStore]>;
def PPCxxswapd : SDNode<"PPCISD::XXSWAPD", SDT_PPCxxswapd, [SDNPHasChain]>;
def PPCmfvsr : SDNode<"PPCISD::MFVSR", SDTUnaryOp, []>;
def PPCmtvsra : SDNode<"PPCISD::MTVSRA", SDTUnaryOp, []>;
def PPCmtvsrz : SDNode<"PPCISD::MTVSRZ", SDTUnaryOp, []>;
def PPCsvec2fp : SDNode<"PPCISD::SINT_VEC_TO_FP", SDTVecConv, []>;
def PPCuvec2fp: SDNode<"PPCISD::UINT_VEC_TO_FP", SDTVecConv, []>;
def PPCswapNoChain : SDNode<"PPCISD::SWAP_NO_CHAIN", SDT_PPCxxswapd>;

def PPCfpexth : SDNode<"PPCISD::FP_EXTEND_HALF", SDT_PPCfpexth, []>;
def PPCldvsxlh : SDNode<"PPCISD::LD_VSX_LH", SDT_PPCldvsxlh,
                        [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;
def PPCldsplat : SDNode<"PPCISD::LD_SPLAT", SDT_PPCldsplat,
                        [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;
def PPCzextldsplat : SDNode<"PPCISD::ZEXT_LD_SPLAT", SDT_PPCldsplat,
                        [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;
def PPCsextldsplat : SDNode<"PPCISD::SEXT_LD_SPLAT", SDT_PPCldsplat,
                        [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;
def PPCSToV : SDNode<"PPCISD::SCALAR_TO_VECTOR_PERMUTED",
                     SDTypeProfile<1, 1, []>, []>;

def PPCxxperm : SDNode<"PPCISD::XXPERM", SDT_PPCxxperm, []>;
//-------------------------- Predicate definitions ---------------------------//
def HasVSX : Predicate<"Subtarget->hasVSX()">;
def IsLittleEndian : Predicate<"Subtarget->isLittleEndian()">;
def IsBigEndian : Predicate<"!Subtarget->isLittleEndian()">;
def IsPPC64 : Predicate<"Subtarget->isPPC64()">;
def HasOnlySwappingMemOps : Predicate<"!Subtarget->hasP9Vector()">;
def NoP8Vector : Predicate<"!Subtarget->hasP8Vector()">;
def HasP8Vector : Predicate<"Subtarget->hasP8Vector()">;
def HasDirectMove : Predicate<"Subtarget->hasDirectMove()">;
def NoP9Vector : Predicate<"!Subtarget->hasP9Vector()">;
def HasP9Vector : Predicate<"Subtarget->hasP9Vector()">;
def NoP9Altivec : Predicate<"!Subtarget->hasP9Altivec()">;
def NoP10Vector: Predicate<"!Subtarget->hasP10Vector()">;
def HasP10Vector: Predicate<"Subtarget->hasP10Vector()">;

def PPCldsplatAlign16 : PatFrag<(ops node:$ptr), (PPCldsplat node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getAlign() >= Align(16) &&
         isOffsetMultipleOf(N, 16);
}]>;

//--------------------- VSX-specific instruction formats ---------------------//
// By default, all VSX instructions are to be selected over their Altivec
// counter parts and they do not have unmodeled sideeffects.
let AddedComplexity = 400, hasSideEffects = 0 in {
multiclass XX3Form_Rcr<bits<6> opcode, bits<7> xo, string asmbase,
                    string asmstr, InstrItinClass itin, Intrinsic Int,
                    ValueType OutTy, ValueType InTy> {
  let BaseName = asmbase in {
    def NAME : XX3Form_Rc<opcode, xo, (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB),
                       !strconcat(asmbase, !strconcat(" ", asmstr)), itin,
                       [(set OutTy:$XT, (Int InTy:$XA, InTy:$XB))]>;
    let Defs = [CR6] in
    def _rec    : XX3Form_Rc<opcode, xo, (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB),
                       !strconcat(asmbase, !strconcat(". ", asmstr)), itin,
                       [(set InTy:$XT,
                                (InTy (PPCvcmp_rec InTy:$XA, InTy:$XB, xo)))]>,
                       isRecordForm;
  }
}

// Instruction form with a single input register for instructions such as
// XXPERMDI. The reason for defining this is that specifying multiple chained
// operands (such as loads) to an instruction will perform both chained
// operations rather than coalescing them into a single register - even though
// the source memory location is the same. This simply forces the instruction
// to use the same register for both inputs.
// For example, an output DAG such as this:
//   (XXPERMDI (LXSIBZX xoaddr:$src), (LXSIBZX xoaddr:$src ), 0))
// would result in two load instructions emitted and used as separate inputs
// to the XXPERMDI instruction.
class XX3Form_2s<bits<6> opcode, bits<5> xo, dag OOL, dag IOL, string asmstr,
                 InstrItinClass itin, list<dag> pattern>
  : XX3Form_2<opcode, xo, OOL, IOL, asmstr, itin, pattern> {
    let XB = XA;
}

let Predicates = [HasVSX, HasP9Vector] in {
class X_VT5_XO5_VB5<bits<6> opcode, bits<5> xo2, bits<10> xo, string opc,
                    list<dag> pattern>
  : X_RD5_XO5_RS5<opcode, xo2, xo, (outs vrrc:$RST), (ins vrrc:$RB),
                  !strconcat(opc, " $RST, $RB"), IIC_VecFP, pattern>;

// [PO VRT XO VRB XO RO], Round to Odd version of [PO VRT XO VRB XO /]
class X_VT5_XO5_VB5_Ro<bits<6> opcode, bits<5> xo2, bits<10> xo, string opc,
                       list<dag> pattern>
  : X_VT5_XO5_VB5<opcode, xo2, xo, opc, pattern>, isRecordForm;

// [PO VRT XO VRB XO /], but the VRB is only used the left 64 bits (or less),
// So we use different operand class for VRB
class X_VT5_XO5_VB5_TyVB<bits<6> opcode, bits<5> xo2, bits<10> xo, string opc,
                         RegisterOperand vbtype, list<dag> pattern>
  : X_RD5_XO5_RS5<opcode, xo2, xo, (outs vrrc:$RST), (ins vbtype:$RB),
                  !strconcat(opc, " $RST, $RB"), IIC_VecFP, pattern>;

// [PO VRT XO VRB XO /]
class X_VT5_XO5_VB5_VSFR<bits<6> opcode, bits<5> xo2, bits<10> xo, string opc,
                    list<dag> pattern>
  : X_RD5_XO5_RS5<opcode, xo2, xo, (outs vfrc:$RST), (ins vrrc:$RB),
                  !strconcat(opc, " $RST, $RB"), IIC_VecFP, pattern>;

// [PO VRT XO VRB XO RO], Round to Odd version of [PO VRT XO VRB XO /]
class X_VT5_XO5_VB5_VSFR_Ro<bits<6> opcode, bits<5> xo2, bits<10> xo, string opc,
                       list<dag> pattern>
  : X_VT5_XO5_VB5_VSFR<opcode, xo2, xo, opc, pattern>, isRecordForm;

// [PO T XO B XO BX /]
class XX2_RT5_XO5_XB6<bits<6> opcode, bits<5> xo2, bits<9> xo, string opc,
                      list<dag> pattern>
  : XX2_RD5_XO5_RS6<opcode, xo2, xo, (outs g8rc:$RT), (ins vsfrc:$XB),
                    !strconcat(opc, " $RT, $XB"), IIC_VecFP, pattern>;

// [PO T XO B XO BX TX]
class XX2_XT6_XO5_XB6<bits<6> opcode, bits<5> xo2, bits<9> xo, string opc,
                      RegisterOperand vtype, list<dag> pattern>
  : XX2_RD6_XO5_RS6<opcode, xo2, xo, (outs vtype:$XT), (ins vtype:$XB),
                    !strconcat(opc, " $XT, $XB"), IIC_VecFP, pattern>;

// [PO T A B XO AX BX TX], src and dest register use different operand class
class XX3_XT5_XA5_XB5<bits<6> opcode, bits<8> xo, string opc,
                RegisterOperand xty, RegisterOperand aty, RegisterOperand bty,
                InstrItinClass itin, list<dag> pattern>
  : XX3Form<opcode, xo, (outs xty:$XT), (ins aty:$XA, bty:$XB),
            !strconcat(opc, " $XT, $XA, $XB"), itin, pattern>;

// [PO VRT VRA VRB XO /]
class X_VT5_VA5_VB5<bits<6> opcode, bits<10> xo, string opc,
                    list<dag> pattern>
  : XForm_1<opcode, xo, (outs vrrc:$RST), (ins vrrc:$RA, vrrc:$RB),
            !strconcat(opc, " $RST, $RA, $RB"), IIC_VecFP, pattern>;

// [PO VRT VRA VRB XO RO], Round to Odd version of [PO VRT VRA VRB XO /]
class X_VT5_VA5_VB5_Ro<bits<6> opcode, bits<10> xo, string opc,
                       list<dag> pattern>
  : X_VT5_VA5_VB5<opcode, xo, opc, pattern>, isRecordForm;

// [PO VRT VRA VRB XO /]
class X_VT5_VA5_VB5_FMA<bits<6> opcode, bits<10> xo, string opc,
                        list<dag> pattern>
  : XForm_1<opcode, xo, (outs vrrc:$RST), (ins vrrc:$RSTi, vrrc:$RA, vrrc:$RB),
            !strconcat(opc, " $RST, $RA, $RB"), IIC_VecFP, pattern>,
            RegConstraint<"$RSTi = $RST">, NoEncode<"$RSTi">;

// [PO VRT VRA VRB XO RO], Round to Odd version of [PO VRT VRA VRB XO /]
class X_VT5_VA5_VB5_FMA_Ro<bits<6> opcode, bits<10> xo, string opc,
                        list<dag> pattern>
  : X_VT5_VA5_VB5_FMA<opcode, xo, opc, pattern>, isRecordForm;

class Z23_VT5_R1_VB5_RMC2_EX1<bits<6> opcode, bits<8> xo, bit ex, string opc,
                              list<dag> pattern>
  : Z23Form_8<opcode, xo,
              (outs vrrc:$VRT), (ins u1imm:$R, vrrc:$VRB, u2imm:$idx),
              !strconcat(opc, " $R, $VRT, $VRB, $idx"), IIC_VecFP, pattern> {
  let RC = ex;
}

// [PO BF // VRA VRB XO /]
class X_BF3_VA5_VB5<bits<6> opcode, bits<10> xo, string opc,
                    list<dag> pattern>
  : XForm_17<opcode, xo, (outs crrc:$BF), (ins vrrc:$RA, vrrc:$RB),
             !strconcat(opc, " $BF, $RA, $RB"), IIC_FPCompare> {
  let Pattern = pattern;
}

// [PO T RA RB XO TX] almost equal to [PO S RA RB XO SX], but has different
// "out" and "in" dag
class X_XT6_RA5_RB5<bits<6> opcode, bits<10> xo, string opc,
                    RegisterOperand vtype, list<dag> pattern>
  : XX1Form_memOp<opcode, xo, (outs vtype:$XT), (ins (memrr $RA, $RB):$addr),
            !strconcat(opc, " $XT, $addr"), IIC_LdStLFD, pattern>;

// [PO S RA RB XO SX]
class X_XS6_RA5_RB5<bits<6> opcode, bits<10> xo, string opc,
                    RegisterOperand vtype, list<dag> pattern>
  : XX1Form_memOp<opcode, xo, (outs), (ins vtype:$XT, (memrr $RA, $RB):$addr),
            !strconcat(opc, " $XT, $addr"), IIC_LdStSTFD, pattern>;
} // Predicates = HasP9Vector
} // AddedComplexity = 400, hasSideEffects = 0

multiclass ScalToVecWPermute<ValueType Ty, dag In, dag NonPermOut, dag PermOut> {
  def : Pat<(Ty (scalar_to_vector In)), (Ty NonPermOut)>;
  def : Pat<(Ty (PPCSToV In)), (Ty PermOut)>;
}

//-------------------------- Instruction definitions -------------------------//
// VSX instructions require the VSX feature, they are to be selected over
// equivalent Altivec patterns (as they address a larger register set) and
// they do not have unmodeled side effects.
let Predicates = [HasVSX], AddedComplexity = 400 in {
let hasSideEffects = 0 in {

  // Load indexed instructions
  let mayLoad = 1, mayStore = 0 in {
    let CodeSize = 3 in
    def LXSDX : XX1Form_memOp<31, 588,
                        (outs vsfrc:$XT), (ins (memrr $RA, $RB):$addr),
                        "lxsdx $XT, $addr", IIC_LdStLFD,
                        []>;

    // Pseudo instruction XFLOADf64 will be expanded to LXSDX or LFDX later
    let CodeSize = 3 in
      def XFLOADf64  : PseudoXFormMemOp<(outs vsfrc:$XT), (ins (memrr $RA, $RB):$addr),
                              "#XFLOADf64",
                              [(set f64:$XT, (load XForm:$addr))]>;

    let Predicates = [HasVSX, HasOnlySwappingMemOps] in
    def LXVD2X : XX1Form_memOp<31, 844,
                         (outs vsrc:$XT), (ins (memrr $RA, $RB):$addr),
                         "lxvd2x $XT, $addr", IIC_LdStLFD,
                         []>;

    def LXVDSX : XX1Form_memOp<31, 332,
                         (outs vsrc:$XT), (ins (memrr $RA, $RB):$addr),
                         "lxvdsx $XT, $addr", IIC_LdStLFD, []>;

    let Predicates = [HasVSX, HasOnlySwappingMemOps] in
    def LXVW4X : XX1Form_memOp<31, 780,
                         (outs vsrc:$XT), (ins (memrr $RA, $RB):$addr),
                         "lxvw4x $XT, $addr", IIC_LdStLFD,
                         []>;
  } // mayLoad

  // Store indexed instructions
  let mayStore = 1, mayLoad = 0 in {
    let CodeSize = 3 in
    def STXSDX : XX1Form_memOp<31, 716,
                        (outs), (ins vsfrc:$XT, (memrr $RA, $RB):$addr),
                        "stxsdx $XT, $addr", IIC_LdStSTFD,
                        []>;

    // Pseudo instruction XFSTOREf64  will be expanded to STXSDX or STFDX later
    let CodeSize = 3 in
      def XFSTOREf64 : PseudoXFormMemOp<(outs), (ins vsfrc:$XT, (memrr $RA, $RB):$addr),
                              "#XFSTOREf64",
                              [(store f64:$XT, XForm:$addr)]>;

    let Predicates = [HasVSX, HasOnlySwappingMemOps] in {
    // The behaviour of this instruction is endianness-specific so we provide no
    // pattern to match it without considering endianness.
    def STXVD2X : XX1Form_memOp<31, 972,
                         (outs), (ins vsrc:$XT, (memrr $RA, $RB):$addr),
                         "stxvd2x $XT, $addr", IIC_LdStSTFD,
                         []>;

    def STXVW4X : XX1Form_memOp<31, 908,
                         (outs), (ins vsrc:$XT, (memrr $RA, $RB):$addr),
                         "stxvw4x $XT, $addr", IIC_LdStSTFD,
                         []>;
    }
  } // mayStore

  let mayRaiseFPException = 1 in {
  let Uses = [RM] in {
  // Add/Mul Instructions
  let isCommutable = 1 in {
    def XSADDDP : XX3Form<60, 32,
                          (outs vsfrc:$XT), (ins vsfrc:$XA, vsfrc:$XB),
                          "xsadddp $XT, $XA, $XB", IIC_VecFP,
                          [(set f64:$XT, (any_fadd f64:$XA, f64:$XB))]>;
    def XSMULDP : XX3Form<60, 48,
                          (outs vsfrc:$XT), (ins vsfrc:$XA, vsfrc:$XB),
                          "xsmuldp $XT, $XA, $XB", IIC_VecFP,
                          [(set f64:$XT, (any_fmul f64:$XA, f64:$XB))]>;

    def XVADDDP : XX3Form<60, 96,
                          (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB),
                          "xvadddp $XT, $XA, $XB", IIC_VecFP,
                          [(set v2f64:$XT, (any_fadd v2f64:$XA, v2f64:$XB))]>;

    def XVADDSP : XX3Form<60, 64,
                          (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB),
                          "xvaddsp $XT, $XA, $XB", IIC_VecFP,
                          [(set v4f32:$XT, (any_fadd v4f32:$XA, v4f32:$XB))]>;

    def XVMULDP : XX3Form<60, 112,
                          (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB),
                          "xvmuldp $XT, $XA, $XB", IIC_VecFP,
                          [(set v2f64:$XT, (any_fmul v2f64:$XA, v2f64:$XB))]>;

    def XVMULSP : XX3Form<60, 80,
                          (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB),
                          "xvmulsp $XT, $XA, $XB", IIC_VecFP,
                          [(set v4f32:$XT, (any_fmul v4f32:$XA, v4f32:$XB))]>;
  }

  // Subtract Instructions
  def XSSUBDP : XX3Form<60, 40,
                        (outs vsfrc:$XT), (ins vsfrc:$XA, vsfrc:$XB),
                        "xssubdp $XT, $XA, $XB", IIC_VecFP,
                        [(set f64:$XT, (any_fsub f64:$XA, f64:$XB))]>;

  def XVSUBDP : XX3Form<60, 104,
                        (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB),
                        "xvsubdp $XT, $XA, $XB", IIC_VecFP,
                        [(set v2f64:$XT, (any_fsub v2f64:$XA, v2f64:$XB))]>;
  def XVSUBSP : XX3Form<60, 72,
                        (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB),
                        "xvsubsp $XT, $XA, $XB", IIC_VecFP,
                        [(set v4f32:$XT, (any_fsub v4f32:$XA, v4f32:$XB))]>;

  // FMA Instructions
  let BaseName = "XSMADDADP" in {
  let isCommutable = 1 in
  def XSMADDADP : XX3Form<60, 33,
                          (outs vsfrc:$XT), (ins vsfrc:$XTi, vsfrc:$XA, vsfrc:$XB),
                          "xsmaddadp $XT, $XA, $XB", IIC_VecFP,
                          [(set f64:$XT, (any_fma f64:$XA, f64:$XB, f64:$XTi))]>,
                          RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">,
                          AltVSXFMARel;
  let IsVSXFMAAlt = 1 in
  def XSMADDMDP : XX3Form<60, 41,
                          (outs vsfrc:$XT), (ins vsfrc:$XTi, vsfrc:$XA, vsfrc:$XB),
                          "xsmaddmdp $XT, $XA, $XB", IIC_VecFP, []>,
                          RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">,
                          AltVSXFMARel;
  }

  let BaseName = "XSMSUBADP" in {
  let isCommutable = 1 in
  def XSMSUBADP : XX3Form<60, 49,
                          (outs vsfrc:$XT), (ins vsfrc:$XTi, vsfrc:$XA, vsfrc:$XB),
                          "xsmsubadp $XT, $XA, $XB", IIC_VecFP,
                          [(set f64:$XT, (any_fma f64:$XA, f64:$XB, (fneg f64:$XTi)))]>,
                          RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">,
                          AltVSXFMARel;
  let IsVSXFMAAlt = 1 in
  def XSMSUBMDP : XX3Form<60, 57,
                          (outs vsfrc:$XT), (ins vsfrc:$XTi, vsfrc:$XA, vsfrc:$XB),
                          "xsmsubmdp $XT, $XA, $XB", IIC_VecFP, []>,
                          RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">,
                          AltVSXFMARel;
  }

  let BaseName = "XSNMADDADP" in {
  let isCommutable = 1 in
  def XSNMADDADP : XX3Form<60, 161,
                          (outs vsfrc:$XT), (ins vsfrc:$XTi, vsfrc:$XA, vsfrc:$XB),
                          "xsnmaddadp $XT, $XA, $XB", IIC_VecFP,
                          [(set f64:$XT, (fneg (any_fma f64:$XA, f64:$XB, f64:$XTi)))]>,
                          RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">,
                          AltVSXFMARel;
  let IsVSXFMAAlt = 1 in
  def XSNMADDMDP : XX3Form<60, 169,
                          (outs vsfrc:$XT), (ins vsfrc:$XTi, vsfrc:$XA, vsfrc:$XB),
                          "xsnmaddmdp $XT, $XA, $XB", IIC_VecFP, []>,
                          RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">,
                          AltVSXFMARel;
  }

  let BaseName = "XSNMSUBADP" in {
  let isCommutable = 1 in
  def XSNMSUBADP : XX3Form<60, 177,
                          (outs vsfrc:$XT), (ins vsfrc:$XTi, vsfrc:$XA, vsfrc:$XB),
                          "xsnmsubadp $XT, $XA, $XB", IIC_VecFP,
                          [(set f64:$XT, (fneg (any_fma f64:$XA, f64:$XB, (fneg f64:$XTi))))]>,
                          RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">,
                          AltVSXFMARel;
  let IsVSXFMAAlt = 1 in
  def XSNMSUBMDP : XX3Form<60, 185,
                          (outs vsfrc:$XT), (ins vsfrc:$XTi, vsfrc:$XA, vsfrc:$XB),
                          "xsnmsubmdp $XT, $XA, $XB", IIC_VecFP, []>,
                          RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">,
                          AltVSXFMARel;
  }

  let BaseName = "XVMADDADP" in {
  let isCommutable = 1 in
  def XVMADDADP : XX3Form<60, 97,
                          (outs vsrc:$XT), (ins vsrc:$XTi, vsrc:$XA, vsrc:$XB),
                          "xvmaddadp $XT, $XA, $XB", IIC_VecFP,
                          [(set v2f64:$XT, (any_fma v2f64:$XA, v2f64:$XB, v2f64:$XTi))]>,
                          RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">,
                          AltVSXFMARel;
  let IsVSXFMAAlt = 1 in
  def XVMADDMDP : XX3Form<60, 105,
                          (outs vsrc:$XT), (ins vsrc:$XTi, vsrc:$XA, vsrc:$XB),
                          "xvmaddmdp $XT, $XA, $XB", IIC_VecFP, []>,
                          RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">,
                          AltVSXFMARel;
  }

  let BaseName = "XVMADDASP" in {
  let isCommutable = 1 in
  def XVMADDASP : XX3Form<60, 65,
                          (outs vsrc:$XT), (ins vsrc:$XTi, vsrc:$XA, vsrc:$XB),
                          "xvmaddasp $XT, $XA, $XB", IIC_VecFP,
                          [(set v4f32:$XT, (any_fma v4f32:$XA, v4f32:$XB, v4f32:$XTi))]>,
                          RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">,
                          AltVSXFMARel;
  let IsVSXFMAAlt = 1 in
  def XVMADDMSP : XX3Form<60, 73,
                          (outs vsrc:$XT), (ins vsrc:$XTi, vsrc:$XA, vsrc:$XB),
                          "xvmaddmsp $XT, $XA, $XB", IIC_VecFP, []>,
                          RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">,
                          AltVSXFMARel;
  }

  let BaseName = "XVMSUBADP" in {
  let isCommutable = 1 in
  def XVMSUBADP : XX3Form<60, 113,
                          (outs vsrc:$XT), (ins vsrc:$XTi, vsrc:$XA, vsrc:$XB),
                          "xvmsubadp $XT, $XA, $XB", IIC_VecFP,
                          [(set v2f64:$XT, (any_fma v2f64:$XA, v2f64:$XB, (fneg v2f64:$XTi)))]>,
                          RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">,
                          AltVSXFMARel;
  let IsVSXFMAAlt = 1 in
  def XVMSUBMDP : XX3Form<60, 121,
                          (outs vsrc:$XT), (ins vsrc:$XTi, vsrc:$XA, vsrc:$XB),
                          "xvmsubmdp $XT, $XA, $XB", IIC_VecFP, []>,
                          RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">,
                          AltVSXFMARel;
  }

  let BaseName = "XVMSUBASP" in {
  let isCommutable = 1 in
  def XVMSUBASP : XX3Form<60, 81,
                          (outs vsrc:$XT), (ins vsrc:$XTi, vsrc:$XA, vsrc:$XB),
                          "xvmsubasp $XT, $XA, $XB", IIC_VecFP,
                          [(set v4f32:$XT, (any_fma v4f32:$XA, v4f32:$XB, (fneg v4f32:$XTi)))]>,
                          RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">,
                          AltVSXFMARel;
  let IsVSXFMAAlt = 1 in
  def XVMSUBMSP : XX3Form<60, 89,
                          (outs vsrc:$XT), (ins vsrc:$XTi, vsrc:$XA, vsrc:$XB),
                          "xvmsubmsp $XT, $XA, $XB", IIC_VecFP, []>,
                          RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">,
                          AltVSXFMARel;
  }

  let BaseName = "XVNMADDADP" in {
  let isCommutable = 1 in
  def XVNMADDADP : XX3Form<60, 225,
                          (outs vsrc:$XT), (ins vsrc:$XTi, vsrc:$XA, vsrc:$XB),
                          "xvnmaddadp $XT, $XA, $XB", IIC_VecFP,
                          [(set v2f64:$XT, (fneg (any_fma v2f64:$XA, v2f64:$XB, v2f64:$XTi)))]>,
                          RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">,
                          AltVSXFMARel;
  let IsVSXFMAAlt = 1 in
  def XVNMADDMDP : XX3Form<60, 233,
                          (outs vsrc:$XT), (ins vsrc:$XTi, vsrc:$XA, vsrc:$XB),
                          "xvnmaddmdp $XT, $XA, $XB", IIC_VecFP, []>,
                          RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">,
                          AltVSXFMARel;
  }

  let BaseName = "XVNMADDASP" in {
  let isCommutable = 1 in
  def XVNMADDASP : XX3Form<60, 193,
                          (outs vsrc:$XT), (ins vsrc:$XTi, vsrc:$XA, vsrc:$XB),
                          "xvnmaddasp $XT, $XA, $XB", IIC_VecFP,
                          [(set v4f32:$XT, (fneg (fma v4f32:$XA, v4f32:$XB, v4f32:$XTi)))]>,
                          RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">,
                          AltVSXFMARel;
  let IsVSXFMAAlt = 1 in
  def XVNMADDMSP : XX3Form<60, 201,
                          (outs vsrc:$XT), (ins vsrc:$XTi, vsrc:$XA, vsrc:$XB),
                          "xvnmaddmsp $XT, $XA, $XB", IIC_VecFP, []>,
                          RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">,
                          AltVSXFMARel;
  }

  let BaseName = "XVNMSUBADP" in {
  let isCommutable = 1 in
  def XVNMSUBADP : XX3Form<60, 241,
                          (outs vsrc:$XT), (ins vsrc:$XTi, vsrc:$XA, vsrc:$XB),
                          "xvnmsubadp $XT, $XA, $XB", IIC_VecFP,
                          [(set v2f64:$XT, (fneg (any_fma v2f64:$XA, v2f64:$XB, (fneg v2f64:$XTi))))]>,
                          RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">,
                          AltVSXFMARel;
  let IsVSXFMAAlt = 1 in
  def XVNMSUBMDP : XX3Form<60, 249,
                          (outs vsrc:$XT), (ins vsrc:$XTi, vsrc:$XA, vsrc:$XB),
                          "xvnmsubmdp $XT, $XA, $XB", IIC_VecFP, []>,
                          RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">,
                          AltVSXFMARel;
  }

  let BaseName = "XVNMSUBASP" in {
  let isCommutable = 1 in
  def XVNMSUBASP : XX3Form<60, 209,
                          (outs vsrc:$XT), (ins vsrc:$XTi, vsrc:$XA, vsrc:$XB),
                          "xvnmsubasp $XT, $XA, $XB", IIC_VecFP,
                          [(set v4f32:$XT, (fneg (any_fma v4f32:$XA, v4f32:$XB, (fneg v4f32:$XTi))))]>,
                          RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">,
                          AltVSXFMARel;
  let IsVSXFMAAlt = 1 in
  def XVNMSUBMSP : XX3Form<60, 217,
                          (outs vsrc:$XT), (ins vsrc:$XTi, vsrc:$XA, vsrc:$XB),
                          "xvnmsubmsp $XT, $XA, $XB", IIC_VecFP, []>,
                          RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">,
                          AltVSXFMARel;
  }

  // Division Instructions
  def XSDIVDP : XX3Form<60, 56,
                        (outs vsfrc:$XT), (ins vsfrc:$XA, vsfrc:$XB),
                        "xsdivdp $XT, $XA, $XB", IIC_FPDivD,
                        [(set f64:$XT, (any_fdiv f64:$XA, f64:$XB))]>;
  def XSSQRTDP : XX2Form<60, 75,
                        (outs vsfrc:$XT), (ins vsfrc:$XB),
                        "xssqrtdp $XT, $XB", IIC_FPSqrtD,
                        [(set f64:$XT, (any_fsqrt f64:$XB))]>;

  def XSREDP : XX2Form<60, 90,
                        (outs vsfrc:$XT), (ins vsfrc:$XB),
                        "xsredp $XT, $XB", IIC_VecFP,
                        [(set f64:$XT, (PPCfre f64:$XB))]>;
  def XSRSQRTEDP : XX2Form<60, 74,
                           (outs vsfrc:$XT), (ins vsfrc:$XB),
                           "xsrsqrtedp $XT, $XB", IIC_VecFP,
                           [(set f64:$XT, (PPCfrsqrte f64:$XB))]>;

  let mayRaiseFPException = 0 in {
  def XSTDIVDP : XX3Form_1<60, 61,
                         (outs crrc:$CR), (ins vsfrc:$XA, vsfrc:$XB),
                         "xstdivdp $CR, $XA, $XB", IIC_FPCompare, []>;
  def XSTSQRTDP : XX2Form_1<60, 106,
                          (outs crrc:$CR), (ins vsfrc:$XB),
                          "xstsqrtdp $CR, $XB", IIC_FPCompare,
                          [(set i32:$CR, (PPCftsqrt f64:$XB))]>;
  def XVTDIVDP : XX3Form_1<60, 125,
                         (outs crrc:$CR), (ins vsrc:$XA, vsrc:$XB),
                         "xvtdivdp $CR, $XA, $XB", IIC_FPCompare, []>;
  def XVTDIVSP : XX3Form_1<60, 93,
                         (outs crrc:$CR), (ins vsrc:$XA, vsrc:$XB),
                         "xvtdivsp $CR, $XA, $XB", IIC_FPCompare, []>;

  def XVTSQRTDP : XX2Form_1<60, 234,
                          (outs crrc:$CR), (ins vsrc:$XB),
                          "xvtsqrtdp $CR, $XB", IIC_FPCompare,
                          [(set i32:$CR, (PPCftsqrt v2f64:$XB))]>;
  def XVTSQRTSP : XX2Form_1<60, 170,
                          (outs crrc:$CR), (ins vsrc:$XB),
                          "xvtsqrtsp $CR, $XB", IIC_FPCompare,
                          [(set i32:$CR, (PPCftsqrt v4f32:$XB))]>;
  }

  def XVDIVDP : XX3Form<60, 120,
                        (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB),
                        "xvdivdp $XT, $XA, $XB", IIC_FPDivD,
                        [(set v2f64:$XT, (any_fdiv v2f64:$XA, v2f64:$XB))]>;
  def XVDIVSP : XX3Form<60, 88,
                        (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB),
                        "xvdivsp $XT, $XA, $XB", IIC_FPDivS,
                        [(set v4f32:$XT, (any_fdiv v4f32:$XA, v4f32:$XB))]>;

  def XVSQRTDP : XX2Form<60, 203,
                        (outs vsrc:$XT), (ins vsrc:$XB),
                        "xvsqrtdp $XT, $XB", IIC_FPSqrtD,
                        [(set v2f64:$XT, (any_fsqrt v2f64:$XB))]>;
  def XVSQRTSP : XX2Form<60, 139,
                        (outs vsrc:$XT), (ins vsrc:$XB),
                        "xvsqrtsp $XT, $XB", IIC_FPSqrtS,
                        [(set v4f32:$XT, (any_fsqrt v4f32:$XB))]>;

  def XVREDP : XX2Form<60, 218,
                        (outs vsrc:$XT), (ins vsrc:$XB),
                        "xvredp $XT, $XB", IIC_VecFP,
                        [(set v2f64:$XT, (PPCfre v2f64:$XB))]>;
  def XVRESP : XX2Form<60, 154,
                        (outs vsrc:$XT), (ins vsrc:$XB),
                        "xvresp $XT, $XB", IIC_VecFP,
                        [(set v4f32:$XT, (PPCfre v4f32:$XB))]>;

  def XVRSQRTEDP : XX2Form<60, 202,
                           (outs vsrc:$XT), (ins vsrc:$XB),
                           "xvrsqrtedp $XT, $XB", IIC_VecFP,
                           [(set v2f64:$XT, (PPCfrsqrte v2f64:$XB))]>;
  def XVRSQRTESP : XX2Form<60, 138,
                           (outs vsrc:$XT), (ins vsrc:$XB),
                           "xvrsqrtesp $XT, $XB", IIC_VecFP,
                           [(set v4f32:$XT, (PPCfrsqrte v4f32:$XB))]>;

  // Compare Instructions
  def XSCMPODP : XX3Form_1<60, 43,
                           (outs crrc:$CR), (ins vsfrc:$XA, vsfrc:$XB),
                           "xscmpodp $CR, $XA, $XB", IIC_FPCompare, []>;
  def XSCMPUDP : XX3Form_1<60, 35,
                           (outs crrc:$CR), (ins vsfrc:$XA, vsfrc:$XB),
                           "xscmpudp $CR, $XA, $XB", IIC_FPCompare, []>;

  defm XVCMPEQDP : XX3Form_Rcr<60, 99,
                             "xvcmpeqdp", "$XT, $XA, $XB", IIC_VecFPCompare,
                             int_ppc_vsx_xvcmpeqdp, v2i64, v2f64>;
  defm XVCMPEQSP : XX3Form_Rcr<60, 67,
                             "xvcmpeqsp", "$XT, $XA, $XB", IIC_VecFPCompare,
                             int_ppc_vsx_xvcmpeqsp, v4i32, v4f32>;
  defm XVCMPGEDP : XX3Form_Rcr<60, 115,
                             "xvcmpgedp", "$XT, $XA, $XB", IIC_VecFPCompare,
                             int_ppc_vsx_xvcmpgedp, v2i64, v2f64>;
  defm XVCMPGESP : XX3Form_Rcr<60, 83,
                             "xvcmpgesp", "$XT, $XA, $XB", IIC_VecFPCompare,
                             int_ppc_vsx_xvcmpgesp, v4i32, v4f32>;
  defm XVCMPGTDP : XX3Form_Rcr<60, 107,
                             "xvcmpgtdp", "$XT, $XA, $XB", IIC_VecFPCompare,
                             int_ppc_vsx_xvcmpgtdp, v2i64, v2f64>;
  defm XVCMPGTSP : XX3Form_Rcr<60, 75,
                             "xvcmpgtsp", "$XT, $XA, $XB", IIC_VecFPCompare,
                             int_ppc_vsx_xvcmpgtsp, v4i32, v4f32>;

  // Move Instructions
  let mayRaiseFPException = 0 in {
  def XSABSDP : XX2Form<60, 345,
                      (outs vsfrc:$XT), (ins vsfrc:$XB),
                      "xsabsdp $XT, $XB", IIC_VecFP,
                      [(set f64:$XT, (fabs f64:$XB))]>;
  def XSNABSDP : XX2Form<60, 361,
                      (outs vsfrc:$XT), (ins vsfrc:$XB),
                      "xsnabsdp $XT, $XB", IIC_VecFP,
                      [(set f64:$XT, (fneg (fabs f64:$XB)))]>;
  let isCodeGenOnly = 1 in
  def XSNABSDPs : XX2Form<60, 361,
                      (outs vssrc:$XT), (ins vssrc:$XB),
                      "xsnabsdp $XT, $XB", IIC_VecFP,
                      [(set f32:$XT, (fneg (fabs f32:$XB)))]>;
  def XSNEGDP : XX2Form<60, 377,
                      (outs vsfrc:$XT), (ins vsfrc:$XB),
                      "xsnegdp $XT, $XB", IIC_VecFP,
                      [(set f64:$XT, (fneg f64:$XB))]>;
  def XSCPSGNDP : XX3Form<60, 176,
                      (outs vsfrc:$XT), (ins vsfrc:$XA, vsfrc:$XB),
                      "xscpsgndp $XT, $XA, $XB", IIC_VecFP,
                      [(set f64:$XT, (fcopysign f64:$XB, f64:$XA))]>;

  def XVABSDP : XX2Form<60, 473,
                      (outs vsrc:$XT), (ins vsrc:$XB),
                      "xvabsdp $XT, $XB", IIC_VecFP,
                      [(set v2f64:$XT, (fabs v2f64:$XB))]>;

  def XVABSSP : XX2Form<60, 409,
                      (outs vsrc:$XT), (ins vsrc:$XB),
                      "xvabssp $XT, $XB", IIC_VecFP,
                      [(set v4f32:$XT, (fabs v4f32:$XB))]>;

  def XVCPSGNDP : XX3Form<60, 240,
                      (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB),
                      "xvcpsgndp $XT, $XA, $XB", IIC_VecFP,
                      [(set v2f64:$XT, (fcopysign v2f64:$XB, v2f64:$XA))]>;
  def XVCPSGNSP : XX3Form<60, 208,
                      (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB),
                      "xvcpsgnsp $XT, $XA, $XB", IIC_VecFP,
                      [(set v4f32:$XT, (fcopysign v4f32:$XB, v4f32:$XA))]>;

  def XVNABSDP : XX2Form<60, 489,
                      (outs vsrc:$XT), (ins vsrc:$XB),
                      "xvnabsdp $XT, $XB", IIC_VecFP,
                      [(set v2f64:$XT, (fneg (fabs v2f64:$XB)))]>;
  def XVNABSSP : XX2Form<60, 425,
                      (outs vsrc:$XT), (ins vsrc:$XB),
                      "xvnabssp $XT, $XB", IIC_VecFP,
                      [(set v4f32:$XT, (fneg (fabs v4f32:$XB)))]>;

  def XVNEGDP : XX2Form<60, 505,
                      (outs vsrc:$XT), (ins vsrc:$XB),
                      "xvnegdp $XT, $XB", IIC_VecFP,
                      [(set v2f64:$XT, (fneg v2f64:$XB))]>;
  def XVNEGSP : XX2Form<60, 441,
                      (outs vsrc:$XT), (ins vsrc:$XB),
                      "xvnegsp $XT, $XB", IIC_VecFP,
                      [(set v4f32:$XT, (fneg v4f32:$XB))]>;
  }

  // Conversion Instructions
  def XSCVDPSP : XX2Form<60, 265,
                      (outs vsfrc:$XT), (ins vsfrc:$XB),
                      "xscvdpsp $XT, $XB", IIC_VecFP, []>;
  def XSCVDPSXDS : XX2Form<60, 344,
                      (outs vsfrc:$XT), (ins vsfrc:$XB),
                      "xscvdpsxds $XT, $XB", IIC_VecFP,
                      [(set f64:$XT, (PPCany_fctidz f64:$XB))]>;
  let isCodeGenOnly = 1 in
  def XSCVDPSXDSs : XX2Form<60, 344,
                      (outs vssrc:$XT), (ins vssrc:$XB),
                      "xscvdpsxds $XT, $XB", IIC_VecFP,
                      [(set f32:$XT, (PPCany_fctidz f32:$XB))]>;
  def XSCVDPSXWS : XX2Form<60, 88,
                      (outs vsfrc:$XT), (ins vsfrc:$XB),
                      "xscvdpsxws $XT, $XB", IIC_VecFP,
                      [(set f64:$XT, (PPCany_fctiwz f64:$XB))]>;
  let isCodeGenOnly = 1 in
  def XSCVDPSXWSs : XX2Form<60, 88,
                      (outs vssrc:$XT), (ins vssrc:$XB),
                      "xscvdpsxws $XT, $XB", IIC_VecFP,
                      [(set f32:$XT, (PPCany_fctiwz f32:$XB))]>;
  def XSCVDPUXDS : XX2Form<60, 328,
                      (outs vsfrc:$XT), (ins vsfrc:$XB),
                      "xscvdpuxds $XT, $XB", IIC_VecFP,
                      [(set f64:$XT, (PPCany_fctiduz f64:$XB))]>;
  let isCodeGenOnly = 1 in
  def XSCVDPUXDSs : XX2Form<60, 328,
                      (outs vssrc:$XT), (ins vssrc:$XB),
                      "xscvdpuxds $XT, $XB", IIC_VecFP,
                      [(set f32:$XT, (PPCany_fctiduz f32:$XB))]>;
  def XSCVDPUXWS : XX2Form<60, 72,
                      (outs vsfrc:$XT), (ins vsfrc:$XB),
                      "xscvdpuxws $XT, $XB", IIC_VecFP,
                      [(set f64:$XT, (PPCany_fctiwuz f64:$XB))]>;
  let isCodeGenOnly = 1 in
  def XSCVDPUXWSs : XX2Form<60, 72,
                      (outs vssrc:$XT), (ins vssrc:$XB),
                      "xscvdpuxws $XT, $XB", IIC_VecFP,
                      [(set f32:$XT, (PPCany_fctiwuz f32:$XB))]>;
  def XSCVSPDP : XX2Form<60, 329,
                      (outs vsfrc:$XT), (ins vsfrc:$XB),
                      "xscvspdp $XT, $XB", IIC_VecFP, []>;
  def XSCVSXDDP : XX2Form<60, 376,
                      (outs vsfrc:$XT), (ins vsfrc:$XB),
                      "xscvsxddp $XT, $XB", IIC_VecFP,
                      [(set f64:$XT, (PPCany_fcfid f64:$XB))]>;
  def XSCVUXDDP : XX2Form<60, 360,
                      (outs vsfrc:$XT), (ins vsfrc:$XB),
                      "xscvuxddp $XT, $XB", IIC_VecFP,
                      [(set f64:$XT, (PPCany_fcfidu f64:$XB))]>;

  def XVCVDPSP : XX2Form<60, 393,
                      (outs vsrc:$XT), (ins vsrc:$XB),
                      "xvcvdpsp $XT, $XB", IIC_VecFP,
                      [(set v4f32:$XT, (int_ppc_vsx_xvcvdpsp v2f64:$XB))]>;
  def XVCVDPSXDS : XX2Form<60, 472,
                      (outs vsrc:$XT), (ins vsrc:$XB),
                      "xvcvdpsxds $XT, $XB", IIC_VecFP,
                      [(set v2i64:$XT, (any_fp_to_sint v2f64:$XB))]>;
  def XVCVDPSXWS : XX2Form<60, 216,
                      (outs vsrc:$XT), (ins vsrc:$XB),
                      "xvcvdpsxws $XT, $XB", IIC_VecFP,
                      [(set v4i32:$XT, (int_ppc_vsx_xvcvdpsxws v2f64:$XB))]>;
  def XVCVDPUXDS : XX2Form<60, 456,
                      (outs vsrc:$XT), (ins vsrc:$XB),
                      "xvcvdpuxds $XT, $XB", IIC_VecFP,
                      [(set v2i64:$XT, (any_fp_to_uint v2f64:$XB))]>;
  def XVCVDPUXWS : XX2Form<60, 200,
                      (outs vsrc:$XT), (ins vsrc:$XB),
                      "xvcvdpuxws $XT, $XB", IIC_VecFP,
                      [(set v4i32:$XT, (int_ppc_vsx_xvcvdpuxws v2f64:$XB))]>;

  def XVCVSPDP : XX2Form<60, 457,
                      (outs vsrc:$XT), (ins vsrc:$XB),
                      "xvcvspdp $XT, $XB", IIC_VecFP,
                      [(set v2f64:$XT, (int_ppc_vsx_xvcvspdp v4f32:$XB))]>;
  def XVCVSPSXDS : XX2Form<60, 408,
                      (outs vsrc:$XT), (ins vsrc:$XB),
                      "xvcvspsxds $XT, $XB", IIC_VecFP,
                      [(set v2i64:$XT, (int_ppc_vsx_xvcvspsxds v4f32:$XB))]>;
  def XVCVSPSXWS : XX2Form<60, 152,
                      (outs vsrc:$XT), (ins vsrc:$XB),
                      "xvcvspsxws $XT, $XB", IIC_VecFP,
                      [(set v4i32:$XT, (any_fp_to_sint v4f32:$XB))]>;
  def XVCVSPUXDS : XX2Form<60, 392,
                      (outs vsrc:$XT), (ins vsrc:$XB),
                      "xvcvspuxds $XT, $XB", IIC_VecFP,
                      [(set v2i64:$XT, (int_ppc_vsx_xvcvspuxds v4f32:$XB))]>;
  def XVCVSPUXWS : XX2Form<60, 136,
                      (outs vsrc:$XT), (ins vsrc:$XB),
                      "xvcvspuxws $XT, $XB", IIC_VecFP,
                      [(set v4i32:$XT, (any_fp_to_uint v4f32:$XB))]>;
  def XVCVSXDDP : XX2Form<60, 504,
                      (outs vsrc:$XT), (ins vsrc:$XB),
                      "xvcvsxddp $XT, $XB", IIC_VecFP,
                      [(set v2f64:$XT, (any_sint_to_fp v2i64:$XB))]>;
  def XVCVSXDSP : XX2Form<60, 440,
                      (outs vsrc:$XT), (ins vsrc:$XB),
                      "xvcvsxdsp $XT, $XB", IIC_VecFP,
                      [(set v4f32:$XT, (int_ppc_vsx_xvcvsxdsp v2i64:$XB))]>;
  def XVCVSXWSP : XX2Form<60, 184,
                      (outs vsrc:$XT), (ins vsrc:$XB),
                      "xvcvsxwsp $XT, $XB", IIC_VecFP,
                      [(set v4f32:$XT, (any_sint_to_fp v4i32:$XB))]>;
  def XVCVUXDDP : XX2Form<60, 488,
                      (outs vsrc:$XT), (ins vsrc:$XB),
                      "xvcvuxddp $XT, $XB", IIC_VecFP,
                      [(set v2f64:$XT, (any_uint_to_fp v2i64:$XB))]>;
  def XVCVUXDSP : XX2Form<60, 424,
                      (outs vsrc:$XT), (ins vsrc:$XB),
                      "xvcvuxdsp $XT, $XB", IIC_VecFP,
                      [(set v4f32:$XT, (int_ppc_vsx_xvcvuxdsp v2i64:$XB))]>;
  def XVCVUXWSP : XX2Form<60, 168,
                      (outs vsrc:$XT), (ins vsrc:$XB),
                      "xvcvuxwsp $XT, $XB", IIC_VecFP,
                      [(set v4f32:$XT, (any_uint_to_fp v4i32:$XB))]>;

  let mayRaiseFPException = 0 in {
  def XVCVSXWDP : XX2Form<60, 248,
                    (outs vsrc:$XT), (ins vsrc:$XB),
                    "xvcvsxwdp $XT, $XB", IIC_VecFP,
                    [(set v2f64:$XT, (int_ppc_vsx_xvcvsxwdp v4i32:$XB))]>;
  def XVCVUXWDP : XX2Form<60, 232,
                      (outs vsrc:$XT), (ins vsrc:$XB),
                      "xvcvuxwdp $XT, $XB", IIC_VecFP,
                      [(set v2f64:$XT, (int_ppc_vsx_xvcvuxwdp v4i32:$XB))]>;
  }

  // Rounding Instructions respecting current rounding mode
  def XSRDPIC : XX2Form<60, 107,
                      (outs vsfrc:$XT), (ins vsfrc:$XB),
                      "xsrdpic $XT, $XB", IIC_VecFP, []>;
  def XVRDPIC : XX2Form<60, 235,
                      (outs vsrc:$XT), (ins vsrc:$XB),
                      "xvrdpic $XT, $XB", IIC_VecFP, []>;
  def XVRSPIC : XX2Form<60, 171,
                      (outs vsrc:$XT), (ins vsrc:$XB),
                      "xvrspic $XT, $XB", IIC_VecFP, []>;
  // Max/Min Instructions
  let isCommutable = 1 in {
  def XSMAXDP : XX3Form<60, 160,
                        (outs vsfrc:$XT), (ins vsfrc:$XA, vsfrc:$XB),
                        "xsmaxdp $XT, $XA, $XB", IIC_VecFP,
                        [(set vsfrc:$XT,
                              (int_ppc_vsx_xsmaxdp vsfrc:$XA, vsfrc:$XB))]>;
  def XSMINDP : XX3Form<60, 168,
                        (outs vsfrc:$XT), (ins vsfrc:$XA, vsfrc:$XB),
                        "xsmindp $XT, $XA, $XB", IIC_VecFP,
                        [(set vsfrc:$XT,
                              (int_ppc_vsx_xsmindp vsfrc:$XA, vsfrc:$XB))]>;

  def XVMAXDP : XX3Form<60, 224,
                        (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB),
                        "xvmaxdp $XT, $XA, $XB", IIC_VecFP,
                        [(set vsrc:$XT,
                              (int_ppc_vsx_xvmaxdp vsrc:$XA, vsrc:$XB))]>;
  def XVMINDP : XX3Form<60, 232,
                        (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB),
                        "xvmindp $XT, $XA, $XB", IIC_VecFP,
                        [(set vsrc:$XT,
                              (int_ppc_vsx_xvmindp vsrc:$XA, vsrc:$XB))]>;

  def XVMAXSP : XX3Form<60, 192,
                        (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB),
                        "xvmaxsp $XT, $XA, $XB", IIC_VecFP,
                        [(set vsrc:$XT,
                              (int_ppc_vsx_xvmaxsp vsrc:$XA, vsrc:$XB))]>;
  def XVMINSP : XX3Form<60, 200,
                        (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB),
                        "xvminsp $XT, $XA, $XB", IIC_VecFP,
                        [(set vsrc:$XT,
                              (int_ppc_vsx_xvminsp vsrc:$XA, vsrc:$XB))]>;
  } // isCommutable
  } // Uses = [RM]

  // Rounding Instructions with static direction.
  def XSRDPI : XX2Form<60, 73,
                      (outs vsfrc:$XT), (ins vsfrc:$XB),
                      "xsrdpi $XT, $XB", IIC_VecFP,
                      [(set f64:$XT, (any_fround f64:$XB))]>;
  def XSRDPIM : XX2Form<60, 121,
                      (outs vsfrc:$XT), (ins vsfrc:$XB),
                      "xsrdpim $XT, $XB", IIC_VecFP,
                      [(set f64:$XT, (any_ffloor f64:$XB))]>;
  def XSRDPIP : XX2Form<60, 105,
                      (outs vsfrc:$XT), (ins vsfrc:$XB),
                      "xsrdpip $XT, $XB", IIC_VecFP,
                      [(set f64:$XT, (any_fceil f64:$XB))]>;
  def XSRDPIZ : XX2Form<60, 89,
                      (outs vsfrc:$XT), (ins vsfrc:$XB),
                      "xsrdpiz $XT, $XB", IIC_VecFP,
                      [(set f64:$XT, (any_ftrunc f64:$XB))]>;

  def XVRDPI : XX2Form<60, 201,
                      (outs vsrc:$XT), (ins vsrc:$XB),
                      "xvrdpi $XT, $XB", IIC_VecFP,
                      [(set v2f64:$XT, (any_fround v2f64:$XB))]>;
  def XVRDPIM : XX2Form<60, 249,
                      (outs vsrc:$XT), (ins vsrc:$XB),
                      "xvrdpim $XT, $XB", IIC_VecFP,
                      [(set v2f64:$XT, (any_ffloor v2f64:$XB))]>;
  def XVRDPIP : XX2Form<60, 233,
                      (outs vsrc:$XT), (ins vsrc:$XB),
                      "xvrdpip $XT, $XB", IIC_VecFP,
                      [(set v2f64:$XT, (any_fceil v2f64:$XB))]>;
  def XVRDPIZ : XX2Form<60, 217,
                      (outs vsrc:$XT), (ins vsrc:$XB),
                      "xvrdpiz $XT, $XB", IIC_VecFP,
                      [(set v2f64:$XT, (any_ftrunc v2f64:$XB))]>;

  def XVRSPI : XX2Form<60, 137,
                      (outs vsrc:$XT), (ins vsrc:$XB),
                      "xvrspi $XT, $XB", IIC_VecFP,
                      [(set v4f32:$XT, (any_fround v4f32:$XB))]>;
  def XVRSPIM : XX2Form<60, 185,
                      (outs vsrc:$XT), (ins vsrc:$XB),
                      "xvrspim $XT, $XB", IIC_VecFP,
                      [(set v4f32:$XT, (any_ffloor v4f32:$XB))]>;
  def XVRSPIP : XX2Form<60, 169,
                      (outs vsrc:$XT), (ins vsrc:$XB),
                      "xvrspip $XT, $XB", IIC_VecFP,
                      [(set v4f32:$XT, (any_fceil v4f32:$XB))]>;
  def XVRSPIZ : XX2Form<60, 153,
                      (outs vsrc:$XT), (ins vsrc:$XB),
                      "xvrspiz $XT, $XB", IIC_VecFP,
                      [(set v4f32:$XT, (any_ftrunc v4f32:$XB))]>;
  } // mayRaiseFPException

  // Logical Instructions
  let isCommutable = 1 in
  def XXLAND : XX3Form<60, 130,
                       (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB),
                       "xxland $XT, $XA, $XB", IIC_VecGeneral,
                       [(set v4i32:$XT, (and v4i32:$XA, v4i32:$XB))]>;
  def XXLANDC : XX3Form<60, 138,
                        (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB),
                        "xxlandc $XT, $XA, $XB", IIC_VecGeneral,
                        [(set v4i32:$XT, (and v4i32:$XA,
                                              (vnot v4i32:$XB)))]>;
  let isCommutable = 1 in {
  def XXLNOR : XX3Form<60, 162,
                       (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB),
                       "xxlnor $XT, $XA, $XB", IIC_VecGeneral,
                       [(set v4i32:$XT, (vnot (or v4i32:$XA,
                                               v4i32:$XB)))]>;
  def XXLOR : XX3Form<60, 146,
                      (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB),
                      "xxlor $XT, $XA, $XB", IIC_VecGeneral,
                      [(set v4i32:$XT, (or v4i32:$XA, v4i32:$XB))]>;
  let isCodeGenOnly = 1 in
  def XXLORf: XX3Form<60, 146,
                      (outs vsfrc:$XT), (ins vsfrc:$XA, vsfrc:$XB),
                      "xxlor $XT, $XA, $XB", IIC_VecGeneral, []>;
  def XXLXOR : XX3Form<60, 154,
                       (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB),
                       "xxlxor $XT, $XA, $XB", IIC_VecGeneral,
                       [(set v4i32:$XT, (xor v4i32:$XA, v4i32:$XB))]>;
  } // isCommutable

  let isCodeGenOnly = 1, isMoveImm = 1, isAsCheapAsAMove = 1,
      isReMaterializable = 1 in {
    def XXLXORz : XX3Form_SameOp<60, 154, (outs vsrc:$XT), (ins),
                       "xxlxor $XT, $XT, $XT", IIC_VecGeneral,
                       [(set v4i32:$XT, (v4i32 immAllZerosV))]>;
    def XXLXORdpz : XX3Form_SameOp<60, 154,
                         (outs vsfrc:$XT), (ins),
                         "xxlxor $XT, $XT, $XT", IIC_VecGeneral,
                         [(set f64:$XT, (fpimm0))]>;
    def XXLXORspz : XX3Form_SameOp<60, 154,
                         (outs vssrc:$XT), (ins),
                         "xxlxor $XT, $XT, $XT", IIC_VecGeneral,
                         [(set f32:$XT, (fpimm0))]>;
  }

  // Permutation Instructions
  def XXMRGHW : XX3Form<60, 18,
                       (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB),
                       "xxmrghw $XT, $XA, $XB", IIC_VecPerm, []>;
  def XXMRGLW : XX3Form<60, 50,
                       (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB),
                       "xxmrglw $XT, $XA, $XB", IIC_VecPerm, []>;

  def XXPERMDI : XX3Form_2<60, 10,
                       (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB, u2imm:$D),
                       "xxpermdi $XT, $XA, $XB, $D", IIC_VecPerm,
                       [(set v2i64:$XT, (PPCxxpermdi v2i64:$XA, v2i64:$XB,
                         imm32SExt16:$D))]>;
  let isCodeGenOnly = 1 in
  // Note that the input register class for `$XA` of XXPERMDIs is `vsfrc` which
  // is not the same with the input register class(`vsrc`) of XXPERMDI instruction.
  // We did this on purpose because:
  // 1: The input is primarily for loads that load a partial vector(LFIWZX,
  //    etc.), no need for SUBREG_TO_REG.
  // 2: With `vsfrc` register class, in the final assembly, float registers
  //    like `f0` are used instead of vector scalar register like `vs0`. This
  //    helps readability.
  def XXPERMDIs : XX3Form_2s<60, 10, (outs vsrc:$XT), (ins vsfrc:$XA, u2imm:$D),
                             "xxpermdi $XT, $XA, $XA, $D", IIC_VecPerm, []>;
  def XXSEL : XX4Form<60, 3,
                      (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB, vsrc:$XC),
                      "xxsel $XT, $XA, $XB, $XC", IIC_VecPerm, []>;

  def XXSLDWI : XX3Form_2<60, 2,
                       (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB, u2imm:$D),
                       "xxsldwi $XT, $XA, $XB, $D", IIC_VecPerm,
                       [(set v4i32:$XT, (PPCvecshl v4i32:$XA, v4i32:$XB,
                                                  imm32SExt16:$D))]>;

  let isCodeGenOnly = 1 in
  def XXSLDWIs : XX3Form_2s<60, 2,
                       (outs vsrc:$XT), (ins vsfrc:$XA, u2imm:$D),
                       "xxsldwi $XT, $XA, $XA, $D", IIC_VecPerm, []>;

  def XXSPLTW : XX2Form_2<60, 164,
                       (outs vsrc:$XT), (ins vsrc:$XB, u2imm:$D),
                       "xxspltw $XT, $XB, $D", IIC_VecPerm,
                       [(set v4i32:$XT,
                             (PPCxxsplt v4i32:$XB, imm32SExt16:$D))]>;
  let isCodeGenOnly = 1 in
  def XXSPLTWs : XX2Form_2<60, 164,
                       (outs vsrc:$XT), (ins vsfrc:$XB, u2imm:$D),
                       "xxspltw $XT, $XB, $D", IIC_VecPerm, []>;

// The following VSX instructions were introduced in Power ISA 2.07
let Predicates = [HasVSX, HasP8Vector] in {
  let isCommutable = 1 in {
    def XXLEQV : XX3Form<60, 186,
                         (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB),
                         "xxleqv $XT, $XA, $XB", IIC_VecGeneral,
                         [(set v4i32:$XT, (vnot (xor v4i32:$XA, v4i32:$XB)))]>;
    def XXLNAND : XX3Form<60, 178,
                          (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB),
                          "xxlnand $XT, $XA, $XB", IIC_VecGeneral,
                          [(set v4i32:$XT, (vnot (and v4i32:$XA, v4i32:$XB)))]>;
  } // isCommutable

  let isCodeGenOnly = 1, isMoveImm = 1, isAsCheapAsAMove = 1,
      isReMaterializable = 1 in {
    def XXLEQVOnes : XX3Form_SameOp<60, 186, (outs vsrc:$XT), (ins),
                         "xxleqv $XT, $XT, $XT", IIC_VecGeneral,
                         [(set v4i32:$XT, (bitconvert (v16i8 immAllOnesV)))]>;
  }

  def XXLORC : XX3Form<60, 170,
                       (outs vsrc:$XT), (ins vsrc:$XA, vsrc:$XB),
                       "xxlorc $XT, $XA, $XB", IIC_VecGeneral,
                       [(set v4i32:$XT, (or v4i32:$XA, (vnot v4i32:$XB)))]>;

  // VSX scalar loads introduced in ISA 2.07
  let mayLoad = 1, mayStore = 0 in {
    let CodeSize = 3 in
    def LXSSPX : XX1Form_memOp<31, 524, (outs vssrc:$XT), (ins (memrr $RA, $RB):$addr),
                         "lxsspx $XT, $addr", IIC_LdStLFD, []>;
    def LXSIWAX : XX1Form_memOp<31, 76, (outs vsfrc:$XT), (ins (memrr $RA, $RB):$addr),
                          "lxsiwax $XT, $addr", IIC_LdStLFD, []>;
    def LXSIWZX : XX1Form_memOp<31, 12, (outs vsfrc:$XT), (ins (memrr $RA, $RB):$addr),
                          "lxsiwzx $XT, $addr", IIC_LdStLFD, []>;

    // Pseudo instruction XFLOADf32 will be expanded to LXSSPX or LFSX later
    let CodeSize = 3 in
    def XFLOADf32  : PseudoXFormMemOp<(outs vssrc:$XT), (ins memrr:$src),
                            "#XFLOADf32",
                            [(set f32:$XT, (load XForm:$src))]>;
    // Pseudo instruction LIWAX will be expanded to LXSIWAX or LFIWAX later
    def LIWAX : PseudoXFormMemOp<(outs vsfrc:$XT), (ins memrr:$src),
                       "#LIWAX",
                       [(set f64:$XT, (PPClfiwax ForceXForm:$src))]>;
    // Pseudo instruction LIWZX will be expanded to LXSIWZX or LFIWZX later
    def LIWZX : PseudoXFormMemOp<(outs vsfrc:$XT), (ins memrr:$src),
                       "#LIWZX",
                       [(set f64:$XT, (PPClfiwzx ForceXForm:$src))]>;
  } // mayLoad

  // VSX scalar stores introduced in ISA 2.07
  let mayStore = 1, mayLoad = 0 in {
    let CodeSize = 3 in
    def STXSSPX : XX1Form_memOp<31, 652, (outs), (ins vssrc:$XT, (memrr $RA, $RB):$addr),
                          "stxsspx $XT, $addr", IIC_LdStSTFD, []>;
    def STXSIWX : XX1Form_memOp<31, 140, (outs), (ins vsfrc:$XT, (memrr $RA, $RB):$addr),
                          "stxsiwx $XT, $addr", IIC_LdStSTFD, []>;

    // Pseudo instruction XFSTOREf32 will be expanded to STXSSPX or STFSX later
    let CodeSize = 3 in
    def XFSTOREf32 : PseudoXFormMemOp<(outs), (ins vssrc:$XT, memrr:$dst),
                            "#XFSTOREf32",
                            [(store f32:$XT, XForm:$dst)]>;
    // Pseudo instruction STIWX will be expanded to STXSIWX or STFIWX later
    def STIWX : PseudoXFormMemOp<(outs), (ins vsfrc:$XT, memrr:$dst),
                       "#STIWX",
                      [(PPCstfiwx f64:$XT, ForceXForm:$dst)]>;
  } // mayStore

  // VSX Elementary Scalar FP arithmetic (SP)
  let mayRaiseFPException = 1 in {
  let isCommutable = 1 in {
    def XSADDSP : XX3Form<60, 0,
                          (outs vssrc:$XT), (ins vssrc:$XA, vssrc:$XB),
                          "xsaddsp $XT, $XA, $XB", IIC_VecFP,
                          [(set f32:$XT, (any_fadd f32:$XA, f32:$XB))]>;
    def XSMULSP : XX3Form<60, 16,
                          (outs vssrc:$XT), (ins vssrc:$XA, vssrc:$XB),
                          "xsmulsp $XT, $XA, $XB", IIC_VecFP,
                          [(set f32:$XT, (any_fmul f32:$XA, f32:$XB))]>;
  } // isCommutable

  def XSSUBSP : XX3Form<60, 8,
                        (outs vssrc:$XT), (ins vssrc:$XA, vssrc:$XB),
                        "xssubsp $XT, $XA, $XB", IIC_VecFP,
                        [(set f32:$XT, (any_fsub f32:$XA, f32:$XB))]>;
  def XSDIVSP : XX3Form<60, 24,
                        (outs vssrc:$XT), (ins vssrc:$XA, vssrc:$XB),
                        "xsdivsp $XT, $XA, $XB", IIC_FPDivS,
                        [(set f32:$XT, (any_fdiv f32:$XA, f32:$XB))]>;

  def XSRESP : XX2Form<60, 26,
                        (outs vssrc:$XT), (ins vssrc:$XB),
                        "xsresp $XT, $XB", IIC_VecFP,
                        [(set f32:$XT, (PPCfre f32:$XB))]>;
  // FIXME: Setting the hasSideEffects flag here to match current behaviour.
  let hasSideEffects = 1 in
  def XSRSP : XX2Form<60, 281,
                        (outs vssrc:$XT), (ins vsfrc:$XB),
                        "xsrsp $XT, $XB", IIC_VecFP,
                        [(set f32:$XT, (any_fpround f64:$XB))]>;
  def XSSQRTSP : XX2Form<60, 11,
                        (outs vssrc:$XT), (ins vssrc:$XB),
                        "xssqrtsp $XT, $XB", IIC_FPSqrtS,
                        [(set f32:$XT, (any_fsqrt f32:$XB))]>;
  def XSRSQRTESP : XX2Form<60, 10,
                           (outs vssrc:$XT), (ins vssrc:$XB),
                           "xsrsqrtesp $XT, $XB", IIC_VecFP,
                           [(set f32:$XT, (PPCfrsqrte f32:$XB))]>;

  // FMA Instructions
  let BaseName = "XSMADDASP" in {
  let isCommutable = 1 in
  def XSMADDASP : XX3Form<60, 1,
                          (outs vssrc:$XT),
                          (ins vssrc:$XTi, vssrc:$XA, vssrc:$XB),
                          "xsmaddasp $XT, $XA, $XB", IIC_VecFP,
                          [(set f32:$XT, (any_fma f32:$XA, f32:$XB, f32:$XTi))]>,
                          RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">,
                          AltVSXFMARel;
  // FIXME: Setting the hasSideEffects flag here to match current behaviour.
  let IsVSXFMAAlt = 1, hasSideEffects = 1 in
  def XSMADDMSP : XX3Form<60, 9,
                          (outs vssrc:$XT),
                          (ins vssrc:$XTi, vssrc:$XA, vssrc:$XB),
                          "xsmaddmsp $XT, $XA, $XB", IIC_VecFP, []>,
                          RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">,
                          AltVSXFMARel;
  }

  let BaseName = "XSMSUBASP" in {
  let isCommutable = 1 in
  def XSMSUBASP : XX3Form<60, 17,
                          (outs vssrc:$XT),
                          (ins vssrc:$XTi, vssrc:$XA, vssrc:$XB),
                          "xsmsubasp $XT, $XA, $XB", IIC_VecFP,
                          [(set f32:$XT, (any_fma f32:$XA, f32:$XB,
                                              (fneg f32:$XTi)))]>,
                          RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">,
                          AltVSXFMARel;
  // FIXME: Setting the hasSideEffects flag here to match current behaviour.
  let IsVSXFMAAlt = 1, hasSideEffects = 1 in
  def XSMSUBMSP : XX3Form<60, 25,
                          (outs vssrc:$XT),
                          (ins vssrc:$XTi, vssrc:$XA, vssrc:$XB),
                          "xsmsubmsp $XT, $XA, $XB", IIC_VecFP, []>,
                          RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">,
                          AltVSXFMARel;
  }

  let BaseName = "XSNMADDASP" in {
  let isCommutable = 1 in
  def XSNMADDASP : XX3Form<60, 129,
                          (outs vssrc:$XT),
                          (ins vssrc:$XTi, vssrc:$XA, vssrc:$XB),
                          "xsnmaddasp $XT, $XA, $XB", IIC_VecFP,
                          [(set f32:$XT, (fneg (any_fma f32:$XA, f32:$XB,
                                                    f32:$XTi)))]>,
                          RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">,
                          AltVSXFMARel;
  // FIXME: Setting the hasSideEffects flag here to match current behaviour.
  let IsVSXFMAAlt = 1, hasSideEffects = 1 in
  def XSNMADDMSP : XX3Form<60, 137,
                          (outs vssrc:$XT),
                          (ins vssrc:$XTi, vssrc:$XA, vssrc:$XB),
                          "xsnmaddmsp $XT, $XA, $XB", IIC_VecFP, []>,
                          RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">,
                          AltVSXFMARel;
  }

  let BaseName = "XSNMSUBASP" in {
  let isCommutable = 1 in
  def XSNMSUBASP : XX3Form<60, 145,
                          (outs vssrc:$XT),
                          (ins vssrc:$XTi, vssrc:$XA, vssrc:$XB),
                          "xsnmsubasp $XT, $XA, $XB", IIC_VecFP,
                          [(set f32:$XT, (fneg (any_fma f32:$XA, f32:$XB,
                                                    (fneg f32:$XTi))))]>,
                          RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">,
                          AltVSXFMARel;
  // FIXME: Setting the hasSideEffects flag here to match current behaviour.
  let IsVSXFMAAlt = 1, hasSideEffects = 1 in
  def XSNMSUBMSP : XX3Form<60, 153,
                          (outs vssrc:$XT),
                          (ins vssrc:$XTi, vssrc:$XA, vssrc:$XB),
                          "xsnmsubmsp $XT, $XA, $XB", IIC_VecFP, []>,
                          RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">,
                          AltVSXFMARel;
  }

  // Single Precision Conversions (FP <-> INT)
  def XSCVSXDSP : XX2Form<60, 312,
                      (outs vssrc:$XT), (ins vsfrc:$XB),
                      "xscvsxdsp $XT, $XB", IIC_VecFP,
                      [(set f32:$XT, (PPCany_fcfids f64:$XB))]>;
  def XSCVUXDSP : XX2Form<60, 296,
                      (outs vssrc:$XT), (ins vsfrc:$XB),
                      "xscvuxdsp $XT, $XB", IIC_VecFP,
                      [(set f32:$XT, (PPCany_fcfidus f64:$XB))]>;
  } // mayRaiseFPException

  // Conversions between vector and scalar single precision
  def XSCVDPSPN : XX2Form<60, 267, (outs vsrc:$XT), (ins vssrc:$XB),
                          "xscvdpspn $XT, $XB", IIC_VecFP, []>;
  def XSCVSPDPN : XX2Form<60, 331, (outs vssrc:$XT), (ins vsrc:$XB),
                          "xscvspdpn $XT, $XB", IIC_VecFP, []>;

  let Predicates = [HasVSX, HasDirectMove] in {
  // VSX direct move instructions
  def MFVSRD : XX1_RS6_RD5_XO<31, 51, (outs g8rc:$RA), (ins vsfrc:$XT),
                              "mfvsrd $RA, $XT", IIC_VecGeneral,
                              [(set i64:$RA, (PPCmfvsr f64:$XT))]>,
      Requires<[In64BitMode]>;
  // FIXME: Setting the hasSideEffects flag here to match current behaviour.
  let isCodeGenOnly = 1, hasSideEffects = 1 in
  def MFVRD : XX1_RS6_RD5_XO<31, 51, (outs g8rc:$RA), (ins vsrc:$XT),
                             "mfvsrd $RA, $XT", IIC_VecGeneral,
                             []>,
      Requires<[In64BitMode]>;
  def MFVSRWZ : XX1_RS6_RD5_XO<31, 115, (outs gprc:$RA), (ins vsfrc:$XT),
                               "mfvsrwz $RA, $XT", IIC_VecGeneral,
                               [(set i32:$RA, (PPCmfvsr f64:$XT))]>, ZExt32To64;
  // FIXME: Setting the hasSideEffects flag here to match current behaviour.
  let isCodeGenOnly = 1, hasSideEffects = 1 in
  def MFVRWZ : XX1_RS6_RD5_XO<31, 115, (outs gprc:$RA), (ins vsrc:$XT),
                               "mfvsrwz $RA, $XT", IIC_VecGeneral,
                               []>;
  def MTVSRD : XX1_RS6_RD5_XO<31, 179, (outs vsfrc:$XT), (ins g8rc:$RA),
                              "mtvsrd $XT, $RA", IIC_VecGeneral,
                              [(set f64:$XT, (PPCmtvsra i64:$RA))]>,
      Requires<[In64BitMode]>;
  // FIXME: Setting the hasSideEffects flag here to match current behaviour.
  let isCodeGenOnly = 1, hasSideEffects = 1 in
  def MTVRD : XX1_RS6_RD5_XO<31, 179, (outs vsrc:$XT), (ins g8rc:$RA),
                              "mtvsrd $XT, $RA", IIC_VecGeneral,
                              []>,
      Requires<[In64BitMode]>;
  def MTVSRWA : XX1_RS6_RD5_XO<31, 211, (outs vsfrc:$XT), (ins gprc:$RA),
                               "mtvsrwa $XT, $RA", IIC_VecGeneral,
                               [(set f64:$XT, (PPCmtvsra i32:$RA))]>;
  // FIXME: Setting the hasSideEffects flag here to match current behaviour.
  let isCodeGenOnly = 1, hasSideEffects = 1 in
  def MTVRWA : XX1_RS6_RD5_XO<31, 211, (outs vsrc:$XT), (ins gprc:$RA),
                               "mtvsrwa $XT, $RA", IIC_VecGeneral,
                               []>;
  def MTVSRWZ : XX1_RS6_RD5_XO<31, 243, (outs vsfrc:$XT), (ins gprc:$RA),
                               "mtvsrwz $XT, $RA", IIC_VecGeneral,
                               [(set f64:$XT, (PPCmtvsrz i32:$RA))]>;
  // FIXME: Setting the hasSideEffects flag here to match current behaviour.
  let isCodeGenOnly = 1, hasSideEffects = 1 in
  def MTVRWZ : XX1_RS6_RD5_XO<31, 243, (outs vsrc:$XT), (ins gprc:$RA),
                               "mtvsrwz $XT, $RA", IIC_VecGeneral,
                               []>;
  } // HasDirectMove

} // HasVSX, HasP8Vector

let Predicates = [HasVSX, IsISA3_0, HasDirectMove] in {
def MTVSRWS: XX1_RS6_RD5_XO<31, 403, (outs vsrc:$XT), (ins gprc:$RA),
                            "mtvsrws $XT, $RA", IIC_VecGeneral, []>;

def MTVSRDD: XX1Form<31, 435, (outs vsrc:$XT), (ins g8rc_nox0:$RA, g8rc:$RB),
                     "mtvsrdd $XT, $RA, $RB", IIC_VecGeneral,
                     []>, Requires<[In64BitMode]>;

def MFVSRLD: XX1_RS6_RD5_XO<31, 307, (outs g8rc:$RA), (ins vsrc:$XT),
                            "mfvsrld $RA, $XT", IIC_VecGeneral,
                            []>, Requires<[In64BitMode]>;

} // HasVSX, IsISA3_0, HasDirectMove

let Predicates = [HasVSX, HasP9Vector] in {
  // Quad-Precision Scalar Move Instructions:
  // Copy Sign
  def XSCPSGNQP : X_VT5_VA5_VB5<63, 100, "xscpsgnqp",
                                [(set f128:$RST,
                                      (fcopysign f128:$RB, f128:$RA))]>;

  // Absolute/Negative-Absolute/Negate
  def XSABSQP   : X_VT5_XO5_VB5<63,  0, 804, "xsabsqp",
                                [(set f128:$RST, (fabs f128:$RB))]>;
  def XSNABSQP  : X_VT5_XO5_VB5<63,  8, 804, "xsnabsqp",
                                [(set f128:$RST, (fneg (fabs f128:$RB)))]>;
  def XSNEGQP   : X_VT5_XO5_VB5<63, 16, 804, "xsnegqp",
                                [(set f128:$RST, (fneg f128:$RB))]>;

  //===--------------------------------------------------------------------===//
  // Quad-Precision Scalar Floating-Point Arithmetic Instructions:

  // Add/Divide/Multiply/Subtract
  let mayRaiseFPException = 1 in {
  let isCommutable = 1 in {
  def XSADDQP   : X_VT5_VA5_VB5   <63,   4, "xsaddqp",
                                   [(set f128:$RST, (any_fadd f128:$RA, f128:$RB))]>;
  def XSMULQP   : X_VT5_VA5_VB5   <63,  36, "xsmulqp",
                                   [(set f128:$RST, (any_fmul f128:$RA, f128:$RB))]>;
  }
  def XSSUBQP   : X_VT5_VA5_VB5   <63, 516, "xssubqp" ,
                                   [(set f128:$RST, (any_fsub f128:$RA, f128:$RB))]>;
  def XSDIVQP   : X_VT5_VA5_VB5   <63, 548, "xsdivqp",
                                   [(set f128:$RST, (any_fdiv f128:$RA, f128:$RB))]>;
  // Square-Root
  def XSSQRTQP  : X_VT5_XO5_VB5   <63, 27, 804, "xssqrtqp",
                                   [(set f128:$RST, (any_fsqrt f128:$RB))]>;
  // (Negative) Multiply-{Add/Subtract}
  def XSMADDQP : X_VT5_VA5_VB5_FMA <63, 388, "xsmaddqp",
                                    [(set f128:$RST,
                                          (any_fma f128:$RA, f128:$RB, f128:$RSTi))]>;
  def XSMSUBQP  : X_VT5_VA5_VB5_FMA   <63, 420, "xsmsubqp"  ,
                                       [(set f128:$RST,
                                             (any_fma f128:$RA, f128:$RB,
                                                      (fneg f128:$RSTi)))]>;
  def XSNMADDQP : X_VT5_VA5_VB5_FMA <63, 452, "xsnmaddqp",
                                     [(set f128:$RST,
                                           (fneg (any_fma f128:$RA, f128:$RB,
                                                          f128:$RSTi)))]>;
  def XSNMSUBQP : X_VT5_VA5_VB5_FMA <63, 484, "xsnmsubqp",
                                     [(set f128:$RST,
                                           (fneg (any_fma f128:$RA, f128:$RB,
                                                          (fneg f128:$RSTi))))]>;

  let isCommutable = 1 in {
  def XSADDQPO : X_VT5_VA5_VB5_Ro<63, 4, "xsaddqpo",
                                  [(set f128:$RST,
                                  (int_ppc_addf128_round_to_odd
                                  f128:$RA, f128:$RB))]>;
  def XSMULQPO : X_VT5_VA5_VB5_Ro<63, 36, "xsmulqpo",
                                  [(set f128:$RST,
                                  (int_ppc_mulf128_round_to_odd
                                  f128:$RA, f128:$RB))]>;
  }
  def XSSUBQPO : X_VT5_VA5_VB5_Ro<63, 516, "xssubqpo",
                                  [(set f128:$RST,
                                  (int_ppc_subf128_round_to_odd
                                  f128:$RA, f128:$RB))]>;
  def XSDIVQPO : X_VT5_VA5_VB5_Ro<63, 548, "xsdivqpo",
                                  [(set f128:$RST,
                                  (int_ppc_divf128_round_to_odd
                                  f128:$RA, f128:$RB))]>;
  def XSSQRTQPO : X_VT5_XO5_VB5_Ro<63, 27, 804, "xssqrtqpo",
                                  [(set f128:$RST,
                                  (int_ppc_sqrtf128_round_to_odd f128:$RB))]>;


  def XSMADDQPO : X_VT5_VA5_VB5_FMA_Ro<63, 388, "xsmaddqpo",
                                      [(set f128:$RST,
                                      (int_ppc_fmaf128_round_to_odd
                                      f128:$RA,f128:$RB,f128:$RSTi))]>;

  def XSMSUBQPO : X_VT5_VA5_VB5_FMA_Ro<63, 420, "xsmsubqpo" ,
                                      [(set f128:$RST,
                                      (int_ppc_fmaf128_round_to_odd
                                      f128:$RA, f128:$RB, (fneg f128:$RSTi)))]>;
  def XSNMADDQPO: X_VT5_VA5_VB5_FMA_Ro<63, 452, "xsnmaddqpo",
                                      [(set f128:$RST,
                                      (fneg (int_ppc_fmaf128_round_to_odd
                                      f128:$RA, f128:$RB, f128:$RSTi)))]>;
  def XSNMSUBQPO: X_VT5_VA5_VB5_FMA_Ro<63, 484, "xsnmsubqpo",
                                      [(set f128:$RST,
                                      (fneg (int_ppc_fmaf128_round_to_odd
                                      f128:$RA, f128:$RB, (fneg f128:$RSTi))))]>;
  } // mayRaiseFPException

  // FIXME: Setting the hasSideEffects flag here to match current behaviour.
  // QP Compare Ordered/Unordered
  let hasSideEffects = 1 in {
    // DP/QP Compare Exponents
    def XSCMPEXPDP : XX3Form_1<60, 59,
                               (outs crrc:$CR), (ins vsfrc:$XA, vsfrc:$XB),
                               "xscmpexpdp $CR, $XA, $XB", IIC_FPCompare, []>;
    def XSCMPEXPQP : X_BF3_VA5_VB5<63, 164, "xscmpexpqp", []>;

    let mayRaiseFPException = 1 in {
    def XSCMPOQP : X_BF3_VA5_VB5<63, 132, "xscmpoqp", []>;
    def XSCMPUQP : X_BF3_VA5_VB5<63, 644, "xscmpuqp", []>;

    // DP Compare ==, >=, >, !=
    // Use vsrc for XT, because the entire register of XT is set.
    // XT.dword[1] = 0x0000_0000_0000_0000
    def XSCMPEQDP : XX3_XT5_XA5_XB5<60,  3, "xscmpeqdp", vsrc, vsfrc, vsfrc,
                                    IIC_FPCompare, []>;
    def XSCMPGEDP : XX3_XT5_XA5_XB5<60, 19, "xscmpgedp", vsrc, vsfrc, vsfrc,
                                    IIC_FPCompare, []>;
    def XSCMPGTDP : XX3_XT5_XA5_XB5<60, 11, "xscmpgtdp", vsrc, vsfrc, vsfrc,
                                    IIC_FPCompare, []>;
    }
  }

  //===--------------------------------------------------------------------===//
  // Quad-Precision Floating-Point Conversion Instructions:

  let mayRaiseFPException = 1 in {
    // Convert DP -> QP
    def XSCVDPQP  : X_VT5_XO5_VB5_TyVB<63, 22, 836, "xscvdpqp", vfrc,
                                       [(set f128:$RST, (any_fpextend f64:$RB))]>;

    // Round & Convert QP -> DP (dword[1] is set to zero)
    def XSCVQPDP  : X_VT5_XO5_VB5_VSFR<63, 20, 836, "xscvqpdp" , []>;
    def XSCVQPDPO : X_VT5_XO5_VB5_VSFR_Ro<63, 20, 836, "xscvqpdpo",
                                          [(set f64:$RST,
                                          (int_ppc_truncf128_round_to_odd
                                          f128:$RB))]>;
  }

  // Truncate & Convert QP -> (Un)Signed (D)Word (dword[1] is set to zero)
  let mayRaiseFPException = 1 in {
    def XSCVQPSDZ : X_VT5_XO5_VB5<63, 25, 836, "xscvqpsdz",
        [(set f128:$RST, (PPCany_fctidz f128:$RB))]>;
    def XSCVQPSWZ : X_VT5_XO5_VB5<63,  9, 836, "xscvqpswz",
        [(set f128:$RST, (PPCany_fctiwz f128:$RB))]>;
    def XSCVQPUDZ : X_VT5_XO5_VB5<63, 17, 836, "xscvqpudz",
        [(set f128:$RST, (PPCany_fctiduz f128:$RB))]>;
    def XSCVQPUWZ : X_VT5_XO5_VB5<63,  1, 836, "xscvqpuwz",
        [(set f128:$RST, (PPCany_fctiwuz f128:$RB))]>;
  }

  // Convert (Un)Signed DWord -> QP.
  def XSCVSDQP  : X_VT5_XO5_VB5_TyVB<63, 10, 836, "xscvsdqp", vfrc, []>;
  def XSCVUDQP  : X_VT5_XO5_VB5_TyVB<63,  2, 836, "xscvudqp", vfrc, []>;

  // (Round &) Convert DP <-> HP
  // Note! xscvdphp's src and dest register both use the left 64 bits, so we use
  // vsfrc for src and dest register. xscvhpdp's src only use the left 16 bits,
  // but we still use vsfrc for it.
  // FIXME: Setting the hasSideEffects flag here to match current behaviour.
  let hasSideEffects = 1, mayRaiseFPException = 1 in {
    def XSCVDPHP : XX2_XT6_XO5_XB6<60, 17, 347, "xscvdphp", vsfrc, []>;
    def XSCVHPDP : XX2_XT6_XO5_XB6<60, 16, 347, "xscvhpdp", vsfrc, []>;
  }

  let mayRaiseFPException = 1 in {
  // Vector HP -> SP
  // FIXME: Setting the hasSideEffects flag here to match current behaviour.
  let hasSideEffects = 1 in
  def XVCVHPSP : XX2_XT6_XO5_XB6<60, 24, 475, "xvcvhpsp", vsrc, []>;
  def XVCVSPHP : XX2_XT6_XO5_XB6<60, 25, 475, "xvcvsphp", vsrc,
                                 [(set v4f32:$XT,
                                     (int_ppc_vsx_xvcvsphp v4f32:$XB))]>;

  // Round to Quad-Precision Integer [with Inexact]
  def XSRQPI   : Z23_VT5_R1_VB5_RMC2_EX1<63,  5, 0, "xsrqpi" , []>;
  def XSRQPIX  : Z23_VT5_R1_VB5_RMC2_EX1<63,  5, 1, "xsrqpix", []>;

  // Round Quad-Precision to Double-Extended Precision (fp80)
  // FIXME: Setting the hasSideEffects flag here to match current behaviour.
  let hasSideEffects = 1 in
  def XSRQPXP  : Z23_VT5_R1_VB5_RMC2_EX1<63, 37, 0, "xsrqpxp", []>;
  }

  //===--------------------------------------------------------------------===//
  // Insert/Extract Instructions

  // Insert Exponent DP/QP
  // XT NOTE: XT.dword[1] = 0xUUUU_UUUU_UUUU_UUUU
  def XSIEXPDP : XX1Form <60, 918, (outs vsrc:$XT), (ins g8rc:$RA, g8rc:$RB),
                          "xsiexpdp $XT, $RA, $RB", IIC_VecFP, []>;
  // FIXME: Setting the hasSideEffects flag here to match current behaviour.
  let hasSideEffects = 1 in {
    // vB NOTE: only vB.dword[0] is used, that's why we don't use
    //          X_VT5_VA5_VB5 form
    def XSIEXPQP : XForm_18<63, 868, (outs vrrc:$FRT), (ins vrrc:$FRA, vsfrc:$FRB),
                            "xsiexpqp $FRT, $FRA, $FRB", IIC_VecFP, []>;
  }

  // Extract Exponent/Significand DP/QP
  def XSXEXPDP : XX2_RT5_XO5_XB6<60,  0, 347, "xsxexpdp", []>;
  def XSXSIGDP : XX2_RT5_XO5_XB6<60,  1, 347, "xsxsigdp", []>;

  // FIXME: Setting the hasSideEffects flag here to match current behaviour.
  let hasSideEffects = 1 in {
    def XSXEXPQP : X_VT5_XO5_VB5  <63,  2, 804, "xsxexpqp", []>;
    def XSXSIGQP : X_VT5_XO5_VB5  <63, 18, 804, "xsxsigqp", []>;
  }

  // Vector Insert Word
  // XB NOTE: Only XB.dword[1] is used, but we use vsrc on XB.
  def XXINSERTW   :
    XX2_RD6_UIM5_RS6<60, 181, (outs vsrc:$XT),
                     (ins vsrc:$XTi, vsrc:$XB, u4imm:$UIM5),
                     "xxinsertw $XT, $XB, $UIM5", IIC_VecFP,
                     [(set v4i32:$XT, (PPCvecinsert v4i32:$XTi, v4i32:$XB,
                                                   imm32SExt16:$UIM5))]>,
                     RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">;

  // Vector Extract Unsigned Word
  // FIXME: Setting the hasSideEffects flag here to match current behaviour.
  let hasSideEffects = 1 in
  def XXEXTRACTUW : XX2_RD6_UIM5_RS6<60, 165,
                                  (outs vsfrc:$XT), (ins vsrc:$XB, u4imm:$UIM5),
                                  "xxextractuw $XT, $XB, $UIM5", IIC_VecFP, []>;

  // Vector Insert Exponent DP/SP
  def XVIEXPDP : XX3_XT5_XA5_XB5<60, 248, "xviexpdp", vsrc, vsrc, vsrc,
    IIC_VecFP, [(set v2f64: $XT,(int_ppc_vsx_xviexpdp v2i64:$XA, v2i64:$XB))]>;
  def XVIEXPSP : XX3_XT5_XA5_XB5<60, 216, "xviexpsp", vsrc, vsrc, vsrc,
    IIC_VecFP, [(set v4f32: $XT,(int_ppc_vsx_xviexpsp v4i32:$XA, v4i32:$XB))]>;

  // Vector Extract Exponent/Significand DP/SP
  def XVXEXPDP : XX2_XT6_XO5_XB6<60,  0, 475, "xvxexpdp", vsrc,
                                 [(set v2i64: $XT,
                                  (int_ppc_vsx_xvxexpdp v2f64:$XB))]>;
  def XVXEXPSP : XX2_XT6_XO5_XB6<60,  8, 475, "xvxexpsp", vsrc,
                                 [(set v4i32: $XT,
                                  (int_ppc_vsx_xvxexpsp v4f32:$XB))]>;
  def XVXSIGDP : XX2_XT6_XO5_XB6<60,  1, 475, "xvxsigdp", vsrc,
                                 [(set v2i64: $XT,
                                  (int_ppc_vsx_xvxsigdp v2f64:$XB))]>;
  def XVXSIGSP : XX2_XT6_XO5_XB6<60,  9, 475, "xvxsigsp", vsrc,
                                 [(set v4i32: $XT,
                                  (int_ppc_vsx_xvxsigsp v4f32:$XB))]>;

  // Test Data Class SP/DP/QP
  // FIXME: Setting the hasSideEffects flag here to match current behaviour.
  let hasSideEffects = 1 in {
    def XSTSTDCSP : XX2_BF3_DCMX7_RS6<60, 298,
                                (outs crrc:$BF), (ins u7imm:$DCMX, vssrc:$XB),
                                "xststdcsp $BF, $XB, $DCMX", IIC_VecFP, []>;
    def XSTSTDCDP : XX2_BF3_DCMX7_RS6<60, 362,
                                (outs crrc:$BF), (ins u7imm:$DCMX, vsfrc:$XB),
                                "xststdcdp $BF, $XB, $DCMX", IIC_VecFP, []>;
    def XSTSTDCQP : X_BF3_DCMX7_RS5  <63, 708,
                                (outs crrc:$BF), (ins u7imm:$DCMX, vrrc:$VB),
                                "xststdcqp $BF, $VB, $DCMX", IIC_VecFP, []>;
  }

  // Vector Test Data Class SP/DP
  def XVTSTDCSP : XX2_RD6_DCMX7_RS6<60, 13, 5,
                              (outs vsrc:$XT), (ins u7imm:$DCMX, vsrc:$XB),
                              "xvtstdcsp $XT, $XB, $DCMX", IIC_VecFP,
                              [(set v4i32: $XT,
                               (int_ppc_vsx_xvtstdcsp v4f32:$XB, timm:$DCMX))]>;
  def XVTSTDCDP : XX2_RD6_DCMX7_RS6<60, 15, 5,
                              (outs vsrc:$XT), (ins u7imm:$DCMX, vsrc:$XB),
                              "xvtstdcdp $XT, $XB, $DCMX", IIC_VecFP,
                              [(set v2i64: $XT,
                               (int_ppc_vsx_xvtstdcdp v2f64:$XB, timm:$DCMX))]>;

  // Maximum/Minimum Type-C/Type-J DP
  let mayRaiseFPException = 1 in {
  def XSMAXCDP : XX3_XT5_XA5_XB5<60, 128, "xsmaxcdp", vsfrc, vsfrc, vsfrc,
                                 IIC_VecFP,
                                 [(set f64:$XT, (PPCxsmaxc f64:$XA, f64:$XB))]>;
  def XSMINCDP : XX3_XT5_XA5_XB5<60, 136, "xsmincdp", vsfrc, vsfrc, vsfrc,
                                 IIC_VecFP,
                                 [(set f64:$XT, (PPCxsminc f64:$XA, f64:$XB))]>;

  // FIXME: Setting the hasSideEffects flag here to match current behaviour.
  let hasSideEffects = 1 in {
    def XSMAXJDP : XX3_XT5_XA5_XB5<60, 144, "xsmaxjdp", vsrc, vsfrc, vsfrc,
                                   IIC_VecFP, []>;
    def XSMINJDP : XX3_XT5_XA5_XB5<60, 152, "xsminjdp", vsrc, vsfrc, vsfrc,
                                   IIC_VecFP, []>;
  }
  }

  // Vector Byte-Reverse H/W/D/Q Word
  // FIXME: Setting the hasSideEffects flag here to match current behaviour.
  let hasSideEffects = 1 in
  def XXBRH : XX2_XT6_XO5_XB6<60,  7, 475, "xxbrh", vsrc, []>;
  def XXBRW : XX2_XT6_XO5_XB6<60, 15, 475, "xxbrw", vsrc,
    [(set v4i32:$XT, (bswap v4i32:$XB))]>;
  def XXBRD : XX2_XT6_XO5_XB6<60, 23, 475, "xxbrd", vsrc,
    [(set v2i64:$XT, (bswap v2i64:$XB))]>;
  // FIXME: Setting the hasSideEffects flag here to match current behaviour.
  let hasSideEffects = 1 in
  def XXBRQ : XX2_XT6_XO5_XB6<60, 31, 475, "xxbrq", vsrc, []>;

  // Vector Permute
  def XXPERM  : XX3Form<60, 26, (outs vsrc:$XT),
                                (ins vsrc:$XA, vsrc:$XTi, vsrc:$XB),
                        "xxperm $XT, $XA, $XB", IIC_VecPerm, []>,
                        RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">;
  def XXPERMR : XX3Form<60, 58, (outs vsrc:$XT),
                                (ins vsrc:$XA, vsrc:$XTi, vsrc:$XB),
                        "xxpermr $XT, $XA, $XB", IIC_VecPerm, []>,
                        RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">;

  // Vector Splat Immediate Byte
  // FIXME: Setting the hasSideEffects flag here to match current behaviour.
  let hasSideEffects = 1 in
  def XXSPLTIB : X_RD6_IMM8<60, 360, (outs vsrc:$XT), (ins u8imm:$IMM8),
                            "xxspltib $XT, $IMM8", IIC_VecPerm, []>;

  // When adding new D-Form loads/stores, be sure to update the ImmToIdxMap in
  // PPCRegisterInfo::PPCRegisterInfo and maybe save yourself some debugging.
  let mayLoad = 1, mayStore = 0 in {
  // Load Vector
  def LXV : DQ_RD6_RS5_DQ12<61, 1, (outs vsrc:$XT), (ins (memrix16 $DQ, $RA):$addr),
                            "lxv $XT, $addr", IIC_LdStLFD, []>;
  // Load DWord
  def LXSD  : DSForm_1<57, 2, (outs vfrc:$RST), (ins (memrix $D, $RA):$addr),
                       "lxsd $RST, $addr", IIC_LdStLFD, []>;
  // Load SP from src, convert it to DP, and place in dword[0]
  def LXSSP : DSForm_1<57, 3, (outs vfrc:$RST), (ins (memrix $D, $RA):$addr),
                       "lxssp $RST, $addr", IIC_LdStLFD, []>;

  // Load as Integer Byte/Halfword & Zero Indexed
  def LXSIBZX : X_XT6_RA5_RB5<31, 781, "lxsibzx", vsfrc,
                              [(set f64:$XT, (PPClxsizx ForceXForm:$addr, 1))]>;
  def LXSIHZX : X_XT6_RA5_RB5<31, 813, "lxsihzx", vsfrc,
                              [(set f64:$XT, (PPClxsizx ForceXForm:$addr, 2))]>;

  // Load Vector Halfword*8/Byte*16 Indexed
  def LXVH8X  : X_XT6_RA5_RB5<31, 812, "lxvh8x" , vsrc, []>;
  def LXVB16X : X_XT6_RA5_RB5<31, 876, "lxvb16x", vsrc, []>;

  // Load Vector Indexed
  def LXVX    : X_XT6_RA5_RB5<31, 268, "lxvx"   , vsrc,
                [(set v2f64:$XT, (load XForm:$addr))]>;
  // Load Vector (Left-justified) with Length
  def LXVL : XX1Form_memOp<31, 269, (outs vsrc:$XT), (ins (memr $RA):$addr, g8rc:$RB),
                   "lxvl $XT, $addr, $RB", IIC_LdStLoad,
                   [(set v4i32:$XT, (int_ppc_vsx_lxvl addr:$addr, i64:$RB))]>;
  def LXVLL : XX1Form_memOp<31,301, (outs vsrc:$XT), (ins (memr $RA):$addr, g8rc:$RB),
                   "lxvll $XT, $addr, $RB", IIC_LdStLoad,
                   [(set v4i32:$XT, (int_ppc_vsx_lxvll addr:$addr, i64:$RB))]>;

  // Load Vector Word & Splat Indexed
  def LXVWSX  : X_XT6_RA5_RB5<31, 364, "lxvwsx" , vsrc, []>;
  } // mayLoad

  // When adding new D-Form loads/stores, be sure to update the ImmToIdxMap in
  // PPCRegisterInfo::PPCRegisterInfo and maybe save yourself some debugging.
  let mayStore = 1, mayLoad = 0 in {
  // Store Vector
  def STXV : DQ_RD6_RS5_DQ12<61, 5, (outs), (ins vsrc:$XT, (memrix16 $DQ, $RA):$addr),
                             "stxv $XT, $addr", IIC_LdStSTFD, []>;
  // Store DWord
  def STXSD  : DSForm_1<61, 2, (outs), (ins vfrc:$RST, (memrix $D, $RA):$addr),
                        "stxsd $RST, $addr", IIC_LdStSTFD, []>;
  // Convert DP of dword[0] to SP, and Store to dst
  def STXSSP : DSForm_1<61, 3, (outs), (ins vfrc:$RST, (memrix $D, $RA):$addr),
                        "stxssp $RST, $addr", IIC_LdStSTFD, []>;

  // Store as Integer Byte/Halfword Indexed
  def STXSIBX  : X_XS6_RA5_RB5<31,  909, "stxsibx" , vsfrc,
                               [(PPCstxsix f64:$XT, ForceXForm:$addr, 1)]>;
  def STXSIHX  : X_XS6_RA5_RB5<31,  941, "stxsihx" , vsfrc,
                               [(PPCstxsix f64:$XT, ForceXForm:$addr, 2)]>;
  let isCodeGenOnly = 1 in {
    def STXSIBXv  : X_XS6_RA5_RB5<31,  909, "stxsibx" , vsrc, []>;
    def STXSIHXv  : X_XS6_RA5_RB5<31,  941, "stxsihx" , vsrc, []>;
  }

  // Store Vector Halfword*8/Byte*16 Indexed
  def STXVH8X  : X_XS6_RA5_RB5<31,  940, "stxvh8x" , vsrc, []>;
  def STXVB16X : X_XS6_RA5_RB5<31, 1004, "stxvb16x", vsrc, []>;

  // Store Vector Indexed
  def STXVX    : X_XS6_RA5_RB5<31,  396, "stxvx"   , vsrc,
                 [(store v2f64:$XT, XForm:$addr)]>;

  // Store Vector (Left-justified) with Length
  def STXVL : XX1Form_memOp<31, 397, (outs),
                            (ins vsrc:$XT, (memr $RA):$addr, g8rc:$RB),
                            "stxvl $XT, $addr, $RB", IIC_LdStLoad,
                            [(int_ppc_vsx_stxvl v4i32:$XT, addr:$addr,
                              i64:$RB)]>;
  def STXVLL : XX1Form_memOp<31, 429, (outs),
                            (ins vsrc:$XT, (memr $RA):$addr, g8rc:$RB),
                            "stxvll $XT, $addr, $RB", IIC_LdStLoad,
                            [(int_ppc_vsx_stxvll v4i32:$XT, addr:$addr,
                              i64:$RB)]>;
  } // mayStore

  def DFLOADf32  : PPCPostRAExpPseudo<(outs vssrc:$XT), (ins memrix:$src),
                          "#DFLOADf32",
                          [(set f32:$XT, (load DSForm:$src))]>;
  def DFLOADf64  : PPCPostRAExpPseudo<(outs vsfrc:$XT), (ins memrix:$src),
                          "#DFLOADf64",
                          [(set f64:$XT, (load DSForm:$src))]>;
  def DFSTOREf32 : PPCPostRAExpPseudo<(outs), (ins vssrc:$XT, memrix:$dst),
                          "#DFSTOREf32",
                          [(store f32:$XT, DSForm:$dst)]>;
  def DFSTOREf64 : PPCPostRAExpPseudo<(outs), (ins vsfrc:$XT, memrix:$dst),
                          "#DFSTOREf64",
                          [(store f64:$XT, DSForm:$dst)]>;

  let mayStore = 1 in {
    def SPILLTOVSR_STX : PseudoXFormMemOp<(outs),
                                          (ins spilltovsrrc:$XT, memrr:$dst),
                                          "#SPILLTOVSR_STX", []>;
    def SPILLTOVSR_ST : PPCPostRAExpPseudo<(outs), (ins spilltovsrrc:$XT, memrix:$dst),
                              "#SPILLTOVSR_ST", []>;
  }
  let mayLoad = 1 in {
    def SPILLTOVSR_LDX : PseudoXFormMemOp<(outs spilltovsrrc:$XT),
                                          (ins memrr:$src),
                                          "#SPILLTOVSR_LDX", []>;
    def SPILLTOVSR_LD : PPCPostRAExpPseudo<(outs spilltovsrrc:$XT), (ins memrix:$src),
                              "#SPILLTOVSR_LD", []>;

  }
  } // HasP9Vector
} // hasSideEffects = 0

let PPC970_Single = 1, AddedComplexity = 400 in {

  def SELECT_CC_VSRC: PPCCustomInserterPseudo<(outs vsrc:$dst),
                             (ins crrc:$cond, vsrc:$T, vsrc:$F, i32imm:$BROPC),
                             "#SELECT_CC_VSRC",
                             []>;
  def SELECT_VSRC: PPCCustomInserterPseudo<(outs vsrc:$dst),
                          (ins crbitrc:$cond, vsrc:$T, vsrc:$F),
                          "#SELECT_VSRC",
                          [(set v2f64:$dst,
                                (select i1:$cond, v2f64:$T, v2f64:$F))]>;
  def SELECT_CC_VSFRC: PPCCustomInserterPseudo<(outs f8rc:$dst),
                              (ins crrc:$cond, f8rc:$T, f8rc:$F,
                               i32imm:$BROPC), "#SELECT_CC_VSFRC",
                              []>;
  def SELECT_VSFRC: PPCCustomInserterPseudo<(outs f8rc:$dst),
                           (ins crbitrc:$cond, f8rc:$T, f8rc:$F),
                           "#SELECT_VSFRC",
                           [(set f64:$dst,
                                 (select i1:$cond, f64:$T, f64:$F))]>;
  def SELECT_CC_VSSRC: PPCCustomInserterPseudo<(outs f4rc:$dst),
                              (ins crrc:$cond, f4rc:$T, f4rc:$F,
                               i32imm:$BROPC), "#SELECT_CC_VSSRC",
                              []>;
  def SELECT_VSSRC: PPCCustomInserterPseudo<(outs f4rc:$dst),
                           (ins crbitrc:$cond, f4rc:$T, f4rc:$F),
                           "#SELECT_VSSRC",
                           [(set f32:$dst,
                                 (select i1:$cond, f32:$T, f32:$F))]>;
}
}

//----------------------------- DAG Definitions ------------------------------//

// Output dag used to bitcast f32 to i32 and f64 to i64
def Bitcast {
  dag FltToInt = (i32 (MFVSRWZ (EXTRACT_SUBREG (XSCVDPSPN $A), sub_64)));
  dag DblToLong = (i64 (MFVSRD $A));
}

def FpMinMax {
  dag F32Min = (COPY_TO_REGCLASS (XSMINDP (COPY_TO_REGCLASS $A, VSFRC),
                                          (COPY_TO_REGCLASS $B, VSFRC)),
                                 VSSRC);
  dag F32Max = (COPY_TO_REGCLASS (XSMAXDP (COPY_TO_REGCLASS $A, VSFRC),
                                          (COPY_TO_REGCLASS $B, VSFRC)),
                                 VSSRC);
}

def ScalarLoads {
  dag Li8 =       (i32 (extloadi8 ForceXForm:$src));
  dag ZELi8 =     (i32 (zextloadi8 ForceXForm:$src));
  dag ZELi8i64 =  (i64 (zextloadi8 ForceXForm:$src));
  dag SELi8 =     (i32 (sext_inreg (extloadi8 ForceXForm:$src), i8));
  dag SELi8i64 =  (i64 (sext_inreg (extloadi8 ForceXForm:$src), i8));

  dag Li16 =      (i32 (extloadi16 ForceXForm:$src));
  dag ZELi16 =    (i32 (zextloadi16 ForceXForm:$src));
  dag ZELi16i64 = (i64 (zextloadi16 ForceXForm:$src));
  dag SELi16 =    (i32 (sextloadi16 ForceXForm:$src));
  dag SELi16i64 = (i64 (sextloadi16 ForceXForm:$src));

  dag Li32 = (i32 (load ForceXForm:$src));
}

def DWToSPExtractConv {
  dag El0US1 = (f32 (PPCfcfidus
                    (f64 (PPCmtvsra (i64 (vector_extract v2i64:$S1, 0))))));
  dag El1US1 = (f32 (PPCfcfidus
                    (f64 (PPCmtvsra (i64 (vector_extract v2i64:$S1, 1))))));
  dag El0US2 = (f32 (PPCfcfidus
                    (f64 (PPCmtvsra (i64 (vector_extract v2i64:$S2, 0))))));
  dag El1US2 = (f32 (PPCfcfidus
                    (f64 (PPCmtvsra (i64 (vector_extract v2i64:$S2, 1))))));
  dag El0SS1 = (f32 (PPCfcfids
                    (f64 (PPCmtvsra (i64 (vector_extract v2i64:$S1, 0))))));
  dag El1SS1 = (f32 (PPCfcfids
                    (f64 (PPCmtvsra (i64 (vector_extract v2i64:$S1, 1))))));
  dag El0SS2 = (f32 (PPCfcfids
                    (f64 (PPCmtvsra (i64 (vector_extract v2i64:$S2, 0))))));
  dag El1SS2 = (f32 (PPCfcfids
                    (f64 (PPCmtvsra (i64 (vector_extract v2i64:$S2, 1))))));
  dag BVU = (v4f32 (build_vector El0US1, El1US1, El0US2, El1US2));
  dag BVS = (v4f32 (build_vector El0SS1, El1SS1, El0SS2, El1SS2));
}

def WToDPExtractConv {
  dag El0S = (f64 (PPCfcfid (PPCmtvsra (extractelt v4i32:$A, 0))));
  dag El1S = (f64 (PPCfcfid (PPCmtvsra (extractelt v4i32:$A, 1))));
  dag El2S = (f64 (PPCfcfid (PPCmtvsra (extractelt v4i32:$A, 2))));
  dag El3S = (f64 (PPCfcfid (PPCmtvsra (extractelt v4i32:$A, 3))));
  dag El0U = (f64 (PPCfcfidu (PPCmtvsrz (extractelt v4i32:$A, 0))));
  dag El1U = (f64 (PPCfcfidu (PPCmtvsrz (extractelt v4i32:$A, 1))));
  dag El2U = (f64 (PPCfcfidu (PPCmtvsrz (extractelt v4i32:$A, 2))));
  dag El3U = (f64 (PPCfcfidu (PPCmtvsrz (extractelt v4i32:$A, 3))));
  dag BV02S = (v2f64 (build_vector El0S, El2S));
  dag BV13S = (v2f64 (build_vector El1S, El3S));
  dag BV02U = (v2f64 (build_vector El0U, El2U));
  dag BV13U = (v2f64 (build_vector El1U, El3U));
}

/*  Direct moves of various widths from GPR's into VSR's. Each move lines
    the value up into element 0 (both BE and LE). Namely, entities smaller than
    a doubleword are shifted left and moved for BE. For LE, they're moved, then
    swapped to go into the least significant element of the VSR.
*/
def MovesToVSR {
  dag BE_BYTE_0 =
    (MTVSRD
      (RLDICR
        (INSERT_SUBREG (i64 (IMPLICIT_DEF)), $A, sub_32), 56, 7));
  dag BE_HALF_0 =
    (MTVSRD
      (RLDICR
        (INSERT_SUBREG (i64 (IMPLICIT_DEF)), $A, sub_32), 48, 15));
  dag BE_WORD_0 =
    (MTVSRD
      (RLDICR
        (INSERT_SUBREG (i64 (IMPLICIT_DEF)), $A, sub_32), 32, 31));
  dag BE_DWORD_0 = (MTVSRD $A);

  dag LE_MTVSRW = (MTVSRD (INSERT_SUBREG (i64 (IMPLICIT_DEF)), $A, sub_32));
  dag LE_WORD_1 = (v2i64 (INSERT_SUBREG (v2i64 (IMPLICIT_DEF)),
                                        LE_MTVSRW, sub_64));
  dag LE_WORD_0 = (XXPERMDI LE_WORD_1, LE_WORD_1, 2);
  dag LE_DWORD_1 = (v2i64 (INSERT_SUBREG (v2i64 (IMPLICIT_DEF)),
                                         BE_DWORD_0, sub_64));
  dag LE_DWORD_0 = (XXPERMDI LE_DWORD_1, LE_DWORD_1, 2);
}

/*  Patterns for extracting elements out of vectors. Integer elements are
    extracted using direct move operations. Patterns for extracting elements
    whose indices are not available at compile time are also provided with
    various _VARIABLE_ patterns.
    The numbering for the DAG's is for LE, but when used on BE, the correct
    LE element can just be used (i.e. LE_BYTE_2 == BE_BYTE_13).
*/
def VectorExtractions {
  // Doubleword extraction
  dag LE_DWORD_0 =
    (MFVSRD
      (EXTRACT_SUBREG
        (XXPERMDI (COPY_TO_REGCLASS $S, VSRC),
                  (COPY_TO_REGCLASS $S, VSRC), 2), sub_64));
  dag LE_DWORD_1 = (MFVSRD
                     (EXTRACT_SUBREG
                       (v2i64 (COPY_TO_REGCLASS $S, VSRC)), sub_64));

  // Word extraction
  dag LE_WORD_0 = (MFVSRWZ (EXTRACT_SUBREG (XXPERMDI $S, $S, 2), sub_64));
  dag LE_WORD_1 = (MFVSRWZ (EXTRACT_SUBREG (XXSLDWI $S, $S, 1), sub_64));
  dag LE_WORD_2 = (MFVSRWZ (EXTRACT_SUBREG
                             (v2i64 (COPY_TO_REGCLASS $S, VSRC)), sub_64));
  dag LE_WORD_3 = (MFVSRWZ (EXTRACT_SUBREG (XXSLDWI $S, $S, 3), sub_64));

  // Halfword extraction
  dag LE_HALF_0 = (i32 (EXTRACT_SUBREG (RLDICL LE_DWORD_0, 0, 48), sub_32));
  dag LE_HALF_1 = (i32 (EXTRACT_SUBREG (RLDICL LE_DWORD_0, 48, 48), sub_32));
  dag LE_HALF_2 = (i32 (EXTRACT_SUBREG (RLDICL LE_DWORD_0, 32, 48), sub_32));
  dag LE_HALF_3 = (i32 (EXTRACT_SUBREG (RLDICL LE_DWORD_0, 16, 48), sub_32));
  dag LE_HALF_4 = (i32 (EXTRACT_SUBREG (RLDICL LE_DWORD_1, 0, 48), sub_32));
  dag LE_HALF_5 = (i32 (EXTRACT_SUBREG (RLDICL LE_DWORD_1, 48, 48), sub_32));
  dag LE_HALF_6 = (i32 (EXTRACT_SUBREG (RLDICL LE_DWORD_1, 32, 48), sub_32));
  dag LE_HALF_7 = (i32 (EXTRACT_SUBREG (RLDICL LE_DWORD_1, 16, 48), sub_32));

  // Byte extraction
  dag LE_BYTE_0 = (i32 (EXTRACT_SUBREG (RLDICL LE_DWORD_0, 0, 56), sub_32));
  dag LE_BYTE_1 = (i32 (EXTRACT_SUBREG (RLDICL LE_DWORD_0, 56, 56), sub_32));
  dag LE_BYTE_2 = (i32 (EXTRACT_SUBREG (RLDICL LE_DWORD_0, 48, 56), sub_32));
  dag LE_BYTE_3 = (i32 (EXTRACT_SUBREG (RLDICL LE_DWORD_0, 40, 56), sub_32));
  dag LE_BYTE_4 = (i32 (EXTRACT_SUBREG (RLDICL LE_DWORD_0, 32, 56), sub_32));
  dag LE_BYTE_5 = (i32 (EXTRACT_SUBREG (RLDICL LE_DWORD_0, 24, 56), sub_32));
  dag LE_BYTE_6 = (i32 (EXTRACT_SUBREG (RLDICL LE_DWORD_0, 16, 56), sub_32));
  dag LE_BYTE_7 = (i32 (EXTRACT_SUBREG (RLDICL LE_DWORD_0, 8, 56), sub_32));
  dag LE_BYTE_8 = (i32 (EXTRACT_SUBREG (RLDICL LE_DWORD_1, 0, 56), sub_32));
  dag LE_BYTE_9 = (i32 (EXTRACT_SUBREG (RLDICL LE_DWORD_1, 56, 56), sub_32));
  dag LE_BYTE_10 = (i32 (EXTRACT_SUBREG (RLDICL LE_DWORD_1, 48, 56), sub_32));
  dag LE_BYTE_11 = (i32 (EXTRACT_SUBREG (RLDICL LE_DWORD_1, 40, 56), sub_32));
  dag LE_BYTE_12 = (i32 (EXTRACT_SUBREG (RLDICL LE_DWORD_1, 32, 56), sub_32));
  dag LE_BYTE_13 = (i32 (EXTRACT_SUBREG (RLDICL LE_DWORD_1, 24, 56), sub_32));
  dag LE_BYTE_14 = (i32 (EXTRACT_SUBREG (RLDICL LE_DWORD_1, 16, 56), sub_32));
  dag LE_BYTE_15 = (i32 (EXTRACT_SUBREG (RLDICL LE_DWORD_1, 8, 56), sub_32));

  /* Variable element number (BE and LE patterns must be specified separately)
     This is a rather involved process.

     Conceptually, this is how the move is accomplished:
     1. Identify which doubleword contains the element
     2. Shift in the VMX register so that the correct doubleword is correctly
        lined up for the MFVSRD
     3. Perform the move so that the element (along with some extra stuff)
        is in the GPR
     4. Right shift within the GPR so that the element is right-justified

     Of course, the index is an element number which has a different meaning
     on LE/BE so the patterns have to be specified separately.

     Note: The final result will be the element right-justified with high
           order bits being arbitrarily defined (namely, whatever was in the
           vector register to the left of the value originally).
  */

  /*  LE variable byte
      Number 1. above:
      - For elements 0-7, we shift left by 8 bytes since they're on the right
      - For elements 8-15, we need not shift (shift left by zero bytes)
      This is accomplished by inverting the bits of the index and AND-ing
      with 0x8 (i.e. clearing all bits of the index and inverting bit 60).
  */
  dag LE_VBYTE_PERM_VEC = (v16i8 (LVSL ZERO8, (ANDC8 (LI8 8), $Idx)));

  //  Number 2. above:
  //  - Now that we set up the shift amount, we shift in the VMX register
  dag LE_VBYTE_PERMUTE = (v16i8 (VPERM $S, $S, LE_VBYTE_PERM_VEC));

  //  Number 3. above:
  //  - The doubleword containing our element is moved to a GPR
  dag LE_MV_VBYTE = (MFVSRD
                      (EXTRACT_SUBREG
                        (v2i64 (COPY_TO_REGCLASS LE_VBYTE_PERMUTE, VSRC)),
                        sub_64));

  /*  Number 4. above:
      - Truncate the element number to the range 0-7 (8-15 are symmetrical
        and out of range values are truncated accordingly)
      - Multiply by 8 as we need to shift right by the number of bits, not bytes
      - Shift right in the GPR by the calculated value
  */
  dag LE_VBYTE_SHIFT = (EXTRACT_SUBREG (RLDICR (AND8 (LI8 7), $Idx), 3, 60),
                                       sub_32);
  dag LE_VARIABLE_BYTE = (EXTRACT_SUBREG (SRD LE_MV_VBYTE, LE_VBYTE_SHIFT),
                                         sub_32);

  /*  LE variable halfword
      Number 1. above:
      - For elements 0-3, we shift left by 8 since they're on the right
      - For elements 4-7, we need not shift (shift left by zero bytes)
      Similarly to the byte pattern, we invert the bits of the index, but we
      AND with 0x4 (i.e. clear all bits of the index and invert bit 61).
      Of course, the shift is still by 8 bytes, so we must multiply by 2.
  */
  dag LE_VHALF_PERM_VEC =
    (v16i8 (LVSL ZERO8, (RLDICR (ANDC8 (LI8 4), $Idx), 1, 62)));

  //  Number 2. above:
  //  - Now that we set up the shift amount, we shift in the VMX register
  dag LE_VHALF_PERMUTE = (v16i8 (VPERM $S, $S, LE_VHALF_PERM_VEC));

  //  Number 3. above:
  //  - The doubleword containing our element is moved to a GPR
  dag LE_MV_VHALF = (MFVSRD
                      (EXTRACT_SUBREG
                        (v2i64 (COPY_TO_REGCLASS LE_VHALF_PERMUTE, VSRC)),
                        sub_64));

  /*  Number 4. above:
      - Truncate the element number to the range 0-3 (4-7 are symmetrical
        and out of range values are truncated accordingly)
      - Multiply by 16 as we need to shift right by the number of bits
      - Shift right in the GPR by the calculated value
  */
  dag LE_VHALF_SHIFT = (EXTRACT_SUBREG (RLDICR (AND8 (LI8 3), $Idx), 4, 59),
                                       sub_32);
  dag LE_VARIABLE_HALF = (EXTRACT_SUBREG (SRD LE_MV_VHALF, LE_VHALF_SHIFT),
                                         sub_32);

  /*  LE variable word
      Number 1. above:
      - For elements 0-1, we shift left by 8 since they're on the right
      - For elements 2-3, we need not shift
  */
  dag LE_VWORD_PERM_VEC = (v16i8 (LVSL ZERO8,
                                       (RLDICR (ANDC8 (LI8 2), $Idx), 2, 61)));

  //  Number 2. above:
  //  - Now that we set up the shift amount, we shift in the VMX register
  dag LE_VWORD_PERMUTE = (v16i8 (VPERM $S, $S, LE_VWORD_PERM_VEC));

  //  Number 3. above:
  //  - The doubleword containing our element is moved to a GPR
  dag LE_MV_VWORD = (MFVSRD
                      (EXTRACT_SUBREG
                        (v2i64 (COPY_TO_REGCLASS LE_VWORD_PERMUTE, VSRC)),
                        sub_64));

  /*  Number 4. above:
      - Truncate the element number to the range 0-1 (2-3 are symmetrical
        and out of range values are truncated accordingly)
      - Multiply by 32 as we need to shift right by the number of bits
      - Shift right in the GPR by the calculated value
  */
  dag LE_VWORD_SHIFT = (EXTRACT_SUBREG (RLDICR (AND8 (LI8 1), $Idx), 5, 58),
                                       sub_32);
  dag LE_VARIABLE_WORD = (EXTRACT_SUBREG (SRD LE_MV_VWORD, LE_VWORD_SHIFT),
                                         sub_32);

  /*  LE variable doubleword
      Number 1. above:
      - For element 0, we shift left by 8 since it's on the right
      - For element 1, we need not shift
  */
  dag LE_VDWORD_PERM_VEC = (v16i8 (LVSL ZERO8,
                                        (RLDICR (ANDC8 (LI8 1), $Idx), 3, 60)));

  //  Number 2. above:
  //  - Now that we set up the shift amount, we shift in the VMX register
  dag LE_VDWORD_PERMUTE = (v16i8 (VPERM $S, $S, LE_VDWORD_PERM_VEC));

  // Number 3. above:
  //  - The doubleword containing our element is moved to a GPR
  //  - Number 4. is not needed for the doubleword as the value is 64-bits
  dag LE_VARIABLE_DWORD =
        (MFVSRD (EXTRACT_SUBREG
                  (v2i64 (COPY_TO_REGCLASS LE_VDWORD_PERMUTE, VSRC)),
                  sub_64));

  /*  LE variable float
      - Shift the vector to line up the desired element to BE Word 0
      - Convert 32-bit float to a 64-bit single precision float
  */
  dag LE_VFLOAT_PERM_VEC = (v16i8 (LVSL ZERO8,
                                  (RLDICR (XOR8 (LI8 3), $Idx), 2, 61)));
  dag LE_VFLOAT_PERMUTE = (VPERM $S, $S, LE_VFLOAT_PERM_VEC);
  dag LE_VARIABLE_FLOAT = (XSCVSPDPN LE_VFLOAT_PERMUTE);

  /*  LE variable double
      Same as the LE doubleword except there is no move.
  */
  dag LE_VDOUBLE_PERMUTE = (v16i8 (VPERM (v16i8 (COPY_TO_REGCLASS $S, VRRC)),
                                         (v16i8 (COPY_TO_REGCLASS $S, VRRC)),
                                         LE_VDWORD_PERM_VEC));
  dag LE_VARIABLE_DOUBLE = (COPY_TO_REGCLASS LE_VDOUBLE_PERMUTE, VSRC);

  /*  BE variable byte
      The algorithm here is the same as the LE variable byte except:
      - The shift in the VMX register is by 0/8 for opposite element numbers so
        we simply AND the element number with 0x8
      - The order of elements after the move to GPR is reversed, so we invert
        the bits of the index prior to truncating to the range 0-7
  */
  dag BE_VBYTE_PERM_VEC = (v16i8 (LVSL ZERO8, (ANDI8_rec $Idx, 8)));
  dag BE_VBYTE_PERMUTE = (v16i8 (VPERM $S, $S, BE_VBYTE_PERM_VEC));
  dag BE_MV_VBYTE = (MFVSRD
                      (EXTRACT_SUBREG
                        (v2i64 (COPY_TO_REGCLASS BE_VBYTE_PERMUTE, VSRC)),
                        sub_64));
  dag BE_VBYTE_SHIFT = (EXTRACT_SUBREG (RLDICR (ANDC8 (LI8 7), $Idx), 3, 60),
                                       sub_32);
  dag BE_VARIABLE_BYTE = (EXTRACT_SUBREG (SRD BE_MV_VBYTE, BE_VBYTE_SHIFT),
                                         sub_32);

  /*  BE variable halfword
      The algorithm here is the same as the LE variable halfword except:
      - The shift in the VMX register is by 0/8 for opposite element numbers so
        we simply AND the element number with 0x4 and multiply by 2
      - The order of elements after the move to GPR is reversed, so we invert
        the bits of the index prior to truncating to the range 0-3
  */
  dag BE_VHALF_PERM_VEC = (v16i8 (LVSL ZERO8,
                                       (RLDICR (ANDI8_rec $Idx, 4), 1, 62)));
  dag BE_VHALF_PERMUTE = (v16i8 (VPERM $S, $S, BE_VHALF_PERM_VEC));
  dag BE_MV_VHALF = (MFVSRD
                      (EXTRACT_SUBREG
                        (v2i64 (COPY_TO_REGCLASS BE_VHALF_PERMUTE, VSRC)),
                        sub_64));
  dag BE_VHALF_SHIFT = (EXTRACT_SUBREG (RLDICR (ANDC8 (LI8 3), $Idx), 4, 59),
                                       sub_32);
  dag BE_VARIABLE_HALF = (EXTRACT_SUBREG (SRD BE_MV_VHALF, BE_VHALF_SHIFT),
                                         sub_32);

  /*  BE variable word
      The algorithm is the same as the LE variable word except:
      - The shift in the VMX register happens for opposite element numbers
      - The order of elements after the move to GPR is reversed, so we invert
        the bits of the index prior to truncating to the range 0-1
  */
  dag BE_VWORD_PERM_VEC = (v16i8 (LVSL ZERO8,
                                       (RLDICR (ANDI8_rec $Idx, 2), 2, 61)));
  dag BE_VWORD_PERMUTE = (v16i8 (VPERM $S, $S, BE_VWORD_PERM_VEC));
  dag BE_MV_VWORD = (MFVSRD
                      (EXTRACT_SUBREG
                        (v2i64 (COPY_TO_REGCLASS BE_VWORD_PERMUTE, VSRC)),
                        sub_64));
  dag BE_VWORD_SHIFT = (EXTRACT_SUBREG (RLDICR (ANDC8 (LI8 1), $Idx), 5, 58),
                                       sub_32);
  dag BE_VARIABLE_WORD = (EXTRACT_SUBREG (SRD BE_MV_VWORD, BE_VWORD_SHIFT),
                                         sub_32);

  /*  BE variable doubleword
      Same as the LE doubleword except we shift in the VMX register for opposite
      element indices.
  */
  dag BE_VDWORD_PERM_VEC = (v16i8 (LVSL ZERO8,
                                        (RLDICR (ANDI8_rec $Idx, 1), 3, 60)));
  dag BE_VDWORD_PERMUTE = (v16i8 (VPERM $S, $S, BE_VDWORD_PERM_VEC));
  dag BE_VARIABLE_DWORD =
        (MFVSRD (EXTRACT_SUBREG
                  (v2i64 (COPY_TO_REGCLASS BE_VDWORD_PERMUTE, VSRC)),
                  sub_64));

  /*  BE variable float
      - Shift the vector to line up the desired element to BE Word 0
      - Convert 32-bit float to a 64-bit single precision float
  */
  dag BE_VFLOAT_PERM_VEC = (v16i8 (LVSL ZERO8, (RLDICR $Idx, 2, 61)));
  dag BE_VFLOAT_PERMUTE = (VPERM $S, $S, BE_VFLOAT_PERM_VEC);
  dag BE_VARIABLE_FLOAT = (XSCVSPDPN BE_VFLOAT_PERMUTE);

  //  BE variable float 32-bit version
  dag BE_32B_VFLOAT_PERM_VEC = (v16i8 (LVSL (i32 ZERO), (RLWINM $Idx, 2, 0, 29)));
  dag BE_32B_VFLOAT_PERMUTE = (VPERM $S, $S, BE_32B_VFLOAT_PERM_VEC);
  dag BE_32B_VARIABLE_FLOAT = (XSCVSPDPN BE_32B_VFLOAT_PERMUTE);

  /* BE variable double
      Same as the BE doubleword except there is no move.
  */
  dag BE_VDOUBLE_PERMUTE = (v16i8 (VPERM (v16i8 (COPY_TO_REGCLASS $S, VRRC)),
                                         (v16i8 (COPY_TO_REGCLASS $S, VRRC)),
                                         BE_VDWORD_PERM_VEC));
  dag BE_VARIABLE_DOUBLE = (COPY_TO_REGCLASS BE_VDOUBLE_PERMUTE, VSRC);

  //  BE variable double 32-bit version
  dag BE_32B_VDWORD_PERM_VEC = (v16i8 (LVSL (i32 ZERO),
                                        (RLWINM (ANDI_rec $Idx, 1), 3, 0, 28)));
  dag BE_32B_VDOUBLE_PERMUTE = (v16i8 (VPERM (v16i8 (COPY_TO_REGCLASS $S, VRRC)),
                                      (v16i8 (COPY_TO_REGCLASS $S, VRRC)),
                                      BE_32B_VDWORD_PERM_VEC));
  dag BE_32B_VARIABLE_DOUBLE = (COPY_TO_REGCLASS BE_32B_VDOUBLE_PERMUTE, VSRC);
}

def AlignValues {
  dag F32_TO_BE_WORD1 = (v4f32 (XSCVDPSPN $B));
  dag I32_TO_BE_WORD1 = (SUBREG_TO_REG (i64 1), (MTVSRWZ $B), sub_64);
}

// Integer extend helper dags 32 -> 64
def AnyExts {
  dag A = (INSERT_SUBREG (i64 (IMPLICIT_DEF)), $A, sub_32);
  dag B = (INSERT_SUBREG (i64 (IMPLICIT_DEF)), $B, sub_32);
  dag C = (INSERT_SUBREG (i64 (IMPLICIT_DEF)), $C, sub_32);
  dag D = (INSERT_SUBREG (i64 (IMPLICIT_DEF)), $D, sub_32);
}

def DblToFlt {
  dag A0 = (f32 (any_fpround (f64 (extractelt v2f64:$A, 0))));
  dag A1 = (f32 (any_fpround (f64 (extractelt v2f64:$A, 1))));
  dag B0 = (f32 (any_fpround (f64 (extractelt v2f64:$B, 0))));
  dag B1 = (f32 (any_fpround (f64 (extractelt v2f64:$B, 1))));
}

def ExtDbl {
  dag A0S = (i32 (PPCmfvsr (f64 (PPCfctiwz (f64 (extractelt v2f64:$A, 0))))));
  dag A1S = (i32 (PPCmfvsr (f64 (PPCfctiwz (f64 (extractelt v2f64:$A, 1))))));
  dag B0S = (i32 (PPCmfvsr (f64 (PPCfctiwz (f64 (extractelt v2f64:$B, 0))))));
  dag B1S = (i32 (PPCmfvsr (f64 (PPCfctiwz (f64 (extractelt v2f64:$B, 1))))));
  dag A0U = (i32 (PPCmfvsr (f64 (PPCfctiwuz (f64 (extractelt v2f64:$A, 0))))));
  dag A1U = (i32 (PPCmfvsr (f64 (PPCfctiwuz (f64 (extractelt v2f64:$A, 1))))));
  dag B0U = (i32 (PPCmfvsr (f64 (PPCfctiwuz (f64 (extractelt v2f64:$B, 0))))));
  dag B1U = (i32 (PPCmfvsr (f64 (PPCfctiwuz (f64 (extractelt v2f64:$B, 1))))));
}

def ByteToWord {
  dag LE_A0 = (i32 (sext_inreg (i32 (vector_extract v16i8:$A, 0)), i8));
  dag LE_A1 = (i32 (sext_inreg (i32 (vector_extract v16i8:$A, 4)), i8));
  dag LE_A2 = (i32 (sext_inreg (i32 (vector_extract v16i8:$A, 8)), i8));
  dag LE_A3 = (i32 (sext_inreg (i32 (vector_extract v16i8:$A, 12)), i8));
  dag BE_A0 = (i32 (sext_inreg (i32 (vector_extract v16i8:$A, 3)), i8));
  dag BE_A1 = (i32 (sext_inreg (i32 (vector_extract v16i8:$A, 7)), i8));
  dag BE_A2 = (i32 (sext_inreg (i32 (vector_extract v16i8:$A, 11)), i8));
  dag BE_A3 = (i32 (sext_inreg (i32 (vector_extract v16i8:$A, 15)), i8));
}

def ByteToDWord {
  dag LE_A0 = (i64 (sext_inreg
              (i64 (anyext (i32 (vector_extract v16i8:$A, 0)))), i8));
  dag LE_A1 = (i64 (sext_inreg
              (i64 (anyext (i32 (vector_extract v16i8:$A, 8)))), i8));
  dag BE_A0 = (i64 (sext_inreg
              (i64 (anyext (i32 (vector_extract v16i8:$A, 7)))), i8));
  dag BE_A1 = (i64 (sext_inreg
              (i64 (anyext (i32 (vector_extract v16i8:$A, 15)))), i8));
}

def HWordToWord {
  dag LE_A0 = (i32 (sext_inreg (i32 (vector_extract v8i16:$A, 0)), i16));
  dag LE_A1 = (i32 (sext_inreg (i32 (vector_extract v8i16:$A, 2)), i16));
  dag LE_A2 = (i32 (sext_inreg (i32 (vector_extract v8i16:$A, 4)), i16));
  dag LE_A3 = (i32 (sext_inreg (i32 (vector_extract v8i16:$A, 6)), i16));
  dag BE_A0 = (i32 (sext_inreg (i32 (vector_extract v8i16:$A, 1)), i16));
  dag BE_A1 = (i32 (sext_inreg (i32 (vector_extract v8i16:$A, 3)), i16));
  dag BE_A2 = (i32 (sext_inreg (i32 (vector_extract v8i16:$A, 5)), i16));
  dag BE_A3 = (i32 (sext_inreg (i32 (vector_extract v8i16:$A, 7)), i16));
}

def HWordToDWord {
  dag LE_A0 = (i64 (sext_inreg
              (i64 (anyext (i32 (vector_extract v8i16:$A, 0)))), i16));
  dag LE_A1 = (i64 (sext_inreg
              (i64 (anyext (i32 (vector_extract v8i16:$A, 4)))), i16));
  dag BE_A0 = (i64 (sext_inreg
              (i64 (anyext (i32 (vector_extract v8i16:$A, 3)))), i16));
  dag BE_A1 = (i64 (sext_inreg
              (i64 (anyext (i32 (vector_extract v8i16:$A, 7)))), i16));
}

def WordToDWord {
  dag LE_A0 = (i64 (sext (i32 (vector_extract v4i32:$A, 0))));
  dag LE_A1 = (i64 (sext (i32 (vector_extract v4i32:$A, 2))));
  dag BE_A0 = (i64 (sext (i32 (vector_extract v4i32:$A, 1))));
  dag BE_A1 = (i64 (sext (i32 (vector_extract v4i32:$A, 3))));
}

def FltToIntLoad {
  dag A = (i32 (PPCmfvsr (PPCfctiwz (f64 (extloadf32 ForceXForm:$A)))));
}
def FltToUIntLoad {
  dag A = (i32 (PPCmfvsr (PPCfctiwuz (f64 (extloadf32 ForceXForm:$A)))));
}
def FltToLongLoad {
  dag A = (i64 (PPCmfvsr (PPCfctidz (f64 (extloadf32 ForceXForm:$A)))));
}
def FltToLongLoadP9 {
  dag A = (i64 (PPCmfvsr (PPCfctidz (f64 (extloadf32 DSForm:$A)))));
}
def FltToULongLoad {
  dag A = (i64 (PPCmfvsr (PPCfctiduz (f64 (extloadf32 ForceXForm:$A)))));
}
def FltToULongLoadP9 {
  dag A = (i64 (PPCmfvsr (PPCfctiduz (f64 (extloadf32 DSForm:$A)))));
}
def FltToLong {
  dag A = (i64 (PPCmfvsr (f64 (PPCfctidz (fpextend f32:$A)))));
}
def FltToULong {
  dag A = (i64 (PPCmfvsr (f64 (PPCfctiduz (fpextend f32:$A)))));
}
def DblToInt {
  dag A = (i32 (PPCmfvsr (f64 (PPCfctiwz f64:$A))));
  dag B = (i32 (PPCmfvsr (f64 (PPCfctiwz f64:$B))));
  dag C = (i32 (PPCmfvsr (f64 (PPCfctiwz f64:$C))));
  dag D = (i32 (PPCmfvsr (f64 (PPCfctiwz f64:$D))));
}
def DblToUInt {
  dag A = (i32 (PPCmfvsr (f64 (PPCfctiwuz f64:$A))));
  dag B = (i32 (PPCmfvsr (f64 (PPCfctiwuz f64:$B))));
  dag C = (i32 (PPCmfvsr (f64 (PPCfctiwuz f64:$C))));
  dag D = (i32 (PPCmfvsr (f64 (PPCfctiwuz f64:$D))));
}
def DblToLong {
  dag A = (i64 (PPCmfvsr (f64 (PPCfctidz f64:$A))));
}
def DblToULong {
  dag A = (i64 (PPCmfvsr (f64 (PPCfctiduz f64:$A))));
}
def DblToIntLoad {
  dag A = (i32 (PPCmfvsr (PPCfctiwz (f64 (load ForceXForm:$A)))));
}
def DblToIntLoadP9 {
  dag A = (i32 (PPCmfvsr (PPCfctiwz (f64 (load DSForm:$A)))));
}
def DblToUIntLoad {
  dag A = (i32 (PPCmfvsr (PPCfctiwuz (f64 (load ForceXForm:$A)))));
}
def DblToUIntLoadP9 {
  dag A = (i32 (PPCmfvsr (PPCfctiwuz (f64 (load DSForm:$A)))));
}
def DblToLongLoad {
  dag A = (i64 (PPCmfvsr (PPCfctidz (f64 (load ForceXForm:$A)))));
}
def DblToULongLoad {
  dag A = (i64 (PPCmfvsr (PPCfctiduz (f64 (load ForceXForm:$A)))));
}

// FP load dags (for f32 -> v4f32)
def LoadFP {
  dag A = (f32 (load ForceXForm:$A));
  dag B = (f32 (load ForceXForm:$B));
  dag C = (f32 (load ForceXForm:$C));
  dag D = (f32 (load ForceXForm:$D));
}

// FP merge dags (for f32 -> v4f32)
def MrgFP {
  dag LD32A = (SUBREG_TO_REG (i64 1), (LIWZX ForceXForm:$A), sub_64);
  dag LD32B = (SUBREG_TO_REG (i64 1), (LIWZX ForceXForm:$B), sub_64);
  dag LD32C = (SUBREG_TO_REG (i64 1), (LIWZX ForceXForm:$C), sub_64);
  dag LD32D = (SUBREG_TO_REG (i64 1), (LIWZX ForceXForm:$D), sub_64);
  dag AC = (XVCVDPSP (XXPERMDI (SUBREG_TO_REG (i64 1), $A, sub_64),
                               (SUBREG_TO_REG (i64 1), $C, sub_64), 0));
  dag BD = (XVCVDPSP (XXPERMDI (SUBREG_TO_REG (i64 1), $B, sub_64),
                               (SUBREG_TO_REG (i64 1), $D, sub_64), 0));
  dag ABhToFlt = (XVCVDPSP (XXPERMDI $A, $B, 0));
  dag ABlToFlt = (XVCVDPSP (XXPERMDI $A, $B, 3));
  dag BAhToFlt = (XVCVDPSP (XXPERMDI $B, $A, 0));
  dag BAlToFlt = (XVCVDPSP (XXPERMDI $B, $A, 3));
}

// Word-element merge dags - conversions from f64 to i32 merged into vectors.
def MrgWords {
  // For big endian, we merge low and hi doublewords (A, B).
  dag A0B0 = (v2f64 (XXPERMDI v2f64:$A, v2f64:$B, 0));
  dag A1B1 = (v2f64 (XXPERMDI v2f64:$A, v2f64:$B, 3));
  dag CVA1B1S = (v4i32 (XVCVDPSXWS A1B1));
  dag CVA0B0S = (v4i32 (XVCVDPSXWS A0B0));
  dag CVA1B1U = (v4i32 (XVCVDPUXWS A1B1));
  dag CVA0B0U = (v4i32 (XVCVDPUXWS A0B0));

  // For little endian, we merge low and hi doublewords (B, A).
  dag B1A1 = (v2f64 (XXPERMDI v2f64:$B, v2f64:$A, 0));
  dag B0A0 = (v2f64 (XXPERMDI v2f64:$B, v2f64:$A, 3));
  dag CVB1A1S = (v4i32 (XVCVDPSXWS B1A1));
  dag CVB0A0S = (v4i32 (XVCVDPSXWS B0A0));
  dag CVB1A1U = (v4i32 (XVCVDPUXWS B1A1));
  dag CVB0A0U = (v4i32 (XVCVDPUXWS B0A0));

  // For big endian, we merge hi doublewords of (A, C) and (B, D), convert
  // then merge.
  dag AC = (v2f64 (XXPERMDI (SUBREG_TO_REG (i64 1), f64:$A, sub_64),
                            (SUBREG_TO_REG (i64 1), f64:$C, sub_64), 0));
  dag BD = (v2f64 (XXPERMDI (SUBREG_TO_REG (i64 1), f64:$B, sub_64),
                            (SUBREG_TO_REG (i64 1), f64:$D, sub_64), 0));
  dag CVACS = (v4i32 (XVCVDPSXWS AC));
  dag CVBDS = (v4i32 (XVCVDPSXWS BD));
  dag CVACU = (v4i32 (XVCVDPUXWS AC));
  dag CVBDU = (v4i32 (XVCVDPUXWS BD));

  // For little endian, we merge hi doublewords of (D, B) and (C, A), convert
  // then merge.
  dag DB = (v2f64 (XXPERMDI (SUBREG_TO_REG (i64 1), f64:$D, sub_64),
                            (SUBREG_TO_REG (i64 1), f64:$B, sub_64), 0));
  dag CA = (v2f64 (XXPERMDI (SUBREG_TO_REG (i64 1), f64:$C, sub_64),
                            (SUBREG_TO_REG (i64 1), f64:$A, sub_64), 0));
  dag CVDBS = (v4i32 (XVCVDPSXWS DB));
  dag CVCAS = (v4i32 (XVCVDPSXWS CA));
  dag CVDBU = (v4i32 (XVCVDPUXWS DB));
  dag CVCAU = (v4i32 (XVCVDPUXWS CA));
}

def DblwdCmp {
  dag SGTW = (v2i64 (v2i64 (VCMPGTSW v2i64:$vA, v2i64:$vB)));
  dag UGTW = (v2i64 (v2i64 (VCMPGTUW v2i64:$vA, v2i64:$vB)));
  dag EQW = (v2i64 (v2i64 (VCMPEQUW v2i64:$vA, v2i64:$vB)));
  dag UGTWSHAND = (v2i64 (XXLAND (v2i64 (XXSLDWI UGTW, UGTW, 1)), EQW));
  dag EQWSHAND = (v2i64 (XXLAND (v2i64 (XXSLDWI EQW, EQW, 1)), EQW));
  dag SGTWOR = (v2i64 (XXLOR SGTW, UGTWSHAND));
  dag UGTWOR = (v2i64 (XXLOR UGTW, UGTWSHAND));
  dag MRGSGT = (v2i64 (XXPERMDI (v2i64 (XXSPLTW SGTWOR, 0)),
                                (v2i64 (XXSPLTW SGTWOR, 2)), 0));
  dag MRGUGT = (v2i64 (XXPERMDI (v2i64 (XXSPLTW UGTWOR, 0)),
                                (v2i64 (XXSPLTW UGTWOR, 2)), 0));
  dag MRGEQ = (v2i64 (XXPERMDI (v2i64 (XXSPLTW EQWSHAND, 0)),
                               (v2i64 (XXSPLTW EQWSHAND, 2)), 0));
}

//---------------------------- Anonymous Patterns ----------------------------//
// Predicate combinations are kept in roughly chronological order in terms of
// instruction availability in the architecture. For example, VSX came in with
// ISA 2.06 (Power7). There have since been additions in ISA 2.07 (Power8) and
// ISA 3.0 (Power9). However, the granularity of features on later subtargets
// is finer for various reasons. For example, we have Power8Vector,
// Power8Altivec, DirectMove that all came in with ISA 2.07. The situation is
// similar with ISA 3.0 with Power9Vector, Power9Altivec, IsISA3_0. Then there
// are orthogonal predicates such as endianness for which the order was
// arbitrarily chosen to be Big, Little.
//
// Predicate combinations available:
// [HasVSX, IsLittleEndian, HasP8Altivec] Altivec patterns using VSX instr.
// [HasVSX, IsBigEndian, HasP8Altivec] Altivec patterns using VSX instr.
// [HasVSX]
// [HasVSX, IsBigEndian]
// [HasVSX, IsLittleEndian]
// [HasVSX, NoP9Vector]
// [HasVSX, NoP9Vector, IsLittleEndian]
// [HasVSX, NoP9Vector, IsBigEndian]
// [HasVSX, HasOnlySwappingMemOps]
// [HasVSX, HasOnlySwappingMemOps, IsBigEndian]
// [HasVSX, NoP8Vector]
// [HasVSX, HasP8Vector]
// [HasVSX, HasP8Vector, IsBigEndian]
// [HasVSX, HasP8Vector, IsBigEndian, IsPPC64]
// [HasVSX, HasP8Vector, IsLittleEndian]
// [HasVSX, HasP8Vector, NoP9Vector, IsBigEndian, IsPPC64]
// [HasVSX, HasP8Vector, NoP9Vector, IsLittleEndian]
// [HasVSX, HasP8Altivec]
// [HasVSX, HasDirectMove]
// [HasVSX, HasDirectMove, IsBigEndian]
// [HasVSX, HasDirectMove, IsLittleEndian]
// [HasVSX, HasDirectMove, NoP9Altivec, IsBigEndian, IsPPC64]
// [HasVSX, HasDirectMove, NoP9Vector, IsBigEndian, IsPPC64]
// [HasVSX, HasDirectMove, NoP9Altivec, IsLittleEndian]
// [HasVSX, HasDirectMove, NoP9Vector, IsLittleEndian]
// [HasVSX, HasP9Vector]
// [HasVSX, HasP9Vector, NoP10Vector]
// [HasVSX, HasP9Vector, IsBigEndian]
// [HasVSX, HasP9Vector, IsBigEndian, IsPPC64]
// [HasVSX, HasP9Vector, IsLittleEndian]
// [HasVSX, HasP9Altivec]
// [HasVSX, HasP9Altivec, IsBigEndian, IsPPC64]
// [HasVSX, HasP9Altivec, IsLittleEndian]
// [HasVSX, IsISA3_0, HasDirectMove, IsBigEndian, IsPPC64]
// [HasVSX, IsISA3_0, HasDirectMove, IsLittleEndian]

// These Altivec patterns are here because we need a VSX instruction to match
// the intrinsic (but only for little endian system).
let Predicates = [HasVSX, IsLittleEndian, HasP8Altivec] in
  def : Pat<(v16i8 (int_ppc_altivec_crypto_vpermxor v16i8:$a,
                                                    v16i8:$b, v16i8:$c)),
            (v16i8 (VPERMXOR $a, $b, (XXLNOR (COPY_TO_REGCLASS $c, VSRC),
                                             (COPY_TO_REGCLASS $c, VSRC))))>;
let Predicates = [HasVSX, IsBigEndian, HasP8Altivec] in
  def : Pat<(v16i8 (int_ppc_altivec_crypto_vpermxor v16i8:$a,
                                                    v16i8:$b, v16i8:$c)),
            (v16i8 (VPERMXOR $a, $b, $c))>;
let Predicates = [HasVSX, HasP8Altivec] in
  def : Pat<(v16i8 (int_ppc_altivec_crypto_vpermxor_be v16i8:$a,
                                                       v16i8:$b, v16i8:$c)),
            (v16i8 (VPERMXOR $a, $b, $c))>;

let AddedComplexity = 400 in {
// Valid for any VSX subtarget, regardless of endianness.
let Predicates = [HasVSX] in {
def : Pat<(v4i32 (vnot v4i32:$A)),
          (v4i32 (XXLNOR $A, $A))>;
def : Pat<(v4i32 (or (and (vnot v4i32:$C), v4i32:$A),
                     (and v4i32:$B, v4i32:$C))),
          (v4i32 (XXSEL $A, $B, $C))>;

def : Pat<(f64 (fpimm0neg)),
          (f64 (XSNEGDP (XXLXORdpz)))>;

def : Pat<(f64 (nzFPImmExactInti5:$A)),
          (COPY_TO_REGCLASS (XVCVSXWDP (COPY_TO_REGCLASS
                     (VSPLTISW (getFPAs5BitExactInt fpimm:$A)), VSRC)), VSFRC)>;

// Additional fnmsub pattern for PPC specific ISD opcode
def : Pat<(PPCfnmsub f64:$A, f64:$B, f64:$C),
          (XSNMSUBADP $C, $A, $B)>;
def : Pat<(fneg (PPCfnmsub f64:$A, f64:$B, f64:$C)),
          (XSMSUBADP $C, $A, $B)>;
def : Pat<(PPCfnmsub f64:$A, f64:$B, (fneg f64:$C)),
          (XSNMADDADP $C, $A, $B)>;

def : Pat<(PPCfnmsub v2f64:$A, v2f64:$B, v2f64:$C),
          (XVNMSUBADP $C, $A, $B)>;
def : Pat<(fneg (PPCfnmsub v2f64:$A, v2f64:$B, v2f64:$C)),
          (XVMSUBADP $C, $A, $B)>;
def : Pat<(PPCfnmsub v2f64:$A, v2f64:$B, (fneg v2f64:$C)),
          (XVNMADDADP $C, $A, $B)>;

def : Pat<(PPCfnmsub v4f32:$A, v4f32:$B, v4f32:$C),
          (XVNMSUBASP $C, $A, $B)>;
def : Pat<(fneg (PPCfnmsub v4f32:$A, v4f32:$B, v4f32:$C)),
          (XVMSUBASP $C, $A, $B)>;
def : Pat<(PPCfnmsub v4f32:$A, v4f32:$B, (fneg v4f32:$C)),
          (XVNMADDASP $C, $A, $B)>;

def : Pat<(PPCfsqrt f64:$frA), (XSSQRTDP $frA)>;
def : Pat<(PPCfsqrt v2f64:$frA), (XVSQRTDP $frA)>;
def : Pat<(PPCfsqrt v4f32:$frA), (XVSQRTSP $frA)>;

def : Pat<(v2f64 (bitconvert v4f32:$A)),
          (COPY_TO_REGCLASS $A, VSRC)>;
def : Pat<(v2f64 (bitconvert v4i32:$A)),
          (COPY_TO_REGCLASS $A, VSRC)>;
def : Pat<(v2f64 (bitconvert v8i16:$A)),
          (COPY_TO_REGCLASS $A, VSRC)>;
def : Pat<(v2f64 (bitconvert v16i8:$A)),
          (COPY_TO_REGCLASS $A, VSRC)>;

def : Pat<(v4f32 (bitconvert v2f64:$A)),
          (COPY_TO_REGCLASS $A, VRRC)>;
def : Pat<(v4i32 (bitconvert v2f64:$A)),
          (COPY_TO_REGCLASS $A, VRRC)>;
def : Pat<(v8i16 (bitconvert v2f64:$A)),
          (COPY_TO_REGCLASS $A, VRRC)>;
def : Pat<(v16i8 (bitconvert v2f64:$A)),
          (COPY_TO_REGCLASS $A, VRRC)>;

def : Pat<(v2i64 (bitconvert v4f32:$A)),
          (COPY_TO_REGCLASS $A, VSRC)>;
def : Pat<(v2i64 (bitconvert v4i32:$A)),
          (COPY_TO_REGCLASS $A, VSRC)>;
def : Pat<(v2i64 (bitconvert v8i16:$A)),
          (COPY_TO_REGCLASS $A, VSRC)>;
def : Pat<(v2i64 (bitconvert v16i8:$A)),
          (COPY_TO_REGCLASS $A, VSRC)>;

def : Pat<(v4f32 (bitconvert v2i64:$A)),
          (COPY_TO_REGCLASS $A, VRRC)>;
def : Pat<(v4i32 (bitconvert v2i64:$A)),
          (COPY_TO_REGCLASS $A, VRRC)>;
def : Pat<(v8i16 (bitconvert v2i64:$A)),
          (COPY_TO_REGCLASS $A, VRRC)>;
def : Pat<(v16i8 (bitconvert v2i64:$A)),
          (COPY_TO_REGCLASS $A, VRRC)>;

def : Pat<(v2f64 (bitconvert v2i64:$A)),
          (COPY_TO_REGCLASS $A, VRRC)>;
def : Pat<(v2i64 (bitconvert v2f64:$A)),
          (COPY_TO_REGCLASS $A, VRRC)>;

def : Pat<(v2f64 (bitconvert v1i128:$A)),
          (COPY_TO_REGCLASS $A, VRRC)>;
def : Pat<(v1i128 (bitconvert v2f64:$A)),
          (COPY_TO_REGCLASS $A, VRRC)>;

def : Pat<(v2i64 (bitconvert f128:$A)),
          (COPY_TO_REGCLASS $A, VRRC)>;
def : Pat<(v4i32 (bitconvert f128:$A)),
          (COPY_TO_REGCLASS $A, VRRC)>;
def : Pat<(v8i16 (bitconvert f128:$A)),
          (COPY_TO_REGCLASS $A, VRRC)>;
def : Pat<(v16i8 (bitconvert f128:$A)),
          (COPY_TO_REGCLASS $A, VRRC)>;

def : Pat<(v2f64 (PPCsvec2fp v4i32:$C, 0)),
          (v2f64 (XVCVSXWDP (v2i64 (XXMRGHW $C, $C))))>;
def : Pat<(v2f64 (PPCsvec2fp v4i32:$C, 1)),
          (v2f64 (XVCVSXWDP (v2i64 (XXMRGLW $C, $C))))>;

def : Pat<(v2f64 (PPCuvec2fp v4i32:$C, 0)),
          (v2f64 (XVCVUXWDP (v2i64 (XXMRGHW $C, $C))))>;
def : Pat<(v2f64 (PPCuvec2fp v4i32:$C, 1)),
          (v2f64 (XVCVUXWDP (v2i64 (XXMRGLW $C, $C))))>;

def : Pat<(v2f64 (PPCfpexth v4f32:$C, 0)), (XVCVSPDP (XXMRGHW $C, $C))>;
def : Pat<(v2f64 (PPCfpexth v4f32:$C, 1)), (XVCVSPDP (XXMRGLW $C, $C))>;

// Permutes.
def : Pat<(v2f64 (PPCxxswapd v2f64:$src)), (XXPERMDI $src, $src, 2)>;
def : Pat<(v2i64 (PPCxxswapd v2i64:$src)), (XXPERMDI $src, $src, 2)>;
def : Pat<(v4f32 (PPCxxswapd v4f32:$src)), (XXPERMDI $src, $src, 2)>;
def : Pat<(v4i32 (PPCxxswapd v4i32:$src)), (XXPERMDI $src, $src, 2)>;
def : Pat<(v2f64 (PPCswapNoChain v2f64:$src)), (XXPERMDI $src, $src, 2)>;

// PPCvecshl XT, XA, XA, 2 can be selected to both XXSLDWI XT,XA,XA,2 and
// XXSWAPD XT,XA (i.e. XXPERMDI XT,XA,XA,2), the later one is more profitable.
def : Pat<(v4i32 (PPCvecshl v4i32:$src, v4i32:$src, 2)),
          (XXPERMDI $src, $src, 2)>;

// Selects.
def : Pat<(v2f64 (selectcc i1:$lhs, i1:$rhs, v2f64:$tval, v2f64:$fval, SETLT)),
          (SELECT_VSRC (CRANDC $lhs, $rhs), $tval, $fval)>;
def : Pat<(v2f64 (selectcc i1:$lhs, i1:$rhs, v2f64:$tval, v2f64:$fval, SETULT)),
          (SELECT_VSRC (CRANDC $rhs, $lhs), $tval, $fval)>;
def : Pat<(v2f64 (selectcc i1:$lhs, i1:$rhs, v2f64:$tval, v2f64:$fval, SETLE)),
          (SELECT_VSRC (CRORC  $lhs, $rhs), $tval, $fval)>;
def : Pat<(v2f64 (selectcc i1:$lhs, i1:$rhs, v2f64:$tval, v2f64:$fval, SETULE)),
          (SELECT_VSRC (CRORC  $rhs, $lhs), $tval, $fval)>;
def : Pat<(v2f64 (selectcc i1:$lhs, i1:$rhs, v2f64:$tval, v2f64:$fval, SETEQ)),
          (SELECT_VSRC (CREQV $lhs, $rhs), $tval, $fval)>;
def : Pat<(v2f64 (selectcc i1:$lhs, i1:$rhs, v2f64:$tval, v2f64:$fval, SETGE)),
          (SELECT_VSRC (CRORC  $rhs, $lhs), $tval, $fval)>;
def : Pat<(v2f64 (selectcc i1:$lhs, i1:$rhs, v2f64:$tval, v2f64:$fval, SETUGE)),
          (SELECT_VSRC (CRORC  $lhs, $rhs), $tval, $fval)>;
def : Pat<(v2f64 (selectcc i1:$lhs, i1:$rhs, v2f64:$tval, v2f64:$fval, SETGT)),
          (SELECT_VSRC (CRANDC $rhs, $lhs), $tval, $fval)>;
def : Pat<(v2f64 (selectcc i1:$lhs, i1:$rhs, v2f64:$tval, v2f64:$fval, SETUGT)),
          (SELECT_VSRC (CRANDC $lhs, $rhs), $tval, $fval)>;
def : Pat<(v2f64 (selectcc i1:$lhs, i1:$rhs, v2f64:$tval, v2f64:$fval, SETNE)),
          (SELECT_VSRC (CRXOR $lhs, $rhs), $tval, $fval)>;

def : Pat<(f64 (selectcc i1:$lhs, i1:$rhs, f64:$tval, f64:$fval, SETLT)),
          (SELECT_VSFRC (CRANDC $lhs, $rhs), $tval, $fval)>;
def : Pat<(f64 (selectcc i1:$lhs, i1:$rhs, f64:$tval, f64:$fval, SETULT)),
          (SELECT_VSFRC (CRANDC $rhs, $lhs), $tval, $fval)>;
def : Pat<(f64 (selectcc i1:$lhs, i1:$rhs, f64:$tval, f64:$fval, SETLE)),
          (SELECT_VSFRC (CRORC  $lhs, $rhs), $tval, $fval)>;
def : Pat<(f64 (selectcc i1:$lhs, i1:$rhs, f64:$tval, f64:$fval, SETULE)),
          (SELECT_VSFRC (CRORC  $rhs, $lhs), $tval, $fval)>;
def : Pat<(f64 (selectcc i1:$lhs, i1:$rhs, f64:$tval, f64:$fval, SETEQ)),
          (SELECT_VSFRC (CREQV $lhs, $rhs), $tval, $fval)>;
def : Pat<(f64 (selectcc i1:$lhs, i1:$rhs, f64:$tval, f64:$fval, SETGE)),
          (SELECT_VSFRC (CRORC  $rhs, $lhs), $tval, $fval)>;
def : Pat<(f64 (selectcc i1:$lhs, i1:$rhs, f64:$tval, f64:$fval, SETUGE)),
          (SELECT_VSFRC (CRORC  $lhs, $rhs), $tval, $fval)>;
def : Pat<(f64 (selectcc i1:$lhs, i1:$rhs, f64:$tval, f64:$fval, SETGT)),
          (SELECT_VSFRC (CRANDC $rhs, $lhs), $tval, $fval)>;
def : Pat<(f64 (selectcc i1:$lhs, i1:$rhs, f64:$tval, f64:$fval, SETUGT)),
          (SELECT_VSFRC (CRANDC $lhs, $rhs), $tval, $fval)>;
def : Pat<(f64 (selectcc i1:$lhs, i1:$rhs, f64:$tval, f64:$fval, SETNE)),
          (SELECT_VSFRC (CRXOR $lhs, $rhs), $tval, $fval)>;

// Divides.
def : Pat<(int_ppc_vsx_xvdivsp v4f32:$A, v4f32:$B),
          (XVDIVSP $A, $B)>;
def : Pat<(int_ppc_vsx_xvdivdp v2f64:$A, v2f64:$B),
          (XVDIVDP $A, $B)>;

// Vector test for software divide and sqrt.
def : Pat<(i32 (int_ppc_vsx_xvtdivdp v2f64:$A, v2f64:$B)),
          (COPY_TO_REGCLASS (XVTDIVDP $A, $B), GPRC)>;
def : Pat<(i32 (int_ppc_vsx_xvtdivsp v4f32:$A, v4f32:$B)),
          (COPY_TO_REGCLASS (XVTDIVSP $A, $B), GPRC)>;
def : Pat<(i32 (int_ppc_vsx_xvtsqrtdp v2f64:$A)),
          (COPY_TO_REGCLASS (XVTSQRTDP $A), GPRC)>;
def : Pat<(i32 (int_ppc_vsx_xvtsqrtsp v4f32:$A)),
          (COPY_TO_REGCLASS (XVTSQRTSP $A), GPRC)>;

// Reciprocal estimate
def : Pat<(int_ppc_vsx_xvresp v4f32:$A),
          (XVRESP $A)>;
def : Pat<(int_ppc_vsx_xvredp v2f64:$A),
          (XVREDP $A)>;

// Recip. square root estimate
def : Pat<(int_ppc_vsx_xvrsqrtesp v4f32:$A),
          (XVRSQRTESP $A)>;
def : Pat<(int_ppc_vsx_xvrsqrtedp v2f64:$A),
          (XVRSQRTEDP $A)>;

// Vector selection
def : Pat<(v16i8 (vselect v16i8:$vA, v16i8:$vB, v16i8:$vC)),
          (COPY_TO_REGCLASS
                 (XXSEL (COPY_TO_REGCLASS $vC, VSRC),
                        (COPY_TO_REGCLASS $vB, VSRC),
                        (COPY_TO_REGCLASS $vA, VSRC)), VRRC)>;
def : Pat<(v8i16 (vselect v8i16:$vA, v8i16:$vB, v8i16:$vC)),
          (COPY_TO_REGCLASS
                 (XXSEL (COPY_TO_REGCLASS $vC, VSRC),
                        (COPY_TO_REGCLASS $vB, VSRC),
                        (COPY_TO_REGCLASS $vA, VSRC)), VRRC)>;
def : Pat<(vselect v4i32:$vA, v4i32:$vB, v4i32:$vC),
          (XXSEL $vC, $vB, $vA)>;
def : Pat<(vselect v2i64:$vA, v2i64:$vB, v2i64:$vC),
          (XXSEL $vC, $vB, $vA)>;
def : Pat<(vselect v4i32:$vA, v4f32:$vB, v4f32:$vC),
          (XXSEL $vC, $vB, $vA)>;
def : Pat<(vselect v2i64:$vA, v2f64:$vB, v2f64:$vC),
          (XXSEL $vC, $vB, $vA)>;
def : Pat<(v1i128 (vselect v1i128:$vA, v1i128:$vB, v1i128:$vC)),
          (COPY_TO_REGCLASS
                 (XXSEL (COPY_TO_REGCLASS $vC, VSRC),
                        (COPY_TO_REGCLASS $vB, VSRC),
                        (COPY_TO_REGCLASS $vA, VSRC)), VRRC)>;

def : Pat<(v4f32 (any_fmaxnum v4f32:$src1, v4f32:$src2)),
          (v4f32 (XVMAXSP $src1, $src2))>;
def : Pat<(v4f32 (any_fminnum v4f32:$src1, v4f32:$src2)),
          (v4f32 (XVMINSP $src1, $src2))>;
def : Pat<(v2f64 (any_fmaxnum v2f64:$src1, v2f64:$src2)),
          (v2f64 (XVMAXDP $src1, $src2))>;
def : Pat<(v2f64 (any_fminnum v2f64:$src1, v2f64:$src2)),
          (v2f64 (XVMINDP $src1, $src2))>;

// f32 abs
def : Pat<(f32 (fabs f32:$S)),
          (f32 (COPY_TO_REGCLASS (XSABSDP
               (COPY_TO_REGCLASS $S, VSFRC)), VSSRC))>;

// f32 nabs
def : Pat<(f32 (fneg (fabs f32:$S))),
          (f32 (COPY_TO_REGCLASS (XSNABSDP
               (COPY_TO_REGCLASS $S, VSFRC)), VSSRC))>;

// f32 Min.
def : Pat<(f32 (fminnum_ieee f32:$A, f32:$B)),
          (f32 FpMinMax.F32Min)>;
def : Pat<(f32 (fminnum_ieee (fcanonicalize f32:$A), f32:$B)),
          (f32 FpMinMax.F32Min)>;
def : Pat<(f32 (fminnum_ieee f32:$A, (fcanonicalize f32:$B))),
          (f32 FpMinMax.F32Min)>;
def : Pat<(f32 (fminnum_ieee (fcanonicalize f32:$A), (fcanonicalize f32:$B))),
          (f32 FpMinMax.F32Min)>;
// F32 Max.
def : Pat<(f32 (fmaxnum_ieee f32:$A, f32:$B)),
          (f32 FpMinMax.F32Max)>;
def : Pat<(f32 (fmaxnum_ieee (fcanonicalize f32:$A), f32:$B)),
          (f32 FpMinMax.F32Max)>;
def : Pat<(f32 (fmaxnum_ieee f32:$A, (fcanonicalize f32:$B))),
          (f32 FpMinMax.F32Max)>;
def : Pat<(f32 (fmaxnum_ieee (fcanonicalize f32:$A), (fcanonicalize f32:$B))),
          (f32 FpMinMax.F32Max)>;

// f64 Min.
def : Pat<(f64 (fminnum_ieee f64:$A, f64:$B)),
          (f64 (XSMINDP $A, $B))>;
def : Pat<(f64 (fminnum_ieee (fcanonicalize f64:$A), f64:$B)),
          (f64 (XSMINDP $A, $B))>;
def : Pat<(f64 (fminnum_ieee f64:$A, (fcanonicalize f64:$B))),
          (f64 (XSMINDP $A, $B))>;
def : Pat<(f64 (fminnum_ieee (fcanonicalize f64:$A), (fcanonicalize f64:$B))),
          (f64 (XSMINDP $A, $B))>;
// f64 Max.
def : Pat<(f64 (fmaxnum_ieee f64:$A, f64:$B)),
          (f64 (XSMAXDP $A, $B))>;
def : Pat<(f64 (fmaxnum_ieee (fcanonicalize f64:$A), f64:$B)),
          (f64 (XSMAXDP $A, $B))>;
def : Pat<(f64 (fmaxnum_ieee f64:$A, (fcanonicalize f64:$B))),
          (f64 (XSMAXDP $A, $B))>;
def : Pat<(f64 (fmaxnum_ieee (fcanonicalize f64:$A), (fcanonicalize f64:$B))),
          (f64 (XSMAXDP $A, $B))>;

def : Pat<(int_ppc_vsx_stxvd2x_be v2f64:$rS, ForceXForm:$dst),
            (STXVD2X $rS, ForceXForm:$dst)>;
def : Pat<(int_ppc_vsx_stxvw4x_be v4i32:$rS, ForceXForm:$dst),
            (STXVW4X $rS, ForceXForm:$dst)>;
def : Pat<(v4i32 (int_ppc_vsx_lxvw4x_be ForceXForm:$src)), (LXVW4X ForceXForm:$src)>;
def : Pat<(v2f64 (int_ppc_vsx_lxvd2x_be ForceXForm:$src)), (LXVD2X ForceXForm:$src)>;

// Rounding for single precision.
def : Pat<(f32 (any_fround f32:$S)),
          (f32 (COPY_TO_REGCLASS (XSRDPI
                                   (COPY_TO_REGCLASS $S, VSFRC)), VSSRC))>;
def : Pat<(f32 (any_ffloor f32:$S)),
          (f32 (COPY_TO_REGCLASS (XSRDPIM
                                   (COPY_TO_REGCLASS $S, VSFRC)), VSSRC))>;
def : Pat<(f32 (any_fceil f32:$S)),
          (f32 (COPY_TO_REGCLASS (XSRDPIP
                                   (COPY_TO_REGCLASS $S, VSFRC)), VSSRC))>;
def : Pat<(f32 (any_ftrunc f32:$S)),
          (f32 (COPY_TO_REGCLASS (XSRDPIZ
                                   (COPY_TO_REGCLASS $S, VSFRC)), VSSRC))>;
def : Pat<(f32 (any_frint f32:$S)),
          (f32 (COPY_TO_REGCLASS (XSRDPIC
                                   (COPY_TO_REGCLASS $S, VSFRC)), VSSRC))>;
def : Pat<(v4f32 (any_frint v4f32:$S)), (v4f32 (XVRSPIC $S))>;

// Rounding for double precision.
def : Pat<(f64 (any_frint f64:$S)), (f64 (XSRDPIC $S))>;
def : Pat<(v2f64 (any_frint v2f64:$S)), (v2f64 (XVRDPIC $S))>;

// Rounding without exceptions (nearbyint). Due to strange tblgen behaviour,
// these need to be defined after the any_frint versions so ISEL will correctly
// add the chain to the strict versions.
def : Pat<(f32 (fnearbyint f32:$S)),
          (f32 (COPY_TO_REGCLASS (XSRDPIC
                                   (COPY_TO_REGCLASS $S, VSFRC)), VSSRC))>;
def : Pat<(f64 (fnearbyint f64:$S)),
          (f64 (XSRDPIC $S))>;
def : Pat<(v2f64 (fnearbyint v2f64:$S)),
          (v2f64 (XVRDPIC $S))>;
def : Pat<(v4f32 (fnearbyint v4f32:$S)),
          (v4f32 (XVRSPIC $S))>;

// Materialize a zero-vector of long long
def : Pat<(v2i64 immAllZerosV),
          (v2i64 (XXLXORz))>;

// Build vectors of floating point converted to i32.
def : Pat<(v4i32 (build_vector DblToInt.A, DblToInt.A,
                               DblToInt.A, DblToInt.A)),
          (v4i32 (XXSPLTW (SUBREG_TO_REG (i64 1), (XSCVDPSXWS $A), sub_64), 1))>;
def : Pat<(v4i32 (build_vector DblToUInt.A, DblToUInt.A,
                               DblToUInt.A, DblToUInt.A)),
          (v4i32 (XXSPLTW (SUBREG_TO_REG (i64 1), (XSCVDPUXWS $A), sub_64), 1))>;
def : Pat<(v2i64 (build_vector DblToLong.A, DblToLong.A)),
          (v2i64 (XXPERMDI (SUBREG_TO_REG (i64 1), (XSCVDPSXDS $A), sub_64),
                           (SUBREG_TO_REG (i64 1), (XSCVDPSXDS $A), sub_64), 0))>;
def : Pat<(v2i64 (build_vector DblToULong.A, DblToULong.A)),
          (v2i64 (XXPERMDI (SUBREG_TO_REG (i64 1), (XSCVDPUXDS $A), sub_64),
                           (SUBREG_TO_REG (i64 1), (XSCVDPUXDS $A), sub_64), 0))>;
def : Pat<(v4i32 (PPCSToV DblToInt.A)),
          (v4i32 (SUBREG_TO_REG (i64 1), (XSCVDPSXWS f64:$A), sub_64))>;
def : Pat<(v4i32 (PPCSToV DblToUInt.A)),
          (v4i32 (SUBREG_TO_REG (i64 1), (XSCVDPUXWS f64:$A), sub_64))>;
defm : ScalToVecWPermute<
  v4i32, FltToIntLoad.A,
  (XXSPLTW (SUBREG_TO_REG (i64 1), (XSCVDPSXWSs (XFLOADf32 ForceXForm:$A)), sub_64), 1),
  (SUBREG_TO_REG (i64 1), (XSCVDPSXWSs (XFLOADf32 ForceXForm:$A)), sub_64)>;
defm : ScalToVecWPermute<
  v4i32, FltToUIntLoad.A,
  (XXSPLTW (SUBREG_TO_REG (i64 1), (XSCVDPUXWSs (XFLOADf32 ForceXForm:$A)), sub_64), 1),
  (SUBREG_TO_REG (i64 1), (XSCVDPUXWSs (XFLOADf32 ForceXForm:$A)), sub_64)>;
def : Pat<(v4f32 (build_vector (f32 (fpround f64:$A)), (f32 (fpround f64:$A)),
                               (f32 (fpround f64:$A)), (f32 (fpround f64:$A)))),
          (v4f32 (XXSPLTW (SUBREG_TO_REG (i64 1), (XSCVDPSP f64:$A), sub_64), 0))>;

def : Pat<(v4f32 (build_vector f32:$A, f32:$A, f32:$A, f32:$A)),
          (v4f32 (XXSPLTW (v4f32 (XSCVDPSPN $A)), 0))>;

// Splat loads.
def : Pat<(v2f64 (PPCldsplat ForceXForm:$A)),
          (v2f64 (LXVDSX ForceXForm:$A))>;
def : Pat<(v4f32 (PPCldsplat ForceXForm:$A)),
          (v4f32 (XXSPLTW (SUBREG_TO_REG (i64 1), (LFIWZX ForceXForm:$A), sub_64), 1))>;
def : Pat<(v2i64 (PPCldsplat ForceXForm:$A)),
          (v2i64 (LXVDSX ForceXForm:$A))>;
def : Pat<(v4i32 (PPCldsplat ForceXForm:$A)),
          (v4i32 (XXSPLTW (SUBREG_TO_REG (i64 1), (LFIWZX ForceXForm:$A), sub_64), 1))>;
def : Pat<(v2i64 (PPCzextldsplat ForceXForm:$A)),
          (v2i64 (XXPERMDIs (LFIWZX ForceXForm:$A), 0))>;
def : Pat<(v2i64 (PPCsextldsplat ForceXForm:$A)),
          (v2i64 (XXPERMDIs (LFIWAX ForceXForm:$A), 0))>;

// Build vectors of floating point converted to i64.
def : Pat<(v2i64 (build_vector FltToLong.A, FltToLong.A)),
          (v2i64 (XXPERMDIs
                   (COPY_TO_REGCLASS (XSCVDPSXDSs $A), VSFRC), 0))>;
def : Pat<(v2i64 (build_vector FltToULong.A, FltToULong.A)),
          (v2i64 (XXPERMDIs
                   (COPY_TO_REGCLASS (XSCVDPUXDSs $A), VSFRC), 0))>;
defm : ScalToVecWPermute<
  v2i64, DblToLongLoad.A,
  (XVCVDPSXDS (LXVDSX ForceXForm:$A)), (XVCVDPSXDS (LXVDSX ForceXForm:$A))>;
defm : ScalToVecWPermute<
  v2i64, DblToULongLoad.A,
  (XVCVDPUXDS (LXVDSX ForceXForm:$A)), (XVCVDPUXDS (LXVDSX ForceXForm:$A))>;

// Doubleword vector predicate comparisons without Power8.
let AddedComplexity = 0 in {
def : Pat<(v2i64 (PPCvcmp_rec v2i64:$vA, v2i64:$vB, 967)),
          (VCMPGTUB_rec DblwdCmp.MRGSGT, (v2i64 (XXLXORz)))>;
def : Pat<(v2i64 (PPCvcmp_rec v2i64:$vA, v2i64:$vB, 711)),
          (VCMPGTUB_rec DblwdCmp.MRGUGT, (v2i64 (XXLXORz)))>;
def : Pat<(v2i64 (PPCvcmp_rec v2i64:$vA, v2i64:$vB, 199)),
          (VCMPGTUB_rec DblwdCmp.MRGEQ, (v2i64 (XXLXORz)))>;
} // AddedComplexity = 0

// XL Compat builtins.
def : Pat<(int_ppc_fmsub f64:$A, f64:$B, f64:$C), (XSMSUBMDP $A, $B, $C)>;
def : Pat<(int_ppc_fnmadd f64:$A, f64:$B, f64:$C), (XSNMADDMDP $A, $B, $C)>;
def : Pat<(int_ppc_fre f64:$A), (XSREDP $A)>;
def : Pat<(int_ppc_frsqrte vsfrc:$XB), (XSRSQRTEDP $XB)>;
def : Pat<(int_ppc_fnabs f64:$A), (XSNABSDP $A)>;
def : Pat<(int_ppc_fnabss f32:$A), (XSNABSDPs $A)>;

// XXMRG[LH]W is a direct replacement for VMRG[LH]W respectively.
// Prefer the VSX form for greater register range.
def:Pat<(vmrglw_unary_shuffle v16i8:$vA, undef),
        (COPY_TO_REGCLASS (XXMRGLW (COPY_TO_REGCLASS $vA, VSRC),
                                   (COPY_TO_REGCLASS $vA, VSRC)), VRRC)>;
def:Pat<(vmrghw_unary_shuffle v16i8:$vA, undef),
        (COPY_TO_REGCLASS (XXMRGHW (COPY_TO_REGCLASS $vA, VSRC),
                                   (COPY_TO_REGCLASS $vA, VSRC)), VRRC)>;
def:Pat<(vmrglw_shuffle v16i8:$vA, v16i8:$vB),
        (COPY_TO_REGCLASS (XXMRGLW (COPY_TO_REGCLASS $vA, VSRC),
                                   (COPY_TO_REGCLASS $vB, VSRC)), VRRC)>;
def:Pat<(vmrghw_shuffle v16i8:$vA, v16i8:$vB),
        (COPY_TO_REGCLASS (XXMRGHW (COPY_TO_REGCLASS $vA, VSRC),
                                   (COPY_TO_REGCLASS $vB, VSRC)), VRRC)>;
def:Pat<(vmrglw_swapped_shuffle v16i8:$vA, v16i8:$vB),
        (COPY_TO_REGCLASS (XXMRGLW (COPY_TO_REGCLASS $vB, VSRC),
                                   (COPY_TO_REGCLASS $vA, VSRC)), VRRC)>;
def:Pat<(vmrghw_swapped_shuffle v16i8:$vA, v16i8:$vB),
        (COPY_TO_REGCLASS (XXMRGHW (COPY_TO_REGCLASS $vB, VSRC),
                                   (COPY_TO_REGCLASS $vA, VSRC)), VRRC)>;
def : Pat<(PPCstore_scal_int_from_vsr f64:$src, XForm:$dst, 8),
          (STXSDX $src, XForm:$dst)>;
def : Pat<(PPCstore_scal_int_from_vsr f128:$src, XForm:$dst, 8),
          (STXSDX (COPY_TO_REGCLASS $src, VSFRC), XForm:$dst)>;
} // HasVSX

// Any big endian VSX subtarget.
let Predicates = [HasVSX, IsBigEndian] in {
def : Pat<(v2f64 (scalar_to_vector f64:$A)),
          (v2f64 (SUBREG_TO_REG (i64 1), $A, sub_64))>;

def : Pat<(f64 (extractelt v2f64:$S, 0)),
          (f64 (EXTRACT_SUBREG $S, sub_64))>;
def : Pat<(f64 (extractelt v2f64:$S, 1)),
          (f64 (EXTRACT_SUBREG (XXPERMDI $S, $S, 2), sub_64))>;
def : Pat<(f64 (PPCfcfid (PPCmtvsra (i64 (vector_extract v2i64:$S, 0))))),
          (f64 (XSCVSXDDP (COPY_TO_REGCLASS $S, VSFRC)))>;
def : Pat<(f64 (PPCfcfid (PPCmtvsra (i64 (vector_extract v2i64:$S, 1))))),
          (f64 (XSCVSXDDP (COPY_TO_REGCLASS (XXPERMDI $S, $S, 2), VSFRC)))>;
def : Pat<(f64 (PPCfcfidu (PPCmtvsra (i64 (vector_extract v2i64:$S, 0))))),
          (f64 (XSCVUXDDP (COPY_TO_REGCLASS $S, VSFRC)))>;
def : Pat<(f64 (PPCfcfidu (PPCmtvsra (i64 (vector_extract v2i64:$S, 1))))),
          (f64 (XSCVUXDDP (COPY_TO_REGCLASS (XXPERMDI $S, $S, 2), VSFRC)))>;

def : Pat<(f64 (vector_extract v2f64:$S, i64:$Idx)),
          (f64 VectorExtractions.BE_VARIABLE_DOUBLE)>;

def : Pat<(v2f64 (build_vector f64:$A, f64:$B)),
          (v2f64 (XXPERMDI
                    (SUBREG_TO_REG (i64 1), $A, sub_64),
                    (SUBREG_TO_REG (i64 1), $B, sub_64), 0))>;
// Using VMRGEW to assemble the final vector would be a lower latency
// solution. However, we choose to go with the slightly higher latency
// XXPERMDI for 2 reasons:
// 1. This is likely to occur in unrolled loops where regpressure is high,
//    so we want to use the latter as it has access to all 64 VSX registers.
// 2. Using Altivec instructions in this sequence would likely cause the
//    allocation of Altivec registers even for the loads which in turn would
//    force the use of LXSIWZX for the loads, adding a cycle of latency to
//    each of the loads which would otherwise be able to use LFIWZX.
def : Pat<(v4f32 (build_vector LoadFP.A, LoadFP.B, LoadFP.C, LoadFP.D)),
          (v4f32 (XXPERMDI (XXMRGHW MrgFP.LD32A, MrgFP.LD32B),
                           (XXMRGHW MrgFP.LD32C, MrgFP.LD32D), 3))>;
def : Pat<(v4f32 (build_vector f32:$A, f32:$B, f32:$C, f32:$D)),
          (VMRGEW MrgFP.AC, MrgFP.BD)>;
def : Pat<(v4f32 (build_vector DblToFlt.A0, DblToFlt.A1,
                               DblToFlt.B0, DblToFlt.B1)),
          (v4f32 (VMRGEW MrgFP.ABhToFlt, MrgFP.ABlToFlt))>;

// Convert 4 doubles to a vector of ints.
def : Pat<(v4i32 (build_vector DblToInt.A, DblToInt.B,
                               DblToInt.C, DblToInt.D)),
          (v4i32 (VMRGEW MrgWords.CVACS, MrgWords.CVBDS))>;
def : Pat<(v4i32 (build_vector DblToUInt.A, DblToUInt.B,
                               DblToUInt.C, DblToUInt.D)),
          (v4i32 (VMRGEW MrgWords.CVACU, MrgWords.CVBDU))>;
def : Pat<(v4i32 (build_vector ExtDbl.A0S, ExtDbl.A1S,
                               ExtDbl.B0S, ExtDbl.B1S)),
          (v4i32 (VMRGEW MrgWords.CVA0B0S, MrgWords.CVA1B1S))>;
def : Pat<(v4i32 (build_vector ExtDbl.A0U, ExtDbl.A1U,
                               ExtDbl.B0U, ExtDbl.B1U)),
          (v4i32 (VMRGEW MrgWords.CVA0B0U, MrgWords.CVA1B1U))>;
def : Pat<(v2f64 (build_vector (f64 (fpextend (extractelt v4f32:$A, 0))),
                               (f64 (fpextend (extractelt v4f32:$A, 1))))),
          (v2f64 (XVCVSPDP (XXMRGHW $A, $A)))>;
def : Pat<(v2f64 (build_vector (f64 (fpextend (extractelt v4f32:$A, 1))),
                               (f64 (fpextend (extractelt v4f32:$A, 0))))),
          (v2f64 (XXPERMDI (XVCVSPDP (XXMRGHW $A, $A)),
                           (XVCVSPDP (XXMRGHW $A, $A)), 2))>;
def : Pat<(v2f64 (build_vector (f64 (fpextend (extractelt v4f32:$A, 0))),
                               (f64 (fpextend (extractelt v4f32:$A, 2))))),
          (v2f64 (XVCVSPDP $A))>;
def : Pat<(v2f64 (build_vector (f64 (fpextend (extractelt v4f32:$A, 1))),
                               (f64 (fpextend (extractelt v4f32:$A, 3))))),
          (v2f64 (XVCVSPDP (XXSLDWI $A, $A, 1)))>;
def : Pat<(v2f64 (build_vector (f64 (fpextend (extractelt v4f32:$A, 2))),
                               (f64 (fpextend (extractelt v4f32:$A, 3))))),
          (v2f64 (XVCVSPDP (XXMRGLW $A, $A)))>;
def : Pat<(v2f64 (build_vector (f64 (fpextend (extractelt v4f32:$A, 3))),
                               (f64 (fpextend (extractelt v4f32:$A, 2))))),
          (v2f64 (XXPERMDI (XVCVSPDP (XXMRGLW $A, $A)),
                           (XVCVSPDP (XXMRGLW $A, $A)), 2))>;
def : Pat<(v2f64 (build_vector (f64 (fpextend (extractelt v4f32:$A, 0))),
                               (f64 (fpextend (extractelt v4f32:$B, 0))))),
          (v2f64 (XVCVSPDP (XXPERMDI $A, $B, 0)))>;
def : Pat<(v2f64 (build_vector (f64 (fpextend (extractelt v4f32:$A, 3))),
                               (f64 (fpextend (extractelt v4f32:$B, 3))))),
          (v2f64 (XVCVSPDP (XXSLDWI (XXPERMDI $A, $B, 3),
                                    (XXPERMDI $A, $B, 3), 1)))>;
def : Pat<(v2i64 (fp_to_sint
                   (build_vector (f64 (fpextend (extractelt v4f32:$A, 0))),
                                 (f64 (fpextend (extractelt v4f32:$A, 2)))))),
          (v2i64 (XVCVSPSXDS $A))>;
def : Pat<(v2i64 (fp_to_uint
                   (build_vector (f64 (fpextend (extractelt v4f32:$A, 0))),
                                 (f64 (fpextend (extractelt v4f32:$A, 2)))))),
          (v2i64 (XVCVSPUXDS $A))>;
def : Pat<(v2i64 (fp_to_sint
                   (build_vector (f64 (fpextend (extractelt v4f32:$A, 1))),
                                 (f64 (fpextend (extractelt v4f32:$A, 3)))))),
          (v2i64 (XVCVSPSXDS (XXSLDWI $A, $A, 1)))>;
def : Pat<(v2i64 (fp_to_uint
                   (build_vector (f64 (fpextend (extractelt v4f32:$A, 1))),
                                 (f64 (fpextend (extractelt v4f32:$A, 3)))))),
          (v2i64 (XVCVSPUXDS (XXSLDWI $A, $A, 1)))>;
def : Pat<WToDPExtractConv.BV02S,
          (v2f64 (XVCVSXWDP $A))>;
def : Pat<WToDPExtractConv.BV13S,
          (v2f64 (XVCVSXWDP (XXSLDWI $A, $A, 1)))>;
def : Pat<WToDPExtractConv.BV02U,
          (v2f64 (XVCVUXWDP $A))>;
def : Pat<WToDPExtractConv.BV13U,
          (v2f64 (XVCVUXWDP (XXSLDWI $A, $A, 1)))>;
def : Pat<(v2f64 (insertelt v2f64:$A, f64:$B, 0)),
          (v2f64 (XXPERMDI (SUBREG_TO_REG (i64 1), $B, sub_64), $A, 1))>;
def : Pat<(v2f64 (insertelt v2f64:$A, f64:$B, 1)),
          (v2f64 (XXPERMDI $A, (SUBREG_TO_REG (i64 1), $B, sub_64), 0))>;
} // HasVSX, IsBigEndian

// Any little endian VSX subtarget.
let Predicates = [HasVSX, IsLittleEndian] in {
defm : ScalToVecWPermute<v2f64, (f64 f64:$A),
                         (XXPERMDI (SUBREG_TO_REG (i64 1), $A, sub_64),
                                   (SUBREG_TO_REG (i64 1), $A, sub_64), 0),
                         (SUBREG_TO_REG (i64 1), $A, sub_64)>;

def : Pat<(f64 (extractelt v2f64:$S, 0)),
          (f64 (EXTRACT_SUBREG (XXPERMDI $S, $S, 2), sub_64))>;
def : Pat<(f64 (extractelt v2f64:$S, 1)),
          (f64 (EXTRACT_SUBREG $S, sub_64))>;

def : Pat<(v2f64 (PPCld_vec_be ForceXForm:$src)), (LXVD2X ForceXForm:$src)>;
def : Pat<(PPCst_vec_be v2f64:$rS, ForceXForm:$dst), (STXVD2X $rS, ForceXForm:$dst)>;
def : Pat<(v4f32 (PPCld_vec_be ForceXForm:$src)), (LXVW4X ForceXForm:$src)>;
def : Pat<(PPCst_vec_be v4f32:$rS, ForceXForm:$dst), (STXVW4X $rS, ForceXForm:$dst)>;
def : Pat<(v2i64 (PPCld_vec_be ForceXForm:$src)), (LXVD2X ForceXForm:$src)>;
def : Pat<(PPCst_vec_be v2i64:$rS, ForceXForm:$dst), (STXVD2X $rS, ForceXForm:$dst)>;
def : Pat<(v4i32 (PPCld_vec_be ForceXForm:$src)), (LXVW4X ForceXForm:$src)>;
def : Pat<(PPCst_vec_be v4i32:$rS, ForceXForm:$dst), (STXVW4X $rS, ForceXForm:$dst)>;
def : Pat<(f64 (PPCfcfid (PPCmtvsra (i64 (vector_extract v2i64:$S, 0))))),
          (f64 (XSCVSXDDP (COPY_TO_REGCLASS (XXPERMDI $S, $S, 2), VSFRC)))>;
def : Pat<(f64 (PPCfcfid (PPCmtvsra (i64 (vector_extract v2i64:$S, 1))))),
          (f64 (XSCVSXDDP (COPY_TO_REGCLASS (f64 (COPY_TO_REGCLASS $S, VSRC)), VSFRC)))>;
def : Pat<(f64 (PPCfcfidu (PPCmtvsra (i64 (vector_extract v2i64:$S, 0))))),
          (f64 (XSCVUXDDP (COPY_TO_REGCLASS (XXPERMDI $S, $S, 2), VSFRC)))>;
def : Pat<(f64 (PPCfcfidu (PPCmtvsra (i64 (vector_extract v2i64:$S, 1))))),
          (f64 (XSCVUXDDP (COPY_TO_REGCLASS (f64 (COPY_TO_REGCLASS $S, VSRC)), VSFRC)))>;

def : Pat<(f64 (vector_extract v2f64:$S, i64:$Idx)),
          (f64 VectorExtractions.LE_VARIABLE_DOUBLE)>;

// Little endian, available on all targets with VSX
def : Pat<(v2f64 (build_vector f64:$A, f64:$B)),
          (v2f64 (XXPERMDI
                    (SUBREG_TO_REG (i64 1), $B, sub_64),
                    (SUBREG_TO_REG (i64 1), $A, sub_64), 0))>;
// Using VMRGEW to assemble the final vector would be a lower latency
// solution. However, we choose to go with the slightly higher latency
// XXPERMDI for 2 reasons:
// 1. This is likely to occur in unrolled loops where regpressure is high,
//    so we want to use the latter as it has access to all 64 VSX registers.
// 2. Using Altivec instructions in this sequence would likely cause the
//    allocation of Altivec registers even for the loads which in turn would
//    force the use of LXSIWZX for the loads, adding a cycle of latency to
//    each of the loads which would otherwise be able to use LFIWZX.
def : Pat<(v4f32 (build_vector LoadFP.A, LoadFP.B, LoadFP.C, LoadFP.D)),
          (v4f32 (XXPERMDI (XXMRGHW MrgFP.LD32D, MrgFP.LD32C),
                           (XXMRGHW MrgFP.LD32B, MrgFP.LD32A), 3))>;
def : Pat<(v4f32 (build_vector f32:$D, f32:$C, f32:$B, f32:$A)),
          (VMRGEW MrgFP.AC, MrgFP.BD)>;
def : Pat<(v4f32 (build_vector DblToFlt.A0, DblToFlt.A1,
                               DblToFlt.B0, DblToFlt.B1)),
          (v4f32 (VMRGEW MrgFP.BAhToFlt, MrgFP.BAlToFlt))>;

// Convert 4 doubles to a vector of ints.
def : Pat<(v4i32 (build_vector DblToInt.A, DblToInt.B,
                               DblToInt.C, DblToInt.D)),
          (v4i32 (VMRGEW MrgWords.CVDBS, MrgWords.CVCAS))>;
def : Pat<(v4i32 (build_vector DblToUInt.A, DblToUInt.B,
                               DblToUInt.C, DblToUInt.D)),
          (v4i32 (VMRGEW MrgWords.CVDBU, MrgWords.CVCAU))>;
def : Pat<(v4i32 (build_vector ExtDbl.A0S, ExtDbl.A1S,
                               ExtDbl.B0S, ExtDbl.B1S)),
          (v4i32 (VMRGEW MrgWords.CVB1A1S, MrgWords.CVB0A0S))>;
def : Pat<(v4i32 (build_vector ExtDbl.A0U, ExtDbl.A1U,
                               ExtDbl.B0U, ExtDbl.B1U)),
          (v4i32 (VMRGEW MrgWords.CVB1A1U, MrgWords.CVB0A0U))>;
def : Pat<(v2f64 (build_vector (f64 (fpextend (extractelt v4f32:$A, 0))),
                               (f64 (fpextend (extractelt v4f32:$A, 1))))),
          (v2f64 (XVCVSPDP (XXMRGLW $A, $A)))>;
def : Pat<(v2f64 (build_vector (f64 (fpextend (extractelt v4f32:$A, 1))),
                               (f64 (fpextend (extractelt v4f32:$A, 0))))),
          (v2f64 (XXPERMDI (XVCVSPDP (XXMRGLW $A, $A)),
                           (XVCVSPDP (XXMRGLW $A, $A)), 2))>;
def : Pat<(v2f64 (build_vector (f64 (fpextend (extractelt v4f32:$A, 0))),
                               (f64 (fpextend (extractelt v4f32:$A, 2))))),
          (v2f64 (XVCVSPDP (XXSLDWI $A, $A, 1)))>;
def : Pat<(v2f64 (build_vector (f64 (fpextend (extractelt v4f32:$A, 1))),
                               (f64 (fpextend (extractelt v4f32:$A, 3))))),
          (v2f64 (XVCVSPDP $A))>;
def : Pat<(v2f64 (build_vector (f64 (fpextend (extractelt v4f32:$A, 2))),
                               (f64 (fpextend (extractelt v4f32:$A, 3))))),
          (v2f64 (XVCVSPDP (XXMRGHW $A, $A)))>;
def : Pat<(v2f64 (build_vector (f64 (fpextend (extractelt v4f32:$A, 3))),
                               (f64 (fpextend (extractelt v4f32:$A, 2))))),
          (v2f64 (XXPERMDI (XVCVSPDP (XXMRGHW $A, $A)),
                           (XVCVSPDP (XXMRGHW $A, $A)), 2))>;
def : Pat<(v2f64 (build_vector (f64 (fpextend (extractelt v4f32:$A, 0))),
                               (f64 (fpextend (extractelt v4f32:$B, 0))))),
          (v2f64 (XVCVSPDP (XXSLDWI (XXPERMDI $B, $A, 3),
                                    (XXPERMDI $B, $A, 3), 1)))>;
def : Pat<(v2f64 (build_vector (f64 (fpextend (extractelt v4f32:$A, 3))),
                               (f64 (fpextend (extractelt v4f32:$B, 3))))),
          (v2f64 (XVCVSPDP (XXPERMDI $B, $A, 0)))>;
def : Pat<(v2i64 (fp_to_sint
                   (build_vector (f64 (fpextend (extractelt v4f32:$A, 1))),
                                 (f64 (fpextend (extractelt v4f32:$A, 3)))))),
          (v2i64 (XVCVSPSXDS $A))>;
def : Pat<(v2i64 (fp_to_uint
                   (build_vector (f64 (fpextend (extractelt v4f32:$A, 1))),
                                 (f64 (fpextend (extractelt v4f32:$A, 3)))))),
          (v2i64 (XVCVSPUXDS $A))>;
def : Pat<(v2i64 (fp_to_sint
                   (build_vector (f64 (fpextend (extractelt v4f32:$A, 0))),
                                 (f64 (fpextend (extractelt v4f32:$A, 2)))))),
          (v2i64 (XVCVSPSXDS (XXSLDWI $A, $A, 1)))>;
def : Pat<(v2i64 (fp_to_uint
                   (build_vector (f64 (fpextend (extractelt v4f32:$A, 0))),
                                 (f64 (fpextend (extractelt v4f32:$A, 2)))))),
          (v2i64 (XVCVSPUXDS (XXSLDWI $A, $A, 1)))>;
def : Pat<WToDPExtractConv.BV02S,
          (v2f64 (XVCVSXWDP (XXSLDWI $A, $A, 1)))>;
def : Pat<WToDPExtractConv.BV13S,
          (v2f64 (XVCVSXWDP $A))>;
def : Pat<WToDPExtractConv.BV02U,
          (v2f64 (XVCVUXWDP (XXSLDWI $A, $A, 1)))>;
def : Pat<WToDPExtractConv.BV13U,
          (v2f64 (XVCVUXWDP $A))>;
def : Pat<(v2f64 (insertelt v2f64:$A, f64:$B, 0)),
          (v2f64 (XXPERMDI $A, (SUBREG_TO_REG (i64 1), $B, sub_64), 0))>;
def : Pat<(v2f64 (insertelt v2f64:$A, f64:$B, 1)),
          (v2f64 (XXPERMDI (SUBREG_TO_REG (i64 1), $B, sub_64), $A, 1))>;
} // HasVSX, IsLittleEndian

// Any pre-Power9 VSX subtarget.
let Predicates = [HasVSX, NoP9Vector] in {
def : Pat<(PPCstore_scal_int_from_vsr f64:$src, ForceXForm:$dst, 8),
          (STXSDX $src, ForceXForm:$dst)>;
def : Pat<(PPCstore_scal_int_from_vsr f128:$src, ForceXForm:$dst, 8),
          (STXSDX (COPY_TO_REGCLASS $src, VSFRC), ForceXForm:$dst)>;

// Load-and-splat with fp-to-int conversion (using X-Form VSX/FP loads).
defm : ScalToVecWPermute<
  v4i32, DblToIntLoad.A,
  (XXSPLTW (SUBREG_TO_REG (i64 1), (XSCVDPSXWS (XFLOADf64 ForceXForm:$A)), sub_64), 1),
  (SUBREG_TO_REG (i64 1), (XSCVDPSXWS (XFLOADf64 ForceXForm:$A)), sub_64)>;
defm : ScalToVecWPermute<
  v4i32, DblToUIntLoad.A,
  (XXSPLTW (SUBREG_TO_REG (i64 1), (XSCVDPUXWS (XFLOADf64 ForceXForm:$A)), sub_64), 1),
  (SUBREG_TO_REG (i64 1), (XSCVDPUXWS (XFLOADf64 ForceXForm:$A)), sub_64)>;
defm : ScalToVecWPermute<
  v2i64, FltToLongLoad.A,
  (XXPERMDIs (XSCVDPSXDS (COPY_TO_REGCLASS (XFLOADf32 ForceXForm:$A), VSFRC)), 0),
  (SUBREG_TO_REG (i64 1), (XSCVDPSXDS (COPY_TO_REGCLASS (XFLOADf32 ForceXForm:$A),
                                                        VSFRC)), sub_64)>;
defm : ScalToVecWPermute<
  v2i64, FltToULongLoad.A,
  (XXPERMDIs (XSCVDPUXDS (COPY_TO_REGCLASS (XFLOADf32 ForceXForm:$A), VSFRC)), 0),
  (SUBREG_TO_REG (i64 1), (XSCVDPUXDS (COPY_TO_REGCLASS (XFLOADf32 ForceXForm:$A),
                                                        VSFRC)), sub_64)>;
} // HasVSX, NoP9Vector

// Any little endian pre-Power9 VSX subtarget.
let Predicates = [HasVSX, NoP9Vector, IsLittleEndian] in {
// Load-and-splat using only X-Form VSX loads.
defm : ScalToVecWPermute<
  v2i64, (i64 (load ForceXForm:$src)),
  (XXPERMDIs (XFLOADf64 ForceXForm:$src), 2),
  (SUBREG_TO_REG (i64 1), (XFLOADf64 ForceXForm:$src), sub_64)>;
defm : ScalToVecWPermute<
  v2f64, (f64 (load ForceXForm:$src)),
  (XXPERMDIs (XFLOADf64 ForceXForm:$src), 2),
  (SUBREG_TO_REG (i64 1), (XFLOADf64 ForceXForm:$src), sub_64)>;

// Splat loads.
def : Pat<(v8i16 (PPCldsplatAlign16 ForceXForm:$A)),
          (v8i16 (VSPLTH 7, (LVX ForceXForm:$A)))>;
def : Pat<(v16i8 (PPCldsplatAlign16 ForceXForm:$A)),
          (v16i8 (VSPLTB 15, (LVX ForceXForm:$A)))>;
} // HasVSX, NoP9Vector, IsLittleEndian

let Predicates = [HasVSX, NoP9Vector, IsBigEndian] in {
  def : Pat<(v2f64 (int_ppc_vsx_lxvd2x ForceXForm:$src)),
            (LXVD2X ForceXForm:$src)>;
  def : Pat<(int_ppc_vsx_stxvd2x v2f64:$rS, ForceXForm:$dst),
            (STXVD2X $rS, ForceXForm:$dst)>;

  // Splat loads.
  def : Pat<(v8i16 (PPCldsplatAlign16 ForceXForm:$A)),
            (v8i16 (VSPLTH 0, (LVX ForceXForm:$A)))>;
  def : Pat<(v16i8 (PPCldsplatAlign16 ForceXForm:$A)),
            (v16i8 (VSPLTB 0, (LVX ForceXForm:$A)))>;
} // HasVSX, NoP9Vector, IsBigEndian

// Any VSX subtarget that only has loads and stores that load in big endian
// order regardless of endianness. This is really pre-Power9 subtargets.
let Predicates = [HasVSX, HasOnlySwappingMemOps] in {
  def : Pat<(v2f64 (PPClxvd2x ForceXForm:$src)), (LXVD2X ForceXForm:$src)>;

  // Stores.
  def : Pat<(PPCstxvd2x v2f64:$rS, ForceXForm:$dst), (STXVD2X $rS, ForceXForm:$dst)>;
} // HasVSX, HasOnlySwappingMemOps

// Big endian VSX subtarget that only has loads and stores that always
// load in big endian order. Really big endian pre-Power9 subtargets.
let Predicates = [HasVSX, HasOnlySwappingMemOps, IsBigEndian] in {
  def : Pat<(v2f64 (load ForceXForm:$src)), (LXVD2X ForceXForm:$src)>;
  def : Pat<(v2i64 (load ForceXForm:$src)), (LXVD2X ForceXForm:$src)>;
  def : Pat<(v4i32 (load ForceXForm:$src)), (LXVW4X ForceXForm:$src)>;
  def : Pat<(v4i32 (int_ppc_vsx_lxvw4x ForceXForm:$src)), (LXVW4X ForceXForm:$src)>;
  def : Pat<(store v2f64:$rS, ForceXForm:$dst), (STXVD2X $rS, ForceXForm:$dst)>;
  def : Pat<(store v2i64:$rS, ForceXForm:$dst), (STXVD2X $rS, ForceXForm:$dst)>;
  def : Pat<(store v4i32:$XT, ForceXForm:$dst), (STXVW4X $XT, ForceXForm:$dst)>;
  def : Pat<(int_ppc_vsx_stxvw4x v4i32:$rS, ForceXForm:$dst),
            (STXVW4X $rS, ForceXForm:$dst)>;
  def : Pat<(v2i64 (scalar_to_vector (i64 (load ForceXForm:$src)))),
           (SUBREG_TO_REG (i64 1), (XFLOADf64 ForceXForm:$src), sub_64)>;
} // HasVSX, HasOnlySwappingMemOps, IsBigEndian

// Target before Power8 with VSX.
let Predicates = [HasVSX, NoP8Vector] in {
def : Pat<(f32 (fpimm0neg)),
          (f32 (COPY_TO_REGCLASS (XSNEGDP (XXLXORdpz)), F4RC))>;

def : Pat<(f32 (nzFPImmExactInti5:$A)),
          (COPY_TO_REGCLASS (XVCVSXWDP (COPY_TO_REGCLASS
                     (VSPLTISW (getFPAs5BitExactInt fpimm:$A)), VSRC)), F4RC)>;

} // HasVSX, NoP8Vector

// Any Power8 VSX subtarget.
let Predicates = [HasVSX, HasP8Vector] in {
def : Pat<(int_ppc_vsx_xxleqv v4i32:$A, v4i32:$B),
          (XXLEQV $A, $B)>;
def : Pat<(f64 (extloadf32 XForm:$src)),
          (COPY_TO_REGCLASS (XFLOADf32 XForm:$src), VSFRC)>;
def : Pat<(f32 (fpround (f64 (extloadf32 ForceXForm:$src)))),
          (f32 (XFLOADf32 ForceXForm:$src))>;
def : Pat<(f64 (any_fpextend f32:$src)),
          (COPY_TO_REGCLASS $src, VSFRC)>;

def : Pat<(f32 (fpimm0neg)),
          (f32 (COPY_TO_REGCLASS (XSNEGDP (XXLXORdpz)), VSSRC))>;

def : Pat<(f32 (nzFPImmExactInti5:$A)),
          (COPY_TO_REGCLASS (XVCVSXWDP (COPY_TO_REGCLASS
                     (VSPLTISW (getFPAs5BitExactInt fpimm:$A)), VSRC)), VSSRC)>;

def : Pat<(f32 (selectcc i1:$lhs, i1:$rhs, f32:$tval, f32:$fval, SETLT)),
          (SELECT_VSSRC (CRANDC $lhs, $rhs), $tval, $fval)>;
def : Pat<(f32 (selectcc i1:$lhs, i1:$rhs, f32:$tval, f32:$fval, SETULT)),
          (SELECT_VSSRC (CRANDC $rhs, $lhs), $tval, $fval)>;
def : Pat<(f32 (selectcc i1:$lhs, i1:$rhs, f32:$tval, f32:$fval, SETLE)),
          (SELECT_VSSRC (CRORC  $lhs, $rhs), $tval, $fval)>;
def : Pat<(f32 (selectcc i1:$lhs, i1:$rhs, f32:$tval, f32:$fval, SETULE)),
          (SELECT_VSSRC (CRORC  $rhs, $lhs), $tval, $fval)>;
def : Pat<(f32 (selectcc i1:$lhs, i1:$rhs, f32:$tval, f32:$fval, SETEQ)),
          (SELECT_VSSRC (CREQV $lhs, $rhs), $tval, $fval)>;
def : Pat<(f32 (selectcc i1:$lhs, i1:$rhs, f32:$tval, f32:$fval, SETGE)),
          (SELECT_VSSRC (CRORC  $rhs, $lhs), $tval, $fval)>;
def : Pat<(f32 (selectcc i1:$lhs, i1:$rhs, f32:$tval, f32:$fval, SETUGE)),
          (SELECT_VSSRC (CRORC  $lhs, $rhs), $tval, $fval)>;
def : Pat<(f32 (selectcc i1:$lhs, i1:$rhs, f32:$tval, f32:$fval, SETGT)),
          (SELECT_VSSRC (CRANDC $rhs, $lhs), $tval, $fval)>;
def : Pat<(f32 (selectcc i1:$lhs, i1:$rhs, f32:$tval, f32:$fval, SETUGT)),
          (SELECT_VSSRC (CRANDC $lhs, $rhs), $tval, $fval)>;
def : Pat<(f32 (selectcc i1:$lhs, i1:$rhs, f32:$tval, f32:$fval, SETNE)),
          (SELECT_VSSRC (CRXOR $lhs, $rhs), $tval, $fval)>;

// Additional fnmsub pattern for PPC specific ISD opcode
def : Pat<(PPCfnmsub f32:$A, f32:$B, f32:$C),
          (XSNMSUBASP $C, $A, $B)>;
def : Pat<(fneg (PPCfnmsub f32:$A, f32:$B, f32:$C)),
          (XSMSUBASP $C, $A, $B)>;
def : Pat<(PPCfnmsub f32:$A, f32:$B, (fneg f32:$C)),
          (XSNMADDASP $C, $A, $B)>;

// f32 neg
// Although XSNEGDP is available in P7, we want to select it starting from P8,
// so that FNMSUBS can be selected for fneg-fmsub pattern on P7. (VSX version,
// XSNMSUBASP, is available since P8)
def : Pat<(f32 (fneg f32:$S)),
          (f32 (COPY_TO_REGCLASS (XSNEGDP
               (COPY_TO_REGCLASS $S, VSFRC)), VSSRC))>;

// Instructions for converting float to i32 feeding a store.
def : Pat<(PPCstore_scal_int_from_vsr f64:$src, ForceXForm:$dst, 4),
          (STIWX $src, ForceXForm:$dst)>;
def : Pat<(PPCstore_scal_int_from_vsr f128:$src, ForceXForm:$dst, 4),
          (STIWX (COPY_TO_REGCLASS $src, VSFRC), ForceXForm:$dst)>;

def : Pat<(PPCstore_scal_int_from_vsr f64:$src, ForceXForm:$dst, 4),
          (STXSIWX $src, ForceXForm:$dst)>;
def : Pat<(PPCstore_scal_int_from_vsr f128:$src, ForceXForm:$dst, 4),
          (STXSIWX (COPY_TO_REGCLASS $src, VSFRC), ForceXForm:$dst)>;

def : Pat<(v2i64 (smax v2i64:$src1, v2i64:$src2)),
          (v2i64 (VMAXSD (COPY_TO_REGCLASS $src1, VRRC),
                         (COPY_TO_REGCLASS $src2, VRRC)))>;
def : Pat<(v2i64 (umax v2i64:$src1, v2i64:$src2)),
          (v2i64 (VMAXUD (COPY_TO_REGCLASS $src1, VRRC),
                         (COPY_TO_REGCLASS $src2, VRRC)))>;
def : Pat<(v2i64 (smin v2i64:$src1, v2i64:$src2)),
          (v2i64 (VMINSD (COPY_TO_REGCLASS $src1, VRRC),
                         (COPY_TO_REGCLASS $src2, VRRC)))>;
def : Pat<(v2i64 (umin v2i64:$src1, v2i64:$src2)),
          (v2i64 (VMINUD (COPY_TO_REGCLASS $src1, VRRC),
                         (COPY_TO_REGCLASS $src2, VRRC)))>;

def : Pat<(v1i128 (bitconvert (v16i8 immAllOnesV))),
          (v1i128 (COPY_TO_REGCLASS(XXLEQVOnes), VSRC))>;
def : Pat<(v2i64 (bitconvert (v16i8 immAllOnesV))),
          (v2i64 (COPY_TO_REGCLASS(XXLEQVOnes), VSRC))>;
def : Pat<(v8i16 (bitconvert (v16i8 immAllOnesV))),
          (v8i16 (COPY_TO_REGCLASS(XXLEQVOnes), VSRC))>;
def : Pat<(v16i8 (bitconvert (v16i8 immAllOnesV))),
          (v16i8 (COPY_TO_REGCLASS(XXLEQVOnes), VSRC))>;

// XL Compat builtins.
def : Pat<(int_ppc_fmsubs f32:$A, f32:$B, f32:$C), (XSMSUBMSP $A, $B, $C)>;
def : Pat<(int_ppc_fnmadds f32:$A, f32:$B, f32:$C), (XSNMADDMSP $A, $B, $C)>;
def : Pat<(int_ppc_fres f32:$A), (XSRESP $A)>;
def : Pat<(i32 (int_ppc_extract_exp f64:$A)),
          (EXTRACT_SUBREG (XSXEXPDP (COPY_TO_REGCLASS $A, VSFRC)), sub_32)>;
def : Pat<(int_ppc_extract_sig f64:$A),
          (XSXSIGDP (COPY_TO_REGCLASS $A, VSFRC))>;
def : Pat<(f64 (int_ppc_insert_exp f64:$A, i64:$B)),
          (COPY_TO_REGCLASS (XSIEXPDP (COPY_TO_REGCLASS $A, G8RC), $B), F8RC)>;

def : Pat<(int_ppc_stfiw ForceXForm:$dst, f64:$XT),
          (STXSIWX f64:$XT, ForceXForm:$dst)>;
def : Pat<(int_ppc_frsqrtes vssrc:$XB), (XSRSQRTESP $XB)>;
} // HasVSX, HasP8Vector

// Any big endian Power8 VSX subtarget.
let Predicates = [HasVSX, HasP8Vector, IsBigEndian] in {
def : Pat<DWToSPExtractConv.El0SS1,
          (f32 (XSCVSXDSP (COPY_TO_REGCLASS $S1, VSFRC)))>;
def : Pat<DWToSPExtractConv.El1SS1,
          (f32 (XSCVSXDSP (COPY_TO_REGCLASS (XXPERMDI $S1, $S1, 2), VSFRC)))>;
def : Pat<DWToSPExtractConv.El0US1,
          (f32 (XSCVUXDSP (COPY_TO_REGCLASS $S1, VSFRC)))>;
def : Pat<DWToSPExtractConv.El1US1,
          (f32 (XSCVUXDSP (COPY_TO_REGCLASS (XXPERMDI $S1, $S1, 2), VSFRC)))>;

// v4f32 scalar <-> vector conversions (BE)
defm : ScalToVecWPermute<v4f32, (f32 f32:$A), (XSCVDPSPN $A), (XSCVDPSPN $A)>;
def : Pat<(f32 (vector_extract v4f32:$S, 0)),
          (f32 (XSCVSPDPN $S))>;
def : Pat<(f32 (vector_extract v4f32:$S, 1)),
          (f32 (XSCVSPDPN (XXSLDWI $S, $S, 1)))>;
def : Pat<(f32 (vector_extract v4f32:$S, 2)),
          (f32 (XSCVSPDPN (XXPERMDI $S, $S, 2)))>;
def : Pat<(f32 (vector_extract v4f32:$S, 3)),
          (f32 (XSCVSPDPN (XXSLDWI $S, $S, 3)))>;

def : Pat<(f32 (PPCfcfids (f64 (PPCmtvsra (i32 (extractelt v4i32:$A, 0)))))),
          (f32 (XSCVSPDPN (XVCVSXWSP (XXSPLTW $A, 0))))>;
def : Pat<(f32 (PPCfcfids (f64 (PPCmtvsra (i32 (extractelt v4i32:$A, 1)))))),
          (f32 (XSCVSPDPN (XVCVSXWSP (XXSPLTW $A, 1))))>;
def : Pat<(f32 (PPCfcfids (f64 (PPCmtvsra (i32 (extractelt v4i32:$A, 2)))))),
          (f32 (XSCVSPDPN (XVCVSXWSP (XXSPLTW $A, 2))))>;
def : Pat<(f32 (PPCfcfids (f64 (PPCmtvsra (i32 (extractelt v4i32:$A, 3)))))),
          (f32 (XSCVSPDPN (XVCVSXWSP (XXSPLTW $A, 3))))>;
def : Pat<(f64 (PPCfcfid (f64 (PPCmtvsra (i32 (extractelt v4i32:$A, 0)))))),
          (f64 (COPY_TO_REGCLASS (XVCVSXWDP (XXSPLTW $A, 0)), VSFRC))>;
def : Pat<(f64 (PPCfcfid (f64 (PPCmtvsra (i32 (extractelt v4i32:$A, 1)))))),
          (f64 (COPY_TO_REGCLASS (XVCVSXWDP (XXSPLTW $A, 1)), VSFRC))>;
def : Pat<(f64 (PPCfcfid (f64 (PPCmtvsra (i32 (extractelt v4i32:$A, 2)))))),
          (f64 (COPY_TO_REGCLASS (XVCVSXWDP (XXSPLTW $A, 2)), VSFRC))>;
def : Pat<(f64 (PPCfcfid (f64 (PPCmtvsra (i32 (extractelt v4i32:$A, 3)))))),
          (f64 (COPY_TO_REGCLASS (XVCVSXWDP (XXSPLTW $A, 3)), VSFRC))>;

def : Pat<(f32 (vector_extract v4f32:$S, i32:$Idx)),
          (f32 VectorExtractions.BE_32B_VARIABLE_FLOAT)>;

def : Pat<(f64 (vector_extract v2f64:$S, i32:$Idx)),
          (f64 VectorExtractions.BE_32B_VARIABLE_DOUBLE)>;

defm : ScalToVecWPermute<
  v4i32, (i32 (load ForceXForm:$src)),
  (XXSLDWIs (LIWZX ForceXForm:$src), 1),
  (SUBREG_TO_REG (i64 1), (LIWZX ForceXForm:$src), sub_64)>;
defm : ScalToVecWPermute<
  v4f32, (f32 (load ForceXForm:$src)),
  (XXSLDWIs (LIWZX ForceXForm:$src), 1),
  (SUBREG_TO_REG (i64 1), (LIWZX ForceXForm:$src), sub_64)>;
} // HasVSX, HasP8Vector, IsBigEndian

// Big endian Power8 64Bit VSX subtarget.
let Predicates = [HasVSX, HasP8Vector, IsBigEndian, IsPPC64] in {
def : Pat<(f32 (vector_extract v4f32:$S, i64:$Idx)),
          (f32 VectorExtractions.BE_VARIABLE_FLOAT)>;

// LIWAX - This instruction is used for sign extending i32 -> i64.
// LIWZX - This instruction will be emitted for i32, f32, and when
//         zero-extending i32 to i64 (zext i32 -> i64).
def : Pat<(v2i64 (scalar_to_vector (i64 (sextloadi32 ForceXForm:$src)))),
          (v2i64 (SUBREG_TO_REG (i64 1), (LIWAX ForceXForm:$src), sub_64))>;
def : Pat<(v2i64 (scalar_to_vector (i64 (zextloadi32 ForceXForm:$src)))),
          (v2i64 (SUBREG_TO_REG (i64 1), (LIWZX ForceXForm:$src), sub_64))>;

def : Pat<DWToSPExtractConv.BVU,
          (v4f32 (VPKUDUM (XXSLDWI (XVCVUXDSP $S1), (XVCVUXDSP $S1), 3),
                          (XXSLDWI (XVCVUXDSP $S2), (XVCVUXDSP $S2), 3)))>;
def : Pat<DWToSPExtractConv.BVS,
          (v4f32 (VPKUDUM (XXSLDWI (XVCVSXDSP $S1), (XVCVSXDSP $S1), 3),
                          (XXSLDWI (XVCVSXDSP $S2), (XVCVSXDSP $S2), 3)))>;
def : Pat<(store (i32 (extractelt v4i32:$A, 1)), ForceXForm:$src),
          (STIWX (EXTRACT_SUBREG $A, sub_64), ForceXForm:$src)>;
def : Pat<(store (f32 (extractelt v4f32:$A, 1)), ForceXForm:$src),
          (STIWX (EXTRACT_SUBREG $A, sub_64), ForceXForm:$src)>;

// Elements in a register on a BE system are in order <0, 1, 2, 3>.
// The store instructions store the second word from the left.
// So to align element zero, we need to modulo-left-shift by 3 words.
// Similar logic applies for elements 2 and 3.
foreach Idx = [ [0,3], [2,1], [3,2] ] in {
  def : Pat<(store (i32 (extractelt v4i32:$A, !head(Idx))), ForceXForm:$src),
            (STIWX (EXTRACT_SUBREG (XXSLDWI $A, $A, !head(!tail(Idx))),
                                   sub_64), ForceXForm:$src)>;
  def : Pat<(store (f32 (extractelt v4f32:$A, !head(Idx))), ForceXForm:$src),
            (STIWX (EXTRACT_SUBREG (XXSLDWI $A, $A, !head(!tail(Idx))),
                                   sub_64), ForceXForm:$src)>;
}
} // HasVSX, HasP8Vector, IsBigEndian, IsPPC64

// Little endian Power8 VSX subtarget.
let Predicates = [HasVSX, HasP8Vector, IsLittleEndian] in {
def : Pat<DWToSPExtractConv.El0SS1,
          (f32 (XSCVSXDSP (COPY_TO_REGCLASS (XXPERMDI $S1, $S1, 2), VSFRC)))>;
def : Pat<DWToSPExtractConv.El1SS1,
          (f32 (XSCVSXDSP (COPY_TO_REGCLASS
                            (f64 (COPY_TO_REGCLASS $S1, VSRC)), VSFRC)))>;
def : Pat<DWToSPExtractConv.El0US1,
          (f32 (XSCVUXDSP (COPY_TO_REGCLASS (XXPERMDI $S1, $S1, 2), VSFRC)))>;
def : Pat<DWToSPExtractConv.El1US1,
          (f32 (XSCVUXDSP (COPY_TO_REGCLASS
                            (f64 (COPY_TO_REGCLASS $S1, VSRC)), VSFRC)))>;

// v4f32 scalar <-> vector conversions (LE)
  defm : ScalToVecWPermute<v4f32, (f32 f32:$A),
                           (XXSLDWI (XSCVDPSPN $A), (XSCVDPSPN $A), 1),
                           (XSCVDPSPN $A)>;
def : Pat<(f32 (vector_extract v4f32:$S, 0)),
          (f32 (XSCVSPDPN (XXSLDWI $S, $S, 3)))>;
def : Pat<(f32 (vector_extract v4f32:$S, 1)),
          (f32 (XSCVSPDPN (XXPERMDI $S, $S, 2)))>;
def : Pat<(f32 (vector_extract v4f32:$S, 2)),
          (f32 (XSCVSPDPN (XXSLDWI $S, $S, 1)))>;
def : Pat<(f32 (vector_extract v4f32:$S, 3)),
          (f32 (XSCVSPDPN $S))>;
def : Pat<(f32 (vector_extract v4f32:$S, i64:$Idx)),
          (f32 VectorExtractions.LE_VARIABLE_FLOAT)>;

def : Pat<(f32 (PPCfcfids (f64 (PPCmtvsra (i32 (extractelt v4i32:$A, 0)))))),
          (f32 (XSCVSPDPN (XVCVSXWSP (XXSPLTW $A, 3))))>;
def : Pat<(f32 (PPCfcfids (f64 (PPCmtvsra (i32 (extractelt v4i32:$A, 1)))))),
          (f32 (XSCVSPDPN (XVCVSXWSP (XXSPLTW $A, 2))))>;
def : Pat<(f32 (PPCfcfids (f64 (PPCmtvsra (i32 (extractelt v4i32:$A, 2)))))),
          (f32 (XSCVSPDPN (XVCVSXWSP (XXSPLTW $A, 1))))>;
def : Pat<(f32 (PPCfcfids (f64 (PPCmtvsra (i32 (extractelt v4i32:$A, 3)))))),
          (f32 (XSCVSPDPN (XVCVSXWSP (XXSPLTW $A, 0))))>;
def : Pat<(f64 (PPCfcfid (f64 (PPCmtvsra (i32 (extractelt v4i32:$A, 0)))))),
          (f64 (COPY_TO_REGCLASS (XVCVSXWDP (XXSPLTW $A, 3)), VSFRC))>;
def : Pat<(f64 (PPCfcfid (f64 (PPCmtvsra (i32 (extractelt v4i32:$A, 1)))))),
          (f64 (COPY_TO_REGCLASS (XVCVSXWDP (XXSPLTW $A, 2)), VSFRC))>;
def : Pat<(f64 (PPCfcfid (f64 (PPCmtvsra (i32 (extractelt v4i32:$A, 2)))))),
          (f64 (COPY_TO_REGCLASS (XVCVSXWDP (XXSPLTW $A, 1)), VSFRC))>;
def : Pat<(f64 (PPCfcfid (f64 (PPCmtvsra (i32 (extractelt v4i32:$A, 3)))))),
          (f64 (COPY_TO_REGCLASS (XVCVSXWDP (XXSPLTW $A, 0)), VSFRC))>;

// LIWAX - This instruction is used for sign extending i32 -> i64.
// LIWZX - This instruction will be emitted for i32, f32, and when
//         zero-extending i32 to i64 (zext i32 -> i64).
defm : ScalToVecWPermute<
  v2i64, (i64 (sextloadi32 ForceXForm:$src)),
  (XXPERMDIs (LIWAX ForceXForm:$src), 2),
  (SUBREG_TO_REG (i64 1), (LIWAX ForceXForm:$src), sub_64)>;

defm : ScalToVecWPermute<
  v2i64, (i64 (zextloadi32 ForceXForm:$src)),
  (XXPERMDIs (LIWZX ForceXForm:$src), 2),
  (SUBREG_TO_REG (i64 1), (LIWZX ForceXForm:$src), sub_64)>;

defm : ScalToVecWPermute<
  v4i32, (i32 (load ForceXForm:$src)),
  (XXPERMDIs (LIWZX ForceXForm:$src), 2),
  (SUBREG_TO_REG (i64 1), (LIWZX ForceXForm:$src), sub_64)>;

defm : ScalToVecWPermute<
  v4f32, (f32 (load ForceXForm:$src)),
  (XXPERMDIs (LIWZX ForceXForm:$src), 2),
  (SUBREG_TO_REG (i64 1), (LIWZX ForceXForm:$src), sub_64)>;

def : Pat<DWToSPExtractConv.BVU,
          (v4f32 (VPKUDUM (XXSLDWI (XVCVUXDSP $S2), (XVCVUXDSP $S2), 3),
                          (XXSLDWI (XVCVUXDSP $S1), (XVCVUXDSP $S1), 3)))>;
def : Pat<DWToSPExtractConv.BVS,
          (v4f32 (VPKUDUM (XXSLDWI (XVCVSXDSP $S2), (XVCVSXDSP $S2), 3),
                          (XXSLDWI (XVCVSXDSP $S1), (XVCVSXDSP $S1), 3)))>;
def : Pat<(store (i32 (extractelt v4i32:$A, 2)), ForceXForm:$src),
          (STIWX (EXTRACT_SUBREG $A, sub_64), ForceXForm:$src)>;
def : Pat<(store (f32 (extractelt v4f32:$A, 2)), ForceXForm:$src),
          (STIWX (EXTRACT_SUBREG $A, sub_64), ForceXForm:$src)>;

// Elements in a register on a LE system are in order <3, 2, 1, 0>.
// The store instructions store the second word from the left.
// So to align element 3, we need to modulo-left-shift by 3 words.
// Similar logic applies for elements 0 and 1.
foreach Idx = [ [0,2], [1,1], [3,3] ] in {
  def : Pat<(store (i32 (extractelt v4i32:$A, !head(Idx))), ForceXForm:$src),
            (STIWX (EXTRACT_SUBREG (XXSLDWI $A, $A, !head(!tail(Idx))),
                                   sub_64), ForceXForm:$src)>;
  def : Pat<(store (f32 (extractelt v4f32:$A, !head(Idx))), ForceXForm:$src),
            (STIWX (EXTRACT_SUBREG (XXSLDWI $A, $A, !head(!tail(Idx))),
                                   sub_64), ForceXForm:$src)>;
}
} // HasVSX, HasP8Vector, IsLittleEndian

// Big endian pre-Power9 VSX subtarget.
let Predicates = [HasVSX, HasP8Vector, NoP9Vector, IsBigEndian, IsPPC64] in {
def : Pat<(store (i64 (extractelt v2i64:$A, 0)), ForceXForm:$src),
          (XFSTOREf64 (EXTRACT_SUBREG $A, sub_64), ForceXForm:$src)>;
def : Pat<(store (f64 (extractelt v2f64:$A, 0)), ForceXForm:$src),
          (XFSTOREf64 (EXTRACT_SUBREG $A, sub_64), ForceXForm:$src)>;
def : Pat<(store (i64 (extractelt v2i64:$A, 1)), ForceXForm:$src),
          (XFSTOREf64 (EXTRACT_SUBREG (XXPERMDI $A, $A, 2), sub_64),
                      ForceXForm:$src)>;
def : Pat<(store (f64 (extractelt v2f64:$A, 1)), ForceXForm:$src),
          (XFSTOREf64 (EXTRACT_SUBREG (XXPERMDI $A, $A, 2), sub_64),
                      ForceXForm:$src)>;
} // HasVSX, HasP8Vector, NoP9Vector, IsBigEndian, IsPPC64

// Little endian pre-Power9 VSX subtarget.
let Predicates = [HasVSX, HasP8Vector, NoP9Vector, IsLittleEndian] in {
def : Pat<(store (i64 (extractelt v2i64:$A, 0)), ForceXForm:$src),
          (XFSTOREf64 (EXTRACT_SUBREG (XXPERMDI $A, $A, 2), sub_64),
                      ForceXForm:$src)>;
def : Pat<(store (f64 (extractelt v2f64:$A, 0)), ForceXForm:$src),
          (XFSTOREf64 (EXTRACT_SUBREG (XXPERMDI $A, $A, 2), sub_64),
                      ForceXForm:$src)>;
def : Pat<(store (i64 (extractelt v2i64:$A, 1)), ForceXForm:$src),
          (XFSTOREf64 (EXTRACT_SUBREG $A, sub_64), ForceXForm:$src)>;
def : Pat<(store (f64 (extractelt v2f64:$A, 1)), ForceXForm:$src),
          (XFSTOREf64 (EXTRACT_SUBREG $A, sub_64), ForceXForm:$src)>;
} // HasVSX, HasP8Vector, NoP9Vector, IsLittleEndian

// Any VSX target with direct moves.
let Predicates = [HasVSX, HasDirectMove] in {
// bitconvert f32 -> i32
// (convert to 32-bit fp single, shift right 1 word, move to GPR)
def : Pat<(i32 (bitconvert f32:$A)), Bitcast.FltToInt>;

// bitconvert i32 -> f32
// (move to FPR, shift left 1 word, convert to 64-bit fp single)
def : Pat<(f32 (bitconvert i32:$A)),
          (f32 (XSCVSPDPN
                 (XXSLDWI MovesToVSR.LE_WORD_1, MovesToVSR.LE_WORD_1, 1)))>;

// bitconvert f64 -> i64
// (move to GPR, nothing else needed)
def : Pat<(i64 (bitconvert f64:$A)), Bitcast.DblToLong>;

// bitconvert i64 -> f64
// (move to FPR, nothing else needed)
def : Pat<(f64 (bitconvert i64:$S)),
          (f64 (MTVSRD $S))>;

// Rounding to integer.
def : Pat<(i64 (lrint f64:$S)),
          (i64 (MFVSRD (FCTID $S)))>;
def : Pat<(i64 (lrint f32:$S)),
          (i64 (MFVSRD (FCTID (COPY_TO_REGCLASS $S, F8RC))))>;
def : Pat<(i64 (llrint f64:$S)),
          (i64 (MFVSRD (FCTID $S)))>;
def : Pat<(i64 (llrint f32:$S)),
          (i64 (MFVSRD (FCTID (COPY_TO_REGCLASS $S, F8RC))))>;
def : Pat<(i64 (lround f64:$S)),
          (i64 (MFVSRD (FCTID (XSRDPI $S))))>;
def : Pat<(i64 (lround f32:$S)),
          (i64 (MFVSRD (FCTID (XSRDPI (COPY_TO_REGCLASS $S, VSFRC)))))>;
def : Pat<(i64 (llround f64:$S)),
          (i64 (MFVSRD (FCTID (XSRDPI $S))))>;
def : Pat<(i64 (llround f32:$S)),
          (i64 (MFVSRD (FCTID (XSRDPI (COPY_TO_REGCLASS $S, VSFRC)))))>;

// Alternate patterns for PPCmtvsrz where the output is v8i16 or v16i8 instead
// of f64
def : Pat<(v8i16 (PPCmtvsrz i32:$A)),
          (v8i16 (SUBREG_TO_REG (i64 1), (MTVSRWZ $A), sub_64))>;
def : Pat<(v16i8 (PPCmtvsrz i32:$A)),
          (v16i8 (SUBREG_TO_REG (i64 1), (MTVSRWZ $A), sub_64))>;

// Endianness-neutral constant splat on P8 and newer targets. The reason
// for this pattern is that on targets with direct moves, we don't expand
// BUILD_VECTOR nodes for v4i32.
def : Pat<(v4i32 (build_vector immSExt5NonZero:$A, immSExt5NonZero:$A,
                               immSExt5NonZero:$A, immSExt5NonZero:$A)),
          (v4i32 (VSPLTISW imm:$A))>;

// Splat loads.
def : Pat<(v8i16 (PPCldsplat ForceXForm:$A)),
          (v8i16 (VSPLTHs 3, (MTVSRWZ (LHZX ForceXForm:$A))))>;
def : Pat<(v16i8 (PPCldsplat ForceXForm:$A)),
          (v16i8 (VSPLTBs 7, (MTVSRWZ (LBZX ForceXForm:$A))))>;
} // HasVSX, HasDirectMove

// Big endian VSX subtarget with direct moves.
let Predicates = [HasVSX, HasDirectMove, IsBigEndian] in {
// v16i8 scalar <-> vector conversions (BE)
defm : ScalToVecWPermute<
  v16i8, (i32 i32:$A),
  (SUBREG_TO_REG (i64 1), MovesToVSR.BE_BYTE_0, sub_64),
  (SUBREG_TO_REG (i64 1), (MTVSRWZ $A), sub_64)>;
defm : ScalToVecWPermute<
  v8i16, (i32 i32:$A),
  (SUBREG_TO_REG (i64 1), MovesToVSR.BE_HALF_0, sub_64),
  (SUBREG_TO_REG (i64 1), (MTVSRWZ $A), sub_64)>;
defm : ScalToVecWPermute<
  v4i32, (i32 i32:$A),
  (SUBREG_TO_REG (i64 1), MovesToVSR.BE_WORD_0, sub_64),
  (SUBREG_TO_REG (i64 1), (MTVSRWZ $A), sub_64)>;
def : Pat<(v2i64 (scalar_to_vector i64:$A)),
          (v2i64 (SUBREG_TO_REG (i64 1), MovesToVSR.BE_DWORD_0, sub_64))>;

// v2i64 scalar <-> vector conversions (BE)
def : Pat<(i64 (vector_extract v2i64:$S, 0)),
          (i64 VectorExtractions.LE_DWORD_1)>;
def : Pat<(i64 (vector_extract v2i64:$S, 1)),
          (i64 VectorExtractions.LE_DWORD_0)>;
def : Pat<(i64 (vector_extract v2i64:$S, i64:$Idx)),
          (i64 VectorExtractions.BE_VARIABLE_DWORD)>;
} // HasVSX, HasDirectMove, IsBigEndian

// Little endian VSX subtarget with direct moves.
let Predicates = [HasVSX, HasDirectMove, IsLittleEndian] in {
  // v16i8 scalar <-> vector conversions (LE)
  defm : ScalToVecWPermute<v16i8, (i32 i32:$A),
                           (COPY_TO_REGCLASS MovesToVSR.LE_WORD_0, VSRC),
                           (COPY_TO_REGCLASS MovesToVSR.LE_WORD_1, VSRC)>;
  defm : ScalToVecWPermute<v8i16, (i32 i32:$A),
                           (COPY_TO_REGCLASS MovesToVSR.LE_WORD_0, VSRC),
                           (COPY_TO_REGCLASS MovesToVSR.LE_WORD_1, VSRC)>;
  defm : ScalToVecWPermute<v4i32, (i32 i32:$A), MovesToVSR.LE_WORD_0,
                           (SUBREG_TO_REG (i64 1), (MTVSRWZ $A), sub_64)>;
  defm : ScalToVecWPermute<v2i64, (i64 i64:$A), MovesToVSR.LE_DWORD_0,
                           MovesToVSR.LE_DWORD_1>;

  // v2i64 scalar <-> vector conversions (LE)
  def : Pat<(i64 (vector_extract v2i64:$S, 0)),
            (i64 VectorExtractions.LE_DWORD_0)>;
  def : Pat<(i64 (vector_extract v2i64:$S, 1)),
            (i64 VectorExtractions.LE_DWORD_1)>;
  def : Pat<(i64 (vector_extract v2i64:$S, i64:$Idx)),
            (i64 VectorExtractions.LE_VARIABLE_DWORD)>;
} // HasVSX, HasDirectMove, IsLittleEndian

// Big endian pre-P9 VSX subtarget with direct moves.
let Predicates = [HasVSX, HasDirectMove, NoP9Altivec, IsBigEndian] in {
def : Pat<(i32 (vector_extract v16i8:$S, 0)),
          (i32 VectorExtractions.LE_BYTE_15)>;
def : Pat<(i32 (vector_extract v16i8:$S, 1)),
          (i32 VectorExtractions.LE_BYTE_14)>;
def : Pat<(i32 (vector_extract v16i8:$S, 2)),
          (i32 VectorExtractions.LE_BYTE_13)>;
def : Pat<(i32 (vector_extract v16i8:$S, 3)),
          (i32 VectorExtractions.LE_BYTE_12)>;
def : Pat<(i32 (vector_extract v16i8:$S, 4)),
          (i32 VectorExtractions.LE_BYTE_11)>;
def : Pat<(i32 (vector_extract v16i8:$S, 5)),
          (i32 VectorExtractions.LE_BYTE_10)>;
def : Pat<(i32 (vector_extract v16i8:$S, 6)),
          (i32 VectorExtractions.LE_BYTE_9)>;
def : Pat<(i32 (vector_extract v16i8:$S, 7)),
          (i32 VectorExtractions.LE_BYTE_8)>;
def : Pat<(i32 (vector_extract v16i8:$S, 8)),
          (i32 VectorExtractions.LE_BYTE_7)>;
def : Pat<(i32 (vector_extract v16i8:$S, 9)),
          (i32 VectorExtractions.LE_BYTE_6)>;
def : Pat<(i32 (vector_extract v16i8:$S, 10)),
          (i32 VectorExtractions.LE_BYTE_5)>;
def : Pat<(i32 (vector_extract v16i8:$S, 11)),
          (i32 VectorExtractions.LE_BYTE_4)>;
def : Pat<(i32 (vector_extract v16i8:$S, 12)),
          (i32 VectorExtractions.LE_BYTE_3)>;
def : Pat<(i32 (vector_extract v16i8:$S, 13)),
          (i32 VectorExtractions.LE_BYTE_2)>;
def : Pat<(i32 (vector_extract v16i8:$S, 14)),
          (i32 VectorExtractions.LE_BYTE_1)>;
def : Pat<(i32 (vector_extract v16i8:$S, 15)),
          (i32 VectorExtractions.LE_BYTE_0)>;
def : Pat<(i32 (vector_extract v16i8:$S, i64:$Idx)),
          (i32 VectorExtractions.BE_VARIABLE_BYTE)>;

// v8i16 scalar <-> vector conversions (BE)
def : Pat<(i32 (vector_extract v8i16:$S, 0)),
          (i32 VectorExtractions.LE_HALF_7)>;
def : Pat<(i32 (vector_extract v8i16:$S, 1)),
          (i32 VectorExtractions.LE_HALF_6)>;
def : Pat<(i32 (vector_extract v8i16:$S, 2)),
          (i32 VectorExtractions.LE_HALF_5)>;
def : Pat<(i32 (vector_extract v8i16:$S, 3)),
          (i32 VectorExtractions.LE_HALF_4)>;
def : Pat<(i32 (vector_extract v8i16:$S, 4)),
          (i32 VectorExtractions.LE_HALF_3)>;
def : Pat<(i32 (vector_extract v8i16:$S, 5)),
          (i32 VectorExtractions.LE_HALF_2)>;
def : Pat<(i32 (vector_extract v8i16:$S, 6)),
          (i32 VectorExtractions.LE_HALF_1)>;
def : Pat<(i32 (vector_extract v8i16:$S, 7)),
          (i32 VectorExtractions.LE_HALF_0)>;
def : Pat<(i32 (vector_extract v8i16:$S, i64:$Idx)),
          (i32 VectorExtractions.BE_VARIABLE_HALF)>;

// v4i32 scalar <-> vector conversions (BE)
def : Pat<(i32 (vector_extract v4i32:$S, 0)),
          (i32 VectorExtractions.LE_WORD_3)>;
def : Pat<(i32 (vector_extract v4i32:$S, 1)),
          (i32 VectorExtractions.LE_WORD_2)>;
def : Pat<(i32 (vector_extract v4i32:$S, 2)),
          (i32 VectorExtractions.LE_WORD_1)>;
def : Pat<(i32 (vector_extract v4i32:$S, 3)),
          (i32 VectorExtractions.LE_WORD_0)>;
def : Pat<(i32 (vector_extract v4i32:$S, i64:$Idx)),
          (i32 VectorExtractions.BE_VARIABLE_WORD)>;
} // HasVSX, HasDirectMove, NoP9Altivec, IsBigEndian

// Little endian pre-P9 VSX subtarget with direct moves.
let Predicates = [HasVSX, HasDirectMove, NoP9Altivec, IsLittleEndian] in {
def : Pat<(i32 (vector_extract v16i8:$S, 0)),
          (i32 VectorExtractions.LE_BYTE_0)>;
def : Pat<(i32 (vector_extract v16i8:$S, 1)),
          (i32 VectorExtractions.LE_BYTE_1)>;
def : Pat<(i32 (vector_extract v16i8:$S, 2)),
          (i32 VectorExtractions.LE_BYTE_2)>;
def : Pat<(i32 (vector_extract v16i8:$S, 3)),
          (i32 VectorExtractions.LE_BYTE_3)>;
def : Pat<(i32 (vector_extract v16i8:$S, 4)),
          (i32 VectorExtractions.LE_BYTE_4)>;
def : Pat<(i32 (vector_extract v16i8:$S, 5)),
          (i32 VectorExtractions.LE_BYTE_5)>;
def : Pat<(i32 (vector_extract v16i8:$S, 6)),
          (i32 VectorExtractions.LE_BYTE_6)>;
def : Pat<(i32 (vector_extract v16i8:$S, 7)),
          (i32 VectorExtractions.LE_BYTE_7)>;
def : Pat<(i32 (vector_extract v16i8:$S, 8)),
          (i32 VectorExtractions.LE_BYTE_8)>;
def : Pat<(i32 (vector_extract v16i8:$S, 9)),
          (i32 VectorExtractions.LE_BYTE_9)>;
def : Pat<(i32 (vector_extract v16i8:$S, 10)),
          (i32 VectorExtractions.LE_BYTE_10)>;
def : Pat<(i32 (vector_extract v16i8:$S, 11)),
          (i32 VectorExtractions.LE_BYTE_11)>;
def : Pat<(i32 (vector_extract v16i8:$S, 12)),
          (i32 VectorExtractions.LE_BYTE_12)>;
def : Pat<(i32 (vector_extract v16i8:$S, 13)),
          (i32 VectorExtractions.LE_BYTE_13)>;
def : Pat<(i32 (vector_extract v16i8:$S, 14)),
          (i32 VectorExtractions.LE_BYTE_14)>;
def : Pat<(i32 (vector_extract v16i8:$S, 15)),
          (i32 VectorExtractions.LE_BYTE_15)>;
def : Pat<(i32 (vector_extract v16i8:$S, i64:$Idx)),
          (i32 VectorExtractions.LE_VARIABLE_BYTE)>;

// v8i16 scalar <-> vector conversions (LE)
def : Pat<(i32 (vector_extract v8i16:$S, 0)),
          (i32 VectorExtractions.LE_HALF_0)>;
def : Pat<(i32 (vector_extract v8i16:$S, 1)),
          (i32 VectorExtractions.LE_HALF_1)>;
def : Pat<(i32 (vector_extract v8i16:$S, 2)),
          (i32 VectorExtractions.LE_HALF_2)>;
def : Pat<(i32 (vector_extract v8i16:$S, 3)),
          (i32 VectorExtractions.LE_HALF_3)>;
def : Pat<(i32 (vector_extract v8i16:$S, 4)),
          (i32 VectorExtractions.LE_HALF_4)>;
def : Pat<(i32 (vector_extract v8i16:$S, 5)),
          (i32 VectorExtractions.LE_HALF_5)>;
def : Pat<(i32 (vector_extract v8i16:$S, 6)),
          (i32 VectorExtractions.LE_HALF_6)>;
def : Pat<(i32 (vector_extract v8i16:$S, 7)),
          (i32 VectorExtractions.LE_HALF_7)>;
def : Pat<(i32 (vector_extract v8i16:$S, i64:$Idx)),
          (i32 VectorExtractions.LE_VARIABLE_HALF)>;

// v4i32 scalar <-> vector conversions (LE)
def : Pat<(i32 (vector_extract v4i32:$S, 0)),
          (i32 VectorExtractions.LE_WORD_0)>;
def : Pat<(i32 (vector_extract v4i32:$S, 1)),
          (i32 VectorExtractions.LE_WORD_1)>;
def : Pat<(i32 (vector_extract v4i32:$S, 2)),
          (i32 VectorExtractions.LE_WORD_2)>;
def : Pat<(i32 (vector_extract v4i32:$S, 3)),
          (i32 VectorExtractions.LE_WORD_3)>;
def : Pat<(i32 (vector_extract v4i32:$S, i64:$Idx)),
          (i32 VectorExtractions.LE_VARIABLE_WORD)>;
} // HasVSX, HasDirectMove, NoP9Altivec, IsLittleEndian

// Big endian pre-Power9 64Bit VSX subtarget that has direct moves.
let Predicates = [HasVSX, HasDirectMove, NoP9Vector, IsBigEndian, IsPPC64] in {
// Big endian integer vectors using direct moves.
def : Pat<(v2i64 (build_vector i64:$A, i64:$B)),
          (v2i64 (XXPERMDI
                    (SUBREG_TO_REG (i64 1), (MTVSRD $A), sub_64),
                    (SUBREG_TO_REG (i64 1), (MTVSRD $B), sub_64), 0))>;
def : Pat<(v4i32 (build_vector i32:$A, i32:$B, i32:$C, i32:$D)),
          (XXPERMDI
            (SUBREG_TO_REG (i64 1),
              (MTVSRD (RLDIMI AnyExts.B, AnyExts.A, 32, 0)), sub_64),
            (SUBREG_TO_REG (i64 1),
              (MTVSRD (RLDIMI AnyExts.D, AnyExts.C, 32, 0)), sub_64), 0)>;
def : Pat<(v4i32 (build_vector i32:$A, i32:$A, i32:$A, i32:$A)),
          (XXSPLTW (SUBREG_TO_REG (i64 1), (MTVSRWZ $A), sub_64), 1)>;
} // HasVSX, HasDirectMove, NoP9Vector, IsBigEndian, IsPPC64

// Little endian pre-Power9 VSX subtarget that has direct moves.
let Predicates = [HasVSX, HasDirectMove, NoP9Vector, IsLittleEndian] in {
// Little endian integer vectors using direct moves.
def : Pat<(v2i64 (build_vector i64:$A, i64:$B)),
          (v2i64 (XXPERMDI
                    (SUBREG_TO_REG (i64 1), (MTVSRD $B), sub_64),
                    (SUBREG_TO_REG (i64 1), (MTVSRD $A), sub_64), 0))>;
def : Pat<(v4i32 (build_vector i32:$A, i32:$B, i32:$C, i32:$D)),
          (XXPERMDI
            (SUBREG_TO_REG (i64 1),
              (MTVSRD (RLDIMI AnyExts.C, AnyExts.D, 32, 0)), sub_64),
            (SUBREG_TO_REG (i64 1),
              (MTVSRD (RLDIMI AnyExts.A, AnyExts.B, 32, 0)), sub_64), 0)>;
def : Pat<(v4i32 (build_vector i32:$A, i32:$A, i32:$A, i32:$A)),
          (XXSPLTW (SUBREG_TO_REG (i64 1), (MTVSRWZ $A), sub_64), 1)>;
}

// Any Power9 VSX subtarget.
let Predicates = [HasVSX, HasP9Vector] in {
// Additional fnmsub pattern for PPC specific ISD opcode
def : Pat<(PPCfnmsub f128:$A, f128:$B, f128:$C),
          (XSNMSUBQP $C, $A, $B)>;
def : Pat<(fneg (PPCfnmsub f128:$A, f128:$B, f128:$C)),
          (XSMSUBQP $C, $A, $B)>;
def : Pat<(PPCfnmsub f128:$A, f128:$B, (fneg f128:$C)),
          (XSNMADDQP $C, $A, $B)>;

def : Pat<(f128 (any_sint_to_fp i64:$src)),
          (f128 (XSCVSDQP (COPY_TO_REGCLASS $src, VFRC)))>;
def : Pat<(f128 (any_sint_to_fp (i64 (PPCmfvsr f64:$src)))),
          (f128 (XSCVSDQP $src))>;
def : Pat<(f128 (any_sint_to_fp (i32 (PPCmfvsr f64:$src)))),
          (f128 (XSCVSDQP (VEXTSW2Ds $src)))>;
def : Pat<(f128 (any_uint_to_fp i64:$src)),
          (f128 (XSCVUDQP (COPY_TO_REGCLASS $src, VFRC)))>;
def : Pat<(f128 (any_uint_to_fp (i64 (PPCmfvsr f64:$src)))),
          (f128 (XSCVUDQP $src))>;

// Convert (Un)Signed Word -> QP.
def : Pat<(f128 (any_sint_to_fp i32:$src)),
          (f128 (XSCVSDQP (MTVSRWA $src)))>;
def : Pat<(f128 (any_sint_to_fp (i32 (load ForceXForm:$src)))),
          (f128 (XSCVSDQP (LIWAX ForceXForm:$src)))>;
def : Pat<(f128 (any_uint_to_fp i32:$src)),
          (f128 (XSCVUDQP (MTVSRWZ $src)))>;
def : Pat<(f128 (any_uint_to_fp (i32 (load ForceXForm:$src)))),
          (f128 (XSCVUDQP (LIWZX ForceXForm:$src)))>;

// Pattern for matching Vector HP -> Vector SP intrinsic. Defined as a
// separate pattern so that it can convert the input register class from
// VRRC(v8i16) to VSRC.
def : Pat<(v4f32 (int_ppc_vsx_xvcvhpsp v8i16:$A)),
          (v4f32 (XVCVHPSP (COPY_TO_REGCLASS $A, VSRC)))>;

// Use current rounding mode
def : Pat<(f128 (any_fnearbyint f128:$vB)), (f128 (XSRQPI 0, $vB, 3))>;
// Round to nearest, ties away from zero
def : Pat<(f128 (any_fround f128:$vB)), (f128 (XSRQPI 0, $vB, 0))>;
// Round towards Zero
def : Pat<(f128 (any_ftrunc f128:$vB)), (f128 (XSRQPI 1, $vB, 1))>;
// Round towards +Inf
def : Pat<(f128 (any_fceil f128:$vB)), (f128 (XSRQPI 1, $vB, 2))>;
// Round towards -Inf
def : Pat<(f128 (any_ffloor f128:$vB)), (f128 (XSRQPI 1, $vB, 3))>;
// Use current rounding mode, [with Inexact]
def : Pat<(f128 (any_frint f128:$vB)), (f128 (XSRQPIX 0, $vB, 3))>;

def : Pat<(f128 (int_ppc_scalar_insert_exp_qp f128:$vA, i64:$vB)),
          (f128 (XSIEXPQP $vA, (MTVSRD $vB)))>;

def : Pat<(i64 (int_ppc_scalar_extract_expq  f128:$vA)),
          (i64 (MFVSRD (EXTRACT_SUBREG
                          (v2i64 (XSXEXPQP $vA)), sub_64)))>;

// Extra patterns expanding to vector Extract Word/Insert Word
def : Pat<(v4i32 (int_ppc_vsx_xxinsertw v4i32:$A, v2i64:$B, imm:$IMM)),
          (v4i32 (XXINSERTW $A, $B, imm:$IMM))>;
def : Pat<(v2i64 (int_ppc_vsx_xxextractuw v2i64:$A, imm:$IMM)),
          (v2i64 (COPY_TO_REGCLASS (XXEXTRACTUW $A, imm:$IMM), VSRC))>;

// Vector Reverse
def : Pat<(v8i16 (bswap v8i16 :$A)),
          (v8i16 (COPY_TO_REGCLASS (XXBRH (COPY_TO_REGCLASS $A, VSRC)), VRRC))>;
def : Pat<(v1i128 (bswap v1i128 :$A)),
          (v1i128 (COPY_TO_REGCLASS (XXBRQ (COPY_TO_REGCLASS $A, VSRC)), VRRC))>;

// D-Form Load/Store
foreach Ty = [v4i32, v4f32, v2i64, v2f64] in {
  def : Pat<(Ty (load DQForm:$src)), (LXV memrix16:$src)>;
  def : Pat<(Ty (load XForm:$src)), (LXVX XForm:$src)>;
  def : Pat<(store Ty:$rS, DQForm:$dst), (STXV $rS, memrix16:$dst)>;
  def : Pat<(store Ty:$rS, XForm:$dst), (STXVX $rS, XForm:$dst)>;
}

def : Pat<(f128 (load DQForm:$src)),
          (COPY_TO_REGCLASS (LXV memrix16:$src), VRRC)>;
def : Pat<(f128 (load XForm:$src)),
          (COPY_TO_REGCLASS (LXVX XForm:$src), VRRC)>;
def : Pat<(v4i32 (int_ppc_vsx_lxvw4x DQForm:$src)), (LXV memrix16:$src)>;
def : Pat<(v2f64 (int_ppc_vsx_lxvd2x DQForm:$src)), (LXV memrix16:$src)>;
def : Pat<(v4i32 (int_ppc_vsx_lxvw4x XForm:$src)), (LXVX XForm:$src)>;
def : Pat<(v2f64 (int_ppc_vsx_lxvd2x XForm:$src)), (LXVX XForm:$src)>;

def : Pat<(store f128:$rS, DQForm:$dst),
          (STXV (COPY_TO_REGCLASS $rS, VSRC), memrix16:$dst)>;
def : Pat<(store f128:$rS, XForm:$dst),
          (STXVX (COPY_TO_REGCLASS $rS, VSRC), XForm:$dst)>;
def : Pat<(int_ppc_vsx_stxvw4x v4i32:$rS, DQForm:$dst),
          (STXV $rS, memrix16:$dst)>;
def : Pat<(int_ppc_vsx_stxvd2x v2f64:$rS, DQForm:$dst),
          (STXV $rS, memrix16:$dst)>;
def : Pat<(int_ppc_vsx_stxvw4x v4i32:$rS, XForm:$dst),
          (STXVX $rS, XForm:$dst)>;
def : Pat<(int_ppc_vsx_stxvd2x v2f64:$rS, XForm:$dst),
          (STXVX $rS, XForm:$dst)>;

// Build vectors from i8 loads
defm : ScalToVecWPermute<v8i16, ScalarLoads.ZELi8,
                         (VSPLTHs 3, (LXSIBZX ForceXForm:$src)),
                         (SUBREG_TO_REG (i64 1), (LXSIBZX ForceXForm:$src), sub_64)>;
defm : ScalToVecWPermute<v4i32, ScalarLoads.ZELi8,
                         (XXSPLTWs (LXSIBZX ForceXForm:$src), 1),
                         (SUBREG_TO_REG (i64 1), (LXSIBZX ForceXForm:$src), sub_64)>;
defm : ScalToVecWPermute<v2i64, ScalarLoads.ZELi8i64,
                         (XXPERMDIs (LXSIBZX ForceXForm:$src), 0),
                         (SUBREG_TO_REG (i64 1), (LXSIBZX ForceXForm:$src), sub_64)>;
defm : ScalToVecWPermute<
  v4i32, ScalarLoads.SELi8,
  (XXSPLTWs (VEXTSB2Ws (LXSIBZX ForceXForm:$src)), 1),
  (SUBREG_TO_REG (i64 1), (VEXTSB2Ws (LXSIBZX ForceXForm:$src)), sub_64)>;
defm : ScalToVecWPermute<
  v2i64, ScalarLoads.SELi8i64,
  (XXPERMDIs (VEXTSB2Ds (LXSIBZX ForceXForm:$src)), 0),
  (SUBREG_TO_REG (i64 1), (VEXTSB2Ds (LXSIBZX ForceXForm:$src)), sub_64)>;

// Build vectors from i16 loads
defm : ScalToVecWPermute<
  v4i32, ScalarLoads.ZELi16,
  (XXSPLTWs (LXSIHZX ForceXForm:$src), 1),
  (SUBREG_TO_REG (i64 1), (LXSIHZX ForceXForm:$src), sub_64)>;
defm : ScalToVecWPermute<
  v2i64, ScalarLoads.ZELi16i64,
  (XXPERMDIs (LXSIHZX ForceXForm:$src), 0),
  (SUBREG_TO_REG (i64 1), (LXSIHZX ForceXForm:$src), sub_64)>;
defm : ScalToVecWPermute<
  v4i32, ScalarLoads.SELi16,
  (XXSPLTWs (VEXTSH2Ws (LXSIHZX ForceXForm:$src)), 1),
  (SUBREG_TO_REG (i64 1), (VEXTSH2Ws (LXSIHZX ForceXForm:$src)), sub_64)>;
defm : ScalToVecWPermute<
  v2i64, ScalarLoads.SELi16i64,
  (XXPERMDIs (VEXTSH2Ds (LXSIHZX ForceXForm:$src)), 0),
  (SUBREG_TO_REG (i64 1), (VEXTSH2Ds (LXSIHZX ForceXForm:$src)), sub_64)>;

// Load/convert and convert/store patterns for f16.
def : Pat<(f64 (extloadf16 ForceXForm:$src)),
          (f64 (XSCVHPDP (LXSIHZX ForceXForm:$src)))>;
def : Pat<(truncstoref16 f64:$src, ForceXForm:$dst),
          (STXSIHX (XSCVDPHP $src), ForceXForm:$dst)>;
def : Pat<(f32 (extloadf16 ForceXForm:$src)),
          (f32 (COPY_TO_REGCLASS (XSCVHPDP (LXSIHZX ForceXForm:$src)), VSSRC))>;
def : Pat<(truncstoref16 f32:$src, ForceXForm:$dst),
          (STXSIHX (XSCVDPHP (COPY_TO_REGCLASS $src, VSFRC)), ForceXForm:$dst)>;
def : Pat<(f64 (f16_to_fp i32:$A)),
          (f64 (XSCVHPDP (MTVSRWZ $A)))>;
def : Pat<(f32 (f16_to_fp i32:$A)),
          (f32 (COPY_TO_REGCLASS (XSCVHPDP (MTVSRWZ $A)), VSSRC))>;
def : Pat<(i32 (fp_to_f16 f32:$A)),
          (i32 (MFVSRWZ (XSCVDPHP (COPY_TO_REGCLASS $A, VSFRC))))>;
def : Pat<(i32 (fp_to_f16 f64:$A)), (i32 (MFVSRWZ (XSCVDPHP $A)))>;

// Vector sign extensions
def : Pat<(f64 (PPCVexts f64:$A, 1)),
          (f64 (COPY_TO_REGCLASS (VEXTSB2Ds $A), VSFRC))>;
def : Pat<(f64 (PPCVexts f64:$A, 2)),
          (f64 (COPY_TO_REGCLASS (VEXTSH2Ds $A), VSFRC))>;

def : Pat<(f64 (extloadf32 DSForm:$src)),
          (COPY_TO_REGCLASS (DFLOADf32 DSForm:$src), VSFRC)>;
def : Pat<(f32 (fpround (f64 (extloadf32 DSForm:$src)))),
          (f32 (DFLOADf32 DSForm:$src))>;

def : Pat<(v4f32 (PPCldvsxlh XForm:$src)),
          (SUBREG_TO_REG (i64 1), (XFLOADf64 XForm:$src), sub_64)>;
def : Pat<(v4f32 (PPCldvsxlh DSForm:$src)),
          (SUBREG_TO_REG (i64 1), (DFLOADf64 DSForm:$src), sub_64)>;

// Convert (Un)Signed DWord in memory -> QP
def : Pat<(f128 (sint_to_fp (i64 (load XForm:$src)))),
          (f128 (XSCVSDQP (LXSDX XForm:$src)))>;
def : Pat<(f128 (sint_to_fp (i64 (load DSForm:$src)))),
          (f128 (XSCVSDQP (LXSD DSForm:$src)))>;
def : Pat<(f128 (uint_to_fp (i64 (load XForm:$src)))),
          (f128 (XSCVUDQP (LXSDX XForm:$src)))>;
def : Pat<(f128 (uint_to_fp (i64 (load DSForm:$src)))),
          (f128 (XSCVUDQP (LXSD DSForm:$src)))>;

// Convert Unsigned HWord in memory -> QP
def : Pat<(f128 (uint_to_fp ScalarLoads.ZELi16)),
          (f128 (XSCVUDQP (LXSIHZX XForm:$src)))>;

// Convert Unsigned Byte in memory -> QP
def : Pat<(f128 (uint_to_fp ScalarLoads.ZELi8)),
          (f128 (XSCVUDQP (LXSIBZX ForceXForm:$src)))>;

// Truncate & Convert QP -> (Un)Signed (D)Word.
def : Pat<(i64 (any_fp_to_sint f128:$src)), (i64 (MFVRD (XSCVQPSDZ $src)))>;
def : Pat<(i64 (any_fp_to_uint f128:$src)), (i64 (MFVRD (XSCVQPUDZ $src)))>;
def : Pat<(i32 (any_fp_to_sint f128:$src)),
          (i32 (MFVSRWZ (COPY_TO_REGCLASS (XSCVQPSWZ $src), VFRC)))>;
def : Pat<(i32 (any_fp_to_uint f128:$src)),
          (i32 (MFVSRWZ (COPY_TO_REGCLASS (XSCVQPUWZ $src), VFRC)))>;

// Instructions for store(fptosi).
def : Pat<(PPCstore_scal_int_from_vsr f64:$src, DSForm:$dst, 8),
          (STXSD $src, DSForm:$dst)>;
def : Pat<(PPCstore_scal_int_from_vsr f64:$src, ForceXForm:$dst, 2),
          (STXSIHX $src, ForceXForm:$dst)>;
def : Pat<(PPCstore_scal_int_from_vsr f64:$src, ForceXForm:$dst, 1),
          (STXSIBX $src, ForceXForm:$dst)>;

def : Pat<(PPCstore_scal_int_from_vsr f128:$src, DSForm:$dst, 8),
          (STXSD (COPY_TO_REGCLASS $src, VFRC), DSForm:$dst)>;
def : Pat<(PPCstore_scal_int_from_vsr f128:$src, ForceXForm:$dst, 2),
          (STXSIHX (COPY_TO_REGCLASS $src, VSFRC), ForceXForm:$dst)>;
def : Pat<(PPCstore_scal_int_from_vsr f128:$src, ForceXForm:$dst, 1),
          (STXSIBX (COPY_TO_REGCLASS $src, VSFRC), ForceXForm:$dst)>;

// Round & Convert QP -> DP/SP
def : Pat<(f64 (any_fpround f128:$src)), (f64 (XSCVQPDP $src))>;
def : Pat<(f32 (any_fpround f128:$src)), (f32 (XSRSP (XSCVQPDPO $src)))>;

// Convert SP -> QP
def : Pat<(f128 (any_fpextend f32:$src)),
          (f128 (XSCVDPQP (COPY_TO_REGCLASS $src, VFRC)))>;

def : Pat<(f32 (PPCxsmaxc f32:$XA, f32:$XB)),
          (f32 (COPY_TO_REGCLASS (XSMAXCDP (COPY_TO_REGCLASS $XA, VSSRC),
                                           (COPY_TO_REGCLASS $XB, VSSRC)),
                                 VSSRC))>;
def : Pat<(f32 (PPCxsminc f32:$XA, f32:$XB)),
          (f32 (COPY_TO_REGCLASS (XSMINCDP (COPY_TO_REGCLASS $XA, VSSRC),
                                           (COPY_TO_REGCLASS $XB, VSSRC)),
                                 VSSRC))>;

// Endianness-neutral patterns for const splats with ISA 3.0 instructions.
defm : ScalToVecWPermute<v4i32, (i32 i32:$A), (MTVSRWS $A),
                         (SUBREG_TO_REG (i64 1), (MTVSRWZ $A), sub_64)>;
def : Pat<(v4i32 (build_vector i32:$A, i32:$A, i32:$A, i32:$A)),
          (v4i32 (MTVSRWS $A))>;
def : Pat<(v16i8 (build_vector immNonAllOneAnyExt8:$A, immNonAllOneAnyExt8:$A,
                               immNonAllOneAnyExt8:$A, immNonAllOneAnyExt8:$A,
                               immNonAllOneAnyExt8:$A, immNonAllOneAnyExt8:$A,
                               immNonAllOneAnyExt8:$A, immNonAllOneAnyExt8:$A,
                               immNonAllOneAnyExt8:$A, immNonAllOneAnyExt8:$A,
                               immNonAllOneAnyExt8:$A, immNonAllOneAnyExt8:$A,
                               immNonAllOneAnyExt8:$A, immNonAllOneAnyExt8:$A,
                               immNonAllOneAnyExt8:$A, immNonAllOneAnyExt8:$A)),
          (v16i8 (COPY_TO_REGCLASS (XXSPLTIB imm:$A), VSRC))>;
defm : ScalToVecWPermute<
  v4i32, FltToIntLoad.A,
  (XVCVSPSXWS (LXVWSX ForceXForm:$A)),
  (XVCVSPSXWS (SUBREG_TO_REG (i64 1), (LIWZX ForceXForm:$A), sub_64))>;
defm : ScalToVecWPermute<
  v4i32, FltToUIntLoad.A,
  (XVCVSPUXWS (LXVWSX ForceXForm:$A)),
  (XVCVSPUXWS (SUBREG_TO_REG (i64 1), (LIWZX ForceXForm:$A), sub_64))>;
defm : ScalToVecWPermute<
  v4i32, DblToIntLoadP9.A,
  (XXSPLTW (SUBREG_TO_REG (i64 1), (XSCVDPSXWS (DFLOADf64 DSForm:$A)), sub_64), 1),
  (SUBREG_TO_REG (i64 1), (XSCVDPSXWS (DFLOADf64 DSForm:$A)), sub_64)>;
defm : ScalToVecWPermute<
  v4i32, DblToUIntLoadP9.A,
  (XXSPLTW (SUBREG_TO_REG (i64 1), (XSCVDPUXWS (DFLOADf64 DSForm:$A)), sub_64), 1),
  (SUBREG_TO_REG (i64 1), (XSCVDPUXWS (DFLOADf64 DSForm:$A)), sub_64)>;
defm : ScalToVecWPermute<
  v2i64, FltToLongLoadP9.A,
  (XXPERMDIs (XSCVDPSXDS (COPY_TO_REGCLASS (DFLOADf32 DSForm:$A), VSFRC)), 0),
  (SUBREG_TO_REG
     (i64 1),
     (XSCVDPSXDS (COPY_TO_REGCLASS (DFLOADf32 DSForm:$A), VSFRC)), sub_64)>;
defm : ScalToVecWPermute<
  v2i64, FltToULongLoadP9.A,
  (XXPERMDIs (XSCVDPUXDS (COPY_TO_REGCLASS (DFLOADf32 DSForm:$A), VSFRC)), 0),
  (SUBREG_TO_REG
     (i64 1),
     (XSCVDPUXDS (COPY_TO_REGCLASS (DFLOADf32 DSForm:$A), VSFRC)), sub_64)>;
def : Pat<(v4f32 (PPCldsplat ForceXForm:$A)),
          (v4f32 (LXVWSX ForceXForm:$A))>;
def : Pat<(v4i32 (PPCldsplat ForceXForm:$A)),
          (v4i32 (LXVWSX ForceXForm:$A))>;
def : Pat<(v8i16 (PPCldsplat ForceXForm:$A)),
          (v8i16 (VSPLTHs 3, (LXSIHZX ForceXForm:$A)))>;
def : Pat<(v16i8 (PPCldsplat ForceXForm:$A)),
          (v16i8 (VSPLTBs 7, (LXSIBZX ForceXForm:$A)))>;
def : Pat<(v2f64 (PPCxxperm v2f64:$XT, v2f64:$XB, v4i32:$C)),
          (XXPERM v2f64:$XT, v2f64:$XB, v4i32:$C)>;
} // HasVSX, HasP9Vector

// Any Power9 VSX subtarget with equivalent length but better Power10 VSX
// patterns.
// Two identical blocks are required due to the slightly different predicates:
// One without P10 instructions, the other is BigEndian only with P10 instructions.
let Predicates = [HasVSX, HasP9Vector, NoP10Vector] in {
// Little endian Power10 subtargets produce a shorter pattern but require a
// COPY_TO_REGCLASS. The COPY_TO_REGCLASS makes it appear to need two instructions 
// to perform the operation, when only one instruction is produced in practice.
// The NoP10Vector predicate excludes these patterns from Power10 VSX subtargets.
defm : ScalToVecWPermute<
  v16i8, ScalarLoads.Li8,
  (VSPLTBs 7, (LXSIBZX ForceXForm:$src)),
  (SUBREG_TO_REG (i64 1), (LXSIBZX ForceXForm:$src), sub_64)>;
// Build vectors from i16 loads
defm : ScalToVecWPermute<
  v8i16, ScalarLoads.Li16,
  (VSPLTHs 3, (LXSIHZX ForceXForm:$src)),
  (SUBREG_TO_REG (i64 1), (LXSIHZX ForceXForm:$src), sub_64)>;
} // HasVSX, HasP9Vector, NoP10Vector

// Any big endian Power9 VSX subtarget
let Predicates = [HasVSX, HasP9Vector, IsBigEndian] in {
// Power10 VSX subtargets produce a shorter pattern for little endian targets
// but this is still the best pattern for Power9 and Power10 VSX big endian
// Build vectors from i8 loads
defm : ScalToVecWPermute<
  v16i8, ScalarLoads.Li8,
  (VSPLTBs 7, (LXSIBZX ForceXForm:$src)),
  (SUBREG_TO_REG (i64 1), (LXSIBZX ForceXForm:$src), sub_64)>;
// Build vectors from i16 loads
defm : ScalToVecWPermute<
  v8i16, ScalarLoads.Li16,
  (VSPLTHs 3, (LXSIHZX ForceXForm:$src)),
  (SUBREG_TO_REG (i64 1), (LXSIHZX ForceXForm:$src), sub_64)>;

def : Pat<(f32 (PPCfcfidus (f64 (PPCmtvsrz (i32 (extractelt v4i32:$A, 0)))))),
          (f32 (XSCVUXDSP (XXEXTRACTUW $A, 0)))>;
def : Pat<(f32 (PPCfcfidus (f64 (PPCmtvsrz (i32 (extractelt v4i32:$A, 1)))))),
          (f32 (XSCVUXDSP (XXEXTRACTUW $A, 4)))>;
def : Pat<(f32 (PPCfcfidus (f64 (PPCmtvsrz (i32 (extractelt v4i32:$A, 2)))))),
          (f32 (XSCVUXDSP (XXEXTRACTUW $A, 8)))>;
def : Pat<(f32 (PPCfcfidus (f64 (PPCmtvsrz (i32 (extractelt v4i32:$A, 3)))))),
          (f32 (XSCVUXDSP (XXEXTRACTUW $A, 12)))>;
def : Pat<(f64 (PPCfcfidu (f64 (PPCmtvsrz (i32 (extractelt v4i32:$A, 0)))))),
          (f64 (XSCVUXDDP (XXEXTRACTUW $A, 0)))>;
def : Pat<(f64 (PPCfcfidu (f64 (PPCmtvsrz (i32 (extractelt v4i32:$A, 1)))))),
          (f64 (XSCVUXDDP (XXEXTRACTUW $A, 4)))>;
def : Pat<(f64 (PPCfcfidu (f64 (PPCmtvsrz (i32 (extractelt v4i32:$A, 2)))))),
          (f64 (XSCVUXDDP (XXEXTRACTUW $A, 8)))>;
def : Pat<(f64 (PPCfcfidu (f64 (PPCmtvsrz (i32 (extractelt v4i32:$A, 3)))))),
          (f64 (XSCVUXDDP (XXEXTRACTUW $A, 12)))>;
def : Pat<(v4i32 (insertelt v4i32:$A, i32:$B, 0)),
          (v4i32 (XXINSERTW v4i32:$A, AlignValues.I32_TO_BE_WORD1, 0))>;
def : Pat<(v4i32 (insertelt v4i32:$A, DblToInt.B, 0)),
          (v4i32 (XXINSERTW v4i32:$A,
                            (SUBREG_TO_REG (i64 1),
                                           (XSCVDPSXWS f64:$B), sub_64),
                            0))>;
def : Pat<(v4i32 (insertelt v4i32:$A, DblToUInt.B, 0)),
          (v4i32 (XXINSERTW v4i32:$A,
                            (SUBREG_TO_REG (i64 1),
                                           (XSCVDPUXWS f64:$B), sub_64),
                            0))>;
def : Pat<(v4i32 (insertelt v4i32:$A, i32:$B, 1)),
          (v4i32 (XXINSERTW v4i32:$A, AlignValues.I32_TO_BE_WORD1, 4))>;
def : Pat<(v4i32 (insertelt v4i32:$A, DblToInt.B, 1)),
          (v4i32 (XXINSERTW v4i32:$A,
                            (SUBREG_TO_REG (i64 1),
                                           (XSCVDPSXWS f64:$B), sub_64),
                            4))>;
def : Pat<(v4i32 (insertelt v4i32:$A, DblToUInt.B, 1)),
          (v4i32 (XXINSERTW v4i32:$A,
                            (SUBREG_TO_REG (i64 1),
                                           (XSCVDPUXWS f64:$B), sub_64),
                            4))>;
def : Pat<(v4i32 (insertelt v4i32:$A, i32:$B, 2)),
          (v4i32 (XXINSERTW v4i32:$A, AlignValues.I32_TO_BE_WORD1, 8))>;
def : Pat<(v4i32 (insertelt v4i32:$A, DblToInt.B, 2)),
          (v4i32 (XXINSERTW v4i32:$A,
                            (SUBREG_TO_REG (i64 1),
                                           (XSCVDPSXWS f64:$B), sub_64),
                            8))>;
def : Pat<(v4i32 (insertelt v4i32:$A, DblToUInt.B, 2)),
          (v4i32 (XXINSERTW v4i32:$A,
                            (SUBREG_TO_REG (i64 1),
                                           (XSCVDPUXWS f64:$B), sub_64),
                            8))>;
def : Pat<(v4i32 (insertelt v4i32:$A, i32:$B, 3)),
          (v4i32 (XXINSERTW v4i32:$A, AlignValues.I32_TO_BE_WORD1, 12))>;
def : Pat<(v4i32 (insertelt v4i32:$A, DblToInt.B, 3)),
          (v4i32 (XXINSERTW v4i32:$A,
                            (SUBREG_TO_REG (i64 1),
                                           (XSCVDPSXWS f64:$B), sub_64),
                            12))>;
def : Pat<(v4i32 (insertelt v4i32:$A, DblToUInt.B, 3)),
          (v4i32 (XXINSERTW v4i32:$A,
                            (SUBREG_TO_REG (i64 1),
                                           (XSCVDPUXWS f64:$B), sub_64),
                            12))>;
def : Pat<(v4f32 (insertelt v4f32:$A, f32:$B, 0)),
          (v4f32 (XXINSERTW v4f32:$A, AlignValues.F32_TO_BE_WORD1, 0))>;
def : Pat<(v4f32 (insertelt v4f32:$A, f32:$B, 1)),
          (v4f32 (XXINSERTW v4f32:$A, AlignValues.F32_TO_BE_WORD1, 4))>;
def : Pat<(v4f32 (insertelt v4f32:$A, f32:$B, 2)),
          (v4f32 (XXINSERTW v4f32:$A, AlignValues.F32_TO_BE_WORD1, 8))>;
def : Pat<(v4f32 (insertelt v4f32:$A, f32:$B, 3)),
          (v4f32 (XXINSERTW v4f32:$A, AlignValues.F32_TO_BE_WORD1, 12))>;

def : Pat<(v4f32 (insertelt v4f32:$A, (f32 (fpround f64:$B)), 0)),
          (v4f32 (XXINSERTW v4f32:$A,
                  (SUBREG_TO_REG (i64 1), (XSCVDPSP f64:$B), sub_64), 0))>;
def : Pat<(v4f32 (insertelt v4f32:$A, (f32 (fpround f64:$B)), 1)),
          (v4f32 (XXINSERTW v4f32:$A,
                  (SUBREG_TO_REG (i64 1), (XSCVDPSP f64:$B), sub_64), 4))>;
def : Pat<(v4f32 (insertelt v4f32:$A, (f32 (fpround f64:$B)), 2)),
          (v4f32 (XXINSERTW v4f32:$A,
                  (SUBREG_TO_REG (i64 1), (XSCVDPSP f64:$B), sub_64), 8))>;
def : Pat<(v4f32 (insertelt v4f32:$A, (f32 (fpround f64:$B)), 3)),
          (v4f32 (XXINSERTW v4f32:$A,
                  (SUBREG_TO_REG (i64 1), (XSCVDPSP f64:$B), sub_64), 12))>;

// Scalar stores of i8
def : Pat<(truncstorei8 (i32 (vector_extract v16i8:$S, 0)), ForceXForm:$dst),
          (STXSIBXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 9)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei8 (i32 (vector_extract v16i8:$S, 1)), ForceXForm:$dst),
          (STXSIBXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 10)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei8 (i32 (vector_extract v16i8:$S, 2)), ForceXForm:$dst),
          (STXSIBXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 11)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei8 (i32 (vector_extract v16i8:$S, 3)), ForceXForm:$dst),
          (STXSIBXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 12)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei8 (i32 (vector_extract v16i8:$S, 4)), ForceXForm:$dst),
          (STXSIBXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 13)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei8 (i32 (vector_extract v16i8:$S, 5)), ForceXForm:$dst),
          (STXSIBXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 14)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei8 (i32 (vector_extract v16i8:$S, 6)), ForceXForm:$dst),
          (STXSIBXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 15)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei8 (i32 (vector_extract v16i8:$S, 7)), ForceXForm:$dst),
          (STXSIBXv (COPY_TO_REGCLASS $S, VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei8 (i32 (vector_extract v16i8:$S, 8)), ForceXForm:$dst),
          (STXSIBXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 1)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei8 (i32 (vector_extract v16i8:$S, 9)), ForceXForm:$dst),
          (STXSIBXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 2)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei8 (i32 (vector_extract v16i8:$S, 10)), ForceXForm:$dst),
          (STXSIBXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 3)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei8 (i32 (vector_extract v16i8:$S, 11)), ForceXForm:$dst),
          (STXSIBXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 4)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei8 (i32 (vector_extract v16i8:$S, 12)), ForceXForm:$dst),
          (STXSIBXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 5)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei8 (i32 (vector_extract v16i8:$S, 13)), ForceXForm:$dst),
          (STXSIBXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 6)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei8 (i32 (vector_extract v16i8:$S, 14)), ForceXForm:$dst),
          (STXSIBXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 7)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei8 (i32 (vector_extract v16i8:$S, 15)), ForceXForm:$dst),
          (STXSIBXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 8)), VSRC), ForceXForm:$dst)>;

// Scalar stores of i16
def : Pat<(truncstorei16 (i32 (vector_extract v8i16:$S, 0)), ForceXForm:$dst),
          (STXSIHXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 10)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei16 (i32 (vector_extract v8i16:$S, 1)), ForceXForm:$dst),
          (STXSIHXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 12)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei16 (i32 (vector_extract v8i16:$S, 2)), ForceXForm:$dst),
          (STXSIHXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 14)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei16 (i32 (vector_extract v8i16:$S, 3)), ForceXForm:$dst),
          (STXSIHXv (COPY_TO_REGCLASS $S, VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei16 (i32 (vector_extract v8i16:$S, 4)), ForceXForm:$dst),
          (STXSIHXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 2)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei16 (i32 (vector_extract v8i16:$S, 5)), ForceXForm:$dst),
          (STXSIHXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 4)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei16 (i32 (vector_extract v8i16:$S, 6)), ForceXForm:$dst),
          (STXSIHXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 6)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei16 (i32 (vector_extract v8i16:$S, 7)), ForceXForm:$dst),
          (STXSIHXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 8)), VSRC), ForceXForm:$dst)>;
} // HasVSX, HasP9Vector, IsBigEndian

// Big endian 64Bit Power9 subtarget.
let Predicates = [HasVSX, HasP9Vector, IsBigEndian, IsPPC64] in {
def : Pat<(v2i64 (scalar_to_vector (i64 (load DSForm:$src)))),
          (v2i64 (SUBREG_TO_REG (i64 1), (DFLOADf64 DSForm:$src), sub_64))>;
def : Pat<(v2i64 (scalar_to_vector (i64 (load XForm:$src)))),
          (v2i64 (SUBREG_TO_REG (i64 1), (XFLOADf64 XForm:$src), sub_64))>;

def : Pat<(v2f64 (scalar_to_vector (f64 (load DSForm:$src)))),
          (v2f64 (SUBREG_TO_REG (i64 1), (DFLOADf64 DSForm:$src), sub_64))>;
def : Pat<(v2f64 (scalar_to_vector (f64 (load XForm:$src)))),
          (v2f64 (SUBREG_TO_REG (i64 1), (XFLOADf64 XForm:$src), sub_64))>;
def : Pat<(store (i64 (extractelt v2i64:$A, 1)), XForm:$src),
          (XFSTOREf64 (EXTRACT_SUBREG (XXPERMDI $A, $A, 2),
                       sub_64), XForm:$src)>;
def : Pat<(store (f64 (extractelt v2f64:$A, 1)), XForm:$src),
          (XFSTOREf64 (EXTRACT_SUBREG (XXPERMDI $A, $A, 2),
                       sub_64), XForm:$src)>;
def : Pat<(store (i64 (extractelt v2i64:$A, 0)), XForm:$src),
          (XFSTOREf64 (EXTRACT_SUBREG $A, sub_64), XForm:$src)>;
def : Pat<(store (f64 (extractelt v2f64:$A, 0)), XForm:$src),
          (XFSTOREf64 (EXTRACT_SUBREG $A, sub_64), XForm:$src)>;
def : Pat<(store (i64 (extractelt v2i64:$A, 1)), DSForm:$src),
          (DFSTOREf64 (EXTRACT_SUBREG (XXPERMDI $A, $A, 2),
                       sub_64), DSForm:$src)>;
def : Pat<(store (f64 (extractelt v2f64:$A, 1)), DSForm:$src),
          (DFSTOREf64 (EXTRACT_SUBREG (XXPERMDI $A, $A, 2),
                       sub_64), DSForm:$src)>;
def : Pat<(store (i64 (extractelt v2i64:$A, 0)), DSForm:$src),
          (DFSTOREf64 (EXTRACT_SUBREG $A, sub_64), DSForm:$src)>;
def : Pat<(store (f64 (extractelt v2f64:$A, 0)), DSForm:$src),
          (DFSTOREf64 (EXTRACT_SUBREG $A, sub_64), DSForm:$src)>;

// (Un)Signed DWord vector extract -> QP
def : Pat<(f128 (sint_to_fp (i64 (extractelt v2i64:$src, 0)))),
          (f128 (XSCVSDQP (COPY_TO_REGCLASS $src, VFRC)))>;
def : Pat<(f128 (sint_to_fp (i64 (extractelt v2i64:$src, 1)))),
          (f128 (XSCVSDQP
                  (EXTRACT_SUBREG (XXPERMDI $src, $src, 3), sub_64)))>;
def : Pat<(f128 (uint_to_fp (i64 (extractelt v2i64:$src, 0)))),
          (f128 (XSCVUDQP (COPY_TO_REGCLASS $src, VFRC)))>;
def : Pat<(f128 (uint_to_fp (i64 (extractelt v2i64:$src, 1)))),
          (f128 (XSCVUDQP
                  (EXTRACT_SUBREG (XXPERMDI $src, $src, 3), sub_64)))>;

// (Un)Signed Word vector extract -> QP
def : Pat<(f128 (sint_to_fp (i32 (extractelt v4i32:$src, 1)))),
          (f128 (XSCVSDQP (EXTRACT_SUBREG (VEXTSW2D $src), sub_64)))>;
foreach Idx = [0,2,3] in {
  def : Pat<(f128 (sint_to_fp (i32 (extractelt v4i32:$src, Idx)))),
            (f128 (XSCVSDQP (EXTRACT_SUBREG
                            (VEXTSW2D (VSPLTW Idx, $src)), sub_64)))>;
}
foreach Idx = 0-3 in {
  def : Pat<(f128 (uint_to_fp (i32 (extractelt v4i32:$src, Idx)))),
            (f128 (XSCVUDQP (XXEXTRACTUW $src, !shl(Idx, 2))))>;
}

// (Un)Signed HWord vector extract -> QP/DP/SP
foreach Idx = 0-7 in {
  def : Pat<(f128 (sint_to_fp
                    (i32 (sext_inreg
                           (vector_extract v8i16:$src, Idx), i16)))),
          (f128 (XSCVSDQP (EXTRACT_SUBREG
                            (VEXTSH2D (VEXTRACTUH !add(Idx, Idx), $src)),
                            sub_64)))>;
  // The SDAG adds the `and` since an `i16` is being extracted as an `i32`.
  def : Pat<(f128 (uint_to_fp
                    (and (i32 (vector_extract v8i16:$src, Idx)), 65535))),
            (f128 (XSCVUDQP (EXTRACT_SUBREG
                              (VEXTRACTUH !add(Idx, Idx), $src), sub_64)))>;
  def : Pat<(f32 (PPCfcfidus
                   (f64 (PPCmtvsrz (and (i32 (vector_extract v8i16:$src, Idx)),
                                        65535))))),
            (f32 (XSCVUXDSP (EXTRACT_SUBREG
                              (VEXTRACTUH !add(Idx, Idx), $src), sub_64)))>;
  def : Pat<(f32 (PPCfcfids
                   (f64 (PPCmtvsra
                          (i32 (sext_inreg (vector_extract v8i16:$src, Idx),
                               i16)))))),
          (f32 (XSCVSXDSP (EXTRACT_SUBREG
                            (VEXTSH2D (VEXTRACTUH !add(Idx, Idx), $src)),
                            sub_64)))>;
  def : Pat<(f64 (PPCfcfidu
                   (f64 (PPCmtvsrz
                          (and (i32 (vector_extract v8i16:$src, Idx)),
                               65535))))),
            (f64 (XSCVUXDDP (EXTRACT_SUBREG
                              (VEXTRACTUH !add(Idx, Idx), $src), sub_64)))>;
  def : Pat<(f64 (PPCfcfid
                   (f64 (PPCmtvsra
                          (i32 (sext_inreg (vector_extract v8i16:$src, Idx),
                               i16)))))),
          (f64 (XSCVSXDDP (EXTRACT_SUBREG
                            (VEXTSH2D (VEXTRACTUH !add(Idx, Idx), $src)),
                            sub_64)))>;
}

// (Un)Signed Byte vector extract -> QP
foreach Idx = 0-15 in {
  def : Pat<(f128 (sint_to_fp
                    (i32 (sext_inreg (vector_extract v16i8:$src, Idx),
                                     i8)))),
            (f128 (XSCVSDQP (EXTRACT_SUBREG
                              (VEXTSB2D (VEXTRACTUB Idx, $src)), sub_64)))>;
  def : Pat<(f128 (uint_to_fp
                    (and (i32 (vector_extract v16i8:$src, Idx)), 255))),
            (f128 (XSCVUDQP
                    (EXTRACT_SUBREG (VEXTRACTUB Idx, $src), sub_64)))>;

  def : Pat<(f32 (PPCfcfidus
                   (f64 (PPCmtvsrz
                          (and (i32 (vector_extract v16i8:$src, Idx)),
                               255))))),
            (f32 (XSCVUXDSP (EXTRACT_SUBREG
                              (VEXTRACTUB !add(Idx, Idx), $src), sub_64)))>;
  def : Pat<(f32 (PPCfcfids
                   (f64 (PPCmtvsra
                          (i32 (sext_inreg (vector_extract v16i8:$src, Idx),
                               i8)))))),
          (f32 (XSCVSXDSP (EXTRACT_SUBREG
                            (VEXTSH2D (VEXTRACTUB !add(Idx, Idx), $src)),
                            sub_64)))>;
  def : Pat<(f64 (PPCfcfidu
                   (f64 (PPCmtvsrz
                          (and (i32 (vector_extract v16i8:$src, Idx)),
                          255))))),
            (f64 (XSCVUXDDP (EXTRACT_SUBREG
                              (VEXTRACTUB !add(Idx, Idx), $src), sub_64)))>;
  def : Pat<(f64 (PPCfcfid
                   (f64 (PPCmtvsra
                          (i32 (sext_inreg (vector_extract v16i8:$src, Idx),
                               i8)))))),
          (f64 (XSCVSXDDP (EXTRACT_SUBREG
                            (VEXTSH2D (VEXTRACTUB !add(Idx, Idx), $src)),
                            sub_64)))>;
}

// Unsiged int in vsx register -> QP
def : Pat<(f128 (uint_to_fp (i32 (PPCmfvsr f64:$src)))),
          (f128 (XSCVUDQP
                  (XXEXTRACTUW (SUBREG_TO_REG (i64 1), $src, sub_64), 4)))>;
} // HasVSX, HasP9Vector, IsBigEndian, IsPPC64

// Little endian Power9 subtarget.
let Predicates = [HasVSX, HasP9Vector, IsLittleEndian] in {
def : Pat<(f32 (PPCfcfidus (f64 (PPCmtvsrz (i32 (extractelt v4i32:$A, 0)))))),
          (f32 (XSCVUXDSP (XXEXTRACTUW $A, 12)))>;
def : Pat<(f32 (PPCfcfidus (f64 (PPCmtvsrz (i32 (extractelt v4i32:$A, 1)))))),
          (f32 (XSCVUXDSP (XXEXTRACTUW $A, 8)))>;
def : Pat<(f32 (PPCfcfidus (f64 (PPCmtvsrz (i32 (extractelt v4i32:$A, 2)))))),
          (f32 (XSCVUXDSP (XXEXTRACTUW $A, 4)))>;
def : Pat<(f32 (PPCfcfidus (f64 (PPCmtvsrz (i32 (extractelt v4i32:$A, 3)))))),
          (f32 (XSCVUXDSP (XXEXTRACTUW $A, 0)))>;
def : Pat<(f64 (PPCfcfidu (f64 (PPCmtvsrz (i32 (extractelt v4i32:$A, 0)))))),
          (f64 (XSCVUXDDP (XXEXTRACTUW $A, 12)))>;
def : Pat<(f64 (PPCfcfidu (f64 (PPCmtvsrz (i32 (extractelt v4i32:$A, 1)))))),
          (f64 (XSCVUXDDP (XXEXTRACTUW $A, 8)))>;
def : Pat<(f64 (PPCfcfidu (f64 (PPCmtvsrz (i32 (extractelt v4i32:$A, 2)))))),
          (f64 (XSCVUXDDP (XXEXTRACTUW $A, 4)))>;
def : Pat<(f64 (PPCfcfidu (f64 (PPCmtvsrz (i32 (extractelt v4i32:$A, 3)))))),
          (f64 (XSCVUXDDP (XXEXTRACTUW $A, 0)))>;
def : Pat<(v4i32 (insertelt v4i32:$A, i32:$B, 0)),
          (v4i32 (XXINSERTW v4i32:$A, AlignValues.I32_TO_BE_WORD1, 12))>;
def : Pat<(v4i32 (insertelt v4i32:$A, DblToInt.B, 0)),
          (v4i32 (XXINSERTW v4i32:$A,
                            (SUBREG_TO_REG (i64 1),
                                           (XSCVDPSXWS f64:$B), sub_64),
                            12))>;
def : Pat<(v4i32 (insertelt v4i32:$A, DblToUInt.B, 0)),
          (v4i32 (XXINSERTW v4i32:$A,
                            (SUBREG_TO_REG (i64 1),
                                           (XSCVDPUXWS f64:$B), sub_64),
                            12))>;
def : Pat<(v4i32 (insertelt v4i32:$A, i32:$B, 1)),
          (v4i32 (XXINSERTW v4i32:$A, AlignValues.I32_TO_BE_WORD1, 8))>;
def : Pat<(v4i32 (insertelt v4i32:$A, DblToInt.B, 1)),
          (v4i32 (XXINSERTW v4i32:$A,
                            (SUBREG_TO_REG (i64 1),
                                           (XSCVDPSXWS f64:$B), sub_64),
                            8))>;
def : Pat<(v4i32 (insertelt v4i32:$A, DblToUInt.B, 1)),
          (v4i32 (XXINSERTW v4i32:$A,
                            (SUBREG_TO_REG (i64 1),
                                           (XSCVDPUXWS f64:$B), sub_64),
                            8))>;
def : Pat<(v4i32 (insertelt v4i32:$A, i32:$B, 2)),
          (v4i32 (XXINSERTW v4i32:$A, AlignValues.I32_TO_BE_WORD1, 4))>;
def : Pat<(v4i32 (insertelt v4i32:$A, DblToInt.B, 2)),
          (v4i32 (XXINSERTW v4i32:$A,
                            (SUBREG_TO_REG (i64 1),
                                           (XSCVDPSXWS f64:$B), sub_64),
                            4))>;
def : Pat<(v4i32 (insertelt v4i32:$A, DblToUInt.B, 2)),
          (v4i32 (XXINSERTW v4i32:$A,
                            (SUBREG_TO_REG (i64 1),
                                           (XSCVDPUXWS f64:$B), sub_64),
                            4))>;
def : Pat<(v4i32 (insertelt v4i32:$A, i32:$B, 3)),
          (v4i32 (XXINSERTW v4i32:$A, AlignValues.I32_TO_BE_WORD1, 0))>;
def : Pat<(v4i32 (insertelt v4i32:$A, DblToInt.B, 3)),
          (v4i32 (XXINSERTW v4i32:$A,
                            (SUBREG_TO_REG (i64 1),
                                           (XSCVDPSXWS f64:$B), sub_64),
                            0))>;
def : Pat<(v4i32 (insertelt v4i32:$A, DblToUInt.B, 3)),
          (v4i32 (XXINSERTW v4i32:$A,
                            (SUBREG_TO_REG (i64 1),
                                           (XSCVDPUXWS f64:$B), sub_64),
                            0))>;
def : Pat<(v4f32 (insertelt v4f32:$A, f32:$B, 0)),
          (v4f32 (XXINSERTW v4f32:$A, AlignValues.F32_TO_BE_WORD1, 12))>;
def : Pat<(v4f32 (insertelt v4f32:$A, f32:$B, 1)),
          (v4f32 (XXINSERTW v4f32:$A, AlignValues.F32_TO_BE_WORD1, 8))>;
def : Pat<(v4f32 (insertelt v4f32:$A, f32:$B, 2)),
          (v4f32 (XXINSERTW v4f32:$A, AlignValues.F32_TO_BE_WORD1, 4))>;
def : Pat<(v4f32 (insertelt v4f32:$A, f32:$B, 3)),
          (v4f32 (XXINSERTW v4f32:$A, AlignValues.F32_TO_BE_WORD1, 0))>;

def : Pat<(v4f32 (insertelt v4f32:$A, (f32 (fpround f64:$B)), 0)),
          (v4f32 (XXINSERTW v4f32:$A,
                  (SUBREG_TO_REG (i64 1), (XSCVDPSP f64:$B), sub_64), 12))>;
def : Pat<(v4f32 (insertelt v4f32:$A, (f32 (fpround f64:$B)), 1)),
          (v4f32 (XXINSERTW v4f32:$A,
                  (SUBREG_TO_REG (i64 1), (XSCVDPSP f64:$B), sub_64), 8))>;
def : Pat<(v4f32 (insertelt v4f32:$A, (f32 (fpround f64:$B)), 2)),
          (v4f32 (XXINSERTW v4f32:$A,
                  (SUBREG_TO_REG (i64 1), (XSCVDPSP f64:$B), sub_64), 4))>;
def : Pat<(v4f32 (insertelt v4f32:$A, (f32 (fpround f64:$B)), 3)),
          (v4f32 (XXINSERTW v4f32:$A,
                  (SUBREG_TO_REG (i64 1), (XSCVDPSP f64:$B), sub_64), 0))>;

def : Pat<(v8i16 (PPCld_vec_be ForceXForm:$src)),
          (COPY_TO_REGCLASS (LXVH8X ForceXForm:$src), VRRC)>;
def : Pat<(PPCst_vec_be v8i16:$rS, ForceXForm:$dst),
          (STXVH8X (COPY_TO_REGCLASS $rS, VSRC), ForceXForm:$dst)>;

def : Pat<(v16i8 (PPCld_vec_be ForceXForm:$src)),
          (COPY_TO_REGCLASS (LXVB16X ForceXForm:$src), VRRC)>;
def : Pat<(PPCst_vec_be v16i8:$rS, ForceXForm:$dst),
          (STXVB16X (COPY_TO_REGCLASS $rS, VSRC), ForceXForm:$dst)>;

// Scalar stores of i8
def : Pat<(truncstorei8 (i32 (vector_extract v16i8:$S, 0)), ForceXForm:$dst),
          (STXSIBXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 8)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei8 (i32 (vector_extract v16i8:$S, 1)), ForceXForm:$dst),
          (STXSIBXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 7)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei8 (i32 (vector_extract v16i8:$S, 2)), ForceXForm:$dst),
          (STXSIBXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 6)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei8 (i32 (vector_extract v16i8:$S, 3)), ForceXForm:$dst),
          (STXSIBXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 5)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei8 (i32 (vector_extract v16i8:$S, 4)), ForceXForm:$dst),
          (STXSIBXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 4)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei8 (i32 (vector_extract v16i8:$S, 5)), ForceXForm:$dst),
          (STXSIBXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 3)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei8 (i32 (vector_extract v16i8:$S, 6)), ForceXForm:$dst),
          (STXSIBXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 2)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei8 (i32 (vector_extract v16i8:$S, 7)), ForceXForm:$dst),
          (STXSIBXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 1)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei8 (i32 (vector_extract v16i8:$S, 8)), ForceXForm:$dst),
          (STXSIBXv (COPY_TO_REGCLASS $S, VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei8 (i32 (vector_extract v16i8:$S, 9)), ForceXForm:$dst),
          (STXSIBXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 15)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei8 (i32 (vector_extract v16i8:$S, 10)), ForceXForm:$dst),
          (STXSIBXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 14)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei8 (i32 (vector_extract v16i8:$S, 11)), ForceXForm:$dst),
          (STXSIBXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 13)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei8 (i32 (vector_extract v16i8:$S, 12)), ForceXForm:$dst),
          (STXSIBXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 12)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei8 (i32 (vector_extract v16i8:$S, 13)), ForceXForm:$dst),
          (STXSIBXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 11)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei8 (i32 (vector_extract v16i8:$S, 14)), ForceXForm:$dst),
          (STXSIBXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 10)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei8 (i32 (vector_extract v16i8:$S, 15)), ForceXForm:$dst),
          (STXSIBXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 9)), VSRC), ForceXForm:$dst)>;

// Scalar stores of i16
def : Pat<(truncstorei16 (i32 (vector_extract v8i16:$S, 0)), ForceXForm:$dst),
          (STXSIHXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 8)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei16 (i32 (vector_extract v8i16:$S, 1)), ForceXForm:$dst),
          (STXSIHXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 6)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei16 (i32 (vector_extract v8i16:$S, 2)), ForceXForm:$dst),
          (STXSIHXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 4)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei16 (i32 (vector_extract v8i16:$S, 3)), ForceXForm:$dst),
          (STXSIHXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 2)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei16 (i32 (vector_extract v8i16:$S, 4)), ForceXForm:$dst),
          (STXSIHXv (COPY_TO_REGCLASS $S, VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei16 (i32 (vector_extract v8i16:$S, 5)), ForceXForm:$dst),
          (STXSIHXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 14)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei16 (i32 (vector_extract v8i16:$S, 6)), ForceXForm:$dst),
          (STXSIHXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 12)), VSRC), ForceXForm:$dst)>;
def : Pat<(truncstorei16 (i32 (vector_extract v8i16:$S, 7)), ForceXForm:$dst),
          (STXSIHXv (COPY_TO_REGCLASS (v16i8 (VSLDOI $S, $S, 10)), VSRC), ForceXForm:$dst)>;

defm : ScalToVecWPermute<
  v2i64, (i64 (load DSForm:$src)),
  (XXPERMDIs (DFLOADf64 DSForm:$src), 2),
  (SUBREG_TO_REG (i64 1), (DFLOADf64 DSForm:$src), sub_64)>;
defm : ScalToVecWPermute<
  v2i64, (i64 (load XForm:$src)),
  (XXPERMDIs (XFLOADf64 XForm:$src), 2),
  (SUBREG_TO_REG (i64 1), (XFLOADf64 XForm:$src), sub_64)>;
defm : ScalToVecWPermute<
  v2f64, (f64 (load DSForm:$src)),
  (XXPERMDIs (DFLOADf64 DSForm:$src), 2),
  (SUBREG_TO_REG (i64 1), (DFLOADf64 DSForm:$src), sub_64)>;
defm : ScalToVecWPermute<
  v2f64, (f64 (load XForm:$src)),
  (XXPERMDIs (XFLOADf64 XForm:$src), 2),
  (SUBREG_TO_REG (i64 1), (XFLOADf64 XForm:$src), sub_64)>;

def : Pat<(store (i64 (extractelt v2i64:$A, 0)), XForm:$src),
          (XFSTOREf64 (EXTRACT_SUBREG (XXPERMDI $A, $A, 2),
                       sub_64), XForm:$src)>;
def : Pat<(store (f64 (extractelt v2f64:$A, 0)), XForm:$src),
          (XFSTOREf64 (EXTRACT_SUBREG (XXPERMDI $A, $A, 2),
                       sub_64), XForm:$src)>;
def : Pat<(store (i64 (extractelt v2i64:$A, 1)), XForm:$src),
          (XFSTOREf64 (EXTRACT_SUBREG $A, sub_64), XForm:$src)>;
def : Pat<(store (f64 (extractelt v2f64:$A, 1)), XForm:$src),
          (XFSTOREf64 (EXTRACT_SUBREG $A, sub_64), XForm:$src)>;
def : Pat<(store (i64 (extractelt v2i64:$A, 0)), DSForm:$src),
          (DFSTOREf64 (EXTRACT_SUBREG (XXPERMDI $A, $A, 2),
                       sub_64), DSForm:$src)>;
def : Pat<(store (f64 (extractelt v2f64:$A, 0)), DSForm:$src),
          (DFSTOREf64 (EXTRACT_SUBREG (XXPERMDI $A, $A, 2), sub_64),
                      DSForm:$src)>;
def : Pat<(store (i64 (extractelt v2i64:$A, 1)), DSForm:$src),
          (DFSTOREf64 (EXTRACT_SUBREG $A, sub_64), DSForm:$src)>;
def : Pat<(store (f64 (extractelt v2f64:$A, 1)), DSForm:$src),
          (DFSTOREf64 (EXTRACT_SUBREG $A, sub_64), DSForm:$src)>;

// (Un)Signed DWord vector extract -> QP
def : Pat<(f128 (sint_to_fp (i64 (extractelt v2i64:$src, 0)))),
          (f128 (XSCVSDQP
                  (EXTRACT_SUBREG (XXPERMDI $src, $src, 3), sub_64)))>;
def : Pat<(f128 (sint_to_fp (i64 (extractelt v2i64:$src, 1)))),
          (f128 (XSCVSDQP (COPY_TO_REGCLASS $src, VFRC)))>;
def : Pat<(f128 (uint_to_fp (i64 (extractelt v2i64:$src, 0)))),
          (f128 (XSCVUDQP
                  (EXTRACT_SUBREG (XXPERMDI $src, $src, 3), sub_64)))>;
def : Pat<(f128 (uint_to_fp (i64 (extractelt v2i64:$src, 1)))),
          (f128 (XSCVUDQP (COPY_TO_REGCLASS $src, VFRC)))>;

// (Un)Signed Word vector extract -> QP
foreach Idx = [[0,3],[1,2],[3,0]] in {
  def : Pat<(f128 (sint_to_fp (i32 (extractelt v4i32:$src, !head(Idx))))),
            (f128 (XSCVSDQP (EXTRACT_SUBREG
                              (VEXTSW2D (VSPLTW !head(!tail(Idx)), $src)),
                              sub_64)))>;
}
def : Pat<(f128 (sint_to_fp (i32 (extractelt v4i32:$src, 2)))),
          (f128 (XSCVSDQP (EXTRACT_SUBREG (VEXTSW2D $src), sub_64)))>;

foreach Idx = [[0,12],[1,8],[2,4],[3,0]] in {
  def : Pat<(f128 (uint_to_fp (i32 (extractelt v4i32:$src, !head(Idx))))),
            (f128 (XSCVUDQP (XXEXTRACTUW $src, !head(!tail(Idx)))))>;
}

// (Un)Signed HWord vector extract -> QP/DP/SP
// The Nested foreach lists identifies the vector element and corresponding
// register byte location.
foreach Idx = [[0,14],[1,12],[2,10],[3,8],[4,6],[5,4],[6,2],[7,0]] in {
  def : Pat<(f128 (sint_to_fp
                    (i32 (sext_inreg
                           (vector_extract v8i16:$src, !head(Idx)), i16)))),
            (f128 (XSCVSDQP
                    (EXTRACT_SUBREG (VEXTSH2D
                                      (VEXTRACTUH !head(!tail(Idx)), $src)),
                                    sub_64)))>;
  def : Pat<(f128 (uint_to_fp
                    (and (i32 (vector_extract v8i16:$src, !head(Idx))),
                         65535))),
            (f128 (XSCVUDQP (EXTRACT_SUBREG
                              (VEXTRACTUH !head(!tail(Idx)), $src), sub_64)))>;
  def : Pat<(f32 (PPCfcfidus
                   (f64 (PPCmtvsrz
                          (and (i32 (vector_extract v8i16:$src, !head(Idx))),
                          65535))))),
            (f32 (XSCVUXDSP (EXTRACT_SUBREG
                              (VEXTRACTUH !head(!tail(Idx)), $src), sub_64)))>;
  def : Pat<(f32 (PPCfcfids
                   (f64 (PPCmtvsra
                          (i32 (sext_inreg (vector_extract v8i16:$src,
                                           !head(Idx)), i16)))))),
            (f32 (XSCVSXDSP
                    (EXTRACT_SUBREG
                     (VEXTSH2D (VEXTRACTUH !head(!tail(Idx)), $src)),
                     sub_64)))>;
  def : Pat<(f64 (PPCfcfidu
                   (f64 (PPCmtvsrz
                          (and (i32 (vector_extract v8i16:$src, !head(Idx))),
                          65535))))),
            (f64 (XSCVUXDDP (EXTRACT_SUBREG
                              (VEXTRACTUH !head(!tail(Idx)), $src), sub_64)))>;
  def : Pat<(f64 (PPCfcfid
                   (f64 (PPCmtvsra
                        (i32 (sext_inreg
                            (vector_extract v8i16:$src, !head(Idx)), i16)))))),
            (f64 (XSCVSXDDP
                    (EXTRACT_SUBREG (VEXTSH2D
                                      (VEXTRACTUH !head(!tail(Idx)), $src)),
                                    sub_64)))>;
}

// (Un)Signed Byte vector extract -> QP/DP/SP
foreach Idx = [[0,15],[1,14],[2,13],[3,12],[4,11],[5,10],[6,9],[7,8],[8,7],
               [9,6],[10,5],[11,4],[12,3],[13,2],[14,1],[15,0]] in {
  def : Pat<(f128 (sint_to_fp
                    (i32 (sext_inreg
                           (vector_extract v16i8:$src, !head(Idx)), i8)))),
            (f128 (XSCVSDQP
                    (EXTRACT_SUBREG
                      (VEXTSB2D (VEXTRACTUB !head(!tail(Idx)), $src)),
                      sub_64)))>;
  def : Pat<(f128 (uint_to_fp
                    (and (i32 (vector_extract v16i8:$src, !head(Idx))),
                         255))),
            (f128 (XSCVUDQP
                    (EXTRACT_SUBREG
                      (VEXTRACTUB !head(!tail(Idx)), $src), sub_64)))>;

  def : Pat<(f32 (PPCfcfidus
                   (f64 (PPCmtvsrz
                          (and (i32 (vector_extract v16i8:$src, !head(Idx))),
                          255))))),
            (f32 (XSCVUXDSP (EXTRACT_SUBREG
                              (VEXTRACTUB !head(!tail(Idx)), $src), sub_64)))>;
  def : Pat<(f32 (PPCfcfids
                   (f64 (PPCmtvsra
                          (i32 (sext_inreg
                            (vector_extract v16i8:$src, !head(Idx)), i8)))))),
            (f32 (XSCVSXDSP
                    (EXTRACT_SUBREG (VEXTSH2D
                                      (VEXTRACTUB !head(!tail(Idx)), $src)),
                                    sub_64)))>;
  def : Pat<(f64 (PPCfcfidu
                   (f64 (PPCmtvsrz
                          (and (i32
                            (vector_extract v16i8:$src, !head(Idx))), 255))))),
            (f64 (XSCVUXDDP (EXTRACT_SUBREG
                              (VEXTRACTUB !head(!tail(Idx)), $src), sub_64)))>;
  def : Pat<(f64 (PPCfcfidu
                   (f64 (PPCmtvsra
                        (i32 (sext_inreg
                            (vector_extract v16i8:$src, !head(Idx)), i8)))))),
            (f64 (XSCVSXDDP
                    (EXTRACT_SUBREG (VEXTSH2D
                                      (VEXTRACTUB !head(!tail(Idx)), $src)),
                                    sub_64)))>;

  def : Pat<(f64 (PPCfcfid
                   (f64 (PPCmtvsra
                        (i32 (sext_inreg
                          (vector_extract v16i8:$src, !head(Idx)), i8)))))),
            (f64 (XSCVSXDDP
                    (EXTRACT_SUBREG (VEXTSH2D
                                      (VEXTRACTUH !head(!tail(Idx)), $src)),
                                    sub_64)))>;
}

// Unsiged int in vsx register -> QP
def : Pat<(f128 (uint_to_fp (i32 (PPCmfvsr f64:$src)))),
          (f128 (XSCVUDQP
                  (XXEXTRACTUW (SUBREG_TO_REG (i64 1), $src, sub_64), 8)))>;
} // HasVSX, HasP9Vector, IsLittleEndian

// Any Power9 VSX subtarget that supports Power9 Altivec.
let Predicates = [HasVSX, HasP9Altivec] in {
// Unsigned absolute-difference.
def : Pat<(v4i32 (abdu v4i32:$A, v4i32:$B)),
          (v4i32 (VABSDUW $A, $B))>;

def : Pat<(v8i16 (abdu v8i16:$A, v8i16:$B)),
          (v8i16 (VABSDUH $A, $B))>;

def : Pat<(v16i8 (abdu v16i8:$A, v16i8:$B)),
          (v16i8 (VABSDUB $A, $B))>;

// Signed absolute-difference.
// Power9 VABSD* instructions are designed to support unsigned integer
// vectors (byte/halfword/word), if we want to make use of them for signed
// integer vectors, we have to flip their sign bits first. To flip sign bit
// for byte/halfword integer vector would become inefficient, but for word
// integer vector, we can leverage XVNEGSP to make it efficiently.
def : Pat<(v4i32 (abds v4i32:$A, v4i32:$B)),
          (v4i32 (VABSDUW (XVNEGSP $A), (XVNEGSP $B)))>;
} // HasVSX, HasP9Altivec

// Big endian Power9 64Bit VSX subtargets with P9 Altivec support.
let Predicates = [HasVSX, HasP9Altivec, IsBigEndian, IsPPC64] in {
def : Pat<(i64 (anyext (i32 (vector_extract v16i8:$S, i64:$Idx)))),
          (VEXTUBLX $Idx, $S)>;

def : Pat<(i64 (anyext (i32 (vector_extract v8i16:$S, i64:$Idx)))),
          (VEXTUHLX (RLWINM8 $Idx, 1, 28, 30), $S)>;
def : Pat<(i64 (anyext (i32 (vector_extract v8i16:$S, 0)))),
          (VEXTUHLX (LI8 0), $S)>;
def : Pat<(i64 (anyext (i32 (vector_extract v8i16:$S, 1)))),
          (VEXTUHLX (LI8 2), $S)>;
def : Pat<(i64 (anyext (i32 (vector_extract v8i16:$S, 2)))),
          (VEXTUHLX (LI8 4), $S)>;
def : Pat<(i64 (anyext (i32 (vector_extract v8i16:$S, 3)))),
          (VEXTUHLX (LI8 6), $S)>;
def : Pat<(i64 (anyext (i32 (vector_extract v8i16:$S, 4)))),
          (VEXTUHLX (LI8 8), $S)>;
def : Pat<(i64 (anyext (i32 (vector_extract v8i16:$S, 5)))),
          (VEXTUHLX (LI8 10), $S)>;
def : Pat<(i64 (anyext (i32 (vector_extract v8i16:$S, 6)))),
          (VEXTUHLX (LI8 12), $S)>;
def : Pat<(i64 (anyext (i32 (vector_extract v8i16:$S, 7)))),
          (VEXTUHLX (LI8 14), $S)>;

def : Pat<(i64 (zext (i32 (vector_extract v4i32:$S, i64:$Idx)))),
          (VEXTUWLX (RLWINM8 $Idx, 2, 28, 29), $S)>;
def : Pat<(i64 (zext (i32 (vector_extract v4i32:$S, 0)))),
          (VEXTUWLX (LI8 0), $S)>;

// For extracting BE word 1, MFVSRWZ is better than VEXTUWLX
def : Pat<(i64 (zext (i32 (vector_extract v4i32:$S, 1)))),
          (INSERT_SUBREG (i64 (IMPLICIT_DEF)),
          (i32 VectorExtractions.LE_WORD_2), sub_32)>;
def : Pat<(i64 (zext (i32 (vector_extract v4i32:$S, 2)))),
          (VEXTUWLX (LI8 8), $S)>;
def : Pat<(i64 (zext (i32 (vector_extract v4i32:$S, 3)))),
          (VEXTUWLX (LI8 12), $S)>;

def : Pat<(i64 (sext (i32 (vector_extract v4i32:$S, i64:$Idx)))),
          (EXTSW (VEXTUWLX (RLWINM8 $Idx, 2, 28, 29), $S))>;
def : Pat<(i64 (sext (i32 (vector_extract v4i32:$S, 0)))),
          (EXTSW (VEXTUWLX (LI8 0), $S))>;
// For extracting BE word 1, MFVSRWZ is better than VEXTUWLX
def : Pat<(i64 (sext (i32 (vector_extract v4i32:$S, 1)))),
          (EXTSW (INSERT_SUBREG (i64 (IMPLICIT_DEF)),
          (i32 VectorExtractions.LE_WORD_2), sub_32))>;
def : Pat<(i64 (sext (i32 (vector_extract v4i32:$S, 2)))),
          (EXTSW (VEXTUWLX (LI8 8), $S))>;
def : Pat<(i64 (sext (i32 (vector_extract v4i32:$S, 3)))),
          (EXTSW (VEXTUWLX (LI8 12), $S))>;

def : Pat<(i32 (vector_extract v16i8:$S, i64:$Idx)),
          (i32 (EXTRACT_SUBREG (VEXTUBLX $Idx, $S), sub_32))>;
def : Pat<(i32 (vector_extract v16i8:$S, 0)),
          (i32 (EXTRACT_SUBREG (VEXTUBLX (LI8 0), $S), sub_32))>;
def : Pat<(i32 (vector_extract v16i8:$S, 1)),
          (i32 (EXTRACT_SUBREG (VEXTUBLX (LI8 1), $S), sub_32))>;
def : Pat<(i32 (vector_extract v16i8:$S, 2)),
          (i32 (EXTRACT_SUBREG (VEXTUBLX (LI8 2), $S), sub_32))>;
def : Pat<(i32 (vector_extract v16i8:$S, 3)),
          (i32 (EXTRACT_SUBREG (VEXTUBLX (LI8 3), $S), sub_32))>;
def : Pat<(i32 (vector_extract v16i8:$S, 4)),
          (i32 (EXTRACT_SUBREG (VEXTUBLX (LI8 4), $S), sub_32))>;
def : Pat<(i32 (vector_extract v16i8:$S, 5)),
          (i32 (EXTRACT_SUBREG (VEXTUBLX (LI8 5), $S), sub_32))>;
def : Pat<(i32 (vector_extract v16i8:$S, 6)),
          (i32 (EXTRACT_SUBREG (VEXTUBLX (LI8 6), $S), sub_32))>;
def : Pat<(i32 (vector_extract v16i8:$S, 7)),
          (i32 (EXTRACT_SUBREG (VEXTUBLX (LI8 7), $S), sub_32))>;
def : Pat<(i32 (vector_extract v16i8:$S, 8)),
          (i32 (EXTRACT_SUBREG (VEXTUBLX (LI8 8), $S), sub_32))>;
def : Pat<(i32 (vector_extract v16i8:$S, 9)),
          (i32 (EXTRACT_SUBREG (VEXTUBLX (LI8 9), $S), sub_32))>;
def : Pat<(i32 (vector_extract v16i8:$S, 10)),
          (i32 (EXTRACT_SUBREG (VEXTUBLX (LI8 10), $S), sub_32))>;
def : Pat<(i32 (vector_extract v16i8:$S, 11)),
          (i32 (EXTRACT_SUBREG (VEXTUBLX (LI8 11), $S), sub_32))>;
def : Pat<(i32 (vector_extract v16i8:$S, 12)),
          (i32 (EXTRACT_SUBREG (VEXTUBLX (LI8 12), $S), sub_32))>;
def : Pat<(i32 (vector_extract v16i8:$S, 13)),
          (i32 (EXTRACT_SUBREG (VEXTUBLX (LI8 13), $S), sub_32))>;
def : Pat<(i32 (vector_extract v16i8:$S, 14)),
          (i32 (EXTRACT_SUBREG (VEXTUBLX (LI8 14), $S), sub_32))>;
def : Pat<(i32 (vector_extract v16i8:$S, 15)),
          (i32 (EXTRACT_SUBREG (VEXTUBLX (LI8 15), $S), sub_32))>;

def : Pat<(i32 (vector_extract v8i16:$S, i64:$Idx)),
          (i32 (EXTRACT_SUBREG (VEXTUHLX
          (RLWINM8 $Idx, 1, 28, 30), $S), sub_32))>;
def : Pat<(i32 (vector_extract v8i16:$S, 0)),
          (i32 (EXTRACT_SUBREG (VEXTUHLX (LI8 0), $S), sub_32))>;
def : Pat<(i32 (vector_extract v8i16:$S, 1)),
          (i32 (EXTRACT_SUBREG (VEXTUHLX (LI8 2), $S), sub_32))>;
def : Pat<(i32 (vector_extract v8i16:$S, 2)),
          (i32 (EXTRACT_SUBREG (VEXTUHLX (LI8 4), $S), sub_32))>;
def : Pat<(i32 (vector_extract v8i16:$S, 3)),
          (i32 (EXTRACT_SUBREG (VEXTUHLX (LI8 6), $S), sub_32))>;
def : Pat<(i32 (vector_extract v8i16:$S, 4)),
          (i32 (EXTRACT_SUBREG (VEXTUHLX (LI8 8), $S), sub_32))>;
def : Pat<(i32 (vector_extract v8i16:$S, 5)),
          (i32 (EXTRACT_SUBREG (VEXTUHLX (LI8 10), $S), sub_32))>;
def : Pat<(i32 (vector_extract v8i16:$S, 6)),
          (i32 (EXTRACT_SUBREG (VEXTUHLX (LI8 12), $S), sub_32))>;
def : Pat<(i32 (vector_extract v8i16:$S, 6)),
          (i32 (EXTRACT_SUBREG (VEXTUHLX (LI8 14), $S), sub_32))>;

def : Pat<(i32 (vector_extract v4i32:$S, i64:$Idx)),
          (i32 (EXTRACT_SUBREG (VEXTUWLX
          (RLWINM8 $Idx, 2, 28, 29), $S), sub_32))>;
def : Pat<(i32 (vector_extract v4i32:$S, 0)),
          (i32 (EXTRACT_SUBREG (VEXTUWLX (LI8 0), $S), sub_32))>;
// For extracting BE word 1, MFVSRWZ is better than VEXTUWLX
def : Pat<(i32 (vector_extract v4i32:$S, 1)),
          (i32 VectorExtractions.LE_WORD_2)>;
def : Pat<(i32 (vector_extract v4i32:$S, 2)),
          (i32 (EXTRACT_SUBREG (VEXTUWLX (LI8 8), $S), sub_32))>;
def : Pat<(i32 (vector_extract v4i32:$S, 3)),
          (i32 (EXTRACT_SUBREG (VEXTUWLX (LI8 12), $S), sub_32))>;

// P9 Altivec instructions that can be used to build vectors.
// Adding them to PPCInstrVSX.td rather than PPCAltivecVSX.td to compete
// with complexities of existing build vector patterns in this file.
def : Pat<(v2i64 (build_vector WordToDWord.BE_A0, WordToDWord.BE_A1)),
          (v2i64 (VEXTSW2D $A))>;
def : Pat<(v2i64 (build_vector HWordToDWord.BE_A0, HWordToDWord.BE_A1)),
          (v2i64 (VEXTSH2D $A))>;
def : Pat<(v4i32 (build_vector HWordToWord.BE_A0, HWordToWord.BE_A1,
                  HWordToWord.BE_A2, HWordToWord.BE_A3)),
          (v4i32 (VEXTSH2W $A))>;
def : Pat<(v4i32 (build_vector ByteToWord.BE_A0, ByteToWord.BE_A1,
                  ByteToWord.BE_A2, ByteToWord.BE_A3)),
          (v4i32 (VEXTSB2W $A))>;
def : Pat<(v2i64 (build_vector ByteToDWord.BE_A0, ByteToDWord.BE_A1)),
          (v2i64 (VEXTSB2D $A))>;
} // HasVSX, HasP9Altivec, IsBigEndian, IsPPC64

// Little endian Power9 VSX subtargets with P9 Altivec support.
let Predicates = [HasVSX, HasP9Altivec, IsLittleEndian] in {
def : Pat<(i64 (anyext (i32 (vector_extract v16i8:$S, i64:$Idx)))),
          (VEXTUBRX $Idx, $S)>;

def : Pat<(i64 (anyext (i32 (vector_extract v8i16:$S, i64:$Idx)))),
          (VEXTUHRX (RLWINM8 $Idx, 1, 28, 30), $S)>;
def : Pat<(i64 (anyext (i32 (vector_extract v8i16:$S, 0)))),
          (VEXTUHRX (LI8 0), $S)>;
def : Pat<(i64 (anyext (i32 (vector_extract v8i16:$S, 1)))),
          (VEXTUHRX (LI8 2), $S)>;
def : Pat<(i64 (anyext (i32 (vector_extract v8i16:$S, 2)))),
          (VEXTUHRX (LI8 4), $S)>;
def : Pat<(i64 (anyext (i32 (vector_extract v8i16:$S, 3)))),
          (VEXTUHRX (LI8 6), $S)>;
def : Pat<(i64 (anyext (i32 (vector_extract v8i16:$S, 4)))),
          (VEXTUHRX (LI8 8), $S)>;
def : Pat<(i64 (anyext (i32 (vector_extract v8i16:$S, 5)))),
          (VEXTUHRX (LI8 10), $S)>;
def : Pat<(i64 (anyext (i32 (vector_extract v8i16:$S, 6)))),
          (VEXTUHRX (LI8 12), $S)>;
def : Pat<(i64 (anyext (i32 (vector_extract v8i16:$S, 7)))),
          (VEXTUHRX (LI8 14), $S)>;

def : Pat<(i64 (zext (i32 (vector_extract v4i32:$S, i64:$Idx)))),
          (VEXTUWRX (RLWINM8 $Idx, 2, 28, 29), $S)>;
def : Pat<(i64 (zext (i32 (vector_extract v4i32:$S, 0)))),
          (VEXTUWRX (LI8 0), $S)>;
def : Pat<(i64 (zext (i32 (vector_extract v4i32:$S, 1)))),
          (VEXTUWRX (LI8 4), $S)>;
// For extracting LE word 2, MFVSRWZ is better than VEXTUWRX
def : Pat<(i64 (zext (i32 (vector_extract v4i32:$S, 2)))),
          (INSERT_SUBREG (i64 (IMPLICIT_DEF)),
          (i32 VectorExtractions.LE_WORD_2), sub_32)>;
def : Pat<(i64 (zext (i32 (vector_extract v4i32:$S, 3)))),
          (VEXTUWRX (LI8 12), $S)>;

def : Pat<(i64 (sext (i32 (vector_extract v4i32:$S, i64:$Idx)))),
          (EXTSW (VEXTUWRX (RLWINM8 $Idx, 2, 28, 29), $S))>;
def : Pat<(i64 (sext (i32 (vector_extract v4i32:$S, 0)))),
          (EXTSW (VEXTUWRX (LI8 0), $S))>;
def : Pat<(i64 (sext (i32 (vector_extract v4i32:$S, 1)))),
          (EXTSW (VEXTUWRX (LI8 4), $S))>;
// For extracting LE word 2, MFVSRWZ is better than VEXTUWRX
def : Pat<(i64 (sext (i32 (vector_extract v4i32:$S, 2)))),
          (EXTSW (INSERT_SUBREG (i64 (IMPLICIT_DEF)),
          (i32 VectorExtractions.LE_WORD_2), sub_32))>;
def : Pat<(i64 (sext (i32 (vector_extract v4i32:$S, 3)))),
          (EXTSW (VEXTUWRX (LI8 12), $S))>;

def : Pat<(i32 (vector_extract v16i8:$S, i64:$Idx)),
          (i32 (EXTRACT_SUBREG (VEXTUBRX $Idx, $S), sub_32))>;
def : Pat<(i32 (vector_extract v16i8:$S, 0)),
          (i32 (EXTRACT_SUBREG (VEXTUBRX (LI8 0), $S), sub_32))>;
def : Pat<(i32 (vector_extract v16i8:$S, 1)),
          (i32 (EXTRACT_SUBREG (VEXTUBRX (LI8 1), $S), sub_32))>;
def : Pat<(i32 (vector_extract v16i8:$S, 2)),
          (i32 (EXTRACT_SUBREG (VEXTUBRX (LI8 2), $S), sub_32))>;
def : Pat<(i32 (vector_extract v16i8:$S, 3)),
          (i32 (EXTRACT_SUBREG (VEXTUBRX (LI8 3), $S), sub_32))>;
def : Pat<(i32 (vector_extract v16i8:$S, 4)),
          (i32 (EXTRACT_SUBREG (VEXTUBRX (LI8 4), $S), sub_32))>;
def : Pat<(i32 (vector_extract v16i8:$S, 5)),
          (i32 (EXTRACT_SUBREG (VEXTUBRX (LI8 5), $S), sub_32))>;
def : Pat<(i32 (vector_extract v16i8:$S, 6)),
          (i32 (EXTRACT_SUBREG (VEXTUBRX (LI8 6), $S), sub_32))>;
def : Pat<(i32 (vector_extract v16i8:$S, 7)),
          (i32 (EXTRACT_SUBREG (VEXTUBRX (LI8 7), $S), sub_32))>;
def : Pat<(i32 (vector_extract v16i8:$S, 8)),
          (i32 (EXTRACT_SUBREG (VEXTUBRX (LI8 8), $S), sub_32))>;
def : Pat<(i32 (vector_extract v16i8:$S, 9)),
          (i32 (EXTRACT_SUBREG (VEXTUBRX (LI8 9), $S), sub_32))>;
def : Pat<(i32 (vector_extract v16i8:$S, 10)),
          (i32 (EXTRACT_SUBREG (VEXTUBRX (LI8 10), $S), sub_32))>;
def : Pat<(i32 (vector_extract v16i8:$S, 11)),
          (i32 (EXTRACT_SUBREG (VEXTUBRX (LI8 11), $S), sub_32))>;
def : Pat<(i32 (vector_extract v16i8:$S, 12)),
          (i32 (EXTRACT_SUBREG (VEXTUBRX (LI8 12), $S), sub_32))>;
def : Pat<(i32 (vector_extract v16i8:$S, 13)),
          (i32 (EXTRACT_SUBREG (VEXTUBRX (LI8 13), $S), sub_32))>;
def : Pat<(i32 (vector_extract v16i8:$S, 14)),
          (i32 (EXTRACT_SUBREG (VEXTUBRX (LI8 14), $S), sub_32))>;
def : Pat<(i32 (vector_extract v16i8:$S, 15)),
          (i32 (EXTRACT_SUBREG (VEXTUBRX (LI8 15), $S), sub_32))>;

def : Pat<(i32 (vector_extract v8i16:$S, i64:$Idx)),
          (i32 (EXTRACT_SUBREG (VEXTUHRX
          (RLWINM8 $Idx, 1, 28, 30), $S), sub_32))>;
def : Pat<(i32 (vector_extract v8i16:$S, 0)),
          (i32 (EXTRACT_SUBREG (VEXTUHRX (LI8 0), $S), sub_32))>;
def : Pat<(i32 (vector_extract v8i16:$S, 1)),
          (i32 (EXTRACT_SUBREG (VEXTUHRX (LI8 2), $S), sub_32))>;
def : Pat<(i32 (vector_extract v8i16:$S, 2)),
          (i32 (EXTRACT_SUBREG (VEXTUHRX (LI8 4), $S), sub_32))>;
def : Pat<(i32 (vector_extract v8i16:$S, 3)),
          (i32 (EXTRACT_SUBREG (VEXTUHRX (LI8 6), $S), sub_32))>;
def : Pat<(i32 (vector_extract v8i16:$S, 4)),
          (i32 (EXTRACT_SUBREG (VEXTUHRX (LI8 8), $S), sub_32))>;
def : Pat<(i32 (vector_extract v8i16:$S, 5)),
          (i32 (EXTRACT_SUBREG (VEXTUHRX (LI8 10), $S), sub_32))>;
def : Pat<(i32 (vector_extract v8i16:$S, 6)),
          (i32 (EXTRACT_SUBREG (VEXTUHRX (LI8 12), $S), sub_32))>;
def : Pat<(i32 (vector_extract v8i16:$S, 6)),
          (i32 (EXTRACT_SUBREG (VEXTUHRX (LI8 14), $S), sub_32))>;

def : Pat<(i32 (vector_extract v4i32:$S, i64:$Idx)),
          (i32 (EXTRACT_SUBREG (VEXTUWRX
          (RLWINM8 $Idx, 2, 28, 29), $S), sub_32))>;
def : Pat<(i32 (vector_extract v4i32:$S, 0)),
          (i32 (EXTRACT_SUBREG (VEXTUWRX (LI8 0), $S), sub_32))>;
def : Pat<(i32 (vector_extract v4i32:$S, 1)),
          (i32 (EXTRACT_SUBREG (VEXTUWRX (LI8 4), $S), sub_32))>;
// For extracting LE word 2, MFVSRWZ is better than VEXTUWRX
def : Pat<(i32 (vector_extract v4i32:$S, 2)),
          (i32 VectorExtractions.LE_WORD_2)>;
def : Pat<(i32 (vector_extract v4i32:$S, 3)),
          (i32 (EXTRACT_SUBREG (VEXTUWRX (LI8 12), $S), sub_32))>;

// P9 Altivec instructions that can be used to build vectors.
// Adding them to PPCInstrVSX.td rather than PPCAltivecVSX.td to compete
// with complexities of existing build vector patterns in this file.
def : Pat<(v2i64 (build_vector WordToDWord.LE_A0, WordToDWord.LE_A1)),
          (v2i64 (VEXTSW2D $A))>;
def : Pat<(v2i64 (build_vector HWordToDWord.LE_A0, HWordToDWord.LE_A1)),
          (v2i64 (VEXTSH2D $A))>;
def : Pat<(v4i32 (build_vector HWordToWord.LE_A0, HWordToWord.LE_A1,
                  HWordToWord.LE_A2, HWordToWord.LE_A3)),
          (v4i32 (VEXTSH2W $A))>;
def : Pat<(v4i32 (build_vector ByteToWord.LE_A0, ByteToWord.LE_A1,
                  ByteToWord.LE_A2, ByteToWord.LE_A3)),
          (v4i32 (VEXTSB2W $A))>;
def : Pat<(v2i64 (build_vector ByteToDWord.LE_A0, ByteToDWord.LE_A1)),
          (v2i64 (VEXTSB2D $A))>;
} // HasVSX, HasP9Altivec, IsLittleEndian

// Big endian 64Bit VSX subtarget that supports additional direct moves from
// ISA3.0.
let Predicates = [HasVSX, IsISA3_0, HasDirectMove, IsBigEndian, IsPPC64] in {
def : Pat<(i64 (extractelt v2i64:$A, 1)),
          (i64 (MFVSRLD $A))>;
// Better way to build integer vectors if we have MTVSRDD. Big endian.
def : Pat<(v2i64 (build_vector i64:$rB, i64:$rA)),
          (v2i64 (MTVSRDD $rB, $rA))>;
def : Pat<(v4i32 (build_vector i32:$A, i32:$B, i32:$C, i32:$D)),
          (MTVSRDD
            (RLDIMI AnyExts.B, AnyExts.A, 32, 0),
            (RLDIMI AnyExts.D, AnyExts.C, 32, 0))>;

def : Pat<(f128 (PPCbuild_fp128 i64:$rB, i64:$rA)),
          (f128 (COPY_TO_REGCLASS (MTVSRDD $rB, $rA), VRRC))>;
} // HasVSX, IsISA3_0, HasDirectMove, IsBigEndian, IsPPC64

// Little endian VSX subtarget that supports direct moves from ISA3.0.
let Predicates = [HasVSX, IsISA3_0, HasDirectMove, IsLittleEndian] in {
def : Pat<(i64 (extractelt v2i64:$A, 0)),
          (i64 (MFVSRLD $A))>;
// Better way to build integer vectors if we have MTVSRDD. Little endian.
def : Pat<(v2i64 (build_vector i64:$rA, i64:$rB)),
          (v2i64 (MTVSRDD $rB, $rA))>;
def : Pat<(v4i32 (build_vector i32:$A, i32:$B, i32:$C, i32:$D)),
          (MTVSRDD
            (RLDIMI AnyExts.C, AnyExts.D, 32, 0),
            (RLDIMI AnyExts.A, AnyExts.B, 32, 0))>;

def : Pat<(f128 (PPCbuild_fp128 i64:$rA, i64:$rB)),
          (f128 (COPY_TO_REGCLASS (MTVSRDD $rB, $rA), VRRC))>;
} // HasVSX, IsISA3_0, HasDirectMove, IsLittleEndian
} // AddedComplexity = 400

//---------------------------- Instruction aliases ---------------------------//
def : InstAlias<"xvmovdp $XT, $XB",
                (XVCPSGNDP vsrc:$XT, vsrc:$XB, vsrc:$XB)>;
def : InstAlias<"xvmovsp $XT, $XB",
                (XVCPSGNSP vsrc:$XT, vsrc:$XB, vsrc:$XB)>;

// Certain versions of the AIX assembler may missassemble these mnemonics.
let Predicates = [ModernAs] in {
  def : InstAlias<"xxspltd $XT, $XB, 0",
                  (XXPERMDI vsrc:$XT, vsrc:$XB, vsrc:$XB, 0)>;
  def : InstAlias<"xxspltd $XT, $XB, 1",
                  (XXPERMDI vsrc:$XT, vsrc:$XB, vsrc:$XB, 3)>;
  def : InstAlias<"xxspltd $XT, $XB, 0",
                  (XXPERMDIs vsrc:$XT, vsfrc:$XB, 0)>;
  def : InstAlias<"xxspltd $XT, $XB, 1",
                  (XXPERMDIs vsrc:$XT, vsfrc:$XB, 3)>;
}

def : InstAlias<"xxmrghd $XT, $XA, $XB",
                (XXPERMDI vsrc:$XT, vsrc:$XA, vsrc:$XB, 0)>;
def : InstAlias<"xxmrgld $XT, $XA, $XB",
                (XXPERMDI vsrc:$XT, vsrc:$XA, vsrc:$XB, 3)>;
def : InstAlias<"xxswapd $XT, $XB",
                (XXPERMDI vsrc:$XT, vsrc:$XB, vsrc:$XB, 2)>;
def : InstAlias<"xxswapd $XT, $XB",
                (XXPERMDIs vsrc:$XT, vsfrc:$XB, 2)>;
def : InstAlias<"mfvrd $rA, $XT",
                (MFVRD g8rc:$rA, vrrc:$XT), 0>;
def : InstAlias<"mffprd $rA, $src",
                (MFVSRD g8rc:$rA, f8rc:$src)>;
def : InstAlias<"mtvrd $XT, $rA",
                (MTVRD vrrc:$XT, g8rc:$rA), 0>;
def : InstAlias<"mtfprd $dst, $rA",
                (MTVSRD f8rc:$dst, g8rc:$rA)>;
def : InstAlias<"mfvrwz $rA, $XT",
                (MFVRWZ gprc:$rA, vrrc:$XT), 0>;
def : InstAlias<"mffprwz $rA, $src",
                (MFVSRWZ gprc:$rA, f8rc:$src)>;
def : InstAlias<"mtvrwa $XT, $rA",
                (MTVRWA vrrc:$XT, gprc:$rA), 0>;
def : InstAlias<"mtfprwa $dst, $rA",
                (MTVSRWA f8rc:$dst, gprc:$rA)>;
def : InstAlias<"mtvrwz $XT, $rA",
                (MTVRWZ vrrc:$XT, gprc:$rA), 0>;
def : InstAlias<"mtfprwz $dst, $rA",
                (MTVSRWZ f8rc:$dst, gprc:$rA)>;

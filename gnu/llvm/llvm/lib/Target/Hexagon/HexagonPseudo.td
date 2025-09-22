//===--- HexagonPseudo.td -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// The pat frags in the definitions below need to have a named register,
// otherwise i32 will be assumed regardless of the register class. The
// name of the register does not matter.
def I1  : PatLeaf<(i1 PredRegs:$R)>;
def I32 : PatLeaf<(i32 IntRegs:$R)>;
def I64 : PatLeaf<(i64 DoubleRegs:$R)>;
def F32 : PatLeaf<(f32 IntRegs:$R)>;
def F64 : PatLeaf<(f64 DoubleRegs:$R)>;

let PrintMethod = "printGlobalOperand" in {
  def globaladdress : Operand<i32>;
  def globaladdressExt : Operand<i32>;
}

let isPseudo = 1 in {
let isCodeGenOnly = 0 in
def A2_iconst : Pseudo<(outs IntRegs:$Rd32),
    (ins s27_2Imm:$Ii), "${Rd32} = iconst(#${Ii})">;

def DUPLEX_Pseudo : InstHexagon<(outs),
    (ins s32_0Imm:$offset), "DUPLEX", [], "", DUPLEX, TypePSEUDO>;
}

let isExtendable = 1, opExtendable = 1, opExtentBits = 6,
    isAsmParserOnly = 1 in
def TFRI64_V2_ext : InstHexagon<(outs DoubleRegs:$dst),
    (ins s32_0Imm:$src1, s8_0Imm:$src2),
    "$dst = combine(#$src1,#$src2)", [], "",
    A2_combineii.Itinerary, TypeALU32_2op>, OpcodeHexagon;

// HI/LO Instructions
let isReMaterializable = 1, isMoveImm = 1, hasSideEffects = 0,
    hasNewValue = 1, opNewValue = 0 in
class REG_IMMED<string RegHalf, bit Rs, bits<3> MajOp, bit MinOp,
                InstHexagon rootInst>
  : InstHexagon<(outs IntRegs:$dst),
                (ins u16_0Imm:$imm_value),
                "$dst"#RegHalf#" = #$imm_value", [], "",
                rootInst.Itinerary, rootInst.Type>, OpcodeHexagon {
    bits<5> dst;
    bits<32> imm_value;

    let Inst{27} = Rs;
    let Inst{26-24} = MajOp;
    let Inst{21} = MinOp;
    let Inst{20-16} = dst;
    let Inst{23-22} = imm_value{15-14};
    let Inst{13-0} = imm_value{13-0};
}

let isAsmParserOnly = 1 in {
  def LO : REG_IMMED<".l", 0b0, 0b001, 0b1, A2_tfril>;
  def HI : REG_IMMED<".h", 0b0, 0b010, 0b1, A2_tfrih>;
}

let isReMaterializable = 1, isMoveImm = 1, isAsmParserOnly = 1 in {
  def CONST32 : CONSTLDInst<(outs IntRegs:$Rd), (ins i32imm:$v),
                "$Rd = CONST32(#$v)", []>;
  def CONST64 : CONSTLDInst<(outs DoubleRegs:$Rd), (ins i64imm:$v),
                "$Rd = CONST64(#$v)", []>;
}

let hasSideEffects = 0, isReMaterializable = 1, isPseudo = 1,
    isCodeGenOnly = 1 in
def PS_true : InstHexagon<(outs PredRegs:$dst), (ins), "",
              [(set I1:$dst, 1)], "", C2_orn.Itinerary, TypeCR>;

let hasSideEffects = 0, isReMaterializable = 1, isPseudo = 1,
    isCodeGenOnly = 1 in
def PS_false : InstHexagon<(outs PredRegs:$dst), (ins), "",
               [(set I1:$dst, 0)], "", C2_andn.Itinerary, TypeCR>;

let Defs = [R29, R30], Uses = [R31, R30, R29], isPseudo = 1 in
def ADJCALLSTACKDOWN : Pseudo<(outs), (ins i32imm:$amt1, i32imm:$amt2),
                              ".error \"should not emit\" ", []>;

let Defs = [R29, R30, R31], Uses = [R29], isPseudo = 1 in
def ADJCALLSTACKUP : Pseudo<(outs), (ins i32imm:$amt1, i32imm:$amt2),
                             ".error \"should not emit\" ", []>;


let isBranch = 1, isTerminator = 1, hasSideEffects = 0,
    Defs = [PC, LC0], Uses = [SA0, LC0] in {
def ENDLOOP0 : Endloop<(outs), (ins b30_2Imm:$offset),
                       ":endloop0",
                       []>;
}

let isBranch = 1, isTerminator = 1, hasSideEffects = 0,
    Defs = [PC, LC1], Uses = [SA1, LC1] in {
def ENDLOOP1 : Endloop<(outs), (ins b30_2Imm:$offset),
                       ":endloop1",
                       []>;
}

let isBranch = 1, isTerminator = 1, hasSideEffects = 0,
    Defs = [PC, LC0, LC1], Uses = [SA0, SA1, LC0, LC1] in {
def ENDLOOP01 : Endloop<(outs), (ins b30_2Imm:$offset),
                        ":endloop01",
                        []>;
}

let isExtendable = 1, isExtentSigned = 1, opExtentBits = 9, opExtentAlign = 2,
    opExtendable = 0, hasSideEffects = 0 in
class LOOP_iBase<string mnemonic, InstHexagon rootInst>
         : InstHexagon <(outs), (ins b30_2Imm:$offset, u10_0Imm:$src2),
           mnemonic#"($offset,#$src2)",
           [], "", rootInst.Itinerary, rootInst.Type>, OpcodeHexagon {
    bits<9> offset;
    bits<10> src2;

    let IClass = 0b0110;

    let Inst{27-22} = 0b100100;
    let Inst{21} = !if (!eq(mnemonic, "loop0"), 0b0, 0b1);
    let Inst{20-16} = src2{9-5};
    let Inst{12-8} = offset{8-4};
    let Inst{7-5} = src2{4-2};
    let Inst{4-3} = offset{3-2};
    let Inst{1-0} = src2{1-0};
}

let isExtendable = 1, isExtentSigned = 1, opExtentBits = 9, opExtentAlign = 2,
    opExtendable = 0, hasSideEffects = 0 in
class LOOP_rBase<string mnemonic, InstHexagon rootInst>
         : InstHexagon<(outs), (ins b30_2Imm:$offset, IntRegs:$src2),
           mnemonic#"($offset,$src2)",
           [], "", rootInst.Itinerary, rootInst.Type>, OpcodeHexagon {
    bits<9> offset;
    bits<5> src2;

    let IClass = 0b0110;

    let Inst{27-22} = 0b000000;
    let Inst{21} = !if (!eq(mnemonic, "loop0"), 0b0, 0b1);
    let Inst{20-16} = src2;
    let Inst{12-8} = offset{8-4};
    let Inst{4-3} = offset{3-2};
  }

let Defs = [SA0, LC0, USR], isCodeGenOnly = 1, isExtended = 1,
    opExtendable = 0 in {
  def J2_loop0iext : LOOP_iBase<"loop0", J2_loop0i>;
  def J2_loop1iext : LOOP_iBase<"loop1", J2_loop1i>;
}

// Interestingly only loop0's appear to set usr.lpcfg
let Defs = [SA1, LC1], isCodeGenOnly = 1, isExtended = 1, opExtendable = 0 in {
  def J2_loop0rext : LOOP_rBase<"loop0", J2_loop0r>;
  def J2_loop1rext : LOOP_rBase<"loop1", J2_loop1r>;
}

let isCall = 1, hasSideEffects = 1, isPredicable = 0,
    isExtended = 0, isExtendable = 1, opExtendable = 0,
    isExtentSigned = 1, opExtentBits = 24, opExtentAlign = 2 in
class T_Call<string ExtStr>
  : InstHexagon<(outs), (ins a30_2Imm:$dst),
      "call " # ExtStr # "$dst", [], "", J2_call.Itinerary, TypeJ>,
    OpcodeHexagon {
  let BaseOpcode = "call";
  bits<24> dst;

  let IClass = 0b0101;
  let Inst{27-25} = 0b101;
  let Inst{24-16,13-1} = dst{23-2};
  let Inst{0} = 0b0;
}

let isCodeGenOnly = 1, isCall = 1, hasSideEffects = 1, Defs = [R16],
    isPredicable = 0 in
def CALLProfile :  T_Call<"">;

let isCodeGenOnly = 1, isCall = 1, hasSideEffects = 1,
    Defs = [PC, R31, R6, R7, P0] in
def PS_call_stk : T_Call<"">;

// This pseudo instruction is used to replace int_hexagon_instrprof_custom intrinsic
// with a call to custom handler passed as the first argument to the intrinsic.

// Pleae Note:
// 1) The call to the custom handler is being treated as a special one as the
//    callee is responsible for saving and restoring all the registers it needs
//    to modify. This includes caller saved registers as well as r0-r5 argument
//    registers. This is done to reduce the impact of instrumentation on the
//    code being instrumented/profiled.
// 2) R14, R15 and R28 are reserved for PLT handling and therefore are
//    part of the def list.
// 3) R0 is used to pass the unique id associated with an instrumentation site
//    to the handler.
// 4) All the other registers (R29, R30, R31, PC) get modified by the call
//    instruction.

// TODO: It may be a good idea to add a separate pseudo instruction for
// static relocation which doesn't need to reserve r14, r15 and r28.

let hasSideEffects = 1, isCall = 1, Defs = [R0, R14, R15, R28, R29, R30, R31, PC] in
def PS_call_instrprof_custom :  Pseudo<(outs), (ins s32_0Imm:$dst, u32_0Imm:$Ii), "">;

// Call, no return.
let isCall = 1, hasSideEffects = 1, cofMax1 = 1, isCodeGenOnly = 1 in
def PS_callr_nr: InstHexagon<(outs), (ins IntRegs:$Rs),
    "callr $Rs", [], "", J2_callr.Itinerary, TypeJ>, OpcodeHexagon {
    bits<5> Rs;
    bits<2> Pu;
    let isPredicatedFalse = 1;

    let IClass = 0b0101;
    let Inst{27-21} = 0b0000101;
    let Inst{20-16} = Rs;
  }

let isCall = 1, hasSideEffects = 1,
    isExtended = 0, isExtendable = 1, opExtendable = 0, isCodeGenOnly = 1,
    BaseOpcode = "PS_call_nr", isExtentSigned = 1, opExtentAlign = 2 in
class Call_nr<bits<5> nbits, bit isFalse, dag iops,
              InstrItinClass itin>
  : Pseudo<(outs), iops, "">, PredRel {
    bits<2> Pu;
    bits<17> dst;
    let opExtentBits = nbits;
    let isPredicable = 0;  // !if(isPred, 0, 1);
    let isPredicated = 0;  // isPred;
    let isPredicatedFalse = isFalse;
    let Itinerary = itin;
}

def PS_call_nr : Call_nr<24, 0, (ins s32_0Imm:$Ii), J2_call.Itinerary>;
//def PS_call_nrt: Call_nr<17, 1, 0, (ins PredRegs:$Pu, s32_0Imm:$dst),
//                         J2_callt.Itinerary>;
//def PS_call_nrf: Call_nr<17, 1, 1, (ins PredRegs:$Pu, s32_0Imm:$dst),
//                         J2_callf.Itinerary>;

let isBranch = 1, isIndirectBranch = 1, isBarrier = 1, Defs = [PC],
    isPredicable = 1, hasSideEffects = 0, InputType = "reg",
    cofMax1 = 1 in
class T_JMPr <InstHexagon rootInst>
  :  InstHexagon<(outs), (ins IntRegs:$dst), "jumpr $dst", [],
                 "", rootInst.Itinerary, rootInst.Type>, OpcodeHexagon {
    bits<5> dst;

    let IClass = 0b0101;
    let Inst{27-21} = 0b0010100;
    let Inst{20-16} = dst;
}

// A return through builtin_eh_return.
let isReturn = 1, isTerminator = 1, isBarrier = 1, hasSideEffects = 0,
    isCodeGenOnly = 1, Defs = [PC], Uses = [R28], isPredicable = 0 in
def EH_RETURN_JMPR : T_JMPr<J2_jumpr>;

// Indirect tail-call.
let isPseudo = 1, isCall = 1, isReturn = 1, isBarrier = 1, isPredicable = 0,
    isTerminator = 1, isCodeGenOnly = 1 in
def PS_tailcall_r : T_JMPr<J2_jumpr>;

//
// Direct tail-calls.
let isPseudo = 1, isCall = 1, isReturn = 1, isBarrier = 1, isPredicable = 0,
    isTerminator = 1, isCodeGenOnly = 1 in
def PS_tailcall_i : Pseudo<(outs), (ins a30_2Imm:$dst), "", []>;

let isCodeGenOnly = 1, isPseudo = 1, Uses = [R30], hasSideEffects = 0 in
def PS_aligna : Pseudo<(outs IntRegs:$Rd), (ins u32_0Imm:$A), "", []>;

// Generate frameindex addresses. The main reason for the offset operand is
// that every instruction that is allowed to have frame index as an operand
// will then have that operand followed by an immediate operand (the offset).
// This simplifies the frame-index elimination code.
//
let isMoveImm = 1, isAsCheapAsAMove = 1, isReMaterializable = 1,
    isPseudo = 1, isCodeGenOnly = 1, hasSideEffects = 0, isExtendable = 1,
    isExtentSigned = 1, opExtentBits = 16, opExtentAlign = 0 in {
  let opExtendable = 2 in
  def PS_fi  : Pseudo<(outs IntRegs:$Rd),
                      (ins IntRegs:$fi, s32_0Imm:$off), "">;
  let opExtendable = 3 in
  def PS_fia : Pseudo<(outs IntRegs:$Rd),
                      (ins IntRegs:$Rs, IntRegs:$fi, s32_0Imm:$off), "">;
}

class CondStr<string CReg, bit True, bit New> {
  string S = "if (" # !if(True,"","!") # CReg # !if(New,".new","") # ") ";
}
class JumpOpcStr<string Mnemonic, bit Taken> {
  string S = Mnemonic # !if(Taken, ":t", ":nt");
}
let isBranch = 1, isIndirectBranch = 1, Defs = [PC], isPredicated = 1,
    hasSideEffects = 0, InputType = "reg", cofMax1 = 1 in
class T_JMPr_c <bit PredNot, bit isPredNew, bit isTak, InstHexagon rootInst>
  :  InstHexagon<(outs), (ins PredRegs:$src, IntRegs:$dst),
                 CondStr<"$src", !if(PredNot,0,1), isPredNew>.S #
                 JumpOpcStr<"jumpr", isTak>.S # " $dst",
                 [], "", rootInst.Itinerary, rootInst.Type>, OpcodeHexagon {

    let isTaken = isTak;
    let isPredicatedFalse = PredNot;
    let isPredicatedNew = isPredNew;
    bits<2> src;
    bits<5> dst;

    let IClass = 0b0101;

    let Inst{27-22} = 0b001101;
    let Inst{21} = PredNot;
    let Inst{20-16} = dst;
    let Inst{12} = isTak;
    let Inst{11} = isPredNew;
    let Inst{9-8} = src;
}

let isTerminator = 1, hasSideEffects = 0, isReturn = 1, isCodeGenOnly = 1,
    isBarrier = 1, BaseOpcode = "JMPret" in {
  def PS_jmpret : T_JMPr<J2_jumpr>, PredNewRel;
  def PS_jmprett : T_JMPr_c<0, 0, 0, J2_jumprt>, PredNewRel;
  def PS_jmpretf : T_JMPr_c<1, 0, 0, J2_jumprf>, PredNewRel;
  def PS_jmprettnew : T_JMPr_c<0, 1, 0, J2_jumprtnew>, PredNewRel;
  def PS_jmpretfnew : T_JMPr_c<1, 1, 0, J2_jumprfnew>, PredNewRel;
  def PS_jmprettnewpt : T_JMPr_c<0, 1, 1, J2_jumprtnewpt>, PredNewRel;
  def PS_jmpretfnewpt : T_JMPr_c<1, 1, 1, J2_jumprfnewpt>, PredNewRel;
}

//defm V6_vtran2x2_map : HexagonMapping<(outs HvxVR:$Vy32, HvxVR:$Vx32), (ins HvxVR:$Vx32in, IntRegs:$Rt32), "vtrans2x2(${Vy32},${Vx32},${Rt32})", (V6_vshuff HvxVR:$Vy32, HvxVR:$Vx32, HvxVR:$Vx32in, IntRegs:$Rt32)>;

// The reason for the custom inserter is to record all ALLOCA instructions
// in MachineFunctionInfo.
let Defs = [R29], hasSideEffects = 1 in
def PS_alloca: Pseudo <(outs IntRegs:$Rd),
                       (ins IntRegs:$Rs, u32_0Imm:$A), "", []>;

// Load predicate.
let isExtendable = 1, opExtendable = 2, isExtentSigned = 1, opExtentBits = 13,
    isCodeGenOnly = 1, isPseudo = 1, hasSideEffects = 0 in
def LDriw_pred : LDInst<(outs PredRegs:$dst),
                        (ins IntRegs:$addr, s32_0Imm:$off),
                        ".error \"should not emit\"", []>;

// Load modifier.
let isExtendable = 1, opExtendable = 2, isExtentSigned = 1, opExtentBits = 13,
    isCodeGenOnly = 1, isPseudo = 1, hasSideEffects = 0 in
def LDriw_ctr : LDInst<(outs CtrRegs:$dst),
                        (ins IntRegs:$addr, s32_0Imm:$off),
                        ".error \"should not emit\"", []>;


let isCodeGenOnly = 1, isPseudo = 1 in
def PS_pselect: InstHexagon<(outs DoubleRegs:$Rd),
      (ins PredRegs:$Pu, DoubleRegs:$Rs, DoubleRegs:$Rt),
      ".error \"should not emit\" ", [], "", A2_tfrpt.Itinerary, TypeALU32_2op>;

let isBranch = 1, isBarrier = 1, Defs = [PC], hasSideEffects = 0,
    isPredicable = 1,
    isExtendable = 1, opExtendable = 0, isExtentSigned = 1,
    opExtentBits = 24, opExtentAlign = 2, InputType = "imm" in
class T_JMP: InstHexagon<(outs), (ins b30_2Imm:$dst),
      "jump $dst",
      [], "", J2_jump.Itinerary, TypeJ>, OpcodeHexagon {
    bits<24> dst;
    let IClass = 0b0101;

    let Inst{27-25} = 0b100;
    let Inst{24-16} = dst{23-15};
    let Inst{13-1} = dst{14-2};
}

// Restore registers and dealloc return function call.
let isCall = 1, isBarrier = 1, isReturn = 1, isTerminator = 1,
    Defs = [R29, R30, R31, PC], isPredicable = 0, isAsmParserOnly = 1 in {
  def RESTORE_DEALLOC_RET_JMP_V4 : T_JMP;

  let isExtended = 1, opExtendable = 0 in
  def RESTORE_DEALLOC_RET_JMP_V4_EXT : T_JMP;

  let Defs = [R14, R15, R28, R29, R30, R31, PC] in {
    def RESTORE_DEALLOC_RET_JMP_V4_PIC : T_JMP;

    let isExtended = 1, opExtendable = 0 in
    def RESTORE_DEALLOC_RET_JMP_V4_EXT_PIC : T_JMP;
  }
}

// Restore registers and dealloc frame before a tail call.
let isCall = 1, Defs = [R29, R30, R31, PC], isAsmParserOnly = 1 in {
  def RESTORE_DEALLOC_BEFORE_TAILCALL_V4 : T_Call<"">, PredRel;

  let isExtended = 1, opExtendable = 0 in
  def RESTORE_DEALLOC_BEFORE_TAILCALL_V4_EXT : T_Call<"">, PredRel;

  let Defs = [R14, R15, R28, R29, R30, R31, PC] in {
    def RESTORE_DEALLOC_BEFORE_TAILCALL_V4_PIC : T_Call<"">, PredRel;

    let isExtended = 1, opExtendable = 0 in
    def RESTORE_DEALLOC_BEFORE_TAILCALL_V4_EXT_PIC : T_Call<"">, PredRel;
  }
}

// Save registers function call.
let isCall = 1, Uses = [R29, R31], isAsmParserOnly = 1 in {
  def SAVE_REGISTERS_CALL_V4 : T_Call<"">, PredRel;

  let isExtended = 1, opExtendable = 0 in
  def SAVE_REGISTERS_CALL_V4_EXT : T_Call<"">, PredRel;

  let Defs = [P0] in
  def SAVE_REGISTERS_CALL_V4STK : T_Call<"">, PredRel;

  let Defs = [P0], isExtended = 1, opExtendable = 0 in
  def SAVE_REGISTERS_CALL_V4STK_EXT : T_Call<"">, PredRel;

  let Defs = [R14, R15, R28] in
  def SAVE_REGISTERS_CALL_V4_PIC : T_Call<"">, PredRel;

  let Defs = [R14, R15, R28], isExtended = 1, opExtendable = 0 in
  def SAVE_REGISTERS_CALL_V4_EXT_PIC : T_Call<"">, PredRel;

  let Defs = [R14, R15, R28, P0] in
  def SAVE_REGISTERS_CALL_V4STK_PIC : T_Call<"">, PredRel;

  let Defs = [R14, R15, R28, P0], isExtended = 1, opExtendable = 0 in
  def SAVE_REGISTERS_CALL_V4STK_EXT_PIC : T_Call<"">, PredRel;
}

let Predicates = [UseHVX], isPseudo = 1, isCodeGenOnly = 1,
                 hasSideEffects = 0, hasPostISelHook = 1 in
class Vsplatr_template : InstHexagon<(outs HvxVR:$Vd), (ins IntRegs:$Rs),
                         "", [], "", V6_lvsplatw.Itinerary, V6_lvsplatw.Type>;
def PS_vsplatrb: Vsplatr_template;
def PS_vsplatrh: Vsplatr_template;
def PS_vsplatrw: Vsplatr_template;

let Predicates = [UseHVX], isPseudo = 1, isCodeGenOnly = 1,
                 hasSideEffects = 0, hasPostISelHook = 1 in
class Vsplati_template : InstHexagon<(outs HvxVR:$Vd), (ins s32_0Imm:$Val),
                         "", [], "", V6_lvsplatw.Itinerary, V6_lvsplatw.Type>;
def PS_vsplatib: Vsplati_template;
def PS_vsplatih: Vsplati_template;
def PS_vsplatiw: Vsplati_template;

// Vector store pseudos
let Predicates = [HasV60,UseHVX], isPseudo = 1, isCodeGenOnly = 1,
    mayStore = 1, accessSize = HVXVectorAccess, hasSideEffects = 0 in
class STriv_template<RegisterClass RC, InstHexagon rootInst>
  : InstHexagon<(outs), (ins IntRegs:$addr, s32_0Imm:$off, RC:$src),
    "", [], "", rootInst.Itinerary, rootInst.Type>;

def PS_vstorerv_ai: STriv_template<HvxVR, V6_vS32b_ai>,
      Requires<[HasV60,UseHVX]>;
def PS_vstorerv_nt_ai: STriv_template<HvxVR, V6_vS32b_nt_ai>,
      Requires<[HasV60,UseHVX]>;
def PS_vstorerw_ai: STriv_template<HvxWR, V6_vS32b_ai>,
      Requires<[HasV60,UseHVX]>;
def PS_vstorerw_nt_ai: STriv_template<HvxWR, V6_vS32b_nt_ai>,
      Requires<[HasV60,UseHVX]>;

let isPseudo = 1, isCodeGenOnly = 1, mayStore = 1, hasSideEffects = 0 in
def PS_vstorerq_ai: Pseudo<(outs),
      (ins IntRegs:$Rs, s32_0Imm:$Off, HvxQR:$Qt), "", []>,
      Requires<[HasV60,UseHVX]>;

// Vector load pseudos
let Predicates = [HasV60, UseHVX], isPseudo = 1, isCodeGenOnly = 1,
    mayLoad = 1, accessSize = HVXVectorAccess, hasSideEffects = 0 in
class LDriv_template<RegisterClass RC, InstHexagon rootInst>
  : InstHexagon<(outs RC:$dst), (ins IntRegs:$addr, s32_0Imm:$off),
    "", [], "", rootInst.Itinerary, rootInst.Type>;

def PS_vloadrv_ai: LDriv_template<HvxVR, V6_vL32b_ai>,
      Requires<[HasV60,UseHVX]>;
def PS_vloadrv_nt_ai: LDriv_template<HvxVR, V6_vL32b_nt_ai>,
      Requires<[HasV60,UseHVX]>;
def PS_vloadrw_ai: LDriv_template<HvxWR, V6_vL32b_ai>,
      Requires<[HasV60,UseHVX]>;
def PS_vloadrw_nt_ai: LDriv_template<HvxWR, V6_vL32b_nt_ai>,
      Requires<[HasV60,UseHVX]>;

let isPseudo = 1, isCodeGenOnly = 1, mayLoad = 1, hasSideEffects = 0 in
def PS_vloadrq_ai: Pseudo<(outs HvxQR:$Qd),
      (ins IntRegs:$Rs, s32_0Imm:$Off), "", []>,
      Requires<[HasV60,UseHVX]>;


let isCodeGenOnly = 1, isPseudo = 1, hasSideEffects = 0 in
class VSELInst<dag outs, dag ins, InstHexagon rootInst>
  : InstHexagon<outs, ins, "", [], "", rootInst.Itinerary, rootInst.Type>;

def PS_vselect: VSELInst<(outs HvxVR:$dst),
      (ins PredRegs:$src1, HvxVR:$src2, HvxVR:$src3), V6_vcmov>,
      Requires<[HasV60,UseHVX]>;
def PS_wselect: VSELInst<(outs HvxWR:$dst),
      (ins PredRegs:$src1, HvxWR:$src2, HvxWR:$src3), V6_vccombine>,
      Requires<[HasV60,UseHVX]>;

let hasSideEffects = 0, isReMaterializable = 1, isPseudo = 1,
    isCodeGenOnly = 1 in {
  def PS_qtrue:  InstHexagon<(outs HvxQR:$Qd), (ins), "", [], "",
                 V6_veqw.Itinerary, TypeCVI_VA>;
  def PS_qfalse: InstHexagon<(outs HvxQR:$Qd), (ins), "", [], "",
                 V6_vgtw.Itinerary, TypeCVI_VA>;
  def PS_vdd0:   InstHexagon<(outs HvxWR:$Vd), (ins), "", [], "",
                 V6_vsubw_dv.Itinerary, TypeCVI_VA_DV>;
}

// Store predicate.
let isExtendable = 1, opExtendable = 1, isExtentSigned = 1, opExtentBits = 13,
    isCodeGenOnly = 1, isPseudo = 1, hasSideEffects = 0 in
def STriw_pred : STInst<(outs),
      (ins IntRegs:$addr, s32_0Imm:$off, PredRegs:$src1),
      ".error \"should not emit\"", []>;
// Store modifier.
let isExtendable = 1, opExtendable = 1, isExtentSigned = 1, opExtentBits = 13,
    isCodeGenOnly = 1, isPseudo = 1, hasSideEffects = 0 in
def STriw_ctr : STInst<(outs),
      (ins IntRegs:$addr, s32_0Imm:$off, CtrRegs:$src1),
      ".error \"should not emit\"", []>;

let isExtendable = 1, opExtendable = 1, opExtentBits = 6,
    isAsmParserOnly = 1 in
def TFRI64_V4 : InstHexagon<(outs DoubleRegs:$dst),
    (ins u64_0Imm:$src1),
    "$dst = #$src1", [], "",
    A2_combineii.Itinerary, TypeALU32_2op>, OpcodeHexagon;

// Hexagon doesn't have a vector multiply with C semantics.
// Instead, generate a pseudo instruction that gets expanded into two
// scalar MPYI instructions.
// This is expanded by ExpandPostRAPseudos.
let isPseudo = 1 in
def PS_vmulw : PseudoM<(outs DoubleRegs:$Rd),
      (ins DoubleRegs:$Rs, DoubleRegs:$Rt), "", []>;

let isPseudo = 1 in
def PS_vmulw_acc : PseudoM<(outs DoubleRegs:$Rd),
      (ins DoubleRegs:$Rx, DoubleRegs:$Rs, DoubleRegs:$Rt), "", [],
      "$Rd = $Rx">;

def DuplexIClass0:  InstDuplex < 0 >;
def DuplexIClass1:  InstDuplex < 1 >;
def DuplexIClass2:  InstDuplex < 2 >;
let isExtendable = 1 in {
  def DuplexIClass3:  InstDuplex < 3 >;
  def DuplexIClass4:  InstDuplex < 4 >;
  def DuplexIClass5:  InstDuplex < 5 >;
  def DuplexIClass6:  InstDuplex < 6 >;
  def DuplexIClass7:  InstDuplex < 7 >;
}
def DuplexIClass8:  InstDuplex < 8 >;
def DuplexIClass9:  InstDuplex < 9 >;
def DuplexIClassA:  InstDuplex < 0xA >;
def DuplexIClassB:  InstDuplex < 0xB >;
def DuplexIClassC:  InstDuplex < 0xC >;
def DuplexIClassD:  InstDuplex < 0xD >;
def DuplexIClassE:  InstDuplex < 0xE >;
def DuplexIClassF:  InstDuplex < 0xF >;

// Pseudos for circular buffer instructions. These are needed in order to
// allocate the correct pair of CSx and Mx registers.
multiclass NewCircularLoad<RegisterClass RC, MemAccessSize MS> {

let isCodeGenOnly = 1, isPseudo = 1, Defs = [CS], Uses = [CS],
    addrMode = PostInc, accessSize = MS, hasSideEffects = 0 in {
  // Use timing class of L2_loadrb_pci.
  def NAME#_pci : LDInst<(outs RC:$Rd32, IntRegs:$Rx32),
       (ins IntRegs:$Rx32in, s4_0Imm:$Ii, ModRegs:$Mu2, IntRegs:$Cs),
       ".error \"should not emit\" ", [], "$Rx32 = $Rx32in", tc_5ceb2f9e>;

  // Use timing class of L2_loadrb_pcr.
  def NAME#_pcr : LDInst<(outs RC:$Rd32, IntRegs:$Rx32),
       (ins IntRegs:$Rx32in, ModRegs:$Mu2, IntRegs:$Cs),
       ".error \"should not emit\" ", [], "$Rx32 = $Rx32in", tc_075c8dd8>;
}
}

defm PS_loadrub : NewCircularLoad<IntRegs, ByteAccess>;
defm PS_loadrb : NewCircularLoad<IntRegs, ByteAccess>;
defm PS_loadruh : NewCircularLoad<IntRegs, HalfWordAccess>;
defm PS_loadrh : NewCircularLoad<IntRegs, HalfWordAccess>;
defm PS_loadri : NewCircularLoad<IntRegs, WordAccess>;
defm PS_loadrd : NewCircularLoad<DoubleRegs, DoubleWordAccess>;

multiclass NewCircularStore<RegisterClass RC, MemAccessSize MS> {

let isCodeGenOnly = 1, isPseudo = 1, Defs = [CS], Uses = [CS],
    addrMode = PostInc, accessSize = MS, hasSideEffects = 0 in {
  // Use timing class of S2_storerb_pci.
  def NAME#_pci : STInst<(outs IntRegs:$Rx32),
       (ins IntRegs:$Rx32in, s4_0Imm:$Ii, ModRegs:$Mu2, RC:$Rt32, IntRegs:$Cs),
       ".error \"should not emit\" ", [], "$Rx32 = $Rx32in", tc_b4dc7630>;

  // Use timing class of S2_storerb_pcr.
  def NAME#_pcr : STInst<(outs IntRegs:$Rx32),
       (ins IntRegs:$Rx32in, ModRegs:$Mu2, RC:$Rt32, IntRegs:$Cs),
       ".error \"should not emit\" ", [], "$Rx32 = $Rx32in", tc_a2b365d2>;
}
}

defm PS_storerb : NewCircularStore<IntRegs, ByteAccess>;
defm PS_storerh : NewCircularStore<IntRegs, HalfWordAccess>;
defm PS_storerf : NewCircularStore<IntRegs, HalfWordAccess>;
defm PS_storeri : NewCircularStore<IntRegs, WordAccess>;
defm PS_storerd : NewCircularStore<DoubleRegs, WordAccess>;

// A pseudo that generates a runtime crash. This is used to implement
// __builtin_trap.
let hasSideEffects = 1, isPseudo = 1, isCodeGenOnly = 1, isSolo = 1 in
def PS_crash: InstHexagon<(outs), (ins), "", [], "", PSEUDO, TypePSEUDO>;

// This is actual trap1 instruction from before v65. It's here since it is
// no longer included in DepInstrInfo.td.
def PS_trap1 : HInst<(outs), (ins u8_0Imm:$Ii), "trap1(#$Ii)", tc_53c851ab,
                     TypeJ>, Enc_a51a9a, Requires<[HasPreV65]> {
  let Inst{1-0} = 0b00;
  let Inst{7-5} = 0b000;
  let Inst{13-13} = 0b0;
  let Inst{31-16} = 0b0101010010000000;
}


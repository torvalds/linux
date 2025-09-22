//===-- PPCInstr64Bit.td - The PowerPC 64-bit Support ------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the PowerPC 64-bit instructions.  These patterns are used
// both when in ppc64 mode and when in "use 64-bit extensions in 32-bit" mode.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// 64-bit operands.
//
def s16imm64 : Operand<i64> {
  let PrintMethod = "printS16ImmOperand";
  let EncoderMethod = "getImm16Encoding";
  let ParserMatchClass = PPCS16ImmAsmOperand;
  let DecoderMethod = "decodeSImmOperand<16>";
  let OperandType = "OPERAND_IMMEDIATE";
}
def u16imm64 : Operand<i64> {
  let PrintMethod = "printU16ImmOperand";
  let EncoderMethod = "getImm16Encoding";
  let ParserMatchClass = PPCU16ImmAsmOperand;
  let DecoderMethod = "decodeUImmOperand<16>";
  let OperandType = "OPERAND_IMMEDIATE";
}
def s17imm64 : Operand<i64> {
  // This operand type is used for addis/lis to allow the assembler parser
  // to accept immediates in the range -65536..65535 for compatibility with
  // the GNU assembler.  The operand is treated as 16-bit otherwise.
  let PrintMethod = "printS16ImmOperand";
  let EncoderMethod = "getImm16Encoding";
  let ParserMatchClass = PPCS17ImmAsmOperand;
  let DecoderMethod = "decodeSImmOperand<16>";
  let OperandType = "OPERAND_IMMEDIATE";
}
def tocentry : Operand<iPTR> {
  let MIOperandInfo = (ops i64imm:$imm);
}
def tlsreg : Operand<i64> {
  let EncoderMethod = "getTLSRegEncoding";
  let ParserMatchClass = PPCTLSRegOperand;
}
def tlsgd : Operand<i64> {}
def tlscall : Operand<i64> {
  let PrintMethod = "printTLSCall";
  let MIOperandInfo = (ops calltarget:$func, tlsgd:$sym);
  let EncoderMethod = "getTLSCallEncoding";
}

//===----------------------------------------------------------------------===//
// 64-bit transformation functions.
//

def SHL64 : SDNodeXForm<imm, [{
  // Transformation function: 63 - imm
  return getI32Imm(63 - N->getZExtValue(), SDLoc(N));
}]>;

def SRL64 : SDNodeXForm<imm, [{
  // Transformation function: 64 - imm
  return N->getZExtValue() ? getI32Imm(64 - N->getZExtValue(), SDLoc(N))
                           : getI32Imm(0, SDLoc(N));
}]>;


//===----------------------------------------------------------------------===//
// Calls.
//

let Interpretation64Bit = 1, isCodeGenOnly = 1 in {
let isTerminator = 1, isBarrier = 1, PPC970_Unit = 7, hasSideEffects = 0 in {
  let isReturn = 1, isPredicable = 1, Uses = [LR8, RM] in
    def BLR8 : XLForm_2_ext<19, 16, 20, 0, 0, (outs), (ins), "blr", IIC_BrB,
                            [(PPCretglue)]>, Requires<[In64BitMode]>;
  let isBranch = 1, isIndirectBranch = 1, Uses = [CTR8] in {
    let isPredicable = 1 in
      def BCTR8 : XLForm_2_ext<19, 528, 20, 0, 0, (outs), (ins), "bctr", IIC_BrB,
                               []>,
          Requires<[In64BitMode]>;
    def BCCCTR8 : XLForm_2_br<19, 528, 0, (outs), (ins (pred $BIBO, $CR):$cond),
                              "b${cond:cc}ctr${cond:pm} ${cond:reg}", IIC_BrB,
                              []>,
        Requires<[In64BitMode]>;

    def BCCTR8  : XLForm_2_br2<19, 528, 12, 0, (outs), (ins crbitrc:$BI),
                               "bcctr 12, $BI, 0", IIC_BrB, []>,
        Requires<[In64BitMode]>;
    def BCCTR8n : XLForm_2_br2<19, 528, 4, 0, (outs), (ins crbitrc:$BI),
                               "bcctr 4, $BI, 0", IIC_BrB, []>,
        Requires<[In64BitMode]>;
  }
}

let Defs = [LR8] in
  def MovePCtoLR8 : PPCEmitTimePseudo<(outs), (ins), "#MovePCtoLR8", []>,
                    PPC970_Unit_BRU;

let isBranch = 1, isTerminator = 1, hasCtrlDep = 1, PPC970_Unit = 7, hasSideEffects = 0 in {
  let Defs = [CTR8], Uses = [CTR8] in {
    def BDZ8  : BForm_1<16, 18, 0, 0, (outs), (ins condbrtarget:$BD),
                        "bdz $BD">;
    def BDNZ8 : BForm_1<16, 16, 0, 0, (outs), (ins condbrtarget:$BD),
                        "bdnz $BD">;
  }

  let isReturn = 1, Defs = [CTR8], Uses = [CTR8, LR8, RM] in {
    def BDZLR8  : XLForm_2_ext<19, 16, 18, 0, 0, (outs), (ins),
                              "bdzlr", IIC_BrB, []>;
    def BDNZLR8 : XLForm_2_ext<19, 16, 16, 0, 0, (outs), (ins),
                              "bdnzlr", IIC_BrB, []>;
  }
}



let isCall = 1, PPC970_Unit = 7, Defs = [LR8], hasSideEffects = 0 in {
  // Convenient aliases for call instructions
  let Uses = [RM] in {
    def BL8  : IForm<18, 0, 1, (outs), (ins calltarget:$LI),
                     "bl $LI", IIC_BrB, []>;  // See Pat patterns below.

    def BL8_TLS  : IForm<18, 0, 1, (outs), (ins tlscall:$LI),
                         "bl $LI", IIC_BrB, []>;

    def BLA8 : IForm<18, 1, 1, (outs), (ins abscalltarget:$LI),
                     "bla $LI", IIC_BrB, [(PPCcall (i64 imm:$LI))]>;
  }
  let Uses = [RM], isCodeGenOnly = 1 in {
    def BL8_NOP  : IForm_and_DForm_4_zero<18, 0, 1, 24,
                             (outs), (ins calltarget:$LI),
                             "bl $LI\n\tnop", IIC_BrB, []>;

    def BL8_NOP_TLS : IForm_and_DForm_4_zero<18, 0, 1, 24,
                                  (outs), (ins tlscall:$LI),
                                  "bl $LI\n\tnop", IIC_BrB, []>;

    def BLA8_NOP : IForm_and_DForm_4_zero<18, 1, 1, 24,
                             (outs), (ins abscalltarget:$LI),
                             "bla $LI\n\tnop", IIC_BrB,
                             [(PPCcall_nop (i64 imm:$LI))]>;
    let Predicates = [PCRelativeMemops] in {
      // BL8_NOTOC means that the caller does not use the TOC pointer and if
      // it does use R2 then it is just a caller saved register. Therefore it is
      // safe to emit only the bl and not the nop for this instruction. The
      // linker will not try to restore R2 after the call.
      def BL8_NOTOC : IForm<18, 0, 1, (outs),
                            (ins calltarget:$LI),
                            "bl $LI", IIC_BrB, []>;
      def BL8_NOTOC_TLS : IForm<18, 0, 1, (outs),
                                (ins tlscall:$LI),
                                "bl $LI", IIC_BrB, []>;
    }
  }
  let Uses = [CTR8, RM] in {
    let isPredicable = 1 in
      def BCTRL8 : XLForm_2_ext<19, 528, 20, 0, 1, (outs), (ins),
                                "bctrl", IIC_BrB, [(PPCbctrl)]>,
                   Requires<[In64BitMode]>;

    let isCodeGenOnly = 1 in {
      def BCCCTRL8 : XLForm_2_br<19, 528, 1, (outs), (ins (pred $BIBO, $CR):$cond),
                                 "b${cond:cc}ctrl${cond:pm} ${cond:reg}", IIC_BrB,
                                 []>,
          Requires<[In64BitMode]>;

      def BCCTRL8  : XLForm_2_br2<19, 528, 12, 1, (outs), (ins crbitrc:$BI),
                                  "bcctrl 12, $BI, 0", IIC_BrB, []>,
          Requires<[In64BitMode]>;
      def BCCTRL8n : XLForm_2_br2<19, 528, 4, 1, (outs), (ins crbitrc:$BI),
                                  "bcctrl 4, $BI, 0", IIC_BrB, []>,
          Requires<[In64BitMode]>;
    }
  }
}

let isCall = 1, PPC970_Unit = 7, Defs = [LR8, RM], hasSideEffects = 0,
    isCodeGenOnly = 1, Uses = [RM] in {
  // Convenient aliases for call instructions
  def BL8_RM  : IForm<18, 0, 1, (outs), (ins calltarget:$LI),
                      "bl $LI", IIC_BrB, []>;  // See Pat patterns below.

  def BLA8_RM : IForm<18, 1, 1, (outs), (ins abscalltarget:$LI),
                      "bla $LI", IIC_BrB, [(PPCcall_rm (i64 imm:$LI))]>;
  def BL8_NOP_RM  : IForm_and_DForm_4_zero<18, 0, 1, 24,
                           (outs), (ins calltarget:$LI),
                           "bl $LI\n\tnop", IIC_BrB, []>;

  def BLA8_NOP_RM : IForm_and_DForm_4_zero<18, 1, 1, 24,
                           (outs), (ins abscalltarget:$LI),
                           "bla $LI\n\tnop", IIC_BrB,
                           [(PPCcall_nop_rm (i64 imm:$LI))]>;
  let Predicates = [PCRelativeMemops] in {
    // BL8_NOTOC means that the caller does not use the TOC pointer and if
    // it does use R2 then it is just a caller saved register. Therefore it is
    // safe to emit only the bl and not the nop for this instruction. The
    // linker will not try to restore R2 after the call.
    def BL8_NOTOC_RM : IForm<18, 0, 1, (outs),
                             (ins calltarget:$LI),
                             "bl $LI", IIC_BrB, []>;
  }
  let Uses = [CTR8, RM] in {
    let isPredicable = 1 in
      def BCTRL8_RM : XLForm_2_ext<19, 528, 20, 0, 1, (outs), (ins),
                                   "bctrl", IIC_BrB, [(PPCbctrl_rm)]>,
                   Requires<[In64BitMode]>;
  }
}

let isCall = 1, PPC970_Unit = 7, isCodeGenOnly = 1,
    Defs = [LR8, X2], Uses = [CTR8, RM], RST = 2 in {
  def BCTRL8_LDinto_toc :
    XLForm_2_ext_and_DSForm_1<19, 528, 20, 0, 1, 58, 0, (outs),
                              (ins (memrix $D, $RA):$src),
                              "bctrl\n\tld 2, $src", IIC_BrB,
                              [(PPCbctrl_load_toc iaddrX4:$src)]>,
    Requires<[In64BitMode]>;
}

let isCall = 1, PPC970_Unit = 7, isCodeGenOnly = 1,
    Defs = [LR8, X2, RM], Uses = [CTR8, RM], RST = 2 in {
  def BCTRL8_LDinto_toc_RM :
    XLForm_2_ext_and_DSForm_1<19, 528, 20, 0, 1, 58, 0, (outs),
                              (ins (memrix $D, $RA):$src),
                              "bctrl\n\tld 2, $src", IIC_BrB,
                              [(PPCbctrl_load_toc_rm iaddrX4:$src)]>,
    Requires<[In64BitMode]>;
}

} // Interpretation64Bit

// FIXME: Duplicating this for the asm parser should be unnecessary, but the
// previous definition must be marked as CodeGen only to prevent decoding
// conflicts.
let Interpretation64Bit = 1, isAsmParserOnly = 1, hasSideEffects = 0 in
let isCall = 1, PPC970_Unit = 7, Defs = [LR8], Uses = [RM] in
def BL8_TLS_ : IForm<18, 0, 1, (outs), (ins tlscall:$LI),
                     "bl $LI", IIC_BrB, []>;

// Calls
def : Pat<(PPCcall (i64 tglobaladdr:$dst)),
          (BL8 tglobaladdr:$dst)>;
def : Pat<(PPCcall_nop (i64 tglobaladdr:$dst)),
          (BL8_NOP tglobaladdr:$dst)>;

def : Pat<(PPCcall (i64 texternalsym:$dst)),
          (BL8 texternalsym:$dst)>;
def : Pat<(PPCcall_nop (i64 texternalsym:$dst)),
          (BL8_NOP texternalsym:$dst)>;

def : Pat<(PPCcall_notoc (i64 tglobaladdr:$dst)),
          (BL8_NOTOC tglobaladdr:$dst)>;
def : Pat<(PPCcall_notoc (i64 texternalsym:$dst)),
          (BL8_NOTOC texternalsym:$dst)>;

def : Pat<(PPCcall_rm (i64 tglobaladdr:$dst)),
          (BL8_RM tglobaladdr:$dst)>;
def : Pat<(PPCcall_nop_rm (i64 tglobaladdr:$dst)),
          (BL8_NOP_RM tglobaladdr:$dst)>;

def : Pat<(PPCcall_rm (i64 texternalsym:$dst)),
          (BL8_RM texternalsym:$dst)>;
def : Pat<(PPCcall_nop_rm (i64 texternalsym:$dst)),
          (BL8_NOP_RM texternalsym:$dst)>;

def : Pat<(PPCcall_notoc_rm (i64 tglobaladdr:$dst)),
          (BL8_NOTOC_RM tglobaladdr:$dst)>;
def : Pat<(PPCcall_notoc_rm (i64 texternalsym:$dst)),
          (BL8_NOTOC_RM texternalsym:$dst)>;

// Calls for AIX
def : Pat<(PPCcall (i64 mcsym:$dst)),
          (BL8 mcsym:$dst)>;
def : Pat<(PPCcall_nop (i64 mcsym:$dst)),
          (BL8_NOP mcsym:$dst)>;

def : Pat<(PPCcall_rm (i64 mcsym:$dst)),
          (BL8_RM mcsym:$dst)>;
def : Pat<(PPCcall_nop_rm (i64 mcsym:$dst)),
          (BL8_NOP_RM mcsym:$dst)>;

// Atomic operations
// FIXME: some of these might be used with constant operands. This will result
// in constant materialization instructions that may be redundant. We currently
// clean this up in PPCMIPeephole with calls to
// PPCInstrInfo::convertToImmediateForm() but we should probably not emit them
// in the first place.
let Defs = [CR0] in {
  def ATOMIC_LOAD_ADD_I64 : PPCCustomInserterPseudo<
    (outs g8rc:$dst), (ins memrr:$ptr, g8rc:$incr), "#ATOMIC_LOAD_ADD_I64",
    [(set i64:$dst, (atomic_load_add_i64 ForceXForm:$ptr, i64:$incr))]>;
  def ATOMIC_LOAD_SUB_I64 : PPCCustomInserterPseudo<
    (outs g8rc:$dst), (ins memrr:$ptr, g8rc:$incr), "#ATOMIC_LOAD_SUB_I64",
    [(set i64:$dst, (atomic_load_sub_i64 ForceXForm:$ptr, i64:$incr))]>;
  def ATOMIC_LOAD_OR_I64 : PPCCustomInserterPseudo<
    (outs g8rc:$dst), (ins memrr:$ptr, g8rc:$incr), "#ATOMIC_LOAD_OR_I64",
    [(set i64:$dst, (atomic_load_or_i64 ForceXForm:$ptr, i64:$incr))]>;
  def ATOMIC_LOAD_XOR_I64 : PPCCustomInserterPseudo<
    (outs g8rc:$dst), (ins memrr:$ptr, g8rc:$incr), "#ATOMIC_LOAD_XOR_I64",
    [(set i64:$dst, (atomic_load_xor_i64 ForceXForm:$ptr, i64:$incr))]>;
  def ATOMIC_LOAD_AND_I64 : PPCCustomInserterPseudo<
    (outs g8rc:$dst), (ins memrr:$ptr, g8rc:$incr), "#ATOMIC_LOAD_AND_i64",
    [(set i64:$dst, (atomic_load_and_i64 ForceXForm:$ptr, i64:$incr))]>;
  def ATOMIC_LOAD_NAND_I64 : PPCCustomInserterPseudo<
    (outs g8rc:$dst), (ins memrr:$ptr, g8rc:$incr), "#ATOMIC_LOAD_NAND_I64",
    [(set i64:$dst, (atomic_load_nand_i64 ForceXForm:$ptr, i64:$incr))]>;
  def ATOMIC_LOAD_MIN_I64 : PPCCustomInserterPseudo<
    (outs g8rc:$dst), (ins memrr:$ptr, g8rc:$incr), "#ATOMIC_LOAD_MIN_I64",
    [(set i64:$dst, (atomic_load_min_i64 ForceXForm:$ptr, i64:$incr))]>;
  def ATOMIC_LOAD_MAX_I64 : PPCCustomInserterPseudo<
    (outs g8rc:$dst), (ins memrr:$ptr, g8rc:$incr), "#ATOMIC_LOAD_MAX_I64",
    [(set i64:$dst, (atomic_load_max_i64 ForceXForm:$ptr, i64:$incr))]>;
  def ATOMIC_LOAD_UMIN_I64 : PPCCustomInserterPseudo<
    (outs g8rc:$dst), (ins memrr:$ptr, g8rc:$incr), "#ATOMIC_LOAD_UMIN_I64",
    [(set i64:$dst, (atomic_load_umin_i64 ForceXForm:$ptr, i64:$incr))]>;
  def ATOMIC_LOAD_UMAX_I64 : PPCCustomInserterPseudo<
    (outs g8rc:$dst), (ins memrr:$ptr, g8rc:$incr), "#ATOMIC_LOAD_UMAX_I64",
    [(set i64:$dst, (atomic_load_umax_i64 ForceXForm:$ptr, i64:$incr))]>;

  def ATOMIC_CMP_SWAP_I64 : PPCCustomInserterPseudo<
    (outs g8rc:$dst), (ins memrr:$ptr, g8rc:$old, g8rc:$new), "#ATOMIC_CMP_SWAP_I64",
    [(set i64:$dst, (atomic_cmp_swap_i64 ForceXForm:$ptr, i64:$old, i64:$new))]>;

  def ATOMIC_SWAP_I64 : PPCCustomInserterPseudo<
    (outs g8rc:$dst), (ins memrr:$ptr, g8rc:$new), "#ATOMIC_SWAP_I64",
    [(set i64:$dst, (atomic_swap_i64 ForceXForm:$ptr, i64:$new))]>;
}

// Instructions to support atomic operations
let mayLoad = 1, hasSideEffects = 0 in {
def LDARX : XForm_1_memOp<31,  84, (outs g8rc:$RST), (ins (memrr $RA, $RB):$addr),
                          "ldarx $RST, $addr", IIC_LdStLDARX, []>;
// TODO: Add scheduling info.
let hasNoSchedulingInfo = 1 in
def LQARX : XForm_1_memOp<31, 276, (outs g8prc:$RST), (ins (memrr $RA, $RB):$addr),
                          "lqarx $RST, $addr", IIC_LdStLQARX, []>, isPPC64;

// Instruction to support lock versions of atomics
// (EH=1 - see Power ISA 2.07 Book II 4.4.2)
def LDARXL : XForm_1<31,  84, (outs g8rc:$RST), (ins (memrr $RA, $RB):$addr),
                     "ldarx $RST, $addr, 1", IIC_LdStLDARX, []>, isRecordForm;
// TODO: Add scheduling info.
let hasNoSchedulingInfo = 1 in
// FIXME: We have to seek a way to remove isRecordForm since
// LQARXL is not really altering CR0.
def LQARXL : XForm_1<31, 276, (outs g8prc:$RST), (ins (memrr $RA, $RB):$addr),
                     "lqarx $RST, $addr, 1", IIC_LdStLQARX, []>,
                     isPPC64, isRecordForm;

let hasExtraDefRegAllocReq = 1 in
def LDAT : X_RD5_RS5_IM5<31, 614, (outs g8rc:$RST), (ins g8rc:$RA, u5imm:$RB),
                         "ldat $RST, $RA, $RB", IIC_LdStLoad>, isPPC64,
           Requires<[IsISA3_0]>;
}

let Defs = [CR0], mayStore = 1, mayLoad = 0, hasSideEffects = 0 in {
def STDCX : XForm_1_memOp<31, 214, (outs), (ins g8rc:$RST, (memrr $RA, $RB):$addr),
                          "stdcx. $RST, $addr", IIC_LdStSTDCX, []>, isRecordForm;
// TODO: Add scheduling info.
let hasNoSchedulingInfo = 1 in
def STQCX : XForm_1_memOp<31, 182, (outs), (ins g8prc:$RST, (memrr $RA, $RB):$addr),
                          "stqcx. $RST, $addr", IIC_LdStSTQCX, []>,
                          isPPC64, isRecordForm;
}

def SPLIT_QUADWORD : PPCCustomInserterPseudo<(outs g8rc:$lo, g8rc:$hi),
                                             (ins g8prc:$src),
                                             "#SPLIT_QUADWORD", []>;
class AtomicRMW128<string asmstr>
  : PPCPostRAExpPseudo<(outs g8prc:$RTp, g8prc:$scratch),
                       (ins memrr:$ptr, g8rc:$incr_lo, g8rc:$incr_hi),
                       asmstr, []>;
// We have to keep values in MI's uses during LL/SC looping as they are,
// so set both $RTp and $scratch earlyclobber.
let mayStore = 1, mayLoad = 1,
    Defs = [CR0],
    Constraints = "@earlyclobber $scratch,@earlyclobber $RTp" in {
// Atomic pseudo instructions expanded post-ra.
def ATOMIC_SWAP_I128      : AtomicRMW128<"#ATOMIC_SWAP_I128">;
def ATOMIC_LOAD_ADD_I128  : AtomicRMW128<"#ATOMIC_LOAD_ADD_I128">;
def ATOMIC_LOAD_SUB_I128  : AtomicRMW128<"#ATOMIC_LOAD_SUB_I128">;
def ATOMIC_LOAD_AND_I128  : AtomicRMW128<"#ATOMIC_LOAD_AND_I128">;
def ATOMIC_LOAD_XOR_I128  : AtomicRMW128<"#ATOMIC_LOAD_XOR_I128">;
def ATOMIC_LOAD_OR_I128   : AtomicRMW128<"#ATOMIC_LOAD_OR_I128">;
def ATOMIC_LOAD_NAND_I128 : AtomicRMW128<"#ATOMIC_LOAD_NAND_I128">;

def ATOMIC_CMP_SWAP_I128 : PPCPostRAExpPseudo<
                              (outs g8prc:$RTp, g8prc:$scratch),
                              (ins memrr:$ptr, g8rc:$cmp_lo, g8rc:$cmp_hi,
                                   g8rc:$new_lo, g8rc:$new_hi),
                              "#ATOMIC_CMP_SWAP_I128", []>;
}

class PatAtomicRMWI128<SDPatternOperator OpNode, AtomicRMW128 Inst> :
      Pat<(OpNode ForceXForm:$ptr,
                  i64:$incr_lo,
                  i64:$incr_hi),
          (SPLIT_QUADWORD (Inst memrr:$ptr,
                                g8rc:$incr_lo,
                                g8rc:$incr_hi))>;

def : PatAtomicRMWI128<int_ppc_atomicrmw_add_i128,  ATOMIC_LOAD_ADD_I128>;
def : PatAtomicRMWI128<int_ppc_atomicrmw_sub_i128,  ATOMIC_LOAD_SUB_I128>;
def : PatAtomicRMWI128<int_ppc_atomicrmw_xor_i128,  ATOMIC_LOAD_XOR_I128>;
def : PatAtomicRMWI128<int_ppc_atomicrmw_and_i128,  ATOMIC_LOAD_AND_I128>;
def : PatAtomicRMWI128<int_ppc_atomicrmw_nand_i128, ATOMIC_LOAD_NAND_I128>;
def : PatAtomicRMWI128<int_ppc_atomicrmw_or_i128,   ATOMIC_LOAD_OR_I128>;
def : PatAtomicRMWI128<int_ppc_atomicrmw_xchg_i128, ATOMIC_SWAP_I128>;
def : Pat<(int_ppc_cmpxchg_i128 ForceXForm:$ptr,
                                i64:$cmp_lo,
                                i64:$cmp_hi,
                                i64:$new_lo,
                                i64:$new_hi),
          (SPLIT_QUADWORD (ATOMIC_CMP_SWAP_I128
                           memrr:$ptr,
                           g8rc:$cmp_lo,
                           g8rc:$cmp_hi,
                           g8rc:$new_lo,
                           g8rc:$new_hi))>;

let mayStore = 1, mayLoad = 0, hasSideEffects = 0 in
def STDAT : X_RD5_RS5_IM5<31, 742, (outs), (ins g8rc:$RST, g8rc:$RA, u5imm:$RB),
                          "stdat $RST, $RA, $RB", IIC_LdStStore>, isPPC64,
            Requires<[IsISA3_0]>;

let Interpretation64Bit = 1, isCodeGenOnly = 1 in {
let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1, Uses = [RM] in
def TCRETURNdi8 :PPCEmitTimePseudo< (outs),
                        (ins calltarget:$dst, i32imm:$offset),
                 "#TC_RETURNd8 $dst $offset",
                 []>;

let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1, Uses = [RM] in
def TCRETURNai8 :PPCEmitTimePseudo<(outs), (ins abscalltarget:$func, i32imm:$offset),
                 "#TC_RETURNa8 $func $offset",
                 [(PPCtc_return (i64 imm:$func), imm:$offset)]>;

let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1, Uses = [RM] in
def TCRETURNri8 : PPCEmitTimePseudo<(outs), (ins CTRRC8:$dst, i32imm:$offset),
                 "#TC_RETURNr8 $dst $offset",
                 []>;

let hasSideEffects = 0 in {
let isTerminator = 1, isBarrier = 1, PPC970_Unit = 7, isBranch = 1,
    isIndirectBranch = 1, isCall = 1, isReturn = 1, Uses = [CTR8, RM] in
def TAILBCTR8 : XLForm_2_ext<19, 528, 20, 0, 0, (outs), (ins), "bctr", IIC_BrB,
                             []>,
    Requires<[In64BitMode]>;

let isBranch = 1, isTerminator = 1, hasCtrlDep = 1, PPC970_Unit = 7,
    isBarrier = 1, isCall = 1, isReturn = 1, Uses = [RM] in
def TAILB8   : IForm<18, 0, 0, (outs), (ins calltarget:$LI),
                  "b $LI", IIC_BrB,
                  []>;

let isBranch = 1, isTerminator = 1, hasCtrlDep = 1, PPC970_Unit = 7,
    isBarrier = 1, isCall = 1, isReturn = 1, Uses = [RM] in
def TAILBA8   : IForm<18, 0, 0, (outs), (ins abscalltarget:$LI),
                  "ba $LI", IIC_BrB,
                  []>;
}
} // Interpretation64Bit

def : Pat<(PPCtc_return (i64 tglobaladdr:$dst),  imm:$imm),
          (TCRETURNdi8 tglobaladdr:$dst, imm:$imm)>;

def : Pat<(PPCtc_return (i64 texternalsym:$dst), imm:$imm),
          (TCRETURNdi8 texternalsym:$dst, imm:$imm)>;

def : Pat<(PPCtc_return CTRRC8:$dst, imm:$imm),
          (TCRETURNri8 CTRRC8:$dst, imm:$imm)>;


// 64-bit CR instructions
let Interpretation64Bit = 1, isCodeGenOnly = 1 in {
let hasSideEffects = 0 in {
// mtocrf's input needs to be prepared by shifting by an amount dependent
// on the cr register selected. Thus, post-ra anti-dep breaking must not
// later change that register assignment.
let hasExtraDefRegAllocReq = 1 in {
def MTOCRF8: XFXForm_5a<31, 144, (outs crbitm:$FXM), (ins g8rc:$RST),
                        "mtocrf $FXM, $RST", IIC_BrMCRX>,
            PPC970_DGroup_First, PPC970_Unit_CRU;

// Similarly to mtocrf, the mask for mtcrf must be prepared in a way that
// is dependent on the cr fields being set.
def MTCRF8 : XFXForm_5<31, 144, (outs), (ins i32imm:$FXM, g8rc:$RST),
                      "mtcrf $FXM, $RST", IIC_BrMCRX>,
            PPC970_MicroCode, PPC970_Unit_CRU;
} // hasExtraDefRegAllocReq = 1

// mfocrf's input needs to be prepared by shifting by an amount dependent
// on the cr register selected. Thus, post-ra anti-dep breaking must not
// later change that register assignment.
let hasExtraSrcRegAllocReq = 1 in {
def MFOCRF8: XFXForm_5a<31, 19, (outs g8rc:$RST), (ins crbitm:$FXM),
                        "mfocrf $RST, $FXM", IIC_SprMFCRF>,
             PPC970_DGroup_First, PPC970_Unit_CRU;

// Similarly to mfocrf, the mask for mfcrf must be prepared in a way that
// is dependent on the cr fields being copied.
def MFCR8 : XFXForm_3<31, 19, (outs g8rc:$RT), (ins),
                     "mfcr $RT", IIC_SprMFCR>,
                     PPC970_MicroCode, PPC970_Unit_CRU;
} // hasExtraSrcRegAllocReq = 1
} // hasSideEffects = 0

// While longjmp is a control-flow barrier (fallthrough isn't allowed), setjmp
// is not.
let hasSideEffects = 1 in {
  let Defs = [CTR8] in
  def EH_SjLj_SetJmp64  : PPCCustomInserterPseudo<(outs gprc:$dst), (ins memr:$buf),
                            "#EH_SJLJ_SETJMP64",
                            [(set i32:$dst, (PPCeh_sjlj_setjmp addr:$buf))]>,
                          Requires<[In64BitMode]>;
}

let hasSideEffects = 1, isBarrier = 1 in {
  let isTerminator = 1 in
  def EH_SjLj_LongJmp64 : PPCCustomInserterPseudo<(outs), (ins memr:$buf),
                            "#EH_SJLJ_LONGJMP64",
                            [(PPCeh_sjlj_longjmp addr:$buf)]>,
                          Requires<[In64BitMode]>;
}

def MFSPR8 : XFXForm_1<31, 339, (outs g8rc:$RST), (ins i32imm:$SPR),
                       "mfspr $RST, $SPR", IIC_SprMFSPR>;
def MTSPR8 : XFXForm_1<31, 467, (outs), (ins i32imm:$SPR, g8rc:$RST),
                       "mtspr $SPR, $RST", IIC_SprMTSPR>;


//===----------------------------------------------------------------------===//
// 64-bit SPR manipulation instrs.

let Uses = [CTR8] in {
def MFCTR8 : XFXForm_1_ext<31, 339, 9, (outs g8rc:$RST), (ins),
                           "mfctr $RST", IIC_SprMFSPR>,
             PPC970_DGroup_First, PPC970_Unit_FXU;
}
let Pattern = [(PPCmtctr i64:$RST)], Defs = [CTR8] in {
def MTCTR8 : XFXForm_1_ext<31, 467, 9, (outs), (ins g8rc:$RST),
                           "mtctr $RST", IIC_SprMTSPR>,
             PPC970_DGroup_First, PPC970_Unit_FXU;
}
// MTCTR[8|]loop must be inside a loop-preheader, duplicating
// the loop-preheader block will break this assumption.
let hasSideEffects = 1, isNotDuplicable = 1, Defs = [CTR8] in {
let Pattern = [(int_set_loop_iterations i64:$RST)] in
def MTCTR8loop : XFXForm_1_ext<31, 467, 9, (outs), (ins g8rc:$RST),
                               "mtctr $RST", IIC_SprMTSPR>,
                 PPC970_DGroup_First, PPC970_Unit_FXU;
}

let hasSideEffects = 1, hasNoSchedulingInfo = 1, isNotDuplicable = 1, Uses = [CTR8], Defs = [CTR8] in
def DecreaseCTR8loop : PPCEmitTimePseudo<(outs crbitrc:$rT), (ins i64imm:$stride),
                                        "#DecreaseCTR8loop", [(set i1:$rT, (int_loop_decrement (i64 imm:$stride)))]>;

let Pattern = [(set i64:$RST, readcyclecounter)] in
def MFTB8 : XFXForm_1_ext<31, 339, 268, (outs g8rc:$RST), (ins),
                          "mfspr $RST, 268", IIC_SprMFTB>,
            PPC970_DGroup_First, PPC970_Unit_FXU;
// Note that encoding mftb using mfspr is now the preferred form,
// and has been since at least ISA v2.03. The mftb instruction has
// now been phased out. Using mfspr, however, is known not to work on
// the POWER3.

let Defs = [X1], Uses = [X1] in
def DYNALLOC8 : PPCEmitTimePseudo<(outs g8rc:$result), (ins g8rc:$negsize, memri:$fpsi),"#DYNALLOC8",
                       [(set i64:$result,
                             (PPCdynalloc i64:$negsize, iaddr:$fpsi))]>;
def DYNAREAOFFSET8 : PPCEmitTimePseudo<(outs i64imm:$result), (ins memri:$fpsi), "#DYNAREAOFFSET8",
                       [(set i64:$result, (PPCdynareaoffset iaddr:$fpsi))]>;
// Probed alloca to support stack clash protection.
let Defs = [X1], Uses = [X1], hasNoSchedulingInfo = 1 in {
def PROBED_ALLOCA_64 : PPCCustomInserterPseudo<(outs g8rc:$result),
                         (ins g8rc:$negsize, memri:$fpsi), "#PROBED_ALLOCA_64",
                           [(set i64:$result,
                             (PPCprobedalloca i64:$negsize, iaddr:$fpsi))]>;
def PREPARE_PROBED_ALLOCA_64 : PPCEmitTimePseudo<(outs
    g8rc:$fp, g8rc:$actual_negsize),
    (ins g8rc:$negsize, memri:$fpsi), "#PREPARE_PROBED_ALLOCA_64", []>;
def PREPARE_PROBED_ALLOCA_NEGSIZE_SAME_REG_64 : PPCEmitTimePseudo<(outs
    g8rc:$fp, g8rc:$actual_negsize),
    (ins g8rc:$negsize, memri:$fpsi),
    "#PREPARE_PROBED_ALLOCA_NEGSIZE_SAME_REG_64", []>,
    RegConstraint<"$actual_negsize = $negsize">;
def PROBED_STACKALLOC_64 : PPCEmitTimePseudo<(outs g8rc:$scratch, g8rc:$temp),
    (ins i64imm:$stacksize),
    "#PROBED_STACKALLOC_64", []>;
}

let hasSideEffects = 0 in {
let Defs = [LR8] in {
def MTLR8  : XFXForm_1_ext<31, 467, 8, (outs), (ins g8rc:$RST),
                           "mtlr $RST", IIC_SprMTSPR>,
             PPC970_DGroup_First, PPC970_Unit_FXU;
}
let Uses = [LR8] in {
def MFLR8  : XFXForm_1_ext<31, 339, 8, (outs g8rc:$RST), (ins),
                           "mflr $RST", IIC_SprMFSPR>,
             PPC970_DGroup_First, PPC970_Unit_FXU;
}
} // Interpretation64Bit
}

//===----------------------------------------------------------------------===//
// Fixed point instructions.
//

let PPC970_Unit = 1 in {  // FXU Operations.
let Interpretation64Bit = 1 in {
let hasSideEffects = 0 in {
let isCodeGenOnly = 1 in {

let isReMaterializable = 1, isAsCheapAsAMove = 1, isMoveImm = 1 in {
def LI8  : DForm_2_r0<14, (outs g8rc:$RST), (ins s16imm64:$D),
                      "li $RST, $D", IIC_IntSimple,
                      [(set i64:$RST, imm64SExt16:$D)]>, SExt32To64;
def LIS8 : DForm_2_r0<15, (outs g8rc:$RST), (ins s17imm64:$D),
                      "lis $RST, $D", IIC_IntSimple,
                      [(set i64:$RST, imm16ShiftedSExt:$D)]>, SExt32To64;
}

// Logical ops.
let isCommutable = 1 in {
defm NAND8: XForm_6r<31, 476, (outs g8rc:$RA), (ins g8rc:$RST, g8rc:$RB),
                     "nand", "$RA, $RST, $RB", IIC_IntSimple,
                     [(set i64:$RA, (not (and i64:$RST, i64:$RB)))]>;
defm AND8 : XForm_6r<31,  28, (outs g8rc:$RA), (ins g8rc:$RST, g8rc:$RB),
                     "and", "$RA, $RST, $RB", IIC_IntSimple,
                     [(set i64:$RA, (and i64:$RST, i64:$RB))]>;
} // isCommutable
defm ANDC8: XForm_6r<31,  60, (outs g8rc:$RA), (ins g8rc:$RST, g8rc:$RB),
                     "andc", "$RA, $RST, $RB", IIC_IntSimple,
                     [(set i64:$RA, (and i64:$RST, (not i64:$RB)))]>;
let isCommutable = 1 in {
defm OR8  : XForm_6r<31, 444, (outs g8rc:$RA), (ins g8rc:$RST, g8rc:$RB),
                     "or", "$RA, $RST, $RB", IIC_IntSimple,
                     [(set i64:$RA, (or i64:$RST, i64:$RB))]>;
defm NOR8 : XForm_6r<31, 124, (outs g8rc:$RA), (ins g8rc:$RST, g8rc:$RB),
                     "nor", "$RA, $RST, $RB", IIC_IntSimple,
                     [(set i64:$RA, (not (or i64:$RST, i64:$RB)))]>;
} // isCommutable
defm ORC8 : XForm_6r<31, 412, (outs g8rc:$RA), (ins g8rc:$RST, g8rc:$RB),
                     "orc", "$RA, $RST, $RB", IIC_IntSimple,
                     [(set i64:$RA, (or i64:$RST, (not i64:$RB)))]>;
let isCommutable = 1 in {
defm EQV8 : XForm_6r<31, 284, (outs g8rc:$RA), (ins g8rc:$RST, g8rc:$RB),
                     "eqv", "$RA, $RST, $RB", IIC_IntSimple,
                     [(set i64:$RA, (not (xor i64:$RST, i64:$RB)))]>;
defm XOR8 : XForm_6r<31, 316, (outs g8rc:$RA), (ins g8rc:$RST, g8rc:$RB),
                     "xor", "$RA, $RST, $RB", IIC_IntSimple,
                     [(set i64:$RA, (xor i64:$RST, i64:$RB))]>;
} // let isCommutable = 1

// Logical ops with immediate.
let Defs = [CR0] in {
def ANDI8_rec  : DForm_4<28, (outs g8rc:$RA), (ins g8rc:$RST, u16imm64:$D),
                      "andi. $RA, $RST, $D", IIC_IntGeneral,
                      [(set i64:$RA, (and i64:$RST, immZExt16:$D))]>,
                      isRecordForm, SExt32To64, ZExt32To64;
def ANDIS8_rec : DForm_4<29, (outs g8rc:$RA), (ins g8rc:$RST, u16imm64:$D),
                     "andis. $RA, $RST, $D", IIC_IntGeneral,
                    [(set i64:$RA, (and i64:$RST, imm16ShiftedZExt:$D))]>,
                     isRecordForm, ZExt32To64;
}
def ORI8    : DForm_4<24, (outs g8rc:$RA), (ins g8rc:$RST, u16imm64:$D),
                      "ori $RA, $RST, $D", IIC_IntSimple,
                      [(set i64:$RA, (or i64:$RST, immZExt16:$D))]>;
def ORIS8   : DForm_4<25, (outs g8rc:$RA), (ins g8rc:$RST, u16imm64:$D),
                      "oris $RA, $RST, $D", IIC_IntSimple,
                    [(set i64:$RA, (or i64:$RST, imm16ShiftedZExt:$D))]>;
def XORI8   : DForm_4<26, (outs g8rc:$RA), (ins g8rc:$RST, u16imm64:$D),
                      "xori $RA, $RST, $D", IIC_IntSimple,
                      [(set i64:$RA, (xor i64:$RST, immZExt16:$D))]>;
def XORIS8  : DForm_4<27, (outs g8rc:$RA), (ins g8rc:$RST, u16imm64:$D),
                      "xoris $RA, $RST, $D", IIC_IntSimple,
                   [(set i64:$RA, (xor i64:$RST, imm16ShiftedZExt:$D))]>;

let isCommutable = 1 in
defm ADD8  : XOForm_1rx<31, 266, (outs g8rc:$RT), (ins g8rc:$RA, g8rc:$RB),
                        "add", "$RT, $RA, $RB", IIC_IntSimple,
                        [(set i64:$RT, (add i64:$RA, i64:$RB))]>;
// ADD8 has a special form: reg = ADD8(reg, sym@tls) for use by the
// initial-exec thread-local storage model.  We need to forbid r0 here -
// while it works for add just fine, the linker can relax this to local-exec
// addi, which won't work for r0.
def ADD8TLS  : XOForm_1<31, 266, 0, (outs g8rc:$RT), (ins g8rc_nox0:$RA, tlsreg:$RB),
                        "add $RT, $RA, $RB", IIC_IntSimple,
                        [(set i64:$RT, (add i64:$RA, tglobaltlsaddr:$RB))]>;
let mayLoad = 1 in {
def LBZXTLS : XForm_1<31,  87, (outs g8rc:$RST), (ins ptr_rc_nor0:$RA, tlsreg:$RB),
                      "lbzx $RST, $RA, $RB", IIC_LdStLoad, []>;
def LHZXTLS : XForm_1<31, 279, (outs g8rc:$RST), (ins ptr_rc_nor0:$RA, tlsreg:$RB),
                      "lhzx $RST, $RA, $RB", IIC_LdStLoad, []>;
def LHAXTLS : XForm_1<31, 343, (outs g8rc:$RST), (ins ptr_rc_nor0:$RA, tlsreg:$RB),
                      "lhax $RST, $RA, $RB", IIC_LdStLoad, []>;
def LWZXTLS : XForm_1<31,  23, (outs g8rc:$RST), (ins ptr_rc_nor0:$RA, tlsreg:$RB),
                      "lwzx $RST, $RA, $RB", IIC_LdStLoad, []>;
def LWAXTLS : XForm_1<31, 341, (outs g8rc:$RST), (ins ptr_rc_nor0:$RA, tlsreg:$RB),
                      "lwax $RST, $RA, $RB", IIC_LdStLoad, []>;
def LDXTLS  : XForm_1<31,  21, (outs g8rc:$RST), (ins ptr_rc_nor0:$RA, tlsreg:$RB),
                      "ldx $RST, $RA, $RB", IIC_LdStLD, []>, isPPC64;
def LBZXTLS_32 : XForm_1<31,  87, (outs gprc:$RST), (ins ptr_rc_nor0:$RA, tlsreg:$RB),
                         "lbzx $RST, $RA, $RB", IIC_LdStLoad, []>;
def LHZXTLS_32 : XForm_1<31, 279, (outs gprc:$RST), (ins ptr_rc_nor0:$RA, tlsreg:$RB),
                         "lhzx $RST, $RA, $RB", IIC_LdStLoad, []>;
def LHAXTLS_32 : XForm_1<31, 343, (outs gprc:$RST), (ins ptr_rc_nor0:$RA, tlsreg:$RB),
                         "lhax $RST, $RA, $RB", IIC_LdStLoad, []>;
def LWZXTLS_32 : XForm_1<31,  23, (outs gprc:$RST), (ins ptr_rc_nor0:$RA, tlsreg:$RB),
                         "lwzx $RST, $RA, $RB", IIC_LdStLoad, []>;
def LWAXTLS_32 : XForm_1<31, 341, (outs gprc:$RST), (ins ptr_rc_nor0:$RA, tlsreg:$RB),
                         "lwax $RST, $RA, $RB", IIC_LdStLoad, []>;

}
let mayLoad = 1, Predicates = [HasFPU] in {
def LFSXTLS : XForm_25<31, 535, (outs f4rc:$RST), (ins ptr_rc_nor0:$RA, tlsreg:$RB),
                       "lfsx $RST, $RA, $RB", IIC_LdStLFD, []>;
def LFDXTLS : XForm_25<31, 599, (outs f8rc:$RST), (ins ptr_rc_nor0:$RA, tlsreg:$RB),
                       "lfdx $RST, $RA, $RB", IIC_LdStLFD, []>;
}

let mayStore = 1 in {
def STBXTLS : XForm_8<31, 215, (outs), (ins g8rc:$RST, ptr_rc_nor0:$RA, tlsreg:$RB),
                      "stbx $RST, $RA, $RB", IIC_LdStStore, []>,
                      PPC970_DGroup_Cracked;
def STHXTLS : XForm_8<31, 407, (outs), (ins g8rc:$RST, ptr_rc_nor0:$RA, tlsreg:$RB),
                      "sthx $RST, $RA, $RB", IIC_LdStStore, []>,
                      PPC970_DGroup_Cracked;
def STWXTLS : XForm_8<31, 151, (outs), (ins g8rc:$RST, ptr_rc_nor0:$RA, tlsreg:$RB),
                      "stwx $RST, $RA, $RB", IIC_LdStStore, []>,
                      PPC970_DGroup_Cracked;
def STDXTLS  : XForm_8<31, 149, (outs), (ins g8rc:$RST, ptr_rc_nor0:$RA, tlsreg:$RB),
                       "stdx $RST, $RA, $RB", IIC_LdStSTD, []>, isPPC64,
                       PPC970_DGroup_Cracked;
def STBXTLS_32 : XForm_8<31, 215, (outs), (ins gprc:$RST, ptr_rc_nor0:$RA, tlsreg:$RB),
                         "stbx $RST, $RA, $RB", IIC_LdStStore, []>,
                         PPC970_DGroup_Cracked;
def STHXTLS_32 : XForm_8<31, 407, (outs), (ins gprc:$RST, ptr_rc_nor0:$RA, tlsreg:$RB),
                         "sthx $RST, $RA, $RB", IIC_LdStStore, []>,
                         PPC970_DGroup_Cracked;
def STWXTLS_32 : XForm_8<31, 151, (outs), (ins gprc:$RST, ptr_rc_nor0:$RA, tlsreg:$RB),
                         "stwx $RST, $RA, $RB", IIC_LdStStore, []>,
                         PPC970_DGroup_Cracked;

}
let mayStore = 1, Predicates = [HasFPU] in {
def STFSXTLS : XForm_8<31, 663, (outs), (ins f4rc:$RST, ptr_rc_nor0:$RA, tlsreg:$RB),
                       "stfsx $RST, $RA, $RB", IIC_LdStSTFD, []>,
                       PPC970_DGroup_Cracked;
def STFDXTLS : XForm_8<31, 727, (outs), (ins f8rc:$RST, ptr_rc_nor0:$RA, tlsreg:$RB),
                       "stfdx $RST, $RA, $RB", IIC_LdStSTFD, []>,
                       PPC970_DGroup_Cracked;
}

let isCommutable = 1 in
defm ADDC8 : XOForm_1rc<31, 10, 0, (outs g8rc:$RT), (ins g8rc:$RA, g8rc:$RB),
                        "addc", "$RT, $RA, $RB", IIC_IntGeneral,
                        [(set i64:$RT, (addc i64:$RA, i64:$RB))]>,
                        PPC970_DGroup_Cracked;

let Defs = [CARRY] in
def ADDIC8 : DForm_2<12, (outs g8rc:$RST), (ins g8rc:$RA, s16imm64:$D),
                     "addic $RST, $RA, $D", IIC_IntGeneral,
                     [(set i64:$RST, (addc i64:$RA, imm64SExt16:$D))]>;
def ADDI8  : DForm_2<14, (outs g8rc:$RST), (ins g8rc_nox0:$RA, s16imm64:$D),
                     "addi $RST, $RA, $D", IIC_IntSimple,
                     [(set i64:$RST, (add i64:$RA, imm64SExt16:$D))]>;
def ADDIS8 : DForm_2<15, (outs g8rc:$RST), (ins g8rc_nox0:$RA, s17imm64:$D),
                     "addis $RST, $RA, $D", IIC_IntSimple,
                     [(set i64:$RST, (add i64:$RA, imm16ShiftedSExt:$D))]>;

def LA8     : DForm_2<14, (outs g8rc:$RST), (ins g8rc_nox0:$RA, s16imm64:$D),
                     "la $RST, $D($RA)", IIC_IntGeneral,
                     [(set i64:$RST, (add i64:$RA,
                                    (PPClo tglobaladdr:$D, 0)))]>;

let Defs = [CARRY] in {
def SUBFIC8: DForm_2< 8, (outs g8rc:$RST), (ins g8rc:$RA, s16imm64:$D),
                     "subfic $RST, $RA, $D", IIC_IntGeneral,
                     [(set i64:$RST, (subc imm64SExt16:$D, i64:$RA))]>;
}
defm SUBFC8 : XOForm_1rc<31, 8, 0, (outs g8rc:$RT), (ins g8rc:$RA, g8rc:$RB),
                        "subfc", "$RT, $RA, $RB", IIC_IntGeneral,
                        [(set i64:$RT, (subc i64:$RB, i64:$RA))]>,
                        PPC970_DGroup_Cracked;
defm SUBF8 : XOForm_1rx<31, 40, (outs g8rc:$RT), (ins g8rc:$RA, g8rc:$RB),
                        "subf", "$RT, $RA, $RB", IIC_IntGeneral,
                        [(set i64:$RT, (sub i64:$RB, i64:$RA))]>;
defm NEG8    : XOForm_3r<31, 104, 0, (outs g8rc:$RT), (ins g8rc:$RA),
                        "neg", "$RT, $RA", IIC_IntSimple,
                        [(set i64:$RT, (ineg i64:$RA))]>;
let Uses = [CARRY] in {
let isCommutable = 1 in
defm ADDE8   : XOForm_1rc<31, 138, 0, (outs g8rc:$RT), (ins g8rc:$RA, g8rc:$RB),
                          "adde", "$RT, $RA, $RB", IIC_IntGeneral,
                          [(set i64:$RT, (adde i64:$RA, i64:$RB))]>;
defm ADDME8  : XOForm_3rc<31, 234, 0, (outs g8rc:$RT), (ins g8rc:$RA),
                          "addme", "$RT, $RA", IIC_IntGeneral,
                          [(set i64:$RT, (adde i64:$RA, -1))]>;
defm ADDZE8  : XOForm_3rc<31, 202, 0, (outs g8rc:$RT), (ins g8rc:$RA),
                          "addze", "$RT, $RA", IIC_IntGeneral,
                          [(set i64:$RT, (adde i64:$RA, 0))]>;
defm SUBFE8  : XOForm_1rc<31, 136, 0, (outs g8rc:$RT), (ins g8rc:$RA, g8rc:$RB),
                          "subfe", "$RT, $RA, $RB", IIC_IntGeneral,
                          [(set i64:$RT, (sube i64:$RB, i64:$RA))]>;
defm SUBFME8 : XOForm_3rc<31, 232, 0, (outs g8rc:$RT), (ins g8rc:$RA),
                          "subfme", "$RT, $RA", IIC_IntGeneral,
                          [(set i64:$RT, (sube -1, i64:$RA))]>;
defm SUBFZE8 : XOForm_3rc<31, 200, 0, (outs g8rc:$RT), (ins g8rc:$RA),
                          "subfze", "$RT, $RA", IIC_IntGeneral,
                          [(set i64:$RT, (sube 0, i64:$RA))]>;
}
} // isCodeGenOnly

// FIXME: Duplicating this for the asm parser should be unnecessary, but the
// previous definition must be marked as CodeGen only to prevent decoding
// conflicts.
let isAsmParserOnly = 1 in {
def ADD8TLS_ : XOForm_1<31, 266, 0, (outs g8rc:$RT), (ins g8rc:$RA, tlsreg:$RB),
                        "add $RT, $RA, $RB", IIC_IntSimple, []>;

let mayLoad = 1 in {
def LBZXTLS_ : XForm_1<31,  87, (outs g8rc:$RST), (ins ptr_rc_nor0:$RA, tlsreg:$RB),
                      "lbzx $RST, $RA, $RB", IIC_LdStLoad, []>;
def LHZXTLS_ : XForm_1<31, 279, (outs g8rc:$RST), (ins ptr_rc_nor0:$RA, tlsreg:$RB),
                      "lhzx $RST, $RA, $RB", IIC_LdStLoad, []>;
def LHAXTLS_ : XForm_1<31, 343, (outs g8rc:$RST), (ins ptr_rc_nor0:$RA, tlsreg:$RB),
                       "lhax $RST, $RA, $RB", IIC_LdStLoad, []>;
def LWZXTLS_ : XForm_1<31,  23, (outs g8rc:$RST), (ins ptr_rc_nor0:$RA, tlsreg:$RB),
                      "lwzx $RST, $RA, $RB", IIC_LdStLoad, []>;
def LWAXTLS_ : XForm_1<31, 341, (outs g8rc:$RST), (ins ptr_rc_nor0:$RA, tlsreg:$RB),
                       "lwax $RST, $RA, $RB", IIC_LdStLoad, []>;
def LDXTLS_  : XForm_1<31,  21, (outs g8rc:$RST), (ins ptr_rc_nor0:$RA, tlsreg:$RB),
                      "ldx $RST, $RA, $RB", IIC_LdStLD, []>, isPPC64;
}

let mayLoad = 1, Predicates = [HasFPU] in {
def LFSXTLS_ : XForm_25<31, 535, (outs f4rc:$RST), (ins ptr_rc_nor0:$RA, tlsreg:$RB),
                        "lfsx $RST, $RA, $RB", IIC_LdStLFD, []>;
def LFDXTLS_ : XForm_25<31, 599, (outs f8rc:$RST), (ins ptr_rc_nor0:$RA, tlsreg:$RB),
                        "lfdx $RST, $RA, $RB", IIC_LdStLFD, []>;
}

let mayStore = 1 in {
def STBXTLS_ : XForm_8<31, 215, (outs), (ins g8rc:$RST, ptr_rc_nor0:$RA, tlsreg:$RB),
                      "stbx $RST, $RA, $RB", IIC_LdStStore, []>,
                      PPC970_DGroup_Cracked;
def STHXTLS_ : XForm_8<31, 407, (outs), (ins g8rc:$RST, ptr_rc_nor0:$RA, tlsreg:$RB),
                      "sthx $RST, $RA, $RB", IIC_LdStStore, []>,
                      PPC970_DGroup_Cracked;
def STWXTLS_ : XForm_8<31, 151, (outs), (ins g8rc:$RST, ptr_rc_nor0:$RA, tlsreg:$RB),
                      "stwx $RST, $RA, $RB", IIC_LdStStore, []>,
                      PPC970_DGroup_Cracked;
def STDXTLS_  : XForm_8<31, 149, (outs), (ins g8rc:$RST, ptr_rc_nor0:$RA, tlsreg:$RB),
                       "stdx $RST, $RA, $RB", IIC_LdStSTD, []>, isPPC64,
                       PPC970_DGroup_Cracked;
}

let mayStore = 1, Predicates = [HasFPU] in {
def STFSXTLS_ : XForm_8<31, 663, (outs), (ins f4rc:$RST, ptr_rc_nor0:$RA, tlsreg:$RB),
                        "stfsx $RST, $RA, $RB", IIC_LdStSTFD, []>,
                        PPC970_DGroup_Cracked;
def STFDXTLS_ : XForm_8<31, 727, (outs), (ins f8rc:$RST, ptr_rc_nor0:$RA, tlsreg:$RB),
                        "stfdx $RST, $RA, $RB", IIC_LdStSTFD, []>,
                        PPC970_DGroup_Cracked;
}
}

let isCommutable = 1 in {
defm MULHD : XOForm_1r<31, 73, 0, (outs g8rc:$RT), (ins g8rc:$RA, g8rc:$RB),
                       "mulhd", "$RT, $RA, $RB", IIC_IntMulHW,
                       [(set i64:$RT, (mulhs i64:$RA, i64:$RB))]>;
defm MULHDU : XOForm_1r<31, 9, 0, (outs g8rc:$RT), (ins g8rc:$RA, g8rc:$RB),
                       "mulhdu", "$RT, $RA, $RB", IIC_IntMulHWU,
                       [(set i64:$RT, (mulhu i64:$RA, i64:$RB))]>;
} // isCommutable
}
} // Interpretation64Bit

let isCompare = 1, hasSideEffects = 0 in {
  def CMPD   : XForm_16_ext<31, 0, (outs crrc:$BF), (ins g8rc:$RA, g8rc:$RB),
                            "cmpd $BF, $RA, $RB", IIC_IntCompare>, isPPC64;
  def CMPLD  : XForm_16_ext<31, 32, (outs crrc:$BF), (ins g8rc:$RA, g8rc:$RB),
                            "cmpld $BF, $RA, $RB", IIC_IntCompare>, isPPC64;
  def CMPDI  : DForm_5_ext<11, (outs crrc:$BF), (ins g8rc:$RA, s16imm64:$D),
                           "cmpdi $BF, $RA, $D", IIC_IntCompare>, isPPC64;
  def CMPLDI : DForm_6_ext<10, (outs crrc:$BF), (ins g8rc:$RA, u16imm64:$D),
                           "cmpldi $BF, $RA, $D",
                           IIC_IntCompare>, isPPC64;
  let Interpretation64Bit = 1, isCodeGenOnly = 1 in
  def CMPRB8 : X_BF3_L1_RS5_RS5<31, 192, (outs crrc:$BF),
                                (ins u1imm:$L, g8rc:$RA, g8rc:$RB),
                                "cmprb $BF, $L, $RA, $RB", IIC_IntCompare, []>,
               Requires<[IsISA3_0]>;
  def CMPEQB : X_BF3_RS5_RS5<31, 224, (outs crrc:$BF),
                             (ins g8rc:$RA, g8rc:$RB), "cmpeqb $BF, $RA, $RB",
                             IIC_IntCompare, []>, Requires<[IsISA3_0]>;
}

let hasSideEffects = 0 in {
defm SLD  : XForm_6r<31,  27, (outs g8rc:$RA), (ins g8rc:$RST, gprc:$RB),
                     "sld", "$RA, $RST, $RB", IIC_IntRotateD,
                     [(set i64:$RA, (PPCshl i64:$RST, i32:$RB))]>, isPPC64;
defm SRD  : XForm_6r<31, 539, (outs g8rc:$RA), (ins g8rc:$RST, gprc:$RB),
                     "srd", "$RA, $RST, $RB", IIC_IntRotateD,
                     [(set i64:$RA, (PPCsrl i64:$RST, i32:$RB))]>, isPPC64;
defm SRAD : XForm_6rc<31, 794, (outs g8rc:$RA), (ins g8rc:$RST, gprc:$RB),
                      "srad", "$RA, $RST, $RB", IIC_IntRotateD,
                      [(set i64:$RA, (PPCsra i64:$RST, i32:$RB))]>, isPPC64;

let Interpretation64Bit = 1, isCodeGenOnly = 1 in {
defm CNTLZW8 : XForm_11r<31,  26, (outs g8rc:$RA), (ins g8rc:$RST),
                        "cntlzw", "$RA, $RST", IIC_IntGeneral, []>,
                        ZExt32To64, SExt32To64;
defm CNTTZW8 : XForm_11r<31, 538, (outs g8rc:$RA), (ins g8rc:$RST),
                        "cnttzw", "$RA, $RST", IIC_IntGeneral, []>,
               Requires<[IsISA3_0]>, ZExt32To64, SExt32To64;

defm EXTSB8 : XForm_11r<31, 954, (outs g8rc:$RA), (ins g8rc:$RST),
                        "extsb", "$RA, $RST", IIC_IntSimple,
                        [(set i64:$RA, (sext_inreg i64:$RST, i8))]>, SExt32To64;
defm EXTSH8 : XForm_11r<31, 922, (outs g8rc:$RA), (ins g8rc:$RST),
                        "extsh", "$RA, $RST", IIC_IntSimple,
                        [(set i64:$RA, (sext_inreg i64:$RST, i16))]>, SExt32To64;

defm SLW8  : XForm_6r<31,  24, (outs g8rc:$RA), (ins g8rc:$RST, g8rc:$RB),
                      "slw", "$RA, $RST, $RB", IIC_IntGeneral, []>, ZExt32To64;
defm SRW8  : XForm_6r<31, 536, (outs g8rc:$RA), (ins g8rc:$RST, g8rc:$RB),
                      "srw", "$RA, $RST, $RB", IIC_IntGeneral, []>, ZExt32To64;
} // Interpretation64Bit

// For fast-isel:
let isCodeGenOnly = 1 in {
def EXTSB8_32_64 : XForm_11<31, 954, (outs g8rc:$RA), (ins gprc:$RST),
                           "extsb $RA, $RST", IIC_IntSimple, []>, isPPC64,
                           SExt32To64;
def EXTSH8_32_64 : XForm_11<31, 922, (outs g8rc:$RA), (ins gprc:$RST),
                           "extsh $RA, $RST", IIC_IntSimple, []>, isPPC64,
                           SExt32To64;
} // isCodeGenOnly for fast-isel

defm EXTSW  : XForm_11r<31, 986, (outs g8rc:$RA), (ins g8rc:$RST),
                        "extsw", "$RA, $RST", IIC_IntSimple,
                        [(set i64:$RA, (sext_inreg i64:$RST, i32))]>, isPPC64,
                        SExt32To64;
let Interpretation64Bit = 1, isCodeGenOnly = 1 in
defm EXTSW_32_64 : XForm_11r<31, 986, (outs g8rc:$RA), (ins gprc:$RST),
                             "extsw", "$RA, $RST", IIC_IntSimple,
                             [(set i64:$RA, (sext i32:$RST))]>, isPPC64,
                             SExt32To64;
let isCodeGenOnly = 1 in
def EXTSW_32 : XForm_11<31, 986, (outs gprc:$RA), (ins gprc:$RST),
                        "extsw $RA, $RST", IIC_IntSimple,
                        []>, isPPC64;

defm SRADI  : XSForm_1rc<31, 413, (outs g8rc:$RA), (ins g8rc:$RS, u6imm:$SH),
                         "sradi", "$RA, $RS, $SH", IIC_IntRotateDI,
                         [(set i64:$RA, (sra i64:$RS, (i32 imm:$SH)))]>, isPPC64;

let Interpretation64Bit = 1, isCodeGenOnly = 1 in
defm EXTSWSLI_32_64 : XSForm_1r<31, 445, (outs g8rc:$RA),
                                (ins gprc:$RS, u6imm:$SH),
                                "extswsli", "$RA, $RS, $SH", IIC_IntRotateDI,
                                [(set i64:$RA,
                                      (PPCextswsli i32:$RS, (i32 imm:$SH)))]>,
                                isPPC64, Requires<[IsISA3_0]>;

defm EXTSWSLI : XSForm_1rc<31, 445, (outs g8rc:$RA), (ins g8rc:$RS, u6imm:$SH),
                           "extswsli", "$RA, $RS, $SH", IIC_IntRotateDI,
                           []>, isPPC64, Requires<[IsISA3_0]>;

// For fast-isel:
let isCodeGenOnly = 1, Defs = [CARRY] in
def SRADI_32  : XSForm_1<31, 413, (outs gprc:$RA), (ins gprc:$RS, u6imm:$SH),
                         "sradi $RA, $RS, $SH", IIC_IntRotateDI, []>, isPPC64;

defm CNTLZD : XForm_11r<31,  58, (outs g8rc:$RA), (ins g8rc:$RST),
                        "cntlzd", "$RA, $RST", IIC_IntGeneral,
                        [(set i64:$RA, (ctlz i64:$RST))]>,
                        ZExt32To64, SExt32To64;
defm CNTTZD : XForm_11r<31, 570, (outs g8rc:$RA), (ins g8rc:$RST),
                        "cnttzd", "$RA, $RST", IIC_IntGeneral,
                        [(set i64:$RA, (cttz i64:$RST))]>, Requires<[IsISA3_0]>,
                        ZExt32To64, SExt32To64;
def POPCNTD : XForm_11<31, 506, (outs g8rc:$RA), (ins g8rc:$RST),
                       "popcntd $RA, $RST", IIC_IntGeneral,
                       [(set i64:$RA, (ctpop i64:$RST))]>,
                       ZExt32To64, SExt32To64;
def BPERMD : XForm_6<31, 252, (outs g8rc:$RA), (ins g8rc:$RST, g8rc:$RB),
                     "bpermd $RA, $RST, $RB", IIC_IntGeneral,
                     [(set i64:$RA, (int_ppc_bpermd g8rc:$RST, g8rc:$RB))]>,
                     isPPC64, Requires<[HasBPERMD]>;

let isCodeGenOnly = 1, isCommutable = 1 in
def CMPB8 : XForm_6<31, 508, (outs g8rc:$RA), (ins g8rc:$RST, g8rc:$RB),
                    "cmpb $RA, $RST, $RB", IIC_IntGeneral,
                    [(set i64:$RA, (PPCcmpb i64:$RST, i64:$RB))]>;

// popcntw also does a population count on the high 32 bits (storing the
// results in the high 32-bits of the output). We'll ignore that here (which is
// safe because we never separately use the high part of the 64-bit registers).
def POPCNTW : XForm_11<31, 378, (outs gprc:$RA), (ins gprc:$RST),
                       "popcntw $RA, $RST", IIC_IntGeneral,
                       [(set i32:$RA, (ctpop i32:$RST))]>;

let isCodeGenOnly = 1 in {
def POPCNTB8 : XForm_11<31, 122, (outs g8rc:$RA), (ins g8rc:$RST),
                        "popcntb $RA, $RST", IIC_IntGeneral,
                        [(set i64:$RA, (int_ppc_popcntb i64:$RST))]>;

def CDTBCD8 : XForm_11<31, 282, (outs g8rc:$RA), (ins g8rc:$RST),
                       "cdtbcd $RA, $RST", IIC_IntGeneral, []>;
def CBCDTD8 : XForm_11<31, 314, (outs g8rc:$RA), (ins g8rc:$RST),
                       "cbcdtd $RA, $RST", IIC_IntGeneral, []>;

def ADDG6S8 : XOForm_1<31, 74, 0, (outs g8rc:$RT), (ins g8rc:$RA, g8rc:$RB),
                       "addg6s $RT, $RA, $RB", IIC_IntGeneral, []>;
}

defm DIVD  : XOForm_1rcr<31, 489, 0, (outs g8rc:$RT), (ins g8rc:$RA, g8rc:$RB),
                          "divd", "$RT, $RA, $RB", IIC_IntDivD,
                          [(set i64:$RT, (sdiv i64:$RA, i64:$RB))]>, isPPC64;
defm DIVDU : XOForm_1rcr<31, 457, 0, (outs g8rc:$RT), (ins g8rc:$RA, g8rc:$RB),
                          "divdu", "$RT, $RA, $RB", IIC_IntDivD,
                          [(set i64:$RT, (udiv i64:$RA, i64:$RB))]>, isPPC64;
defm DIVDE : XOForm_1rcr<31, 425, 0, (outs g8rc:$RT), (ins g8rc:$RA, g8rc:$RB),
                         "divde", "$RT, $RA, $RB", IIC_IntDivD,
                         [(set i64:$RT, (int_ppc_divde g8rc:$RA, g8rc:$RB))]>,
                         isPPC64, Requires<[HasExtDiv]>;

let Predicates = [IsISA3_0] in {
def MADDHD : VAForm_1a<48, (outs g8rc:$RT), (ins g8rc:$RA, g8rc:$RB, g8rc:$RC),
                       "maddhd $RT, $RA, $RB, $RC", IIC_IntMulHD, []>, isPPC64;
def MADDHDU : VAForm_1a<49,
                       (outs g8rc:$RT), (ins g8rc:$RA, g8rc:$RB, g8rc:$RC),
                       "maddhdu $RT, $RA, $RB, $RC", IIC_IntMulHD, []>, isPPC64;
def MADDLD : VAForm_1a<51, (outs gprc:$RT), (ins gprc:$RA, gprc:$RB, gprc:$RC),
                       "maddld $RT, $RA, $RB, $RC", IIC_IntMulHD,
                       [(set i32:$RT, (add_without_simm16 (mul_without_simm16 i32:$RA, i32:$RB), i32:$RC))]>,
                       isPPC64;
let Interpretation64Bit = 1, isCodeGenOnly = 1 in {
  def MADDLD8 : VAForm_1a<51,
                       (outs g8rc:$RT), (ins g8rc:$RA, g8rc:$RB, g8rc:$RC),
                       "maddld $RT, $RA, $RB, $RC", IIC_IntMulHD,
                       [(set i64:$RT, (add_without_simm16 (mul_without_simm16 i64:$RA, i64:$RB), i64:$RC))]>,
                       isPPC64;
  def SETB8 : XForm_44<31, 128, (outs g8rc:$RT), (ins crrc:$BFA),
                       "setb $RT, $BFA", IIC_IntGeneral>, isPPC64, SExt32To64;
}
def ADDPCIS : DXForm<19, 2, (outs g8rc:$RT), (ins i32imm:$D),
                     "addpcis $RT, $D", IIC_BrB, []>, isPPC64;
def MODSD : XForm_8<31, 777, (outs g8rc:$RST), (ins g8rc:$RA, g8rc:$RB),
                        "modsd $RST, $RA, $RB", IIC_IntDivW,
                        [(set i64:$RST, (srem i64:$RA, i64:$RB))]>;
def MODUD : XForm_8<31, 265, (outs g8rc:$RST), (ins g8rc:$RA, g8rc:$RB),
                        "modud $RST, $RA, $RB", IIC_IntDivW,
                        [(set i64:$RST, (urem i64:$RA, i64:$RB))]>;
}

defm DIVDEU : XOForm_1rcr<31, 393, 0, (outs g8rc:$RT), (ins g8rc:$RA, g8rc:$RB),
                          "divdeu", "$RT, $RA, $RB", IIC_IntDivD,
                          [(set i64:$RT, (int_ppc_divdeu g8rc:$RA, g8rc:$RB))]>,
                          isPPC64, Requires<[HasExtDiv]>;
let isCommutable = 1 in
defm MULLD : XOForm_1rx<31, 233, (outs g8rc:$RT), (ins g8rc:$RA, g8rc:$RB),
                        "mulld", "$RT, $RA, $RB", IIC_IntMulHD,
                        [(set i64:$RT, (mul i64:$RA, i64:$RB))]>, isPPC64;
let Interpretation64Bit = 1, isCodeGenOnly = 1 in
def MULLI8 : DForm_2<7, (outs g8rc:$RST), (ins g8rc:$RA, s16imm64:$D),
                       "mulli $RST, $RA, $D", IIC_IntMulLI,
                       [(set i64:$RST, (mul i64:$RA, imm64SExt16:$D))]>;
}

let hasSideEffects = 1 in {
def DARN : XForm_45<31, 755, (outs g8rc:$RT), (ins u2imm:$L),
                    "darn $RT, $L", IIC_LdStLD>, isPPC64;
}

let hasSideEffects = 0 in {
defm RLDIMI : MDForm_1r<30, 3, (outs g8rc:$RA),
                        (ins g8rc:$RAi, g8rc:$RS, u6imm:$SH, u6imm:$MBE),
                        "rldimi", "$RA, $RS, $SH, $MBE", IIC_IntRotateDI,
                        []>, isPPC64, RegConstraint<"$RAi = $RA">,
                        NoEncode<"$RAi">;

// Rotate instructions.
defm RLDCL  : MDSForm_1r<30, 8,
                        (outs g8rc:$RA), (ins g8rc:$RS, gprc:$RB, u6imm:$MBE),
                        "rldcl", "$RA, $RS, $RB, $MBE", IIC_IntRotateD,
                        []>, isPPC64;
defm RLDCR  : MDSForm_1r<30, 9,
                        (outs g8rc:$RA), (ins g8rc:$RS, gprc:$RB, u6imm:$MBE),
                        "rldcr", "$RA, $RS, $RB, $MBE", IIC_IntRotateD,
                        []>, isPPC64;
defm RLDICL : MDForm_1r<30, 0,
                        (outs g8rc:$RA), (ins g8rc:$RS, u6imm:$SH, u6imm:$MBE),
                        "rldicl", "$RA, $RS, $SH, $MBE", IIC_IntRotateDI,
                        []>, isPPC64;
// For fast-isel:
let isCodeGenOnly = 1 in
def RLDICL_32_64 : MDForm_1<30, 0,
                            (outs g8rc:$RA),
                            (ins gprc:$RS, u6imm:$SH, u6imm:$MBE),
                            "rldicl $RA, $RS, $SH, $MBE", IIC_IntRotateDI,
                            []>, isPPC64;
// End fast-isel.
let Interpretation64Bit = 1, isCodeGenOnly = 1 in
defm RLDICL_32 : MDForm_1r<30, 0,
                           (outs gprc:$RA),
                           (ins gprc:$RS, u6imm:$SH, u6imm:$MBE),
                           "rldicl", "$RA, $RS, $SH, $MBE", IIC_IntRotateDI,
                           []>, isPPC64;
defm RLDICR : MDForm_1r<30, 1,
                        (outs g8rc:$RA), (ins g8rc:$RS, u6imm:$SH, u6imm:$MBE),
                        "rldicr", "$RA, $RS, $SH, $MBE", IIC_IntRotateDI,
                        []>, isPPC64;
let isCodeGenOnly = 1 in
def RLDICR_32 : MDForm_1<30, 1,
                         (outs gprc:$RA), (ins gprc:$RS, u6imm:$SH, u6imm:$MBE),
                         "rldicr $RA, $RS, $SH, $MBE", IIC_IntRotateDI,
                         []>, isPPC64;
defm RLDIC  : MDForm_1r<30, 2,
                        (outs g8rc:$RA), (ins g8rc:$RS, u6imm:$SH, u6imm:$MBE),
                        "rldic", "$RA, $RS, $SH, $MBE", IIC_IntRotateDI,
                        []>, isPPC64;

let Interpretation64Bit = 1, isCodeGenOnly = 1 in {
defm RLWINM8 : MForm_2r<21, (outs g8rc:$RA),
                        (ins g8rc:$RS, u5imm:$SH, u5imm:$MB, u5imm:$ME),
                        "rlwinm", "$RA, $RS, $SH, $MB, $ME", IIC_IntGeneral,
                        []>;

defm RLWNM8  : MForm_1r<23, (outs g8rc:$RA),
                        (ins g8rc:$RS, g8rc:$RB, u5imm:$MB, u5imm:$ME),
                        "rlwnm", "$RA, $RS, $RB, $MB, $ME", IIC_IntGeneral,
                        []>;

// RLWIMI can be commuted if the rotate amount is zero.
let Interpretation64Bit = 1, isCodeGenOnly = 1 in
defm RLWIMI8 : MForm_2r<20, (outs g8rc:$RA),
                        (ins g8rc:$RAi, g8rc:$RS, u5imm:$SH, u5imm:$MB,
                        u5imm:$ME), "rlwimi", "$RA, $RS, $SH, $MB, $ME",
                        IIC_IntRotate, []>, PPC970_DGroup_Cracked,
                        RegConstraint<"$RAi = $RA">, NoEncode<"$RAi">;

let isSelect = 1 in
def ISEL8   : AForm_4<31, 15,
                     (outs g8rc:$RT), (ins g8rc_nox0:$RA, g8rc:$RB, crbitrc:$COND),
                     "isel $RT, $RA, $RB, $COND", IIC_IntISEL,
                     []>;
}  // Interpretation64Bit
}  // hasSideEffects = 0
}  // End FXU Operations.

def : InstAlias<"li $rD, $imm", (ADDI8 g8rc:$rD, ZERO8, s16imm64:$imm)>;
def : InstAlias<"lis $rD, $imm", (ADDIS8 g8rc:$rD, ZERO8, s17imm64:$imm)>;

def : InstAlias<"mr $rA, $rB", (OR8 g8rc:$rA, g8rc:$rB, g8rc:$rB)>;
def : InstAlias<"mr. $rA, $rB", (OR8_rec g8rc:$rA, g8rc:$rB, g8rc:$rB)>;

def : InstAlias<"not $rA, $rB", (NOR8 g8rc:$rA, g8rc:$rB, g8rc:$rB)>;
def : InstAlias<"not. $rA, $rB", (NOR8_rec g8rc:$rA, g8rc:$rB, g8rc:$rB)>;

def : InstAlias<"mtcr $rA", (MTCRF8 255, g8rc:$rA)>;

def : InstAlias<"sub $rA, $rB, $rC", (SUBF8 g8rc:$rA, g8rc:$rC, g8rc:$rB)>;
def : InstAlias<"sub. $rA, $rB, $rC", (SUBF8_rec g8rc:$rA, g8rc:$rC, g8rc:$rB)>;
def : InstAlias<"subc $rA, $rB, $rC", (SUBFC8 g8rc:$rA, g8rc:$rC, g8rc:$rB)>;
def : InstAlias<"subc. $rA, $rB, $rC", (SUBFC8_rec g8rc:$rA, g8rc:$rC, g8rc:$rB)>;

def : InstAlias<"rotlwi $rA, $rS, $n", (RLWINM8 g8rc:$rA, g8rc:$rS, u5imm:$n, 0, 31)>;
def : InstAlias<"rotlwi. $rA, $rS, $n", (RLWINM8_rec g8rc:$rA, g8rc:$rS, u5imm:$n, 0, 31)>;
def : InstAlias<"rotlw $rA, $rS, $rB", (RLWNM8 g8rc:$rA, g8rc:$rS, g8rc:$rB, 0, 31)>;
def : InstAlias<"rotlw. $rA, $rS, $rB", (RLWNM8_rec g8rc:$rA, g8rc:$rS, g8rc:$rB, 0, 31)>;
def : InstAlias<"clrlwi $rA, $rS, $n", (RLWINM8 g8rc:$rA, g8rc:$rS, 0, u5imm:$n, 31)>;
def : InstAlias<"clrlwi. $rA, $rS, $n", (RLWINM8_rec g8rc:$rA, g8rc:$rS, 0, u5imm:$n, 31)>;

def : InstAlias<"isellt $rT, $rA, $rB",
                (ISEL8 g8rc:$rT, g8rc_nox0:$rA, g8rc:$rB, CR0LT)>;
def : InstAlias<"iselgt $rT, $rA, $rB",
                (ISEL8 g8rc:$rT, g8rc_nox0:$rA, g8rc:$rB, CR0GT)>;
def : InstAlias<"iseleq $rT, $rA, $rB",
                (ISEL8 g8rc:$rT, g8rc_nox0:$rA, g8rc:$rB, CR0EQ)>;

def : InstAlias<"nop", (ORI8 X0, X0, 0)>;
def : InstAlias<"xnop", (XORI8 X0, X0, 0)>;

def : InstAlias<"cntlzw $rA, $rS", (CNTLZW8 g8rc:$rA, g8rc:$rS)>;
def : InstAlias<"cntlzw. $rA, $rS", (CNTLZW8_rec g8rc:$rA, g8rc:$rS)>;

def : InstAlias<"mtxer $Rx", (MTSPR8 1, g8rc:$Rx)>;
def : InstAlias<"mfxer $Rx", (MFSPR8 g8rc:$Rx, 1)>;

//Disable this alias on AIX for now because as does not support them.
let Predicates = [ModernAs] in {

def : InstAlias<"mtudscr $Rx", (MTSPR8 3, g8rc:$Rx)>;
def : InstAlias<"mfudscr $Rx", (MFSPR8 g8rc:$Rx, 3)>;

def : InstAlias<"mfrtcu $Rx", (MFSPR8 g8rc:$Rx, 4)>;
def : InstAlias<"mfrtcl $Rx", (MFSPR8 g8rc:$Rx, 5)>;

def : InstAlias<"mtlr $Rx", (MTSPR8 8, g8rc:$Rx)>;
def : InstAlias<"mflr $Rx", (MFSPR8 g8rc:$Rx, 8)>;

def : InstAlias<"mtctr $Rx", (MTSPR8 9, g8rc:$Rx)>;
def : InstAlias<"mfctr $Rx", (MFSPR8 g8rc:$Rx, 9)>;

def : InstAlias<"mtuamr $Rx", (MTSPR8 13, g8rc:$Rx)>;
def : InstAlias<"mfuamr $Rx", (MFSPR8 g8rc:$Rx, 13)>;

def : InstAlias<"mtdscr $Rx", (MTSPR8 17, g8rc:$Rx)>;
def : InstAlias<"mfdscr $Rx", (MFSPR8 g8rc:$Rx, 17)>;

def : InstAlias<"mtdsisr $Rx", (MTSPR8 18, g8rc:$Rx)>;
def : InstAlias<"mfdsisr $Rx", (MFSPR8 g8rc:$Rx, 18)>;

def : InstAlias<"mtdar $Rx", (MTSPR8 19, g8rc:$Rx)>;
def : InstAlias<"mfdar $Rx", (MFSPR8 g8rc:$Rx, 19)>;

def : InstAlias<"mtdec $Rx", (MTSPR8 22, g8rc:$Rx)>;
def : InstAlias<"mfdec $Rx", (MFSPR8 g8rc:$Rx, 22)>;

def : InstAlias<"mtsdr1 $Rx", (MTSPR8 25, g8rc:$Rx)>;
def : InstAlias<"mfsdr1 $Rx", (MFSPR8 g8rc:$Rx, 25)>;

def : InstAlias<"mtsrr0 $Rx", (MTSPR8 26, g8rc:$Rx)>;
def : InstAlias<"mfsrr0 $Rx", (MFSPR8 g8rc:$Rx, 26)>;

def : InstAlias<"mtsrr1 $Rx", (MTSPR8 27, g8rc:$Rx)>;
def : InstAlias<"mfsrr1 $Rx", (MFSPR8 g8rc:$Rx, 27)>;

def : InstAlias<"mtcfar $Rx", (MTSPR8 28, g8rc:$Rx)>;
def : InstAlias<"mfcfar $Rx", (MFSPR8 g8rc:$Rx, 28)>;

def : InstAlias<"mtamr $Rx", (MTSPR8 29, g8rc:$Rx)>;
def : InstAlias<"mfamr $Rx", (MFSPR8 g8rc:$Rx, 29)>;

foreach SPRG = 0-3 in {
  def : InstAlias<"mfsprg $RT, "#SPRG, (MFSPR8 g8rc:$RT, !add(SPRG, 272))>;
  def : InstAlias<"mfsprg"#SPRG#" $RT", (MFSPR8 g8rc:$RT, !add(SPRG, 272))>;
  def : InstAlias<"mtsprg "#SPRG#", $RT", (MTSPR8 !add(SPRG, 272), g8rc:$RT)>;
  def : InstAlias<"mtsprg"#SPRG#" $RT", (MTSPR8 !add(SPRG, 272), g8rc:$RT)>;
}

def : InstAlias<"mfasr $RT", (MFSPR8 g8rc:$RT, 280)>;
def : InstAlias<"mtasr $RT", (MTSPR8 280, g8rc:$RT)>;

def : InstAlias<"mttbl $Rx", (MTSPR8 284, g8rc:$Rx)>;
def : InstAlias<"mttbu $Rx", (MTSPR8 285, g8rc:$Rx)>;

def : InstAlias<"mfpvr $RT", (MFSPR8 g8rc:$RT, 287)>;

def : InstAlias<"mfspefscr $Rx", (MFSPR8 g8rc:$Rx, 512)>;
def : InstAlias<"mtspefscr $Rx", (MTSPR8 512, g8rc:$Rx)>;

}

//===----------------------------------------------------------------------===//
// Load/Store instructions.
//


// Sign extending loads.
let PPC970_Unit = 2 in {
let Interpretation64Bit = 1, isCodeGenOnly = 1 in
def LHA8: DForm_1<42, (outs g8rc:$RST), (ins (memri $D, $RA):$addr),
                  "lha $RST, $addr", IIC_LdStLHA,
                  [(set i64:$RST, (sextloadi16 DForm:$addr))]>,
                  PPC970_DGroup_Cracked, SExt32To64;
def LWA  : DSForm_1<58, 2, (outs g8rc:$RST), (ins (memrix $D, $RA):$addr),
                    "lwa $RST, $addr", IIC_LdStLWA,
                    [(set i64:$RST,
                          (sextloadi32 DSForm:$addr))]>, isPPC64,
                    PPC970_DGroup_Cracked, SExt32To64;
let Interpretation64Bit = 1, isCodeGenOnly = 1 in
def LHAX8: XForm_1_memOp<31, 343, (outs g8rc:$RST), (ins (memrr $RA, $RB):$addr),
                        "lhax $RST, $addr", IIC_LdStLHA,
                        [(set i64:$RST, (sextloadi16 XForm:$addr))]>,
                        PPC970_DGroup_Cracked, SExt32To64;
def LWAX : XForm_1_memOp<31, 341, (outs g8rc:$RST), (ins (memrr $RA, $RB):$addr),
                        "lwax $RST, $addr", IIC_LdStLHA,
                        [(set i64:$RST, (sextloadi32 XForm:$addr))]>, isPPC64,
                        PPC970_DGroup_Cracked, SExt32To64;
// For fast-isel:
let isCodeGenOnly = 1, mayLoad = 1, hasSideEffects = 0 in {
def LWA_32  : DSForm_1<58, 2, (outs gprc:$RST), (ins (memrix $D, $RA):$addr),
                      "lwa $RST, $addr", IIC_LdStLWA, []>, isPPC64,
                      PPC970_DGroup_Cracked, SExt32To64;
def LWAX_32 : XForm_1_memOp<31, 341, (outs gprc:$RST), (ins (memrr $RA, $RB):$addr),
                            "lwax $RST, $addr", IIC_LdStLHA, []>, isPPC64,
                            PPC970_DGroup_Cracked, SExt32To64;
} // end fast-isel isCodeGenOnly

// Update forms.
let mayLoad = 1, hasSideEffects = 0 in {
let Interpretation64Bit = 1, isCodeGenOnly = 1 in
def LHAU8 : DForm_1<43, (outs g8rc:$RST, ptr_rc_nor0:$ea_result),
                    (ins (memri $D, $RA):$addr),
                    "lhau $RST, $addr", IIC_LdStLHAU,
                    []>, RegConstraint<"$addr.reg = $ea_result">,
                    NoEncode<"$ea_result">;
// NO LWAU!

let Interpretation64Bit = 1, isCodeGenOnly = 1 in
def LHAUX8 : XForm_1_memOp<31, 375, (outs g8rc:$RST, ptr_rc_nor0:$ea_result),
                          (ins (memrr $RA, $RB):$addr),
                          "lhaux $RST, $addr", IIC_LdStLHAUX,
                          []>, RegConstraint<"$addr.ptrreg = $ea_result">,
                          NoEncode<"$ea_result">;
def LWAUX : XForm_1_memOp<31, 373, (outs g8rc:$RST, ptr_rc_nor0:$ea_result),
                          (ins (memrr $RA, $RB):$addr),
                          "lwaux $RST, $addr", IIC_LdStLHAUX,
                          []>, RegConstraint<"$addr.ptrreg = $ea_result">,
                          NoEncode<"$ea_result">, isPPC64;
}
}

let Interpretation64Bit = 1, isCodeGenOnly = 1 in {
// Zero extending loads.
let PPC970_Unit = 2 in {
def LBZ8 : DForm_1<34, (outs g8rc:$RST), (ins (memri $D, $RA):$addr),
                  "lbz $RST, $addr", IIC_LdStLoad,
                  [(set i64:$RST, (zextloadi8 DForm:$addr))]>, ZExt32To64,
                  SExt32To64;
def LHZ8 : DForm_1<40, (outs g8rc:$RST), (ins (memri $D, $RA):$addr),
                  "lhz $RST, $addr", IIC_LdStLoad,
                  [(set i64:$RST, (zextloadi16 DForm:$addr))]>, ZExt32To64,
                  SExt32To64;
def LWZ8 : DForm_1<32, (outs g8rc:$RST), (ins (memri $D, $RA):$addr),
                  "lwz $RST, $addr", IIC_LdStLoad,
                  [(set i64:$RST, (zextloadi32 DForm:$addr))]>, isPPC64,
                  ZExt32To64;

def LBZX8 : XForm_1_memOp<31,  87, (outs g8rc:$RST), (ins (memrr $RA, $RB):$addr),
                          "lbzx $RST, $addr", IIC_LdStLoad,
                          [(set i64:$RST, (zextloadi8 XForm:$addr))]>, ZExt32To64,
                          SExt32To64;
def LHZX8 : XForm_1_memOp<31, 279, (outs g8rc:$RST), (ins (memrr $RA, $RB):$addr),
                          "lhzx $RST, $addr", IIC_LdStLoad,
                          [(set i64:$RST, (zextloadi16 XForm:$addr))]>,
                          ZExt32To64, SExt32To64;
def LWZX8 : XForm_1_memOp<31,  23, (outs g8rc:$RST), (ins (memrr $RA, $RB):$addr),
                          "lwzx $RST, $addr", IIC_LdStLoad,
                          [(set i64:$RST, (zextloadi32 XForm:$addr))]>,
                          ZExt32To64;


// Update forms.
let mayLoad = 1, hasSideEffects = 0 in {
def LBZU8 : DForm_1<35, (outs g8rc:$RST, ptr_rc_nor0:$ea_result),
                    (ins (memri $D, $RA):$addr),
                    "lbzu $RST, $addr", IIC_LdStLoadUpd,
                    []>, RegConstraint<"$addr.reg = $ea_result">,
                    NoEncode<"$ea_result">;
def LHZU8 : DForm_1<41, (outs g8rc:$RST, ptr_rc_nor0:$ea_result),
                    (ins (memri $D, $RA):$addr),
                    "lhzu $RST, $addr", IIC_LdStLoadUpd,
                    []>, RegConstraint<"$addr.reg = $ea_result">,
                    NoEncode<"$ea_result">;
def LWZU8 : DForm_1<33, (outs g8rc:$RST, ptr_rc_nor0:$ea_result),
                    (ins (memri $D, $RA):$addr),
                    "lwzu $RST, $addr", IIC_LdStLoadUpd,
                    []>, RegConstraint<"$addr.reg = $ea_result">,
                    NoEncode<"$ea_result">;

def LBZUX8 : XForm_1_memOp<31, 119, (outs g8rc:$RST, ptr_rc_nor0:$ea_result),
                          (ins (memrr $RA, $RB):$addr),
                          "lbzux $RST, $addr", IIC_LdStLoadUpdX,
                          []>, RegConstraint<"$addr.ptrreg = $ea_result">,
                          NoEncode<"$ea_result">;
def LHZUX8 : XForm_1_memOp<31, 311, (outs g8rc:$RST, ptr_rc_nor0:$ea_result),
                          (ins (memrr $RA, $RB):$addr),
                          "lhzux $RST, $addr", IIC_LdStLoadUpdX,
                          []>, RegConstraint<"$addr.ptrreg = $ea_result">,
                          NoEncode<"$ea_result">;
def LWZUX8 : XForm_1_memOp<31, 55, (outs g8rc:$RST, ptr_rc_nor0:$ea_result),
                          (ins (memrr $RA, $RB):$addr),
                          "lwzux $RST, $addr", IIC_LdStLoadUpdX,
                          []>, RegConstraint<"$addr.ptrreg = $ea_result">,
                          NoEncode<"$ea_result">;
}
}
} // Interpretation64Bit


// Full 8-byte loads.
let PPC970_Unit = 2 in {
def LD   : DSForm_1<58, 0, (outs g8rc:$RST), (ins (memrix $D, $RA):$addr),
                    "ld $RST, $addr", IIC_LdStLD,
                    [(set i64:$RST, (load DSForm:$addr))]>, isPPC64;
// The following four definitions are selected for small code model only.
// Otherwise, we need to create two instructions to form a 32-bit offset,
// so we have a custom matcher for TOC_ENTRY in PPCDAGToDAGIsel::Select().
def LDtoc: PPCEmitTimePseudo<(outs g8rc:$rD), (ins tocentry:$disp, g8rc:$reg),
                  "#LDtoc",
                  [(set i64:$rD,
                     (PPCtoc_entry tglobaladdr:$disp, i64:$reg))]>, isPPC64;
def LDtocJTI: PPCEmitTimePseudo<(outs g8rc:$rD), (ins tocentry:$disp, g8rc:$reg),
                  "#LDtocJTI",
                  [(set i64:$rD,
                     (PPCtoc_entry tjumptable:$disp, i64:$reg))]>, isPPC64;
def LDtocCPT: PPCEmitTimePseudo<(outs g8rc:$rD), (ins tocentry:$disp, g8rc:$reg),
                  "#LDtocCPT",
                  [(set i64:$rD,
                     (PPCtoc_entry tconstpool:$disp, i64:$reg))]>, isPPC64;
def LDtocBA: PPCEmitTimePseudo<(outs g8rc:$rD), (ins tocentry:$disp, g8rc:$reg),
                  "#LDtocCPT",
                  [(set i64:$rD,
                     (PPCtoc_entry tblockaddress:$disp, i64:$reg))]>, isPPC64;

def LDX  : XForm_1_memOp<31,  21, (outs g8rc:$RST), (ins (memrr $RA, $RB):$addr),
                        "ldx $RST, $addr", IIC_LdStLD,
                        [(set i64:$RST, (load XForm:$addr))]>, isPPC64;

let Predicates = [IsISA2_06] in {
def LDBRX : XForm_1_memOp<31,  532, (outs g8rc:$RST), (ins (memrr $RA, $RB):$addr),
                          "ldbrx $RST, $addr", IIC_LdStLoad,
                          [(set i64:$RST, (PPClbrx ForceXForm:$addr, i64))]>, isPPC64;
}

let mayLoad = 1, hasSideEffects = 0, isCodeGenOnly = 1 in {
def LHBRX8 : XForm_1_memOp<31, 790, (outs g8rc:$RST), (ins (memrr $RA, $RB):$addr),
                          "lhbrx $RST, $addr", IIC_LdStLoad, []>, ZExt32To64;
def LWBRX8 : XForm_1_memOp<31,  534, (outs g8rc:$RST), (ins (memrr $RA, $RB):$addr),
                          "lwbrx $RST, $addr", IIC_LdStLoad, []>, ZExt32To64;
}

let mayLoad = 1, hasSideEffects = 0 in {
def LDU  : DSForm_1<58, 1, (outs g8rc:$RST, ptr_rc_nor0:$ea_result),
                    (ins (memrix $D, $RA):$addr),
                    "ldu $RST, $addr", IIC_LdStLDU,
                    []>, RegConstraint<"$addr.reg = $ea_result">, isPPC64,
                    NoEncode<"$ea_result">;

def LDUX : XForm_1_memOp<31, 53, (outs g8rc:$RST, ptr_rc_nor0:$ea_result),
                        (ins (memrr $RA, $RB):$addr),
                        "ldux $RST, $addr", IIC_LdStLDUX,
                        []>, RegConstraint<"$addr.ptrreg = $ea_result">,
                        NoEncode<"$ea_result">, isPPC64;
}

let mayLoad = 1, hasNoSchedulingInfo = 1 in {
// Full 16-byte load.
// Early clobber $RTp to avoid assigned to the same register as RA.
// TODO: Add scheduling info.
def LQ   : DQForm_RTp5_RA17_MEM<56, 0,
                                (outs g8prc:$RTp),
                                (ins (memrix16 $DQ, $RA):$addr),
                                "lq $RTp, $addr", IIC_LdStLQ,
                                []>,
                                RegConstraint<"@earlyclobber $RTp">,
                                isPPC64;
// We don't really have LQX in the ISA, make a pseudo one so that we can
// handle x-form during isel. Make it pre-ra may expose
// oppotunities to some opts(CSE, LICM and etc.) for the result of adding
// RA and RB.
def LQX_PSEUDO : PPCCustomInserterPseudo<(outs g8prc:$RTp),
                                         (ins memrr:$src), "#LQX_PSEUDO", []>;

def RESTORE_QUADWORD : PPCEmitTimePseudo<(outs g8prc:$RTp), (ins memrix:$src),
                                         "#RESTORE_QUADWORD", []>;
}

}

def : Pat<(int_ppc_atomic_load_i128 iaddrX16:$src),
          (SPLIT_QUADWORD (LQ memrix16:$src))>;

def : Pat<(int_ppc_atomic_load_i128 ForceXForm:$src),
          (SPLIT_QUADWORD (LQX_PSEUDO memrr:$src))>;

// Support for medium and large code model.
let hasSideEffects = 0 in {
let isReMaterializable = 1 in {
def ADDIStocHA8: PPCEmitTimePseudo<(outs g8rc:$rD), (ins g8rc_nox0:$reg, tocentry:$disp),
                       "#ADDIStocHA8", []>, isPPC64;
def ADDItocL8: PPCEmitTimePseudo<(outs g8rc:$rD), (ins g8rc_nox0:$reg, tocentry:$disp),
                     "#ADDItocL8", []>, isPPC64;
}

// Local Data Transform
def ADDItoc8 : PPCEmitTimePseudo<(outs g8rc:$rD), (ins g8rc_nox0:$reg, tocentry:$disp),
                   "#ADDItoc8",
                   []>, isPPC64;
let mayLoad = 1 in
def LDtocL: PPCEmitTimePseudo<(outs g8rc:$rD), (ins tocentry:$disp, g8rc_nox0:$reg),
                   "#LDtocL", []>, isPPC64;
}

// Support for thread-local storage.
def ADDISgotTprelHA: PPCEmitTimePseudo<(outs g8rc:$rD), (ins g8rc_nox0:$reg, s16imm64:$disp),
                         "#ADDISgotTprelHA",
                         [(set i64:$rD,
                           (PPCaddisGotTprelHA i64:$reg,
                                               tglobaltlsaddr:$disp))]>,
                  isPPC64;
def LDgotTprelL: PPCEmitTimePseudo<(outs g8rc_nox0:$rD), (ins s16imm64:$disp, g8rc_nox0:$reg),
                        "#LDgotTprelL",
                        [(set i64:$rD,
                          (PPCldGotTprelL tglobaltlsaddr:$disp, i64:$reg))]>,
                 isPPC64;

let Defs = [CR7], Itinerary = IIC_LdStSync in
def CFENCE8 : PPCPostRAExpPseudo<(outs), (ins g8rc:$cr), "#CFENCE8", []>;

def : Pat<(PPCaddTls i64:$in, tglobaltlsaddr:$g),
          (ADD8TLS $in, tglobaltlsaddr:$g)>;
def ADDIStlsgdHA: PPCEmitTimePseudo<(outs g8rc:$rD), (ins g8rc_nox0:$reg, s16imm64:$disp),
                         "#ADDIStlsgdHA",
                         [(set i64:$rD,
                           (PPCaddisTlsgdHA i64:$reg, tglobaltlsaddr:$disp))]>,
                  isPPC64;
def ADDItlsgdL : PPCEmitTimePseudo<(outs g8rc:$rD), (ins g8rc_nox0:$reg, s16imm64:$disp),
                       "#ADDItlsgdL",
                       [(set i64:$rD,
                         (PPCaddiTlsgdL i64:$reg, tglobaltlsaddr:$disp))]>,
                 isPPC64;

class GETtlsADDRPseudo <string asmstr> : PPCEmitTimePseudo<(outs g8rc:$rD), (ins g8rc:$reg, tlsgd:$sym),
                                             asmstr,
                                             [(set i64:$rD,
                                               (PPCgetTlsAddr i64:$reg, tglobaltlsaddr:$sym))]>,
                                      isPPC64;
class GETtlsldADDRPseudo <string asmstr> : PPCEmitTimePseudo<(outs g8rc:$rD), (ins g8rc:$reg, tlsgd:$sym),
                                             asmstr,
                                             [(set i64:$rD,
                                               (PPCgetTlsldAddr i64:$reg, tglobaltlsaddr:$sym))]>,
                                      isPPC64;

let hasExtraSrcRegAllocReq = 1, hasExtraDefRegAllocReq = 1 in {
// LR8 is a true define, while the rest of the Defs are clobbers. X3 is
// explicitly defined when this op is created, so not mentioned here.
// This is lowered to BL8_NOP_TLS by the assembly printer, so the size must be
// correct because the branch select pass is relying on it.
let Defs = [X0,X4,X5,X6,X7,X8,X9,X10,X11,X12,LR8,CTR8,CR0,CR1,CR5,CR6,CR7], Size = 8 in
def GETtlsADDR : GETtlsADDRPseudo <"#GETtlsADDR">;
let Defs = [X0,X2,X4,X5,X6,X7,X8,X9,X10,X11,X12,LR8,CTR8,CR0,CR1,CR5,CR6,CR7], Size = 8 in
def GETtlsADDRPCREL : GETtlsADDRPseudo <"#GETtlsADDRPCREL">;

// LR8 is a true define, while the rest of the Defs are clobbers. X3 is
// explicitly defined when this op is created, so not mentioned here.
let Defs = [X0,X4,X5,X6,X7,X8,X9,X10,X11,X12,LR8,CTR8,CR0,CR1,CR5,CR6,CR7] in
def GETtlsldADDR : GETtlsldADDRPseudo <"#GETtlsldADDR">;
let Defs = [X0,X2,X4,X5,X6,X7,X8,X9,X10,X11,X12,LR8,CTR8,CR0,CR1,CR5,CR6,CR7] in
def GETtlsldADDRPCREL : GETtlsldADDRPseudo <"#GETtlsldADDRPCREL">;

// On AIX, the call to __tls_get_addr needs two inputs in X3/X4 for the
// offset and region handle respectively. The call is not followed by a nop
// so we don't need to mark it with a size of 8 bytes. Finally, the assembly
// manual mentions this exact set of registers as the clobbered set, others
// are guaranteed not to be clobbered.
let Defs = [X0,X4,X5,X11,LR8,CR0] in {
def GETtlsADDR64AIX :
  PPCEmitTimePseudo<(outs g8rc:$rD),(ins g8rc:$offset, g8rc:$handle),
                    "GETtlsADDR64AIX",
                    [(set i64:$rD,
                      (PPCgetTlsAddr i64:$offset, i64:$handle))]>, isPPC64;
// On AIX, the call to .__tls_get_mod needs one input in X3 for the module handle.
def GETtlsMOD64AIX :
  PPCEmitTimePseudo<(outs g8rc:$rD),(ins g8rc:$handle),
                    "GETtlsMOD64AIX",
                    [(set i64:$rD,
                      (PPCgetTlsMod i64:$handle))]>, isPPC64;
}
}

// Combined op for ADDItlsgdL and GETtlsADDR, late expanded.  X3 and LR8
// are true defines while the rest of the Defs are clobbers.
let hasExtraSrcRegAllocReq = 1, hasExtraDefRegAllocReq = 1,
    Defs = [X0,X3,X4,X5,X6,X7,X8,X9,X10,X11,X12,LR8,CTR8,CR0,CR1,CR5,CR6,CR7]
    in
def ADDItlsgdLADDR : PPCEmitTimePseudo<(outs g8rc:$rD),
                            (ins g8rc_nox0:$reg, s16imm64:$disp, tlsgd:$sym),
                            "#ADDItlsgdLADDR",
                            [(set i64:$rD,
                              (PPCaddiTlsgdLAddr i64:$reg,
                                                 tglobaltlsaddr:$disp,
                                                 tglobaltlsaddr:$sym))]>,
                     isPPC64;
def ADDIStlsldHA: PPCEmitTimePseudo<(outs g8rc:$rD), (ins g8rc_nox0:$reg, s16imm64:$disp),
                         "#ADDIStlsldHA",
                         [(set i64:$rD,
                           (PPCaddisTlsldHA i64:$reg, tglobaltlsaddr:$disp))]>,
                  isPPC64;
def ADDItlsldL : PPCEmitTimePseudo<(outs g8rc:$rD), (ins g8rc_nox0:$reg, s16imm64:$disp),
                       "#ADDItlsldL",
                       [(set i64:$rD,
                         (PPCaddiTlsldL i64:$reg, tglobaltlsaddr:$disp))]>,
                 isPPC64;
// This pseudo is expanded to two copies to put the variable offset in R4 and
// the region handle in R3 and GETtlsADDR64AIX.
def TLSGDAIX8 :
  PPCEmitTimePseudo<(outs g8rc:$rD), (ins g8rc:$offset, g8rc:$handle),
                     "#TLSGDAIX8",
                     [(set i64:$rD,
                       (PPCTlsgdAIX i64:$offset, i64:$handle))]>;
// This pseudo is expanded to the call to GETtlsMOD64AIX.
def TLSLDAIX8 : PPCEmitTimePseudo<(outs g8rc:$rD), (ins g8rc:$handle),
                     "#TLSLDAIX8", [(set i64:$rD, (PPCTlsldAIX i64:$handle))]>;
// Combined op for ADDItlsldL and GETtlsADDR, late expanded.  X3 and LR8
// are true defines, while the rest of the Defs are clobbers.
let hasExtraSrcRegAllocReq = 1, hasExtraDefRegAllocReq = 1,
    Defs = [X0,X3,X4,X5,X6,X7,X8,X9,X10,X11,X12,LR8,CTR8,CR0,CR1,CR5,CR6,CR7]
    in
def ADDItlsldLADDR : PPCEmitTimePseudo<(outs g8rc:$rD),
                            (ins g8rc_nox0:$reg, s16imm64:$disp, tlsgd:$sym),
                            "#ADDItlsldLADDR",
                            [(set i64:$rD,
                              (PPCaddiTlsldLAddr i64:$reg,
                                                 tglobaltlsaddr:$disp,
                                                 tglobaltlsaddr:$sym))]>,
                     isPPC64;
def ADDISdtprelHA: PPCEmitTimePseudo<(outs g8rc:$rD), (ins g8rc_nox0:$reg, s16imm64:$disp),
                          "#ADDISdtprelHA",
                          [(set i64:$rD,
                            (PPCaddisDtprelHA i64:$reg,
                                              tglobaltlsaddr:$disp))]>,
                   isPPC64;
def ADDIdtprelL : PPCEmitTimePseudo<(outs g8rc:$rD), (ins g8rc_nox0:$reg, s16imm64:$disp),
                         "#ADDIdtprelL",
                         [(set i64:$rD,
                           (PPCaddiDtprelL i64:$reg, tglobaltlsaddr:$disp))]>,
                  isPPC64;
def PADDIdtprel : PPCEmitTimePseudo<(outs g8rc:$rD), (ins g8rc_nox0:$reg, s16imm64:$disp),
                          "#PADDIdtprel",
                          [(set i64:$rD,
                            (PPCpaddiDtprel i64:$reg, tglobaltlsaddr:$disp))]>,
                  isPPC64;

let PPC970_Unit = 2 in {
let Interpretation64Bit = 1, isCodeGenOnly = 1 in {
// Truncating stores.
def STB8 : DForm_1<38, (outs), (ins g8rc:$RST, (memri $D, $RA):$addr),
                   "stb $RST, $addr", IIC_LdStStore,
                   [(truncstorei8 i64:$RST, DForm:$addr)]>;
def STH8 : DForm_1<44, (outs), (ins g8rc:$RST, (memri $D, $RA):$addr),
                   "sth $RST, $addr", IIC_LdStStore,
                   [(truncstorei16 i64:$RST, DForm:$addr)]>;
def STW8 : DForm_1<36, (outs), (ins g8rc:$RST, (memri $D, $RA):$addr),
                   "stw $RST, $addr", IIC_LdStStore,
                   [(truncstorei32 i64:$RST, DForm:$addr)]>;
def STBX8 : XForm_8_memOp<31, 215, (outs), (ins g8rc:$RST, (memrr $RA, $RB):$addr),
                          "stbx $RST, $addr", IIC_LdStStore,
                          [(truncstorei8 i64:$RST, XForm:$addr)]>,
                          PPC970_DGroup_Cracked;
def STHX8 : XForm_8_memOp<31, 407, (outs), (ins g8rc:$RST, (memrr $RA, $RB):$addr),
                          "sthx $RST, $addr", IIC_LdStStore,
                          [(truncstorei16 i64:$RST, XForm:$addr)]>,
                          PPC970_DGroup_Cracked;
def STWX8 : XForm_8_memOp<31, 151, (outs), (ins g8rc:$RST, (memrr $RA, $RB):$addr),
                          "stwx $RST, $addr", IIC_LdStStore,
                          [(truncstorei32 i64:$RST, XForm:$addr)]>,
                          PPC970_DGroup_Cracked;
} // Interpretation64Bit

// Normal 8-byte stores.
def STD  : DSForm_1<62, 0, (outs), (ins g8rc:$RST, (memrix $D, $RA):$addr),
                    "std $RST, $addr", IIC_LdStSTD,
                    [(store i64:$RST, DSForm:$addr)]>, isPPC64;
def STDX  : XForm_8_memOp<31, 149, (outs), (ins g8rc:$RST, (memrr $RA, $RB):$addr),
                          "stdx $RST, $addr", IIC_LdStSTD,
                          [(store i64:$RST, XForm:$addr)]>, isPPC64,
                          PPC970_DGroup_Cracked;

let Predicates = [IsISA2_06] in {
def STDBRX: XForm_8_memOp<31, 660, (outs), (ins g8rc:$RST, (memrr $RA, $RB):$addr),
                          "stdbrx $RST, $addr", IIC_LdStStore,
                          [(PPCstbrx i64:$RST, ForceXForm:$addr, i64)]>, isPPC64,
                          PPC970_DGroup_Cracked;
}

let mayStore = 1, hasNoSchedulingInfo = 1 in {
// Normal 16-byte stores.
// TODO: Add scheduling info.
def STQ : DSForm_1<62, 2, (outs), (ins g8prc:$RST, (memrix $D, $RA):$addr),
                   "stq $RST, $addr", IIC_LdStSTQ,
                   []>, isPPC64;

def STQX_PSEUDO : PPCCustomInserterPseudo<(outs),
                                          (ins g8prc:$RSp, memrr:$dst),
                                          "#STQX_PSEUDO", []>;

def SPILL_QUADWORD : PPCEmitTimePseudo<(outs), (ins g8prc:$RSp, memrix:$dst),
                                       "#SPILL_QUADWORD", []>;
}

}

def BUILD_QUADWORD : PPCPostRAExpPseudo<
                       (outs g8prc:$RTp),
                       (ins g8rc:$lo, g8rc:$hi),
                       "#BUILD_QUADWORD", []>;

def : Pat<(int_ppc_atomic_store_i128 i64:$lo, i64:$hi, DSForm:$dst),
          (STQ (BUILD_QUADWORD g8rc:$lo, g8rc:$hi), memrix:$dst)>;

def : Pat<(int_ppc_atomic_store_i128 i64:$lo, i64:$hi, ForceXForm:$dst),
          (STQX_PSEUDO (BUILD_QUADWORD g8rc:$lo, g8rc:$hi), memrr:$dst)>;

// Stores with Update (pre-inc).
let PPC970_Unit = 2, mayStore = 1, mayLoad = 0 in {
let Interpretation64Bit = 1, isCodeGenOnly = 1 in {
def STBU8 : DForm_1<39, (outs ptr_rc_nor0:$ea_res), (ins g8rc:$RST, (memri $D, $RA):$addr),
                   "stbu $RST, $addr", IIC_LdStSTU, []>,
                   RegConstraint<"$addr.reg = $ea_res">, NoEncode<"$ea_res">;
def STHU8 : DForm_1<45, (outs ptr_rc_nor0:$ea_res), (ins g8rc:$RST, (memri $D, $RA):$addr),
                   "sthu $RST, $addr", IIC_LdStSTU, []>,
                   RegConstraint<"$addr.reg = $ea_res">, NoEncode<"$ea_res">;
def STWU8 : DForm_1<37, (outs ptr_rc_nor0:$ea_res), (ins g8rc:$RST, (memri $D, $RA):$addr),
                   "stwu $RST, $addr", IIC_LdStSTU, []>,
                   RegConstraint<"$addr.reg = $ea_res">, NoEncode<"$ea_res">;

def STBUX8: XForm_8_memOp<31, 247, (outs ptr_rc_nor0:$ea_res),
                          (ins g8rc:$RST, (memrr $RA, $RB):$addr),
                          "stbux $RST, $addr", IIC_LdStSTUX, []>,
                          RegConstraint<"$addr.ptrreg = $ea_res">,
                          NoEncode<"$ea_res">,
                          PPC970_DGroup_Cracked;
def STHUX8: XForm_8_memOp<31, 439, (outs ptr_rc_nor0:$ea_res),
                          (ins g8rc:$RST, (memrr $RA, $RB):$addr),
                          "sthux $RST, $addr", IIC_LdStSTUX, []>,
                          RegConstraint<"$addr.ptrreg = $ea_res">,
                          NoEncode<"$ea_res">,
                          PPC970_DGroup_Cracked;
def STWUX8: XForm_8_memOp<31, 183, (outs ptr_rc_nor0:$ea_res),
                          (ins g8rc:$RST, (memrr $RA, $RB):$addr),
                          "stwux $RST, $addr", IIC_LdStSTUX, []>,
                          RegConstraint<"$addr.ptrreg = $ea_res">,
                          NoEncode<"$ea_res">,
                          PPC970_DGroup_Cracked;
} // Interpretation64Bit

def STDU : DSForm_1<62, 1, (outs ptr_rc_nor0:$ea_res),
                   (ins g8rc:$RST, (memrix $D, $RA):$addr),
                   "stdu $RST, $addr", IIC_LdStSTU, []>,
                   RegConstraint<"$addr.reg = $ea_res">, NoEncode<"$ea_res">,
                   isPPC64;

def STDUX : XForm_8_memOp<31, 181, (outs ptr_rc_nor0:$ea_res),
                          (ins g8rc:$RST, (memrr $RA, $RB):$addr),
                          "stdux $RST, $addr", IIC_LdStSTUX, []>,
                          RegConstraint<"$addr.ptrreg = $ea_res">,
                          NoEncode<"$ea_res">,
                          PPC970_DGroup_Cracked, isPPC64;
}

// Patterns to match the pre-inc stores.  We can't put the patterns on
// the instruction definitions directly as ISel wants the address base
// and offset to be separate operands, not a single complex operand.
def : Pat<(pre_truncsti8 i64:$rS, iPTR:$ptrreg, iaddroff:$ptroff),
          (STBU8 $rS, iaddroff:$ptroff, $ptrreg)>;
def : Pat<(pre_truncsti16 i64:$rS, iPTR:$ptrreg, iaddroff:$ptroff),
          (STHU8 $rS, iaddroff:$ptroff, $ptrreg)>;
def : Pat<(pre_truncsti32 i64:$rS, iPTR:$ptrreg, iaddroff:$ptroff),
          (STWU8 $rS, iaddroff:$ptroff, $ptrreg)>;
def : Pat<(DSFormPreStore i64:$rS, iPTR:$ptrreg, iaddroff:$ptroff),
          (STDU $rS, iaddroff:$ptroff, $ptrreg)>;

def : Pat<(pre_truncsti8 i64:$rS, iPTR:$ptrreg, iPTR:$ptroff),
          (STBUX8 $rS, $ptrreg, $ptroff)>;
def : Pat<(pre_truncsti16 i64:$rS, iPTR:$ptrreg, iPTR:$ptroff),
          (STHUX8 $rS, $ptrreg, $ptroff)>;
def : Pat<(pre_truncsti32 i64:$rS, iPTR:$ptrreg, iPTR:$ptroff),
          (STWUX8 $rS, $ptrreg, $ptroff)>;
def : Pat<(pre_store i64:$rS, iPTR:$ptrreg, iPTR:$ptroff),
          (STDUX $rS, $ptrreg, $ptroff)>;


//===----------------------------------------------------------------------===//
// Floating point instructions.
//


let PPC970_Unit = 3, hasSideEffects = 0, mayRaiseFPException = 1,
    Uses = [RM] in {  // FPU Operations.
defm FCFID  : XForm_26r<63, 846, (outs f8rc:$RST), (ins f8rc:$RB),
                        "fcfid", "$RST, $RB", IIC_FPGeneral,
                        [(set f64:$RST, (PPCany_fcfid f64:$RB))]>, isPPC64;
defm FCTID  : XForm_26r<63, 814, (outs f8rc:$RST), (ins f8rc:$RB),
                        "fctid", "$RST, $RB", IIC_FPGeneral,
                        []>, isPPC64;
defm FCTIDU : XForm_26r<63, 942, (outs f8rc:$RST), (ins f8rc:$RB),
                        "fctidu", "$RST, $RB", IIC_FPGeneral,
                        []>, isPPC64;
defm FCTIDZ : XForm_26r<63, 815, (outs f8rc:$RST), (ins f8rc:$RB),
                        "fctidz", "$RST, $RB", IIC_FPGeneral,
                        [(set f64:$RST, (PPCany_fctidz f64:$RB))]>, isPPC64;

defm FCFIDU  : XForm_26r<63, 974, (outs f8rc:$RST), (ins f8rc:$RB),
                        "fcfidu", "$RST, $RB", IIC_FPGeneral,
                        [(set f64:$RST, (PPCany_fcfidu f64:$RB))]>, isPPC64;
defm FCFIDS  : XForm_26r<59, 846, (outs f4rc:$RST), (ins f8rc:$RB),
                        "fcfids", "$RST, $RB", IIC_FPGeneral,
                        [(set f32:$RST, (PPCany_fcfids f64:$RB))]>, isPPC64;
defm FCFIDUS : XForm_26r<59, 974, (outs f4rc:$RST), (ins f8rc:$RB),
                        "fcfidus", "$RST, $RB", IIC_FPGeneral,
                        [(set f32:$RST, (PPCany_fcfidus f64:$RB))]>, isPPC64;
defm FCTIDUZ : XForm_26r<63, 943, (outs f8rc:$RST), (ins f8rc:$RB),
                        "fctiduz", "$RST, $RB", IIC_FPGeneral,
                        [(set f64:$RST, (PPCany_fctiduz f64:$RB))]>, isPPC64;
defm FCTIWUZ : XForm_26r<63, 143, (outs f8rc:$RST), (ins f8rc:$RB),
                        "fctiwuz", "$RST, $RB", IIC_FPGeneral,
                        [(set f64:$RST, (PPCany_fctiwuz f64:$RB))]>, isPPC64;
}

// These instructions store a hash computed from the value of the link register
// and the value of the stack pointer.
let mayStore = 1, Interpretation64Bit = 1, isCodeGenOnly = 1 in {
def HASHST8 : XForm_XD6_RA5_RB5<31, 722, (outs),
                                (ins g8rc:$RB, (memrihash $D, $RA):$addr),
                                "hashst $RB, $addr", IIC_IntGeneral, []>;
def HASHSTP8 : XForm_XD6_RA5_RB5<31, 658, (outs),
                                 (ins g8rc:$RB, (memrihash $D, $RA):$addr),
                                 "hashstp $RB, $addr", IIC_IntGeneral, []>;
}

// These instructions check a hash computed from the value of the link register
// and the value of the stack pointer. The hasSideEffects flag is needed as the
// instruction may TRAP if the hash does not match the hash stored at the
// specified address.
let mayLoad = 1, hasSideEffects = 1,
    Interpretation64Bit = 1, isCodeGenOnly = 1 in {
def HASHCHK8 : XForm_XD6_RA5_RB5<31, 754, (outs),
                                 (ins g8rc:$RB, (memrihash $D, $RA):$addr),
                                 "hashchk $RB, $addr", IIC_IntGeneral, []>;
def HASHCHKP8 : XForm_XD6_RA5_RB5<31, 690, (outs),
                                  (ins g8rc:$RB, (memrihash $D, $RA):$addr),
                                  "hashchkp $RB, $addr", IIC_IntGeneral, []>;
}

let Interpretation64Bit = 1, isCodeGenOnly = 1, hasSideEffects = 1 in
def ADDEX8 : Z23Form_RTAB5_CY2<31, 170, (outs g8rc:$RT),
                              (ins g8rc:$RA, g8rc:$RB, u2imm:$CY),
                              "addex $RT, $RA, $RB, $CY", IIC_IntGeneral,
                              [(set i64:$RT, (int_ppc_addex i64:$RA, i64:$RB,
                                                            timm:$CY))]>;

//===----------------------------------------------------------------------===//
// Instruction Patterns
//

// Extensions and truncates to/from 32-bit regs.
def : Pat<(i64 (zext i32:$in)),
          (RLDICL (INSERT_SUBREG (i64 (IMPLICIT_DEF)), $in, sub_32),
                  0, 32)>;
def : Pat<(i64 (anyext i32:$in)),
          (INSERT_SUBREG (i64 (IMPLICIT_DEF)), $in, sub_32)>;
def : Pat<(i32 (trunc i64:$in)),
          (EXTRACT_SUBREG $in, sub_32)>;

// Implement the 'not' operation with the NOR instruction.
// (we could use the default xori pattern, but nor has lower latency on some
// cores (such as the A2)).
def i64not : OutPatFrag<(ops node:$in),
                        (NOR8 $in, $in)>;
def        : Pat<(not i64:$in),
                 (i64not $in)>;

// Extending loads with i64 targets.
def : Pat<(zextloadi1 DForm:$src),
          (LBZ8 DForm:$src)>;
def : Pat<(zextloadi1 XForm:$src),
          (LBZX8 XForm:$src)>;
def : Pat<(extloadi1 DForm:$src),
          (LBZ8 DForm:$src)>;
def : Pat<(extloadi1 XForm:$src),
          (LBZX8 XForm:$src)>;
def : Pat<(extloadi8 DForm:$src),
          (LBZ8 DForm:$src)>;
def : Pat<(extloadi8 XForm:$src),
          (LBZX8 XForm:$src)>;
def : Pat<(extloadi16 DForm:$src),
          (LHZ8 DForm:$src)>;
def : Pat<(extloadi16 XForm:$src),
          (LHZX8 XForm:$src)>;
def : Pat<(extloadi32 DForm:$src),
          (LWZ8 DForm:$src)>;
def : Pat<(extloadi32 XForm:$src),
          (LWZX8 XForm:$src)>;

// Standard shifts.  These are represented separately from the real shifts above
// so that we can distinguish between shifts that allow 6-bit and 7-bit shift
// amounts.
def : Pat<(sra i64:$rS, i32:$rB),
          (SRAD $rS, $rB)>;
def : Pat<(srl i64:$rS, i32:$rB),
          (SRD $rS, $rB)>;
def : Pat<(shl i64:$rS, i32:$rB),
          (SLD $rS, $rB)>;

// SUBFIC
def : Pat<(sub imm64SExt16:$imm, i64:$in),
          (SUBFIC8 $in, imm:$imm)>;

// SHL/SRL
def : Pat<(shl i64:$in, (i32 imm:$imm)),
          (RLDICR $in, imm:$imm, (SHL64 imm:$imm))>;
def : Pat<(srl i64:$in, (i32 imm:$imm)),
          (RLDICL $in, (SRL64 imm:$imm), imm:$imm)>;

// ROTL
def : Pat<(rotl i64:$in, i32:$sh),
          (RLDCL $in, $sh, 0)>;
def : Pat<(rotl i64:$in, (i32 imm:$imm)),
          (RLDICL $in, imm:$imm, 0)>;

// Hi and Lo for Darwin Global Addresses.
def : Pat<(PPChi tglobaladdr:$in, 0), (LIS8 tglobaladdr:$in)>;
def : Pat<(PPClo tglobaladdr:$in, 0), (LI8  tglobaladdr:$in)>;
def : Pat<(PPChi tconstpool:$in , 0), (LIS8 tconstpool:$in)>;
def : Pat<(PPClo tconstpool:$in , 0), (LI8  tconstpool:$in)>;
def : Pat<(PPChi tjumptable:$in , 0), (LIS8 tjumptable:$in)>;
def : Pat<(PPClo tjumptable:$in , 0), (LI8  tjumptable:$in)>;
def : Pat<(PPChi tblockaddress:$in, 0), (LIS8 tblockaddress:$in)>;
def : Pat<(PPClo tblockaddress:$in, 0), (LI8  tblockaddress:$in)>;
def : Pat<(PPChi tglobaltlsaddr:$g, i64:$in),
          (ADDIS8 $in, tglobaltlsaddr:$g)>;
def : Pat<(PPClo tglobaltlsaddr:$g, i64:$in),
          (ADDI8 $in, tglobaltlsaddr:$g)>;
def : Pat<(add i64:$in, (PPChi tglobaladdr:$g, 0)),
          (ADDIS8 $in, tglobaladdr:$g)>;
def : Pat<(add i64:$in, (PPChi tconstpool:$g, 0)),
          (ADDIS8 $in, tconstpool:$g)>;
def : Pat<(add i64:$in, (PPChi tjumptable:$g, 0)),
          (ADDIS8 $in, tjumptable:$g)>;
def : Pat<(add i64:$in, (PPChi tblockaddress:$g, 0)),
          (ADDIS8 $in, tblockaddress:$g)>;

// AIX 64-bit small code model TLS access.
// This is used for global dynamic accesses when loading the region handle and
// variable offset, and also for local-exec accesses to load the offset of a
// TLS variable from the TOC, prior to adding it to r13.
def : Pat<(i64 (PPCtoc_entry tglobaltlsaddr:$disp, i64:$reg)),
          (i64 (LDtoc tglobaltlsaddr:$disp, i64:$reg))>;

// The following pattern matches 64-bit local- and initial-exec TLS accesses on AIX.
// PPCaddTls is used in local- and initial-exec accesses in order to:
//   - Get the address of a variable (adding the variable offset to the thread
//     pointer in r13).
//   - Create an opportunity to optimize the user of the loaded address.
def : Pat<(PPCaddTls i64:$in, i64:$addr),
          (ADD8TLS $in, $addr)>;

// 64-bits atomic loads and stores
def : Pat<(atomic_load_64 DSForm:$src), (LD  memrix:$src)>;
def : Pat<(atomic_load_64 XForm:$src),  (LDX memrr:$src)>;

def : Pat<(atomic_store_64 i64:$val, DSForm:$ptr), (STD  g8rc:$val, memrix:$ptr)>;
def : Pat<(atomic_store_64 i64:$val, XForm:$ptr), (STDX g8rc:$val, memrr:$ptr)>;

let Predicates = [IsISA3_0, In64BitMode] in {
def : Pat<(i64 (int_ppc_cmpeqb g8rc:$a, g8rc:$b)),
          (i64 (SETB8 (CMPEQB $a, $b)))>;
def : Pat<(i64 (int_ppc_setb g8rc:$a, g8rc:$b)),
          (i64 (SETB8 (CMPD $a, $b)))>;
def : Pat<(i64 (int_ppc_maddhd g8rc:$a, g8rc:$b, g8rc:$c)),
          (i64 (MADDHD $a, $b, $c))>;
def : Pat<(i64 (int_ppc_maddhdu g8rc:$a, g8rc:$b, g8rc:$c)),
          (i64 (MADDHDU $a, $b, $c))>;
def : Pat<(i64 (int_ppc_maddld g8rc:$a, g8rc:$b, g8rc:$c)),
          (i64 (MADDLD8 $a, $b, $c))>;
}

let Predicates = [In64BitMode] in {
def : Pat<(i64 (int_ppc_mulhd g8rc:$a, g8rc:$b)),
          (i64 (MULHD $a, $b))>;
def : Pat<(i64 (int_ppc_mulhdu g8rc:$a, g8rc:$b)),
          (i64 (MULHDU $a, $b))>;
def : Pat<(int_ppc_load8r ForceXForm:$ptr),
          (LDBRX ForceXForm:$ptr)>;
def : Pat<(int_ppc_store8r g8rc:$a, ForceXForm:$ptr),
          (STDBRX g8rc:$a, ForceXForm:$ptr)>;
}

def : Pat<(i64 (int_ppc_cmpb g8rc:$a, g8rc:$b)),
          (i64 (CMPB8 $a, $b))>;

let Predicates = [IsISA3_0] in {
// DARN (deliver random number)
// L=0 for 32-bit, L=1 for conditioned random, L=2 for raw random
def : Pat<(int_ppc_darn32), (EXTRACT_SUBREG (DARN 0), sub_32)>;
def : Pat<(int_ppc_darn), (DARN 1)>;
def : Pat<(int_ppc_darnraw), (DARN 2)>;

class X_RA5_RB5<bits<6> opcode, bits<10> xo, string opc, RegisterOperand ty,
                   InstrItinClass itin, list<dag> pattern>
  : X_L1_RS5_RS5<opcode, xo, (outs), (ins ty:$RA, ty:$RB, u1imm:$L),
                 !strconcat(opc, " $RA, $RB"), itin, pattern>{
   let L = 1;
}

class X_L1_RA5_RB5<bits<6> opcode, bits<10> xo, string opc, RegisterOperand ty,
                   InstrItinClass itin, list<dag> pattern>
  : X_L1_RS5_RS5<opcode, xo, (outs), (ins ty:$RA, ty:$RB, u1imm:$L),
                 !strconcat(opc, " $RA, $RB, $L"), itin, pattern>;

let Interpretation64Bit = 1, isCodeGenOnly = 1 in {
def CP_COPY8   : X_RA5_RB5<31, 774, "copy"  , g8rc, IIC_LdStCOPY, []>;
def CP_PASTE8_rec : X_L1_RA5_RB5<31, 902, "paste.", g8rc, IIC_LdStPASTE, []>,isRecordForm;
}

// SLB Invalidate Entry Global
def SLBIEG : XForm_26<31, 466, (outs), (ins gprc:$RST, gprc:$RB),
                      "slbieg $RST, $RB", IIC_SprSLBIEG, []>;
// SLB Synchronize
def SLBSYNC : XForm_0<31, 338, (outs), (ins), "slbsync", IIC_SprSLBSYNC, []>;

} // IsISA3_0

def : Pat<(int_ppc_stdcx ForceXForm:$dst, g8rc:$A),
          (RLWINM (STDCX g8rc:$A, ForceXForm:$dst), 31, 31, 31)>;
def : Pat<(PPCStoreCond ForceXForm:$dst, g8rc:$A, 8),
          (RLWINM (STDCX g8rc:$A, ForceXForm:$dst), 31, 31, 31)>;

def : Pat<(i64 (int_ppc_mfspr timm:$SPR)),
          (MFSPR8 $SPR)>;
def : Pat<(int_ppc_mtspr timm:$SPR, g8rc:$RT),
          (MTSPR8 $SPR, $RT)>;

//===-- M68kInstrControl.td - Control Flow Instructions ----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file describes the M68k jump, return, call, and related instructions.
/// Here is the current status of the file:
///
///  Machine:
///
///       BRA   [x]     BSR  [~]     Bcc [~]     DBcc [ ]     FBcc [ ]
///       FDBcc [ ]     FNOP [ ]     FPn [ ]     FScc [ ]     FTST [ ]
///       JMP   [~]     JSR  [x]     NOP [x]     RTD  [!]     RTR  [ ]
///       RTS   [x]     Scc  [~]     TST [ ]
///
///  Pseudo:
///
///          RET [x]
///    TCRETURNj [x]   TCRETURNq [x]
///     TAILJMPj [x]    TAILJMPq [x]
///
///  Map:
///
///   [ ] - was not touched at all
///   [!] - requires extarnal stuff implemented
///   [~] - in progress but usable
///   [x] - done
///
///
///                                   NOTE
///      Though branch and jump instructions are using memory operands they
///      DO NOT read the jump address from memory, they just calculate EA
///      and jump there.
///
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// NOP
//===----------------------------------------------------------------------===//

let hasSideEffects = 0 in {
  def NOP : MxInst<(outs), (ins), "nop", []> {
    let Inst = (descend 0b0100, 0b1110, 0b0111, 0b0001);
  }
}


//===----------------------------------------------------------------------===//
// Conditions
//===----------------------------------------------------------------------===//

/// CC—Carry clear      GE—Greater than or equal
/// LS—Lower or same    PL—Plus
/// CS—Carry set        GT—Greater than
/// LT—Less than        T—Always true*
/// EQ—Equal            HI—Higher
/// MI—Minus            VC—Overflow clear
/// F—Never true*       LE—Less than or equal
/// NE—Not equal        VS—Overflow set
///
/// *Not applicable to the Bcc instructions.
class MxEncCondOp<bits<4> cond> {
  dag Value = (descend cond);
}

def MxCCt  : MxEncCondOp<0b0000>;
def MxCCf  : MxEncCondOp<0b0001>;
def MxCChi : MxEncCondOp<0b0010>;
def MxCCls : MxEncCondOp<0b0011>;
def MxCCcc : MxEncCondOp<0b0100>;
def MxCCcs : MxEncCondOp<0b0101>;
def MxCCne : MxEncCondOp<0b0110>;
def MxCCeq : MxEncCondOp<0b0111>;
def MxCCvc : MxEncCondOp<0b1000>;
def MxCCvs : MxEncCondOp<0b1001>;
def MxCCpl : MxEncCondOp<0b1010>;
def MxCCmi : MxEncCondOp<0b1011>;
def MxCCge : MxEncCondOp<0b1100>;
def MxCClt : MxEncCondOp<0b1101>;
def MxCCgt : MxEncCondOp<0b1110>;
def MxCCle : MxEncCondOp<0b1111>;



/// --------------------------------+---------+---------
///  F  E  D  C | B  A  9  8 | 7  6 | 5  4  3 | 2  1  0
/// --------------------------------+---------+---------
///  0  1  0  1 | CONDITION  | 1  1 |   MODE  |   REG
/// ----------------------------------------------------

let Uses = [CCR] in {
class MxSccR<string CC>
    : MxInst<(outs MxDRD8:$dst), (ins), "s"#CC#"\t$dst",
             [(set i8:$dst, (MxSetCC !cast<PatLeaf>("MxCOND"#CC), CCR))]> {
  let Inst = (descend 0b0101, !cast<MxEncCondOp>("MxCC"#CC).Value, 0b11,
              /*MODE without last bit*/0b00,
              /*REGISTER prefixed with D/A bit*/(operand "$dst", 4));
}

class MxSccM<string CC, MxOperand MEMOpd, ComplexPattern MEMPat, MxEncMemOp DST_ENC>
    : MxInst<(outs), (ins MEMOpd:$dst), "s"#CC#"\t$dst",
             [(store (MxSetCC !cast<PatLeaf>("MxCOND"#CC), CCR), MEMPat:$dst)]> {
  let Inst =
    (ascend
      (descend 0b0101, !cast<MxEncCondOp>("MxCC"#CC).Value, 0b11, DST_ENC.EA),
      DST_ENC.Supplement
    );
}
}

foreach cc = [ "cc", "ls", "lt", "eq", "mi", "f", "ne", "ge",
               "cs", "pl", "gt", "t", "hi", "vc", "le", "vs"] in {
def SET#"d8"#cc : MxSccR<cc>;
def SET#"j8"#cc : MxSccM<cc, MxType8.JOp, MxType8.JPat, MxEncAddrMode_j<"dst">>;
def SET#"p8"#cc : MxSccM<cc, MxType8.POp, MxType8.PPat, MxEncAddrMode_p<"dst">>;
}

//===----------------------------------------------------------------------===//
// Jumps
//===----------------------------------------------------------------------===//

///------------------------------+---------+---------
/// F  E  D  C  B  A  9  8  7  6 | 5  4  3 | 2  1  0
///------------------------------+---------+---------
/// 0  1  0  0  1  1  1  0  1  1 |  MODE   |   REG
///------------------------------+---------+---------
let isBranch = 1, isTerminator = 1, isBarrier = 1, isIndirectBranch = 1 in
class MxJMP<MxOperand LOCOp, MxEncMemOp DST_ENC>
    : MxInst<(outs), (ins LOCOp:$dst), "jmp\t$dst", [(brind iPTR:$dst)]> {
  let Inst =
    (ascend
      (descend 0b0100, 0b1110, 0b11, DST_ENC.EA),
      DST_ENC.Supplement
    );
}

def JMP32j : MxJMP<MxARI32, MxEncAddrMode_j<"dst">>;


// FIXME Support 16 bit indirect jump.
// Currently M68k does not allow 16 bit indirect jumps use sext operands
// def JMP16r     : MxInst<(outs), (ins M68k_ARI16:$dst),
//                             "jmp\t$dst",
//                             [(brind AR16:$dst)]>;

//===----------------------------------------------------------------------===//
// Branches
//===----------------------------------------------------------------------===//

/// --------------------------------------------------
///  F  E  D  C | B  A  9  8 | 7  6  5  4  3  2  1  0
/// --------------------------------------------------
///  0  1  1  0 | CONDITION |   8-BIT DISPLACEMENT
/// --------------------------------------------------
///  16-BIT DISPLACEMENT IF 8-BIT DISPLACEMENT = $00
/// --------------------------------------------------
///  32-BIT DISPLACEMENT IF 8-BIT DISPLACEMENT = $FF
/// --------------------------------------------------
let isBranch = 1, isTerminator = 1, Uses = [CCR] in
class MxBcc<string cc, Operand TARGET, dag disp_8, dag disp_16_32>
    : MxInst<(outs), (ins TARGET:$dst), "b"#cc#"\t$dst", []> {
  // FIXME: If we want to avoid supplying disp_16_32 with empty
  //        (ascend) for 16/32 bits variants, we can use conditional
  //        bang operator like this:
  //        ```
  //        class MxBcc<string cc, Operand TARGET, int SIZE>
  //        ...
  //        let Inst = !cond(
  //            !eq(SIZE, 8):   /* encoding for Bcc8  */
  //            !eq(SIZE, 16):  /* encoding for Bcc16 */
  //            !eq(SIZE, 32):  /* encoding for Bcc32 */
  //        );
  let Inst =
      (ascend
        (descend 0b0110, !cast<MxEncCondOp>("MxCC"#cc).Value, disp_8),
        disp_16_32
      );
}

foreach cc = [ "cc", "ls", "lt", "eq", "mi", "ne", "ge",
               "cs", "pl", "gt", "hi", "vc", "le", "vs"] in {
  def B#cc#"8"
    : MxBcc<cc, MxBrTarget8,
            (operand "$dst", 8, (encoder "encodePCRelImm<8>")), (ascend)>;

  def B#cc#"16"
    : MxBcc<cc, MxBrTarget16, (descend 0b0000, 0b0000),
            (operand "$dst", 16, (encoder "encodePCRelImm<16>"))>;
}

foreach cc = [ "cc", "ls", "lt", "eq", "mi", "ne", "ge",
               "cs", "pl", "gt", "hi", "vc", "le", "vs"] in {
def : Pat<(MxBrCond bb:$target, !cast<PatLeaf>("MxCOND"#cc), CCR),
          (!cast<Instruction>("B"#cc#"8") MxBrTarget8:$target)>;
}

/// -------------------------------------------------
///  F  E  D  C  B  A  9  8 | 7  6  5  4  3  2  1  0
/// -------------------------------------------------
///  0  1  1  0  0  0  0  0 |   8-BIT DISPLACEMENT
/// -------------------------------------------------
///  16-BIT DISPLACEMENT IF 8-BIT DISPLACEMENT = $00
/// -------------------------------------------------
///  32-BIT DISPLACEMENT IF 8-BIT DISPLACEMENT = $FF
/// -------------------------------------------------
let isBranch = 1, isTerminator = 1, isBarrier = 1 in
class MxBra<Operand TARGET, dag disp_8, dag disp_16_32>
    : MxInst<(outs), (ins TARGET:$dst), "bra\t$dst", []> {
  let Inst =
    (ascend
      (descend 0b0110, 0b0000, disp_8),
      disp_16_32
    );
}

def BRA8  : MxBra<MxBrTarget8,
                  (operand "$dst", 8, (encoder "encodePCRelImm<8>")), (ascend)>;

def BRA16 : MxBra<MxBrTarget16, (descend 0b0000, 0b0000),
                  (operand "$dst", 16, (encoder "encodePCRelImm<16>"))>;

def : Pat<(br bb:$target), (BRA8 MxBrTarget8:$target)>;

/// -------------------------------------------------
///  F  E  D  C  B  A  9  8 | 7  6  5  4  3  2  1  0
/// -------------------------------------------------
///  0  1  1  0  0  0  0  1 |   8-BIT DISPLACEMENT
/// -------------------------------------------------
///  16-BIT DISPLACEMENT IF 8-BIT DISPLACEMENT = $00
/// -------------------------------------------------
///  32-BIT DISPLACEMENT IF 8-BIT DISPLACEMENT = $FF
/// -------------------------------------------------

let isBranch = 1, isTerminator = 1 in
class MxBsr<Operand TARGET, MxType TYPE, dag disp_8, dag disp_16_32>
    : MxInst<(outs), (ins TARGET:$dst), "bsr."#TYPE.Prefix#"\t$dst"> {
  let Inst = (ascend
                (descend 0b0110, 0b0001, disp_8),
                 disp_16_32
              );
}

def BSR8 : MxBsr<MxBrTarget8, MxType8,
                (operand "$dst", 8, (encoder "encodePCRelImm<8>")), (ascend)>;

def BSR16 : MxBsr<MxBrTarget16, MxType16, (descend 0b0000, 0b0000),
                (operand "$dst", 16, (encoder "encodePCRelImm<16>"))>;

def BSR32 : MxBsr<MxBrTarget32, MxType32, (descend 0b1111, 0b1111),
                (operand "$dst", 32, (encoder "encodePCRelImm<32>"),
                                     (decoder "DecodeImm32"))>;

//===----------------------------------------------------------------------===//
// Call
//===----------------------------------------------------------------------===//

// All calls clobber the non-callee saved registers. %SP is marked as
// a use to prevent stack-pointer assignments that appear immediately
// before calls from potentially appearing dead. Uses for argument
// registers are added manually.
let Uses = [SP] in
let isCall = 1 in
///------------------------------+---------+---------
/// F  E  D  C  B  A  9  8  7  6 | 5  4  3 | 2  1  0
///------------------------------+---------+---------
/// 0  1  0  0  1  1  1  0  1  0 |  MODE   |   REG
///------------------------------+---------+---------
class MxCall<MxOperand LOCOp, MxEncMemOp DST_ENC>
    : MxInst<(outs), (ins LOCOp:$dst), "jsr\t$dst", []> {
  let Inst =
    (ascend
      (descend 0b0100, 0b1110, 0b10, DST_ENC.EA),
      DST_ENC.Supplement
    );
}

def CALLk : MxCall<MxPCI32, MxEncAddrMode_k<"dst">>;
def CALLq : MxCall<MxPCD32, MxEncAddrMode_q<"dst">>;
def CALLb : MxCall<MxAL32,  MxEncAddrMode_abs<"dst", true>>;
def CALLj : MxCall<MxARI32, MxEncAddrMode_j<"dst">>;

multiclass CallPat<MxCall callOp, Predicate pred> {
  let Predicates = [pred] in {
    def : Pat<(MxCall (i32 tglobaladdr:$dst)),  (callOp tglobaladdr:$dst)>;
    def : Pat<(MxCall (i32 texternalsym:$dst)), (callOp texternalsym:$dst)>;
    def : Pat<(MxCall (i32 imm:$dst)),          (callOp imm:$dst)>;
  }
}

defm : CallPat<CALLq, IsPIC>;
defm : CallPat<CALLb, IsNotPIC>;

def : Pat<(MxCall iPTR:$dst), (CALLj MxARI32:$dst)>;

//===----------------------------------------------------------------------===//
// Tail Call
//===----------------------------------------------------------------------===//

let isCodeGenOnly = 1 in {
let Uses = [SP] in {
let isCall = 1, isTerminator = 1, isBarrier = 1 in {

let isReturn = 1 in
def TCRETURNq : MxPseudo<(outs), (ins MxPCD32:$dst,    i32imm:$adj)>;
def TAILJMPq  : MxPseudo<(outs), (ins MxPCD32:$dst)>;

// NOTE j does not mean load and jump M68k jmp just calculates EA and jumps
// and it is using Mem form like (An) thus j letter.
let isReturn = 1 in
def TCRETURNj : MxPseudo<(outs), (ins MxARI32_TC:$dst, i32imm:$adj)>;
def TAILJMPj  : MxPseudo<(outs), (ins MxARI32_TC:$dst)>;
} // isCall = 1, isTerminator = 1, isBarrier = 1
} // Uses = [SP]
} // isCodeGenOnly = 1

//===----------------------------------------------------------------------===//
// Return
//===----------------------------------------------------------------------===//

let isTerminator = 1, isReturn = 1, isBarrier = 1, hasCtrlDep = 1 in {

def RTS : MxInst<(outs), (ins), "rts", []> {
  let Inst = (descend 0b0100, 0b1110, 0b0111, 0b0101);
}

def RTE: MxInst<(outs), (ins), "rte", []> {
  let Inst = (descend 0b0100, 0b1110, 0b0111, 0b0011);
}

let isCodeGenOnly = 1 in
def RET : MxPseudo<(outs), (ins i32imm:$adj, variable_ops),
                   [(MxRet timm:$adj)]>;
} // isTerminator = 1, isReturn = 1, isBarrier = 1, hasCtrlDep = 1

//===----------------------------------------------------------------------===//
// SETCC_C Patterns
//===----------------------------------------------------------------------===//

// Use subx to materialize carry bit.
let Uses = [CCR], Defs = [CCR], isPseudo = 1 in {
// FIXME These are pseudo ops that should be replaced with Pat<> patterns.
// However, Pat<> can't replicate the destination reg into the inputs of the
// result.
def SETCS_C8d : MxPseudo<(outs MxDRD8:$dst), (ins),
                         [(set MxDRD8:$dst, (MxSetCC_C MxCONDcs, CCR))]>;
def SETCS_C16d : MxPseudo<(outs MxDRD16:$dst), (ins),
                          [(set MxDRD16:$dst, (MxSetCC_C MxCONDcs, CCR))]>;
def SETCS_C32d : MxPseudo<(outs MxXRD32:$dst), (ins),
                          [(set MxXRD32:$dst, (MxSetCC_C MxCONDcs, CCR))]>;
} // Uses = [CCR], Defs = [CCR], isPseudo = 1


def : Pat<(i16 (anyext (i8 (MxSetCC_C MxCONDcs, CCR)))), (SETCS_C16d)>;
def : Pat<(i32 (anyext (i8 (MxSetCC_C MxCONDcs, CCR)))), (SETCS_C32d)>;

def : Pat<(i16 (sext (i8 (MxSetCC_C MxCONDcs, CCR)))), (SETCS_C16d)>;
def : Pat<(i32 (sext (i8 (MxSetCC_C MxCONDcs, CCR)))), (SETCS_C32d)>;

// We canonicalize 'scs' to "(and (subx reg,reg), 1)" on the hope that the and
// will be eliminated and that the subx can be extended up to a wider type.  When
// this happens, it is great.  However, if we are left with an 8-bit subx and an
// and, we might as well just match it as a setb.
def : Pat<(and (i8 (MxSetCC_C MxCONDcs, CCR)), 1), (SETd8cs)>;

// (add OP, SETB) -> (addx OP, (move 0))
def : Pat<(add (and (i8 (MxSetCC_C MxCONDcs, CCR)), 1), MxDRD8:$op),
          (ADDX8dd MxDRD8:$op, (MOV8di 0))>;
def : Pat<(add (and (i32 (MxSetCC_C MxCONDcs, CCR)), 1), MxXRD32:$op),
          (ADDX32dd MxDRD32:$op, (MOV32ri 0))>;

// (sub OP, SETB) -> (subx OP, (move 0))
def : Pat<(sub MxDRD8:$op, (and (i8 (MxSetCC_C MxCONDcs, CCR)), 1)),
          (SUBX8dd MxDRD8:$op, (MOV8di 0))>;
def : Pat<(sub MxXRD32:$op, (and (i32 (MxSetCC_C MxCONDcs, CCR)), 1)),
          (SUBX32dd MxDRD32:$op, (MOV32ri 0))>;

// (sub OP, SETCC_CARRY) -> (addx OP, (move 0))
def : Pat<(sub MxDRD8:$op, (i8 (MxSetCC_C MxCONDcs, CCR))),
          (ADDX8dd MxDRD8:$op, (MOV8di 0))>;
def : Pat<(sub MxXRD32:$op, (i32 (MxSetCC_C MxCONDcs, CCR))),
          (ADDX32dd MxDRD32:$op, (MOV32ri 0))>;

//===------------===//
// Trap / Breakpoint
//===------------===//

let RenderMethod = "addImmOperands", ParserMethod = "parseImm" in {
  def MxTrapImm : AsmOperandClass {
    let Name = "MxTrapImm";
    let PredicateMethod = "isTrapImm";
  }

  def MxBkptImm : AsmOperandClass {
    let Name = "MxBkptImm";
    let PredicateMethod = "isBkptImm";
  }
}

let ParserMatchClass = MxTrapImm in
def MxTrapimm : MxOp<i8,  MxSize8,  "i">;

let ParserMatchClass = MxBkptImm in
def MxBkptimm : MxOp<i8,  MxSize8,  "i">;

def TRAP : MxInst<(outs), (ins MxTrapimm:$vect), "trap\t$vect", []> {
  let Inst = (descend 0b0100, 0b1110, 0b0100, (operand "$vect", 4));
}

def TRAPV : MxInst<(outs), (ins), "trapv", []> {
  let Inst = (descend 0b0100, 0b1110, 0b0111, 0b0110);
}

def BKPT : MxInst<(outs), (ins MxBkptimm:$vect), "bkpt\t$vect", []> {
  let Inst = (descend 0b0100, 0b1000, 0b0100, 0b1 , (operand "$vect", 3));
}

def ILLEGAL : MxInst<(outs), (ins), "illegal", []> {
  let Inst = (descend 0b0100, 0b1010, 0b1111, 0b1100);
}

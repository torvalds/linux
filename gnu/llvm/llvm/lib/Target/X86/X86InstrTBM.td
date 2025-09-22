//====-- X86InstrTBM.td - TBM X86 Instruction Definition -*- tablegen -*-=====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defining the TBM X86 instructions.
// 
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// TBM Instructions
//
let Predicates = [HasTBM], Defs = [EFLAGS] in {

multiclass tbm_bextri<bits<8> opc, RegisterClass RC, string OpcodeStr,
                      X86MemOperand x86memop, PatFrag ld_frag,
                      SDNode OpNode, Operand immtype,
                      SDPatternOperator immoperator,
                      X86FoldableSchedWrite Sched> {
  def ri : Ii32<opc,  MRMSrcReg, (outs RC:$dst), (ins RC:$src1, immtype:$cntl),
                !strconcat(OpcodeStr,
                           "\t{$cntl, $src1, $dst|$dst, $src1, $cntl}"),
                [(set RC:$dst, (OpNode RC:$src1, immoperator:$cntl))]>,
                XOP, XOPA, Sched<[Sched]>;
  def mi : Ii32<opc,  MRMSrcMem, (outs RC:$dst),
                (ins x86memop:$src1, immtype:$cntl),
                !strconcat(OpcodeStr,
                           "\t{$cntl, $src1, $dst|$dst, $src1, $cntl}"),
                [(set RC:$dst, (OpNode (ld_frag addr:$src1), immoperator:$cntl))]>,
                XOP, XOPA, Sched<[Sched.Folded]>;
}

defm BEXTRI32 : tbm_bextri<0x10, GR32, "bextr{l}", i32mem, loadi32,
                           X86bextri, i32imm, timm, WriteBEXTR>;
let ImmT = Imm32S in
defm BEXTRI64 : tbm_bextri<0x10, GR64, "bextr{q}", i64mem, loadi64,
                           X86bextri, i64i32imm,
                           i64timmSExt32, WriteBEXTR>, REX_W;

multiclass tbm_binary_rm<bits<8> opc, Format FormReg, Format FormMem,
                         RegisterClass RC, string OpcodeStr,
                         X86MemOperand x86memop, X86FoldableSchedWrite Sched> {
let hasSideEffects = 0 in {
  def rr : I<opc,  FormReg, (outs RC:$dst), (ins RC:$src),
             !strconcat(OpcodeStr,"\t{$src, $dst|$dst, $src}"), []>,
             XOP, VVVV, XOP9, Sched<[Sched]>;
  let mayLoad = 1 in
  def rm : I<opc,  FormMem, (outs RC:$dst), (ins x86memop:$src),
             !strconcat(OpcodeStr,"\t{$src, $dst|$dst, $src}"), []>,
             XOP, VVVV, XOP9, Sched<[Sched.Folded]>;
}
}

multiclass tbm_binary_intr<bits<8> opc, string OpcodeStr,
                           X86FoldableSchedWrite Sched,
                           Format FormReg, Format FormMem> {
  defm NAME#32 : tbm_binary_rm<opc, FormReg, FormMem, GR32, OpcodeStr#"{l}",
                               i32mem, Sched>;
  defm NAME#64 : tbm_binary_rm<opc, FormReg, FormMem, GR64, OpcodeStr#"{q}",
                               i64mem, Sched>, REX_W;
}

defm BLCFILL : tbm_binary_intr<0x01, "blcfill", WriteALU, MRM1r, MRM1m>;
defm BLCI    : tbm_binary_intr<0x02, "blci", WriteALU, MRM6r, MRM6m>;
defm BLCIC   : tbm_binary_intr<0x01, "blcic", WriteALU, MRM5r, MRM5m>;
defm BLCMSK  : tbm_binary_intr<0x02, "blcmsk", WriteALU, MRM1r, MRM1m>;
defm BLCS    : tbm_binary_intr<0x01, "blcs", WriteALU, MRM3r, MRM3m>;
defm BLSFILL : tbm_binary_intr<0x01, "blsfill", WriteALU, MRM2r, MRM2m>;
defm BLSIC   : tbm_binary_intr<0x01, "blsic", WriteALU, MRM6r, MRM6m>;
defm T1MSKC  : tbm_binary_intr<0x01, "t1mskc", WriteALU, MRM7r, MRM7m>;
defm TZMSK   : tbm_binary_intr<0x01, "tzmsk", WriteALU, MRM4r, MRM4m>;
} // HasTBM, EFLAGS

// Use BEXTRI for 64-bit 'and' with large immediate 'mask'.
let Predicates = [HasTBM] in {
  def : Pat<(and GR64:$src, AndMask64:$mask),
            (BEXTRI64ri GR64:$src, (BEXTRMaskXForm imm:$mask))>;

  def : Pat<(and (loadi64 addr:$src), AndMask64:$mask),
            (BEXTRI64mi addr:$src, (BEXTRMaskXForm imm:$mask))>;
}

//===----------------------------------------------------------------------===//
// Pattern fragments to auto generate TBM instructions.
//===----------------------------------------------------------------------===//

let Predicates = [HasTBM] in {
  // FIXME: patterns for the load versions are not implemented
  def : Pat<(and GR32:$src, (add GR32:$src, 1)),
            (BLCFILL32rr GR32:$src)>;
  def : Pat<(and GR64:$src, (add GR64:$src, 1)),
            (BLCFILL64rr GR64:$src)>;

  def : Pat<(or GR32:$src, (not (add GR32:$src, 1))),
            (BLCI32rr GR32:$src)>;
  def : Pat<(or GR64:$src, (not (add GR64:$src, 1))),
            (BLCI64rr GR64:$src)>;

  // Extra patterns because opt can optimize the above patterns to this.
  def : Pat<(or GR32:$src, (sub -2, GR32:$src)),
            (BLCI32rr GR32:$src)>;
  def : Pat<(or GR64:$src, (sub -2, GR64:$src)),
            (BLCI64rr GR64:$src)>;

  def : Pat<(and (not GR32:$src), (add GR32:$src, 1)),
            (BLCIC32rr GR32:$src)>;
  def : Pat<(and (not GR64:$src), (add GR64:$src, 1)),
            (BLCIC64rr GR64:$src)>;

  def : Pat<(xor GR32:$src, (add GR32:$src, 1)),
            (BLCMSK32rr GR32:$src)>;
  def : Pat<(xor GR64:$src, (add GR64:$src, 1)),
            (BLCMSK64rr GR64:$src)>;

  def : Pat<(or GR32:$src, (add GR32:$src, 1)),
            (BLCS32rr GR32:$src)>;
  def : Pat<(or GR64:$src, (add GR64:$src, 1)),
            (BLCS64rr GR64:$src)>;

  def : Pat<(or GR32:$src, (add GR32:$src, -1)),
            (BLSFILL32rr GR32:$src)>;
  def : Pat<(or GR64:$src, (add GR64:$src, -1)),
            (BLSFILL64rr GR64:$src)>;

  def : Pat<(or (not GR32:$src), (add GR32:$src, -1)),
            (BLSIC32rr GR32:$src)>;
  def : Pat<(or (not GR64:$src), (add GR64:$src, -1)),
            (BLSIC64rr GR64:$src)>;

  def : Pat<(or (not GR32:$src), (add GR32:$src, 1)),
            (T1MSKC32rr GR32:$src)>;
  def : Pat<(or (not GR64:$src), (add GR64:$src, 1)),
            (T1MSKC64rr GR64:$src)>;

  def : Pat<(and (not GR32:$src), (add GR32:$src, -1)),
            (TZMSK32rr GR32:$src)>;
  def : Pat<(and (not GR64:$src), (add GR64:$src, -1)),
            (TZMSK64rr GR64:$src)>;

  // Patterns to match flag producing ops.
  def : Pat<(and_flag_nocf GR32:$src, (add GR32:$src, 1)),
            (BLCFILL32rr GR32:$src)>;
  def : Pat<(and_flag_nocf GR64:$src, (add GR64:$src, 1)),
            (BLCFILL64rr GR64:$src)>;

  def : Pat<(or_flag_nocf GR32:$src, (not (add GR32:$src, 1))),
            (BLCI32rr GR32:$src)>;
  def : Pat<(or_flag_nocf GR64:$src, (not (add GR64:$src, 1))),
            (BLCI64rr GR64:$src)>;

  // Extra patterns because opt can optimize the above patterns to this.
  def : Pat<(or_flag_nocf GR32:$src, (sub -2, GR32:$src)),
            (BLCI32rr GR32:$src)>;
  def : Pat<(or_flag_nocf GR64:$src, (sub -2, GR64:$src)),
            (BLCI64rr GR64:$src)>;

  def : Pat<(and_flag_nocf (not GR32:$src), (add GR32:$src, 1)),
            (BLCIC32rr GR32:$src)>;
  def : Pat<(and_flag_nocf (not GR64:$src), (add GR64:$src, 1)),
            (BLCIC64rr GR64:$src)>;

  def : Pat<(xor_flag_nocf GR32:$src, (add GR32:$src, 1)),
            (BLCMSK32rr GR32:$src)>;
  def : Pat<(xor_flag_nocf GR64:$src, (add GR64:$src, 1)),
            (BLCMSK64rr GR64:$src)>;

  def : Pat<(or_flag_nocf GR32:$src, (add GR32:$src, 1)),
            (BLCS32rr GR32:$src)>;
  def : Pat<(or_flag_nocf GR64:$src, (add GR64:$src, 1)),
            (BLCS64rr GR64:$src)>;

  def : Pat<(or_flag_nocf GR32:$src, (add GR32:$src, -1)),
            (BLSFILL32rr GR32:$src)>;
  def : Pat<(or_flag_nocf GR64:$src, (add GR64:$src, -1)),
            (BLSFILL64rr GR64:$src)>;

  def : Pat<(or_flag_nocf (not GR32:$src), (add GR32:$src, -1)),
            (BLSIC32rr GR32:$src)>;
  def : Pat<(or_flag_nocf (not GR64:$src), (add GR64:$src, -1)),
            (BLSIC64rr GR64:$src)>;

  def : Pat<(or_flag_nocf (not GR32:$src), (add GR32:$src, 1)),
            (T1MSKC32rr GR32:$src)>;
  def : Pat<(or_flag_nocf (not GR64:$src), (add GR64:$src, 1)),
            (T1MSKC64rr GR64:$src)>;

  def : Pat<(and_flag_nocf (not GR32:$src), (add GR32:$src, -1)),
            (TZMSK32rr GR32:$src)>;
  def : Pat<(and_flag_nocf (not GR64:$src), (add GR64:$src, -1)),
            (TZMSK64rr GR64:$src)>;
} // HasTBM

//===-- X86Instr3DNow.td - The 3DNow! Instruction Set ------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the 3DNow! instruction set, which extends MMX to support
// floating point and also adds a few more random instructions for good measure.
//
//===----------------------------------------------------------------------===//

class I3DNow<bits<8> o, Format F, dag outs, dag ins, string asm, list<dag> pat>
      : I<o, F, outs, ins, asm, pat> {
}

class I3DNow_binop<bits<8> o, Format F, dag ins, string Mnemonic, list<dag> pat>
      : I3DNow<o, F, (outs VR64:$dst), ins,
          !strconcat(Mnemonic, "\t{$src2, $dst|$dst, $src2}"), pat>, ThreeDNow {
  let Constraints = "$src1 = $dst";
}

class I3DNow_conv<bits<8> o, Format F, dag ins, string Mnemonic, list<dag> pat>
      : I3DNow<o, F, (outs VR64:$dst), ins,
          !strconcat(Mnemonic, "\t{$src, $dst|$dst, $src}"), pat>, ThreeDNow;

multiclass I3DNow_binop_rm<bits<8> opc, string Mn,
                           X86FoldableSchedWrite sched, bit Commutable = 0> {
  let mayStore=0, hasSideEffects=0 in {
    let isCommutable = Commutable, mayLoad=0 in
    def rr : I3DNow_binop<opc, MRMSrcReg, (ins VR64:$src1, VR64:$src2), Mn,
      []>, Sched<[sched]>;
    let mayLoad=1 in
    def rm : I3DNow_binop<opc, MRMSrcMem, (ins VR64:$src1, i64mem:$src2), Mn,
      []>, Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

multiclass I3DNow_conv_rm<bits<8> opc, string Mn,
                              X86FoldableSchedWrite sched> {
  let mayStore=0, hasSideEffects=0 in {
    let mayLoad=0 in
    def rr : I3DNow_conv<opc, MRMSrcReg, (ins VR64:$src), Mn,
      []>, Sched<[sched]>;
    let mayLoad=1 in
    def rm : I3DNow_conv<opc, MRMSrcMem, (ins i64mem:$src), Mn,
      []>, Sched<[sched.Folded, sched.ReadAfterFold]>;
  }
}

defm PAVGUSB  : I3DNow_binop_rm<0xBF, "pavgusb", SchedWriteVecALU.MMX, 1>;
defm PF2ID    : I3DNow_conv_rm<0x1D, "pf2id", WriteCvtPS2I>;
defm PFACC    : I3DNow_binop_rm<0xAE, "pfacc", WriteFAdd>;
defm PFADD    : I3DNow_binop_rm<0x9E, "pfadd", WriteFAdd, 1>;
defm PFCMPEQ  : I3DNow_binop_rm<0xB0, "pfcmpeq", WriteFAdd, 1>;
defm PFCMPGE  : I3DNow_binop_rm<0x90, "pfcmpge", WriteFAdd>;
defm PFCMPGT  : I3DNow_binop_rm<0xA0, "pfcmpgt", WriteFAdd>;
defm PFMAX    : I3DNow_binop_rm<0xA4, "pfmax", WriteFAdd>;
defm PFMIN    : I3DNow_binop_rm<0x94, "pfmin", WriteFAdd>;
defm PFMUL    : I3DNow_binop_rm<0xB4, "pfmul", WriteFAdd, 1>;
defm PFRCP    : I3DNow_conv_rm<0x96, "pfrcp", WriteFAdd>;
defm PFRCPIT1 : I3DNow_binop_rm<0xA6, "pfrcpit1", WriteFAdd>;
defm PFRCPIT2 : I3DNow_binop_rm<0xB6, "pfrcpit2", WriteFAdd>;
defm PFRSQIT1 : I3DNow_binop_rm<0xA7, "pfrsqit1", WriteFAdd>;
defm PFRSQRT  : I3DNow_conv_rm<0x97, "pfrsqrt", WriteFAdd>;
defm PFSUB    : I3DNow_binop_rm<0x9A, "pfsub", WriteFAdd, 1>;
defm PFSUBR   : I3DNow_binop_rm<0xAA, "pfsubr", WriteFAdd, 1>;
defm PI2FD    : I3DNow_conv_rm<0x0D, "pi2fd", WriteCvtI2PS>;
defm PMULHRW  : I3DNow_binop_rm<0xB7, "pmulhrw", SchedWriteVecIMul.MMX, 1>;

let SchedRW = [WriteEMMS], mayLoad=1, mayStore=1, hasSideEffects=1 in
def FEMMS : I3DNow<0x0E, RawFrm, (outs), (ins), "femms",
                   []>, TB;

let SchedRW = [WriteLoad], mayLoad=1, mayStore=1, hasSideEffects=0 in {
def PREFETCH : I3DNow<0x0D, MRM0m, (outs), (ins i8mem:$addr),
                      "prefetch\t$addr",
                      []>, TB;

// Note: PREFETCHW is the only instruction in this file which is NOT specific to 3DNow!
def PREFETCHW : I<0x0D, MRM1m, (outs), (ins i8mem:$addr), "prefetchw\t$addr",
                  [(prefetch addr:$addr, (i32 1), (i32 PrefetchWLevel), (i32 1))]>,
                  TB, Requires<[HasPrefetchW]>;

def PREFETCHWT1 : I<0x0D, MRM2m, (outs), (ins i8mem:$addr), "prefetchwt1\t$addr",
                    []>, TB;
}

// "3DNowA" instructions
defm PF2IW    : I3DNow_conv_rm<0x1C, "pf2iw", WriteCvtPS2I>;
defm PI2FW    : I3DNow_conv_rm<0x0C, "pi2fw", WriteCvtI2PS>;
defm PFNACC   : I3DNow_binop_rm<0x8A, "pfnacc", WriteFAdd, 0>;
defm PFPNACC  : I3DNow_binop_rm<0x8E, "pfpnacc", WriteFAdd, 0>;
defm PSWAPD   : I3DNow_conv_rm<0xBB, "pswapd", SchedWriteShuffle.MMX>;

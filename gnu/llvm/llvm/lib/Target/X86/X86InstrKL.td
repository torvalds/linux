//===---------------------------*-tablegen-*-------------------------------===//
//===------------- X86InstrKL.td - KL Instruction Set Extension -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the instructions that make up the Intel key locker
// instruction set.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Key Locker instructions
class Encodekey<bits<8> opcode, string m>
  : I<opcode, MRMSrcReg, (outs GR32:$dst), (ins GR32:$src), m#"\t{$src, $dst|$dst, $src}", []>,
    NoCD8, XS;

multiclass Aesencdec<string suffix> {
 def AESENC128KL#suffix : I<0xDC, MRMSrcMem, (outs VR128:$dst),
                            (ins VR128:$src1, opaquemem:$src2),
                            "aesenc128kl\t{$src2, $src1|$src1, $src2}",
                            [(set VR128:$dst, EFLAGS, (X86aesenc128kl VR128:$src1, addr:$src2))]>,
                          NoCD8, XS;
 def AESDEC128KL#suffix : I<0xDD, MRMSrcMem, (outs VR128:$dst),
                            (ins VR128:$src1, opaquemem:$src2),
                            "aesdec128kl\t{$src2, $src1|$src1, $src2}",
                            [(set VR128:$dst, EFLAGS, (X86aesdec128kl VR128:$src1, addr:$src2))]>,
                          NoCD8, XS;
 def AESENC256KL#suffix : I<0xDE, MRMSrcMem, (outs VR128:$dst),
                            (ins VR128:$src1, opaquemem:$src2),
                            "aesenc256kl\t{$src2, $src1|$src1, $src2}",
                            [(set VR128:$dst, EFLAGS, (X86aesenc256kl VR128:$src1, addr:$src2))]>,
                          NoCD8, XS;
 def AESDEC256KL#suffix : I<0xDF, MRMSrcMem, (outs VR128:$dst),
                            (ins VR128:$src1, opaquemem:$src2),
                            "aesdec256kl\t{$src2, $src1|$src1, $src2}",
                            [(set VR128:$dst, EFLAGS, (X86aesdec256kl VR128:$src1, addr:$src2))]>,
                          NoCD8, XS;
}

let SchedRW = [WriteSystem] in {
  let Uses = [XMM0, EAX], Defs = [EFLAGS], Predicates = [HasKL] in {
    def LOADIWKEY : I<0xDC, MRMSrcReg, (outs), (ins VR128:$src1, VR128:$src2),
                      "loadiwkey\t{$src2, $src1|$src1, $src2}",
                      [(int_x86_loadiwkey XMM0, VR128:$src1, VR128:$src2, EAX)]>, T8, XS;
  }

  let Predicates = [HasKL] in {
    let Uses = [XMM0], Defs = [XMM0, XMM1, XMM2, XMM4, XMM5, XMM6, EFLAGS] in
      def ENCODEKEY128 : Encodekey<0xFA, "encodekey128">, T8;

    let Uses = [XMM0, XMM1], Defs = [XMM0, XMM1, XMM2, XMM3, XMM4, XMM5, XMM6, EFLAGS] in
      def ENCODEKEY256 : Encodekey<0xFB, "encodekey256">, T8;

    let Constraints = "$src1 = $dst", Defs = [EFLAGS] in
      defm "" : Aesencdec<"">, T8;
  }
} // SchedRW

multiclass Aesencdecwide<string suffix> {
  def AESENCWIDE128KL#suffix : I<0xD8, MRM0m, (outs), (ins opaquemem:$src), "aesencwide128kl\t$src", []>, NoCD8, XS;
  def AESDECWIDE128KL#suffix : I<0xD8, MRM1m, (outs), (ins opaquemem:$src), "aesdecwide128kl\t$src", []>, NoCD8, XS;
  def AESENCWIDE256KL#suffix : I<0xD8, MRM2m, (outs), (ins opaquemem:$src), "aesencwide256kl\t$src", []>, NoCD8, XS;
  def AESDECWIDE256KL#suffix : I<0xD8, MRM3m, (outs), (ins opaquemem:$src), "aesdecwide256kl\t$src", []>, NoCD8, XS;
}

let SchedRW = [WriteSystem], Uses = [XMM0, XMM1, XMM2, XMM3, XMM4, XMM5, XMM6, XMM7],
    Defs = [EFLAGS, XMM0, XMM1, XMM2, XMM3, XMM4, XMM5, XMM6, XMM7], mayLoad = 1 in {
  let Predicates = [HasWIDEKL] in
    defm "" : Aesencdecwide<"">, T8;
} // SchedRW

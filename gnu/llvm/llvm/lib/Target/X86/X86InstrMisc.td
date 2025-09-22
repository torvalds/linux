//===-- X86InstrMisc.td - Misc X86 Instruction Definition -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defining the misc X86 instructions.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Instruction list.
//

// Nop
let hasSideEffects = 0, SchedRW = [WriteNop] in {
  def NOOP : I<0x90, RawFrm, (outs), (ins), "nop", []>;
  def NOOPW : I<0x1f, MRMXm, (outs), (ins i16mem:$zero),
                "nop{w}\t$zero", []>, TB, OpSize16;
  def NOOPL : I<0x1f, MRMXm, (outs), (ins i32mem:$zero),
                "nop{l}\t$zero", []>, TB, OpSize32;
  def NOOPQ : RI<0x1f, MRMXm, (outs), (ins i64mem:$zero),
                "nop{q}\t$zero", []>, TB, Requires<[In64BitMode]>;
  // Also allow register so we can assemble/disassemble
  def NOOPWr : I<0x1f, MRMXr, (outs), (ins GR16:$zero),
                 "nop{w}\t$zero", []>, TB, OpSize16;
  def NOOPLr : I<0x1f, MRMXr, (outs), (ins GR32:$zero),
                 "nop{l}\t$zero", []>, TB, OpSize32;
  def NOOPQr : RI<0x1f, MRMXr, (outs), (ins GR64:$zero),
                  "nop{q}\t$zero", []>, TB, Requires<[In64BitMode]>;
}


// Constructing a stack frame.
def ENTER : Ii16<0xC8, RawFrmImm8, (outs), (ins i16imm:$len, i8imm:$lvl),
                 "enter\t$len, $lvl", []>, Sched<[WriteMicrocoded]>;

let SchedRW = [WriteALU] in {
let Defs = [EBP, ESP], Uses = [EBP, ESP], mayLoad = 1, hasSideEffects=0 in
def LEAVE    : I<0xC9, RawFrm, (outs), (ins), "leave", []>,
                 Requires<[Not64BitMode]>;

let Defs = [RBP,RSP], Uses = [RBP,RSP], mayLoad = 1, hasSideEffects = 0 in
def LEAVE64  : I<0xC9, RawFrm, (outs), (ins), "leave", []>,
                 Requires<[In64BitMode]>;
} // SchedRW

//===----------------------------------------------------------------------===//
//  Miscellaneous Instructions.
//

let isBarrier = 1, hasSideEffects = 1, usesCustomInserter = 1,
    SchedRW = [WriteSystem] in
  def Int_eh_sjlj_setup_dispatch
    : PseudoI<(outs), (ins), [(X86eh_sjlj_setup_dispatch)]>;

let Defs = [ESP], Uses = [ESP], hasSideEffects=0 in {
let mayLoad = 1, SchedRW = [WriteLoad] in {
def POP16r  : I<0x58, AddRegFrm, (outs GR16:$reg), (ins), "pop{w}\t$reg", []>,
                OpSize16;
def POP32r  : I<0x58, AddRegFrm, (outs GR32:$reg), (ins), "pop{l}\t$reg", []>,
                OpSize32, Requires<[Not64BitMode]>;
// Long form for the disassembler.
let isCodeGenOnly = 1, ForceDisassemble = 1 in {
def POP16rmr: I<0x8F, MRM0r, (outs GR16:$reg), (ins), "pop{w}\t$reg", []>,
                OpSize16;
def POP32rmr: I<0x8F, MRM0r, (outs GR32:$reg), (ins), "pop{l}\t$reg", []>,
                OpSize32, Requires<[Not64BitMode]>;
} // isCodeGenOnly = 1, ForceDisassemble = 1
} // mayLoad, SchedRW
let mayStore = 1, mayLoad = 1, SchedRW = [WriteCopy] in {
def POP16rmm: I<0x8F, MRM0m, (outs), (ins i16mem:$dst), "pop{w}\t$dst", []>,
                OpSize16;
def POP32rmm: I<0x8F, MRM0m, (outs), (ins i32mem:$dst), "pop{l}\t$dst", []>,
                OpSize32, Requires<[Not64BitMode]>;
} // mayStore, mayLoad, SchedRW

let mayStore = 1, SchedRW = [WriteStore] in {
def PUSH16r  : I<0x50, AddRegFrm, (outs), (ins GR16:$reg), "push{w}\t$reg",[]>,
                 OpSize16;
def PUSH32r  : I<0x50, AddRegFrm, (outs), (ins GR32:$reg), "push{l}\t$reg",[]>,
                 OpSize32, Requires<[Not64BitMode]>;
// Long form for the disassembler.
let isCodeGenOnly = 1, ForceDisassemble = 1 in {
def PUSH16rmr: I<0xFF, MRM6r, (outs), (ins GR16:$reg), "push{w}\t$reg",[]>,
                 OpSize16;
def PUSH32rmr: I<0xFF, MRM6r, (outs), (ins GR32:$reg), "push{l}\t$reg",[]>,
                 OpSize32, Requires<[Not64BitMode]>;
} // isCodeGenOnly = 1, ForceDisassemble = 1

def PUSH16i8 : Ii8<0x6a, RawFrm, (outs), (ins i16i8imm:$imm),
                   "push{w}\t$imm", []>, OpSize16;
def PUSH16i  : Ii16<0x68, RawFrm, (outs), (ins i16imm:$imm),
                   "push{w}\t$imm", []>, OpSize16;

def PUSH32i8 : Ii8<0x6a, RawFrm, (outs), (ins i32i8imm:$imm),
                   "push{l}\t$imm", []>, OpSize32,
                   Requires<[Not64BitMode]>;
def PUSH32i  : Ii32<0x68, RawFrm, (outs), (ins i32imm:$imm),
                   "push{l}\t$imm", []>, OpSize32,
                   Requires<[Not64BitMode]>;
} // mayStore, SchedRW

let mayLoad = 1, mayStore = 1, SchedRW = [WriteCopy] in {
def PUSH16rmm: I<0xFF, MRM6m, (outs), (ins i16mem:$src), "push{w}\t$src", []>,
                 OpSize16;
def PUSH32rmm: I<0xFF, MRM6m, (outs), (ins i32mem:$src), "push{l}\t$src", []>,
                 OpSize32, Requires<[Not64BitMode]>;
} // mayLoad, mayStore, SchedRW

}

let isPseudo = 1, mayLoad = 1, mayStore = 1,
    SchedRW = [WriteRMW], Defs = [ESP] in {
  let Uses = [ESP] in
  def RDFLAGS32 : PseudoI<(outs GR32:$dst), (ins),
                   [(set GR32:$dst, (int_x86_flags_read_u32))]>,
                Requires<[Not64BitMode]>;

  let Uses = [RSP] in
  def RDFLAGS64 : PseudoI<(outs GR64:$dst), (ins),
                   [(set GR64:$dst, (int_x86_flags_read_u64))]>,
                Requires<[In64BitMode]>;
}

let isPseudo = 1, mayLoad = 1, mayStore = 1,
    SchedRW = [WriteRMW] in {
  let Defs = [ESP, EFLAGS, DF], Uses = [ESP] in
  def WRFLAGS32 : PseudoI<(outs), (ins GR32:$src),
                   [(int_x86_flags_write_u32 GR32:$src)]>,
                Requires<[Not64BitMode]>;

  let Defs = [RSP, EFLAGS, DF], Uses = [RSP] in
  def WRFLAGS64 : PseudoI<(outs), (ins GR64:$src),
                   [(int_x86_flags_write_u64 GR64:$src)]>,
                Requires<[In64BitMode]>;
}

let Defs = [ESP, EFLAGS, DF], Uses = [ESP], mayLoad = 1, hasSideEffects=0,
    SchedRW = [WriteLoad] in {
def POPF16   : I<0x9D, RawFrm, (outs), (ins), "popf{w}", []>, OpSize16;
def POPF32   : I<0x9D, RawFrm, (outs), (ins), "popf{l|d}", []>, OpSize32,
                 Requires<[Not64BitMode]>;
}

let Defs = [ESP], Uses = [ESP, EFLAGS, DF], mayStore = 1, hasSideEffects=0,
    SchedRW = [WriteStore] in {
def PUSHF16  : I<0x9C, RawFrm, (outs), (ins), "pushf{w}", []>, OpSize16;
def PUSHF32  : I<0x9C, RawFrm, (outs), (ins), "pushf{l|d}", []>, OpSize32,
                 Requires<[Not64BitMode]>;
}

let Defs = [RSP], Uses = [RSP], hasSideEffects=0 in {
let mayLoad = 1, SchedRW = [WriteLoad] in {
def POP64r   : I<0x58, AddRegFrm, (outs GR64:$reg), (ins), "pop{q}\t$reg", []>,
                 OpSize32, Requires<[In64BitMode]>;
// Long form for the disassembler.
let isCodeGenOnly = 1, ForceDisassemble = 1 in {
def POP64rmr: I<0x8F, MRM0r, (outs GR64:$reg), (ins), "pop{q}\t$reg", []>,
                OpSize32, Requires<[In64BitMode]>;
} // isCodeGenOnly = 1, ForceDisassemble = 1
def POPP64r  : I<0x58, AddRegFrm, (outs GR64:$reg), (ins), "popp\t$reg", []>,
                 REX_W, ExplicitREX2Prefix, Requires<[In64BitMode]>;
def POP2: I<0x8F, MRM0r, (outs GR64:$reg1, GR64:$reg2), (ins),
            "pop2\t{$reg2, $reg1|$reg1, $reg2}",
            []>, EVEX, VVVV, EVEX_B, T_MAP4;
def POP2P: I<0x8F, MRM0r, (outs GR64:$reg1, GR64:$reg2), (ins),
             "pop2p\t{$reg2, $reg1|$reg1, $reg2}",
             []>, EVEX, VVVV, EVEX_B, T_MAP4, REX_W;

} // mayLoad, SchedRW
let mayLoad = 1, mayStore = 1, SchedRW = [WriteCopy] in
def POP64rmm: I<0x8F, MRM0m, (outs), (ins i64mem:$dst), "pop{q}\t$dst", []>,
                OpSize32, Requires<[In64BitMode]>;
let mayStore = 1, SchedRW = [WriteStore] in {
def PUSH64r  : I<0x50, AddRegFrm, (outs), (ins GR64:$reg), "push{q}\t$reg", []>,
                 OpSize32, Requires<[In64BitMode]>;
// Long form for the disassembler.
let isCodeGenOnly = 1, ForceDisassemble = 1 in {
def PUSH64rmr: I<0xFF, MRM6r, (outs), (ins GR64:$reg), "push{q}\t$reg", []>,
                 OpSize32, Requires<[In64BitMode]>;
} // isCodeGenOnly = 1, ForceDisassemble = 1
def PUSHP64r  : I<0x50, AddRegFrm, (outs), (ins GR64:$reg), "pushp\t$reg", []>,
                  REX_W, ExplicitREX2Prefix, Requires<[In64BitMode]>;
def PUSH2: I<0xFF, MRM6r, (outs), (ins GR64:$reg1, GR64:$reg2),
            "push2\t{$reg2, $reg1|$reg1, $reg2}",
            []>, EVEX, VVVV, EVEX_B, T_MAP4;
def PUSH2P: I<0xFF, MRM6r, (outs), (ins GR64:$reg1, GR64:$reg2),
             "push2p\t{$reg2, $reg1|$reg1, $reg2}",
             []>, EVEX, VVVV, EVEX_B, T_MAP4, REX_W;
} // mayStore, SchedRW
let mayLoad = 1, mayStore = 1, SchedRW = [WriteCopy] in {
def PUSH64rmm: I<0xFF, MRM6m, (outs), (ins i64mem:$src), "push{q}\t$src", []>,
                 OpSize32, Requires<[In64BitMode]>;
} // mayLoad, mayStore, SchedRW
}

let Defs = [RSP], Uses = [RSP], hasSideEffects = 0, mayStore = 1,
    SchedRW = [WriteStore] in {
def PUSH64i8   : Ii8<0x6a, RawFrm, (outs), (ins i64i8imm:$imm),
                    "push{q}\t$imm", []>, OpSize32,
                    Requires<[In64BitMode]>;
def PUSH64i32  : Ii32S<0x68, RawFrm, (outs), (ins i64i32imm:$imm),
                    "push{q}\t$imm", []>, OpSize32,
                    Requires<[In64BitMode]>;
}

let Defs = [RSP, EFLAGS, DF], Uses = [RSP], mayLoad = 1, hasSideEffects=0 in
def POPF64   : I<0x9D, RawFrm, (outs), (ins), "popfq", []>,
               OpSize32, Requires<[In64BitMode]>, Sched<[WriteLoad]>;
let Defs = [RSP], Uses = [RSP, EFLAGS, DF], mayStore = 1, hasSideEffects=0 in
def PUSHF64    : I<0x9C, RawFrm, (outs), (ins), "pushfq", []>,
                 OpSize32, Requires<[In64BitMode]>, Sched<[WriteStore]>;

let Defs = [EDI, ESI, EBP, EBX, EDX, ECX, EAX, ESP], Uses = [ESP],
    mayLoad = 1, hasSideEffects = 0, SchedRW = [WriteLoad] in {
def POPA32   : I<0x61, RawFrm, (outs), (ins), "popal", []>,
               OpSize32, Requires<[Not64BitMode]>;
def POPA16   : I<0x61, RawFrm, (outs), (ins), "popaw", []>,
               OpSize16, Requires<[Not64BitMode]>;
}
let Defs = [ESP], Uses = [EDI, ESI, EBP, EBX, EDX, ECX, EAX, ESP],
    mayStore = 1, hasSideEffects = 0, SchedRW = [WriteStore] in {
def PUSHA32  : I<0x60, RawFrm, (outs), (ins), "pushal", []>,
               OpSize32, Requires<[Not64BitMode]>;
def PUSHA16  : I<0x60, RawFrm, (outs), (ins), "pushaw", []>,
               OpSize16, Requires<[Not64BitMode]>;
}

let Constraints = "$src = $dst", SchedRW = [WriteBSWAP32], Predicates = [NoNDD_Or_NoMOVBE] in {
// This instruction is a consequence of BSWAP32r observing operand size. The
// encoding is valid, but the behavior is undefined.
let isCodeGenOnly = 1, ForceDisassemble = 1, hasSideEffects = 0 in
def BSWAP16r_BAD : I<0xC8, AddRegFrm, (outs GR16:$dst), (ins GR16:$src),
                     "bswap{w}\t$dst", []>, OpSize16, TB;
// GR32 = bswap GR32
def BSWAP32r : I<0xC8, AddRegFrm, (outs GR32:$dst), (ins GR32:$src),
                 "bswap{l}\t$dst",
                 [(set GR32:$dst, (bswap GR32:$src))]>, OpSize32, TB;

let SchedRW = [WriteBSWAP64] in
def BSWAP64r : RI<0xC8, AddRegFrm, (outs GR64:$dst), (ins GR64:$src),
                  "bswap{q}\t$dst",
                  [(set GR64:$dst, (bswap GR64:$src))]>, TB;
} // Constraints = "$src = $dst", SchedRW

// Bit scan instructions.
let Defs = [EFLAGS] in {
def BSF16rr  : I<0xBC, MRMSrcReg, (outs GR16:$dst), (ins GR16:$src),
                 "bsf{w}\t{$src, $dst|$dst, $src}",
                 [(set GR16:$dst, EFLAGS, (X86bsf GR16:$src))]>,
                  TB, OpSize16, Sched<[WriteBSF]>;
def BSF16rm  : I<0xBC, MRMSrcMem, (outs GR16:$dst), (ins i16mem:$src),
                 "bsf{w}\t{$src, $dst|$dst, $src}",
                 [(set GR16:$dst, EFLAGS, (X86bsf (loadi16 addr:$src)))]>,
                 TB, OpSize16, Sched<[WriteBSFLd]>;
def BSF32rr  : I<0xBC, MRMSrcReg, (outs GR32:$dst), (ins GR32:$src),
                 "bsf{l}\t{$src, $dst|$dst, $src}",
                 [(set GR32:$dst, EFLAGS, (X86bsf GR32:$src))]>,
                 TB, OpSize32, Sched<[WriteBSF]>;
def BSF32rm  : I<0xBC, MRMSrcMem, (outs GR32:$dst), (ins i32mem:$src),
                 "bsf{l}\t{$src, $dst|$dst, $src}",
                 [(set GR32:$dst, EFLAGS, (X86bsf (loadi32 addr:$src)))]>,
                 TB, OpSize32, Sched<[WriteBSFLd]>;
def BSF64rr  : RI<0xBC, MRMSrcReg, (outs GR64:$dst), (ins GR64:$src),
                  "bsf{q}\t{$src, $dst|$dst, $src}",
                  [(set GR64:$dst, EFLAGS, (X86bsf GR64:$src))]>,
                  TB, Sched<[WriteBSF]>;
def BSF64rm  : RI<0xBC, MRMSrcMem, (outs GR64:$dst), (ins i64mem:$src),
                  "bsf{q}\t{$src, $dst|$dst, $src}",
                  [(set GR64:$dst, EFLAGS, (X86bsf (loadi64 addr:$src)))]>,
                  TB, Sched<[WriteBSFLd]>;

def BSR16rr  : I<0xBD, MRMSrcReg, (outs GR16:$dst), (ins GR16:$src),
                 "bsr{w}\t{$src, $dst|$dst, $src}",
                 [(set GR16:$dst, EFLAGS, (X86bsr GR16:$src))]>,
                 TB, OpSize16, Sched<[WriteBSR]>;
def BSR16rm  : I<0xBD, MRMSrcMem, (outs GR16:$dst), (ins i16mem:$src),
                 "bsr{w}\t{$src, $dst|$dst, $src}",
                 [(set GR16:$dst, EFLAGS, (X86bsr (loadi16 addr:$src)))]>,
                 TB, OpSize16, Sched<[WriteBSRLd]>;
def BSR32rr  : I<0xBD, MRMSrcReg, (outs GR32:$dst), (ins GR32:$src),
                 "bsr{l}\t{$src, $dst|$dst, $src}",
                 [(set GR32:$dst, EFLAGS, (X86bsr GR32:$src))]>,
                 TB, OpSize32, Sched<[WriteBSR]>;
def BSR32rm  : I<0xBD, MRMSrcMem, (outs GR32:$dst), (ins i32mem:$src),
                 "bsr{l}\t{$src, $dst|$dst, $src}",
                 [(set GR32:$dst, EFLAGS, (X86bsr (loadi32 addr:$src)))]>,
                 TB, OpSize32, Sched<[WriteBSRLd]>;
def BSR64rr  : RI<0xBD, MRMSrcReg, (outs GR64:$dst), (ins GR64:$src),
                  "bsr{q}\t{$src, $dst|$dst, $src}",
                  [(set GR64:$dst, EFLAGS, (X86bsr GR64:$src))]>,
                  TB, Sched<[WriteBSR]>;
def BSR64rm  : RI<0xBD, MRMSrcMem, (outs GR64:$dst), (ins i64mem:$src),
                  "bsr{q}\t{$src, $dst|$dst, $src}",
                  [(set GR64:$dst, EFLAGS, (X86bsr (loadi64 addr:$src)))]>,
                  TB, Sched<[WriteBSRLd]>;
} // Defs = [EFLAGS]

let SchedRW = [WriteMicrocoded] in {
let Defs = [EDI,ESI], Uses = [EDI,ESI,DF] in {
def MOVSB : I<0xA4, RawFrmDstSrc, (outs), (ins dstidx8:$dst, srcidx8:$src),
              "movsb\t{$src, $dst|$dst, $src}", []>;
def MOVSW : I<0xA5, RawFrmDstSrc, (outs), (ins dstidx16:$dst, srcidx16:$src),
              "movsw\t{$src, $dst|$dst, $src}", []>, OpSize16;
def MOVSL : I<0xA5, RawFrmDstSrc, (outs), (ins dstidx32:$dst, srcidx32:$src),
              "movs{l|d}\t{$src, $dst|$dst, $src}", []>, OpSize32;
def MOVSQ : RI<0xA5, RawFrmDstSrc, (outs), (ins dstidx64:$dst, srcidx64:$src),
               "movsq\t{$src, $dst|$dst, $src}", []>,
               Requires<[In64BitMode]>;
}

let Defs = [EDI], Uses = [AL,EDI,DF] in
def STOSB : I<0xAA, RawFrmDst, (outs), (ins dstidx8:$dst),
              "stosb\t{%al, $dst|$dst, al}", []>;
let Defs = [EDI], Uses = [AX,EDI,DF] in
def STOSW : I<0xAB, RawFrmDst, (outs), (ins dstidx16:$dst),
              "stosw\t{%ax, $dst|$dst, ax}", []>, OpSize16;
let Defs = [EDI], Uses = [EAX,EDI,DF] in
def STOSL : I<0xAB, RawFrmDst, (outs), (ins dstidx32:$dst),
              "stos{l|d}\t{%eax, $dst|$dst, eax}", []>, OpSize32;
let Defs = [RDI], Uses = [RAX,RDI,DF] in
def STOSQ : RI<0xAB, RawFrmDst, (outs), (ins dstidx64:$dst),
               "stosq\t{%rax, $dst|$dst, rax}", []>,
               Requires<[In64BitMode]>;

let Defs = [EDI,EFLAGS], Uses = [AL,EDI,DF] in
def SCASB : I<0xAE, RawFrmDst, (outs), (ins dstidx8:$dst),
              "scasb\t{$dst, %al|al, $dst}", []>;
let Defs = [EDI,EFLAGS], Uses = [AX,EDI,DF] in
def SCASW : I<0xAF, RawFrmDst, (outs), (ins dstidx16:$dst),
              "scasw\t{$dst, %ax|ax, $dst}", []>, OpSize16;
let Defs = [EDI,EFLAGS], Uses = [EAX,EDI,DF] in
def SCASL : I<0xAF, RawFrmDst, (outs), (ins dstidx32:$dst),
              "scas{l|d}\t{$dst, %eax|eax, $dst}", []>, OpSize32;
let Defs = [EDI,EFLAGS], Uses = [RAX,EDI,DF] in
def SCASQ : RI<0xAF, RawFrmDst, (outs), (ins dstidx64:$dst),
               "scasq\t{$dst, %rax|rax, $dst}", []>,
               Requires<[In64BitMode]>;

let Defs = [EDI,ESI,EFLAGS], Uses = [EDI,ESI,DF] in {
def CMPSB : I<0xA6, RawFrmDstSrc, (outs), (ins dstidx8:$dst, srcidx8:$src),
              "cmpsb\t{$dst, $src|$src, $dst}", []>;
def CMPSW : I<0xA7, RawFrmDstSrc, (outs), (ins dstidx16:$dst, srcidx16:$src),
              "cmpsw\t{$dst, $src|$src, $dst}", []>, OpSize16;
def CMPSL : I<0xA7, RawFrmDstSrc, (outs), (ins dstidx32:$dst, srcidx32:$src),
              "cmps{l|d}\t{$dst, $src|$src, $dst}", []>, OpSize32;
def CMPSQ : RI<0xA7, RawFrmDstSrc, (outs), (ins dstidx64:$dst, srcidx64:$src),
               "cmpsq\t{$dst, $src|$src, $dst}", []>,
               Requires<[In64BitMode]>;
}
} // SchedRW

//===----------------------------------------------------------------------===//
//  Move Instructions.
//
let SchedRW = [WriteMove] in {
let hasSideEffects = 0, isMoveReg = 1 in {
def MOV8rr  : I<0x88, MRMDestReg, (outs GR8 :$dst), (ins GR8 :$src),
                "mov{b}\t{$src, $dst|$dst, $src}", []>;
def MOV16rr : I<0x89, MRMDestReg, (outs GR16:$dst), (ins GR16:$src),
                "mov{w}\t{$src, $dst|$dst, $src}", []>, OpSize16;
def MOV32rr : I<0x89, MRMDestReg, (outs GR32:$dst), (ins GR32:$src),
                "mov{l}\t{$src, $dst|$dst, $src}", []>, OpSize32;
def MOV64rr : RI<0x89, MRMDestReg, (outs GR64:$dst), (ins GR64:$src),
                 "mov{q}\t{$src, $dst|$dst, $src}", []>;
}

let isReMaterializable = 1, isAsCheapAsAMove = 1, isMoveImm = 1 in {
def MOV8ri  : Ii8 <0xB0, AddRegFrm, (outs GR8 :$dst), (ins i8imm :$src),
                   "mov{b}\t{$src, $dst|$dst, $src}",
                   [(set GR8:$dst, imm:$src)]>;
def MOV16ri : Ii16<0xB8, AddRegFrm, (outs GR16:$dst), (ins i16imm:$src),
                   "mov{w}\t{$src, $dst|$dst, $src}",
                   [(set GR16:$dst, imm:$src)]>, OpSize16;
def MOV32ri : Ii32<0xB8, AddRegFrm, (outs GR32:$dst), (ins i32imm:$src),
                   "mov{l}\t{$src, $dst|$dst, $src}",
                   [(set GR32:$dst, imm:$src)]>, OpSize32;
def MOV64ri32 : RIi32S<0xC7, MRM0r, (outs GR64:$dst), (ins i64i32imm:$src),
                       "mov{q}\t{$src, $dst|$dst, $src}",
                       [(set GR64:$dst, i64immSExt32:$src)]>;
}
let isReMaterializable = 1, isMoveImm = 1 in {
def MOV64ri : RIi64<0xB8, AddRegFrm, (outs GR64:$dst), (ins i64imm:$src),
                    "movabs{q}\t{$src, $dst|$dst, $src}",
                    [(set GR64:$dst, imm:$src)]>;
}

// Longer forms that use a ModR/M byte. Needed for disassembler
let isCodeGenOnly = 1, ForceDisassemble = 1, hasSideEffects = 0 in {
def MOV8ri_alt  : Ii8 <0xC6, MRM0r, (outs GR8 :$dst), (ins i8imm :$src),
                   "mov{b}\t{$src, $dst|$dst, $src}", []>;
def MOV16ri_alt : Ii16<0xC7, MRM0r, (outs GR16:$dst), (ins i16imm:$src),
                   "mov{w}\t{$src, $dst|$dst, $src}", []>, OpSize16;
def MOV32ri_alt : Ii32<0xC7, MRM0r, (outs GR32:$dst), (ins i32imm:$src),
                   "mov{l}\t{$src, $dst|$dst, $src}", []>, OpSize32;
}
} // SchedRW

let SchedRW = [WriteStore] in {
def MOV8mi  : Ii8 <0xC6, MRM0m, (outs), (ins i8mem :$dst, i8imm :$src),
                   "mov{b}\t{$src, $dst|$dst, $src}",
                   [(store (i8 imm_su:$src), addr:$dst)]>;
def MOV16mi : Ii16<0xC7, MRM0m, (outs), (ins i16mem:$dst, i16imm:$src),
                   "mov{w}\t{$src, $dst|$dst, $src}",
                   [(store (i16 imm_su:$src), addr:$dst)]>, OpSize16;
def MOV32mi : Ii32<0xC7, MRM0m, (outs), (ins i32mem:$dst, i32imm:$src),
                   "mov{l}\t{$src, $dst|$dst, $src}",
                   [(store (i32 imm_su:$src), addr:$dst)]>, OpSize32;
def MOV64mi32 : RIi32S<0xC7, MRM0m, (outs), (ins i64mem:$dst, i64i32imm:$src),
                       "mov{q}\t{$src, $dst|$dst, $src}",
                       [(store i64immSExt32_su:$src, addr:$dst)]>,
                       Requires<[In64BitMode]>;
} // SchedRW

def : Pat<(i32 relocImm:$src), (MOV32ri relocImm:$src)>;
def : Pat<(i64 relocImm:$src), (MOV64ri relocImm:$src)>;

def : Pat<(store (i8 relocImm8_su:$src), addr:$dst),
          (MOV8mi addr:$dst, relocImm8_su:$src)>;
def : Pat<(store (i16 relocImm16_su:$src), addr:$dst),
          (MOV16mi addr:$dst, relocImm16_su:$src)>;
def : Pat<(store (i32 relocImm32_su:$src), addr:$dst),
          (MOV32mi addr:$dst, relocImm32_su:$src)>;
def : Pat<(store (i64 i64relocImmSExt32_su:$src), addr:$dst),
          (MOV64mi32 addr:$dst, i64immSExt32_su:$src)>;

let hasSideEffects = 0 in {

/// Memory offset versions of moves. The immediate is an address mode sized
/// offset from the segment base.
let SchedRW = [WriteALU] in {
let mayLoad = 1 in {
let Defs = [AL] in
def MOV8ao32 : Ii32<0xA0, RawFrmMemOffs, (outs), (ins offset32_8:$src),
                    "mov{b}\t{$src, %al|al, $src}", []>,
                    AdSize32;
let Defs = [AX] in
def MOV16ao32 : Ii32<0xA1, RawFrmMemOffs, (outs), (ins offset32_16:$src),
                     "mov{w}\t{$src, %ax|ax, $src}", []>,
                     OpSize16, AdSize32;
let Defs = [EAX] in
def MOV32ao32 : Ii32<0xA1, RawFrmMemOffs, (outs), (ins offset32_32:$src),
                     "mov{l}\t{$src, %eax|eax, $src}", []>,
                     OpSize32, AdSize32;
let Defs = [RAX] in
def MOV64ao32 : RIi32<0xA1, RawFrmMemOffs, (outs), (ins offset32_64:$src),
                      "mov{q}\t{$src, %rax|rax, $src}", []>,
                      AdSize32;

let Defs = [AL] in
def MOV8ao16 : Ii16<0xA0, RawFrmMemOffs, (outs), (ins offset16_8:$src),
                    "mov{b}\t{$src, %al|al, $src}", []>, AdSize16;
let Defs = [AX] in
def MOV16ao16 : Ii16<0xA1, RawFrmMemOffs, (outs), (ins offset16_16:$src),
                     "mov{w}\t{$src, %ax|ax, $src}", []>,
                     OpSize16, AdSize16;
let Defs = [EAX] in
def MOV32ao16 : Ii16<0xA1, RawFrmMemOffs, (outs), (ins offset16_32:$src),
                     "mov{l}\t{$src, %eax|eax, $src}", []>,
                     AdSize16, OpSize32;
} // mayLoad
let mayStore = 1 in {
let Uses = [AL] in
def MOV8o32a : Ii32<0xA2, RawFrmMemOffs, (outs), (ins offset32_8:$dst),
                    "mov{b}\t{%al, $dst|$dst, al}", []>, AdSize32;
let Uses = [AX] in
def MOV16o32a : Ii32<0xA3, RawFrmMemOffs, (outs), (ins offset32_16:$dst),
                     "mov{w}\t{%ax, $dst|$dst, ax}", []>,
                     OpSize16, AdSize32;
let Uses = [EAX] in
def MOV32o32a : Ii32<0xA3, RawFrmMemOffs, (outs), (ins offset32_32:$dst),
                     "mov{l}\t{%eax, $dst|$dst, eax}", []>,
                     OpSize32, AdSize32;
let Uses = [RAX] in
def MOV64o32a : RIi32<0xA3, RawFrmMemOffs, (outs), (ins offset32_64:$dst),
                      "mov{q}\t{%rax, $dst|$dst, rax}", []>,
                      AdSize32;

let Uses = [AL] in
def MOV8o16a : Ii16<0xA2, RawFrmMemOffs, (outs), (ins offset16_8:$dst),
                    "mov{b}\t{%al, $dst|$dst, al}", []>, AdSize16;
let Uses = [AX] in
def MOV16o16a : Ii16<0xA3, RawFrmMemOffs, (outs), (ins offset16_16:$dst),
                     "mov{w}\t{%ax, $dst|$dst, ax}", []>,
                     OpSize16, AdSize16;
let Uses = [EAX] in
def MOV32o16a : Ii16<0xA3, RawFrmMemOffs, (outs), (ins offset16_32:$dst),
                     "mov{l}\t{%eax, $dst|$dst, eax}", []>,
                     OpSize32, AdSize16;
} // mayStore

// These forms all have full 64-bit absolute addresses in their instructions
// and use the movabs mnemonic to indicate this specific form.
let mayLoad = 1 in {
let Defs = [AL] in
def MOV8ao64 : Ii64<0xA0, RawFrmMemOffs, (outs), (ins offset64_8:$src),
                    "movabs{b}\t{$src, %al|al, $src}", []>,
                    AdSize64;
let Defs = [AX] in
def MOV16ao64 : Ii64<0xA1, RawFrmMemOffs, (outs), (ins offset64_16:$src),
                     "movabs{w}\t{$src, %ax|ax, $src}", []>,
                     OpSize16, AdSize64;
let Defs = [EAX] in
def MOV32ao64 : Ii64<0xA1, RawFrmMemOffs, (outs), (ins offset64_32:$src),
                     "movabs{l}\t{$src, %eax|eax, $src}", []>,
                     OpSize32, AdSize64;
let Defs = [RAX] in
def MOV64ao64 : RIi64<0xA1, RawFrmMemOffs, (outs), (ins offset64_64:$src),
                     "movabs{q}\t{$src, %rax|rax, $src}", []>,
                     AdSize64;
} // mayLoad

let mayStore = 1 in {
let Uses = [AL] in
def MOV8o64a : Ii64<0xA2, RawFrmMemOffs, (outs), (ins offset64_8:$dst),
                    "movabs{b}\t{%al, $dst|$dst, al}", []>,
                    AdSize64;
let Uses = [AX] in
def MOV16o64a : Ii64<0xA3, RawFrmMemOffs, (outs), (ins offset64_16:$dst),
                     "movabs{w}\t{%ax, $dst|$dst, ax}", []>,
                     OpSize16, AdSize64;
let Uses = [EAX] in
def MOV32o64a : Ii64<0xA3, RawFrmMemOffs, (outs), (ins offset64_32:$dst),
                     "movabs{l}\t{%eax, $dst|$dst, eax}", []>,
                     OpSize32, AdSize64;
let Uses = [RAX] in
def MOV64o64a : RIi64<0xA3, RawFrmMemOffs, (outs), (ins offset64_64:$dst),
                     "movabs{q}\t{%rax, $dst|$dst, rax}", []>,
                     AdSize64;
} // mayStore
} // SchedRW
} // hasSideEffects = 0

let isCodeGenOnly = 1, ForceDisassemble = 1, hasSideEffects = 0,
    SchedRW = [WriteMove], isMoveReg = 1 in {
def MOV8rr_REV : I<0x8A, MRMSrcReg, (outs GR8:$dst), (ins GR8:$src),
                   "mov{b}\t{$src, $dst|$dst, $src}", []>;
def MOV16rr_REV : I<0x8B, MRMSrcReg, (outs GR16:$dst), (ins GR16:$src),
                    "mov{w}\t{$src, $dst|$dst, $src}", []>, OpSize16;
def MOV32rr_REV : I<0x8B, MRMSrcReg, (outs GR32:$dst), (ins GR32:$src),
                    "mov{l}\t{$src, $dst|$dst, $src}", []>, OpSize32;
def MOV64rr_REV : RI<0x8B, MRMSrcReg, (outs GR64:$dst), (ins GR64:$src),
                     "mov{q}\t{$src, $dst|$dst, $src}", []>;
}

let canFoldAsLoad = 1, isReMaterializable = 1, SchedRW = [WriteLoad] in {
def MOV8rm  : I<0x8A, MRMSrcMem, (outs GR8 :$dst), (ins i8mem :$src),
                "mov{b}\t{$src, $dst|$dst, $src}",
                [(set GR8:$dst, (loadi8 addr:$src))]>;
def MOV16rm : I<0x8B, MRMSrcMem, (outs GR16:$dst), (ins i16mem:$src),
                "mov{w}\t{$src, $dst|$dst, $src}",
                [(set GR16:$dst, (loadi16 addr:$src))]>, OpSize16;
def MOV32rm : I<0x8B, MRMSrcMem, (outs GR32:$dst), (ins i32mem:$src),
                "mov{l}\t{$src, $dst|$dst, $src}",
                [(set GR32:$dst, (loadi32 addr:$src))]>, OpSize32;
def MOV64rm : RI<0x8B, MRMSrcMem, (outs GR64:$dst), (ins i64mem:$src),
                 "mov{q}\t{$src, $dst|$dst, $src}",
                 [(set GR64:$dst, (load addr:$src))]>;
}

let SchedRW = [WriteStore] in {
def MOV8mr  : I<0x88, MRMDestMem, (outs), (ins i8mem :$dst, GR8 :$src),
                "mov{b}\t{$src, $dst|$dst, $src}",
                [(store GR8:$src, addr:$dst)]>;
def MOV16mr : I<0x89, MRMDestMem, (outs), (ins i16mem:$dst, GR16:$src),
                "mov{w}\t{$src, $dst|$dst, $src}",
                [(store GR16:$src, addr:$dst)]>, OpSize16;
def MOV32mr : I<0x89, MRMDestMem, (outs), (ins i32mem:$dst, GR32:$src),
                "mov{l}\t{$src, $dst|$dst, $src}",
                [(store GR32:$src, addr:$dst)]>, OpSize32;
def MOV64mr : RI<0x89, MRMDestMem, (outs), (ins i64mem:$dst, GR64:$src),
                 "mov{q}\t{$src, $dst|$dst, $src}",
                 [(store GR64:$src, addr:$dst)]>;
} // SchedRW

// Versions of MOV8rr, MOV8mr, and MOV8rm that use i8mem_NOREX and GR8_NOREX so
// that they can be used for copying and storing h registers, which can't be
// encoded when a REX prefix is present.
let isCodeGenOnly = 1 in {
let hasSideEffects = 0, isMoveReg = 1 in
def MOV8rr_NOREX : I<0x88, MRMDestReg,
                     (outs GR8_NOREX:$dst), (ins GR8_NOREX:$src),
                     "mov{b}\t{$src, $dst|$dst, $src}", []>,
                   Sched<[WriteMove]>;
let mayStore = 1, hasSideEffects = 0 in
def MOV8mr_NOREX : I<0x88, MRMDestMem,
                     (outs), (ins i8mem_NOREX:$dst, GR8_NOREX:$src),
                     "mov{b}\t{$src, $dst|$dst, $src}", []>,
                     Sched<[WriteStore]>;
let mayLoad = 1, hasSideEffects = 0,
    canFoldAsLoad = 1, isReMaterializable = 1 in
def MOV8rm_NOREX : I<0x8A, MRMSrcMem,
                     (outs GR8_NOREX:$dst), (ins i8mem_NOREX:$src),
                     "mov{b}\t{$src, $dst|$dst, $src}", []>,
                     Sched<[WriteLoad]>;
}


// Condition code ops, incl. set if equal/not equal/...
let SchedRW = [WriteLAHFSAHF] in {
let Defs = [EFLAGS], Uses = [AH], hasSideEffects = 0 in
def SAHF     : I<0x9E, RawFrm, (outs),  (ins), "sahf", []>,  // flags = AH
                 Requires<[HasLAHFSAHF]>;
let Defs = [AH], Uses = [EFLAGS], hasSideEffects = 0 in
def LAHF     : I<0x9F, RawFrm, (outs),  (ins), "lahf", []>,  // AH = flags
               Requires<[HasLAHFSAHF]>;
} // SchedRW

//===----------------------------------------------------------------------===//
// Bit tests instructions: BT, BTS, BTR, BTC.

let Defs = [EFLAGS] in {
let SchedRW = [WriteBitTest] in {
def BT16rr : I<0xA3, MRMDestReg, (outs), (ins GR16:$src1, GR16:$src2),
               "bt{w}\t{$src2, $src1|$src1, $src2}",
               [(set EFLAGS, (X86bt GR16:$src1, GR16:$src2))]>,
               OpSize16, TB;
def BT32rr : I<0xA3, MRMDestReg, (outs), (ins GR32:$src1, GR32:$src2),
               "bt{l}\t{$src2, $src1|$src1, $src2}",
               [(set EFLAGS, (X86bt GR32:$src1, GR32:$src2))]>,
               OpSize32, TB;
def BT64rr : RI<0xA3, MRMDestReg, (outs), (ins GR64:$src1, GR64:$src2),
               "bt{q}\t{$src2, $src1|$src1, $src2}",
               [(set EFLAGS, (X86bt GR64:$src1, GR64:$src2))]>, TB;
} // SchedRW

// Unlike with the register+register form, the memory+register form of the
// bt instruction does not ignore the high bits of the index. From ISel's
// perspective, this is pretty bizarre. Make these instructions disassembly
// only for now. These instructions are also slow on modern CPUs so that's
// another reason to avoid generating them.

let mayLoad = 1, hasSideEffects = 0, SchedRW = [WriteBitTestRegLd] in {
  def BT16mr : I<0xA3, MRMDestMem, (outs), (ins i16mem:$src1, GR16:$src2),
                 "bt{w}\t{$src2, $src1|$src1, $src2}",
                 []>, OpSize16, TB;
  def BT32mr : I<0xA3, MRMDestMem, (outs), (ins i32mem:$src1, GR32:$src2),
                 "bt{l}\t{$src2, $src1|$src1, $src2}",
                 []>, OpSize32, TB;
  def BT64mr : RI<0xA3, MRMDestMem, (outs), (ins i64mem:$src1, GR64:$src2),
                 "bt{q}\t{$src2, $src1|$src1, $src2}",
                  []>, TB;
}

let SchedRW = [WriteBitTest] in {
def BT16ri8 : Ii8<0xBA, MRM4r, (outs), (ins GR16:$src1, i16u8imm:$src2),
                "bt{w}\t{$src2, $src1|$src1, $src2}",
                [(set EFLAGS, (X86bt GR16:$src1, imm:$src2))]>,
                OpSize16, TB;
def BT32ri8 : Ii8<0xBA, MRM4r, (outs), (ins GR32:$src1, i32u8imm:$src2),
                "bt{l}\t{$src2, $src1|$src1, $src2}",
                [(set EFLAGS, (X86bt GR32:$src1, imm:$src2))]>,
                OpSize32, TB;
def BT64ri8 : RIi8<0xBA, MRM4r, (outs), (ins GR64:$src1, i64u8imm:$src2),
                "bt{q}\t{$src2, $src1|$src1, $src2}",
                [(set EFLAGS, (X86bt GR64:$src1, imm:$src2))]>, TB;
} // SchedRW

// Note that these instructions aren't slow because that only applies when the
// other operand is in a register. When it's an immediate, bt is still fast.
let SchedRW = [WriteBitTestImmLd] in {
def BT16mi8 : Ii8<0xBA, MRM4m, (outs), (ins i16mem:$src1, i16u8imm:$src2),
                  "bt{w}\t{$src2, $src1|$src1, $src2}",
                  [(set EFLAGS, (X86bt (loadi16 addr:$src1),
                                       imm:$src2))]>,
                  OpSize16, TB;
def BT32mi8 : Ii8<0xBA, MRM4m, (outs), (ins i32mem:$src1, i32u8imm:$src2),
                  "bt{l}\t{$src2, $src1|$src1, $src2}",
                  [(set EFLAGS, (X86bt (loadi32 addr:$src1),
                                       imm:$src2))]>,
                  OpSize32, TB;
def BT64mi8 : RIi8<0xBA, MRM4m, (outs), (ins i64mem:$src1, i64u8imm:$src2),
                "bt{q}\t{$src2, $src1|$src1, $src2}",
                [(set EFLAGS, (X86bt (loadi64 addr:$src1),
                                     imm:$src2))]>, TB,
                Requires<[In64BitMode]>;
} // SchedRW

let hasSideEffects = 0 in {
let SchedRW = [WriteBitTestSet], Constraints = "$src1 = $dst" in {
def BTC16rr : I<0xBB, MRMDestReg, (outs GR16:$dst), (ins GR16:$src1, GR16:$src2),
                "btc{w}\t{$src2, $src1|$src1, $src2}", []>,
                OpSize16, TB;
def BTC32rr : I<0xBB, MRMDestReg, (outs GR32:$dst), (ins GR32:$src1, GR32:$src2),
                "btc{l}\t{$src2, $src1|$src1, $src2}", []>,
                OpSize32, TB;
def BTC64rr : RI<0xBB, MRMDestReg, (outs GR64:$dst), (ins GR64:$src1, GR64:$src2),
                 "btc{q}\t{$src2, $src1|$src1, $src2}", []>, TB;
} // SchedRW

let mayLoad = 1, mayStore = 1, SchedRW = [WriteBitTestSetRegRMW] in {
def BTC16mr : I<0xBB, MRMDestMem, (outs), (ins i16mem:$src1, GR16:$src2),
                "btc{w}\t{$src2, $src1|$src1, $src2}", []>,
                OpSize16, TB;
def BTC32mr : I<0xBB, MRMDestMem, (outs), (ins i32mem:$src1, GR32:$src2),
                "btc{l}\t{$src2, $src1|$src1, $src2}", []>,
                OpSize32, TB;
def BTC64mr : RI<0xBB, MRMDestMem, (outs), (ins i64mem:$src1, GR64:$src2),
                 "btc{q}\t{$src2, $src1|$src1, $src2}", []>, TB;
}

let SchedRW = [WriteBitTestSet], Constraints = "$src1 = $dst" in {
def BTC16ri8 : Ii8<0xBA, MRM7r, (outs GR16:$dst), (ins GR16:$src1, i16u8imm:$src2),
                    "btc{w}\t{$src2, $src1|$src1, $src2}", []>, OpSize16, TB;
def BTC32ri8 : Ii8<0xBA, MRM7r, (outs GR32:$dst), (ins GR32:$src1, i32u8imm:$src2),
                    "btc{l}\t{$src2, $src1|$src1, $src2}", []>, OpSize32, TB;
def BTC64ri8 : RIi8<0xBA, MRM7r, (outs GR64:$dst), (ins GR64:$src1, i64u8imm:$src2),
                    "btc{q}\t{$src2, $src1|$src1, $src2}", []>, TB;
} // SchedRW

let mayLoad = 1, mayStore = 1, SchedRW = [WriteBitTestSetImmRMW] in {
def BTC16mi8 : Ii8<0xBA, MRM7m, (outs), (ins i16mem:$src1, i16u8imm:$src2),
                    "btc{w}\t{$src2, $src1|$src1, $src2}", []>, OpSize16, TB;
def BTC32mi8 : Ii8<0xBA, MRM7m, (outs), (ins i32mem:$src1, i32u8imm:$src2),
                    "btc{l}\t{$src2, $src1|$src1, $src2}", []>, OpSize32, TB;
def BTC64mi8 : RIi8<0xBA, MRM7m, (outs), (ins i64mem:$src1, i64u8imm:$src2),
                    "btc{q}\t{$src2, $src1|$src1, $src2}", []>, TB,
                    Requires<[In64BitMode]>;
}

let SchedRW = [WriteBitTestSet], Constraints = "$src1 = $dst" in {
def BTR16rr : I<0xB3, MRMDestReg, (outs GR16:$dst), (ins GR16:$src1, GR16:$src2),
                "btr{w}\t{$src2, $src1|$src1, $src2}", []>,
                OpSize16, TB;
def BTR32rr : I<0xB3, MRMDestReg, (outs GR32:$dst), (ins GR32:$src1, GR32:$src2),
                "btr{l}\t{$src2, $src1|$src1, $src2}", []>,
                OpSize32, TB;
def BTR64rr : RI<0xB3, MRMDestReg, (outs GR64:$dst), (ins GR64:$src1, GR64:$src2),
                 "btr{q}\t{$src2, $src1|$src1, $src2}", []>, TB;
} // SchedRW

let mayLoad = 1, mayStore = 1, SchedRW = [WriteBitTestSetRegRMW] in {
def BTR16mr : I<0xB3, MRMDestMem, (outs), (ins i16mem:$src1, GR16:$src2),
                "btr{w}\t{$src2, $src1|$src1, $src2}", []>,
                OpSize16, TB;
def BTR32mr : I<0xB3, MRMDestMem, (outs), (ins i32mem:$src1, GR32:$src2),
                "btr{l}\t{$src2, $src1|$src1, $src2}", []>,
                OpSize32, TB;
def BTR64mr : RI<0xB3, MRMDestMem, (outs), (ins i64mem:$src1, GR64:$src2),
                 "btr{q}\t{$src2, $src1|$src1, $src2}", []>, TB;
}

let SchedRW = [WriteBitTestSet], Constraints = "$src1 = $dst" in {
def BTR16ri8 : Ii8<0xBA, MRM6r, (outs GR16:$dst), (ins GR16:$src1, i16u8imm:$src2),
                    "btr{w}\t{$src2, $src1|$src1, $src2}", []>,
                    OpSize16, TB;
def BTR32ri8 : Ii8<0xBA, MRM6r, (outs GR32:$dst), (ins GR32:$src1, i32u8imm:$src2),
                    "btr{l}\t{$src2, $src1|$src1, $src2}", []>,
                    OpSize32, TB;
def BTR64ri8 : RIi8<0xBA, MRM6r, (outs GR64:$dst), (ins GR64:$src1, i64u8imm:$src2),
                    "btr{q}\t{$src2, $src1|$src1, $src2}", []>, TB;
} // SchedRW

let mayLoad = 1, mayStore = 1, SchedRW = [WriteBitTestSetImmRMW] in {
def BTR16mi8 : Ii8<0xBA, MRM6m, (outs), (ins i16mem:$src1, i16u8imm:$src2),
                    "btr{w}\t{$src2, $src1|$src1, $src2}", []>,
                    OpSize16, TB;
def BTR32mi8 : Ii8<0xBA, MRM6m, (outs), (ins i32mem:$src1, i32u8imm:$src2),
                    "btr{l}\t{$src2, $src1|$src1, $src2}", []>,
                    OpSize32, TB;
def BTR64mi8 : RIi8<0xBA, MRM6m, (outs), (ins i64mem:$src1, i64u8imm:$src2),
                    "btr{q}\t{$src2, $src1|$src1, $src2}", []>, TB,
                    Requires<[In64BitMode]>;
}

let SchedRW = [WriteBitTestSet], Constraints = "$src1 = $dst" in {
def BTS16rr : I<0xAB, MRMDestReg, (outs GR16:$dst), (ins GR16:$src1, GR16:$src2),
                "bts{w}\t{$src2, $src1|$src1, $src2}", []>,
                OpSize16, TB;
def BTS32rr : I<0xAB, MRMDestReg, (outs GR32:$dst), (ins GR32:$src1, GR32:$src2),
                "bts{l}\t{$src2, $src1|$src1, $src2}", []>,
              OpSize32, TB;
def BTS64rr : RI<0xAB, MRMDestReg, (outs GR64:$dst), (ins GR64:$src1, GR64:$src2),
               "bts{q}\t{$src2, $src1|$src1, $src2}", []>, TB;
} // SchedRW

let mayLoad = 1, mayStore = 1, SchedRW = [WriteBitTestSetRegRMW] in {
def BTS16mr : I<0xAB, MRMDestMem, (outs), (ins i16mem:$src1, GR16:$src2),
              "bts{w}\t{$src2, $src1|$src1, $src2}", []>,
              OpSize16, TB;
def BTS32mr : I<0xAB, MRMDestMem, (outs), (ins i32mem:$src1, GR32:$src2),
              "bts{l}\t{$src2, $src1|$src1, $src2}", []>,
              OpSize32, TB;
def BTS64mr : RI<0xAB, MRMDestMem, (outs), (ins i64mem:$src1, GR64:$src2),
                 "bts{q}\t{$src2, $src1|$src1, $src2}", []>, TB;
}

let SchedRW = [WriteBitTestSet], Constraints = "$src1 = $dst" in {
def BTS16ri8 : Ii8<0xBA, MRM5r, (outs GR16:$dst), (ins GR16:$src1, i16u8imm:$src2),
                    "bts{w}\t{$src2, $src1|$src1, $src2}", []>, OpSize16, TB;
def BTS32ri8 : Ii8<0xBA, MRM5r, (outs GR32:$dst), (ins GR32:$src1, i32u8imm:$src2),
                    "bts{l}\t{$src2, $src1|$src1, $src2}", []>, OpSize32, TB;
def BTS64ri8 : RIi8<0xBA, MRM5r, (outs GR64:$dst), (ins GR64:$src1, i64u8imm:$src2),
                    "bts{q}\t{$src2, $src1|$src1, $src2}", []>, TB;
} // SchedRW

let mayLoad = 1, mayStore = 1, SchedRW = [WriteBitTestSetImmRMW] in {
def BTS16mi8 : Ii8<0xBA, MRM5m, (outs), (ins i16mem:$src1, i16u8imm:$src2),
                    "bts{w}\t{$src2, $src1|$src1, $src2}", []>, OpSize16, TB;
def BTS32mi8 : Ii8<0xBA, MRM5m, (outs), (ins i32mem:$src1, i32u8imm:$src2),
                    "bts{l}\t{$src2, $src1|$src1, $src2}", []>, OpSize32, TB;
def BTS64mi8 : RIi8<0xBA, MRM5m, (outs), (ins i64mem:$src1, i64u8imm:$src2),
                    "bts{q}\t{$src2, $src1|$src1, $src2}", []>, TB,
                    Requires<[In64BitMode]>;
}
} // hasSideEffects = 0
} // Defs = [EFLAGS]


//===----------------------------------------------------------------------===//
// Atomic support
//

// Atomic swap. These are just normal xchg instructions. But since a memory
// operand is referenced, the atomicity is ensured.
multiclass ATOMIC_SWAP<bits<8> opc8, bits<8> opc, string mnemonic, string frag> {
  let Constraints = "$val = $dst", SchedRW = [WriteALULd, WriteRMW] in {
    def NAME#8rm  : I<opc8, MRMSrcMem, (outs GR8:$dst),
                      (ins GR8:$val, i8mem:$ptr),
                      !strconcat(mnemonic, "{b}\t{$val, $ptr|$ptr, $val}"),
                      [(set
                         GR8:$dst,
                         (!cast<PatFrag>(frag # "_i8") addr:$ptr, GR8:$val))]>;
    def NAME#16rm : I<opc, MRMSrcMem, (outs GR16:$dst),
                      (ins GR16:$val, i16mem:$ptr),
                      !strconcat(mnemonic, "{w}\t{$val, $ptr|$ptr, $val}"),
                      [(set
                         GR16:$dst,
                         (!cast<PatFrag>(frag # "_i16") addr:$ptr, GR16:$val))]>,
                      OpSize16;
    def NAME#32rm : I<opc, MRMSrcMem, (outs GR32:$dst),
                      (ins GR32:$val, i32mem:$ptr),
                      !strconcat(mnemonic, "{l}\t{$val, $ptr|$ptr, $val}"),
                      [(set
                         GR32:$dst,
                         (!cast<PatFrag>(frag # "_i32") addr:$ptr, GR32:$val))]>,
                      OpSize32;
    def NAME#64rm : RI<opc, MRMSrcMem, (outs GR64:$dst),
                       (ins GR64:$val, i64mem:$ptr),
                       !strconcat(mnemonic, "{q}\t{$val, $ptr|$ptr, $val}"),
                       [(set
                         GR64:$dst,
                         (!cast<PatFrag>(frag # "_i64") addr:$ptr, GR64:$val))]>;
  }
}

defm XCHG    : ATOMIC_SWAP<0x86, 0x87, "xchg", "atomic_swap">;

// Swap between registers.
let SchedRW = [WriteXCHG] in {
let Constraints = "$src1 = $dst1, $src2 = $dst2", hasSideEffects = 0 in {
def XCHG8rr : I<0x86, MRMSrcReg, (outs GR8:$dst1, GR8:$dst2),
                (ins GR8:$src1, GR8:$src2),
                "xchg{b}\t{$src2, $src1|$src1, $src2}", []>;
def XCHG16rr : I<0x87, MRMSrcReg, (outs GR16:$dst1, GR16:$dst2),
                 (ins GR16:$src1, GR16:$src2),
                 "xchg{w}\t{$src2, $src1|$src1, $src2}", []>,
                 OpSize16;
def XCHG32rr : I<0x87, MRMSrcReg, (outs GR32:$dst1, GR32:$dst2),
                 (ins GR32:$src1, GR32:$src2),
                 "xchg{l}\t{$src2, $src1|$src1, $src2}", []>,
                 OpSize32;
def XCHG64rr : RI<0x87, MRMSrcReg, (outs GR64:$dst1, GR64:$dst2),
                  (ins GR64:$src1 ,GR64:$src2),
                  "xchg{q}\t{$src2, $src1|$src1, $src2}", []>;
}

// Swap between EAX and other registers.
let Constraints = "$src = $dst", hasSideEffects = 0 in {
let Uses = [AX], Defs = [AX] in
def XCHG16ar : I<0x90, AddRegFrm, (outs GR16:$dst), (ins GR16:$src),
                  "xchg{w}\t{$src, %ax|ax, $src}", []>, OpSize16;
let Uses = [EAX], Defs = [EAX] in
def XCHG32ar : I<0x90, AddRegFrm, (outs GR32:$dst), (ins GR32:$src),
                  "xchg{l}\t{$src, %eax|eax, $src}", []>, OpSize32;
let Uses = [RAX], Defs = [RAX] in
def XCHG64ar : RI<0x90, AddRegFrm, (outs GR64:$dst), (ins GR64:$src),
                  "xchg{q}\t{$src, %rax|rax, $src}", []>;
}
} // SchedRW

let hasSideEffects = 0, Constraints = "$src1 = $dst1, $src2 = $dst2",
    Defs = [EFLAGS], SchedRW = [WriteXCHG] in {
def XADD8rr : I<0xC0, MRMDestReg, (outs GR8:$dst1, GR8:$dst2),
                (ins GR8:$src1, GR8:$src2),
                "xadd{b}\t{$src2, $src1|$src1, $src2}", []>, TB;
def XADD16rr : I<0xC1, MRMDestReg, (outs GR16:$dst1, GR16:$dst2),
                 (ins GR16:$src1, GR16:$src2),
                 "xadd{w}\t{$src2, $src1|$src1, $src2}", []>, TB, OpSize16;
def XADD32rr : I<0xC1, MRMDestReg, (outs GR32:$dst1, GR32:$dst2),
                  (ins GR32:$src1, GR32:$src2),
                 "xadd{l}\t{$src2, $src1|$src1, $src2}", []>, TB, OpSize32;
def XADD64rr : RI<0xC1, MRMDestReg, (outs GR64:$dst1, GR64:$dst2),
                  (ins GR64:$src1, GR64:$src2),
                  "xadd{q}\t{$src2, $src1|$src1, $src2}", []>, TB;
} // SchedRW

let mayLoad = 1, mayStore = 1, hasSideEffects = 0, Constraints = "$val = $dst",
    Defs = [EFLAGS], SchedRW = [WriteALULd, WriteRMW] in {
def XADD8rm   : I<0xC0, MRMSrcMem, (outs GR8:$dst),
                  (ins GR8:$val, i8mem:$ptr),
                 "xadd{b}\t{$val, $ptr|$ptr, $val}", []>, TB;
def XADD16rm  : I<0xC1, MRMSrcMem, (outs GR16:$dst),
                  (ins GR16:$val, i16mem:$ptr),
                 "xadd{w}\t{$val, $ptr|$ptr, $val}", []>, TB,
                 OpSize16;
def XADD32rm  : I<0xC1, MRMSrcMem, (outs GR32:$dst),
                  (ins GR32:$val, i32mem:$ptr),
                 "xadd{l}\t{$val, $ptr|$ptr, $val}", []>, TB,
                 OpSize32;
def XADD64rm  : RI<0xC1, MRMSrcMem, (outs GR64:$dst),
                   (ins GR64:$val, i64mem:$ptr),
                   "xadd{q}\t{$val, $ptr|$ptr, $val}", []>, TB;

}

let SchedRW = [WriteCMPXCHG], hasSideEffects = 0 in {
let Defs = [AL, EFLAGS], Uses = [AL] in
def CMPXCHG8rr : I<0xB0, MRMDestReg, (outs GR8:$dst), (ins GR8:$src),
                   "cmpxchg{b}\t{$src, $dst|$dst, $src}", []>, TB;
let Defs = [AX, EFLAGS], Uses = [AX] in
def CMPXCHG16rr : I<0xB1, MRMDestReg, (outs GR16:$dst), (ins GR16:$src),
                    "cmpxchg{w}\t{$src, $dst|$dst, $src}", []>, TB, OpSize16;
let Defs = [EAX, EFLAGS], Uses = [EAX] in
def CMPXCHG32rr  : I<0xB1, MRMDestReg, (outs GR32:$dst), (ins GR32:$src),
                     "cmpxchg{l}\t{$src, $dst|$dst, $src}", []>, TB, OpSize32;
let Defs = [RAX, EFLAGS], Uses = [RAX] in
def CMPXCHG64rr  : RI<0xB1, MRMDestReg, (outs GR64:$dst), (ins GR64:$src),
                      "cmpxchg{q}\t{$src, $dst|$dst, $src}", []>, TB;
} // SchedRW, hasSideEffects

let SchedRW = [WriteCMPXCHGRMW], mayLoad = 1, mayStore = 1,
    hasSideEffects = 0 in {
let Defs = [AL, EFLAGS], Uses = [AL] in
def CMPXCHG8rm   : I<0xB0, MRMDestMem, (outs), (ins i8mem:$dst, GR8:$src),
                     "cmpxchg{b}\t{$src, $dst|$dst, $src}", []>, TB;
let Defs = [AX, EFLAGS], Uses = [AX] in
def CMPXCHG16rm  : I<0xB1, MRMDestMem, (outs), (ins i16mem:$dst, GR16:$src),
                     "cmpxchg{w}\t{$src, $dst|$dst, $src}", []>, TB, OpSize16;
let Defs = [EAX, EFLAGS], Uses = [EAX] in
def CMPXCHG32rm  : I<0xB1, MRMDestMem, (outs), (ins i32mem:$dst, GR32:$src),
                     "cmpxchg{l}\t{$src, $dst|$dst, $src}", []>, TB, OpSize32;
let Defs = [RAX, EFLAGS], Uses = [RAX] in
def CMPXCHG64rm  : RI<0xB1, MRMDestMem, (outs), (ins i64mem:$dst, GR64:$src),
                      "cmpxchg{q}\t{$src, $dst|$dst, $src}", []>, TB;

let Defs = [EAX, EDX, EFLAGS], Uses = [EAX, EBX, ECX, EDX] in
def CMPXCHG8B : I<0xC7, MRM1m, (outs), (ins i64mem:$dst),
                  "cmpxchg8b\t$dst", []>, TB, Requires<[HasCX8]>;

let Defs = [RAX, RDX, EFLAGS], Uses = [RAX, RBX, RCX, RDX] in
// NOTE: In64BitMode check needed for the AssemblerPredicate.
def CMPXCHG16B : RI<0xC7, MRM1m, (outs), (ins i128mem:$dst),
                    "cmpxchg16b\t$dst", []>,
                    TB, Requires<[HasCX16,In64BitMode]>;
} // SchedRW, mayLoad, mayStore, hasSideEffects


// Lock instruction prefix
let SchedRW = [WriteMicrocoded] in
def LOCK_PREFIX : I<0xF0, PrefixByte, (outs),  (ins), "lock", []>;

let SchedRW = [WriteNop] in {

// Rex64 instruction prefix
def REX64_PREFIX : I<0x48, PrefixByte, (outs),  (ins), "rex64", []>,
                     Requires<[In64BitMode]>;

// Data16 instruction prefix
def DATA16_PREFIX : I<0x66, PrefixByte, (outs),  (ins), "data16", []>;
} // SchedRW

// Repeat string operation instruction prefixes
let Defs = [ECX], Uses = [ECX,DF], SchedRW = [WriteMicrocoded] in {
// Repeat (used with INS, OUTS, MOVS, LODS and STOS)
def REP_PREFIX : I<0xF3, PrefixByte, (outs),  (ins), "rep", []>;
// Repeat while not equal (used with CMPS and SCAS)
def REPNE_PREFIX : I<0xF2, PrefixByte, (outs),  (ins), "repne", []>;
}

// String manipulation instructions
let SchedRW = [WriteMicrocoded] in {
let Defs = [AL,ESI], Uses = [ESI,DF] in
def LODSB : I<0xAC, RawFrmSrc, (outs), (ins srcidx8:$src),
              "lodsb\t{$src, %al|al, $src}", []>;
let Defs = [AX,ESI], Uses = [ESI,DF] in
def LODSW : I<0xAD, RawFrmSrc, (outs), (ins srcidx16:$src),
              "lodsw\t{$src, %ax|ax, $src}", []>, OpSize16;
let Defs = [EAX,ESI], Uses = [ESI,DF] in
def LODSL : I<0xAD, RawFrmSrc, (outs), (ins srcidx32:$src),
              "lods{l|d}\t{$src, %eax|eax, $src}", []>, OpSize32;
let Defs = [RAX,ESI], Uses = [ESI,DF] in
def LODSQ : RI<0xAD, RawFrmSrc, (outs), (ins srcidx64:$src),
               "lodsq\t{$src, %rax|rax, $src}", []>,
               Requires<[In64BitMode]>;
}

let SchedRW = [WriteSystem] in {
let Defs = [ESI], Uses = [DX,ESI,DF] in {
def OUTSB : I<0x6E, RawFrmSrc, (outs), (ins srcidx8:$src),
             "outsb\t{$src, %dx|dx, $src}", []>;
def OUTSW : I<0x6F, RawFrmSrc, (outs), (ins srcidx16:$src),
              "outsw\t{$src, %dx|dx, $src}", []>, OpSize16;
def OUTSL : I<0x6F, RawFrmSrc, (outs), (ins srcidx32:$src),
              "outs{l|d}\t{$src, %dx|dx, $src}", []>, OpSize32;
}

let Defs = [EDI], Uses = [DX,EDI,DF] in {
def INSB : I<0x6C, RawFrmDst, (outs), (ins dstidx8:$dst),
             "insb\t{%dx, $dst|$dst, dx}", []>;
def INSW : I<0x6D, RawFrmDst, (outs), (ins dstidx16:$dst),
             "insw\t{%dx, $dst|$dst, dx}", []>,  OpSize16;
def INSL : I<0x6D, RawFrmDst, (outs), (ins dstidx32:$dst),
             "ins{l|d}\t{%dx, $dst|$dst, dx}", []>, OpSize32;
}
}

// EFLAGS management instructions.
let SchedRW = [WriteALU], Defs = [EFLAGS], Uses = [EFLAGS] in {
def CLC : I<0xF8, RawFrm, (outs), (ins), "clc", []>;
def STC : I<0xF9, RawFrm, (outs), (ins), "stc", []>;
def CMC : I<0xF5, RawFrm, (outs), (ins), "cmc", []>;
}

// DF management instructions.
let SchedRW = [WriteALU], Defs = [DF] in {
def CLD : I<0xFC, RawFrm, (outs), (ins), "cld", []>;
def STD : I<0xFD, RawFrm, (outs), (ins), "std", []>;
}

// Table lookup instructions
let Uses = [AL,EBX], Defs = [AL], hasSideEffects = 0, mayLoad = 1 in
def XLAT : I<0xD7, RawFrm, (outs), (ins), "xlatb", []>, Sched<[WriteLoad]>;

let SchedRW = [WriteMicrocoded] in {
// ASCII Adjust After Addition
let Uses = [AL,EFLAGS], Defs = [AX,EFLAGS], hasSideEffects = 0 in
def AAA : I<0x37, RawFrm, (outs), (ins), "aaa", []>,
            Requires<[Not64BitMode]>;

// ASCII Adjust AX Before Division
let Uses = [AX], Defs = [AX,EFLAGS], hasSideEffects = 0 in
def AAD8i8 : Ii8<0xD5, RawFrm, (outs), (ins i8imm:$src),
                 "aad\t$src", []>, Requires<[Not64BitMode]>;

// ASCII Adjust AX After Multiply
let Uses = [AL], Defs = [AX,EFLAGS], hasSideEffects = 0 in
def AAM8i8 : Ii8<0xD4, RawFrm, (outs), (ins i8imm:$src),
                 "aam\t$src", []>, Requires<[Not64BitMode]>;

// ASCII Adjust AL After Subtraction - sets
let Uses = [AL,EFLAGS], Defs = [AX,EFLAGS], hasSideEffects = 0 in
def AAS : I<0x3F, RawFrm, (outs), (ins), "aas", []>,
            Requires<[Not64BitMode]>;

// Decimal Adjust AL after Addition
let Uses = [AL,EFLAGS], Defs = [AL,EFLAGS], hasSideEffects = 0 in
def DAA : I<0x27, RawFrm, (outs), (ins), "daa", []>,
            Requires<[Not64BitMode]>;

// Decimal Adjust AL after Subtraction
let Uses = [AL,EFLAGS], Defs = [AL,EFLAGS], hasSideEffects = 0 in
def DAS : I<0x2F, RawFrm, (outs), (ins), "das", []>,
            Requires<[Not64BitMode]>;
} // SchedRW

let SchedRW = [WriteSystem] in {
// Check Array Index Against Bounds
// Note: "bound" does not have reversed operands in at&t syntax.
def BOUNDS16rm : I<0x62, MRMSrcMem, (outs GR16:$dst), (ins i16mem:$src),
                   "bound\t$dst, $src", []>, OpSize16,
                   Requires<[Not64BitMode]>;
def BOUNDS32rm : I<0x62, MRMSrcMem, (outs GR32:$dst), (ins i32mem:$src),
                   "bound\t$dst, $src", []>, OpSize32,
                   Requires<[Not64BitMode]>;

// Adjust RPL Field of Segment Selector
def ARPL16rr : I<0x63, MRMDestReg, (outs GR16:$dst), (ins GR16:$src),
                 "arpl\t{$src, $dst|$dst, $src}", []>,
                 Requires<[Not64BitMode]>;
let mayStore = 1 in
def ARPL16mr : I<0x63, MRMDestMem, (outs), (ins i16mem:$dst, GR16:$src),
                 "arpl\t{$src, $dst|$dst, $src}", []>,
                 Requires<[Not64BitMode]>;
} // SchedRW

//===----------------------------------------------------------------------===//
// MOVBE Instructions
//
multiclass Movbe<bits<8> o, X86TypeInfo t, string suffix = ""> {
  def rm#suffix : ITy<o, MRMSrcMem, t, (outs t.RegClass:$dst),
                      (ins t.MemOperand:$src1), "movbe", unaryop_ndd_args,
                      [(set t.RegClass:$dst, (bswap (t.LoadNode addr:$src1)))]>,
                  Sched<[WriteALULd]>;
  def mr#suffix : ITy<!add(o, 1), MRMDestMem, t, (outs),
                      (ins t.MemOperand:$dst, t.RegClass:$src1),
                      "movbe", unaryop_ndd_args,
                      [(store (bswap t.RegClass:$src1), addr:$dst)]>,
                  Sched<[WriteStore]>;
}

let Predicates = [HasMOVBE, NoEGPR] in {
  defm MOVBE16 : Movbe<0xF0, Xi16>, OpSize16, T8;
  defm MOVBE32 : Movbe<0xF0, Xi32>, OpSize32, T8;
  defm MOVBE64 : Movbe<0xF0, Xi64>, T8;
}

let Predicates = [HasMOVBE, HasEGPR, In64BitMode] in {
  defm MOVBE16 : Movbe<0x60, Xi16, "_EVEX">, EVEX, T_MAP4, PD;
  defm MOVBE32 : Movbe<0x60, Xi32, "_EVEX">, EVEX, T_MAP4;
  defm MOVBE64 : Movbe<0x60, Xi64, "_EVEX">, EVEX, T_MAP4;
}

multiclass Movberr<X86TypeInfo t> {
  def rr : ITy<0x61, MRMDestReg, t, (outs t.RegClass:$dst),
               (ins t.RegClass:$src1), "movbe", unaryop_ndd_args,
               [(set t.RegClass:$dst, (bswap t.RegClass:$src1))]>,
           EVEX, T_MAP4;
  def rr_REV : ITy<0x60, MRMSrcReg, t, (outs t.RegClass:$dst),
                   (ins t.RegClass:$src1), "movbe", unaryop_ndd_args, []>,
               EVEX, T_MAP4, DisassembleOnly;
}
let SchedRW = [WriteALU], Predicates = [HasMOVBE, HasNDD, In64BitMode] in {
  defm MOVBE16 : Movberr<Xi16>, PD;
  defm MOVBE32 : Movberr<Xi32>;
  defm MOVBE64 : Movberr<Xi64>;
}

//===----------------------------------------------------------------------===//
// RDRAND Instruction
//
let Predicates = [HasRDRAND], Defs = [EFLAGS], SchedRW = [WriteSystem] in {
  def RDRAND16r : I<0xC7, MRM6r, (outs GR16:$dst), (ins),
                    "rdrand{w}\t$dst", [(set GR16:$dst, EFLAGS, (X86rdrand))]>,
                    OpSize16, TB;
  def RDRAND32r : I<0xC7, MRM6r, (outs GR32:$dst), (ins),
                    "rdrand{l}\t$dst", [(set GR32:$dst, EFLAGS, (X86rdrand))]>,
                    OpSize32, TB;
  def RDRAND64r : RI<0xC7, MRM6r, (outs GR64:$dst), (ins),
                     "rdrand{q}\t$dst", [(set GR64:$dst, EFLAGS, (X86rdrand))]>,
                     TB;
}

//===----------------------------------------------------------------------===//
// RDSEED Instruction
//
let Predicates = [HasRDSEED], Defs = [EFLAGS], SchedRW = [WriteSystem] in {
  def RDSEED16r : I<0xC7, MRM7r, (outs GR16:$dst), (ins), "rdseed{w}\t$dst",
                    [(set GR16:$dst, EFLAGS, (X86rdseed))]>, OpSize16, TB;
  def RDSEED32r : I<0xC7, MRM7r, (outs GR32:$dst), (ins), "rdseed{l}\t$dst",
                    [(set GR32:$dst, EFLAGS, (X86rdseed))]>, OpSize32, TB;
  def RDSEED64r : RI<0xC7, MRM7r, (outs GR64:$dst), (ins), "rdseed{q}\t$dst",
                     [(set GR64:$dst, EFLAGS, (X86rdseed))]>, TB;
}

//===----------------------------------------------------------------------===//
// LZCNT Instruction
//
multiclass Lzcnt<bits<8> o, string m, SDPatternOperator node, X86TypeInfo t,
                 SchedWrite schedrr, SchedWrite schedrm, string suffix = ""> {
  def rr#suffix : ITy<o, MRMSrcReg, t, (outs t.RegClass:$dst),
                      (ins t.RegClass:$src1), m, unaryop_ndd_args,
                      [(set t.RegClass:$dst, (node t.RegClass:$src1)),
                                              (implicit EFLAGS)]>,
                  TB, Sched<[schedrr]>;
  let mayLoad = 1 in
    def rm#suffix : ITy<o, MRMSrcMem, t, (outs t.RegClass:$dst),
                        (ins t.MemOperand:$src1), m, unaryop_ndd_args,
                        [(set t.RegClass:$dst, (node (t.LoadNode addr:$src1))),
                                                (implicit EFLAGS)]>,
                    TB, Sched<[schedrm]>;
}

let Predicates = [HasLZCNT], Defs = [EFLAGS] in {
  defm LZCNT16 : Lzcnt<0xBD, "lzcnt", ctlz, Xi16, WriteLZCNT, WriteLZCNTLd>, OpSize16, XS;
  defm LZCNT32 : Lzcnt<0xBD, "lzcnt", ctlz, Xi32, WriteLZCNT, WriteLZCNTLd>, OpSize32, XS;
  defm LZCNT64 : Lzcnt<0xBD, "lzcnt", ctlz, Xi64, WriteLZCNT, WriteLZCNTLd>, XS;

  defm LZCNT16 : Lzcnt<0xF5, "lzcnt", null_frag, Xi16, WriteLZCNT, WriteLZCNTLd, "_EVEX">, PL, PD;
  defm LZCNT32 : Lzcnt<0xF5, "lzcnt", null_frag, Xi32, WriteLZCNT, WriteLZCNTLd, "_EVEX">, PL;
  defm LZCNT64 : Lzcnt<0xF5, "lzcnt", null_frag, Xi64, WriteLZCNT, WriteLZCNTLd, "_EVEX">, PL;
}

defm LZCNT16 : Lzcnt<0xF5, "lzcnt", null_frag, Xi16, WriteLZCNT, WriteLZCNTLd, "_NF">, NF, PD;
defm LZCNT32 : Lzcnt<0xF5, "lzcnt", null_frag, Xi32, WriteLZCNT, WriteLZCNTLd, "_NF">, NF;
defm LZCNT64 : Lzcnt<0xF5, "lzcnt", null_frag, Xi64, WriteLZCNT, WriteLZCNTLd, "_NF">, NF;

//===----------------------------------------------------------------------===//
// BMI Instructions
//
let Predicates = [HasBMI], Defs = [EFLAGS] in {
  defm TZCNT16 : Lzcnt<0xBC, "tzcnt", cttz, Xi16, WriteTZCNT, WriteTZCNTLd>, OpSize16, XS;
  defm TZCNT32 : Lzcnt<0xBC, "tzcnt", cttz, Xi32, WriteTZCNT, WriteTZCNTLd>, OpSize32, XS;
  defm TZCNT64 : Lzcnt<0xBC, "tzcnt", cttz, Xi64, WriteTZCNT, WriteTZCNTLd>, XS;

  defm TZCNT16 : Lzcnt<0xF4, "tzcnt", null_frag, Xi16, WriteTZCNT, WriteTZCNTLd, "_EVEX">, PL, PD;
  defm TZCNT32 : Lzcnt<0xF4, "tzcnt", null_frag, Xi32, WriteTZCNT, WriteTZCNTLd, "_EVEX">, PL;
  defm TZCNT64 : Lzcnt<0xF4, "tzcnt", null_frag, Xi64, WriteTZCNT, WriteTZCNTLd, "_EVEX">, PL;
}

defm TZCNT16 : Lzcnt<0xF4, "tzcnt", null_frag, Xi16, WriteTZCNT, WriteTZCNTLd, "_NF">, NF, PD;
defm TZCNT32 : Lzcnt<0xF4, "tzcnt", null_frag, Xi32, WriteTZCNT, WriteTZCNTLd, "_NF">, NF;
defm TZCNT64 : Lzcnt<0xF4, "tzcnt", null_frag, Xi64, WriteTZCNT, WriteTZCNTLd, "_NF">, NF;

multiclass Bls<string m, Format RegMRM, Format MemMRM, X86TypeInfo t, string Suffix = ""> {
  let SchedRW = [WriteBLS] in {
    def rr#Suffix : UnaryOpR<0xF3, RegMRM, m, unaryop_ndd_args, t,
                             (outs t.RegClass:$dst), []>, T8, VVVV;
  }

  let SchedRW = [WriteBLS.Folded] in
    def rm#Suffix : UnaryOpM<0xF3, MemMRM, m, unaryop_ndd_args, t,
                             (outs t.RegClass:$dst), []>, T8, VVVV;
}

let Defs = [EFLAGS] in {
  defm BLSR32 : Bls<"blsr", MRM1r, MRM1m, Xi32>, VEX;
  defm BLSR64 : Bls<"blsr", MRM1r, MRM1m, Xi64>, VEX;
  defm BLSMSK32 : Bls<"blsmsk", MRM2r, MRM2m, Xi32>, VEX;
  defm BLSMSK64 : Bls<"blsmsk", MRM2r, MRM2m, Xi64>, VEX;
  defm BLSI32 : Bls<"blsi", MRM3r, MRM3m, Xi32>, VEX;
  defm BLSI64 : Bls<"blsi", MRM3r, MRM3m, Xi64>, VEX;
}

let Predicates = [In64BitMode], Defs = [EFLAGS] in {
  defm BLSR32 : Bls<"blsr", MRM1r, MRM1m, Xi32, "_EVEX">, EVEX;
  defm BLSR64 : Bls<"blsr", MRM1r, MRM1m, Xi64, "_EVEX">, EVEX;
  defm BLSMSK32 : Bls<"blsmsk", MRM2r, MRM2m, Xi32, "_EVEX">, EVEX;
  defm BLSMSK64 : Bls<"blsmsk", MRM2r, MRM2m, Xi64, "_EVEX">, EVEX;
  defm BLSI32 : Bls<"blsi", MRM3r, MRM3m, Xi32, "_EVEX">, EVEX;
  defm BLSI64 : Bls<"blsi", MRM3r, MRM3m, Xi64, "_EVEX">, EVEX;
}

let Predicates = [In64BitMode] in {
  defm BLSR32 : Bls<"blsr", MRM1r, MRM1m, Xi32, "_NF">, EVEX, EVEX_NF;
  defm BLSR64 : Bls<"blsr", MRM1r, MRM1m, Xi64, "_NF">, EVEX, EVEX_NF;
  defm BLSMSK32 : Bls<"blsmsk", MRM2r, MRM2m, Xi32, "_NF">, EVEX, EVEX_NF;
  defm BLSMSK64 : Bls<"blsmsk", MRM2r, MRM2m, Xi64, "_NF">, EVEX, EVEX_NF;
  defm BLSI32 : Bls<"blsi", MRM3r, MRM3m, Xi32, "_NF">, EVEX, EVEX_NF;
  defm BLSI64 : Bls<"blsi", MRM3r, MRM3m, Xi64, "_NF">, EVEX, EVEX_NF;
}

multiclass Bls_Pats<string suffix> {
  // FIXME(1): patterns for the load versions are not implemented
  // FIXME(2): By only matching `add_su` and `ineg_su` we may emit
  // extra `mov` instructions if `src` has future uses. It may be better
  // to always match if `src` has more users.
  def : Pat<(and GR32:$src, (add_su GR32:$src, -1)),
            (!cast<Instruction>(BLSR32rr#suffix) GR32:$src)>;
  def : Pat<(and GR64:$src, (add_su GR64:$src, -1)),
            (!cast<Instruction>(BLSR64rr#suffix) GR64:$src)>;

  def : Pat<(xor GR32:$src, (add_su GR32:$src, -1)),
            (!cast<Instruction>(BLSMSK32rr#suffix) GR32:$src)>;
  def : Pat<(xor GR64:$src, (add_su GR64:$src, -1)),
            (!cast<Instruction>(BLSMSK64rr#suffix) GR64:$src)>;

  def : Pat<(and GR32:$src, (ineg_su GR32:$src)),
            (!cast<Instruction>(BLSI32rr#suffix) GR32:$src)>;
  def : Pat<(and GR64:$src, (ineg_su GR64:$src)),
            (!cast<Instruction>(BLSI64rr#suffix) GR64:$src)>;

  // Versions to match flag producing ops.
  def : Pat<(and_flag_nocf GR32:$src, (add_su GR32:$src, -1)),
            (!cast<Instruction>(BLSR32rr#suffix) GR32:$src)>;
  def : Pat<(and_flag_nocf GR64:$src, (add_su GR64:$src, -1)),
            (!cast<Instruction>(BLSR64rr#suffix) GR64:$src)>;

  def : Pat<(xor_flag_nocf GR32:$src, (add_su GR32:$src, -1)),
            (!cast<Instruction>(BLSMSK32rr#suffix) GR32:$src)>;
  def : Pat<(xor_flag_nocf GR64:$src, (add_su GR64:$src, -1)),
            (!cast<Instruction>(BLSMSK64rr#suffix) GR64:$src)>;

  def : Pat<(and_flag_nocf GR32:$src, (ineg_su GR32:$src)),
            (!cast<Instruction>(BLSI32rr#suffix) GR32:$src)>;
  def : Pat<(and_flag_nocf GR64:$src, (ineg_su GR64:$src)),
            (!cast<Instruction>(BLSI64rr#suffix) GR64:$src)>;
}

let Predicates = [HasBMI, NoEGPR] in
  defm : Bls_Pats<"">;

let Predicates = [HasBMI, HasEGPR] in
  defm : Bls_Pats<"_EVEX">;

multiclass Bmi4VOp3<bits<8> o, string m, X86TypeInfo t, SDPatternOperator node,
                    X86FoldableSchedWrite sched, string Suffix = ""> {
  let SchedRW = [sched], Form = MRMSrcReg4VOp3 in
    def rr#Suffix : BinOpRR<o, m, binop_ndd_args, t, (outs t.RegClass:$dst),
                            [(set t.RegClass:$dst, EFLAGS,
                             (node t.RegClass:$src1, t.RegClass:$src2))]>, T8;
  let SchedRW = [sched.Folded,
                 ReadDefault, ReadDefault, ReadDefault, ReadDefault, ReadDefault,
                 sched.ReadAfterFold], Form =  MRMSrcMem4VOp3 in
    def rm#Suffix : BinOpMR<o, m, binop_ndd_args, t, (outs t.RegClass:$dst),
                            [(set t.RegClass:$dst, EFLAGS, (node (t.LoadNode addr:$src1),
                             t.RegClass:$src2))]>, T8;
}

let Predicates = [HasBMI, NoEGPR], Defs = [EFLAGS] in {
  defm BEXTR32 : Bmi4VOp3<0xF7, "bextr", Xi32, X86bextr, WriteBEXTR>, VEX;
  defm BEXTR64 : Bmi4VOp3<0xF7, "bextr", Xi64, X86bextr, WriteBEXTR>, VEX;
}
let Predicates = [HasBMI2, NoEGPR], Defs = [EFLAGS] in {
  defm BZHI32 : Bmi4VOp3<0xF5, "bzhi", Xi32, X86bzhi, WriteBZHI>, VEX;
  defm BZHI64 : Bmi4VOp3<0xF5, "bzhi", Xi64, X86bzhi, WriteBZHI>, VEX;
}
let Predicates = [HasBMI, HasEGPR, In64BitMode], Defs = [EFLAGS] in {
  defm BEXTR32 : Bmi4VOp3<0xF7, "bextr", Xi32, X86bextr, WriteBEXTR, "_EVEX">, EVEX;
  defm BEXTR64 : Bmi4VOp3<0xF7, "bextr", Xi64, X86bextr, WriteBEXTR, "_EVEX">, EVEX;
}
let Predicates = [HasBMI2, HasEGPR, In64BitMode], Defs = [EFLAGS] in {
  defm BZHI32 : Bmi4VOp3<0xF5, "bzhi", Xi32, X86bzhi, WriteBZHI, "_EVEX">, EVEX;
  defm BZHI64 : Bmi4VOp3<0xF5, "bzhi", Xi64, X86bzhi, WriteBZHI, "_EVEX">, EVEX;
}

let Predicates = [In64BitMode] in {
  defm BEXTR32 : Bmi4VOp3<0xF7, "bextr", Xi32, null_frag, WriteBEXTR, "_NF">, EVEX, EVEX_NF;
  defm BEXTR64 : Bmi4VOp3<0xF7, "bextr", Xi64, null_frag, WriteBEXTR, "_NF">, EVEX, EVEX_NF;
  defm BZHI32 : Bmi4VOp3<0xF5, "bzhi", Xi32, null_frag, WriteBZHI, "_NF">, EVEX, EVEX_NF;
  defm BZHI64 : Bmi4VOp3<0xF5, "bzhi", Xi64, null_frag, WriteBZHI, "_NF">, EVEX, EVEX_NF;
}

def CountTrailingOnes : SDNodeXForm<imm, [{
  // Count the trailing ones in the immediate.
  return getI8Imm(llvm::countr_one(N->getZExtValue()), SDLoc(N));
}]>;

def BEXTRMaskXForm : SDNodeXForm<imm, [{
  unsigned Length = llvm::countr_one(N->getZExtValue());
  return getI32Imm(Length << 8, SDLoc(N));
}]>;

def AndMask64 : ImmLeaf<i64, [{
  return isMask_64(Imm) && !isUInt<32>(Imm);
}]>;

// Use BEXTR for 64-bit 'and' with large immediate 'mask'.
let Predicates = [HasBMI, NoBMI2, NoTBM, NoEGPR] in {
  def : Pat<(and GR64:$src, AndMask64:$mask),
            (BEXTR64rr GR64:$src,
              (SUBREG_TO_REG (i64 0),
                             (MOV32ri (BEXTRMaskXForm imm:$mask)), sub_32bit))>;
  def : Pat<(and (loadi64 addr:$src), AndMask64:$mask),
            (BEXTR64rm addr:$src,
              (SUBREG_TO_REG (i64 0),
                             (MOV32ri (BEXTRMaskXForm imm:$mask)), sub_32bit))>;
}

let Predicates = [HasBMI, NoBMI2, NoTBM, HasEGPR] in {
  def : Pat<(and GR64:$src, AndMask64:$mask),
            (BEXTR64rr_EVEX GR64:$src,
              (SUBREG_TO_REG (i64 0),
                             (MOV32ri (BEXTRMaskXForm imm:$mask)), sub_32bit))>;
  def : Pat<(and (loadi64 addr:$src), AndMask64:$mask),
            (BEXTR64rm_EVEX addr:$src,
              (SUBREG_TO_REG (i64 0),
                             (MOV32ri (BEXTRMaskXForm imm:$mask)), sub_32bit))>;
}

// Use BZHI for 64-bit 'and' with large immediate 'mask'.
let Predicates = [HasBMI2, NoTBM, NoEGPR] in {
  def : Pat<(and GR64:$src, AndMask64:$mask),
            (BZHI64rr GR64:$src,
              (INSERT_SUBREG (i64 (IMPLICIT_DEF)),
                             (MOV8ri (CountTrailingOnes imm:$mask)), sub_8bit))>;
  def : Pat<(and (loadi64 addr:$src), AndMask64:$mask),
            (BZHI64rm addr:$src,
              (INSERT_SUBREG (i64 (IMPLICIT_DEF)),
                             (MOV8ri (CountTrailingOnes imm:$mask)), sub_8bit))>;
}

let Predicates = [HasBMI2, NoTBM, HasEGPR] in {
  def : Pat<(and GR64:$src, AndMask64:$mask),
            (BZHI64rr_EVEX GR64:$src,
              (INSERT_SUBREG (i64 (IMPLICIT_DEF)),
                             (MOV8ri (CountTrailingOnes imm:$mask)), sub_8bit))>;
  def : Pat<(and (loadi64 addr:$src), AndMask64:$mask),
            (BZHI64rm_EVEX addr:$src,
              (INSERT_SUBREG (i64 (IMPLICIT_DEF)),
                             (MOV8ri (CountTrailingOnes imm:$mask)), sub_8bit))>;
}

multiclass PdepPext<string m, X86TypeInfo t, SDPatternOperator node,
                    string suffix = ""> {
  def rr#suffix : ITy<0xF5, MRMSrcReg, t, (outs t.RegClass:$dst),
                      (ins t.RegClass:$src1, t.RegClass:$src2), m, binop_ndd_args,
                      [(set t.RegClass:$dst, (node t.RegClass:$src1, t.RegClass:$src2))]>,
                  T8, VVVV, Sched<[WriteALU]>;
  def rm#suffix : ITy<0xF5, MRMSrcMem, t, (outs t.RegClass:$dst),
                      (ins t.RegClass:$src1, t.MemOperand:$src2), m, binop_ndd_args,
                      [(set t.RegClass:$dst, (node t.RegClass:$src1, (t.LoadNode addr:$src2)))]>,
                  T8, VVVV, Sched<[WriteALU.Folded, WriteALU.ReadAfterFold]>;
}

let Predicates = [HasBMI2, NoEGPR] in {
  defm PDEP32 : PdepPext<"pdep", Xi32, X86pdep>, XD, VEX;
  defm PDEP64 : PdepPext<"pdep", Xi64, X86pdep>, XD, REX_W, VEX;
  defm PEXT32 : PdepPext<"pext", Xi32, X86pext>, XS, VEX;
  defm PEXT64 : PdepPext<"pext", Xi64, X86pext>, XS, REX_W, VEX;
}

let Predicates = [HasBMI2, HasEGPR] in {
  defm PDEP32 : PdepPext<"pdep", Xi32, X86pdep, "_EVEX">, XD, EVEX;
  defm PDEP64 : PdepPext<"pdep", Xi64, X86pdep, "_EVEX">, XD, REX_W, EVEX;
  defm PEXT32 : PdepPext<"pext", Xi32, X86pext, "_EVEX">, XS, EVEX;
  defm PEXT64 : PdepPext<"pext", Xi64, X86pext, "_EVEX">, XS, REX_W, EVEX;
}

//===----------------------------------------------------------------------===//
// Lightweight Profiling Instructions

let Predicates = [HasLWP], SchedRW = [WriteSystem] in {

def LLWPCB : I<0x12, MRM0r, (outs), (ins GR32:$src), "llwpcb\t$src",
               [(int_x86_llwpcb GR32:$src)]>, XOP, XOP9;
def SLWPCB : I<0x12, MRM1r, (outs GR32:$dst), (ins), "slwpcb\t$dst",
               [(set GR32:$dst, (int_x86_slwpcb))]>, XOP, XOP9;

def LLWPCB64 : I<0x12, MRM0r, (outs), (ins GR64:$src), "llwpcb\t$src",
                 [(int_x86_llwpcb GR64:$src)]>, XOP, XOP9, REX_W;
def SLWPCB64 : I<0x12, MRM1r, (outs GR64:$dst), (ins), "slwpcb\t$dst",
                 [(set GR64:$dst, (int_x86_slwpcb))]>, XOP, XOP9, REX_W;

multiclass lwpins_intr<RegisterClass RC> {
  def rri : Ii32<0x12, MRM0r, (outs), (ins RC:$src0, GR32:$src1, i32imm:$cntl),
                 "lwpins\t{$cntl, $src1, $src0|$src0, $src1, $cntl}",
                 [(set EFLAGS, (X86lwpins RC:$src0, GR32:$src1, timm:$cntl))]>,
                 XOP, VVVV, XOPA;
  let mayLoad = 1 in
  def rmi : Ii32<0x12, MRM0m, (outs), (ins RC:$src0, i32mem:$src1, i32imm:$cntl),
                 "lwpins\t{$cntl, $src1, $src0|$src0, $src1, $cntl}",
                 [(set EFLAGS, (X86lwpins RC:$src0, (loadi32 addr:$src1), timm:$cntl))]>,
                 XOP, VVVV, XOPA;
}

let Defs = [EFLAGS] in {
  defm LWPINS32 : lwpins_intr<GR32>;
  defm LWPINS64 : lwpins_intr<GR64>, REX_W;
} // EFLAGS

multiclass lwpval_intr<RegisterClass RC, Intrinsic Int> {
  def rri : Ii32<0x12, MRM1r, (outs), (ins RC:$src0, GR32:$src1, i32imm:$cntl),
                 "lwpval\t{$cntl, $src1, $src0|$src0, $src1, $cntl}",
                 [(Int RC:$src0, GR32:$src1, timm:$cntl)]>, XOP, VVVV, XOPA;
  let mayLoad = 1 in
  def rmi : Ii32<0x12, MRM1m, (outs), (ins RC:$src0, i32mem:$src1, i32imm:$cntl),
                 "lwpval\t{$cntl, $src1, $src0|$src0, $src1, $cntl}",
                 [(Int RC:$src0, (loadi32 addr:$src1), timm:$cntl)]>,
                 XOP, VVVV, XOPA;
}

defm LWPVAL32 : lwpval_intr<GR32, int_x86_lwpval32>;
defm LWPVAL64 : lwpval_intr<GR64, int_x86_lwpval64>, REX_W;

} // HasLWP, SchedRW

//===----------------------------------------------------------------------===//
// MONITORX/MWAITX Instructions
//
let SchedRW = [ WriteSystem ] in {
  let Uses = [ EAX, ECX, EDX ] in
  def MONITORX32rrr : I<0x01, MRM_FA, (outs), (ins), "monitorx", []>,
                      TB, Requires<[ HasMWAITX, Not64BitMode ]>;
  let Uses = [ RAX, ECX, EDX ] in
  def MONITORX64rrr : I<0x01, MRM_FA, (outs), (ins), "monitorx", []>,
                      TB, Requires<[ HasMWAITX, In64BitMode ]>;

  let Uses = [ ECX, EAX, EBX ] in {
    def MWAITXrrr : I<0x01, MRM_FB, (outs), (ins), "mwaitx",
                    []>, TB, Requires<[ HasMWAITX ]>;
  }
} // SchedRW

//===----------------------------------------------------------------------===//
// WAITPKG Instructions
//
let SchedRW = [WriteSystem] in {
  def UMONITOR16 : I<0xAE, MRM6r, (outs), (ins GR16:$src),
                     "umonitor\t$src", [(int_x86_umonitor GR16:$src)]>,
                     TB, XS, AdSize16, Requires<[HasWAITPKG, Not64BitMode]>;
  def UMONITOR32 : I<0xAE, MRM6r, (outs), (ins GR32:$src),
                     "umonitor\t$src", [(int_x86_umonitor GR32:$src)]>,
                     TB, XS, AdSize32, Requires<[HasWAITPKG]>;
  def UMONITOR64 : I<0xAE, MRM6r, (outs), (ins GR64:$src),
                     "umonitor\t$src", [(int_x86_umonitor GR64:$src)]>,
                     TB, XS, AdSize64, Requires<[HasWAITPKG, In64BitMode]>;
  let Uses = [EAX, EDX], Defs = [EFLAGS] in {
    def UMWAIT : I<0xAE, MRM6r,
                     (outs), (ins GR32orGR64:$src), "umwait\t$src",
                     [(set EFLAGS, (X86umwait GR32orGR64:$src, EDX, EAX))]>,
                     TB, XD, Requires<[HasWAITPKG]>;
    def TPAUSE : I<0xAE, MRM6r,
                     (outs), (ins GR32orGR64:$src), "tpause\t$src",
                     [(set EFLAGS, (X86tpause GR32orGR64:$src, EDX, EAX))]>,
                     TB, PD, Requires<[HasWAITPKG]>;
  }
} // SchedRW

//===----------------------------------------------------------------------===//
// MOVDIRI - Move doubleword/quadword as direct store
//
let SchedRW = [WriteStore] in {
def MOVDIRI32 : I<0xF9, MRMDestMem, (outs), (ins i32mem:$dst, GR32:$src),
                  "movdiri\t{$src, $dst|$dst, $src}",
                  [(int_x86_directstore32 addr:$dst, GR32:$src)]>,
                 T8, Requires<[HasMOVDIRI, NoEGPR]>;
def MOVDIRI64 : RI<0xF9, MRMDestMem, (outs), (ins i64mem:$dst, GR64:$src),
                   "movdiri\t{$src, $dst|$dst, $src}",
                   [(int_x86_directstore64 addr:$dst, GR64:$src)]>,
                  T8, Requires<[In64BitMode, HasMOVDIRI, NoEGPR]>;
def MOVDIRI32_EVEX : I<0xF9, MRMDestMem, (outs), (ins i32mem:$dst, GR32:$src),
                       "movdiri\t{$src, $dst|$dst, $src}",
                       [(int_x86_directstore32 addr:$dst, GR32:$src)]>,
                     EVEX, NoCD8, T_MAP4, Requires<[In64BitMode, HasMOVDIRI, HasEGPR]>;
def MOVDIRI64_EVEX : RI<0xF9, MRMDestMem, (outs), (ins i64mem:$dst, GR64:$src),
                        "movdiri\t{$src, $dst|$dst, $src}",
                        [(int_x86_directstore64 addr:$dst, GR64:$src)]>,
                     EVEX, NoCD8, T_MAP4, Requires<[In64BitMode, HasMOVDIRI, HasEGPR]>;
} // SchedRW

//===----------------------------------------------------------------------===//
// MOVDIR64B - Move 64 bytes as direct store
//
let SchedRW = [WriteStore] in {
def MOVDIR64B16 : I<0xF8, MRMSrcMem, (outs), (ins GR16:$dst, i512mem_GR16:$src),
                    "movdir64b\t{$src, $dst|$dst, $src}", []>,
                   T8, PD, AdSize16, Requires<[HasMOVDIR64B, Not64BitMode]>;
def MOVDIR64B32 : I<0xF8, MRMSrcMem, (outs), (ins GR32:$dst, i512mem_GR32:$src),
                    "movdir64b\t{$src, $dst|$dst, $src}",
                    [(int_x86_movdir64b GR32:$dst, addr:$src)]>,
                   T8, PD, AdSize32, Requires<[HasMOVDIR64B, NoEGPR]>;
def MOVDIR64B64 : I<0xF8, MRMSrcMem, (outs), (ins GR64:$dst, i512mem_GR64:$src),
                    "movdir64b\t{$src, $dst|$dst, $src}",
                    [(int_x86_movdir64b GR64:$dst, addr:$src)]>,
                   T8, PD, AdSize64, Requires<[HasMOVDIR64B, NoEGPR, In64BitMode]>;
def MOVDIR64B32_EVEX : I<0xF8, MRMSrcMem, (outs), (ins GR32:$dst, i512mem_GR32:$src),
                         "movdir64b\t{$src, $dst|$dst, $src}",
                         [(int_x86_movdir64b GR32:$dst, addr:$src)]>,
                       EVEX, NoCD8, T_MAP4, PD, AdSize32, Requires<[HasMOVDIR64B, HasEGPR, In64BitMode]>;
def MOVDIR64B64_EVEX : I<0xF8, MRMSrcMem, (outs), (ins GR64:$dst, i512mem_GR64:$src),
                         "movdir64b\t{$src, $dst|$dst, $src}",
                         [(int_x86_movdir64b GR64:$dst, addr:$src)]>,
                       EVEX, NoCD8, T_MAP4, PD, AdSize64, Requires<[HasMOVDIR64B, HasEGPR, In64BitMode]>;
} // SchedRW

//===----------------------------------------------------------------------===//
// ENQCMD/S - Enqueue 64-byte command as user with 64-byte write atomicity
//
multiclass Enqcmds<string suffix> {
  def ENQCMD32#suffix : I<0xF8, MRMSrcMem, (outs), (ins GR32:$dst, i512mem_GR32:$src),
                          "enqcmd\t{$src, $dst|$dst, $src}",
                          [(set EFLAGS, (X86enqcmd GR32:$dst, addr:$src))]>,
                        NoCD8, XD, AdSize32;
  def ENQCMD64#suffix : I<0xF8, MRMSrcMem, (outs), (ins GR64:$dst, i512mem_GR64:$src),
                          "enqcmd\t{$src, $dst|$dst, $src}",
                          [(set EFLAGS, (X86enqcmd GR64:$dst, addr:$src))]>,
                        NoCD8, XD, AdSize64;

  def ENQCMDS32#suffix : I<0xF8, MRMSrcMem, (outs), (ins GR32:$dst, i512mem_GR32:$src),
                           "enqcmds\t{$src, $dst|$dst, $src}",
                           [(set EFLAGS, (X86enqcmds GR32:$dst, addr:$src))]>,
                         NoCD8, XS, AdSize32;
  def ENQCMDS64#suffix : I<0xF8, MRMSrcMem, (outs), (ins GR64:$dst, i512mem_GR64:$src),
                          "enqcmds\t{$src, $dst|$dst, $src}",
                          [(set EFLAGS, (X86enqcmds GR64:$dst, addr:$src))]>,
                         NoCD8, XS, AdSize64;
}

let SchedRW = [WriteStore], Defs = [EFLAGS] in {
  def ENQCMD16 : I<0xF8, MRMSrcMem, (outs), (ins GR16:$dst, i512mem_GR16:$src),
                   "enqcmd\t{$src, $dst|$dst, $src}",
                   [(set EFLAGS, (X86enqcmd GR16:$dst, addr:$src))]>,
                 T8, XD, AdSize16, Requires<[HasENQCMD, Not64BitMode]>;
  def ENQCMDS16 : I<0xF8, MRMSrcMem, (outs), (ins GR16:$dst, i512mem_GR16:$src),
                    "enqcmds\t{$src, $dst|$dst, $src}",
                    [(set EFLAGS, (X86enqcmds GR16:$dst, addr:$src))]>,
                  T8, XS, AdSize16, Requires<[HasENQCMD, Not64BitMode]>;

  defm "" : Enqcmds<"">, T8, Requires<[HasENQCMD, NoEGPR]>;
  defm "" : Enqcmds<"_EVEX">, EVEX, T_MAP4, Requires<[HasENQCMD, HasEGPR, In64BitMode]>;

}

//===----------------------------------------------------------------------===//
// CLZERO Instruction
//
let SchedRW = [WriteLoad] in {
  let Uses = [EAX] in
  def CLZERO32r : I<0x01, MRM_FC, (outs), (ins), "clzero", []>,
                  TB, Requires<[HasCLZERO, Not64BitMode]>;
  let Uses = [RAX] in
  def CLZERO64r : I<0x01, MRM_FC, (outs), (ins), "clzero", []>,
                  TB, Requires<[HasCLZERO, In64BitMode]>;
} // SchedRW

//===----------------------------------------------------------------------===//
// INVLPGB Instruction
// OPCODE 0F 01 FE
//
let SchedRW = [WriteSystem] in {
  let Uses = [EAX, EDX] in
  def INVLPGB32 : I<0x01, MRM_FE, (outs), (ins),
                  "invlpgb", []>,
                  TB, Requires<[Not64BitMode]>;
  let Uses = [RAX, EDX] in
  def INVLPGB64 : I<0x01, MRM_FE, (outs), (ins),
                  "invlpgb", []>,
                  TB, Requires<[In64BitMode]>;
} // SchedRW

//===----------------------------------------------------------------------===//
// TLBSYNC Instruction
// OPCODE 0F 01 FF
//
let SchedRW = [WriteSystem] in {
  def TLBSYNC   : I<0x01, MRM_FF, (outs), (ins),
                  "tlbsync", []>,
                  TB, Requires<[]>;
} // SchedRW

//===----------------------------------------------------------------------===//
// HRESET Instruction
//
let Uses = [EAX], SchedRW = [WriteSystem] in
  def HRESET : Ii8<0xF0, MRM_C0, (outs), (ins i32u8imm:$imm), "hreset\t$imm", []>,
                   Requires<[HasHRESET]>, TA, XS;

//===----------------------------------------------------------------------===//
// SERIALIZE Instruction
//
let SchedRW = [WriteSystem] in
  def SERIALIZE : I<0x01, MRM_E8, (outs), (ins), "serialize",
                    [(int_x86_serialize)]>, TB,
                    Requires<[HasSERIALIZE]>;

//===----------------------------------------------------------------------===//
// TSXLDTRK - TSX Suspend Load Address Tracking
//
let Predicates = [HasTSXLDTRK], SchedRW = [WriteSystem] in {
  def XSUSLDTRK : I<0x01, MRM_E8, (outs), (ins), "xsusldtrk",
                    [(int_x86_xsusldtrk)]>, TB, XD;
  def XRESLDTRK : I<0x01, MRM_E9, (outs), (ins), "xresldtrk",
                    [(int_x86_xresldtrk)]>, TB, XD;
}

//===----------------------------------------------------------------------===//
// UINTR Instructions
//
let Predicates = [HasUINTR, In64BitMode], SchedRW = [WriteSystem] in {
  def UIRET : I<0x01, MRM_EC, (outs), (ins), "uiret",
               []>, TB, XS;
  def CLUI : I<0x01, MRM_EE, (outs), (ins), "clui",
               [(int_x86_clui)]>, TB, XS;
  def STUI : I<0x01, MRM_EF, (outs), (ins), "stui",
               [(int_x86_stui)]>, TB, XS;

  def SENDUIPI : I<0xC7, MRM6r, (outs), (ins GR64:$arg), "senduipi\t$arg",
                   [(int_x86_senduipi GR64:$arg)]>, TB, XS;

  let Defs = [EFLAGS] in
    def TESTUI : I<0x01, MRM_ED, (outs), (ins), "testui",
                   [(set EFLAGS, (X86testui))]>, TB, XS;
}

//===----------------------------------------------------------------------===//
// PREFETCHIT0 and PREFETCHIT1 Instructions
// prefetch ADDR, RW, Locality, Data
let Predicates = [HasPREFETCHI, In64BitMode], SchedRW = [WriteLoad] in {
  def PREFETCHIT0 : I<0x18, MRM7m, (outs), (ins i8mem:$src),
    "prefetchit0\t$src", [(prefetch addr:$src, (i32 0), (i32 3), (i32 0))]>, TB;
  def PREFETCHIT1 : I<0x18, MRM6m, (outs), (ins i8mem:$src),
    "prefetchit1\t$src", [(prefetch addr:$src, (i32 0), (i32 2), (i32 0))]>, TB;
}

//===----------------------------------------------------------------------===//
// CMPCCXADD Instructions
//
let isCodeGenOnly = 1, ForceDisassemble = 1, mayLoad = 1, mayStore = 1,
    Defs = [EFLAGS], Constraints = "$dstsrc1 = $dst" in {
let Predicates = [HasCMPCCXADD, NoEGPR, In64BitMode] in {
def CMPCCXADDmr32 : I<0xe0, MRMDestMem4VOp3CC, (outs GR32:$dst),
          (ins GR32:$dstsrc1, i32mem:$dstsrc2, GR32:$src3, ccode:$cond),
          "cmp${cond}xadd\t{$src3, $dst, $dstsrc2|$dstsrc2, $dst, $src3}",
          [(set GR32:$dst, (X86cmpccxadd addr:$dstsrc2,
            GR32:$dstsrc1, GR32:$src3, timm:$cond))]>,
          VEX, VVVV, T8, PD, Sched<[WriteXCHG]>;

def CMPCCXADDmr64 : I<0xe0, MRMDestMem4VOp3CC, (outs GR64:$dst),
          (ins GR64:$dstsrc1, i64mem:$dstsrc2, GR64:$src3, ccode:$cond),
          "cmp${cond}xadd\t{$src3, $dst, $dstsrc2|$dstsrc2, $dst, $src3}",
          [(set GR64:$dst, (X86cmpccxadd addr:$dstsrc2,
            GR64:$dstsrc1, GR64:$src3, timm:$cond))]>,
          VEX, VVVV, REX_W, T8, PD, Sched<[WriteXCHG]>;
}

let Predicates = [HasCMPCCXADD, HasEGPR, In64BitMode] in {
def CMPCCXADDmr32_EVEX : I<0xe0, MRMDestMem4VOp3CC, (outs GR32:$dst),
          (ins GR32:$dstsrc1, i32mem:$dstsrc2, GR32:$src3, ccode:$cond),
          "cmp${cond}xadd\t{$src3, $dst, $dstsrc2|$dstsrc2, $dst, $src3}",
          [(set GR32:$dst, (X86cmpccxadd addr:$dstsrc2,
            GR32:$dstsrc1, GR32:$src3, timm:$cond))]>,
          EVEX, VVVV, NoCD8, T8, PD, Sched<[WriteXCHG]>;

def CMPCCXADDmr64_EVEX : I<0xe0, MRMDestMem4VOp3CC, (outs GR64:$dst),
          (ins GR64:$dstsrc1, i64mem:$dstsrc2, GR64:$src3, ccode:$cond),
          "cmp${cond}xadd\t{$src3, $dst, $dstsrc2|$dstsrc2, $dst, $src3}",
          [(set GR64:$dst, (X86cmpccxadd addr:$dstsrc2,
            GR64:$dstsrc1, GR64:$src3, timm:$cond))]>,
          EVEX, VVVV, NoCD8, REX_W, T8, PD, Sched<[WriteXCHG]>;
}
}

//===----------------------------------------------------------------------===//
// Memory Instructions
//

let Predicates = [HasCLFLUSHOPT], SchedRW = [WriteLoad] in
def CLFLUSHOPT : I<0xAE, MRM7m, (outs), (ins i8mem:$src),
                   "clflushopt\t$src", [(int_x86_clflushopt addr:$src)]>, TB, PD;

let Predicates = [HasCLWB], SchedRW = [WriteLoad] in
def CLWB       : I<0xAE, MRM6m, (outs), (ins i8mem:$src), "clwb\t$src",
                   [(int_x86_clwb addr:$src)]>, TB, PD;

let Predicates = [HasCLDEMOTE], SchedRW = [WriteLoad] in
def CLDEMOTE : I<0x1C, MRM0m, (outs), (ins i8mem:$src), "cldemote\t$src",
                   [(int_x86_cldemote addr:$src)]>, TB;

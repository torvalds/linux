//==- X86InstrAsmAlias.td - Assembler Instruction Aliases --*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the assembler mnemonic/instruction aliases in the X86
// architecture.
//
//===----------------------------------------------------------------------===//

// Reversed version with ".s" suffix for GAS compatibility.
def : InstAlias<"mov{b}.s\t{$src, $dst|$dst, $src}",
                (MOV8rr_REV GR8:$dst, GR8:$src), 0>;
def : InstAlias<"mov{w}.s\t{$src, $dst|$dst, $src}",
                (MOV16rr_REV GR16:$dst, GR16:$src), 0>;
def : InstAlias<"mov{l}.s\t{$src, $dst|$dst, $src}",
                (MOV32rr_REV GR32:$dst, GR32:$src), 0>;
def : InstAlias<"mov{q}.s\t{$src, $dst|$dst, $src}",
                (MOV64rr_REV GR64:$dst, GR64:$src), 0>;
def : InstAlias<"mov.s\t{$src, $dst|$dst, $src}",
                (MOV8rr_REV GR8:$dst, GR8:$src), 0, "att">;
def : InstAlias<"mov.s\t{$src, $dst|$dst, $src}",
                (MOV16rr_REV GR16:$dst, GR16:$src), 0, "att">;
def : InstAlias<"mov.s\t{$src, $dst|$dst, $src}",
                (MOV32rr_REV GR32:$dst, GR32:$src), 0, "att">;
def : InstAlias<"mov.s\t{$src, $dst|$dst, $src}",
                (MOV64rr_REV GR64:$dst, GR64:$src), 0, "att">;

// MONITORX/MWAITX Instructions Alias
def : InstAlias<"mwaitx\t{%eax, %ecx, %ebx|ebx, ecx, eax}", (MWAITXrrr)>,
      Requires<[ Not64BitMode ]>;
def : InstAlias<"mwaitx\t{%rax, %rcx, %rbx|rbx, rcx, rax}", (MWAITXrrr)>,
      Requires<[ In64BitMode ]>;

// MONITORX/MWAITX Instructions Alias
def : InstAlias<"monitorx\t{%eax, %ecx, %edx|edx, ecx, eax}", (MONITORX32rrr)>,
      Requires<[ Not64BitMode ]>;
def : InstAlias<"monitorx\t{%rax, %rcx, %rdx|rdx, rcx, rax}", (MONITORX64rrr)>,
      Requires<[ In64BitMode ]>;

// CLZERO Instruction Alias
def : InstAlias<"clzero\t{%eax|eax}", (CLZERO32r)>, Requires<[Not64BitMode]>;
def : InstAlias<"clzero\t{%rax|rax}", (CLZERO64r)>, Requires<[In64BitMode]>;

// INVLPGB Instruction Alias
def : InstAlias<"invlpgb\t{%eax, %edx|eax, edx}", (INVLPGB32)>, Requires<[Not64BitMode]>;
def : InstAlias<"invlpgb\t{%rax, %edx|rax, edx}", (INVLPGB64)>, Requires<[In64BitMode]>;

// CMPCCXADD Instructions Alias
multiclass CMPCCXADD_Aliases<string Cond, int CC> {
  let Predicates = [In64BitMode] in {
    def : InstAlias<"cmp"#Cond#"xadd"#"\t{$src3, $dst, $dstsrc2|$dstsrc2, $dst, $src3}",
                    (CMPCCXADDmr32 GR32:$dst, i32mem:$dstsrc2, GR32:$src3, CC), 0>;
    def : InstAlias<"cmp"#Cond#"xadd"#"\t{$src3, $dst, $dstsrc2|$dstsrc2, $dst, $src3}",
                    (CMPCCXADDmr64 GR64:$dst, i64mem:$dstsrc2, GR64:$src3, CC), 0>;

    def : InstAlias<"cmp"#Cond#"xadd"#"\t{$src3, $dst, $dstsrc2|$dstsrc2, $dst, $src3}",
                    (CMPCCXADDmr32_EVEX GR32:$dst, i32mem:$dstsrc2, GR32:$src3, CC), 0>;
    def : InstAlias<"cmp"#Cond#"xadd"#"\t{$src3, $dst, $dstsrc2|$dstsrc2, $dst, $src3}",
                    (CMPCCXADDmr64_EVEX GR64:$dst, i64mem:$dstsrc2, GR64:$src3, CC), 0>;
  }
}

// CCMP Instructions Alias
multiclass CCMP_Aliases<string Cond, int CC> {
let Predicates = [In64BitMode] in {
def : InstAlias<"ccmp"#Cond#"{b} $dcf\t{$src2, $src1|$src1, $src2}",
                (CCMP8rr  GR8:$src1,  GR8:$src2,  cflags:$dcf, CC), 0>;
def : InstAlias<"ccmp"#Cond#"{w} $dcf\t{$src2, $src1|$src1, $src2}",
                (CCMP16rr GR16:$src1, GR16:$src2, cflags:$dcf, CC), 0>;
def : InstAlias<"ccmp"#Cond#"{l} $dcf\t{$src2, $src1|$src1, $src2}",
                (CCMP32rr GR32:$src1, GR32:$src2, cflags:$dcf, CC), 0>;
def : InstAlias<"ccmp"#Cond#"{q} $dcf\t{$src2, $src1|$src1, $src2}",
                (CCMP64rr GR64:$src1, GR64:$src2, cflags:$dcf, CC), 0>;
def : InstAlias<"ccmp"#Cond#"{b} $dcf\t{$src2, $src1|$src1, $src2}",
                (CCMP8rm  GR8:$src1,  i8mem:$src2,  cflags:$dcf, CC), 0>;
def : InstAlias<"ccmp"#Cond#"{w} $dcf\t{$src2, $src1|$src1, $src2}",
                (CCMP16rm GR16:$src1, i16mem:$src2, cflags:$dcf, CC), 0>;
def : InstAlias<"ccmp"#Cond#"{l} $dcf\t{$src2, $src1|$src1, $src2}",
                (CCMP32rm GR32:$src1, i32mem:$src2, cflags:$dcf, CC), 0>;
def : InstAlias<"ccmp"#Cond#"{q} $dcf\t{$src2, $src1|$src1, $src2}",
                (CCMP64rm GR64:$src1, i64mem:$src2, cflags:$dcf, CC), 0>;
def : InstAlias<"ccmp"#Cond#"{b} $dcf\t{$src2, $src1|$src1, $src2}",
                (CCMP8mr  i8mem:$src1,  GR8:$src2,  cflags:$dcf, CC), 0>;
def : InstAlias<"ccmp"#Cond#"{w} $dcf\t{$src2, $src1|$src1, $src2}",
                (CCMP16mr i16mem:$src1, GR16:$src2, cflags:$dcf, CC), 0>;
def : InstAlias<"ccmp"#Cond#"{l} $dcf\t{$src2, $src1|$src1, $src2}",
                (CCMP32mr i32mem:$src1, GR32:$src2, cflags:$dcf, CC), 0>;
def : InstAlias<"ccmp"#Cond#"{q} $dcf\t{$src2, $src1|$src1, $src2}",
                (CCMP64mr i64mem:$src1, GR64:$src2, cflags:$dcf, CC), 0>;
def : InstAlias<"ccmp"#Cond#"{b} $dcf\t{$src2, $src1|$src1, $src2}",
                (CCMP8ri  GR8:$src1,  i8imm:$src2,  cflags:$dcf, CC), 0>;
def : InstAlias<"ccmp"#Cond#"{w} $dcf\t{$src2, $src1|$src1, $src2}",
                (CCMP16ri GR16:$src1, i16imm:$src2, cflags:$dcf, CC), 0>;
def : InstAlias<"ccmp"#Cond#"{l} $dcf\t{$src2, $src1|$src1, $src2}",
                (CCMP32ri GR32:$src1, i32imm:$src2, cflags:$dcf, CC), 0>;
def : InstAlias<"ccmp"#Cond#"{q} $dcf\t{$src2, $src1|$src1, $src2}",
                (CCMP64ri32 GR64:$src1, i64i32imm:$src2, cflags:$dcf, CC), 0>;
def : InstAlias<"ccmp"#Cond#"{w} $dcf\t{$src2, $src1|$src1, $src2}",
                (CCMP16ri8 GR16:$src1, i16i8imm:$src2, cflags:$dcf, CC), 0>;
def : InstAlias<"ccmp"#Cond#"{l} $dcf\t{$src2, $src1|$src1, $src2}",
                (CCMP32ri8 GR32:$src1, i32i8imm:$src2, cflags:$dcf, CC), 0>;
def : InstAlias<"ccmp"#Cond#"{q} $dcf\t{$src2, $src1|$src1, $src2}",
                (CCMP64ri8 GR64:$src1, i64i8imm:$src2, cflags:$dcf, CC), 0>;
def : InstAlias<"ccmp"#Cond#"{b} $dcf\t{$src2, $src1|$src1, $src2}",
                (CCMP8mi  i8mem:$src1,  i8imm:$src2,  cflags:$dcf, CC), 0>;
def : InstAlias<"ccmp"#Cond#"{w} $dcf\t{$src2, $src1|$src1, $src2}",
                (CCMP16mi i16mem:$src1, i16imm:$src2, cflags:$dcf, CC), 0>;
def : InstAlias<"ccmp"#Cond#"{l} $dcf\t{$src2, $src1|$src1, $src2}",
                (CCMP32mi i32mem:$src1, i32imm:$src2, cflags:$dcf, CC), 0>;
def : InstAlias<"ccmp"#Cond#"{q} $dcf\t{$src2, $src1|$src1, $src2}",
                (CCMP64mi32 i64mem:$src1, i64i32imm:$src2, cflags:$dcf, CC), 0>;
def : InstAlias<"ccmp"#Cond#"{w} $dcf\t{$src2, $src1|$src1, $src2}",
                (CCMP16mi8 i16mem:$src1, i16i8imm:$src2, cflags:$dcf, CC), 0>;
def : InstAlias<"ccmp"#Cond#"{l} $dcf\t{$src2, $src1|$src1, $src2}",
                (CCMP32mi8 i32mem:$src1, i32i8imm:$src2, cflags:$dcf, CC), 0>;
def : InstAlias<"ccmp"#Cond#"{q} $dcf\t{$src2, $src1|$src1, $src2}",
                (CCMP64mi8 i64mem:$src1, i64i8imm:$src2, cflags:$dcf, CC), 0>;
}
}
defm : CCMP_Aliases<"o" ,  0>;
defm : CCMP_Aliases<"no",  1>;
defm : CCMP_Aliases<"b" ,  2>;
defm : CCMP_Aliases<"ae",  3>;
defm : CCMP_Aliases<"e" ,  4>;
defm : CCMP_Aliases<"ne",  5>;
defm : CCMP_Aliases<"be",  6>;
defm : CCMP_Aliases<"a" ,  7>;
defm : CCMP_Aliases<"s" ,  8>;
defm : CCMP_Aliases<"ns",  9>;
defm : CCMP_Aliases<"t" , 10>;
defm : CCMP_Aliases<"f", 11>;
defm : CCMP_Aliases<"l" , 12>;
defm : CCMP_Aliases<"ge", 13>;
defm : CCMP_Aliases<"le", 14>;
defm : CCMP_Aliases<"g" , 15>;

// CTEST Instructions Alias
multiclass CTEST_Aliases<string Cond, int CC> {
let Predicates = [In64BitMode] in {
def : InstAlias<"ctest"#Cond#"{b} $dcf\t{$src2, $src1|$src1, $src2}",
                (CTEST8rr  GR8:$src1,  GR8:$src2,  cflags:$dcf, CC), 0>;
def : InstAlias<"ctest"#Cond#"{w} $dcf\t{$src2, $src1|$src1, $src2}",
                (CTEST16rr GR16:$src1, GR16:$src2, cflags:$dcf, CC), 0>;
def : InstAlias<"ctest"#Cond#"{l} $dcf\t{$src2, $src1|$src1, $src2}",
                (CTEST32rr GR32:$src1, GR32:$src2, cflags:$dcf, CC), 0>;
def : InstAlias<"ctest"#Cond#"{q} $dcf\t{$src2, $src1|$src1, $src2}",
                (CTEST64rr GR64:$src1, GR64:$src2, cflags:$dcf, CC), 0>;
def : InstAlias<"ctest"#Cond#"{b} $dcf\t{$src2, $src1|$src1, $src2}",
                (CTEST8mr  i8mem:$src1,  GR8:$src2,  cflags:$dcf, CC), 0>;
def : InstAlias<"ctest"#Cond#"{w} $dcf\t{$src2, $src1|$src1, $src2}",
                (CTEST16mr i16mem:$src1, GR16:$src2, cflags:$dcf, CC), 0>;
def : InstAlias<"ctest"#Cond#"{l} $dcf\t{$src2, $src1|$src1, $src2}",
                (CTEST32mr i32mem:$src1, GR32:$src2, cflags:$dcf, CC), 0>;
def : InstAlias<"ctest"#Cond#"{q} $dcf\t{$src2, $src1|$src1, $src2}",
                (CTEST64mr i64mem:$src1, GR64:$src2, cflags:$dcf, CC), 0>;
def : InstAlias<"ctest"#Cond#"{b} $dcf\t{$src2, $src1|$src1, $src2}",
                (CTEST8ri  GR8:$src1,  i8imm:$src2,  cflags:$dcf, CC), 0>;
def : InstAlias<"ctest"#Cond#"{w} $dcf\t{$src2, $src1|$src1, $src2}",
                (CTEST16ri GR16:$src1, i16imm:$src2, cflags:$dcf, CC), 0>;
def : InstAlias<"ctest"#Cond#"{l} $dcf\t{$src2, $src1|$src1, $src2}",
                (CTEST32ri GR32:$src1, i32imm:$src2, cflags:$dcf, CC), 0>;
def : InstAlias<"ctest"#Cond#"{q} $dcf\t{$src2, $src1|$src1, $src2}",
                (CTEST64ri32 GR64:$src1, i64i32imm:$src2, cflags:$dcf, CC), 0>;
def : InstAlias<"ctest"#Cond#"{b} $dcf\t{$src2, $src1|$src1, $src2}",
                (CTEST8mi  i8mem:$src1,  i8imm:$src2,  cflags:$dcf, CC), 0>;
def : InstAlias<"ctest"#Cond#"{w} $dcf\t{$src2, $src1|$src1, $src2}",
                (CTEST16mi i16mem:$src1, i16imm:$src2, cflags:$dcf, CC), 0>;
def : InstAlias<"ctest"#Cond#"{l} $dcf\t{$src2, $src1|$src1, $src2}",
                (CTEST32mi i32mem:$src1, i32imm:$src2, cflags:$dcf, CC), 0>;
def : InstAlias<"ctest"#Cond#"{q} $dcf\t{$src2, $src1|$src1, $src2}",
                (CTEST64mi32 i64mem:$src1, i64i32imm:$src2, cflags:$dcf, CC), 0>;
}
}
defm : CTEST_Aliases<"o" ,  0>;
defm : CTEST_Aliases<"no",  1>;
defm : CTEST_Aliases<"b" ,  2>;
defm : CTEST_Aliases<"ae",  3>;
defm : CTEST_Aliases<"e" ,  4>;
defm : CTEST_Aliases<"ne",  5>;
defm : CTEST_Aliases<"be",  6>;
defm : CTEST_Aliases<"a" ,  7>;
defm : CTEST_Aliases<"s" ,  8>;
defm : CTEST_Aliases<"ns",  9>;
defm : CTEST_Aliases<"t" , 10>;
defm : CTEST_Aliases<"f", 11>;
defm : CTEST_Aliases<"l" , 12>;
defm : CTEST_Aliases<"ge", 13>;
defm : CTEST_Aliases<"le", 14>;
defm : CTEST_Aliases<"g" , 15>;

//===----------------------------------------------------------------------===//
// Assembler Mnemonic Aliases
//===----------------------------------------------------------------------===//

defm : CMPCCXADD_Aliases<"o" ,  0>;
defm : CMPCCXADD_Aliases<"no",  1>;
defm : CMPCCXADD_Aliases<"b" ,  2>;
defm : CMPCCXADD_Aliases<"ae",  3>;
defm : CMPCCXADD_Aliases<"nb",  3>;
defm : CMPCCXADD_Aliases<"e" ,  4>;
defm : CMPCCXADD_Aliases<"z" ,  4>;
defm : CMPCCXADD_Aliases<"ne",  5>;
defm : CMPCCXADD_Aliases<"nz",  5>;
defm : CMPCCXADD_Aliases<"be",  6>;
defm : CMPCCXADD_Aliases<"nbe", 7>;
defm : CMPCCXADD_Aliases<"a",   7>;
defm : CMPCCXADD_Aliases<"s" ,  8>;
defm : CMPCCXADD_Aliases<"ns",  9>;
defm : CMPCCXADD_Aliases<"p" , 10>;
defm : CMPCCXADD_Aliases<"np", 11>;
defm : CMPCCXADD_Aliases<"l" , 12>;
defm : CMPCCXADD_Aliases<"ge", 13>;
defm : CMPCCXADD_Aliases<"nl", 13>;
defm : CMPCCXADD_Aliases<"le", 14>;
defm : CMPCCXADD_Aliases<"g",  15>;
defm : CMPCCXADD_Aliases<"nle",15>;


def : MnemonicAlias<"call", "callw", "att">, Requires<[In16BitMode]>;
def : MnemonicAlias<"call", "calll", "att">, Requires<[In32BitMode]>;
def : MnemonicAlias<"call", "callq", "att">, Requires<[In64BitMode]>;

def : MnemonicAlias<"cbw",  "cbtw", "att">;
def : MnemonicAlias<"cwde", "cwtl", "att">;
def : MnemonicAlias<"cwd",  "cwtd", "att">;
def : MnemonicAlias<"cdq",  "cltd", "att">;
def : MnemonicAlias<"cdqe", "cltq", "att">;
def : MnemonicAlias<"cqo",  "cqto", "att">;

// In 64-bit mode lret maps to lretl; it is not ambiguous with lretq.
def : MnemonicAlias<"lret", "lretw", "att">, Requires<[In16BitMode]>;
def : MnemonicAlias<"lret", "lretl", "att">, Requires<[Not16BitMode]>;

def : MnemonicAlias<"leavel", "leave", "att">, Requires<[Not64BitMode]>;
def : MnemonicAlias<"leaveq", "leave", "att">, Requires<[In64BitMode]>;

def : MnemonicAlias<"loopz",  "loope">;
def : MnemonicAlias<"loopnz", "loopne">;

def : MnemonicAlias<"pop",   "popw",  "att">, Requires<[In16BitMode]>;
def : MnemonicAlias<"pop",   "popl",  "att">, Requires<[In32BitMode]>;
def : MnemonicAlias<"pop",   "popq",  "att">, Requires<[In64BitMode]>;
def : MnemonicAlias<"popf",  "popfw", "att">, Requires<[In16BitMode]>;
def : MnemonicAlias<"popf",  "popfl", "att">, Requires<[In32BitMode]>;
def : MnemonicAlias<"popf",  "popfq", "att">, Requires<[In64BitMode]>;
def : MnemonicAlias<"popf",  "popfq", "intel">, Requires<[In64BitMode]>;
def : MnemonicAlias<"popfd", "popfl", "att">;
def : MnemonicAlias<"popfw", "popf",  "intel">, Requires<[In32BitMode]>;
def : MnemonicAlias<"popfw", "popf",  "intel">, Requires<[In64BitMode]>;

// FIXME: This is wrong for "push reg".  "push %bx" should turn into pushw in
// all modes.  However: "push (addr)" and "push $42" should default to
// pushl/pushq depending on the current mode.  Similar for "pop %bx"
def : MnemonicAlias<"push",   "pushw",  "att">, Requires<[In16BitMode]>;
def : MnemonicAlias<"push",   "pushl",  "att">, Requires<[In32BitMode]>;
def : MnemonicAlias<"push",   "pushq",  "att">, Requires<[In64BitMode]>;
def : MnemonicAlias<"pushf",  "pushfw", "att">, Requires<[In16BitMode]>;
def : MnemonicAlias<"pushf",  "pushfl", "att">, Requires<[In32BitMode]>;
def : MnemonicAlias<"pushf",  "pushfq", "att">, Requires<[In64BitMode]>;
def : MnemonicAlias<"pushf",  "pushfq", "intel">, Requires<[In64BitMode]>;
def : MnemonicAlias<"pushfd", "pushfl", "att">;
def : MnemonicAlias<"pushfw", "pushf",  "intel">, Requires<[In32BitMode]>;
def : MnemonicAlias<"pushfw", "pushf",  "intel">, Requires<[In64BitMode]>;

def : MnemonicAlias<"popad",  "popal",  "intel">, Requires<[Not64BitMode]>;
def : MnemonicAlias<"pushad", "pushal", "intel">, Requires<[Not64BitMode]>;
def : MnemonicAlias<"popa",   "popaw",  "intel">, Requires<[In16BitMode]>;
def : MnemonicAlias<"pusha",  "pushaw", "intel">, Requires<[In16BitMode]>;
def : MnemonicAlias<"popa",   "popal",  "intel">, Requires<[In32BitMode]>;
def : MnemonicAlias<"pusha",  "pushal", "intel">, Requires<[In32BitMode]>;

def : MnemonicAlias<"popa",   "popaw",  "att">, Requires<[In16BitMode]>;
def : MnemonicAlias<"pusha",  "pushaw", "att">, Requires<[In16BitMode]>;
def : MnemonicAlias<"popa",   "popal",  "att">, Requires<[In32BitMode]>;
def : MnemonicAlias<"pusha",  "pushal", "att">, Requires<[In32BitMode]>;

def : MnemonicAlias<"repe",  "rep">;
def : MnemonicAlias<"repz",  "rep">;
def : MnemonicAlias<"repnz", "repne">;

def : MnemonicAlias<"ret", "retw", "att">, Requires<[In16BitMode]>;
def : MnemonicAlias<"ret", "retl", "att">, Requires<[In32BitMode]>;
def : MnemonicAlias<"ret", "retq", "att">, Requires<[In64BitMode]>;

// Apply 'ret' behavior to 'retn'
def : MnemonicAlias<"retn", "retw", "att">, Requires<[In16BitMode]>;
def : MnemonicAlias<"retn", "retl", "att">, Requires<[In32BitMode]>;
def : MnemonicAlias<"retn", "retq", "att">, Requires<[In64BitMode]>;
def : MnemonicAlias<"retn", "ret", "intel">;

def : MnemonicAlias<"sal", "shl", "intel">;
def : MnemonicAlias<"salb", "shlb", "att">;
def : MnemonicAlias<"salw", "shlw", "att">;
def : MnemonicAlias<"sall", "shll", "att">;
def : MnemonicAlias<"salq", "shlq", "att">;

def : MnemonicAlias<"smovb", "movsb", "att">;
def : MnemonicAlias<"smovw", "movsw", "att">;
def : MnemonicAlias<"smovl", "movsl", "att">;
def : MnemonicAlias<"smovq", "movsq", "att">;

def : MnemonicAlias<"ud2a",  "ud2",  "att">;
def : MnemonicAlias<"ud2bw", "ud1w", "att">;
def : MnemonicAlias<"ud2bl", "ud1l", "att">;
def : MnemonicAlias<"ud2bq", "ud1q", "att">;
def : MnemonicAlias<"verrw", "verr", "att">;

// MS recognizes 'xacquire'/'xrelease' as 'acquire'/'release'
def : MnemonicAlias<"acquire", "xacquire", "intel">;
def : MnemonicAlias<"release", "xrelease", "intel">;

// System instruction aliases.
def : MnemonicAlias<"iret",    "iretw",    "att">, Requires<[In16BitMode]>;
def : MnemonicAlias<"iret",    "iretl",    "att">, Requires<[Not16BitMode]>;
def : MnemonicAlias<"sysret",  "sysretl",  "att">;
def : MnemonicAlias<"sysexit", "sysexitl", "att">;

def : MnemonicAlias<"lgdt", "lgdtw", "att">, Requires<[In16BitMode]>;
def : MnemonicAlias<"lgdt", "lgdtl", "att">, Requires<[In32BitMode]>;
def : MnemonicAlias<"lgdt", "lgdtq", "att">, Requires<[In64BitMode]>;
def : MnemonicAlias<"lidt", "lidtw", "att">, Requires<[In16BitMode]>;
def : MnemonicAlias<"lidt", "lidtl", "att">, Requires<[In32BitMode]>;
def : MnemonicAlias<"lidt", "lidtq", "att">, Requires<[In64BitMode]>;
def : MnemonicAlias<"sgdt", "sgdtw", "att">, Requires<[In16BitMode]>;
def : MnemonicAlias<"sgdt", "sgdtl", "att">, Requires<[In32BitMode]>;
def : MnemonicAlias<"sgdt", "sgdtq", "att">, Requires<[In64BitMode]>;
def : MnemonicAlias<"sidt", "sidtw", "att">, Requires<[In16BitMode]>;
def : MnemonicAlias<"sidt", "sidtl", "att">, Requires<[In32BitMode]>;
def : MnemonicAlias<"sidt", "sidtq", "att">, Requires<[In64BitMode]>;
def : MnemonicAlias<"lgdt", "lgdtw", "intel">, Requires<[In16BitMode]>;
def : MnemonicAlias<"lgdt", "lgdtd", "intel">, Requires<[In32BitMode]>;
def : MnemonicAlias<"lidt", "lidtw", "intel">, Requires<[In16BitMode]>;
def : MnemonicAlias<"lidt", "lidtd", "intel">, Requires<[In32BitMode]>;
def : MnemonicAlias<"sgdt", "sgdtw", "intel">, Requires<[In16BitMode]>;
def : MnemonicAlias<"sgdt", "sgdtd", "intel">, Requires<[In32BitMode]>;
def : MnemonicAlias<"sidt", "sidtw", "intel">, Requires<[In16BitMode]>;
def : MnemonicAlias<"sidt", "sidtd", "intel">, Requires<[In32BitMode]>;


// Floating point stack aliases.
def : MnemonicAlias<"fcmovz",   "fcmove",   "att">;
def : MnemonicAlias<"fcmova",   "fcmovnbe", "att">;
def : MnemonicAlias<"fcmovnae", "fcmovb",   "att">;
def : MnemonicAlias<"fcmovna",  "fcmovbe",  "att">;
def : MnemonicAlias<"fcmovae",  "fcmovnb",  "att">;
def : MnemonicAlias<"fcomip",   "fcompi">;
def : MnemonicAlias<"fildq",    "fildll",   "att">;
def : MnemonicAlias<"fistpq",   "fistpll",  "att">;
def : MnemonicAlias<"fisttpq",  "fisttpll", "att">;
def : MnemonicAlias<"fldcww",   "fldcw",    "att">;
def : MnemonicAlias<"fnstcww",  "fnstcw",   "att">;
def : MnemonicAlias<"fnstsww",  "fnstsw",   "att">;
def : MnemonicAlias<"fucomip",  "fucompi">;
def : MnemonicAlias<"fwait",    "wait">;

def : MnemonicAlias<"fxsaveq",   "fxsave64",   "att">;
def : MnemonicAlias<"fxrstorq",  "fxrstor64",  "att">;
def : MnemonicAlias<"xsaveq",    "xsave64",    "att">;
def : MnemonicAlias<"xrstorq",   "xrstor64",   "att">;
def : MnemonicAlias<"xsaveoptq", "xsaveopt64", "att">;
def : MnemonicAlias<"xrstorsq",  "xrstors64",  "att">;
def : MnemonicAlias<"xsavecq",   "xsavec64",   "att">;
def : MnemonicAlias<"xsavesq",   "xsaves64",   "att">;

class CondCodeAlias<string Prefix,string Suffix, string OldCond, string NewCond,
                    string VariantName>
  : MnemonicAlias<!strconcat(Prefix, OldCond, Suffix),
                  !strconcat(Prefix, NewCond, Suffix), VariantName>;

/// IntegerCondCodeMnemonicAlias - This multiclass defines a bunch of
/// MnemonicAlias's that canonicalize the condition code in a mnemonic, for
/// example "setz" -> "sete".
multiclass IntegerCondCodeMnemonicAlias<string Prefix, string Suffix,
                                        string V = ""> {
  def C   : CondCodeAlias<Prefix, Suffix, "c",   "b",  V>; // setc   -> setb
  def Z   : CondCodeAlias<Prefix, Suffix, "z" ,  "e",  V>; // setz   -> sete
  def NA  : CondCodeAlias<Prefix, Suffix, "na",  "be", V>; // setna  -> setbe
  def NB  : CondCodeAlias<Prefix, Suffix, "nb",  "ae", V>; // setnb  -> setae
  def NC  : CondCodeAlias<Prefix, Suffix, "nc",  "ae", V>; // setnc  -> setae
  def NG  : CondCodeAlias<Prefix, Suffix, "ng",  "le", V>; // setng  -> setle
  def NL  : CondCodeAlias<Prefix, Suffix, "nl",  "ge", V>; // setnl  -> setge
  def NZ  : CondCodeAlias<Prefix, Suffix, "nz",  "ne", V>; // setnz  -> setne
  def PE  : CondCodeAlias<Prefix, Suffix, "pe",  "p",  V>; // setpe  -> setp
  def PO  : CondCodeAlias<Prefix, Suffix, "po",  "np", V>; // setpo  -> setnp

  def NAE : CondCodeAlias<Prefix, Suffix, "nae", "b",  V>; // setnae -> setb
  def NBE : CondCodeAlias<Prefix, Suffix, "nbe", "a",  V>; // setnbe -> seta
  def NGE : CondCodeAlias<Prefix, Suffix, "nge", "l",  V>; // setnge -> setl
  def NLE : CondCodeAlias<Prefix, Suffix, "nle", "g",  V>; // setnle -> setg
}

// Aliases for set<CC>
defm : IntegerCondCodeMnemonicAlias<"set", "">;
// Aliases for j<CC>
defm : IntegerCondCodeMnemonicAlias<"j", "">;
// Aliases for cmov<CC>{w,l,q}
defm : IntegerCondCodeMnemonicAlias<"cmov", "w", "att">;
defm : IntegerCondCodeMnemonicAlias<"cmov", "l", "att">;
defm : IntegerCondCodeMnemonicAlias<"cmov", "q", "att">;
// No size suffix for intel-style asm.
defm : IntegerCondCodeMnemonicAlias<"cmov", "", "intel">;

// Aliases for cfcmov<CC>{w,l,q}
defm : IntegerCondCodeMnemonicAlias<"cfcmov", "w", "att">;
defm : IntegerCondCodeMnemonicAlias<"cfcmov", "l", "att">;
defm : IntegerCondCodeMnemonicAlias<"cfcmov", "q", "att">;
// No size suffix for intel-style asm.
defm : IntegerCondCodeMnemonicAlias<"cfcmov", "", "intel">;
//===----------------------------------------------------------------------===//
// Assembler Instruction Aliases
//===----------------------------------------------------------------------===//

// aad/aam default to base 10 if no operand is specified.
def : InstAlias<"aad", (AAD8i8 10)>, Requires<[Not64BitMode]>;
def : InstAlias<"aam", (AAM8i8 10)>, Requires<[Not64BitMode]>;

// Disambiguate the mem/imm form of bt-without-a-suffix as btl.
// Likewise for btc/btr/bts.
def : InstAlias<"bt\t{$imm, $mem|$mem, $imm}",
                (BT32mi8 i32mem:$mem, i32u8imm:$imm), 0, "att">;
def : InstAlias<"btc\t{$imm, $mem|$mem, $imm}",
                (BTC32mi8 i32mem:$mem, i32u8imm:$imm), 0, "att">;
def : InstAlias<"btr\t{$imm, $mem|$mem, $imm}",
                (BTR32mi8 i32mem:$mem, i32u8imm:$imm), 0, "att">;
def : InstAlias<"bts\t{$imm, $mem|$mem, $imm}",
                (BTS32mi8 i32mem:$mem, i32u8imm:$imm), 0, "att">;

// clr aliases.
def : InstAlias<"clr{b}\t$reg", (XOR8rr  GR8 :$reg, GR8 :$reg), 0>;
def : InstAlias<"clr{w}\t$reg", (XOR16rr GR16:$reg, GR16:$reg), 0>;
def : InstAlias<"clr{l}\t$reg", (XOR32rr GR32:$reg, GR32:$reg), 0>;
def : InstAlias<"clr{q}\t$reg", (XOR64rr GR64:$reg, GR64:$reg), 0>;

// lods aliases. Accept the destination being omitted because it's implicit
// in the mnemonic, or the mnemonic suffix being omitted because it's implicit
// in the destination.
def : InstAlias<"lodsb\t$src", (LODSB srcidx8:$src),  0>;
def : InstAlias<"lodsw\t$src", (LODSW srcidx16:$src), 0>;
def : InstAlias<"lods{l|d}\t$src", (LODSL srcidx32:$src), 0>;
def : InstAlias<"lodsq\t$src", (LODSQ srcidx64:$src), 0>, Requires<[In64BitMode]>;
def : InstAlias<"lods\t{$src, %al|al, $src}", (LODSB srcidx8:$src),  0>;
def : InstAlias<"lods\t{$src, %ax|ax, $src}", (LODSW srcidx16:$src), 0>;
def : InstAlias<"lods\t{$src, %eax|eax, $src}", (LODSL srcidx32:$src), 0>;
def : InstAlias<"lods\t{$src, %rax|rax, $src}", (LODSQ srcidx64:$src), 0>, Requires<[In64BitMode]>;
def : InstAlias<"lods\t$src", (LODSB srcidx8:$src),  0, "intel">;
def : InstAlias<"lods\t$src", (LODSW srcidx16:$src), 0, "intel">;
def : InstAlias<"lods\t$src", (LODSL srcidx32:$src), 0, "intel">;
def : InstAlias<"lods\t$src", (LODSQ srcidx64:$src), 0, "intel">, Requires<[In64BitMode]>;


// stos aliases. Accept the source being omitted because it's implicit in
// the mnemonic, or the mnemonic suffix being omitted because it's implicit
// in the source.
def : InstAlias<"stosb\t$dst", (STOSB dstidx8:$dst),  0>;
def : InstAlias<"stosw\t$dst", (STOSW dstidx16:$dst), 0>;
def : InstAlias<"stos{l|d}\t$dst", (STOSL dstidx32:$dst), 0>;
def : InstAlias<"stosq\t$dst", (STOSQ dstidx64:$dst), 0>, Requires<[In64BitMode]>;
def : InstAlias<"stos\t{%al, $dst|$dst, al}", (STOSB dstidx8:$dst),  0>;
def : InstAlias<"stos\t{%ax, $dst|$dst, ax}", (STOSW dstidx16:$dst), 0>;
def : InstAlias<"stos\t{%eax, $dst|$dst, eax}", (STOSL dstidx32:$dst), 0>;
def : InstAlias<"stos\t{%rax, $dst|$dst, rax}", (STOSQ dstidx64:$dst), 0>, Requires<[In64BitMode]>;
def : InstAlias<"stos\t$dst", (STOSB dstidx8:$dst),  0, "intel">;
def : InstAlias<"stos\t$dst", (STOSW dstidx16:$dst), 0, "intel">;
def : InstAlias<"stos\t$dst", (STOSL dstidx32:$dst), 0, "intel">;
def : InstAlias<"stos\t$dst", (STOSQ dstidx64:$dst), 0, "intel">, Requires<[In64BitMode]>;


// scas aliases. Accept the destination being omitted because it's implicit
// in the mnemonic, or the mnemonic suffix being omitted because it's implicit
// in the destination.
def : InstAlias<"scasb\t$dst", (SCASB dstidx8:$dst),  0>;
def : InstAlias<"scasw\t$dst", (SCASW dstidx16:$dst), 0>;
def : InstAlias<"scas{l|d}\t$dst", (SCASL dstidx32:$dst), 0>;
def : InstAlias<"scasq\t$dst", (SCASQ dstidx64:$dst), 0>, Requires<[In64BitMode]>;
def : InstAlias<"scas\t{$dst, %al|al, $dst}", (SCASB dstidx8:$dst),  0>;
def : InstAlias<"scas\t{$dst, %ax|ax, $dst}", (SCASW dstidx16:$dst), 0>;
def : InstAlias<"scas\t{$dst, %eax|eax, $dst}", (SCASL dstidx32:$dst), 0>;
def : InstAlias<"scas\t{$dst, %rax|rax, $dst}", (SCASQ dstidx64:$dst), 0>, Requires<[In64BitMode]>;
def : InstAlias<"scas\t$dst", (SCASB dstidx8:$dst),  0, "intel">;
def : InstAlias<"scas\t$dst", (SCASW dstidx16:$dst), 0, "intel">;
def : InstAlias<"scas\t$dst", (SCASL dstidx32:$dst), 0, "intel">;
def : InstAlias<"scas\t$dst", (SCASQ dstidx64:$dst), 0, "intel">, Requires<[In64BitMode]>;

// cmps aliases. Mnemonic suffix being omitted because it's implicit
// in the destination.
def : InstAlias<"cmps\t{$dst, $src|$src, $dst}", (CMPSB dstidx8:$dst, srcidx8:$src),   0, "intel">;
def : InstAlias<"cmps\t{$dst, $src|$src, $dst}", (CMPSW dstidx16:$dst, srcidx16:$src), 0, "intel">;
def : InstAlias<"cmps\t{$dst, $src|$src, $dst}", (CMPSL dstidx32:$dst, srcidx32:$src), 0, "intel">;
def : InstAlias<"cmps\t{$dst, $src|$src, $dst}", (CMPSQ dstidx64:$dst, srcidx64:$src), 0, "intel">, Requires<[In64BitMode]>;

// movs aliases. Mnemonic suffix being omitted because it's implicit
// in the destination.
def : InstAlias<"movs\t{$src, $dst|$dst, $src}", (MOVSB dstidx8:$dst, srcidx8:$src),   0, "intel">;
def : InstAlias<"movs\t{$src, $dst|$dst, $src}", (MOVSW dstidx16:$dst, srcidx16:$src), 0, "intel">;
def : InstAlias<"movs\t{$src, $dst|$dst, $src}", (MOVSL dstidx32:$dst, srcidx32:$src), 0, "intel">;
def : InstAlias<"movs\t{$src, $dst|$dst, $src}", (MOVSQ dstidx64:$dst, srcidx64:$src), 0, "intel">, Requires<[In64BitMode]>;

// div and idiv aliases for explicit A register.
def : InstAlias<"div{b}\t{$src, %al|al, $src}", (DIV8r  GR8 :$src)>;
def : InstAlias<"div{w}\t{$src, %ax|ax, $src}", (DIV16r GR16:$src)>;
def : InstAlias<"div{l}\t{$src, %eax|eax, $src}", (DIV32r GR32:$src)>;
def : InstAlias<"div{q}\t{$src, %rax|rax, $src}", (DIV64r GR64:$src)>;
def : InstAlias<"div{b}\t{$src, %al|al, $src}", (DIV8m  i8mem :$src)>;
def : InstAlias<"div{w}\t{$src, %ax|ax, $src}", (DIV16m i16mem:$src)>;
def : InstAlias<"div{l}\t{$src, %eax|eax, $src}", (DIV32m i32mem:$src)>;
def : InstAlias<"div{q}\t{$src, %rax|rax, $src}", (DIV64m i64mem:$src)>;
def : InstAlias<"idiv{b}\t{$src, %al|al, $src}", (IDIV8r  GR8 :$src)>;
def : InstAlias<"idiv{w}\t{$src, %ax|ax, $src}", (IDIV16r GR16:$src)>;
def : InstAlias<"idiv{l}\t{$src, %eax|eax, $src}", (IDIV32r GR32:$src)>;
def : InstAlias<"idiv{q}\t{$src, %rax|rax, $src}", (IDIV64r GR64:$src)>;
def : InstAlias<"idiv{b}\t{$src, %al|al, $src}", (IDIV8m  i8mem :$src)>;
def : InstAlias<"idiv{w}\t{$src, %ax|ax, $src}", (IDIV16m i16mem:$src)>;
def : InstAlias<"idiv{l}\t{$src, %eax|eax, $src}", (IDIV32m i32mem:$src)>;
def : InstAlias<"idiv{q}\t{$src, %rax|rax, $src}", (IDIV64m i64mem:$src)>;



// Various unary fpstack operations default to operating on ST1.
// For example, "fxch" -> "fxch %st(1)"
def : InstAlias<"faddp",        (ADD_FPrST0  ST1), 0>;
def:  InstAlias<"fadd",         (ADD_FPrST0  ST1), 0>;
def : InstAlias<"fsub{|r}p",    (SUBR_FPrST0 ST1), 0>;
def : InstAlias<"fsub{r|}p",    (SUB_FPrST0  ST1), 0>;
def : InstAlias<"fmul",         (MUL_FPrST0  ST1), 0>;
def : InstAlias<"fmulp",        (MUL_FPrST0  ST1), 0>;
def : InstAlias<"fdiv{|r}p",    (DIVR_FPrST0 ST1), 0>;
def : InstAlias<"fdiv{r|}p",    (DIV_FPrST0  ST1), 0>;
def : InstAlias<"fxch",         (XCH_F       ST1), 0>;
def : InstAlias<"fcom",         (COM_FST0r   ST1), 0>;
def : InstAlias<"fcomp",        (COMP_FST0r  ST1), 0>;
def : InstAlias<"fcomi",        (COM_FIr     ST1), 0>;
def : InstAlias<"fcompi",       (COM_FIPr    ST1), 0>;
def : InstAlias<"fucom",        (UCOM_Fr     ST1), 0>;
def : InstAlias<"fucomp",       (UCOM_FPr    ST1), 0>;
def : InstAlias<"fucomi",       (UCOM_FIr    ST1), 0>;
def : InstAlias<"fucompi",      (UCOM_FIPr   ST1), 0>;

// Handle fmul/fadd/fsub/fdiv instructions with explicitly written st(0) op.
// For example, "fadd %st(4), %st(0)" -> "fadd %st(4)".  We also disambiguate
// instructions like "fadd %st(0), %st(0)" as "fadd %st(0)" for consistency with
// gas.
multiclass FpUnaryAlias<string Mnemonic, Instruction Inst, bit EmitAlias = 1> {
 def : InstAlias<!strconcat(Mnemonic, "\t$op"),
                 (Inst RSTi:$op), EmitAlias>;
 def : InstAlias<!strconcat(Mnemonic, "\t{%st, %st|st, st}"),
                 (Inst ST0), EmitAlias>;
}

defm : FpUnaryAlias<"fadd",   ADD_FST0r, 0>;
defm : FpUnaryAlias<"faddp",  ADD_FPrST0, 0>;
defm : FpUnaryAlias<"fsub",   SUB_FST0r, 0>;
defm : FpUnaryAlias<"fsub{|r}p",  SUBR_FPrST0, 0>;
defm : FpUnaryAlias<"fsubr",  SUBR_FST0r, 0>;
defm : FpUnaryAlias<"fsub{r|}p", SUB_FPrST0, 0>;
defm : FpUnaryAlias<"fmul",   MUL_FST0r, 0>;
defm : FpUnaryAlias<"fmulp",  MUL_FPrST0, 0>;
defm : FpUnaryAlias<"fdiv",   DIV_FST0r, 0>;
defm : FpUnaryAlias<"fdiv{|r}p",  DIVR_FPrST0, 0>;
defm : FpUnaryAlias<"fdivr",  DIVR_FST0r, 0>;
defm : FpUnaryAlias<"fdiv{r|}p", DIV_FPrST0, 0>;
defm : FpUnaryAlias<"fcomi",   COM_FIr, 0>;
defm : FpUnaryAlias<"fucomi",  UCOM_FIr, 0>;
defm : FpUnaryAlias<"fcompi",   COM_FIPr, 0>;
defm : FpUnaryAlias<"fucompi",  UCOM_FIPr, 0>;


// Handle "f{mulp,addp} $op, %st(0)" the same as "f{mulp,addp} $op", since they
// commute.  We also allow fdiv[r]p/fsubrp even though they don't commute,
// solely because gas supports it.
def : InstAlias<"faddp\t{$op, %st|st, $op}", (ADD_FPrST0 RSTi:$op), 0>;
def : InstAlias<"fmulp\t{$op, %st|st, $op}", (MUL_FPrST0 RSTi:$op), 0>;
def : InstAlias<"fsub{|r}p\t{$op, %st|st, $op}", (SUBR_FPrST0 RSTi:$op), 0>;
def : InstAlias<"fsub{r|}p\t{$op, %st|st, $op}", (SUB_FPrST0 RSTi:$op), 0>;
def : InstAlias<"fdiv{|r}p\t{$op, %st|st, $op}", (DIVR_FPrST0 RSTi:$op), 0>;
def : InstAlias<"fdiv{r|}p\t{$op, %st|st, $op}", (DIV_FPrST0 RSTi:$op), 0>;

def : InstAlias<"fnstsw"     , (FNSTSW16r), 0>;

// lcall and ljmp aliases.  This seems to be an odd mapping in 64-bit mode, but
// this is compatible with what GAS does.
def : InstAlias<"lcall\t$seg, $off", (FARCALL32i i32imm:$off, i16imm:$seg), 0>, Requires<[In32BitMode]>;
def : InstAlias<"ljmp\t$seg, $off",  (FARJMP32i  i32imm:$off, i16imm:$seg), 0>, Requires<[In32BitMode]>;
def : InstAlias<"lcall\t{*}$dst",    (FARCALL32m opaquemem:$dst), 0>, Requires<[Not16BitMode]>;
def : InstAlias<"ljmp\t{*}$dst",     (FARJMP32m  opaquemem:$dst), 0>, Requires<[Not16BitMode]>;
def : InstAlias<"lcall\t$seg, $off", (FARCALL16i i16imm:$off, i16imm:$seg), 0>, Requires<[In16BitMode]>;
def : InstAlias<"ljmp\t$seg, $off",  (FARJMP16i  i16imm:$off, i16imm:$seg), 0>, Requires<[In16BitMode]>;
def : InstAlias<"lcall\t{*}$dst",    (FARCALL16m opaquemem:$dst), 0>, Requires<[In16BitMode]>;
def : InstAlias<"ljmp\t{*}$dst",     (FARJMP16m  opaquemem:$dst), 0>, Requires<[In16BitMode]>;

def : InstAlias<"jmp\t{*}$dst",      (JMP64m  i64mem:$dst), 0, "att">, Requires<[In64BitMode]>;
def : InstAlias<"jmp\t{*}$dst",      (JMP32m  i32mem:$dst), 0, "att">, Requires<[In32BitMode]>;
def : InstAlias<"jmp\t{*}$dst",      (JMP16m  i16mem:$dst), 0, "att">, Requires<[In16BitMode]>;


// "imul <imm>, B" is an alias for "imul <imm>, B, B".
def : InstAlias<"imul{w}\t{$imm, $r|$r, $imm}", (IMUL16rri  GR16:$r, GR16:$r, i16imm:$imm), 0>;
def : InstAlias<"imul{w}\t{$imm, $r|$r, $imm}", (IMUL16rri8 GR16:$r, GR16:$r, i16i8imm:$imm), 0>;
def : InstAlias<"imul{l}\t{$imm, $r|$r, $imm}", (IMUL32rri  GR32:$r, GR32:$r, i32imm:$imm), 0>;
def : InstAlias<"imul{l}\t{$imm, $r|$r, $imm}", (IMUL32rri8 GR32:$r, GR32:$r, i32i8imm:$imm), 0>;
def : InstAlias<"imul{q}\t{$imm, $r|$r, $imm}", (IMUL64rri32 GR64:$r, GR64:$r, i64i32imm:$imm), 0>;
def : InstAlias<"imul{q}\t{$imm, $r|$r, $imm}", (IMUL64rri8 GR64:$r, GR64:$r, i64i8imm:$imm), 0>;

// ins aliases. Accept the mnemonic suffix being omitted because it's implicit
// in the destination.
def : InstAlias<"ins\t{%dx, $dst|$dst, dx}", (INSB dstidx8:$dst),  0, "intel">;
def : InstAlias<"ins\t{%dx, $dst|$dst, dx}", (INSW dstidx16:$dst), 0, "intel">;
def : InstAlias<"ins\t{%dx, $dst|$dst, dx}", (INSL dstidx32:$dst), 0, "intel">;

// outs aliases. Accept the mnemonic suffix being omitted because it's implicit
// in the source.
def : InstAlias<"outs\t{$src, %dx|dx, $src}", (OUTSB srcidx8:$src),  0, "intel">;
def : InstAlias<"outs\t{$src, %dx|dx, $src}", (OUTSW srcidx16:$src), 0, "intel">;
def : InstAlias<"outs\t{$src, %dx|dx, $src}", (OUTSL srcidx32:$src), 0, "intel">;

// inb %dx -> inb %al, %dx
def : InstAlias<"inb\t{%dx|dx}", (IN8rr), 0>;
def : InstAlias<"inw\t{%dx|dx}", (IN16rr), 0>;
def : InstAlias<"inl\t{%dx|dx}", (IN32rr), 0>;
def : InstAlias<"inb\t$port", (IN8ri u8imm:$port), 0>;
def : InstAlias<"inw\t$port", (IN16ri u8imm:$port), 0>;
def : InstAlias<"inl\t$port", (IN32ri u8imm:$port), 0>;


// jmp and call aliases for lcall and ljmp.  jmp $42,$5 -> ljmp
def : InstAlias<"call\t$seg, $off",  (FARCALL16i i16imm:$off, i16imm:$seg)>, Requires<[In16BitMode]>;
def : InstAlias<"jmp\t$seg, $off",   (FARJMP16i  i16imm:$off, i16imm:$seg)>, Requires<[In16BitMode]>;
def : InstAlias<"call\t$seg, $off",  (FARCALL32i i32imm:$off, i16imm:$seg)>, Requires<[In32BitMode]>;
def : InstAlias<"jmp\t$seg, $off",   (FARJMP32i  i32imm:$off, i16imm:$seg)>, Requires<[In32BitMode]>;
def : InstAlias<"callw\t$seg, $off", (FARCALL16i i16imm:$off, i16imm:$seg)>, Requires<[Not64BitMode]>;
def : InstAlias<"jmpw\t$seg, $off",  (FARJMP16i  i16imm:$off, i16imm:$seg)>, Requires<[Not64BitMode]>;
def : InstAlias<"calll\t$seg, $off", (FARCALL32i i32imm:$off, i16imm:$seg)>, Requires<[Not64BitMode]>;
def : InstAlias<"jmpl\t$seg, $off",  (FARJMP32i  i32imm:$off, i16imm:$seg)>, Requires<[Not64BitMode]>;

// Match 'movq <largeimm>, <reg>' as an alias for movabsq.
def : InstAlias<"mov{q}\t{$imm, $reg|$reg, $imm}", (MOV64ri GR64:$reg, i64imm:$imm), 0>;

// Match 'movd GR64, MMX' as an alias for movq to be compatible with gas,
// which supports this due to an old AMD documentation bug when 64-bit mode was
// created.
def : InstAlias<"movd\t{$src, $dst|$dst, $src}",
                (MMX_MOVD64to64rr VR64:$dst, GR64:$src), 0>;
def : InstAlias<"movd\t{$src, $dst|$dst, $src}",
                (MMX_MOVD64from64rr GR64:$dst, VR64:$src), 0>;

// movsx aliases
def : InstAlias<"movsx\t{$src, $dst|$dst, $src}", (MOVSX16rr8 GR16:$dst, GR8:$src), 0, "att">;
def : InstAlias<"movsx\t{$src, $dst|$dst, $src}", (MOVSX16rm8 GR16:$dst, i8mem:$src), 0, "att">;
def : InstAlias<"movsx\t{$src, $dst|$dst, $src}", (MOVSX32rr8 GR32:$dst, GR8:$src), 0, "att">;
def : InstAlias<"movsx\t{$src, $dst|$dst, $src}", (MOVSX32rr16 GR32:$dst, GR16:$src), 0, "att">;
def : InstAlias<"movsx\t{$src, $dst|$dst, $src}", (MOVSX64rr8 GR64:$dst, GR8:$src), 0, "att">;
def : InstAlias<"movsx\t{$src, $dst|$dst, $src}", (MOVSX64rr16 GR64:$dst, GR16:$src), 0, "att">;
def : InstAlias<"movsx\t{$src, $dst|$dst, $src}", (MOVSX64rr32 GR64:$dst, GR32:$src), 0, "att">;

// movzx aliases
def : InstAlias<"movzx\t{$src, $dst|$dst, $src}", (MOVZX16rr8 GR16:$dst, GR8:$src), 0, "att">;
def : InstAlias<"movzx\t{$src, $dst|$dst, $src}", (MOVZX16rm8 GR16:$dst, i8mem:$src), 0, "att">;
def : InstAlias<"movzx\t{$src, $dst|$dst, $src}", (MOVZX32rr8 GR32:$dst, GR8:$src), 0, "att">;
def : InstAlias<"movzx\t{$src, $dst|$dst, $src}", (MOVZX32rr16 GR32:$dst, GR16:$src), 0, "att">;
def : InstAlias<"movzx\t{$src, $dst|$dst, $src}", (MOVZX64rr8 GR64:$dst, GR8:$src), 0, "att">;
def : InstAlias<"movzx\t{$src, $dst|$dst, $src}", (MOVZX64rr16 GR64:$dst, GR16:$src), 0, "att">;
// Note: No GR32->GR64 movzx form.

// outb %dx -> outb %al, %dx
def : InstAlias<"outb\t{%dx|dx}", (OUT8rr), 0>;
def : InstAlias<"outw\t{%dx|dx}", (OUT16rr), 0>;
def : InstAlias<"outl\t{%dx|dx}", (OUT32rr), 0>;
def : InstAlias<"outb\t$port", (OUT8ir u8imm:$port), 0>;
def : InstAlias<"outw\t$port", (OUT16ir u8imm:$port), 0>;
def : InstAlias<"outl\t$port", (OUT32ir u8imm:$port), 0>;

// 'sldt <mem>' can be encoded with either sldtw or sldtq with the same
// effect (both store to a 16-bit mem).  Force to sldtw to avoid ambiguity
// errors, since its encoding is the most compact.
def : InstAlias<"sldt $mem", (SLDT16m i16mem:$mem), 0>;

// shld/shrd op,op -> shld op, op, CL
def : InstAlias<"shld{w}\t{$r2, $r1|$r1, $r2}", (SHLD16rrCL GR16:$r1, GR16:$r2), 0>;
def : InstAlias<"shld{l}\t{$r2, $r1|$r1, $r2}", (SHLD32rrCL GR32:$r1, GR32:$r2), 0>;
def : InstAlias<"shld{q}\t{$r2, $r1|$r1, $r2}", (SHLD64rrCL GR64:$r1, GR64:$r2), 0>;
def : InstAlias<"shrd{w}\t{$r2, $r1|$r1, $r2}", (SHRD16rrCL GR16:$r1, GR16:$r2), 0>;
def : InstAlias<"shrd{l}\t{$r2, $r1|$r1, $r2}", (SHRD32rrCL GR32:$r1, GR32:$r2), 0>;
def : InstAlias<"shrd{q}\t{$r2, $r1|$r1, $r2}", (SHRD64rrCL GR64:$r1, GR64:$r2), 0>;

def : InstAlias<"shld{w}\t{$reg, $mem|$mem, $reg}", (SHLD16mrCL i16mem:$mem, GR16:$reg), 0>;
def : InstAlias<"shld{l}\t{$reg, $mem|$mem, $reg}", (SHLD32mrCL i32mem:$mem, GR32:$reg), 0>;
def : InstAlias<"shld{q}\t{$reg, $mem|$mem, $reg}", (SHLD64mrCL i64mem:$mem, GR64:$reg), 0>;
def : InstAlias<"shrd{w}\t{$reg, $mem|$mem, $reg}", (SHRD16mrCL i16mem:$mem, GR16:$reg), 0>;
def : InstAlias<"shrd{l}\t{$reg, $mem|$mem, $reg}", (SHRD32mrCL i32mem:$mem, GR32:$reg), 0>;
def : InstAlias<"shrd{q}\t{$reg, $mem|$mem, $reg}", (SHRD64mrCL i64mem:$mem, GR64:$reg), 0>;

// test: We accept "testX <reg>, <mem>" and "testX <mem>, <reg>" as synonyms.
def : InstAlias<"test{b}\t{$mem, $val|$val, $mem}",
                (TEST8mr  i8mem :$mem, GR8 :$val), 0>;
def : InstAlias<"test{w}\t{$mem, $val|$val, $mem}",
                (TEST16mr i16mem:$mem, GR16:$val), 0>;
def : InstAlias<"test{l}\t{$mem, $val|$val, $mem}",
                (TEST32mr i32mem:$mem, GR32:$val), 0>;
def : InstAlias<"test{q}\t{$mem, $val|$val, $mem}",
                (TEST64mr i64mem:$mem, GR64:$val), 0>;

// xchg: We accept "xchgX <reg>, <mem>" and "xchgX <mem>, <reg>" as synonyms.
def : InstAlias<"xchg{b}\t{$mem, $val|$val, $mem}",
                (XCHG8rm  GR8 :$val, i8mem :$mem), 0>;
def : InstAlias<"xchg{w}\t{$mem, $val|$val, $mem}",
                (XCHG16rm GR16:$val, i16mem:$mem), 0>;
def : InstAlias<"xchg{l}\t{$mem, $val|$val, $mem}",
                (XCHG32rm GR32:$val, i32mem:$mem), 0>;
def : InstAlias<"xchg{q}\t{$mem, $val|$val, $mem}",
                (XCHG64rm GR64:$val, i64mem:$mem), 0>;

// xchg: We accept "xchgX <reg>, %eax" and "xchgX %eax, <reg>" as synonyms.
def : InstAlias<"xchg{w}\t{%ax, $src|$src, ax}", (XCHG16ar GR16:$src), 0>;
def : InstAlias<"xchg{l}\t{%eax, $src|$src, eax}", (XCHG32ar GR32:$src), 0>;
def : InstAlias<"xchg{q}\t{%rax, $src|$src, rax}", (XCHG64ar GR64:$src), 0>;

// In 64-bit mode, xchg %eax, %eax can't be encoded with the 0x90 opcode we
// would get by default because it's defined as NOP. But xchg %eax, %eax implies
// implicit zeroing of the upper 32 bits. So alias to the longer encoding.
def : InstAlias<"xchg{l}\t{%eax, %eax|eax, eax}",
                (XCHG32rr EAX, EAX), 0>, Requires<[In64BitMode]>;

// xchg %rax, %rax is a nop in x86-64 and can be encoded as such. Without this
// we emit an unneeded REX.w prefix.
def : InstAlias<"xchg{q}\t{%rax, %rax|rax, rax}", (NOOP), 0>;

// These aliases exist to get the parser to prioritize matching 8-bit
// immediate encodings over matching the implicit ax/eax/rax encodings. By
// explicitly mentioning the A register here, these entries will be ordered
// first due to the more explicit immediate type.
def : InstAlias<"adc{w}\t{$imm, %ax|ax, $imm}", (ADC16ri8 AX, i16i8imm:$imm), 0>;
def : InstAlias<"add{w}\t{$imm, %ax|ax, $imm}", (ADD16ri8 AX, i16i8imm:$imm), 0>;
def : InstAlias<"and{w}\t{$imm, %ax|ax, $imm}", (AND16ri8 AX, i16i8imm:$imm), 0>;
def : InstAlias<"cmp{w}\t{$imm, %ax|ax, $imm}", (CMP16ri8 AX, i16i8imm:$imm), 0>;
def : InstAlias<"or{w}\t{$imm, %ax|ax, $imm}",  (OR16ri8 AX,  i16i8imm:$imm), 0>;
def : InstAlias<"sbb{w}\t{$imm, %ax|ax, $imm}", (SBB16ri8 AX, i16i8imm:$imm), 0>;
def : InstAlias<"sub{w}\t{$imm, %ax|ax, $imm}", (SUB16ri8 AX, i16i8imm:$imm), 0>;
def : InstAlias<"xor{w}\t{$imm, %ax|ax, $imm}", (XOR16ri8 AX, i16i8imm:$imm), 0>;

def : InstAlias<"adc{l}\t{$imm, %eax|eax, $imm}", (ADC32ri8 EAX, i32i8imm:$imm), 0>;
def : InstAlias<"add{l}\t{$imm, %eax|eax, $imm}", (ADD32ri8 EAX, i32i8imm:$imm), 0>;
def : InstAlias<"and{l}\t{$imm, %eax|eax, $imm}", (AND32ri8 EAX, i32i8imm:$imm), 0>;
def : InstAlias<"cmp{l}\t{$imm, %eax|eax, $imm}", (CMP32ri8 EAX, i32i8imm:$imm), 0>;
def : InstAlias<"or{l}\t{$imm, %eax|eax, $imm}",  (OR32ri8 EAX,  i32i8imm:$imm), 0>;
def : InstAlias<"sbb{l}\t{$imm, %eax|eax, $imm}", (SBB32ri8 EAX, i32i8imm:$imm), 0>;
def : InstAlias<"sub{l}\t{$imm, %eax|eax, $imm}", (SUB32ri8 EAX, i32i8imm:$imm), 0>;
def : InstAlias<"xor{l}\t{$imm, %eax|eax, $imm}", (XOR32ri8 EAX, i32i8imm:$imm), 0>;

def : InstAlias<"adc{q}\t{$imm, %rax|rax, $imm}", (ADC64ri8 RAX, i64i8imm:$imm), 0>;
def : InstAlias<"add{q}\t{$imm, %rax|rax, $imm}", (ADD64ri8 RAX, i64i8imm:$imm), 0>;
def : InstAlias<"and{q}\t{$imm, %rax|rax, $imm}", (AND64ri8 RAX, i64i8imm:$imm), 0>;
def : InstAlias<"cmp{q}\t{$imm, %rax|rax, $imm}", (CMP64ri8 RAX, i64i8imm:$imm), 0>;
def : InstAlias<"or{q}\t{$imm, %rax|rax, $imm}",  (OR64ri8 RAX,  i64i8imm:$imm), 0>;
def : InstAlias<"sbb{q}\t{$imm, %rax|rax, $imm}", (SBB64ri8 RAX, i64i8imm:$imm), 0>;
def : InstAlias<"sub{q}\t{$imm, %rax|rax, $imm}", (SUB64ri8 RAX, i64i8imm:$imm), 0>;
def : InstAlias<"xor{q}\t{$imm, %rax|rax, $imm}", (XOR64ri8 RAX, i64i8imm:$imm), 0>;

//  MMX instr alia
def : InstAlias<"movq.s\t{$src, $dst|$dst, $src}",
                (MMX_MOVQ64rr_REV VR64:$dst, VR64:$src), 0>;

//  CMOV SETCC SETZUCC Aliases
multiclass CMOV_SETCC_Aliases<string Cond, int CC> {
  def : InstAlias<"cmov"#Cond#"{w}\t{$src, $dst|$dst, $src}",
                  (CMOV16rr GR16:$dst, GR16:$src, CC), 0>;
  def : InstAlias<"cmov"#Cond#"{w}\t{$src, $dst|$dst, $src}",
                  (CMOV16rm GR16:$dst, i16mem:$src, CC), 0>;
  def : InstAlias<"cmov"#Cond#"{l}\t{$src, $dst|$dst, $src}",
                  (CMOV32rr GR32:$dst, GR32:$src, CC), 0>;
  def : InstAlias<"cmov"#Cond#"{l}\t{$src, $dst|$dst, $src}",
                  (CMOV32rm GR32:$dst, i32mem:$src, CC), 0>;
  def : InstAlias<"cmov"#Cond#"{q}\t{$src, $dst|$dst, $src}",
                  (CMOV64rr GR64:$dst, GR64:$src, CC), 0>;
  def : InstAlias<"cmov"#Cond#"{q}\t{$src, $dst|$dst, $src}",
                  (CMOV64rm GR64:$dst, i64mem:$src, CC), 0>;
let Predicates = [In64BitMode] in {
  def : InstAlias<"cmov"#Cond#"{w}\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                  (CMOV16rr_ND GR16:$dst, GR16:$src1, GR16:$src2, CC), 0>;
  def : InstAlias<"cmov"#Cond#"{w}\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                  (CMOV16rm_ND GR16:$dst, GR16:$src1, i16mem:$src2, CC), 0>;
  def : InstAlias<"cmov"#Cond#"{l}\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                  (CMOV32rr_ND GR32:$dst, GR32:$src1, GR32:$src2, CC), 0>;
  def : InstAlias<"cmov"#Cond#"{l}\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                  (CMOV32rm_ND GR32:$dst, GR32:$src1, i32mem:$src2, CC), 0>;
  def : InstAlias<"cmov"#Cond#"{q}\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                  (CMOV64rr_ND GR64:$dst, GR64:$src1, GR64:$src2, CC), 0>;
  def : InstAlias<"cmov"#Cond#"{q}\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                  (CMOV64rm_ND GR64:$dst, GR64:$src1, i64mem:$src2, CC), 0>;

  def : InstAlias<"setzu"#Cond#"\t$dst", (SETZUCCr GR8:$dst, CC), 0>;
  def : InstAlias<"setzu"#Cond#"\t$dst", (SETZUCCm i8mem:$dst, CC), 0>;
  def : InstAlias<"set"#Cond#"\t$dst", (SETCCr_EVEX GR8:$dst, CC), 0>;
  def : InstAlias<"set"#Cond#"\t$dst", (SETCCm_EVEX i8mem:$dst, CC), 0>;
}
  def : InstAlias<"set"#Cond#"\t$dst", (SETCCr GR8:$dst, CC), 0>;
  def : InstAlias<"set"#Cond#"\t$dst", (SETCCm i8mem:$dst, CC), 0>;
}

defm : CMOV_SETCC_Aliases<"o" ,  0>;
defm : CMOV_SETCC_Aliases<"no",  1>;
defm : CMOV_SETCC_Aliases<"b" ,  2>;
defm : CMOV_SETCC_Aliases<"ae",  3>;
defm : CMOV_SETCC_Aliases<"e" ,  4>;
defm : CMOV_SETCC_Aliases<"ne",  5>;
defm : CMOV_SETCC_Aliases<"be",  6>;
defm : CMOV_SETCC_Aliases<"a" ,  7>;
defm : CMOV_SETCC_Aliases<"s" ,  8>;
defm : CMOV_SETCC_Aliases<"ns",  9>;
defm : CMOV_SETCC_Aliases<"p" , 10>;
defm : CMOV_SETCC_Aliases<"np", 11>;
defm : CMOV_SETCC_Aliases<"l" , 12>;
defm : CMOV_SETCC_Aliases<"ge", 13>;
defm : CMOV_SETCC_Aliases<"le", 14>;
defm : CMOV_SETCC_Aliases<"g" , 15>;

multiclass CFCMOV_Aliases<string Cond, int CC> {
let Predicates = [In64BitMode] in {
  def : InstAlias<"cfcmov"#Cond#"{w}\t{$src, $dst|$dst, $src}",
                  (CFCMOV16rr GR16:$dst, GR16:$src, CC), 0>;
  def : InstAlias<"cfcmov"#Cond#"{l}\t{$src, $dst|$dst, $src}",
                  (CFCMOV32rr GR32:$dst, GR32:$src, CC), 0>;
  def : InstAlias<"cfcmov"#Cond#"{q}\t{$src, $dst|$dst, $src}",
                  (CFCMOV64rr GR64:$dst, GR64:$src, CC), 0>;
  def : InstAlias<"cfcmov"#Cond#"{w}\t{$src, $dst|$dst, $src}",
                  (CFCMOV16rm GR16:$dst, i16mem:$src, CC), 0>;
  def : InstAlias<"cfcmov"#Cond#"{l}\t{$src, $dst|$dst, $src}",
                  (CFCMOV32rm GR32:$dst, i32mem:$src, CC), 0>;
  def : InstAlias<"cfcmov"#Cond#"{q}\t{$src, $dst|$dst, $src}",
                  (CFCMOV64rm GR64:$dst, i64mem:$src, CC), 0>;
  def : InstAlias<"cfcmov"#Cond#"{w}\t{$src, $dst|$dst, $src}",
                  (CFCMOV16mr i16mem:$dst, GR16:$src, CC), 0>;
  def : InstAlias<"cfcmov"#Cond#"{l}\t{$src, $dst|$dst, $src}",
                  (CFCMOV32mr i32mem:$dst, GR32:$src, CC), 0>;
  def : InstAlias<"cfcmov"#Cond#"{q}\t{$src, $dst|$dst, $src}",
                  (CFCMOV64mr i64mem:$dst, GR64:$src, CC), 0>;
  def : InstAlias<"cfcmov"#Cond#"{w}\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                  (CFCMOV16rr_ND GR16:$dst, GR16:$src1, GR16:$src2, CC), 0>;
  def : InstAlias<"cfcmov"#Cond#"{l}\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                  (CFCMOV32rr_ND GR32:$dst, GR32:$src1, GR32:$src2, CC), 0>;
  def : InstAlias<"cfcmov"#Cond#"{q}\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                  (CFCMOV64rr_ND GR64:$dst, GR64:$src1, GR64:$src2, CC), 0>;
  def : InstAlias<"cfcmov"#Cond#"{w}\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                  (CFCMOV16rm_ND GR16:$dst, GR16:$src1, i16mem:$src2, CC), 0>;
  def : InstAlias<"cfcmov"#Cond#"{l}\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                  (CFCMOV32rm_ND GR32:$dst, GR32:$src1, i32mem:$src2, CC), 0>;
  def : InstAlias<"cfcmov"#Cond#"{q}\t{$src2, $src1, $dst|$dst, $src1, $src2}",
                  (CFCMOV64rm_ND GR64:$dst, GR64:$src1, i64mem:$src2, CC), 0>;
}
}
defm : CFCMOV_Aliases<"o" ,  0>;
defm : CFCMOV_Aliases<"no",  1>;
defm : CFCMOV_Aliases<"b" ,  2>;
defm : CFCMOV_Aliases<"ae",  3>;
defm : CFCMOV_Aliases<"e" ,  4>;
defm : CFCMOV_Aliases<"ne",  5>;
defm : CFCMOV_Aliases<"be",  6>;
defm : CFCMOV_Aliases<"a" ,  7>;
defm : CFCMOV_Aliases<"s" ,  8>;
defm : CFCMOV_Aliases<"ns",  9>;
defm : CFCMOV_Aliases<"p" , 10>;
defm : CFCMOV_Aliases<"np", 11>;
defm : CFCMOV_Aliases<"l" , 12>;
defm : CFCMOV_Aliases<"ge", 13>;
defm : CFCMOV_Aliases<"le", 14>;
defm : CFCMOV_Aliases<"g" , 15>;

// Condition dump instructions Alias
def : InstAlias<"jo\t$dst",  (JCC_1 brtarget8:$dst,  0), 0>;
def : InstAlias<"jno\t$dst", (JCC_1 brtarget8:$dst,  1), 0>;
def : InstAlias<"jb\t$dst",  (JCC_1 brtarget8:$dst,  2), 0>;
def : InstAlias<"jae\t$dst", (JCC_1 brtarget8:$dst,  3), 0>;
def : InstAlias<"je\t$dst",  (JCC_1 brtarget8:$dst,  4), 0>;
def : InstAlias<"jne\t$dst", (JCC_1 brtarget8:$dst,  5), 0>;
def : InstAlias<"jbe\t$dst", (JCC_1 brtarget8:$dst,  6), 0>;
def : InstAlias<"ja\t$dst",  (JCC_1 brtarget8:$dst,  7), 0>;
def : InstAlias<"js\t$dst",  (JCC_1 brtarget8:$dst,  8), 0>;
def : InstAlias<"jns\t$dst", (JCC_1 brtarget8:$dst,  9), 0>;
def : InstAlias<"jp\t$dst",  (JCC_1 brtarget8:$dst, 10), 0>;
def : InstAlias<"jnp\t$dst", (JCC_1 brtarget8:$dst, 11), 0>;
def : InstAlias<"jl\t$dst",  (JCC_1 brtarget8:$dst, 12), 0>;
def : InstAlias<"jge\t$dst", (JCC_1 brtarget8:$dst, 13), 0>;
def : InstAlias<"jle\t$dst", (JCC_1 brtarget8:$dst, 14), 0>;
def : InstAlias<"jg\t$dst",  (JCC_1 brtarget8:$dst, 15), 0>;

// SVM instructions Alias
def : InstAlias<"skinit\t{%eax|eax}", (SKINIT), 0>;
def : InstAlias<"vmrun\t{%eax|eax}", (VMRUN32), 0>, Requires<[Not64BitMode]>;
def : InstAlias<"vmrun\t{%rax|rax}", (VMRUN64), 0>, Requires<[In64BitMode]>;
def : InstAlias<"vmload\t{%eax|eax}", (VMLOAD32), 0>, Requires<[Not64BitMode]>;
def : InstAlias<"vmload\t{%rax|rax}", (VMLOAD64), 0>, Requires<[In64BitMode]>;
def : InstAlias<"vmsave\t{%eax|eax}", (VMSAVE32), 0>, Requires<[Not64BitMode]>;
def : InstAlias<"vmsave\t{%rax|rax}", (VMSAVE64), 0>, Requires<[In64BitMode]>;
def : InstAlias<"invlpga\t{%eax, %ecx|eax, ecx}", (INVLPGA32), 0>, Requires<[Not64BitMode]>;
def : InstAlias<"invlpga\t{%rax, %ecx|rax, ecx}", (INVLPGA64), 0>, Requires<[In64BitMode]>;

// Aliases with explicit %xmm0
def : InstAlias<"sha256rnds2\t{$src2, $dst|$dst, $src2}",
                (SHA256RNDS2rr VR128:$dst, VR128:$src2), 0>;
def : InstAlias<"sha256rnds2\t{$src2, $dst|$dst, $src2}",
                (SHA256RNDS2rm VR128:$dst, i128mem:$src2), 0>;

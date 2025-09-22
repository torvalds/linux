//===- X86InstrCompiler.td - Compiler Pseudos and Patterns -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the various pseudo instructions used by the compiler,
// as well as Pat patterns used during instruction selection.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Pattern Matching Support

def GetLo32XForm : SDNodeXForm<imm, [{
  // Transformation function: get the low 32 bits.
  return getI32Imm((uint32_t)N->getZExtValue(), SDLoc(N));
}]>;


//===----------------------------------------------------------------------===//
// Random Pseudo Instructions.

// PIC base construction.  This expands to code that looks like this:
//     call  $next_inst
//     popl %destreg"
let hasSideEffects = 0, isNotDuplicable = 1, Uses = [ESP, SSP],
    SchedRW = [WriteJump] in
  def MOVPC32r : Ii32<0xE8, Pseudo, (outs GR32:$reg), (ins i32imm:$label),
                      "", []>;

// ADJCALLSTACKDOWN/UP implicitly use/def ESP because they may be expanded into
// a stack adjustment and the codegen must know that they may modify the stack
// pointer before prolog-epilog rewriting occurs.
// Pessimistically assume ADJCALLSTACKDOWN / ADJCALLSTACKUP will become
// sub / add which can clobber EFLAGS.
let Defs = [ESP, EFLAGS, SSP], Uses = [ESP, SSP], SchedRW = [WriteALU] in {
def ADJCALLSTACKDOWN32 : I<0, Pseudo, (outs),
                           (ins i32imm:$amt1, i32imm:$amt2, i32imm:$amt3),
                           "#ADJCALLSTACKDOWN", []>, Requires<[NotLP64]>;
def ADJCALLSTACKUP32   : I<0, Pseudo, (outs), (ins i32imm:$amt1, i32imm:$amt2),
                           "#ADJCALLSTACKUP",
                           [(X86callseq_end timm:$amt1, timm:$amt2)]>,
                           Requires<[NotLP64]>;
}
def : Pat<(X86callseq_start timm:$amt1, timm:$amt2),
       (ADJCALLSTACKDOWN32 i32imm:$amt1, i32imm:$amt2, 0)>, Requires<[NotLP64]>;


// ADJCALLSTACKDOWN/UP implicitly use/def RSP because they may be expanded into
// a stack adjustment and the codegen must know that they may modify the stack
// pointer before prolog-epilog rewriting occurs.
// Pessimistically assume ADJCALLSTACKDOWN / ADJCALLSTACKUP will become
// sub / add which can clobber EFLAGS.
let Defs = [RSP, EFLAGS, SSP], Uses = [RSP, SSP], SchedRW = [WriteALU] in {
def ADJCALLSTACKDOWN64 : I<0, Pseudo, (outs),
                           (ins i32imm:$amt1, i32imm:$amt2, i32imm:$amt3),
                           "#ADJCALLSTACKDOWN", []>, Requires<[IsLP64]>;
def ADJCALLSTACKUP64   : I<0, Pseudo, (outs), (ins i32imm:$amt1, i32imm:$amt2),
                           "#ADJCALLSTACKUP",
                           [(X86callseq_end timm:$amt1, timm:$amt2)]>,
                           Requires<[IsLP64]>;
}
def : Pat<(X86callseq_start timm:$amt1, timm:$amt2),
        (ADJCALLSTACKDOWN64 i32imm:$amt1, i32imm:$amt2, 0)>, Requires<[IsLP64]>;

let SchedRW = [WriteSystem] in {

// x86-64 va_start lowering magic.
let hasSideEffects = 1, mayStore = 1, Defs = [EFLAGS] in {
def VASTART_SAVE_XMM_REGS : I<0, Pseudo,
                              (outs),
                              (ins GR8:$al, i8mem:$regsavefi, variable_ops),
                              "#VASTART_SAVE_XMM_REGS $al, $regsavefi",
                              [(X86vastart_save_xmm_regs GR8:$al, addr:$regsavefi),
                               (implicit EFLAGS)]>;
}

let usesCustomInserter = 1, Defs = [EFLAGS] in {
// The VAARG_64 and VAARG_X32 pseudo-instructions take the address of the
// va_list, and place the address of the next argument into a register.
let Defs = [EFLAGS] in {
def VAARG_64 : I<0, Pseudo,
                 (outs GR64:$dst),
                 (ins i8mem:$ap, i32imm:$size, i8imm:$mode, i32imm:$align),
                 "#VAARG_64 $dst, $ap, $size, $mode, $align",
                 [(set GR64:$dst,
                    (X86vaarg64 addr:$ap, timm:$size, timm:$mode, timm:$align)),
                  (implicit EFLAGS)]>, Requires<[In64BitMode, IsLP64]>;
def VAARG_X32 : I<0, Pseudo,
                 (outs GR32:$dst),
                 (ins i8mem:$ap, i32imm:$size, i8imm:$mode, i32imm:$align),
                 "#VAARG_X32 $dst, $ap, $size, $mode, $align",
                 [(set GR32:$dst,
                    (X86vaargx32 addr:$ap, timm:$size, timm:$mode, timm:$align)),
                  (implicit EFLAGS)]>, Requires<[In64BitMode, NotLP64]>;
}

// When using segmented stacks these are lowered into instructions which first
// check if the current stacklet has enough free memory. If it does, memory is
// allocated by bumping the stack pointer. Otherwise memory is allocated from
// the heap.

let Defs = [EAX, ESP, EFLAGS], Uses = [ESP] in
def SEG_ALLOCA_32 : I<0, Pseudo, (outs GR32:$dst), (ins GR32:$size),
                      "# variable sized alloca for segmented stacks",
                      [(set GR32:$dst,
                         (X86SegAlloca GR32:$size))]>,
                    Requires<[NotLP64]>;

let Defs = [RAX, RSP, EFLAGS], Uses = [RSP] in
def SEG_ALLOCA_64 : I<0, Pseudo, (outs GR64:$dst), (ins GR64:$size),
                      "# variable sized alloca for segmented stacks",
                      [(set GR64:$dst,
                         (X86SegAlloca GR64:$size))]>,
                    Requires<[In64BitMode]>;

// To protect against stack clash, dynamic allocation should perform a memory
// probe at each page.

let Defs = [EAX, ESP, EFLAGS], Uses = [ESP] in
def PROBED_ALLOCA_32 : I<0, Pseudo, (outs GR32:$dst), (ins GR32:$size),
                      "# variable sized alloca with probing",
                      [(set GR32:$dst,
                         (X86ProbedAlloca GR32:$size))]>,
                    Requires<[NotLP64]>;

let Defs = [RAX, RSP, EFLAGS], Uses = [RSP] in
def PROBED_ALLOCA_64 : I<0, Pseudo, (outs GR64:$dst), (ins GR64:$size),
                      "# variable sized alloca with probing",
                      [(set GR64:$dst,
                         (X86ProbedAlloca GR64:$size))]>,
                    Requires<[In64BitMode]>;
}

let hasNoSchedulingInfo = 1 in
def STACKALLOC_W_PROBING : I<0, Pseudo, (outs), (ins i64imm:$stacksize),
                             "# fixed size alloca with probing",
                             []>;

// Dynamic stack allocation yields a _chkstk or _alloca call for all Windows
// targets.  These calls are needed to probe the stack when allocating more than
// 4k bytes in one go. Touching the stack at 4K increments is necessary to
// ensure that the guard pages used by the OS virtual memory manager are
// allocated in correct sequence.
// The main point of having separate instruction are extra unmodelled effects
// (compared to ordinary calls) like stack pointer change.

let Defs = [EAX, ESP, EFLAGS], Uses = [ESP] in
def DYN_ALLOCA_32 : I<0, Pseudo, (outs), (ins GR32:$size),
                     "# dynamic stack allocation",
                     [(X86DynAlloca GR32:$size)]>,
                     Requires<[NotLP64]>;

let Defs = [RAX, RSP, EFLAGS], Uses = [RSP] in
def DYN_ALLOCA_64 : I<0, Pseudo, (outs), (ins GR64:$size),
                     "# dynamic stack allocation",
                     [(X86DynAlloca GR64:$size)]>,
                     Requires<[In64BitMode]>;
} // SchedRW

// These instructions XOR the frame pointer into a GPR. They are used in some
// stack protection schemes. These are post-RA pseudos because we only know the
// frame register after register allocation.
let Constraints = "$src = $dst", isMoveImm = 1, isPseudo = 1, Defs = [EFLAGS] in {
  def XOR32_FP : I<0, Pseudo, (outs GR32:$dst), (ins GR32:$src),
                  "xorl\t$$FP, $src", []>,
                  Requires<[NotLP64]>, Sched<[WriteALU]>;
  def XOR64_FP : I<0, Pseudo, (outs GR64:$dst), (ins GR64:$src),
                  "xorq\t$$FP $src", []>,
                  Requires<[In64BitMode]>, Sched<[WriteALU]>;
}

//===----------------------------------------------------------------------===//
// EH Pseudo Instructions
//
let SchedRW = [WriteSystem] in {
let isTerminator = 1, isReturn = 1, isBarrier = 1,
    hasCtrlDep = 1, isCodeGenOnly = 1 in {
def EH_RETURN   : I<0xC3, RawFrm, (outs), (ins GR32:$addr),
                    "ret\t#eh_return, addr: $addr",
                    [(X86ehret GR32:$addr)]>, Sched<[WriteJumpLd]>;

}

let isTerminator = 1, isReturn = 1, isBarrier = 1,
    hasCtrlDep = 1, isCodeGenOnly = 1 in {
def EH_RETURN64   : I<0xC3, RawFrm, (outs), (ins GR64:$addr),
                     "ret\t#eh_return, addr: $addr",
                     [(X86ehret GR64:$addr)]>, Sched<[WriteJumpLd]>;

}

let isTerminator = 1, hasSideEffects = 1, isBarrier = 1, hasCtrlDep = 1,
    isCodeGenOnly = 1, isReturn = 1, isEHScopeReturn = 1 in {
  def CLEANUPRET : I<0, Pseudo, (outs), (ins), "# CLEANUPRET",
                     [(cleanupret bb)]>;

  // CATCHRET needs a custom inserter for SEH.
  let usesCustomInserter = 1 in
    def CATCHRET : I<0, Pseudo, (outs), (ins brtarget32:$dst, brtarget32:$from),
                     "# CATCHRET",
                     [(catchret bb:$dst, bb:$from)]>;
}

let hasSideEffects = 1, isBarrier = 1, isCodeGenOnly = 1,
    usesCustomInserter = 1 in {
  def EH_SjLj_SetJmp32  : I<0, Pseudo, (outs GR32:$dst), (ins i32mem:$buf),
                            "#EH_SJLJ_SETJMP32",
                            [(set GR32:$dst, (X86eh_sjlj_setjmp addr:$buf))]>,
                          Requires<[Not64BitMode]>;
  def EH_SjLj_SetJmp64  : I<0, Pseudo, (outs GR32:$dst), (ins i64mem:$buf),
                            "#EH_SJLJ_SETJMP64",
                            [(set GR32:$dst, (X86eh_sjlj_setjmp addr:$buf))]>,
                          Requires<[In64BitMode]>;
  let isTerminator = 1 in {
  def EH_SjLj_LongJmp32 : I<0, Pseudo, (outs), (ins i32mem:$buf),
                            "#EH_SJLJ_LONGJMP32",
                            [(X86eh_sjlj_longjmp addr:$buf)]>,
                          Requires<[Not64BitMode]>;
  def EH_SjLj_LongJmp64 : I<0, Pseudo, (outs), (ins i64mem:$buf),
                            "#EH_SJLJ_LONGJMP64",
                            [(X86eh_sjlj_longjmp addr:$buf)]>,
                          Requires<[In64BitMode]>;
  }
}

let isBranch = 1, isTerminator = 1, isCodeGenOnly = 1 in {
  def EH_SjLj_Setup : I<0, Pseudo, (outs), (ins brtarget:$dst),
                        "#EH_SjLj_Setup\t$dst", []>;
}
} // SchedRW

//===----------------------------------------------------------------------===//
// Pseudo instructions used by unwind info.
//
let isPseudo = 1, SchedRW = [WriteSystem] in {
  def SEH_PushReg : I<0, Pseudo, (outs), (ins i32imm:$reg),
                            "#SEH_PushReg $reg", []>;
  def SEH_SaveReg : I<0, Pseudo, (outs), (ins i32imm:$reg, i32imm:$dst),
                            "#SEH_SaveReg $reg, $dst", []>;
  def SEH_SaveXMM : I<0, Pseudo, (outs), (ins i32imm:$reg, i32imm:$dst),
                            "#SEH_SaveXMM $reg, $dst", []>;
  def SEH_StackAlloc : I<0, Pseudo, (outs), (ins i32imm:$size),
                            "#SEH_StackAlloc $size", []>;
  def SEH_StackAlign : I<0, Pseudo, (outs), (ins i32imm:$align),
                            "#SEH_StackAlign $align", []>;
  def SEH_SetFrame : I<0, Pseudo, (outs), (ins i32imm:$reg, i32imm:$offset),
                            "#SEH_SetFrame $reg, $offset", []>;
  def SEH_PushFrame : I<0, Pseudo, (outs), (ins i1imm:$mode),
                            "#SEH_PushFrame $mode", []>;
  def SEH_EndPrologue : I<0, Pseudo, (outs), (ins),
                            "#SEH_EndPrologue", []>;
  def SEH_Epilogue : I<0, Pseudo, (outs), (ins),
                            "#SEH_Epilogue", []>;
}

//===----------------------------------------------------------------------===//
// Pseudo instructions used by KCFI.
//===----------------------------------------------------------------------===//
let
  Defs = [R10, R11, EFLAGS] in {
def KCFI_CHECK : PseudoI<
  (outs), (ins GR64:$ptr, i32imm:$type), []>, Sched<[]>;
}

//===----------------------------------------------------------------------===//
// Pseudo instructions used by address sanitizer.
//===----------------------------------------------------------------------===//
let
  Defs = [R10, R11, EFLAGS] in {
def ASAN_CHECK_MEMACCESS : PseudoI<
  (outs), (ins GR64PLTSafe:$addr, i32imm:$accessinfo),
  [(int_asan_check_memaccess GR64PLTSafe:$addr, (i32 timm:$accessinfo))]>,
  Sched<[]>;
}

//===----------------------------------------------------------------------===//
// Pseudo instructions used by segmented stacks.
//

// This is lowered into a RET instruction by MCInstLower.  We need
// this so that we don't have to have a MachineBasicBlock which ends
// with a RET and also has successors.
let isPseudo = 1, SchedRW = [WriteJumpLd] in {
def MORESTACK_RET: I<0, Pseudo, (outs), (ins), "", []>;

// This instruction is lowered to a RET followed by a MOV.  The two
// instructions are not generated on a higher level since then the
// verifier sees a MachineBasicBlock ending with a non-terminator.
def MORESTACK_RET_RESTORE_R10 : I<0, Pseudo, (outs), (ins), "", []>;
}

//===----------------------------------------------------------------------===//
// Pseudo instruction used by retguard

// This is lowered to a JE 2; INT3; INT3. Prior to this pseudo should be a
// compare instruction to ensure the retguard cookie is correct.
// We use a pseudo here in order to avoid splitting the BB just before the return.
// Splitting the BB and inserting a JE_1 over a new INT3 BB occasionally
// resulted in incorrect code when a value from a byte register (CL) was
// used as a return value. When emitted as a split BB, the single byte
// register would sometimes be widened to 4 bytes, which would corrupt
// the return value (ie mov %ecx, %eax instead of mov %cl, %al).
let isCodeGenOnly = 1, hasNoSchedulingInfo = 1, Uses = [EFLAGS] in {
def RETGUARD_JMP_TRAP: I<0, Pseudo, (outs), (ins), "", []>;
}

let isCodeGenOnly = 1, hasNoSchedulingInfo = 1 in {
def JMP_TRAP: I<0, Pseudo, (outs), (ins), "", []>;
}

//===----------------------------------------------------------------------===//
// Alias Instructions
//===----------------------------------------------------------------------===//

// Alias instruction mapping movr0 to xor.
// FIXME: remove when we can teach regalloc that xor reg, reg is ok.
let Defs = [EFLAGS], isReMaterializable = 1, isAsCheapAsAMove = 1,
    isPseudo = 1, isMoveImm = 1, AddedComplexity = 10 in
def MOV32r0  : I<0, Pseudo, (outs GR32:$dst), (ins), "",
                 [(set GR32:$dst, 0)]>, Sched<[WriteZero]>;

// Other widths can also make use of the 32-bit xor, which may have a smaller
// encoding and avoid partial register updates.
let AddedComplexity = 10 in {
def : Pat<(i8 0), (EXTRACT_SUBREG (MOV32r0), sub_8bit)>;
def : Pat<(i16 0), (EXTRACT_SUBREG (MOV32r0), sub_16bit)>;
def : Pat<(i64 0), (SUBREG_TO_REG (i64 0), (MOV32r0), sub_32bit)>;
}

let Predicates = [OptForSize, Not64BitMode],
    AddedComplexity = 10 in {
  let SchedRW = [WriteALU] in {
  // Pseudo instructions for materializing 1 and -1 using XOR+INC/DEC,
  // which only require 3 bytes compared to MOV32ri which requires 5.
  let Defs = [EFLAGS], isReMaterializable = 1, isPseudo = 1 in {
    def MOV32r1 : I<0, Pseudo, (outs GR32:$dst), (ins), "",
                        [(set GR32:$dst, 1)]>;
    def MOV32r_1 : I<0, Pseudo, (outs GR32:$dst), (ins), "",
                        [(set GR32:$dst, -1)]>;
  }
  } // SchedRW

  // MOV16ri is 4 bytes, so the instructions above are smaller.
  def : Pat<(i16 1), (EXTRACT_SUBREG (MOV32r1), sub_16bit)>;
  def : Pat<(i16 -1), (EXTRACT_SUBREG (MOV32r_1), sub_16bit)>;
}

let isReMaterializable = 1, isPseudo = 1, AddedComplexity = 5,
    SchedRW = [WriteALU] in {
// AddedComplexity higher than MOV64ri but lower than MOV32r0 and MOV32r1.
def MOV32ImmSExti8 : I<0, Pseudo, (outs GR32:$dst), (ins i32i8imm:$src), "",
                       [(set GR32:$dst, i32immSExt8:$src)]>,
                       Requires<[OptForMinSize, NotWin64WithoutFP]>;
def MOV64ImmSExti8 : I<0, Pseudo, (outs GR64:$dst), (ins i64i8imm:$src), "",
                       [(set GR64:$dst, i64immSExt8:$src)]>,
                       Requires<[OptForMinSize, NotWin64WithoutFP]>;
}

// Materialize i64 constant where top 32-bits are zero. This could theoretically
// use MOV32ri with a SUBREG_TO_REG to represent the zero-extension, however
// that would make it more difficult to rematerialize.
let AddedComplexity = 1, isReMaterializable = 1, isAsCheapAsAMove = 1,
    isPseudo = 1, SchedRW = [WriteMove] in
def MOV32ri64 : I<0, Pseudo, (outs GR64:$dst), (ins i64i32imm:$src), "",
                  [(set GR64:$dst, i64immZExt32:$src)]>;

// This 64-bit pseudo-move can also be used for labels in the x86-64 small code
// model.
def mov64imm32 : ComplexPattern<i64, 1, "selectMOV64Imm32", [X86Wrapper]>;
def : Pat<(i64 mov64imm32:$src), (MOV32ri64 mov64imm32:$src)>;

// Use sbb to materialize carry bit.
let Uses = [EFLAGS], Defs = [EFLAGS], isPseudo = 1, SchedRW = [WriteADC],
    hasSideEffects = 0 in {
// FIXME: These are pseudo ops that should be replaced with Pat<> patterns.
// However, Pat<> can't replicate the destination reg into the inputs of the
// result.
def SETB_C32r : I<0, Pseudo, (outs GR32:$dst), (ins), "", []>;
def SETB_C64r : I<0, Pseudo, (outs GR64:$dst), (ins), "", []>;
} // isCodeGenOnly

//===----------------------------------------------------------------------===//
// String Pseudo Instructions
//
let SchedRW = [WriteMicrocoded] in {
let Defs = [ECX,EDI,ESI], Uses = [ECX,EDI,ESI], isCodeGenOnly = 1 in {
def REP_MOVSB_32 : I<0xA4, RawFrm, (outs), (ins),
                    "{rep;movsb (%esi), %es:(%edi)|rep movsb es:[edi], [esi]}",
                    [(X86rep_movs i8)]>, REP, AdSize32,
                   Requires<[NotLP64]>;
def REP_MOVSW_32 : I<0xA5, RawFrm, (outs), (ins),
                    "{rep;movsw (%esi), %es:(%edi)|rep movsw es:[edi], [esi]}",
                    [(X86rep_movs i16)]>, REP, AdSize32, OpSize16,
                   Requires<[NotLP64]>;
def REP_MOVSD_32 : I<0xA5, RawFrm, (outs), (ins),
                    "{rep;movsl (%esi), %es:(%edi)|rep movsd es:[edi], [esi]}",
                    [(X86rep_movs i32)]>, REP, AdSize32, OpSize32,
                   Requires<[NotLP64]>;
def REP_MOVSQ_32 : RI<0xA5, RawFrm, (outs), (ins),
                    "{rep;movsq (%esi), %es:(%edi)|rep movsq es:[edi], [esi]}",
                    [(X86rep_movs i64)]>, REP, AdSize32,
                   Requires<[NotLP64, In64BitMode]>;
}

let Defs = [RCX,RDI,RSI], Uses = [RCX,RDI,RSI], isCodeGenOnly = 1 in {
def REP_MOVSB_64 : I<0xA4, RawFrm, (outs), (ins),
                    "{rep;movsb (%rsi), %es:(%rdi)|rep movsb es:[rdi], [rsi]}",
                    [(X86rep_movs i8)]>, REP, AdSize64,
                   Requires<[IsLP64]>;
def REP_MOVSW_64 : I<0xA5, RawFrm, (outs), (ins),
                    "{rep;movsw (%rsi), %es:(%rdi)|rep movsw es:[rdi], [rsi]}",
                    [(X86rep_movs i16)]>, REP, AdSize64, OpSize16,
                   Requires<[IsLP64]>;
def REP_MOVSD_64 : I<0xA5, RawFrm, (outs), (ins),
                    "{rep;movsl (%rsi), %es:(%rdi)|rep movsdi es:[rdi], [rsi]}",
                    [(X86rep_movs i32)]>, REP, AdSize64, OpSize32,
                   Requires<[IsLP64]>;
def REP_MOVSQ_64 : RI<0xA5, RawFrm, (outs), (ins),
                    "{rep;movsq (%rsi), %es:(%rdi)|rep movsq es:[rdi], [rsi]}",
                    [(X86rep_movs i64)]>, REP, AdSize64,
                   Requires<[IsLP64]>;
}

// FIXME: Should use "(X86rep_stos AL)" as the pattern.
let Defs = [ECX,EDI], isCodeGenOnly = 1 in {
  let Uses = [AL,ECX,EDI] in
  def REP_STOSB_32 : I<0xAA, RawFrm, (outs), (ins),
                       "{rep;stosb %al, %es:(%edi)|rep stosb es:[edi], al}",
                      [(X86rep_stos i8)]>, REP, AdSize32,
                     Requires<[NotLP64]>;
  let Uses = [AX,ECX,EDI] in
  def REP_STOSW_32 : I<0xAB, RawFrm, (outs), (ins),
                      "{rep;stosw %ax, %es:(%edi)|rep stosw es:[edi], ax}",
                      [(X86rep_stos i16)]>, REP, AdSize32, OpSize16,
                     Requires<[NotLP64]>;
  let Uses = [EAX,ECX,EDI] in
  def REP_STOSD_32 : I<0xAB, RawFrm, (outs), (ins),
                      "{rep;stosl %eax, %es:(%edi)|rep stosd es:[edi], eax}",
                      [(X86rep_stos i32)]>, REP, AdSize32, OpSize32,
                     Requires<[NotLP64]>;
  let Uses = [RAX,RCX,RDI] in
  def REP_STOSQ_32 : RI<0xAB, RawFrm, (outs), (ins),
                        "{rep;stosq %rax, %es:(%edi)|rep stosq es:[edi], rax}",
                        [(X86rep_stos i64)]>, REP, AdSize32,
                        Requires<[NotLP64, In64BitMode]>;
}

let Defs = [RCX,RDI], isCodeGenOnly = 1 in {
  let Uses = [AL,RCX,RDI] in
  def REP_STOSB_64 : I<0xAA, RawFrm, (outs), (ins),
                       "{rep;stosb %al, %es:(%rdi)|rep stosb es:[rdi], al}",
                       [(X86rep_stos i8)]>, REP, AdSize64,
                       Requires<[IsLP64]>;
  let Uses = [AX,RCX,RDI] in
  def REP_STOSW_64 : I<0xAB, RawFrm, (outs), (ins),
                       "{rep;stosw %ax, %es:(%rdi)|rep stosw es:[rdi], ax}",
                       [(X86rep_stos i16)]>, REP, AdSize64, OpSize16,
                       Requires<[IsLP64]>;
  let Uses = [RAX,RCX,RDI] in
  def REP_STOSD_64 : I<0xAB, RawFrm, (outs), (ins),
                      "{rep;stosl %eax, %es:(%rdi)|rep stosd es:[rdi], eax}",
                       [(X86rep_stos i32)]>, REP, AdSize64, OpSize32,
                       Requires<[IsLP64]>;

  let Uses = [RAX,RCX,RDI] in
  def REP_STOSQ_64 : RI<0xAB, RawFrm, (outs), (ins),
                        "{rep;stosq %rax, %es:(%rdi)|rep stosq es:[rdi], rax}",
                        [(X86rep_stos i64)]>, REP, AdSize64,
                        Requires<[IsLP64]>;
}
} // SchedRW

//===----------------------------------------------------------------------===//
// Thread Local Storage Instructions
//
let SchedRW = [WriteSystem] in {

// ELF TLS Support
// All calls clobber the non-callee saved registers. ESP is marked as
// a use to prevent stack-pointer assignments that appear immediately
// before calls from potentially appearing dead.
let Defs = [EAX, ECX, EDX, FP0, FP1, FP2, FP3, FP4, FP5, FP6, FP7,
            ST0, ST1, ST2, ST3, ST4, ST5, ST6, ST7,
            MM0, MM1, MM2, MM3, MM4, MM5, MM6, MM7,
            XMM0, XMM1, XMM2, XMM3, XMM4, XMM5, XMM6, XMM7,
            XMM8, XMM9, XMM10, XMM11, XMM12, XMM13, XMM14, XMM15, EFLAGS, DF],
    usesCustomInserter = 1, Uses = [ESP, SSP] in {
def TLS_addr32 : I<0, Pseudo, (outs), (ins i32mem:$sym),
                  "# TLS_addr32",
                  [(X86tlsaddr tls32addr:$sym)]>,
                  Requires<[Not64BitMode]>;
def TLS_base_addr32 : I<0, Pseudo, (outs), (ins i32mem:$sym),
                  "# TLS_base_addr32",
                  [(X86tlsbaseaddr tls32baseaddr:$sym)]>,
                  Requires<[Not64BitMode]>;
}

// All calls clobber the non-callee saved registers. RSP is marked as
// a use to prevent stack-pointer assignments that appear immediately
// before calls from potentially appearing dead.
let Defs = [RAX, RCX, RDX, RSI, RDI, R8, R9, R10, R11,
            FP0, FP1, FP2, FP3, FP4, FP5, FP6, FP7,
            ST0, ST1, ST2, ST3, ST4, ST5, ST6, ST7,
            MM0, MM1, MM2, MM3, MM4, MM5, MM6, MM7,
            XMM0, XMM1, XMM2, XMM3, XMM4, XMM5, XMM6, XMM7,
            XMM8, XMM9, XMM10, XMM11, XMM12, XMM13, XMM14, XMM15, EFLAGS, DF],
    usesCustomInserter = 1, Uses = [RSP, SSP] in {
def TLS_addr64 : I<0, Pseudo, (outs), (ins i64mem:$sym),
                   "# TLS_addr64",
                  [(X86tlsaddr tls64addr:$sym)]>,
                  Requires<[In64BitMode, IsLP64]>;
def TLS_base_addr64 : I<0, Pseudo, (outs), (ins i64mem:$sym),
                   "# TLS_base_addr64",
                  [(X86tlsbaseaddr tls64baseaddr:$sym)]>,
                  Requires<[In64BitMode, IsLP64]>;
def TLS_addrX32 : I<0, Pseudo, (outs), (ins i32mem:$sym),
                   "# TLS_addrX32",
                  [(X86tlsaddr tls32addr:$sym)]>,
                  Requires<[In64BitMode, NotLP64]>;
def TLS_base_addrX32 : I<0, Pseudo, (outs), (ins i32mem:$sym),
                   "# TLS_base_addrX32",
                  [(X86tlsbaseaddr tls32baseaddr:$sym)]>,
                  Requires<[In64BitMode, NotLP64]>;
}

// TLSDESC only clobbers EAX and EFLAGS. ESP is marked as a use to prevent
// stack-pointer assignments that appear immediately before calls from
// potentially appearing dead.
let Defs = [EAX, EFLAGS], usesCustomInserter = 1, Uses = [RSP, SSP] in {
  def TLS_desc32 : I<0, Pseudo, (outs), (ins i32mem:$sym),
                     "# TLS_desc32", [(X86tlsdesc tls32addr:$sym)]>;
  def TLS_desc64 : I<0, Pseudo, (outs), (ins i64mem:$sym),
                     "# TLS_desc64", [(X86tlsdesc tls64addr:$sym)]>;
}

// Darwin TLS Support
// For i386, the address of the thunk is passed on the stack, on return the
// address of the variable is in %eax.  %ecx is trashed during the function
// call.  All other registers are preserved.
let Defs = [EAX, ECX, EFLAGS, DF],
    Uses = [ESP, SSP],
    usesCustomInserter = 1 in
def TLSCall_32 : I<0, Pseudo, (outs), (ins i32mem:$sym),
                "# TLSCall_32",
                [(X86TLSCall addr:$sym)]>,
                Requires<[Not64BitMode]>;

// For x86_64, the address of the thunk is passed in %rdi, but the
// pseudo directly use the symbol, so do not add an implicit use of
// %rdi. The lowering will do the right thing with RDI.
// On return the address of the variable is in %rax.  All other
// registers are preserved.
let Defs = [RAX, EFLAGS, DF],
    Uses = [RSP, SSP],
    usesCustomInserter = 1 in
def TLSCall_64 : I<0, Pseudo, (outs), (ins i64mem:$sym),
                  "# TLSCall_64",
                  [(X86TLSCall addr:$sym)]>,
                  Requires<[In64BitMode]>;
} // SchedRW

//===----------------------------------------------------------------------===//
// Conditional Move Pseudo Instructions

// CMOV* - Used to implement the SELECT DAG operation.  Expanded after
// instruction selection into a branch sequence.
multiclass CMOVrr_PSEUDO<RegisterClass RC, ValueType VT> {
  def CMOV#NAME  : I<0, Pseudo,
                    (outs RC:$dst), (ins RC:$t, RC:$f, i8imm:$cond),
                    "#CMOV_"#NAME#" PSEUDO!",
                    [(set RC:$dst, (VT (X86cmov RC:$t, RC:$f, timm:$cond,
                                                EFLAGS)))]>;
}

let usesCustomInserter = 1, hasNoSchedulingInfo = 1, Uses = [EFLAGS] in {
  // X86 doesn't have 8-bit conditional moves. Use a customInserter to
  // emit control flow. An alternative to this is to mark i8 SELECT as Promote,
  // however that requires promoting the operands, and can induce additional
  // i8 register pressure.
  defm _GR8 : CMOVrr_PSEUDO<GR8, i8>;

  let Predicates = [NoCMOV] in {
    defm _GR32 : CMOVrr_PSEUDO<GR32, i32>;
    defm _GR16 : CMOVrr_PSEUDO<GR16, i16>;
  } // Predicates = [NoCMOV]

  // fcmov doesn't handle all possible EFLAGS, provide a fallback if there is no
  // SSE1/SSE2.
  let Predicates = [FPStackf32] in
    defm _RFP32 : CMOVrr_PSEUDO<RFP32, f32>;

  let Predicates = [FPStackf64] in
    defm _RFP64 : CMOVrr_PSEUDO<RFP64, f64>;

  defm _RFP80 : CMOVrr_PSEUDO<RFP80, f80>;

  let Predicates = [HasMMX] in
    defm _VR64   : CMOVrr_PSEUDO<VR64, x86mmx>;

  let Predicates = [HasSSE1,NoAVX512] in
    defm _FR32   : CMOVrr_PSEUDO<FR32, f32>;
  let Predicates = [HasSSE2,NoAVX512] in {
    defm _FR16   : CMOVrr_PSEUDO<FR16, f16>;
    defm _FR64   : CMOVrr_PSEUDO<FR64, f64>;
  }
  let Predicates = [HasAVX512] in {
    defm _FR16X  : CMOVrr_PSEUDO<FR16X, f16>;
    defm _FR32X  : CMOVrr_PSEUDO<FR32X, f32>;
    defm _FR64X  : CMOVrr_PSEUDO<FR64X, f64>;
  }
  let Predicates = [NoVLX] in {
    defm _VR128  : CMOVrr_PSEUDO<VR128, v2i64>;
    defm _VR256  : CMOVrr_PSEUDO<VR256, v4i64>;
  }
  let Predicates = [HasVLX] in {
    defm _VR128X : CMOVrr_PSEUDO<VR128X, v2i64>;
    defm _VR256X : CMOVrr_PSEUDO<VR256X, v4i64>;
  }
  defm _VR512  : CMOVrr_PSEUDO<VR512, v8i64>;
  defm _VK1    : CMOVrr_PSEUDO<VK1,  v1i1>;
  defm _VK2    : CMOVrr_PSEUDO<VK2,  v2i1>;
  defm _VK4    : CMOVrr_PSEUDO<VK4,  v4i1>;
  defm _VK8    : CMOVrr_PSEUDO<VK8,  v8i1>;
  defm _VK16   : CMOVrr_PSEUDO<VK16, v16i1>;
  defm _VK32   : CMOVrr_PSEUDO<VK32, v32i1>;
  defm _VK64   : CMOVrr_PSEUDO<VK64, v64i1>;
} // usesCustomInserter = 1, hasNoSchedulingInfo = 1, Uses = [EFLAGS]

def : Pat<(f128 (X86cmov VR128:$t, VR128:$f, timm:$cond, EFLAGS)),
          (CMOV_VR128 VR128:$t, VR128:$f, timm:$cond)>;

let Predicates = [NoVLX] in {
  def : Pat<(v16i8 (X86cmov VR128:$t, VR128:$f, timm:$cond, EFLAGS)),
            (CMOV_VR128 VR128:$t, VR128:$f, timm:$cond)>;
  def : Pat<(v8i16 (X86cmov VR128:$t, VR128:$f, timm:$cond, EFLAGS)),
            (CMOV_VR128 VR128:$t, VR128:$f, timm:$cond)>;
  def : Pat<(v4i32 (X86cmov VR128:$t, VR128:$f, timm:$cond, EFLAGS)),
            (CMOV_VR128 VR128:$t, VR128:$f, timm:$cond)>;
  def : Pat<(v4f32 (X86cmov VR128:$t, VR128:$f, timm:$cond, EFLAGS)),
            (CMOV_VR128 VR128:$t, VR128:$f, timm:$cond)>;
  def : Pat<(v2f64 (X86cmov VR128:$t, VR128:$f, timm:$cond, EFLAGS)),
            (CMOV_VR128 VR128:$t, VR128:$f, timm:$cond)>;

  def : Pat<(v32i8 (X86cmov VR256:$t, VR256:$f, timm:$cond, EFLAGS)),
            (CMOV_VR256 VR256:$t, VR256:$f, timm:$cond)>;
  def : Pat<(v16i16 (X86cmov VR256:$t, VR256:$f, timm:$cond, EFLAGS)),
            (CMOV_VR256 VR256:$t, VR256:$f, timm:$cond)>;
  def : Pat<(v8i32 (X86cmov VR256:$t, VR256:$f, timm:$cond, EFLAGS)),
            (CMOV_VR256 VR256:$t, VR256:$f, timm:$cond)>;
  def : Pat<(v8f32 (X86cmov VR256:$t, VR256:$f, timm:$cond, EFLAGS)),
            (CMOV_VR256 VR256:$t, VR256:$f, timm:$cond)>;
  def : Pat<(v4f64 (X86cmov VR256:$t, VR256:$f, timm:$cond, EFLAGS)),
            (CMOV_VR256 VR256:$t, VR256:$f, timm:$cond)>;
}
let Predicates = [HasVLX] in {
  def : Pat<(v16i8 (X86cmov VR128X:$t, VR128X:$f, timm:$cond, EFLAGS)),
            (CMOV_VR128X VR128X:$t, VR128X:$f, timm:$cond)>;
  def : Pat<(v8i16 (X86cmov VR128X:$t, VR128X:$f, timm:$cond, EFLAGS)),
            (CMOV_VR128X VR128X:$t, VR128X:$f, timm:$cond)>;
  def : Pat<(v8f16 (X86cmov VR128X:$t, VR128X:$f, timm:$cond, EFLAGS)),
            (CMOV_VR128X VR128X:$t, VR128X:$f, timm:$cond)>;
  def : Pat<(v4i32 (X86cmov VR128X:$t, VR128X:$f, timm:$cond, EFLAGS)),
            (CMOV_VR128X VR128X:$t, VR128X:$f, timm:$cond)>;
  def : Pat<(v4f32 (X86cmov VR128X:$t, VR128X:$f, timm:$cond, EFLAGS)),
            (CMOV_VR128X VR128X:$t, VR128X:$f, timm:$cond)>;
  def : Pat<(v2f64 (X86cmov VR128X:$t, VR128X:$f, timm:$cond, EFLAGS)),
            (CMOV_VR128X VR128X:$t, VR128X:$f, timm:$cond)>;

  def : Pat<(v32i8 (X86cmov VR256X:$t, VR256X:$f, timm:$cond, EFLAGS)),
            (CMOV_VR256X VR256X:$t, VR256X:$f, timm:$cond)>;
  def : Pat<(v16i16 (X86cmov VR256X:$t, VR256X:$f, timm:$cond, EFLAGS)),
            (CMOV_VR256X VR256X:$t, VR256X:$f, timm:$cond)>;
  def : Pat<(v16f16 (X86cmov VR256X:$t, VR256X:$f, timm:$cond, EFLAGS)),
            (CMOV_VR256X VR256X:$t, VR256X:$f, timm:$cond)>;
  def : Pat<(v8i32 (X86cmov VR256X:$t, VR256X:$f, timm:$cond, EFLAGS)),
            (CMOV_VR256X VR256X:$t, VR256X:$f, timm:$cond)>;
  def : Pat<(v8f32 (X86cmov VR256X:$t, VR256X:$f, timm:$cond, EFLAGS)),
            (CMOV_VR256X VR256X:$t, VR256X:$f, timm:$cond)>;
  def : Pat<(v4f64 (X86cmov VR256X:$t, VR256X:$f, timm:$cond, EFLAGS)),
            (CMOV_VR256X VR256X:$t, VR256X:$f, timm:$cond)>;
}

def : Pat<(v64i8 (X86cmov VR512:$t, VR512:$f, timm:$cond, EFLAGS)),
          (CMOV_VR512 VR512:$t, VR512:$f, timm:$cond)>;
def : Pat<(v32i16 (X86cmov VR512:$t, VR512:$f, timm:$cond, EFLAGS)),
          (CMOV_VR512 VR512:$t, VR512:$f, timm:$cond)>;
def : Pat<(v32f16 (X86cmov VR512:$t, VR512:$f, timm:$cond, EFLAGS)),
          (CMOV_VR512 VR512:$t, VR512:$f, timm:$cond)>;
def : Pat<(v16i32 (X86cmov VR512:$t, VR512:$f, timm:$cond, EFLAGS)),
          (CMOV_VR512 VR512:$t, VR512:$f, timm:$cond)>;
def : Pat<(v16f32 (X86cmov VR512:$t, VR512:$f, timm:$cond, EFLAGS)),
          (CMOV_VR512 VR512:$t, VR512:$f, timm:$cond)>;
def : Pat<(v8f64 (X86cmov VR512:$t, VR512:$f, timm:$cond, EFLAGS)),
          (CMOV_VR512 VR512:$t, VR512:$f, timm:$cond)>;

//===----------------------------------------------------------------------===//
// Normal-Instructions-With-Lock-Prefix Pseudo Instructions
//===----------------------------------------------------------------------===//

// FIXME: Use normal instructions and add lock prefix dynamically.

// Memory barriers

let isCodeGenOnly = 1, Defs = [EFLAGS] in
def OR32mi8Locked  : Ii8<0x83, MRM1m, (outs), (ins i32mem:$dst, i32i8imm:$zero),
                         "or{l}\t{$zero, $dst|$dst, $zero}", []>,
                         Requires<[Not64BitMode]>, OpSize32, LOCK,
                         Sched<[WriteALURMW]>;

// RegOpc corresponds to the mr version of the instruction
// ImmOpc corresponds to the mi version of the instruction
// ImmOpc8 corresponds to the mi8 version of the instruction
// ImmMod corresponds to the instruction format of the mi and mi8 versions
multiclass LOCK_ArithBinOp<bits<8> RegOpc, bits<8> ImmOpc, bits<8> ImmOpc8,
                           Format ImmMod, SDNode Op, string mnemonic> {
let Defs = [EFLAGS], mayLoad = 1, mayStore = 1, isCodeGenOnly = 1,
    SchedRW = [WriteALURMW] in {

def NAME#8mr : I<{RegOpc{7}, RegOpc{6}, RegOpc{5}, RegOpc{4},
                  RegOpc{3}, RegOpc{2}, RegOpc{1}, 0 },
                  MRMDestMem, (outs), (ins i8mem:$dst, GR8:$src2),
                  !strconcat(mnemonic, "{b}\t",
                             "{$src2, $dst|$dst, $src2}"),
                  [(set EFLAGS, (Op addr:$dst, GR8:$src2))]>, LOCK;

def NAME#16mr : I<{RegOpc{7}, RegOpc{6}, RegOpc{5}, RegOpc{4},
                   RegOpc{3}, RegOpc{2}, RegOpc{1}, 1 },
                   MRMDestMem, (outs), (ins i16mem:$dst, GR16:$src2),
                   !strconcat(mnemonic, "{w}\t",
                              "{$src2, $dst|$dst, $src2}"),
                   [(set EFLAGS, (Op addr:$dst, GR16:$src2))]>,
                   OpSize16, LOCK;

def NAME#32mr : I<{RegOpc{7}, RegOpc{6}, RegOpc{5}, RegOpc{4},
                   RegOpc{3}, RegOpc{2}, RegOpc{1}, 1 },
                   MRMDestMem, (outs), (ins i32mem:$dst, GR32:$src2),
                   !strconcat(mnemonic, "{l}\t",
                              "{$src2, $dst|$dst, $src2}"),
                   [(set EFLAGS, (Op addr:$dst, GR32:$src2))]>,
                   OpSize32, LOCK;

def NAME#64mr : RI<{RegOpc{7}, RegOpc{6}, RegOpc{5}, RegOpc{4},
                    RegOpc{3}, RegOpc{2}, RegOpc{1}, 1 },
                    MRMDestMem, (outs), (ins i64mem:$dst, GR64:$src2),
                    !strconcat(mnemonic, "{q}\t",
                               "{$src2, $dst|$dst, $src2}"),
                    [(set EFLAGS, (Op addr:$dst, GR64:$src2))]>, LOCK;

// NOTE: These are order specific, we want the mi8 forms to be listed
// first so that they are slightly preferred to the mi forms.
def NAME#16mi8 : Ii8<{ImmOpc8{7}, ImmOpc8{6}, ImmOpc8{5}, ImmOpc8{4},
                      ImmOpc8{3}, ImmOpc8{2}, ImmOpc8{1}, 1 },
                      ImmMod, (outs), (ins i16mem :$dst, i16i8imm :$src2),
                      !strconcat(mnemonic, "{w}\t",
                                 "{$src2, $dst|$dst, $src2}"),
                      [(set EFLAGS, (Op addr:$dst, i16immSExt8:$src2))]>,
                      OpSize16, LOCK;

def NAME#32mi8 : Ii8<{ImmOpc8{7}, ImmOpc8{6}, ImmOpc8{5}, ImmOpc8{4},
                      ImmOpc8{3}, ImmOpc8{2}, ImmOpc8{1}, 1 },
                      ImmMod, (outs), (ins i32mem :$dst, i32i8imm :$src2),
                      !strconcat(mnemonic, "{l}\t",
                                 "{$src2, $dst|$dst, $src2}"),
                      [(set EFLAGS, (Op addr:$dst, i32immSExt8:$src2))]>,
                      OpSize32, LOCK;

def NAME#64mi8 : RIi8<{ImmOpc8{7}, ImmOpc8{6}, ImmOpc8{5}, ImmOpc8{4},
                       ImmOpc8{3}, ImmOpc8{2}, ImmOpc8{1}, 1 },
                       ImmMod, (outs), (ins i64mem :$dst, i64i8imm :$src2),
                       !strconcat(mnemonic, "{q}\t",
                                  "{$src2, $dst|$dst, $src2}"),
                       [(set EFLAGS, (Op addr:$dst, i64immSExt8:$src2))]>,
                       LOCK;

def NAME#8mi : Ii8<{ImmOpc{7}, ImmOpc{6}, ImmOpc{5}, ImmOpc{4},
                    ImmOpc{3}, ImmOpc{2}, ImmOpc{1}, 0 },
                    ImmMod, (outs), (ins i8mem :$dst, i8imm :$src2),
                    !strconcat(mnemonic, "{b}\t",
                               "{$src2, $dst|$dst, $src2}"),
                    [(set EFLAGS, (Op addr:$dst, (i8 imm:$src2)))]>, LOCK;

def NAME#16mi : Ii16<{ImmOpc{7}, ImmOpc{6}, ImmOpc{5}, ImmOpc{4},
                      ImmOpc{3}, ImmOpc{2}, ImmOpc{1}, 1 },
                      ImmMod, (outs), (ins i16mem :$dst, i16imm :$src2),
                      !strconcat(mnemonic, "{w}\t",
                                 "{$src2, $dst|$dst, $src2}"),
                      [(set EFLAGS, (Op addr:$dst, (i16 imm:$src2)))]>,
                      OpSize16, LOCK;

def NAME#32mi : Ii32<{ImmOpc{7}, ImmOpc{6}, ImmOpc{5}, ImmOpc{4},
                      ImmOpc{3}, ImmOpc{2}, ImmOpc{1}, 1 },
                      ImmMod, (outs), (ins i32mem :$dst, i32imm :$src2),
                      !strconcat(mnemonic, "{l}\t",
                                 "{$src2, $dst|$dst, $src2}"),
                      [(set EFLAGS, (Op addr:$dst, (i32 imm:$src2)))]>,
                      OpSize32, LOCK;

def NAME#64mi32 : RIi32S<{ImmOpc{7}, ImmOpc{6}, ImmOpc{5}, ImmOpc{4},
                          ImmOpc{3}, ImmOpc{2}, ImmOpc{1}, 1 },
                          ImmMod, (outs), (ins i64mem :$dst, i64i32imm :$src2),
                          !strconcat(mnemonic, "{q}\t",
                                     "{$src2, $dst|$dst, $src2}"),
                          [(set EFLAGS, (Op addr:$dst, i64immSExt32:$src2))]>,
                          LOCK;
}

}

defm LOCK_ADD : LOCK_ArithBinOp<0x00, 0x80, 0x83, MRM0m, X86lock_add, "add">;
defm LOCK_SUB : LOCK_ArithBinOp<0x28, 0x80, 0x83, MRM5m, X86lock_sub, "sub">;
defm LOCK_OR  : LOCK_ArithBinOp<0x08, 0x80, 0x83, MRM1m, X86lock_or , "or">;
defm LOCK_AND : LOCK_ArithBinOp<0x20, 0x80, 0x83, MRM4m, X86lock_and, "and">;
defm LOCK_XOR : LOCK_ArithBinOp<0x30, 0x80, 0x83, MRM6m, X86lock_xor, "xor">;

let Defs = [EFLAGS], mayLoad = 1, mayStore = 1, isCodeGenOnly = 1,
    SchedRW = [WriteALURMW]  in {
  let Predicates = [UseIncDec] in {
    def LOCK_INC8m  : I<0xFE, MRM0m, (outs), (ins i8mem :$dst),
                        "inc{b}\t$dst",
                        [(set EFLAGS, (X86lock_add_nocf addr:$dst, (i8 1)))]>,
                        LOCK;
    def LOCK_INC16m : I<0xFF, MRM0m, (outs), (ins i16mem:$dst),
                        "inc{w}\t$dst",
                        [(set EFLAGS, (X86lock_add_nocf addr:$dst, (i16 1)))]>,
                        OpSize16, LOCK;
    def LOCK_INC32m : I<0xFF, MRM0m, (outs), (ins i32mem:$dst),
                        "inc{l}\t$dst",
                        [(set EFLAGS, (X86lock_add_nocf addr:$dst, (i32 1)))]>,
                        OpSize32, LOCK;

    def LOCK_DEC8m  : I<0xFE, MRM1m, (outs), (ins i8mem :$dst),
                        "dec{b}\t$dst",
                        [(set EFLAGS, (X86lock_sub_nocf addr:$dst, (i8 1)))]>,
                        LOCK;
    def LOCK_DEC16m : I<0xFF, MRM1m, (outs), (ins i16mem:$dst),
                        "dec{w}\t$dst",
                        [(set EFLAGS, (X86lock_sub_nocf addr:$dst, (i16 1)))]>,
                        OpSize16, LOCK;
    def LOCK_DEC32m : I<0xFF, MRM1m, (outs), (ins i32mem:$dst),
                        "dec{l}\t$dst",
                        [(set EFLAGS, (X86lock_sub_nocf addr:$dst, (i32 1)))]>,
                        OpSize32, LOCK;
  }

  let Predicates = [UseIncDec, In64BitMode] in {
    def LOCK_INC64m : RI<0xFF, MRM0m, (outs), (ins i64mem:$dst),
                         "inc{q}\t$dst",
                         [(set EFLAGS, (X86lock_add_nocf addr:$dst, (i64 1)))]>,
                         LOCK;
    def LOCK_DEC64m : RI<0xFF, MRM1m, (outs), (ins i64mem:$dst),
                         "dec{q}\t$dst",
                         [(set EFLAGS, (X86lock_sub_nocf addr:$dst, (i64 1)))]>,
                         LOCK;
  }
}

let Predicates = [UseIncDec] in {
  // Additional patterns for -1 constant.
  def : Pat<(X86lock_add addr:$dst, (i8  -1)), (LOCK_DEC8m  addr:$dst)>;
  def : Pat<(X86lock_add addr:$dst, (i16 -1)), (LOCK_DEC16m addr:$dst)>;
  def : Pat<(X86lock_add addr:$dst, (i32 -1)), (LOCK_DEC32m addr:$dst)>;
  def : Pat<(X86lock_sub addr:$dst, (i8  -1)), (LOCK_INC8m  addr:$dst)>;
  def : Pat<(X86lock_sub addr:$dst, (i16 -1)), (LOCK_INC16m addr:$dst)>;
  def : Pat<(X86lock_sub addr:$dst, (i32 -1)), (LOCK_INC32m addr:$dst)>;
}

let Predicates = [UseIncDec, In64BitMode] in {
  // Additional patterns for -1 constant.
  def : Pat<(X86lock_add addr:$dst, (i64 -1)), (LOCK_DEC64m addr:$dst)>;
  def : Pat<(X86lock_sub addr:$dst, (i64 -1)), (LOCK_INC64m addr:$dst)>;
}

// Atomic bit test.
def X86LBTest : SDTypeProfile<1, 3, [SDTCisVT<0, i32>, SDTCisPtrTy<1>,
                                     SDTCisVT<2, i8>, SDTCisVT<3, i32>]>;
def x86bts : SDNode<"X86ISD::LBTS", X86LBTest,
                    [SDNPHasChain, SDNPMayLoad, SDNPMayStore, SDNPMemOperand]>;
def x86btc : SDNode<"X86ISD::LBTC", X86LBTest,
                    [SDNPHasChain, SDNPMayLoad, SDNPMayStore, SDNPMemOperand]>;
def x86btr : SDNode<"X86ISD::LBTR", X86LBTest,
                    [SDNPHasChain, SDNPMayLoad, SDNPMayStore, SDNPMemOperand]>;

def X86LBTestRM : SDTypeProfile<1, 2, [SDTCisVT<0, i32>, SDTCisPtrTy<1>,
                                       SDTCisInt<2>]>;

def x86_rm_bts : SDNode<"X86ISD::LBTS_RM", X86LBTestRM,
                        [SDNPHasChain, SDNPMayLoad, SDNPMayStore, SDNPMemOperand]>;
def x86_rm_btc : SDNode<"X86ISD::LBTC_RM", X86LBTestRM,
                        [SDNPHasChain, SDNPMayLoad, SDNPMayStore, SDNPMemOperand]>;
def x86_rm_btr : SDNode<"X86ISD::LBTR_RM", X86LBTestRM,
                        [SDNPHasChain, SDNPMayLoad, SDNPMayStore, SDNPMemOperand]>;


multiclass ATOMIC_LOGIC_OP<Format Form, string s> {
  let Defs = [EFLAGS], mayLoad = 1, mayStore = 1, isCodeGenOnly = 1,
      SchedRW = [WriteBitTestSetRegRMW]  in {
    def 16m : Ii8<0xBA, Form, (outs), (ins i16mem:$src1, i8imm:$src2),
                  !strconcat(s, "{w}\t{$src2, $src1|$src1, $src2}"),
                  [(set EFLAGS, (!cast<SDNode>("x86" # s) addr:$src1, timm:$src2, (i32 16)))]>,
              OpSize16, TB, LOCK;
    def 32m : Ii8<0xBA, Form, (outs), (ins i32mem:$src1, i8imm:$src2),
                  !strconcat(s, "{l}\t{$src2, $src1|$src1, $src2}"),
                  [(set EFLAGS, (!cast<SDNode>("x86" # s) addr:$src1, timm:$src2, (i32 32)))]>,
              OpSize32, TB, LOCK;
    def 64m : RIi8<0xBA, Form, (outs), (ins i64mem:$src1, i8imm:$src2),
                   !strconcat(s, "{q}\t{$src2, $src1|$src1, $src2}"),
                   [(set EFLAGS, (!cast<SDNode>("x86" # s) addr:$src1, timm:$src2, (i32 64)))]>,
              TB, LOCK;
  }
}

multiclass ATOMIC_LOGIC_OP_RM<bits<8> Opc8, string s> {
  let Defs = [EFLAGS], mayLoad = 1, mayStore = 1, isCodeGenOnly = 1,
      SchedRW = [WriteBitTestSetRegRMW]  in {
    def 16rm : I<Opc8, MRMDestMem, (outs), (ins i16mem:$src1, GR16:$src2),
                  !strconcat(s, "{w}\t{$src2, $src1|$src1, $src2}"),
                  [(set EFLAGS, (!cast<SDNode>("x86_rm_" # s) addr:$src1, GR16:$src2))]>,
               OpSize16, TB, LOCK;
    def 32rm : I<Opc8, MRMDestMem, (outs), (ins i32mem:$src1, GR32:$src2),
                  !strconcat(s, "{l}\t{$src2, $src1|$src1, $src2}"),
                  [(set EFLAGS, (!cast<SDNode>("x86_rm_" # s) addr:$src1, GR32:$src2))]>,
               OpSize32, TB, LOCK;
    def 64rm : RI<Opc8, MRMDestMem, (outs), (ins i64mem:$src1, GR64:$src2),
                   !strconcat(s, "{q}\t{$src2, $src1|$src1, $src2}"),
                   [(set EFLAGS, (!cast<SDNode>("x86_rm_" # s) addr:$src1, GR64:$src2))]>,
               TB, LOCK;
  }
}


defm LOCK_BTS : ATOMIC_LOGIC_OP<MRM5m, "bts">;
defm LOCK_BTC : ATOMIC_LOGIC_OP<MRM7m, "btc">;
defm LOCK_BTR : ATOMIC_LOGIC_OP<MRM6m, "btr">;

defm LOCK_BTS_RM : ATOMIC_LOGIC_OP_RM<0xAB, "bts">;
defm LOCK_BTC_RM : ATOMIC_LOGIC_OP_RM<0xBB, "btc">;
defm LOCK_BTR_RM : ATOMIC_LOGIC_OP_RM<0xB3, "btr">;

// Atomic compare and swap.
multiclass LCMPXCHG_BinOp<bits<8> Opc8, bits<8> Opc, Format Form,
                          string mnemonic, SDPatternOperator frag> {
let isCodeGenOnly = 1, SchedRW = [WriteCMPXCHGRMW] in {
  let Defs = [AL, EFLAGS], Uses = [AL] in
  def NAME#8  : I<Opc8, Form, (outs), (ins i8mem:$ptr, GR8:$swap),
                  !strconcat(mnemonic, "{b}\t{$swap, $ptr|$ptr, $swap}"),
                  [(frag addr:$ptr, GR8:$swap, 1)]>, TB, LOCK;
  let Defs = [AX, EFLAGS], Uses = [AX] in
  def NAME#16 : I<Opc, Form, (outs), (ins i16mem:$ptr, GR16:$swap),
                  !strconcat(mnemonic, "{w}\t{$swap, $ptr|$ptr, $swap}"),
                  [(frag addr:$ptr, GR16:$swap, 2)]>, TB, OpSize16, LOCK;
  let Defs = [EAX, EFLAGS], Uses = [EAX] in
  def NAME#32 : I<Opc, Form, (outs), (ins i32mem:$ptr, GR32:$swap),
                  !strconcat(mnemonic, "{l}\t{$swap, $ptr|$ptr, $swap}"),
                  [(frag addr:$ptr, GR32:$swap, 4)]>, TB, OpSize32, LOCK;
  let Defs = [RAX, EFLAGS], Uses = [RAX] in
  def NAME#64 : RI<Opc, Form, (outs), (ins i64mem:$ptr, GR64:$swap),
                   !strconcat(mnemonic, "{q}\t{$swap, $ptr|$ptr, $swap}"),
                   [(frag addr:$ptr, GR64:$swap, 8)]>, TB, LOCK;
}
}

let Defs = [EAX, EDX, EFLAGS], Uses = [EAX, EBX, ECX, EDX],
    Predicates = [HasCX8], SchedRW = [WriteCMPXCHGRMW],
    isCodeGenOnly = 1, usesCustomInserter = 1 in {
def LCMPXCHG8B : I<0xC7, MRM1m, (outs), (ins i64mem:$ptr),
                   "cmpxchg8b\t$ptr",
                   [(X86cas8 addr:$ptr)]>, TB, LOCK;
}

let Defs = [RAX, RDX, EFLAGS], Uses = [RAX, RBX, RCX, RDX],
    Predicates = [HasCX16,In64BitMode], SchedRW = [WriteCMPXCHGRMW],
    isCodeGenOnly = 1, mayLoad = 1, mayStore = 1, hasSideEffects = 0 in {
def LCMPXCHG16B : RI<0xC7, MRM1m, (outs), (ins i128mem:$ptr),
                     "cmpxchg16b\t$ptr",
                     []>, TB, LOCK;
}

// This pseudo must be used when the frame uses RBX as
// the base pointer. Indeed, in such situation RBX is a reserved
// register and the register allocator will ignore any use/def of
// it. In other words, the register will not fix the clobbering of
// RBX that will happen when setting the arguments for the instrucion.
//
// Unlike the actual related instruction, we mark that this one
// defines RBX (instead of using RBX).
// The rationale is that we will define RBX during the expansion of
// the pseudo. The argument feeding RBX is rbx_input.
//
// The additional argument, $rbx_save, is a temporary register used to
// save the value of RBX across the actual instruction.
//
// To make sure the register assigned to $rbx_save does not interfere with
// the definition of the actual instruction, we use a definition $dst which
// is tied to $rbx_save. That way, the live-range of $rbx_save spans across
// the instruction and we are sure we will have a valid register to restore
// the value of RBX.
let Defs = [RAX, RDX, RBX, EFLAGS], Uses = [RAX, RCX, RDX],
    Predicates = [HasCX16,In64BitMode], SchedRW = [WriteCMPXCHGRMW],
    isCodeGenOnly = 1, isPseudo = 1,
    mayLoad = 1, mayStore = 1, hasSideEffects = 0,
    Constraints = "$rbx_save = $dst" in {
def LCMPXCHG16B_SAVE_RBX :
    I<0, Pseudo, (outs GR64:$dst),
      (ins i128mem:$ptr, GR64:$rbx_input, GR64:$rbx_save), "", []>;
}

// Pseudo instruction that doesn't read/write RBX. Will be turned into either
// LCMPXCHG16B_SAVE_RBX or LCMPXCHG16B via a custom inserter.
let Defs = [RAX, RDX, EFLAGS], Uses = [RAX, RCX, RDX],
    Predicates = [HasCX16,In64BitMode], SchedRW = [WriteCMPXCHGRMW],
    isCodeGenOnly = 1, isPseudo = 1,
    mayLoad = 1, mayStore = 1, hasSideEffects = 0,
    usesCustomInserter = 1 in {
def LCMPXCHG16B_NO_RBX :
    I<0, Pseudo, (outs), (ins i128mem:$ptr, GR64:$rbx_input), "",
      [(X86cas16 addr:$ptr, GR64:$rbx_input)]>;
}

// This pseudo must be used when the frame uses RBX/EBX as
// the base pointer.
// cf comment for LCMPXCHG16B_SAVE_RBX.
let Defs = [EBX], Uses = [ECX, EAX],
    Predicates = [HasMWAITX], SchedRW = [WriteSystem],
    isCodeGenOnly = 1, isPseudo = 1, Constraints = "$rbx_save = $dst" in {
def MWAITX_SAVE_RBX :
    I<0, Pseudo, (outs GR64:$dst),
      (ins GR32:$ebx_input, GR64:$rbx_save),
      "mwaitx",
      []>;
}

// Pseudo mwaitx instruction to use for custom insertion.
let Predicates = [HasMWAITX], SchedRW = [WriteSystem],
    isCodeGenOnly = 1, isPseudo = 1,
    usesCustomInserter = 1 in {
def MWAITX :
    I<0, Pseudo, (outs), (ins GR32:$ecx, GR32:$eax, GR32:$ebx),
      "mwaitx",
      [(int_x86_mwaitx GR32:$ecx, GR32:$eax, GR32:$ebx)]>;
}


defm LCMPXCHG : LCMPXCHG_BinOp<0xB0, 0xB1, MRMDestMem, "cmpxchg", X86cas>;

// Atomic exchange and add
multiclass ATOMIC_RMW_BINOP<bits<8> opc8, bits<8> opc, string mnemonic,
                            string frag> {
  let Constraints = "$val = $dst", Defs = [EFLAGS], mayLoad = 1, mayStore = 1,
      isCodeGenOnly = 1, SchedRW = [WriteALURMW] in {
    def NAME#8  : I<opc8, MRMSrcMem, (outs GR8:$dst),
                    (ins GR8:$val, i8mem:$ptr),
                    !strconcat(mnemonic, "{b}\t{$val, $ptr|$ptr, $val}"),
                    [(set GR8:$dst,
                          (!cast<PatFrag>(frag # "_i8") addr:$ptr, GR8:$val))]>;
    def NAME#16 : I<opc, MRMSrcMem, (outs GR16:$dst),
                    (ins GR16:$val, i16mem:$ptr),
                    !strconcat(mnemonic, "{w}\t{$val, $ptr|$ptr, $val}"),
                    [(set
                       GR16:$dst,
                       (!cast<PatFrag>(frag # "_i16") addr:$ptr, GR16:$val))]>,
                    OpSize16;
    def NAME#32 : I<opc, MRMSrcMem, (outs GR32:$dst),
                    (ins GR32:$val, i32mem:$ptr),
                    !strconcat(mnemonic, "{l}\t{$val, $ptr|$ptr, $val}"),
                    [(set
                       GR32:$dst,
                       (!cast<PatFrag>(frag # "_i32") addr:$ptr, GR32:$val))]>,
                    OpSize32;
    def NAME#64 : RI<opc, MRMSrcMem, (outs GR64:$dst),
                     (ins GR64:$val, i64mem:$ptr),
                     !strconcat(mnemonic, "{q}\t{$val, $ptr|$ptr, $val}"),
                     [(set
                        GR64:$dst,
                        (!cast<PatFrag>(frag # "_i64") addr:$ptr, GR64:$val))]>;
  }
}

defm LXADD : ATOMIC_RMW_BINOP<0xc0, 0xc1, "xadd", "atomic_load_add">, TB, LOCK;

/* The following multiclass tries to make sure that in code like
 *    x.store (immediate op x.load(acquire), release)
 * and
 *    x.store (register op x.load(acquire), release)
 * an operation directly on memory is generated instead of wasting a register.
 * It is not automatic as atomic_store/load are only lowered to MOV instructions
 * extremely late to prevent them from being accidentally reordered in the backend
 * (see below the RELEASE_MOV* / ACQUIRE_MOV* pseudo-instructions)
 */
multiclass RELEASE_BINOP_MI<string Name, SDNode op> {
  def : Pat<(atomic_store_8 (op (atomic_load_8 addr:$dst), (i8 imm:$src)),
                            addr:$dst),
            (!cast<Instruction>(Name#"8mi") addr:$dst, imm:$src)>;
  def : Pat<(atomic_store_16 (op (atomic_load_16 addr:$dst), (i16 imm:$src)),
                             addr:$dst),
            (!cast<Instruction>(Name#"16mi") addr:$dst, imm:$src)>;
  def : Pat<(atomic_store_32 (op (atomic_load_32 addr:$dst), (i32 imm:$src)),
                             addr:$dst),
            (!cast<Instruction>(Name#"32mi") addr:$dst, imm:$src)>;
  def : Pat<(atomic_store_64 (op (atomic_load_64 addr:$dst), (i64immSExt32:$src)),
                             addr:$dst),
            (!cast<Instruction>(Name#"64mi32") addr:$dst, (i64immSExt32:$src))>;
  def : Pat<(atomic_store_8 (op (atomic_load_8 addr:$dst), (i8 GR8:$src)), addr:$dst),
            (!cast<Instruction>(Name#"8mr") addr:$dst, GR8:$src)>;
  def : Pat<(atomic_store_16 (op (atomic_load_16 addr:$dst), (i16 GR16:$src)),
                             addr:$dst),
            (!cast<Instruction>(Name#"16mr") addr:$dst, GR16:$src)>;
  def : Pat<(atomic_store_32 (op (atomic_load_32 addr:$dst), (i32 GR32:$src)),
                             addr:$dst),
            (!cast<Instruction>(Name#"32mr") addr:$dst, GR32:$src)>;
  def : Pat<(atomic_store_64 (op (atomic_load_64 addr:$dst), (i64 GR64:$src)),
                             addr:$dst),
            (!cast<Instruction>(Name#"64mr") addr:$dst, GR64:$src)>;
}
defm : RELEASE_BINOP_MI<"ADD", add>;
defm : RELEASE_BINOP_MI<"AND", and>;
defm : RELEASE_BINOP_MI<"OR",  or>;
defm : RELEASE_BINOP_MI<"XOR", xor>;
defm : RELEASE_BINOP_MI<"SUB", sub>;

// Atomic load + floating point patterns.
// FIXME: This could also handle SIMD operations with *ps and *pd instructions.
multiclass ATOMIC_LOAD_FP_BINOP_MI<string Name, SDNode op> {
  def : Pat<(op FR32:$src1, (bitconvert (i32 (atomic_load_32 addr:$src2)))),
            (!cast<Instruction>(Name#"SSrm") FR32:$src1, addr:$src2)>,
            Requires<[UseSSE1]>;
  def : Pat<(op FR32:$src1, (bitconvert (i32 (atomic_load_32 addr:$src2)))),
            (!cast<Instruction>("V"#Name#"SSrm") FR32:$src1, addr:$src2)>,
            Requires<[UseAVX]>;
  def : Pat<(op FR32X:$src1, (bitconvert (i32 (atomic_load_32 addr:$src2)))),
            (!cast<Instruction>("V"#Name#"SSZrm") FR32X:$src1, addr:$src2)>,
            Requires<[HasAVX512]>;

  def : Pat<(op FR64:$src1, (bitconvert (i64 (atomic_load_64 addr:$src2)))),
            (!cast<Instruction>(Name#"SDrm") FR64:$src1, addr:$src2)>,
            Requires<[UseSSE1]>;
  def : Pat<(op FR64:$src1, (bitconvert (i64 (atomic_load_64 addr:$src2)))),
            (!cast<Instruction>("V"#Name#"SDrm") FR64:$src1, addr:$src2)>,
            Requires<[UseAVX]>;
  def : Pat<(op FR64X:$src1, (bitconvert (i64 (atomic_load_64 addr:$src2)))),
            (!cast<Instruction>("V"#Name#"SDZrm") FR64X:$src1, addr:$src2)>,
            Requires<[HasAVX512]>;
}
defm : ATOMIC_LOAD_FP_BINOP_MI<"ADD", fadd>;
defm : ATOMIC_LOAD_FP_BINOP_MI<"SUB", fsub>;
defm : ATOMIC_LOAD_FP_BINOP_MI<"MUL", fmul>;
defm : ATOMIC_LOAD_FP_BINOP_MI<"DIV", fdiv>;

multiclass RELEASE_UNOP<string Name, dag dag8, dag dag16, dag dag32,
                        dag dag64> {
  def : Pat<(atomic_store_8 dag8, addr:$dst),
            (!cast<Instruction>(Name#8m) addr:$dst)>;
  def : Pat<(atomic_store_16 dag16, addr:$dst),
            (!cast<Instruction>(Name#16m) addr:$dst)>;
  def : Pat<(atomic_store_32 dag32, addr:$dst),
            (!cast<Instruction>(Name#32m) addr:$dst)>;
  def : Pat<(atomic_store_64 dag64, addr:$dst),
            (!cast<Instruction>(Name#64m) addr:$dst)>;
}

let Predicates = [UseIncDec] in {
  defm : RELEASE_UNOP<"INC",
      (add (atomic_load_8  addr:$dst), (i8 1)),
      (add (atomic_load_16 addr:$dst), (i16 1)),
      (add (atomic_load_32 addr:$dst), (i32 1)),
      (add (atomic_load_64 addr:$dst), (i64 1))>;
  defm : RELEASE_UNOP<"DEC",
      (add (atomic_load_8  addr:$dst), (i8 -1)),
      (add (atomic_load_16 addr:$dst), (i16 -1)),
      (add (atomic_load_32 addr:$dst), (i32 -1)),
      (add (atomic_load_64 addr:$dst), (i64 -1))>;
}

defm : RELEASE_UNOP<"NEG",
    (ineg (i8 (atomic_load_8  addr:$dst))),
    (ineg (i16 (atomic_load_16 addr:$dst))),
    (ineg (i32 (atomic_load_32 addr:$dst))),
    (ineg (i64 (atomic_load_64 addr:$dst)))>;
defm : RELEASE_UNOP<"NOT",
    (not (i8 (atomic_load_8  addr:$dst))),
    (not (i16 (atomic_load_16 addr:$dst))),
    (not (i32 (atomic_load_32 addr:$dst))),
    (not (i64 (atomic_load_64 addr:$dst)))>;

def : Pat<(atomic_store_8 (i8 imm:$src), addr:$dst),
          (MOV8mi addr:$dst, imm:$src)>;
def : Pat<(atomic_store_16 (i16 imm:$src), addr:$dst),
          (MOV16mi addr:$dst, imm:$src)>;
def : Pat<(atomic_store_32 (i32 imm:$src), addr:$dst),
          (MOV32mi addr:$dst, imm:$src)>;
def : Pat<(atomic_store_64 (i64immSExt32:$src), addr:$dst),
          (MOV64mi32 addr:$dst, i64immSExt32:$src)>;

def : Pat<(atomic_store_8 GR8:$src, addr:$dst),
          (MOV8mr addr:$dst, GR8:$src)>;
def : Pat<(atomic_store_16 GR16:$src, addr:$dst),
          (MOV16mr addr:$dst, GR16:$src)>;
def : Pat<(atomic_store_32 GR32:$src, addr:$dst),
          (MOV32mr addr:$dst, GR32:$src)>;
def : Pat<(atomic_store_64 GR64:$src, addr:$dst),
          (MOV64mr addr:$dst, GR64:$src)>;

def : Pat<(i8  (atomic_load_8 addr:$src)),  (MOV8rm addr:$src)>;
def : Pat<(i16 (atomic_load_16 addr:$src)), (MOV16rm addr:$src)>;
def : Pat<(i32 (atomic_load_32 addr:$src)), (MOV32rm addr:$src)>;
def : Pat<(i64 (atomic_load_64 addr:$src)), (MOV64rm addr:$src)>;

// Floating point loads/stores.
def : Pat<(atomic_store_32 (i32 (bitconvert (f32 FR32:$src))), addr:$dst),
          (MOVSSmr addr:$dst, FR32:$src)>, Requires<[UseSSE1]>;
def : Pat<(atomic_store_32 (i32 (bitconvert (f32 FR32:$src))), addr:$dst),
          (VMOVSSmr addr:$dst, FR32:$src)>, Requires<[UseAVX]>;
def : Pat<(atomic_store_32 (i32 (bitconvert (f32 FR32:$src))), addr:$dst),
          (VMOVSSZmr addr:$dst, FR32:$src)>, Requires<[HasAVX512]>;

def : Pat<(atomic_store_64 (i64 (bitconvert (f64 FR64:$src))), addr:$dst),
          (MOVSDmr addr:$dst, FR64:$src)>, Requires<[UseSSE2]>;
def : Pat<(atomic_store_64 (i64 (bitconvert (f64 FR64:$src))), addr:$dst),
          (VMOVSDmr addr:$dst, FR64:$src)>, Requires<[UseAVX]>;
def : Pat<(atomic_store_64 (i64 (bitconvert (f64 FR64:$src))), addr:$dst),
          (VMOVSDmr addr:$dst, FR64:$src)>, Requires<[HasAVX512]>;

def : Pat<(f32 (bitconvert (i32 (atomic_load_32 addr:$src)))),
          (MOVSSrm_alt addr:$src)>, Requires<[UseSSE1]>;
def : Pat<(f32 (bitconvert (i32 (atomic_load_32 addr:$src)))),
          (VMOVSSrm_alt addr:$src)>, Requires<[UseAVX]>;
def : Pat<(f32 (bitconvert (i32 (atomic_load_32 addr:$src)))),
          (VMOVSSZrm_alt addr:$src)>, Requires<[HasAVX512]>;

def : Pat<(f64 (bitconvert (i64 (atomic_load_64 addr:$src)))),
          (MOVSDrm_alt addr:$src)>, Requires<[UseSSE2]>;
def : Pat<(f64 (bitconvert (i64 (atomic_load_64 addr:$src)))),
          (VMOVSDrm_alt addr:$src)>, Requires<[UseAVX]>;
def : Pat<(f64 (bitconvert (i64 (atomic_load_64 addr:$src)))),
          (VMOVSDZrm_alt addr:$src)>, Requires<[HasAVX512]>;

//===----------------------------------------------------------------------===//
// DAG Pattern Matching Rules
//===----------------------------------------------------------------------===//

// Use AND/OR to store 0/-1 in memory when optimizing for minsize. This saves
// binary size compared to a regular MOV, but it introduces an unnecessary
// load, so is not suitable for regular or optsize functions.
let Predicates = [OptForMinSize] in {
def : Pat<(simple_store (i16 0), addr:$dst), (AND16mi addr:$dst, 0)>;
def : Pat<(simple_store (i32 0), addr:$dst), (AND32mi addr:$dst, 0)>;
def : Pat<(simple_store (i64 0), addr:$dst), (AND64mi32 addr:$dst, 0)>;
def : Pat<(simple_store (i16 -1), addr:$dst), (OR16mi addr:$dst, -1)>;
def : Pat<(simple_store (i32 -1), addr:$dst), (OR32mi addr:$dst, -1)>;
def : Pat<(simple_store (i64 -1), addr:$dst), (OR64mi32 addr:$dst, -1)>;
}

// In kernel code model, we can get the address of a label
// into a register with 'movq'.  FIXME: This is a hack, the 'imm' predicate of
// the MOV64ri32 should accept these.
def : Pat<(i64 (X86Wrapper tconstpool  :$dst)),
          (MOV64ri32 tconstpool  :$dst)>, Requires<[KernelCode]>;
def : Pat<(i64 (X86Wrapper tjumptable  :$dst)),
          (MOV64ri32 tjumptable  :$dst)>, Requires<[KernelCode]>;
def : Pat<(i64 (X86Wrapper tglobaladdr :$dst)),
          (MOV64ri32 tglobaladdr :$dst)>, Requires<[KernelCode]>;
def : Pat<(i64 (X86Wrapper texternalsym:$dst)),
          (MOV64ri32 texternalsym:$dst)>, Requires<[KernelCode]>;
def : Pat<(i64 (X86Wrapper mcsym:$dst)),
          (MOV64ri32 mcsym:$dst)>, Requires<[KernelCode]>;
def : Pat<(i64 (X86Wrapper tblockaddress:$dst)),
          (MOV64ri32 tblockaddress:$dst)>, Requires<[KernelCode]>;

// If we have small model and -static mode, it is safe to store global addresses
// directly as immediates.  FIXME: This is really a hack, the 'imm' predicate
// for MOV64mi32 should handle this sort of thing.
def : Pat<(store (i64 (X86Wrapper tconstpool:$src)), addr:$dst),
          (MOV64mi32 addr:$dst, tconstpool:$src)>,
          Requires<[NearData, IsNotPIC]>;
def : Pat<(store (i64 (X86Wrapper tjumptable:$src)), addr:$dst),
          (MOV64mi32 addr:$dst, tjumptable:$src)>,
          Requires<[NearData, IsNotPIC]>;
def : Pat<(store (i64 (X86Wrapper tglobaladdr:$src)), addr:$dst),
          (MOV64mi32 addr:$dst, tglobaladdr:$src)>,
          Requires<[NearData, IsNotPIC]>;
def : Pat<(store (i64 (X86Wrapper texternalsym:$src)), addr:$dst),
          (MOV64mi32 addr:$dst, texternalsym:$src)>,
          Requires<[NearData, IsNotPIC]>;
def : Pat<(store (i64 (X86Wrapper mcsym:$src)), addr:$dst),
          (MOV64mi32 addr:$dst, mcsym:$src)>,
          Requires<[NearData, IsNotPIC]>;
def : Pat<(store (i64 (X86Wrapper tblockaddress:$src)), addr:$dst),
          (MOV64mi32 addr:$dst, tblockaddress:$src)>,
          Requires<[NearData, IsNotPIC]>;

def : Pat<(i32 (X86RecoverFrameAlloc mcsym:$dst)), (MOV32ri mcsym:$dst)>;
def : Pat<(i64 (X86RecoverFrameAlloc mcsym:$dst)), (MOV64ri mcsym:$dst)>;

// Calls

// tls has some funny stuff here...
// This corresponds to movabs $foo@tpoff, %rax
def : Pat<(i64 (X86Wrapper tglobaltlsaddr :$dst)),
          (MOV64ri32 tglobaltlsaddr :$dst)>;
// This corresponds to add $foo@tpoff, %rax
def : Pat<(add GR64:$src1, (X86Wrapper tglobaltlsaddr :$dst)),
          (ADD64ri32 GR64:$src1, tglobaltlsaddr :$dst)>;


// Direct PC relative function call for small code model. 32-bit displacement
// sign extended to 64-bit.
def : Pat<(X86call (i64 tglobaladdr:$dst)),
          (CALL64pcrel32 tglobaladdr:$dst)>;
def : Pat<(X86call (i64 texternalsym:$dst)),
          (CALL64pcrel32 texternalsym:$dst)>;

def : Pat<(X86call_rvmarker (i64 tglobaladdr:$rvfunc), (i64 texternalsym:$dst)),
          (CALL64pcrel32_RVMARKER tglobaladdr:$rvfunc, texternalsym:$dst)>;
def : Pat<(X86call_rvmarker (i64 tglobaladdr:$rvfunc), (i64 tglobaladdr:$dst)),
          (CALL64pcrel32_RVMARKER tglobaladdr:$rvfunc, tglobaladdr:$dst)>;


// Tailcall stuff. The TCRETURN instructions execute after the epilog, so they
// can never use callee-saved registers. That is the purpose of the GR64_TC
// register classes.
//
// The only volatile register that is never used by the calling convention is
// %r11. This happens when calling a vararg function with 6 arguments.
//
// Match an X86tcret that uses less than 7 volatile registers.
def : Pat<(X86tcret ptr_rc_tailcall:$dst, timm:$off),
          (TCRETURNri ptr_rc_tailcall:$dst, timm:$off)>,
          Requires<[Not64BitMode, NotUseIndirectThunkCalls]>;

// FIXME: This is disabled for 32-bit PIC mode because the global base
// register which is part of the address mode may be assigned a
// callee-saved register.
// Similar to X86tcret_6regs, here we only have 1 register left
def : Pat<(X86tcret_1reg (load addr:$dst), timm:$off),
          (TCRETURNmi addr:$dst, timm:$off)>,
          Requires<[Not64BitMode, IsNotPIC, NotUseIndirectThunkCalls]>;

def : Pat<(X86tcret (i32 tglobaladdr:$dst), timm:$off),
          (TCRETURNdi tglobaladdr:$dst, timm:$off)>,
          Requires<[NotLP64]>;

def : Pat<(X86tcret (i32 texternalsym:$dst), timm:$off),
          (TCRETURNdi texternalsym:$dst, timm:$off)>,
          Requires<[NotLP64]>;

def : Pat<(X86tcret ptr_rc_tailcall:$dst, timm:$off),
          (TCRETURNri64 ptr_rc_tailcall:$dst, timm:$off)>,
          Requires<[In64BitMode, NotUseIndirectThunkCalls]>;

// Don't fold loads into X86tcret requiring more than 6 regs.
// There wouldn't be enough scratch registers for base+index.
def : Pat<(X86tcret_6regs (load addr:$dst), timm:$off),
          (TCRETURNmi64 addr:$dst, timm:$off)>,
          Requires<[In64BitMode, NotUseIndirectThunkCalls]>;

def : Pat<(X86tcret ptr_rc_tailcall:$dst, timm:$off),
          (INDIRECT_THUNK_TCRETURN64 ptr_rc_tailcall:$dst, timm:$off)>,
          Requires<[In64BitMode, UseIndirectThunkCalls]>;

def : Pat<(X86tcret ptr_rc_tailcall:$dst, timm:$off),
          (INDIRECT_THUNK_TCRETURN32 ptr_rc_tailcall:$dst, timm:$off)>,
          Requires<[Not64BitMode, UseIndirectThunkCalls]>;

def : Pat<(X86tcret (i64 tglobaladdr:$dst), timm:$off),
          (TCRETURNdi64 tglobaladdr:$dst, timm:$off)>,
          Requires<[IsLP64]>;

def : Pat<(X86tcret (i64 texternalsym:$dst), timm:$off),
          (TCRETURNdi64 texternalsym:$dst, timm:$off)>,
          Requires<[IsLP64]>;

// Normal calls, with various flavors of addresses.
def : Pat<(X86call (i32 tglobaladdr:$dst)),
          (CALLpcrel32 tglobaladdr:$dst)>;
def : Pat<(X86call (i32 texternalsym:$dst)),
          (CALLpcrel32 texternalsym:$dst)>;
def : Pat<(X86call (i32 imm:$dst)),
          (CALLpcrel32 imm:$dst)>, Requires<[CallImmAddr]>;

// Comparisons.

// TEST R,R is smaller than CMP R,0
def : Pat<(X86cmp GR8:$src1, 0),
          (TEST8rr GR8:$src1, GR8:$src1)>;
def : Pat<(X86cmp GR16:$src1, 0),
          (TEST16rr GR16:$src1, GR16:$src1)>;
def : Pat<(X86cmp GR32:$src1, 0),
          (TEST32rr GR32:$src1, GR32:$src1)>;
def : Pat<(X86cmp GR64:$src1, 0),
          (TEST64rr GR64:$src1, GR64:$src1)>;

// zextload bool -> zextload byte
// i1 stored in one byte in zero-extended form.
// Upper bits cleanup should be executed before Store.
def : Pat<(zextloadi8i1  addr:$src), (MOV8rm addr:$src)>;
def : Pat<(zextloadi16i1 addr:$src),
          (EXTRACT_SUBREG (MOVZX32rm8 addr:$src), sub_16bit)>;
def : Pat<(zextloadi32i1 addr:$src), (MOVZX32rm8 addr:$src)>;
def : Pat<(zextloadi64i1 addr:$src),
          (SUBREG_TO_REG (i64 0), (MOVZX32rm8 addr:$src), sub_32bit)>;

// extload bool -> extload byte
// When extloading from 16-bit and smaller memory locations into 64-bit
// registers, use zero-extending loads so that the entire 64-bit register is
// defined, avoiding partial-register updates.

def : Pat<(extloadi8i1 addr:$src),   (MOV8rm      addr:$src)>;
def : Pat<(extloadi16i1 addr:$src),
          (EXTRACT_SUBREG (MOVZX32rm8 addr:$src), sub_16bit)>;
def : Pat<(extloadi32i1 addr:$src),  (MOVZX32rm8  addr:$src)>;
def : Pat<(extloadi16i8 addr:$src),
          (EXTRACT_SUBREG (MOVZX32rm8 addr:$src), sub_16bit)>;
def : Pat<(extloadi32i8 addr:$src),  (MOVZX32rm8  addr:$src)>;
def : Pat<(extloadi32i16 addr:$src), (MOVZX32rm16 addr:$src)>;

// For other extloads, use subregs, since the high contents of the register are
// defined after an extload.
// NOTE: The extloadi64i32 pattern needs to be first as it will try to form
// 32-bit loads for 4 byte aligned i8/i16 loads.
def : Pat<(extloadi64i32 addr:$src),
          (SUBREG_TO_REG (i64 0), (MOV32rm addr:$src), sub_32bit)>;
def : Pat<(extloadi64i1 addr:$src),
          (SUBREG_TO_REG (i64 0), (MOVZX32rm8 addr:$src), sub_32bit)>;
def : Pat<(extloadi64i8 addr:$src),
          (SUBREG_TO_REG (i64 0), (MOVZX32rm8 addr:$src), sub_32bit)>;
def : Pat<(extloadi64i16 addr:$src),
          (SUBREG_TO_REG (i64 0), (MOVZX32rm16 addr:$src), sub_32bit)>;

// anyext. Define these to do an explicit zero-extend to
// avoid partial-register updates.
def : Pat<(i16 (anyext GR8 :$src)), (EXTRACT_SUBREG
                                     (MOVZX32rr8 GR8 :$src), sub_16bit)>;
def : Pat<(i32 (anyext GR8 :$src)), (MOVZX32rr8  GR8 :$src)>;

// Except for i16 -> i32 since isel expect i16 ops to be promoted to i32.
def : Pat<(i32 (anyext GR16:$src)),
          (INSERT_SUBREG (i32 (IMPLICIT_DEF)), GR16:$src, sub_16bit)>;

def : Pat<(i64 (anyext GR8 :$src)),
          (SUBREG_TO_REG (i64 0), (MOVZX32rr8  GR8  :$src), sub_32bit)>;
def : Pat<(i64 (anyext GR16:$src)),
          (SUBREG_TO_REG (i64 0), (MOVZX32rr16 GR16 :$src), sub_32bit)>;
def : Pat<(i64 (anyext GR32:$src)),
          (INSERT_SUBREG (i64 (IMPLICIT_DEF)), GR32:$src, sub_32bit)>;

def : Pat<(i32 (anyext_sdiv GR8:$src)), (MOVSX32rr8 GR8:$src)>;

// In the case of a 32-bit def that is known to implicitly zero-extend,
// we can use a SUBREG_TO_REG.
def : Pat<(i64 (zext def32:$src)),
          (SUBREG_TO_REG (i64 0), GR32:$src, sub_32bit)>;
def : Pat<(i64 (and (anyext def32:$src), 0x00000000FFFFFFFF)),
          (SUBREG_TO_REG (i64 0), GR32:$src, sub_32bit)>;

//===----------------------------------------------------------------------===//
// Pattern match OR as ADD
//===----------------------------------------------------------------------===//

// If safe, we prefer to pattern match OR as ADD at isel time. ADD can be
// 3-addressified into an LEA instruction to avoid copies.  However, we also
// want to finally emit these instructions as an or at the end of the code
// generator to make the generated code easier to read.  To do this, we select
// into "disjoint bits" pseudo ops.

// (or x1, x2) -> (add x1, x2) if two operands are known not to share bits.
// Try this before the selecting to OR.
let SchedRW = [WriteALU] in {

let isConvertibleToThreeAddress = 1, isPseudo = 1,
    Constraints = "$src1 = $dst", Defs = [EFLAGS] in {
let isCommutable = 1 in {
def ADD8rr_DB   : I<0, Pseudo, (outs GR8:$dst), (ins GR8:$src1, GR8:$src2),
                    "", // orb/addb REG, REG
                    [(set GR8:$dst, (or_is_add GR8:$src1, GR8:$src2))]>;
def ADD16rr_DB  : I<0, Pseudo, (outs GR16:$dst), (ins GR16:$src1, GR16:$src2),
                    "", // orw/addw REG, REG
                    [(set GR16:$dst, (or_is_add GR16:$src1, GR16:$src2))]>;
def ADD32rr_DB  : I<0, Pseudo, (outs GR32:$dst), (ins GR32:$src1, GR32:$src2),
                    "", // orl/addl REG, REG
                    [(set GR32:$dst, (or_is_add GR32:$src1, GR32:$src2))]>;
def ADD64rr_DB  : I<0, Pseudo, (outs GR64:$dst), (ins GR64:$src1, GR64:$src2),
                    "", // orq/addq REG, REG
                    [(set GR64:$dst, (or_is_add GR64:$src1, GR64:$src2))]>;
} // isCommutable

def ADD8ri_DB :   I<0, Pseudo,
                    (outs GR8:$dst), (ins GR8:$src1, i8imm:$src2),
                    "", // orb/addb REG, imm8
                    [(set GR8:$dst, (or_is_add GR8:$src1, imm:$src2))]>;
def ADD16ri_DB  : I<0, Pseudo, (outs GR16:$dst), (ins GR16:$src1, i16imm:$src2),
                    "", // orw/addw REG, imm
                    [(set GR16:$dst, (or_is_add GR16:$src1, imm:$src2))]>;
def ADD32ri_DB  : I<0, Pseudo, (outs GR32:$dst), (ins GR32:$src1, i32imm:$src2),
                    "", // orl/addl REG, imm
                    [(set GR32:$dst, (or_is_add GR32:$src1, imm:$src2))]>;
def ADD64ri32_DB : I<0, Pseudo,
                     (outs GR64:$dst), (ins GR64:$src1, i64i32imm:$src2),
                     "", // orq/addq REG, imm
                     [(set GR64:$dst, (or_is_add GR64:$src1,
                                                 i64immSExt32:$src2))]>;
}
} // AddedComplexity, SchedRW

//===----------------------------------------------------------------------===//
// Pattern match XOR as ADD
//===----------------------------------------------------------------------===//

// Prefer to pattern match XOR with min_signed_value as ADD at isel time.
// ADD can be 3-addressified into an LEA instruction to avoid copies.
let AddedComplexity = 5 in {
def : Pat<(xor GR8:$src1, -128),
          (ADD8ri GR8:$src1, -128)>;
def : Pat<(xor GR16:$src1, -32768),
          (ADD16ri GR16:$src1, -32768)>;
def : Pat<(xor GR32:$src1, -2147483648),
          (ADD32ri GR32:$src1, -2147483648)>;
}

//===----------------------------------------------------------------------===//
// Some peepholes
//===----------------------------------------------------------------------===//

// Odd encoding trick: -128 fits into an 8-bit immediate field while
// +128 doesn't, so in this special case use a sub instead of an add.
let Predicates = [NoNDD] in {
  def : Pat<(add GR16:$src1, 128),
            (SUB16ri GR16:$src1, -128)>;
  def : Pat<(add GR32:$src1, 128),
            (SUB32ri GR32:$src1, -128)>;
  def : Pat<(add GR64:$src1, 128),
            (SUB64ri32 GR64:$src1, -128)>;

  def : Pat<(X86add_flag_nocf GR16:$src1, 128),
            (SUB16ri GR16:$src1, -128)>;
  def : Pat<(X86add_flag_nocf GR32:$src1, 128),
            (SUB32ri GR32:$src1, -128)>;
  def : Pat<(X86add_flag_nocf GR64:$src1, 128),
            (SUB64ri32 GR64:$src1, -128)>;
}
let Predicates = [HasNDD] in {
  def : Pat<(add GR16:$src1, 128),
            (SUB16ri_ND GR16:$src1, -128)>;
  def : Pat<(add GR32:$src1, 128),
            (SUB32ri_ND GR32:$src1, -128)>;
  def : Pat<(add GR64:$src1, 128),
            (SUB64ri32_ND GR64:$src1, -128)>;

  def : Pat<(X86add_flag_nocf GR16:$src1, 128),
            (SUB16ri_ND GR16:$src1, -128)>;
  def : Pat<(X86add_flag_nocf GR32:$src1, 128),
            (SUB32ri_ND GR32:$src1, -128)>;
  def : Pat<(X86add_flag_nocf GR64:$src1, 128),
            (SUB64ri32_ND GR64:$src1, -128)>;
}
def : Pat<(store (add (loadi16 addr:$dst), 128), addr:$dst),
          (SUB16mi addr:$dst, -128)>;
def : Pat<(store (add (loadi32 addr:$dst), 128), addr:$dst),
          (SUB32mi addr:$dst, -128)>;
def : Pat<(store (add (loadi64 addr:$dst), 128), addr:$dst),
          (SUB64mi32 addr:$dst, -128)>;
let Predicates = [HasNDD] in {
  def : Pat<(add (loadi16 addr:$src), 128),
            (SUB16mi_ND addr:$src, -128)>;
  def : Pat<(add (loadi32 addr:$src), 128),
            (SUB32mi_ND addr:$src, -128)>;
  def : Pat<(add (loadi64 addr:$src), 128),
            (SUB64mi32_ND addr:$src, -128)>;
}

// The same trick applies for 32-bit immediate fields in 64-bit
// instructions.
let Predicates = [NoNDD] in {
  def : Pat<(add GR64:$src1, 0x0000000080000000),
            (SUB64ri32 GR64:$src1, 0xffffffff80000000)>;
  def : Pat<(X86add_flag_nocf GR64:$src1, 0x0000000080000000),
            (SUB64ri32 GR64:$src1, 0xffffffff80000000)>;
}
let Predicates = [HasNDD] in {
  def : Pat<(add GR64:$src1, 0x0000000080000000),
            (SUB64ri32_ND GR64:$src1, 0xffffffff80000000)>;
  def : Pat<(X86add_flag_nocf GR64:$src1, 0x0000000080000000),
            (SUB64ri32_ND GR64:$src1, 0xffffffff80000000)>;
}
def : Pat<(store (add (loadi64 addr:$dst), 0x0000000080000000), addr:$dst),
          (SUB64mi32 addr:$dst, 0xffffffff80000000)>;
let Predicates = [HasNDD] in {
  def : Pat<(add(loadi64 addr:$src), 0x0000000080000000),
            (SUB64mi32_ND addr:$src, 0xffffffff80000000)>;
}

// Depositing value to 8/16 bit subreg:
def : Pat<(or (and GR64:$dst, -256),
              (i64 (zextloadi8 addr:$src))),
          (INSERT_SUBREG (i64 (COPY $dst)), (MOV8rm  i8mem:$src), sub_8bit)>;

def : Pat<(or (and GR32:$dst, -256),
              (i32 (zextloadi8 addr:$src))),
          (INSERT_SUBREG (i32 (COPY $dst)), (MOV8rm  i8mem:$src), sub_8bit)>;

def : Pat<(or (and GR64:$dst, -65536),
              (i64 (zextloadi16 addr:$src))),
          (INSERT_SUBREG (i64 (COPY $dst)), (MOV16rm  i16mem:$src), sub_16bit)>;

def : Pat<(or (and GR32:$dst, -65536),
              (i32 (zextloadi16 addr:$src))),
          (INSERT_SUBREG (i32 (COPY $dst)), (MOV16rm  i16mem:$src), sub_16bit)>;

// To avoid needing to materialize an immediate in a register, use a 32-bit and
// with implicit zero-extension instead of a 64-bit and if the immediate has at
// least 32 bits of leading zeros. If in addition the last 32 bits can be
// represented with a sign extension of a 8 bit constant, use that.
// This can also reduce instruction size by eliminating the need for the REX
// prefix.

// AddedComplexity is needed to give priority over i64immSExt8 and i64immSExt32.
let AddedComplexity = 1 in {
  let Predicates = [NoNDD] in {
    def : Pat<(and GR64:$src, i64immZExt32:$imm),
              (SUBREG_TO_REG
                (i64 0),
                (AND32ri
                  (EXTRACT_SUBREG GR64:$src, sub_32bit),
                  (i32 (GetLo32XForm imm:$imm))),
                sub_32bit)>;
  }
  let Predicates = [HasNDD] in {
    def : Pat<(and GR64:$src, i64immZExt32:$imm),
              (SUBREG_TO_REG
                (i64 0),
                (AND32ri_ND
                  (EXTRACT_SUBREG GR64:$src, sub_32bit),
                  (i32 (GetLo32XForm imm:$imm))),
                sub_32bit)>;
  }
} // AddedComplexity = 1


// AddedComplexity is needed due to the increased complexity on the
// i64immZExt32SExt8 and i64immZExt32 patterns above. Applying this to all
// the MOVZX patterns keeps thems together in DAGIsel tables.
let AddedComplexity = 1 in {
// r & (2^16-1) ==> movz
def : Pat<(and GR32:$src1, 0xffff),
          (MOVZX32rr16 (EXTRACT_SUBREG GR32:$src1, sub_16bit))>;
// r & (2^8-1) ==> movz
def : Pat<(and GR32:$src1, 0xff),
          (MOVZX32rr8 (EXTRACT_SUBREG GR32:$src1, sub_8bit))>;
// r & (2^8-1) ==> movz
def : Pat<(and GR16:$src1, 0xff),
           (EXTRACT_SUBREG (MOVZX32rr8 (EXTRACT_SUBREG GR16:$src1, sub_8bit)),
             sub_16bit)>;

// r & (2^32-1) ==> movz
def : Pat<(and GR64:$src, 0x00000000FFFFFFFF),
          (SUBREG_TO_REG (i64 0),
                         (MOV32rr (EXTRACT_SUBREG GR64:$src, sub_32bit)),
                         sub_32bit)>;
// r & (2^16-1) ==> movz
def : Pat<(and GR64:$src, 0xffff),
          (SUBREG_TO_REG (i64 0),
                      (MOVZX32rr16 (i16 (EXTRACT_SUBREG GR64:$src, sub_16bit))),
                      sub_32bit)>;
// r & (2^8-1) ==> movz
def : Pat<(and GR64:$src, 0xff),
          (SUBREG_TO_REG (i64 0),
                         (MOVZX32rr8 (i8 (EXTRACT_SUBREG GR64:$src, sub_8bit))),
                         sub_32bit)>;
} // AddedComplexity = 1


// Try to use BTS/BTR/BTC for single bit operations on the upper 32-bits.

def BTRXForm : SDNodeXForm<imm, [{
  // Transformation function: Find the lowest 0.
  return getI64Imm((uint8_t)N->getAPIntValue().countr_one(), SDLoc(N));
}]>;

def BTCBTSXForm : SDNodeXForm<imm, [{
  // Transformation function: Find the lowest 1.
  return getI64Imm((uint8_t)N->getAPIntValue().countr_zero(), SDLoc(N));
}]>;

def BTRMask64 : ImmLeaf<i64, [{
  return !isUInt<32>(Imm) && !isInt<32>(Imm) && isPowerOf2_64(~Imm);
}]>;

def BTCBTSMask64 : ImmLeaf<i64, [{
  return !isInt<32>(Imm) && isPowerOf2_64(Imm);
}]>;

// For now only do this for optsize.
let AddedComplexity = 1, Predicates=[OptForSize] in {
  def : Pat<(and GR64:$src1, BTRMask64:$mask),
            (BTR64ri8 GR64:$src1, (BTRXForm imm:$mask))>;
  def : Pat<(or GR64:$src1, BTCBTSMask64:$mask),
            (BTS64ri8 GR64:$src1, (BTCBTSXForm imm:$mask))>;
  def : Pat<(xor GR64:$src1, BTCBTSMask64:$mask),
            (BTC64ri8 GR64:$src1, (BTCBTSXForm imm:$mask))>;
}


// sext_inreg patterns
def : Pat<(sext_inreg GR32:$src, i16),
          (MOVSX32rr16 (EXTRACT_SUBREG GR32:$src, sub_16bit))>;
def : Pat<(sext_inreg GR32:$src, i8),
          (MOVSX32rr8 (EXTRACT_SUBREG GR32:$src, sub_8bit))>;

def : Pat<(sext_inreg GR16:$src, i8),
           (EXTRACT_SUBREG (MOVSX32rr8 (EXTRACT_SUBREG GR16:$src, sub_8bit)),
             sub_16bit)>;

def : Pat<(sext_inreg GR64:$src, i32),
          (MOVSX64rr32 (EXTRACT_SUBREG GR64:$src, sub_32bit))>;
def : Pat<(sext_inreg GR64:$src, i16),
          (MOVSX64rr16 (EXTRACT_SUBREG GR64:$src, sub_16bit))>;
def : Pat<(sext_inreg GR64:$src, i8),
          (MOVSX64rr8 (EXTRACT_SUBREG GR64:$src, sub_8bit))>;

// sext, sext_load, zext, zext_load
def: Pat<(i16 (sext GR8:$src)),
          (EXTRACT_SUBREG (MOVSX32rr8 GR8:$src), sub_16bit)>;
def: Pat<(sextloadi16i8 addr:$src),
          (EXTRACT_SUBREG (MOVSX32rm8 addr:$src), sub_16bit)>;
def: Pat<(i16 (zext GR8:$src)),
          (EXTRACT_SUBREG (MOVZX32rr8 GR8:$src), sub_16bit)>;
def: Pat<(zextloadi16i8 addr:$src),
          (EXTRACT_SUBREG (MOVZX32rm8 addr:$src), sub_16bit)>;

// trunc patterns
def : Pat<(i16 (trunc GR32:$src)),
          (EXTRACT_SUBREG GR32:$src, sub_16bit)>;
def : Pat<(i8 (trunc GR32:$src)),
          (EXTRACT_SUBREG (i32 (COPY_TO_REGCLASS GR32:$src, GR32_ABCD)),
                          sub_8bit)>,
      Requires<[Not64BitMode]>;
def : Pat<(i8 (trunc GR16:$src)),
          (EXTRACT_SUBREG (i16 (COPY_TO_REGCLASS GR16:$src, GR16_ABCD)),
                          sub_8bit)>,
      Requires<[Not64BitMode]>;
def : Pat<(i32 (trunc GR64:$src)),
          (EXTRACT_SUBREG GR64:$src, sub_32bit)>;
def : Pat<(i16 (trunc GR64:$src)),
          (EXTRACT_SUBREG GR64:$src, sub_16bit)>;
def : Pat<(i8 (trunc GR64:$src)),
          (EXTRACT_SUBREG GR64:$src, sub_8bit)>;
def : Pat<(i8 (trunc GR32:$src)),
          (EXTRACT_SUBREG GR32:$src, sub_8bit)>,
      Requires<[In64BitMode]>;
def : Pat<(i8 (trunc GR16:$src)),
          (EXTRACT_SUBREG GR16:$src, sub_8bit)>,
      Requires<[In64BitMode]>;

def immff00_ffff  : ImmLeaf<i32, [{
  return Imm >= 0xff00 && Imm <= 0xffff;
}]>;

// h-register tricks
def : Pat<(i8 (trunc (srl_su GR16:$src, (i8 8)))),
          (EXTRACT_SUBREG GR16:$src, sub_8bit_hi)>,
      Requires<[Not64BitMode]>;
def : Pat<(i8 (trunc (srl_su (i32 (anyext GR16:$src)), (i8 8)))),
          (EXTRACT_SUBREG GR16:$src, sub_8bit_hi)>,
      Requires<[Not64BitMode]>;
def : Pat<(i8 (trunc (srl_su GR32:$src, (i8 8)))),
          (EXTRACT_SUBREG GR32:$src, sub_8bit_hi)>,
      Requires<[Not64BitMode]>;
def : Pat<(srl GR16:$src, (i8 8)),
          (EXTRACT_SUBREG
            (MOVZX32rr8_NOREX (EXTRACT_SUBREG GR16:$src, sub_8bit_hi)),
            sub_16bit)>;
def : Pat<(i32 (zext (srl_su GR16:$src, (i8 8)))),
          (MOVZX32rr8_NOREX (EXTRACT_SUBREG GR16:$src, sub_8bit_hi))>;
def : Pat<(i32 (anyext (srl_su GR16:$src, (i8 8)))),
          (MOVZX32rr8_NOREX (EXTRACT_SUBREG GR16:$src, sub_8bit_hi))>;
def : Pat<(and (srl_su GR32:$src, (i8 8)), (i32 255)),
          (MOVZX32rr8_NOREX (EXTRACT_SUBREG GR32:$src, sub_8bit_hi))>;
def : Pat<(srl (and_su GR32:$src, immff00_ffff), (i8 8)),
          (MOVZX32rr8_NOREX (EXTRACT_SUBREG GR32:$src, sub_8bit_hi))>;

// h-register tricks.
// For now, be conservative on x86-64 and use an h-register extract only if the
// value is immediately zero-extended or stored, which are somewhat common
// cases. This uses a bunch of code to prevent a register requiring a REX prefix
// from being allocated in the same instruction as the h register, as there's
// currently no way to describe this requirement to the register allocator.

// h-register extract and zero-extend.
def : Pat<(and (srl_su GR64:$src, (i8 8)), (i64 255)),
          (SUBREG_TO_REG
            (i64 0),
            (MOVZX32rr8_NOREX
              (EXTRACT_SUBREG GR64:$src, sub_8bit_hi)),
            sub_32bit)>;
def : Pat<(i64 (zext (srl_su GR16:$src, (i8 8)))),
          (SUBREG_TO_REG
            (i64 0),
            (MOVZX32rr8_NOREX
              (EXTRACT_SUBREG GR16:$src, sub_8bit_hi)),
            sub_32bit)>;
def : Pat<(i64 (anyext (srl_su GR16:$src, (i8 8)))),
          (SUBREG_TO_REG
            (i64 0),
            (MOVZX32rr8_NOREX
              (EXTRACT_SUBREG GR16:$src, sub_8bit_hi)),
            sub_32bit)>;

// h-register extract and store.
def : Pat<(store (i8 (trunc_su (srl_su GR64:$src, (i8 8)))), addr:$dst),
          (MOV8mr_NOREX
            addr:$dst,
            (EXTRACT_SUBREG GR64:$src, sub_8bit_hi))>;
def : Pat<(store (i8 (trunc_su (srl_su GR32:$src, (i8 8)))), addr:$dst),
          (MOV8mr_NOREX
            addr:$dst,
            (EXTRACT_SUBREG GR32:$src, sub_8bit_hi))>,
      Requires<[In64BitMode]>;
def : Pat<(store (i8 (trunc_su (srl_su GR16:$src, (i8 8)))), addr:$dst),
          (MOV8mr_NOREX
            addr:$dst,
            (EXTRACT_SUBREG GR16:$src, sub_8bit_hi))>,
      Requires<[In64BitMode]>;

// Special pattern to catch the last step of __builtin_parity handling. Our
// goal is to use an xor of an h-register with the corresponding l-register.
// The above patterns would handle this on non 64-bit targets, but for 64-bit
// we need to be more careful. We're using a NOREX instruction here in case
// register allocation fails to keep the two registers together. So we need to
// make sure we can't accidentally mix R8-R15 with an h-register.
def : Pat<(X86xor_flag (i8 (trunc GR32:$src)),
                       (i8 (trunc (srl_su GR32:$src, (i8 8))))),
          (XOR8rr_NOREX (EXTRACT_SUBREG GR32:$src, sub_8bit),
                        (EXTRACT_SUBREG GR32:$src, sub_8bit_hi))>;

// (shl x, 1) ==> (add x, x)
// Note that if x is undef (immediate or otherwise), we could theoretically
// end up with the two uses of x getting different values, producing a result
// where the least significant bit is not 0. However, the probability of this
// happening is considered low enough that this is officially not a
// "real problem".
let Predicates = [NoNDD] in {
  def : Pat<(shl GR8 :$src1, (i8 1)), (ADD8rr  GR8 :$src1, GR8 :$src1)>;
  def : Pat<(shl GR16:$src1, (i8 1)), (ADD16rr GR16:$src1, GR16:$src1)>;
  def : Pat<(shl GR32:$src1, (i8 1)), (ADD32rr GR32:$src1, GR32:$src1)>;
  def : Pat<(shl GR64:$src1, (i8 1)), (ADD64rr GR64:$src1, GR64:$src1)>;
}
let Predicates = [HasNDD] in {
  def : Pat<(shl GR8 :$src1, (i8 1)), (ADD8rr_ND  GR8 :$src1, GR8 :$src1)>;
  def : Pat<(shl GR16:$src1, (i8 1)), (ADD16rr_ND GR16:$src1, GR16:$src1)>;
  def : Pat<(shl GR32:$src1, (i8 1)), (ADD32rr_ND GR32:$src1, GR32:$src1)>;
  def : Pat<(shl GR64:$src1, (i8 1)), (ADD64rr_ND GR64:$src1, GR64:$src1)>;
}

// Shift amount is implicitly masked.
multiclass MaskedShiftAmountPats<SDNode frag> {
  // (shift x (and y, 31)) ==> (shift x, y)
  // (shift x (and y, 63)) ==> (shift x, y)
  let Predicates = [NoNDD] in {
    def : Pat<(frag GR8:$src1, (shiftMask32 CL)),
              (!cast<Instruction>(NAME # "8rCL") GR8:$src1)>;
    def : Pat<(frag GR16:$src1, (shiftMask32 CL)),
              (!cast<Instruction>(NAME # "16rCL") GR16:$src1)>;
    def : Pat<(frag GR32:$src1, (shiftMask32 CL)),
              (!cast<Instruction>(NAME # "32rCL") GR32:$src1)>;
    def : Pat<(frag GR64:$src1, (shiftMask64 CL)),
              (!cast<Instruction>(NAME # "64rCL") GR64:$src1)>;
  }
  let Predicates = [HasNDD] in {
    def : Pat<(frag GR8:$src1, (shiftMask32 CL)),
              (!cast<Instruction>(NAME # "8rCL_ND") GR8:$src1)>;
    def : Pat<(frag GR16:$src1, (shiftMask32 CL)),
              (!cast<Instruction>(NAME # "16rCL_ND") GR16:$src1)>;
    def : Pat<(frag GR32:$src1, (shiftMask32 CL)),
              (!cast<Instruction>(NAME # "32rCL_ND") GR32:$src1)>;
    def : Pat<(frag GR64:$src1, (shiftMask64 CL)),
              (!cast<Instruction>(NAME # "64rCL_ND") GR64:$src1)>;
  }

  def : Pat<(store (frag (loadi8 addr:$dst), (shiftMask32 CL)), addr:$dst),
            (!cast<Instruction>(NAME # "8mCL") addr:$dst)>;
  def : Pat<(store (frag (loadi16 addr:$dst), (shiftMask32 CL)), addr:$dst),
            (!cast<Instruction>(NAME # "16mCL") addr:$dst)>;
  def : Pat<(store (frag (loadi32 addr:$dst), (shiftMask32 CL)), addr:$dst),
            (!cast<Instruction>(NAME # "32mCL") addr:$dst)>;
  def : Pat<(store (frag (loadi64 addr:$dst), (shiftMask64 CL)), addr:$dst),
            (!cast<Instruction>(NAME # "64mCL") addr:$dst)>;

  let Predicates = [HasNDD] in {
    def : Pat<(frag (loadi8 addr:$src), (shiftMask32 CL)),
              (!cast<Instruction>(NAME # "8mCL_ND") addr:$src)>;
    def : Pat<(frag (loadi16 addr:$src), (shiftMask32 CL)),
              (!cast<Instruction>(NAME # "16mCL_ND") addr:$src)>;
    def : Pat<(frag (loadi32 addr:$src), (shiftMask32 CL)),
              (!cast<Instruction>(NAME # "32mCL_ND") addr:$src)>;
    def : Pat<(frag (loadi64 addr:$src), (shiftMask64 CL)),
              (!cast<Instruction>(NAME # "64mCL_ND") addr:$src)>;
  }
}

defm SHL : MaskedShiftAmountPats<shl>;
defm SHR : MaskedShiftAmountPats<srl>;
defm SAR : MaskedShiftAmountPats<sra>;

// ROL/ROR instructions allow a stronger mask optimization than shift for 8- and
// 16-bit. We can remove a mask of any (bitwidth - 1) on the rotation amount
// because over-rotating produces the same result. This is noted in the Intel
// docs with: "tempCOUNT <- (COUNT & COUNTMASK) MOD SIZE". Masking the rotation
// amount could affect EFLAGS results, but that does not matter because we are
// not tracking flags for these nodes.
multiclass MaskedRotateAmountPats<SDNode frag> {
  // (rot x (and y, BitWidth - 1)) ==> (rot x, y)
  let Predicates = [NoNDD] in {
    def : Pat<(frag GR8:$src1, (shiftMask8 CL)),
              (!cast<Instruction>(NAME # "8rCL") GR8:$src1)>;
    def : Pat<(frag GR16:$src1, (shiftMask16 CL)),
              (!cast<Instruction>(NAME # "16rCL") GR16:$src1)>;
    def : Pat<(frag GR32:$src1, (shiftMask32 CL)),
              (!cast<Instruction>(NAME # "32rCL") GR32:$src1)>;
    def : Pat<(frag GR64:$src1, (shiftMask64 CL)),
              (!cast<Instruction>(NAME # "64rCL") GR64:$src1)>;
  }
  let Predicates = [HasNDD] in {
    def : Pat<(frag GR8:$src1, (shiftMask8 CL)),
              (!cast<Instruction>(NAME # "8rCL_ND") GR8:$src1)>;
    def : Pat<(frag GR16:$src1, (shiftMask16 CL)),
              (!cast<Instruction>(NAME # "16rCL_ND") GR16:$src1)>;
    def : Pat<(frag GR32:$src1, (shiftMask32 CL)),
              (!cast<Instruction>(NAME # "32rCL_ND") GR32:$src1)>;
    def : Pat<(frag GR64:$src1, (shiftMask64 CL)),
              (!cast<Instruction>(NAME # "64rCL_ND") GR64:$src1)>;
  }

  def : Pat<(store (frag (loadi8 addr:$dst), (shiftMask8 CL)), addr:$dst),
            (!cast<Instruction>(NAME # "8mCL") addr:$dst)>;
  def : Pat<(store (frag (loadi16 addr:$dst), (shiftMask16 CL)), addr:$dst),
            (!cast<Instruction>(NAME # "16mCL") addr:$dst)>;
  def : Pat<(store (frag (loadi32 addr:$dst), (shiftMask32 CL)), addr:$dst),
            (!cast<Instruction>(NAME # "32mCL") addr:$dst)>;
  def : Pat<(store (frag (loadi64 addr:$dst), (shiftMask64 CL)), addr:$dst),
            (!cast<Instruction>(NAME # "64mCL") addr:$dst)>;

  let Predicates = [HasNDD] in {
    def : Pat<(frag (loadi8 addr:$src), (shiftMask8 CL)),
              (!cast<Instruction>(NAME # "8mCL_ND") addr:$src)>;
    def : Pat<(frag (loadi16 addr:$src), (shiftMask16 CL)),
              (!cast<Instruction>(NAME # "16mCL_ND") addr:$src)>;
    def : Pat<(frag (loadi32 addr:$src), (shiftMask32 CL)),
              (!cast<Instruction>(NAME # "32mCL_ND") addr:$src)>;
    def : Pat<(frag (loadi64 addr:$src), (shiftMask64 CL)),
              (!cast<Instruction>(NAME # "64mCL_ND") addr:$src)>;
  }
}

defm ROL : MaskedRotateAmountPats<rotl>;
defm ROR : MaskedRotateAmountPats<rotr>;

multiclass MaskedShlrdAmountPats<string suffix, Predicate p> {
  let Predicates = [p] in {
    // Double "funnel" shift amount is implicitly masked.
    // (fshl/fshr x (and y, 31)) ==> (fshl/fshr x, y) (NOTE: modulo32)
    def : Pat<(X86fshl GR16:$src1, GR16:$src2, (shiftMask32 CL)),
              (!cast<Instruction>(SHLD16rrCL#suffix) GR16:$src1, GR16:$src2)>;
    def : Pat<(X86fshr GR16:$src2, GR16:$src1, (shiftMask32 CL)),
              (!cast<Instruction>(SHRD16rrCL#suffix) GR16:$src1, GR16:$src2)>;

    // (fshl/fshr x (and y, 31)) ==> (fshl/fshr x, y)
    def : Pat<(fshl GR32:$src1, GR32:$src2, (shiftMask32 CL)),
              (!cast<Instruction>(SHLD32rrCL#suffix) GR32:$src1, GR32:$src2)>;
    def : Pat<(fshr GR32:$src2, GR32:$src1, (shiftMask32 CL)),
              (!cast<Instruction>(SHRD32rrCL#suffix) GR32:$src1, GR32:$src2)>;

    // (fshl/fshr x (and y, 63)) ==> (fshl/fshr x, y)
    def : Pat<(fshl GR64:$src1, GR64:$src2, (shiftMask64 CL)),
              (!cast<Instruction>(SHLD64rrCL#suffix) GR64:$src1, GR64:$src2)>;
    def : Pat<(fshr GR64:$src2, GR64:$src1, (shiftMask64 CL)),
              (!cast<Instruction>(SHRD64rrCL#suffix) GR64:$src1, GR64:$src2)>;
  }
}

defm : MaskedShlrdAmountPats<"", NoNDD>;
defm : MaskedShlrdAmountPats<"_ND", HasNDD>;

// Use BTR/BTS/BTC for clearing/setting/toggling a bit in a variable location.
multiclass OneBitPats<RegisterClass rc, ValueType vt, Instruction btr,
                      Instruction bts, Instruction btc, PatFrag mask> {
  def : Pat<(and rc:$src1, (rotl -2, GR8:$src2)),
            (btr rc:$src1,
                 (INSERT_SUBREG (vt (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
  def : Pat<(or rc:$src1, (shl 1, GR8:$src2)),
            (bts rc:$src1,
                 (INSERT_SUBREG (vt (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
  def : Pat<(xor rc:$src1, (shl 1, GR8:$src2)),
            (btc rc:$src1,
                 (INSERT_SUBREG (vt (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;

  // Similar to above, but removing unneeded masking of the shift amount.
  def : Pat<(and rc:$src1, (rotl -2, (mask GR8:$src2))),
            (btr rc:$src1,
                 (INSERT_SUBREG (vt (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
  def : Pat<(or rc:$src1, (shl 1, (mask GR8:$src2))),
            (bts rc:$src1,
                (INSERT_SUBREG (vt (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
  def : Pat<(xor rc:$src1, (shl 1, (mask GR8:$src2))),
            (btc rc:$src1,
                (INSERT_SUBREG (vt (IMPLICIT_DEF)), GR8:$src2, sub_8bit))>;
}

defm : OneBitPats<GR16, i16, BTR16rr, BTS16rr, BTC16rr, shiftMask16>;
defm : OneBitPats<GR32, i32, BTR32rr, BTS32rr, BTC32rr, shiftMask32>;
defm : OneBitPats<GR64, i64, BTR64rr, BTS64rr, BTC64rr, shiftMask64>;

//===----------------------------------------------------------------------===//
// EFLAGS-defining Patterns
//===----------------------------------------------------------------------===//

multiclass EFLAGSDefiningPats<string suffix, Predicate p> {
  let Predicates = [p] in {
    // add reg, reg
    def : Pat<(add GR8 :$src1, GR8 :$src2), (!cast<Instruction>(ADD8rr#suffix) GR8 :$src1, GR8 :$src2)>;
    def : Pat<(add GR16:$src1, GR16:$src2), (!cast<Instruction>(ADD16rr#suffix) GR16:$src1, GR16:$src2)>;
    def : Pat<(add GR32:$src1, GR32:$src2), (!cast<Instruction>(ADD32rr#suffix) GR32:$src1, GR32:$src2)>;
    def : Pat<(add GR64:$src1, GR64:$src2), (!cast<Instruction>(ADD64rr#suffix) GR64:$src1, GR64:$src2)>;

    // add reg, mem
    def : Pat<(add GR8:$src1, (loadi8 addr:$src2)),
              (!cast<Instruction>(ADD8rm#suffix) GR8:$src1, addr:$src2)>;
    def : Pat<(add GR16:$src1, (loadi16 addr:$src2)),
              (!cast<Instruction>(ADD16rm#suffix) GR16:$src1, addr:$src2)>;
    def : Pat<(add GR32:$src1, (loadi32 addr:$src2)),
              (!cast<Instruction>(ADD32rm#suffix) GR32:$src1, addr:$src2)>;
    def : Pat<(add GR64:$src1, (loadi64 addr:$src2)),
              (!cast<Instruction>(ADD64rm#suffix) GR64:$src1, addr:$src2)>;

    // add reg, imm
    def : Pat<(add GR8 :$src1, imm:$src2), (!cast<Instruction>(ADD8ri#suffix) GR8:$src1 , imm:$src2)>;
    def : Pat<(add GR16:$src1, imm:$src2), (!cast<Instruction>(ADD16ri#suffix) GR16:$src1, imm:$src2)>;
    def : Pat<(add GR32:$src1, imm:$src2), (!cast<Instruction>(ADD32ri#suffix) GR32:$src1, imm:$src2)>;
    def : Pat<(add GR64:$src1, i64immSExt32:$src2), (!cast<Instruction>(ADD64ri32#suffix) GR64:$src1, i64immSExt32:$src2)>;

    // sub reg, reg
    def : Pat<(sub GR8 :$src1, GR8 :$src2), (!cast<Instruction>(SUB8rr#suffix)  GR8 :$src1, GR8 :$src2)>;
    def : Pat<(sub GR16:$src1, GR16:$src2), (!cast<Instruction>(SUB16rr#suffix) GR16:$src1, GR16:$src2)>;
    def : Pat<(sub GR32:$src1, GR32:$src2), (!cast<Instruction>(SUB32rr#suffix) GR32:$src1, GR32:$src2)>;
    def : Pat<(sub GR64:$src1, GR64:$src2), (!cast<Instruction>(SUB64rr#suffix) GR64:$src1, GR64:$src2)>;

    // sub reg, mem
    def : Pat<(sub GR8:$src1, (loadi8 addr:$src2)),
              (!cast<Instruction>(SUB8rm#suffix) GR8:$src1, addr:$src2)>;
    def : Pat<(sub GR16:$src1, (loadi16 addr:$src2)),
              (!cast<Instruction>(SUB16rm#suffix) GR16:$src1, addr:$src2)>;
    def : Pat<(sub GR32:$src1, (loadi32 addr:$src2)),
              (!cast<Instruction>(SUB32rm#suffix) GR32:$src1, addr:$src2)>;
    def : Pat<(sub GR64:$src1, (loadi64 addr:$src2)),
              (!cast<Instruction>(SUB64rm#suffix) GR64:$src1, addr:$src2)>;

    // sub reg, imm
    def : Pat<(sub GR8:$src1, imm:$src2),
              (!cast<Instruction>(SUB8ri#suffix) GR8:$src1, imm:$src2)>;
    def : Pat<(sub GR16:$src1, imm:$src2),
              (!cast<Instruction>(SUB16ri#suffix) GR16:$src1, imm:$src2)>;
    def : Pat<(sub GR32:$src1, imm:$src2),
              (!cast<Instruction>(SUB32ri#suffix) GR32:$src1, imm:$src2)>;
    def : Pat<(sub GR64:$src1, i64immSExt32:$src2),
              (!cast<Instruction>(SUB64ri32#suffix) GR64:$src1, i64immSExt32:$src2)>;

    // sub 0, reg
    def : Pat<(X86sub_flag 0, GR8 :$src), (!cast<Instruction>(NEG8r#suffix)  GR8 :$src)>;
    def : Pat<(X86sub_flag 0, GR16:$src), (!cast<Instruction>(NEG16r#suffix) GR16:$src)>;
    def : Pat<(X86sub_flag 0, GR32:$src), (!cast<Instruction>(NEG32r#suffix) GR32:$src)>;
    def : Pat<(X86sub_flag 0, GR64:$src), (!cast<Instruction>(NEG64r#suffix) GR64:$src)>;

    // mul reg, reg
    def : Pat<(mul GR16:$src1, GR16:$src2),
              (!cast<Instruction>(IMUL16rr#suffix) GR16:$src1, GR16:$src2)>;
    def : Pat<(mul GR32:$src1, GR32:$src2),
              (!cast<Instruction>(IMUL32rr#suffix) GR32:$src1, GR32:$src2)>;
    def : Pat<(mul GR64:$src1, GR64:$src2),
              (!cast<Instruction>(IMUL64rr#suffix) GR64:$src1, GR64:$src2)>;

    // mul reg, mem
    def : Pat<(mul GR16:$src1, (loadi16 addr:$src2)),
              (!cast<Instruction>(IMUL16rm#suffix) GR16:$src1, addr:$src2)>;
    def : Pat<(mul GR32:$src1, (loadi32 addr:$src2)),
              (!cast<Instruction>(IMUL32rm#suffix) GR32:$src1, addr:$src2)>;
    def : Pat<(mul GR64:$src1, (loadi64 addr:$src2)),
              (!cast<Instruction>(IMUL64rm#suffix) GR64:$src1, addr:$src2)>;

    // or reg/reg.
    def : Pat<(or GR8 :$src1, GR8 :$src2), (!cast<Instruction>(OR8rr#suffix)  GR8 :$src1, GR8 :$src2)>;
    def : Pat<(or GR16:$src1, GR16:$src2), (!cast<Instruction>(OR16rr#suffix) GR16:$src1, GR16:$src2)>;
    def : Pat<(or GR32:$src1, GR32:$src2), (!cast<Instruction>(OR32rr#suffix) GR32:$src1, GR32:$src2)>;
    def : Pat<(or GR64:$src1, GR64:$src2), (!cast<Instruction>(OR64rr#suffix) GR64:$src1, GR64:$src2)>;

    // or reg/mem
    def : Pat<(or GR8:$src1, (loadi8 addr:$src2)),
              (!cast<Instruction>(OR8rm#suffix) GR8:$src1, addr:$src2)>;
    def : Pat<(or GR16:$src1, (loadi16 addr:$src2)),
              (!cast<Instruction>(OR16rm#suffix) GR16:$src1, addr:$src2)>;
    def : Pat<(or GR32:$src1, (loadi32 addr:$src2)),
              (!cast<Instruction>(OR32rm#suffix) GR32:$src1, addr:$src2)>;
    def : Pat<(or GR64:$src1, (loadi64 addr:$src2)),
              (!cast<Instruction>(OR64rm#suffix) GR64:$src1, addr:$src2)>;

    // or reg/imm
    def : Pat<(or GR8:$src1 , imm:$src2), (!cast<Instruction>(OR8ri#suffix)  GR8 :$src1, imm:$src2)>;
    def : Pat<(or GR16:$src1, imm:$src2), (!cast<Instruction>(OR16ri#suffix) GR16:$src1, imm:$src2)>;
    def : Pat<(or GR32:$src1, imm:$src2), (!cast<Instruction>(OR32ri#suffix) GR32:$src1, imm:$src2)>;
    def : Pat<(or GR64:$src1, i64immSExt32:$src2),
              (!cast<Instruction>(OR64ri32#suffix) GR64:$src1, i64immSExt32:$src2)>;

    // xor reg/reg
    def : Pat<(xor GR8 :$src1, GR8 :$src2), (!cast<Instruction>(XOR8rr#suffix)  GR8 :$src1, GR8 :$src2)>;
    def : Pat<(xor GR16:$src1, GR16:$src2), (!cast<Instruction>(XOR16rr#suffix) GR16:$src1, GR16:$src2)>;
    def : Pat<(xor GR32:$src1, GR32:$src2), (!cast<Instruction>(XOR32rr#suffix) GR32:$src1, GR32:$src2)>;
    def : Pat<(xor GR64:$src1, GR64:$src2), (!cast<Instruction>(XOR64rr#suffix) GR64:$src1, GR64:$src2)>;

    // xor reg/mem
    def : Pat<(xor GR8:$src1, (loadi8 addr:$src2)),
              (!cast<Instruction>(XOR8rm#suffix) GR8:$src1, addr:$src2)>;
    def : Pat<(xor GR16:$src1, (loadi16 addr:$src2)),
              (!cast<Instruction>(XOR16rm#suffix) GR16:$src1, addr:$src2)>;
    def : Pat<(xor GR32:$src1, (loadi32 addr:$src2)),
              (!cast<Instruction>(XOR32rm#suffix) GR32:$src1, addr:$src2)>;
    def : Pat<(xor GR64:$src1, (loadi64 addr:$src2)),
              (!cast<Instruction>(XOR64rm#suffix) GR64:$src1, addr:$src2)>;

    // xor reg/imm
    def : Pat<(xor GR8:$src1, imm:$src2),
              (!cast<Instruction>(XOR8ri#suffix) GR8:$src1, imm:$src2)>;
    def : Pat<(xor GR16:$src1, imm:$src2),
              (!cast<Instruction>(XOR16ri#suffix) GR16:$src1, imm:$src2)>;
    def : Pat<(xor GR32:$src1, imm:$src2),
              (!cast<Instruction>(XOR32ri#suffix) GR32:$src1, imm:$src2)>;
    def : Pat<(xor GR64:$src1, i64immSExt32:$src2),
              (!cast<Instruction>(XOR64ri32#suffix) GR64:$src1, i64immSExt32:$src2)>;

    // and reg/reg
    def : Pat<(and GR8 :$src1, GR8 :$src2), (!cast<Instruction>(AND8rr#suffix)  GR8 :$src1, GR8 :$src2)>;
    def : Pat<(and GR16:$src1, GR16:$src2), (!cast<Instruction>(AND16rr#suffix) GR16:$src1, GR16:$src2)>;
    def : Pat<(and GR32:$src1, GR32:$src2), (!cast<Instruction>(AND32rr#suffix) GR32:$src1, GR32:$src2)>;
    def : Pat<(and GR64:$src1, GR64:$src2), (!cast<Instruction>(AND64rr#suffix) GR64:$src1, GR64:$src2)>;

    // and reg/mem
    def : Pat<(and GR8:$src1, (loadi8 addr:$src2)),
              (!cast<Instruction>(AND8rm#suffix) GR8:$src1, addr:$src2)>;
    def : Pat<(and GR16:$src1, (loadi16 addr:$src2)),
              (!cast<Instruction>(AND16rm#suffix) GR16:$src1, addr:$src2)>;
    def : Pat<(and GR32:$src1, (loadi32 addr:$src2)),
              (!cast<Instruction>(AND32rm#suffix) GR32:$src1, addr:$src2)>;
    def : Pat<(and GR64:$src1, (loadi64 addr:$src2)),
              (!cast<Instruction>(AND64rm#suffix) GR64:$src1, addr:$src2)>;

    // and reg/imm
    def : Pat<(and GR8:$src1, imm:$src2),
              (!cast<Instruction>(AND8ri#suffix) GR8:$src1, imm:$src2)>;
    def : Pat<(and GR16:$src1, imm:$src2),
              (!cast<Instruction>(AND16ri#suffix) GR16:$src1, imm:$src2)>;
    def : Pat<(and GR32:$src1, imm:$src2),
              (!cast<Instruction>(AND32ri#suffix) GR32:$src1, imm:$src2)>;
    def : Pat<(and GR64:$src1, i64immSExt32:$src2),
              (!cast<Instruction>(AND64ri32#suffix) GR64:$src1, i64immSExt32:$src2)>;
  }

  // Increment/Decrement reg.
  // Do not make INC/DEC if it is slow
  let Predicates = [UseIncDec, p] in {
    def : Pat<(add GR8:$src, 1),   (!cast<Instruction>(INC8r#suffix) GR8:$src)>;
    def : Pat<(add GR16:$src, 1),  (!cast<Instruction>(INC16r#suffix) GR16:$src)>;
    def : Pat<(add GR32:$src, 1),  (!cast<Instruction>(INC32r#suffix) GR32:$src)>;
    def : Pat<(add GR64:$src, 1),  (!cast<Instruction>(INC64r#suffix) GR64:$src)>;
    def : Pat<(add GR8:$src, -1),  (!cast<Instruction>(DEC8r#suffix) GR8:$src)>;
    def : Pat<(add GR16:$src, -1), (!cast<Instruction>(DEC16r#suffix) GR16:$src)>;
    def : Pat<(add GR32:$src, -1), (!cast<Instruction>(DEC32r#suffix) GR32:$src)>;
    def : Pat<(add GR64:$src, -1), (!cast<Instruction>(DEC64r#suffix) GR64:$src)>;

    def : Pat<(X86add_flag_nocf GR8:$src, -1),  (!cast<Instruction>(DEC8r#suffix) GR8:$src)>;
    def : Pat<(X86add_flag_nocf GR16:$src, -1), (!cast<Instruction>(DEC16r#suffix) GR16:$src)>;
    def : Pat<(X86add_flag_nocf GR32:$src, -1), (!cast<Instruction>(DEC32r#suffix) GR32:$src)>;
    def : Pat<(X86add_flag_nocf GR64:$src, -1), (!cast<Instruction>(DEC64r#suffix) GR64:$src)>;
    def : Pat<(X86sub_flag_nocf GR8:$src, -1),  (!cast<Instruction>(INC8r#suffix) GR8:$src)>;
    def : Pat<(X86sub_flag_nocf GR16:$src, -1), (!cast<Instruction>(INC16r#suffix) GR16:$src)>;
    def : Pat<(X86sub_flag_nocf GR32:$src, -1), (!cast<Instruction>(INC32r#suffix) GR32:$src)>;
    def : Pat<(X86sub_flag_nocf GR64:$src, -1), (!cast<Instruction>(INC64r#suffix) GR64:$src)>;

    def : Pat<(or_is_add GR8:$src, 1),   (!cast<Instruction>(INC8r#suffix) GR8:$src)>;
    def : Pat<(or_is_add GR16:$src, 1),  (!cast<Instruction>(INC16r#suffix) GR16:$src)>;
    def : Pat<(or_is_add GR32:$src, 1),  (!cast<Instruction>(INC32r#suffix) GR32:$src)>;
    def : Pat<(or_is_add GR64:$src, 1),  (!cast<Instruction>(INC64r#suffix) GR64:$src)>;
  }
}

defm : EFLAGSDefiningPats<"", NoNDD>;
defm : EFLAGSDefiningPats<"_ND", HasNDD>;

// mul reg, imm
def : Pat<(mul GR16:$src1, imm:$src2),
          (IMUL16rri GR16:$src1, imm:$src2)>;
def : Pat<(mul GR32:$src1, imm:$src2),
          (IMUL32rri GR32:$src1, imm:$src2)>;
def : Pat<(mul GR64:$src1, i64immSExt32:$src2),
          (IMUL64rri32 GR64:$src1, i64immSExt32:$src2)>;

// reg = mul mem, imm
def : Pat<(mul (loadi16 addr:$src1), imm:$src2),
          (IMUL16rmi addr:$src1, imm:$src2)>;
def : Pat<(mul (loadi32 addr:$src1), imm:$src2),
          (IMUL32rmi addr:$src1, imm:$src2)>;
def : Pat<(mul (loadi64 addr:$src1), i64immSExt32:$src2),
          (IMUL64rmi32 addr:$src1, i64immSExt32:$src2)>;

// Bit scan instruction patterns to match explicit zero-undef behavior.
def : Pat<(cttz_zero_undef GR16:$src), (BSF16rr GR16:$src)>;
def : Pat<(cttz_zero_undef GR32:$src), (BSF32rr GR32:$src)>;
def : Pat<(cttz_zero_undef GR64:$src), (BSF64rr GR64:$src)>;
def : Pat<(cttz_zero_undef (loadi16 addr:$src)), (BSF16rm addr:$src)>;
def : Pat<(cttz_zero_undef (loadi32 addr:$src)), (BSF32rm addr:$src)>;
def : Pat<(cttz_zero_undef (loadi64 addr:$src)), (BSF64rm addr:$src)>;

// When HasMOVBE is enabled it is possible to get a non-legalized
// register-register 16 bit bswap. This maps it to a ROL instruction.
let Predicates = [HasMOVBE] in {
 def : Pat<(bswap GR16:$src), (ROL16ri GR16:$src, (i8 8))>;
}

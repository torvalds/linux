//===-- SystemZInstrInfo.td - General SystemZ instructions ----*- tblgen-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

def IsTargetXPLINK64      : Predicate<"Subtarget->isTargetXPLINK64()">;
def IsTargetELF           : Predicate<"Subtarget->isTargetELF()">;

//===----------------------------------------------------------------------===//
// Stack allocation
//===----------------------------------------------------------------------===//

// These pseudos carry values needed to compute the MaxcallFrameSize of the
// function.  The callseq_start node requires the hasSideEffects flag.
let usesCustomInserter = 1, hasNoSchedulingInfo = 1, hasSideEffects = 1 in {
  def ADJCALLSTACKDOWN : Pseudo<(outs), (ins i64imm:$amt1, i64imm:$amt2),
                                [(callseq_start timm:$amt1, timm:$amt2)]>;
  def ADJCALLSTACKUP   : Pseudo<(outs), (ins i64imm:$amt1, i64imm:$amt2),
                                [(callseq_end timm:$amt1, timm:$amt2)]>;
}

// Takes as input the value of the stack pointer after a dynamic allocation
// has been made.  Sets the output to the address of the dynamically-
// allocated area itself, skipping the outgoing arguments.
//
// This expands to an LA or LAY instruction.  We restrict the offset
// to the range of LA and keep the LAY range in reserve for when
// the size of the outgoing arguments is added.
def ADJDYNALLOC : Pseudo<(outs GR64:$dst), (ins dynalloc12only:$src),
                         [(set GR64:$dst, dynalloc12only:$src)]>;

let Defs = [R15D, CC], Uses = [R15D], hasNoSchedulingInfo = 1,
    usesCustomInserter = 1 in
  def PROBED_ALLOCA : Pseudo<(outs GR64:$dst),
                             (ins GR64:$oldSP, GR64:$space),
           [(set GR64:$dst, (z_probed_alloca GR64:$oldSP, GR64:$space))]>;

let Defs = [R1D, R15D, CC], Uses = [R15D], hasNoSchedulingInfo = 1,
    hasSideEffects = 1 in
  def PROBED_STACKALLOC : Pseudo<(outs), (ins i64imm:$stacksize), []>;

let Defs = [R3D, CC], Uses = [R3D, R4D], hasNoSchedulingInfo = 1,
    hasSideEffects = 1 in
  def XPLINK_STACKALLOC : Pseudo<(outs), (ins), []>;

//===----------------------------------------------------------------------===//
// Branch instructions
//===----------------------------------------------------------------------===//

// Conditional branches.
let isBranch = 1, isTerminator = 1, Uses = [CC] in {
  // It's easier for LLVM to handle these branches in their raw BRC/BRCL form
  // with the condition-code mask being the first operand.  It seems friendlier
  // to use mnemonic forms like JE and JLH when writing out the assembly though.
  let isCodeGenOnly = 1 in {
    // An assembler extended mnemonic for BRC.
    def BRC  : CondBranchRI <"j#",  0xA74, z_br_ccmask>;
    // An assembler extended mnemonic for BRCL.  (The extension is "G"
    // rather than "L" because "JL" is "Jump if Less".)
    def BRCL : CondBranchRIL<"jg#", 0xC04>;
    let isIndirectBranch = 1 in {
      def BC  : CondBranchRX<"b#",  0x47>;
      def BCR : CondBranchRR<"b#r", 0x07>;
      def BIC : CondBranchRXY<"bi#", 0xe347>,
                Requires<[FeatureMiscellaneousExtensions2]>;
    }
  }

  // Allow using the raw forms directly from the assembler (and occasional
  // special code generation needs) as well.
  def BRCAsm  : AsmCondBranchRI <"brc",  0xA74>;
  def BRCLAsm : AsmCondBranchRIL<"brcl", 0xC04>;
  let isIndirectBranch = 1 in {
    def BCAsm  : AsmCondBranchRX<"bc",  0x47>;
    def BCRAsm : AsmCondBranchRR<"bcr", 0x07>;
    def BICAsm : AsmCondBranchRXY<"bic", 0xe347>,
                 Requires<[FeatureMiscellaneousExtensions2]>;
  }

  // Define AsmParser extended mnemonics for each general condition-code mask
  // (integer or floating-point)
  foreach V = [ "E", "NE", "H", "NH", "L", "NL", "HE", "NHE", "LE", "NLE",
                "Z", "NZ", "P", "NP", "M", "NM", "LH", "NLH", "O", "NO" ] in {
    def JAsm#V  : FixedCondBranchRI <CV<V>, "j#",  0xA74>;
    def JGAsm#V : FixedCondBranchRIL<CV<V>, "j{g|l}#", 0xC04>;
    let isIndirectBranch = 1 in {
      def BAsm#V  : FixedCondBranchRX <CV<V>, "b#",  0x47>;
      def BRAsm#V : FixedCondBranchRR <CV<V>, "b#r", 0x07>;
      def BIAsm#V : FixedCondBranchRXY<CV<V>, "bi#", 0xe347>,
                    Requires<[FeatureMiscellaneousExtensions2]>;
    }
  }
}

// Unconditional branches.  These are in fact simply variants of the
// conditional branches with the condition mask set to "always".
let isBranch = 1, isTerminator = 1, isBarrier = 1 in {
  def J  : FixedCondBranchRI <CondAlways, "j",  0xA74, br>;
  def JG : FixedCondBranchRIL<CondAlways, "j{g|lu}", 0xC04>;
  let isIndirectBranch = 1 in {
    def B  : FixedCondBranchRX<CondAlways, "b",  0x47>;
    def BR : FixedCondBranchRR<CondAlways, "br", 0x07, brind>;
    def BI : FixedCondBranchRXY<CondAlways, "bi", 0xe347, brind>,
             Requires<[FeatureMiscellaneousExtensions2]>;
  }
}

// NOPs.  These are again variants of the conditional branches, with the
// condition mask set to "never".  NOP_bare can't be an InstAlias since it
// would need R0D hard coded which is not part of ADDR64BitRegClass.
def NOP : NeverCondBranchRX<"nop", 0x47>;
let isAsmParserOnly = 1, hasNoSchedulingInfo = 1, M1 = 0, X2 = 0, B2 = 0, D2 = 0 in
  def NOP_bare  : InstRXb<0x47,(outs), (ins), "nop", []>;
def NOPR : NeverCondBranchRR<"nopr", 0x07>;
def NOPR_bare : InstAlias<"nopr", (NOPR R0D), 0>;

// An alias of BRC 0, label
def JNOP : InstAlias<"jnop\t$RI2", (BRCAsm 0, brtarget16:$RI2), 0>;

// An alias of BRCL 0, label
// jgnop on att ; jlnop on hlasm
def JGNOP : InstAlias<"{jgnop|jlnop}\t$RI2", (BRCLAsm 0, brtarget32:$RI2), 0>;

// Fused compare-and-branch instructions.
//
// These instructions do not use or clobber the condition codes.
// We nevertheless pretend that the relative compare-and-branch
// instructions clobber CC, so that we can lower them to separate
// comparisons and BRCLs if the branch ends up being out of range.
let isBranch = 1, isTerminator = 1 in {
  // As for normal branches, we handle these instructions internally in
  // their raw CRJ-like form, but use assembly macros like CRJE when writing
  // them out.  Using the *Pair multiclasses, we also create the raw forms.
  let Defs = [CC] in {
    defm CRJ   : CmpBranchRIEbPair<"crj",   0xEC76, GR32>;
    defm CGRJ  : CmpBranchRIEbPair<"cgrj",  0xEC64, GR64>;
    defm CIJ   : CmpBranchRIEcPair<"cij",   0xEC7E, GR32, imm32sx8>;
    defm CGIJ  : CmpBranchRIEcPair<"cgij",  0xEC7C, GR64, imm64sx8>;
    defm CLRJ  : CmpBranchRIEbPair<"clrj",  0xEC77, GR32>;
    defm CLGRJ : CmpBranchRIEbPair<"clgrj", 0xEC65, GR64>;
    defm CLIJ  : CmpBranchRIEcPair<"clij",  0xEC7F, GR32, imm32zx8>;
    defm CLGIJ : CmpBranchRIEcPair<"clgij", 0xEC7D, GR64, imm64zx8>;
  }
  let isIndirectBranch = 1 in {
    defm CRB   : CmpBranchRRSPair<"crb",   0xECF6, GR32>;
    defm CGRB  : CmpBranchRRSPair<"cgrb",  0xECE4, GR64>;
    defm CIB   : CmpBranchRISPair<"cib",   0xECFE, GR32, imm32sx8>;
    defm CGIB  : CmpBranchRISPair<"cgib",  0xECFC, GR64, imm64sx8>;
    defm CLRB  : CmpBranchRRSPair<"clrb",  0xECF7, GR32>;
    defm CLGRB : CmpBranchRRSPair<"clgrb", 0xECE5, GR64>;
    defm CLIB  : CmpBranchRISPair<"clib",  0xECFF, GR32, imm32zx8>;
    defm CLGIB : CmpBranchRISPair<"clgib", 0xECFD, GR64, imm64zx8>;
  }

  // Define AsmParser mnemonics for each integer condition-code mask.
  foreach V = [ "E", "H", "L", "HE", "LE", "LH",
                "NE", "NH", "NL", "NHE", "NLE", "NLH" ] in {
    let Defs = [CC] in {
      def CRJAsm#V   : FixedCmpBranchRIEb<ICV<V>, "crj",   0xEC76, GR32>;
      def CGRJAsm#V  : FixedCmpBranchRIEb<ICV<V>, "cgrj",  0xEC64, GR64>;
      def CIJAsm#V   : FixedCmpBranchRIEc<ICV<V>, "cij",   0xEC7E, GR32,
                                          imm32sx8>;
      def CGIJAsm#V  : FixedCmpBranchRIEc<ICV<V>, "cgij",  0xEC7C, GR64,
                                          imm64sx8>;
      def CLRJAsm#V  : FixedCmpBranchRIEb<ICV<V>, "clrj",  0xEC77, GR32>;
      def CLGRJAsm#V : FixedCmpBranchRIEb<ICV<V>, "clgrj", 0xEC65, GR64>;
      def CLIJAsm#V  : FixedCmpBranchRIEc<ICV<V>, "clij",  0xEC7F, GR32,
                                          imm32zx8>;
      def CLGIJAsm#V : FixedCmpBranchRIEc<ICV<V>, "clgij", 0xEC7D, GR64,
                                          imm64zx8>;
    }
    let isIndirectBranch = 1 in {
      def CRBAsm#V   : FixedCmpBranchRRS<ICV<V>, "crb",   0xECF6, GR32>;
      def CGRBAsm#V  : FixedCmpBranchRRS<ICV<V>, "cgrb",  0xECE4, GR64>;
      def CIBAsm#V   : FixedCmpBranchRIS<ICV<V>, "cib",   0xECFE, GR32,
                                         imm32sx8>;
      def CGIBAsm#V  : FixedCmpBranchRIS<ICV<V>, "cgib",  0xECFC, GR64,
                                         imm64sx8>;
      def CLRBAsm#V  : FixedCmpBranchRRS<ICV<V>, "clrb",  0xECF7, GR32>;
      def CLGRBAsm#V : FixedCmpBranchRRS<ICV<V>, "clgrb", 0xECE5, GR64>;
      def CLIBAsm#V  : FixedCmpBranchRIS<ICV<V>, "clib",  0xECFF, GR32,
                                         imm32zx8>;
      def CLGIBAsm#V : FixedCmpBranchRIS<ICV<V>, "clgib", 0xECFD, GR64,
                                         imm64zx8>;
    }
  }
}

// Decrement a register and branch if it is nonzero.  These don't clobber CC,
// but we might need to split long relative branches into sequences that do.
let isBranch = 1, isTerminator = 1 in {
  let Defs = [CC] in {
    def BRCT  : BranchUnaryRI<"brct",  0xA76, GR32>;
    def BRCTG : BranchUnaryRI<"brctg", 0xA77, GR64>;
  }
  // This doesn't need to clobber CC since we never need to split it.
  def BRCTH : BranchUnaryRIL<"brcth", 0xCC6, GRH32>,
              Requires<[FeatureHighWord]>;

  def BCT   : BranchUnaryRX<"bct",  0x46,GR32>;
  def BCTR  : BranchUnaryRR<"bctr", 0x06, GR32>;
  def BCTG  : BranchUnaryRXY<"bctg",  0xE346, GR64>;
  def BCTGR : BranchUnaryRRE<"bctgr", 0xB946, GR64>;
}

let isBranch = 1, isTerminator = 1 in {
  let Defs = [CC] in {
    def BRXH  : BranchBinaryRSI<"brxh",  0x84, GR32>;
    def BRXLE : BranchBinaryRSI<"brxle", 0x85, GR32>;
    def BRXHG : BranchBinaryRIEe<"brxhg", 0xEC44, GR64>;
    def BRXLG : BranchBinaryRIEe<"brxlg", 0xEC45, GR64>;
  }
  def BXH   : BranchBinaryRS<"bxh",  0x86, GR32>;
  def BXLE  : BranchBinaryRS<"bxle", 0x87, GR32>;
  def BXHG  : BranchBinaryRSY<"bxhg",  0xEB44, GR64>;
  def BXLEG : BranchBinaryRSY<"bxleg", 0xEB45, GR64>;
}

//===----------------------------------------------------------------------===//
// Trap instructions
//===----------------------------------------------------------------------===//

// Unconditional trap.
let hasCtrlDep = 1, hasSideEffects = 1 in
  def Trap : Alias<4, (outs), (ins), [(trap)]>;

// Conditional trap.
let hasCtrlDep = 1, Uses = [CC], hasSideEffects = 1 in
  def CondTrap : Alias<4, (outs), (ins cond4:$valid, cond4:$R1), []>;

// Fused compare-and-trap instructions.
let hasCtrlDep = 1, hasSideEffects = 1 in {
  // These patterns work the same way as for compare-and-branch.
  defm CRT   : CmpBranchRRFcPair<"crt",   0xB972, GR32>;
  defm CGRT  : CmpBranchRRFcPair<"cgrt",  0xB960, GR64>;
  defm CLRT  : CmpBranchRRFcPair<"clrt",  0xB973, GR32>;
  defm CLGRT : CmpBranchRRFcPair<"clgrt", 0xB961, GR64>;
  defm CIT   : CmpBranchRIEaPair<"cit",   0xEC72, GR32, imm32sx16>;
  defm CGIT  : CmpBranchRIEaPair<"cgit",  0xEC70, GR64, imm64sx16>;
  defm CLFIT : CmpBranchRIEaPair<"clfit", 0xEC73, GR32, imm32zx16>;
  defm CLGIT : CmpBranchRIEaPair<"clgit", 0xEC71, GR64, imm64zx16>;
  let Predicates = [FeatureMiscellaneousExtensions] in {
    defm CLT  : CmpBranchRSYbPair<"clt",  0xEB23, GR32>;
    defm CLGT : CmpBranchRSYbPair<"clgt", 0xEB2B, GR64>;
  }

  foreach V = [ "E", "H", "L", "HE", "LE", "LH",
                "NE", "NH", "NL", "NHE", "NLE", "NLH" ] in {
    def CRTAsm#V   : FixedCmpBranchRRFc<ICV<V>, "crt",   0xB972, GR32>;
    def CGRTAsm#V  : FixedCmpBranchRRFc<ICV<V>, "cgrt",  0xB960, GR64>;
    def CLRTAsm#V  : FixedCmpBranchRRFc<ICV<V>, "clrt",  0xB973, GR32>;
    def CLGRTAsm#V : FixedCmpBranchRRFc<ICV<V>, "clgrt", 0xB961, GR64>;
    def CITAsm#V   : FixedCmpBranchRIEa<ICV<V>, "cit",   0xEC72, GR32,
                                         imm32sx16>;
    def CGITAsm#V  : FixedCmpBranchRIEa<ICV<V>, "cgit",  0xEC70, GR64,
                                         imm64sx16>;
    def CLFITAsm#V : FixedCmpBranchRIEa<ICV<V>, "clfit", 0xEC73, GR32,
                                         imm32zx16>;
    def CLGITAsm#V : FixedCmpBranchRIEa<ICV<V>, "clgit", 0xEC71, GR64,
                                         imm64zx16>;
    let Predicates = [FeatureMiscellaneousExtensions] in {
      def CLTAsm#V  : FixedCmpBranchRSYb<ICV<V>, "clt",  0xEB23, GR32>;
      def CLGTAsm#V : FixedCmpBranchRSYb<ICV<V>, "clgt", 0xEB2B, GR64>;
    }
  }
}

//===----------------------------------------------------------------------===//
// Call and return instructions
//===----------------------------------------------------------------------===//

// Define the general form of the call instructions for the asm parser.
// These instructions don't hard-code %r14 as the return address register.
let isCall = 1, Defs = [CC] in {
  def BRAS  : CallRI <"bras", 0xA75>;
  def BRASL : CallRIL<"brasl", 0xC05>;
  def BAS   : CallRX <"bas", 0x4D>;
  def BASR  : CallRR <"basr", 0x0D>;
}

// A symbol in the ADA (z/OS only).
def adasym : Operand<i64>;

// z/OS XPLINK
let Predicates = [IsTargetXPLINK64] in {
  let isCall = 1, Defs = [R7D, CC], Uses = [FPC] in {
    def CallBRASL_XPLINK64 : Alias<8, (outs), (ins pcrel32:$I2, variable_ops),
                          [(z_call pcrel32:$I2)]>;
    def CallBASR_XPLINK64  : Alias<4, (outs), (ins ADDR64:$R2, variable_ops),
                          [(z_call ADDR64:$R2)]>;
  }

  let isCall = 1, Defs = [R3D, CC], Uses = [FPC] in {
    def CallBASR_STACKEXT  : Alias<4, (outs), (ins ADDR64:$R2), []>;
  }

  let hasNoSchedulingInfo = 1, Defs = [CC] in {
    def ADA_ENTRY : Alias<12, (outs GR64:$Reg), (ins adasym:$addr,
                              ADDR64:$ADA, imm64:$Offset),
                            [(set i64:$Reg, (z_ada_entry i64:$addr,
                              i64:$ADA, i64:$Offset))]>;
  }
  let mayLoad = 1, AddedComplexity = 20, hasNoSchedulingInfo = 1, Defs = [CC] in {
    def ADA_ENTRY_VALUE : Alias<12, (outs GR64:$Reg), (ins adasym:$addr,
                              ADDR64:$ADA, imm64:$Offset),
                            [(set i64:$Reg, (z_load (z_ada_entry
                              iPTR:$addr, iPTR:$ADA, i64:$Offset)))]>;
 }
}

// Regular calls.
// z/Linux ELF
let Predicates = [IsTargetELF] in {
  let isCall = 1, Defs = [R14D, CC], Uses = [FPC] in {
    def CallBRASL : Alias<6, (outs), (ins pcrel32:$I2, variable_ops),
                          [(z_call pcrel32:$I2)]>;
    def CallBASR  : Alias<2, (outs), (ins ADDR64:$R2, variable_ops),
                          [(z_call ADDR64:$R2)]>;
  }

  // TLS calls.  These will be lowered into a call to __tls_get_offset,
  // with an extra relocation specifying the TLS symbol.
  let isCall = 1, Defs = [R14D, CC] in {
    def TLS_GDCALL : Alias<6, (outs), (ins tlssym:$I2, variable_ops),
                           [(z_tls_gdcall tglobaltlsaddr:$I2)]>;
    def TLS_LDCALL : Alias<6, (outs), (ins tlssym:$I2, variable_ops),
                           [(z_tls_ldcall tglobaltlsaddr:$I2)]>;
  }
}

// Sibling calls. Indirect sibling calls must be via R6 for XPLink,
// R1 used for ELF
let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1 in {
  def CallJG : Alias<6, (outs), (ins pcrel32:$I2),
                     [(z_sibcall pcrel32:$I2)]>;
  def CallBR : Alias<2, (outs), (ins ADDR64:$R2),
                     [(z_sibcall ADDR64:$R2)]>;
}

// Conditional sibling calls.
let CCMaskFirst = 1, isCall = 1, isTerminator = 1, isReturn = 1 in {
  def CallBRCL : Alias<6, (outs), (ins cond4:$valid, cond4:$R1,
                                   pcrel32:$I2), []>;
  def CallBCR : Alias<2, (outs), (ins cond4:$valid, cond4:$R1,
                                  ADDR64:$R2), []>;
}

// Fused compare and conditional sibling calls.
let isCall = 1, isTerminator = 1, isReturn = 1 in {
  def CRBCall : Alias<6, (outs), (ins GR32:$R1, GR32:$R2, cond4:$M3, ADDR64:$R4), []>;
  def CGRBCall : Alias<6, (outs), (ins GR64:$R1, GR64:$R2, cond4:$M3, ADDR64:$R4), []>;
  def CIBCall : Alias<6, (outs), (ins GR32:$R1, imm32sx8:$I2, cond4:$M3, ADDR64:$R4), []>;
  def CGIBCall : Alias<6, (outs), (ins GR64:$R1, imm64sx8:$I2, cond4:$M3, ADDR64:$R4), []>;
  def CLRBCall : Alias<6, (outs), (ins GR32:$R1, GR32:$R2, cond4:$M3, ADDR64:$R4), []>;
  def CLGRBCall : Alias<6, (outs), (ins GR64:$R1, GR64:$R2, cond4:$M3, ADDR64:$R4), []>;
  def CLIBCall : Alias<6, (outs), (ins GR32:$R1, imm32zx8:$I2, cond4:$M3, ADDR64:$R4), []>;
  def CLGIBCall : Alias<6, (outs), (ins GR64:$R1, imm64zx8:$I2, cond4:$M3, ADDR64:$R4), []>;
}

let Predicates = [IsTargetXPLINK64] in {
  // A return instruction (b 2(%r7)).
  let isReturn = 1, isTerminator = 1, isBarrier = 1, hasCtrlDep = 1 in
    def Return_XPLINK : Alias<4, (outs), (ins), [(z_retglue)]>;

  // A conditional return instruction (bc <cond>, 2(%r7)).
  let isReturn = 1, isTerminator = 1, hasCtrlDep = 1, CCMaskFirst = 1, Uses = [CC] in
    def CondReturn_XPLINK : Alias<4, (outs), (ins cond4:$valid, cond4:$R1), []>;
}

let Predicates = [IsTargetELF] in {
  // A return instruction (br %r14).
  let isReturn = 1, isTerminator = 1, isBarrier = 1, hasCtrlDep = 1 in
    def Return : Alias<2, (outs), (ins), [(z_retglue)]>;

  // A conditional return instruction (bcr <cond>, %r14).
  let isReturn = 1, isTerminator = 1, hasCtrlDep = 1, CCMaskFirst = 1, Uses = [CC] in
    def CondReturn : Alias<2, (outs), (ins cond4:$valid, cond4:$R1), []>;
}

// Fused compare and conditional returns.
let isReturn = 1, isTerminator = 1, hasCtrlDep = 1 in {
  def CRBReturn : Alias<6, (outs), (ins GR32:$R1, GR32:$R2, cond4:$M3), []>;
  def CGRBReturn : Alias<6, (outs), (ins GR64:$R1, GR64:$R2, cond4:$M3), []>;
  def CIBReturn : Alias<6, (outs), (ins GR32:$R1, imm32sx8:$I2, cond4:$M3), []>;
  def CGIBReturn : Alias<6, (outs), (ins GR64:$R1, imm64sx8:$I2, cond4:$M3), []>;
  def CLRBReturn : Alias<6, (outs), (ins GR32:$R1, GR32:$R2, cond4:$M3), []>;
  def CLGRBReturn : Alias<6, (outs), (ins GR64:$R1, GR64:$R2, cond4:$M3), []>;
  def CLIBReturn : Alias<6, (outs), (ins GR32:$R1, imm32zx8:$I2, cond4:$M3), []>;
  def CLGIBReturn : Alias<6, (outs), (ins GR64:$R1, imm64zx8:$I2, cond4:$M3), []>;
}

//===----------------------------------------------------------------------===//
// Select instructions
//===----------------------------------------------------------------------===//

def Select32    : SelectWrapper<i32, GR32>,
                  Requires<[FeatureNoLoadStoreOnCond]>;
def Select64    : SelectWrapper<i64, GR64>,
                  Requires<[FeatureNoLoadStoreOnCond]>;

// We don't define 32-bit Mux stores if we don't have STOCFH, because the
// low-only STOC should then always be used if possible.
defm CondStore8Mux  : CondStores<GRX32, nonvolatile_truncstorei8,
                                 nonvolatile_anyextloadi8, bdxaddr20only>,
                      Requires<[FeatureHighWord]>;
defm CondStore16Mux : CondStores<GRX32, nonvolatile_truncstorei16,
                                 nonvolatile_anyextloadi16, bdxaddr20only>,
                      Requires<[FeatureHighWord]>;
defm CondStore32Mux : CondStores<GRX32, simple_store,
                                 simple_load, bdxaddr20only>,
                      Requires<[FeatureLoadStoreOnCond2]>;
defm CondStore8     : CondStores<GR32, nonvolatile_truncstorei8,
                                 nonvolatile_anyextloadi8, bdxaddr20only>;
defm CondStore16    : CondStores<GR32, nonvolatile_truncstorei16,
                                 nonvolatile_anyextloadi16, bdxaddr20only>;
defm CondStore32    : CondStores<GR32, simple_store,
                                 simple_load, bdxaddr20only>;

defm : CondStores64<CondStore8, CondStore8Inv, nonvolatile_truncstorei8,
                    nonvolatile_anyextloadi8, bdxaddr20only>;
defm : CondStores64<CondStore16, CondStore16Inv, nonvolatile_truncstorei16,
                    nonvolatile_anyextloadi16, bdxaddr20only>;
defm : CondStores64<CondStore32, CondStore32Inv, nonvolatile_truncstorei32,
                    nonvolatile_anyextloadi32, bdxaddr20only>;
defm CondStore64 : CondStores<GR64, simple_store,
                              simple_load, bdxaddr20only>;

//===----------------------------------------------------------------------===//
// Move instructions
//===----------------------------------------------------------------------===//

// Register moves.
def LR  : UnaryRR <"lr",  0x18,   null_frag, GR32, GR32>;
def LGR : UnaryRRE<"lgr", 0xB904, null_frag, GR64, GR64>;

let Defs = [CC], CCValues = 0xE, CompareZeroCCMask = 0xE in {
  def LTR  : UnaryRR <"ltr",  0x12,   null_frag, GR32, GR32>;
  def LTGR : UnaryRRE<"ltgr", 0xB902, null_frag, GR64, GR64>;
}

let usesCustomInserter = 1, hasNoSchedulingInfo = 1 in
  def PAIR128 : Pseudo<(outs GR128:$dst), (ins GR64:$hi, GR64:$lo), []>;

// Immediate moves.
let isAsCheapAsAMove = 1, isMoveImm = 1, isReMaterializable = 1 in {
  // 16-bit sign-extended immediates.  LHIMux expands to LHI or IIHF,
  // deopending on the choice of register.
  def LHIMux : UnaryRIPseudo<bitconvert, GRX32, imm32sx16>,
               Requires<[FeatureHighWord]>;
  def LHI  : UnaryRI<"lhi",  0xA78, bitconvert, GR32, imm32sx16>;
  def LGHI : UnaryRI<"lghi", 0xA79, bitconvert, GR64, imm64sx16>;

  // Other 16-bit immediates.
  def LLILL : UnaryRI<"llill", 0xA5F, bitconvert, GR64, imm64ll16>;
  def LLILH : UnaryRI<"llilh", 0xA5E, bitconvert, GR64, imm64lh16>;
  def LLIHL : UnaryRI<"llihl", 0xA5D, bitconvert, GR64, imm64hl16>;
  def LLIHH : UnaryRI<"llihh", 0xA5C, bitconvert, GR64, imm64hh16>;

  // 32-bit immediates.
  def LGFI  : UnaryRIL<"lgfi",  0xC01, bitconvert, GR64, imm64sx32>;
  def LLILF : UnaryRIL<"llilf", 0xC0F, bitconvert, GR64, imm64lf32>;
  def LLIHF : UnaryRIL<"llihf", 0xC0E, bitconvert, GR64, imm64hf32>;
}
def LLGFI : InstAlias<"llgfi\t$R1, $RI1", (LLILF GR64:$R1, imm64lf32:$RI1)>;
def LLGHI : InstAlias<"llghi\t$R1, $RI1", (LLILL GR64:$R1, imm64ll16:$RI1)>;

// Register loads.
let canFoldAsLoad = 1, SimpleBDXLoad = 1, mayLoad = 1 in {
  // Expands to L, LY or LFH, depending on the choice of register.
  def LMux : UnaryRXYPseudo<"l", z_load, GRX32, 4>,
             Requires<[FeatureHighWord]>;
  defm L : UnaryRXPair<"l", 0x58, 0xE358, z_load, GR32, 4>;
  def LFH : UnaryRXY<"lfh", 0xE3CA, z_load, GRH32, 4>,
            Requires<[FeatureHighWord]>;
  def LG : UnaryRXY<"lg", 0xE304, z_load, GR64, 8>;

  // These instructions are split after register allocation, so we don't
  // want a custom inserter.
  let Has20BitOffset = 1, HasIndex = 1, Is128Bit = 1 in {
    def L128 : Pseudo<(outs GR128:$dst), (ins bdxaddr20only128:$src),
                      [(set GR128:$dst, (load bdxaddr20only128:$src))]>;
  }
}
let Defs = [CC], CCValues = 0xE, CompareZeroCCMask = 0xE in {
  def LT  : UnaryRXY<"lt",  0xE312, z_load, GR32, 4>;
  def LTG : UnaryRXY<"ltg", 0xE302, z_load, GR64, 8>;
}

let canFoldAsLoad = 1 in {
  def LRL  : UnaryRILPC<"lrl",  0xC4D, aligned_z_load, GR32>;
  def LGRL : UnaryRILPC<"lgrl", 0xC48, aligned_z_load, GR64>;
}

// Load and zero rightmost byte.
let Predicates = [FeatureLoadAndZeroRightmostByte] in {
  def LZRF : UnaryRXY<"lzrf", 0xE33B, null_frag, GR32, 4>;
  def LZRG : UnaryRXY<"lzrg", 0xE32A, null_frag, GR64, 8>;
  def : Pat<(and (i32 (z_load bdxaddr20only:$src)), 0xffffff00),
            (LZRF bdxaddr20only:$src)>;
  def : Pat<(and (i64 (z_load bdxaddr20only:$src)), 0xffffffffffffff00),
            (LZRG bdxaddr20only:$src)>;
}

// Load and trap.
let Predicates = [FeatureLoadAndTrap], hasSideEffects = 1 in {
  def LAT   : UnaryRXY<"lat",   0xE39F, null_frag, GR32, 4>;
  def LFHAT : UnaryRXY<"lfhat", 0xE3C8, null_frag, GRH32, 4>;
  def LGAT  : UnaryRXY<"lgat",  0xE385, null_frag, GR64, 8>;
}

// Register stores.
let SimpleBDXStore = 1, mayStore = 1 in {
  // Expands to ST, STY or STFH, depending on the choice of register.
  def STMux : StoreRXYPseudo<store, GRX32, 4>,
              Requires<[FeatureHighWord]>;
  defm ST : StoreRXPair<"st", 0x50, 0xE350, store, GR32, 4>;
  def STFH : StoreRXY<"stfh", 0xE3CB, store, GRH32, 4>,
             Requires<[FeatureHighWord]>;
  def STG : StoreRXY<"stg", 0xE324, store, GR64, 8>;

  // These instructions are split after register allocation, so we don't
  // want a custom inserter.
  let Has20BitOffset = 1, HasIndex = 1, Is128Bit = 1 in {
    def ST128 : Pseudo<(outs), (ins GR128:$src, bdxaddr20only128:$dst),
                       [(store GR128:$src, bdxaddr20only128:$dst)]>;
  }
}
def STRL  : StoreRILPC<"strl", 0xC4F, aligned_store, GR32>;
def STGRL : StoreRILPC<"stgrl", 0xC4B, aligned_store, GR64>;

// 8-bit immediate stores to 8-bit fields.
defm MVI : StoreSIPair<"mvi", 0x92, 0xEB52, truncstorei8, imm32zx8trunc>;

// 16-bit immediate stores to 16-, 32- or 64-bit fields.
def MVHHI : StoreSIL<"mvhhi", 0xE544, truncstorei16, imm32sx16trunc>;
def MVHI  : StoreSIL<"mvhi",  0xE54C, store,         imm32sx16>;
def MVGHI : StoreSIL<"mvghi", 0xE548, store,         imm64sx16>;

// Memory-to-memory moves.
let mayLoad = 1, mayStore = 1 in
  defm MVC : MemorySS<"mvc", 0xD2, z_mvc>;
let mayLoad = 1, mayStore = 1, Defs = [CC] in {
  def MVCL  : SideEffectBinaryMemMemRR<"mvcl", 0x0E, GR128, GR128>;
  def MVCLE : SideEffectTernaryMemMemRS<"mvcle", 0xA8, GR128, GR128>;
  def MVCLU : SideEffectTernaryMemMemRSY<"mvclu", 0xEB8E, GR128, GR128>;
}

// Memset[Length][Byte] pseudos.
def MemsetImmImm : MemsetPseudo<imm64, imm32zx8trunc>;
def MemsetImmReg : MemsetPseudo<imm64, GR32>;
def MemsetRegImm : MemsetPseudo<ADDR64, imm32zx8trunc>;
def MemsetRegReg : MemsetPseudo<ADDR64, GR32>;

// Move right.
let Predicates = [FeatureMiscellaneousExtensions3],
    mayLoad = 1, mayStore = 1, Uses = [R0L] in
  def MVCRL : SideEffectBinarySSE<"mvcrl", 0xE50A>;

// String moves.
let mayLoad = 1, mayStore = 1, Defs = [CC] in
  defm MVST : StringRRE<"mvst", 0xB255, z_stpcpy>;

//===----------------------------------------------------------------------===//
// Conditional move instructions
//===----------------------------------------------------------------------===//

let Predicates = [FeatureMiscellaneousExtensions3], Uses = [CC] in {
  // Select.
  let isCommutable = 1 in {
    // Expands to SELR or SELFHR or a branch-and-move sequence,
    // depending on the choice of registers.
    def  SELRMux : CondBinaryRRFaPseudo<"MUXselr", GRX32, GRX32, GRX32>;
    defm SELFHR  : CondBinaryRRFaPair<"selfhr", 0xB9C0, GRH32, GRH32, GRH32>;
    defm SELR    : CondBinaryRRFaPair<"selr",   0xB9F0, GR32, GR32, GR32>;
    defm SELGR   : CondBinaryRRFaPair<"selgr",  0xB9E3, GR64, GR64, GR64>;
  }

  // Define AsmParser extended mnemonics for each general condition-code mask.
  foreach V = [ "E", "NE", "H", "NH", "L", "NL", "HE", "NHE", "LE", "NLE",
                "Z", "NZ", "P", "NP", "M", "NM", "LH", "NLH", "O", "NO" ] in {
    def SELRAsm#V   : FixedCondBinaryRRFa<CV<V>, "selr",   0xB9F0,
                                          GR32, GR32, GR32>;
    def SELFHRAsm#V : FixedCondBinaryRRFa<CV<V>, "selfhr", 0xB9C0,
                                          GRH32, GRH32, GRH32>;
    def SELGRAsm#V  : FixedCondBinaryRRFa<CV<V>, "selgr",  0xB9E3,
                                          GR64, GR64, GR64>;
  }
}

let Predicates = [FeatureLoadStoreOnCond2], Uses = [CC] in {
  // Load immediate on condition.  Matched via DAG pattern and created
  // by the PeepholeOptimizer via FoldImmediate.

  // Expands to LOCHI or LOCHHI, depending on the choice of register.
  def LOCHIMux : CondBinaryRIEPseudo<GRX32, imm32sx16>;
  defm LOCHHI  : CondBinaryRIEPair<"lochhi", 0xEC4E, GRH32, imm32sx16>;
  defm LOCHI   : CondBinaryRIEPair<"lochi",  0xEC42, GR32, imm32sx16>;
  defm LOCGHI  : CondBinaryRIEPair<"locghi", 0xEC46, GR64, imm64sx16>;

  // Move register on condition.  Matched via DAG pattern and
  // created by early if-conversion.
  let isCommutable = 1 in {
    // Expands to LOCR or LOCFHR or a branch-and-move sequence,
    // depending on the choice of registers.
    def LOCRMux : CondBinaryRRFPseudo<"MUXlocr", GRX32, GRX32>;
    defm LOCFHR : CondBinaryRRFPair<"locfhr", 0xB9E0, GRH32, GRH32>;
  }

  // Load on condition.  Matched via DAG pattern.
  // Expands to LOC or LOCFH, depending on the choice of register.
  defm LOCMux : CondUnaryRSYPseudoAndMemFold<"MUXloc", simple_load, GRX32, 4>;
  defm LOCFH : CondUnaryRSYPair<"locfh", 0xEBE0, simple_load, GRH32, 4>;

  // Store on condition.  Expanded from CondStore* pseudos.
  // Expands to STOC or STOCFH, depending on the choice of register.
  def STOCMux : CondStoreRSYPseudo<GRX32, 4>;
  defm STOCFH : CondStoreRSYPair<"stocfh", 0xEBE1, GRH32, 4>;

  // Define AsmParser extended mnemonics for each general condition-code mask.
  foreach V = [ "E", "NE", "H", "NH", "L", "NL", "HE", "NHE", "LE", "NLE",
                "Z", "NZ", "P", "NP", "M", "NM", "LH", "NLH", "O", "NO" ] in {
    def LOCHIAsm#V  : FixedCondBinaryRIE<CV<V>, "lochi",  0xEC42, GR32,
                                         imm32sx16>;
    def LOCGHIAsm#V : FixedCondBinaryRIE<CV<V>, "locghi", 0xEC46, GR64,
                                         imm64sx16>;
    def LOCHHIAsm#V : FixedCondBinaryRIE<CV<V>, "lochhi", 0xEC4E, GRH32,
                                         imm32sx16>;
    def LOCFHRAsm#V : FixedCondBinaryRRF<CV<V>, "locfhr", 0xB9E0, GRH32, GRH32>;
    def LOCFHAsm#V  : FixedCondUnaryRSY<CV<V>, "locfh",  0xEBE0, GRH32, 4>;
    def STOCFHAsm#V : FixedCondStoreRSY<CV<V>, "stocfh", 0xEBE1, GRH32, 4>;
  }
}

let Predicates = [FeatureLoadStoreOnCond], Uses = [CC] in {
  // Move register on condition.  Matched via DAG pattern and
  // created by early if-conversion.
  let isCommutable = 1 in {
    defm LOCR  : CondBinaryRRFPair<"locr",  0xB9F2, GR32, GR32>;
    defm LOCGR : CondBinaryRRFPair<"locgr", 0xB9E2, GR64, GR64>;
  }

  // Load on condition.  Matched via DAG pattern.
  defm LOC  : CondUnaryRSYPair<"loc",  0xEBF2, simple_load, GR32, 4>;
  defm LOCG : CondUnaryRSYPairAndMemFold<"locg", 0xEBE2, simple_load, GR64, 8>;

  // Store on condition.  Expanded from CondStore* pseudos.
  defm STOC  : CondStoreRSYPair<"stoc",  0xEBF3, GR32, 4>;
  defm STOCG : CondStoreRSYPair<"stocg", 0xEBE3, GR64, 8>;

  // Define AsmParser extended mnemonics for each general condition-code mask.
  foreach V = [ "E", "NE", "H", "NH", "L", "NL", "HE", "NHE", "LE", "NLE",
                "Z", "NZ", "P", "NP", "M", "NM", "LH", "NLH", "O", "NO" ] in {
    def LOCRAsm#V   : FixedCondBinaryRRF<CV<V>, "locr",  0xB9F2, GR32, GR32>;
    def LOCGRAsm#V  : FixedCondBinaryRRF<CV<V>, "locgr", 0xB9E2, GR64, GR64>;
    def LOCAsm#V    : FixedCondUnaryRSY<CV<V>, "loc",   0xEBF2, GR32, 4>;
    def LOCGAsm#V   : FixedCondUnaryRSY<CV<V>, "locg",  0xEBE2, GR64, 8>;
    def STOCAsm#V   : FixedCondStoreRSY<CV<V>, "stoc",  0xEBF3, GR32, 4>;
    def STOCGAsm#V  : FixedCondStoreRSY<CV<V>, "stocg", 0xEBE3, GR64, 8>;
  }
}
//===----------------------------------------------------------------------===//
// Sign extensions
//===----------------------------------------------------------------------===//
//
// Note that putting these before zero extensions mean that we will prefer
// them for anyextload*.  There's not really much to choose between the two
// either way, but signed-extending loads have a short LH and a long LHY,
// while zero-extending loads have only the long LLH.
//
//===----------------------------------------------------------------------===//

// 32-bit extensions from registers.
def LBR : UnaryRRE<"lbr", 0xB926, sext8,  GR32, GR32>;
def LHR : UnaryRRE<"lhr", 0xB927, sext16, GR32, GR32>;

// 64-bit extensions from registers.
def LGBR : UnaryRRE<"lgbr", 0xB906, sext8,  GR64, GR64>;
def LGHR : UnaryRRE<"lghr", 0xB907, sext16, GR64, GR64>;
def LGFR : UnaryRRE<"lgfr", 0xB914, sext32, GR64, GR32>;

let Defs = [CC], CCValues = 0xE, CompareZeroCCMask = 0xE in
  def LTGFR : UnaryRRE<"ltgfr", 0xB912, null_frag, GR64, GR32>;

// Match 32-to-64-bit sign extensions in which the source is already
// in a 64-bit register.
def : Pat<(sext_inreg GR64:$src, i32),
          (LGFR (EXTRACT_SUBREG GR64:$src, subreg_l32))>;

// 32-bit extensions from 8-bit memory.  LBMux expands to LB or LBH,
// depending on the choice of register.
def LBMux : UnaryRXYPseudo<"lb", z_asextloadi8, GRX32, 1>,
            Requires<[FeatureHighWord]>;
def LB  : UnaryRXY<"lb", 0xE376, z_asextloadi8, GR32, 1>;
def LBH : UnaryRXY<"lbh", 0xE3C0, z_asextloadi8, GRH32, 1>,
          Requires<[FeatureHighWord]>;

// 32-bit extensions from 16-bit memory.  LHMux expands to LH or LHH,
// depending on the choice of register.
def LHMux : UnaryRXYPseudo<"lh", z_asextloadi16, GRX32, 2>,
            Requires<[FeatureHighWord]>;
defm LH   : UnaryRXPair<"lh", 0x48, 0xE378, z_asextloadi16, GR32, 2>;
def  LHH  : UnaryRXY<"lhh", 0xE3C4, z_asextloadi16, GRH32, 2>,
            Requires<[FeatureHighWord]>;
def  LHRL : UnaryRILPC<"lhrl", 0xC45, aligned_z_asextloadi16, GR32>;

// 64-bit extensions from memory.
def LGB   : UnaryRXY<"lgb", 0xE377, z_asextloadi8,  GR64, 1>;
def LGH   : UnaryRXY<"lgh", 0xE315, z_asextloadi16, GR64, 2>;
def LGF   : UnaryRXY<"lgf", 0xE314, z_asextloadi32, GR64, 4>;
def LGHRL : UnaryRILPC<"lghrl", 0xC44, aligned_z_asextloadi16, GR64>;
def LGFRL : UnaryRILPC<"lgfrl", 0xC4C, aligned_z_asextloadi32, GR64>;
let Defs = [CC], CCValues = 0xE, CompareZeroCCMask = 0xE in
  def LTGF : UnaryRXY<"ltgf", 0xE332, z_asextloadi32, GR64, 4>;

//===----------------------------------------------------------------------===//
// Zero extensions
//===----------------------------------------------------------------------===//

// 32-bit extensions from registers.

// Expands to LLCR or RISB[LH]G, depending on the choice of registers.
def LLCRMux : UnaryRRPseudo<"llcr", zext8, GRX32, GRX32>,
              Requires<[FeatureHighWord]>;
def LLCR    : UnaryRRE<"llcr", 0xB994, zext8,  GR32, GR32>;
// Expands to LLHR or RISB[LH]G, depending on the choice of registers.
def LLHRMux : UnaryRRPseudo<"llhr", zext16, GRX32, GRX32>,
              Requires<[FeatureHighWord]>;
def LLHR    : UnaryRRE<"llhr", 0xB995, zext16, GR32, GR32>;

// 64-bit extensions from registers.
def LLGCR : UnaryRRE<"llgcr", 0xB984, zext8,  GR64, GR64>;
def LLGHR : UnaryRRE<"llghr", 0xB985, zext16, GR64, GR64>;
def LLGFR : UnaryRRE<"llgfr", 0xB916, zext32, GR64, GR32>;

// Match 32-to-64-bit zero extensions in which the source is already
// in a 64-bit register.
def : Pat<(and GR64:$src, 0xffffffff),
          (LLGFR (EXTRACT_SUBREG GR64:$src, subreg_l32))>;

// 32-bit extensions from 8-bit memory.  LLCMux expands to LLC or LLCH,
// depending on the choice of register.
def LLCMux : UnaryRXYPseudo<"llc", z_azextloadi8, GRX32, 1>,
             Requires<[FeatureHighWord]>;
def LLC  : UnaryRXY<"llc", 0xE394, z_azextloadi8, GR32, 1>;
def LLCH : UnaryRXY<"llch", 0xE3C2, z_azextloadi8, GRH32, 1>,
           Requires<[FeatureHighWord]>;

// 32-bit extensions from 16-bit memory.  LLHMux expands to LLH or LLHH,
// depending on the choice of register.
def LLHMux : UnaryRXYPseudo<"llh", z_azextloadi16, GRX32, 2>,
             Requires<[FeatureHighWord]>;
def LLH   : UnaryRXY<"llh", 0xE395, z_azextloadi16, GR32, 2>;
def LLHH  : UnaryRXY<"llhh", 0xE3C6, z_azextloadi16, GRH32, 2>,
            Requires<[FeatureHighWord]>;
def LLHRL : UnaryRILPC<"llhrl", 0xC42, aligned_z_azextloadi16, GR32>;

// 64-bit extensions from memory.
def LLGC   : UnaryRXY<"llgc", 0xE390, z_azextloadi8,  GR64, 1>;
def LLGH   : UnaryRXY<"llgh", 0xE391, z_azextloadi16, GR64, 2>;
def LLGF   : UnaryRXY<"llgf", 0xE316, z_azextloadi32, GR64, 4>;
def LLGHRL : UnaryRILPC<"llghrl", 0xC46, aligned_z_azextloadi16, GR64>;
def LLGFRL : UnaryRILPC<"llgfrl", 0xC4E, aligned_z_azextloadi32, GR64>;

// 31-to-64-bit zero extensions.
def LLGTR : UnaryRRE<"llgtr", 0xB917, null_frag, GR64, GR64>;
def LLGT  : UnaryRXY<"llgt",  0xE317, null_frag, GR64, 4>;
def : Pat<(and GR64:$src, 0x7fffffff),
          (LLGTR GR64:$src)>;
def : Pat<(and (i64 (z_azextloadi32 bdxaddr20only:$src)), 0x7fffffff),
          (LLGT bdxaddr20only:$src)>;

// Load and zero rightmost byte.
let Predicates = [FeatureLoadAndZeroRightmostByte] in {
  def LLZRGF : UnaryRXY<"llzrgf", 0xE33A, null_frag, GR64, 4>;
  def : Pat<(and (i64 (z_azextloadi32 bdxaddr20only:$src)), 0xffffff00),
            (LLZRGF bdxaddr20only:$src)>;
}

// Load and trap.
let Predicates = [FeatureLoadAndTrap], hasSideEffects = 1 in {
  def LLGFAT : UnaryRXY<"llgfat", 0xE39D, null_frag, GR64, 4>;
  def LLGTAT : UnaryRXY<"llgtat", 0xE39C, null_frag, GR64, 4>;
}

// Extend GR64s to GR128s.
let usesCustomInserter = 1, hasNoSchedulingInfo = 1 in
  def ZEXT128 : Pseudo<(outs GR128:$dst), (ins GR64:$src), []>;

//===----------------------------------------------------------------------===//
// "Any" extensions
//===----------------------------------------------------------------------===//

// Use subregs to populate the "don't care" bits in a 32-bit to 64-bit anyext.
def : Pat<(i64 (anyext GR32:$src)),
          (INSERT_SUBREG (i64 (IMPLICIT_DEF)), GR32:$src, subreg_l32)>;

// Extend GR64s to GR128s.
let usesCustomInserter = 1, hasNoSchedulingInfo = 1 in
  def AEXT128 : Pseudo<(outs GR128:$dst), (ins GR64:$src), []>;

//===----------------------------------------------------------------------===//
// Truncations
//===----------------------------------------------------------------------===//

// Truncations of 64-bit registers to 32-bit registers.
def : Pat<(i32 (trunc GR64:$src)),
          (EXTRACT_SUBREG GR64:$src, subreg_l32)>;

// Truncations of 32-bit registers to 8-bit memory.  STCMux expands to
// STC, STCY or STCH, depending on the choice of register.
def STCMux : StoreRXYPseudo<truncstorei8, GRX32, 1>,
             Requires<[FeatureHighWord]>;
defm STC : StoreRXPair<"stc", 0x42, 0xE372, truncstorei8, GR32, 1>;
def STCH : StoreRXY<"stch", 0xE3C3, truncstorei8, GRH32, 1>,
           Requires<[FeatureHighWord]>;

// Truncations of 32-bit registers to 16-bit memory.  STHMux expands to
// STH, STHY or STHH, depending on the choice of register.
def STHMux : StoreRXYPseudo<truncstorei16, GRX32, 1>,
             Requires<[FeatureHighWord]>;
defm STH : StoreRXPair<"sth", 0x40, 0xE370, truncstorei16, GR32, 2>;
def STHH : StoreRXY<"sthh", 0xE3C7, truncstorei16, GRH32, 2>,
           Requires<[FeatureHighWord]>;
def STHRL : StoreRILPC<"sthrl", 0xC47, aligned_truncstorei16, GR32>;

// Truncations of 64-bit registers to memory.
defm : StoreGR64Pair<STC, STCY, truncstorei8>;
defm : StoreGR64Pair<STH, STHY, truncstorei16>;
def  : StoreGR64PC<STHRL, aligned_truncstorei16>;
defm : StoreGR64Pair<ST, STY, truncstorei32>;
def  : StoreGR64PC<STRL, aligned_truncstorei32>;

// Store characters under mask -- not (yet) used for codegen.
defm STCM : StoreBinaryRSPair<"stcm", 0xBE, 0xEB2D, GR32, 0>;
def STCMH : StoreBinaryRSY<"stcmh", 0xEB2C, GRH32, 0>;

//===----------------------------------------------------------------------===//
// Multi-register moves
//===----------------------------------------------------------------------===//

// Multi-register loads.
defm LM : LoadMultipleRSPair<"lm", 0x98, 0xEB98, GR32>;
def LMG : LoadMultipleRSY<"lmg", 0xEB04, GR64>;
def LMH : LoadMultipleRSY<"lmh", 0xEB96, GRH32>;
def LMD : LoadMultipleSSe<"lmd", 0xEF, GR64>;

// Multi-register stores.
defm STM : StoreMultipleRSPair<"stm", 0x90, 0xEB90, GR32>;
def STMG : StoreMultipleRSY<"stmg", 0xEB24, GR64>;
def STMH : StoreMultipleRSY<"stmh", 0xEB26, GRH32>;

//===----------------------------------------------------------------------===//
// Byte swaps
//===----------------------------------------------------------------------===//

// Byte-swapping register moves.
def LRVR  : UnaryRRE<"lrvr",  0xB91F, bswap, GR32, GR32>;
def LRVGR : UnaryRRE<"lrvgr", 0xB90F, bswap, GR64, GR64>;

// Byte-swapping loads.
def LRVH : UnaryRXY<"lrvh", 0xE31F, z_loadbswap16, GR32, 2>;
def LRV  : UnaryRXY<"lrv",  0xE31E, z_loadbswap32, GR32, 4>;
def LRVG : UnaryRXY<"lrvg", 0xE30F, z_loadbswap64, GR64, 8>;

// Byte-swapping stores.
def STRVH : StoreRXY<"strvh", 0xE33F, z_storebswap16, GR32, 2>;
def STRV  : StoreRXY<"strv",  0xE33E, z_storebswap32, GR32, 4>;
def STRVG : StoreRXY<"strvg", 0xE32F, z_storebswap64, GR64, 8>;

// Byte-swapping memory-to-memory moves.
let mayLoad = 1, mayStore = 1 in
  def MVCIN : SideEffectBinarySSa<"mvcin", 0xE8>;

//===----------------------------------------------------------------------===//
// Load address instructions
//===----------------------------------------------------------------------===//

// Load BDX-style addresses.
let isAsCheapAsAMove = 1, isReMaterializable = 1 in
  defm LA : LoadAddressRXPair<"la", 0x41, 0xE371, bitconvert>;

// Load a PC-relative address.  There's no version of this instruction
// with a 16-bit offset, so there's no relaxation.
let isAsCheapAsAMove = 1, isMoveImm = 1, isReMaterializable = 1 in
  def LARL : LoadAddressRIL<"larl", 0xC00, bitconvert>;

// Load the Global Offset Table address.  This will be lowered into a
//     larl $R1, _GLOBAL_OFFSET_TABLE_
// instruction.
def GOT : Alias<6, (outs GR64:$R1), (ins),
                [(set GR64:$R1, (global_offset_table))]>;

//===----------------------------------------------------------------------===//
// Absolute and Negation
//===----------------------------------------------------------------------===//

let Defs = [CC] in {
  let CCValues = 0xF, CompareZeroCCMask = 0x8 in {
    def LPR  : UnaryRR <"lpr",  0x10,   abs, GR32, GR32>;
    def LPGR : UnaryRRE<"lpgr", 0xB900, abs, GR64, GR64>;
  }
  let CCValues = 0xE, CompareZeroCCMask = 0xE in
    def LPGFR : UnaryRRE<"lpgfr", 0xB910, null_frag, GR64, GR32>;
}
defm : SXU<abs, LPGFR>;

let Defs = [CC] in {
  let CCValues = 0xF, CompareZeroCCMask = 0x8 in {
    def LNR  : UnaryRR <"lnr",  0x11,   z_inegabs, GR32, GR32>;
    def LNGR : UnaryRRE<"lngr", 0xB901, z_inegabs, GR64, GR64>;
  }
  let CCValues = 0xE, CompareZeroCCMask = 0xE in
    def LNGFR : UnaryRRE<"lngfr", 0xB911, null_frag, GR64, GR32>;
}
defm : SXU<z_inegabs, LNGFR>;

let Defs = [CC] in {
  let CCValues = 0xF, CompareZeroCCMask = 0x8 in {
    def LCR  : UnaryRR <"lcr",  0x13,   ineg, GR32, GR32>;
    def LCGR : UnaryRRE<"lcgr", 0xB903, ineg, GR64, GR64>;
  }
  let CCValues = 0xE, CompareZeroCCMask = 0xE in
    def LCGFR : UnaryRRE<"lcgfr", 0xB913, null_frag, GR64, GR32>;
}
defm : SXU<ineg, LCGFR>;

//===----------------------------------------------------------------------===//
// Insertion
//===----------------------------------------------------------------------===//

let isCodeGenOnly = 1 in
  defm IC32 : BinaryRXPair<"ic", 0x43, 0xE373, inserti8, GR32, z_azextloadi8, 1>;
defm IC : BinaryRXPair<"ic", 0x43, 0xE373, inserti8, GR64, z_azextloadi8, 1>;

defm : InsertMem<"inserti8", IC32,  GR32, z_azextloadi8, bdxaddr12pair>;
defm : InsertMem<"inserti8", IC32Y, GR32, z_azextloadi8, bdxaddr20pair>;

defm : InsertMem<"inserti8", IC,  GR64, z_azextloadi8, bdxaddr12pair>;
defm : InsertMem<"inserti8", ICY, GR64, z_azextloadi8, bdxaddr20pair>;

// Insert characters under mask -- not (yet) used for codegen.
let Defs = [CC] in {
  defm ICM : TernaryRSPair<"icm", 0xBF, 0xEB81, GR32, 0>;
  def ICMH : TernaryRSY<"icmh", 0xEB80, GRH32, 0>;
}

// Insertions of a 16-bit immediate, leaving other bits unaffected.
// We don't have or_as_insert equivalents of these operations because
// OI is available instead.
//
// IIxMux expands to II[LH]x, depending on the choice of register.
def IILMux : BinaryRIPseudo<insertll, GRX32, imm32ll16>,
             Requires<[FeatureHighWord]>;
def IIHMux : BinaryRIPseudo<insertlh, GRX32, imm32lh16>,
             Requires<[FeatureHighWord]>;
def IILL : BinaryRI<"iill", 0xA53, insertll, GR32, imm32ll16>;
def IILH : BinaryRI<"iilh", 0xA52, insertlh, GR32, imm32lh16>;
def IIHL : BinaryRI<"iihl", 0xA51, insertll, GRH32, imm32ll16>;
def IIHH : BinaryRI<"iihh", 0xA50, insertlh, GRH32, imm32lh16>;
def IILL64 : BinaryAliasRI<insertll, GR64, imm64ll16>;
def IILH64 : BinaryAliasRI<insertlh, GR64, imm64lh16>;
def IIHL64 : BinaryAliasRI<inserthl, GR64, imm64hl16>;
def IIHH64 : BinaryAliasRI<inserthh, GR64, imm64hh16>;

// ...likewise for 32-bit immediates.  For GR32s this is a general
// full-width move.  (We use IILF rather than something like LLILF
// for 32-bit moves because IILF leaves the upper 32 bits of the
// GR64 unchanged.)
let isAsCheapAsAMove = 1, isMoveImm = 1, isReMaterializable = 1 in {
  def IIFMux : UnaryRIPseudo<bitconvert, GRX32, uimm32>,
               Requires<[FeatureHighWord]>;
  def IILF : UnaryRIL<"iilf", 0xC09, bitconvert, GR32, uimm32>;
  def IIHF : UnaryRIL<"iihf", 0xC08, bitconvert, GRH32, uimm32>;
}
def LFI : InstAlias<"lfi\t$R1, $RI1", (IILF GR32:$R1, uimm32:$RI1)>;
def IILF64 : BinaryAliasRIL<insertlf, GR64, imm64lf32>;
def IIHF64 : BinaryAliasRIL<inserthf, GR64, imm64hf32>;

// An alternative model of inserthf, with the first operand being
// a zero-extended value.
def : Pat<(or (zext32 GR32:$src), imm64hf32:$imm),
          (IIHF64 (INSERT_SUBREG (i64 (IMPLICIT_DEF)), GR32:$src, subreg_l32),
                  imm64hf32:$imm)>;

//===----------------------------------------------------------------------===//
// Addition
//===----------------------------------------------------------------------===//

// Addition producing a signed overflow flag.
let Defs = [CC], CCValues = 0xF, CCIfNoSignedWrap = 1 in {
  // Addition of a register.
  let isCommutable = 1 in {
    defm AR : BinaryRRAndK<"ar", 0x1A, 0xB9F8, z_sadd, GR32, GR32>;
    defm AGR : BinaryRREAndK<"agr", 0xB908, 0xB9E8, z_sadd, GR64, GR64>;
  }
  def AGFR : BinaryRRE<"agfr", 0xB918, null_frag, GR64, GR32>;

  // Addition to a high register.
  def AHHHR : BinaryRRFa<"ahhhr", 0xB9C8, null_frag, GRH32, GRH32, GRH32>,
              Requires<[FeatureHighWord]>;
  def AHHLR : BinaryRRFa<"ahhlr", 0xB9D8, null_frag, GRH32, GRH32, GR32>,
              Requires<[FeatureHighWord]>;

  // Addition of signed 16-bit immediates.
  defm AHIMux : BinaryRIAndKPseudo<"ahimux", z_sadd, GRX32, imm32sx16>;
  defm AHI  : BinaryRIAndK<"ahi",  0xA7A, 0xECD8, z_sadd, GR32, imm32sx16>;
  defm AGHI : BinaryRIAndK<"aghi", 0xA7B, 0xECD9, z_sadd, GR64, imm64sx16>;

  // Addition of signed 32-bit immediates.
  def AFIMux : BinaryRIPseudo<z_sadd, GRX32, simm32>,
               Requires<[FeatureHighWord]>;
  def AFI  : BinaryRIL<"afi",  0xC29, z_sadd, GR32, simm32>;
  def AIH  : BinaryRIL<"aih",  0xCC8, z_sadd, GRH32, simm32>,
             Requires<[FeatureHighWord]>;
  def AGFI : BinaryRIL<"agfi", 0xC28, z_sadd, GR64, imm64sx32>;

  // Addition of memory.
  defm AH  : BinaryRXPair<"ah", 0x4A, 0xE37A, z_sadd, GR32, z_asextloadi16, 2>;
  defm A   : BinaryRXPairAndPseudo<"a",  0x5A, 0xE35A, z_sadd, GR32, z_load, 4>;
  def  AGH : BinaryRXY<"agh", 0xE338, z_sadd, GR64, z_asextloadi16, 2>,
             Requires<[FeatureMiscellaneousExtensions2]>;
  def  AGF : BinaryRXY<"agf", 0xE318, z_sadd, GR64, z_asextloadi32, 4>;
  defm AG  : BinaryRXYAndPseudo<"ag",  0xE308, z_sadd, GR64, z_load, 8>;

  // Addition to memory.
  def ASI  : BinarySIY<"asi",  0xEB6A, add, imm32sx8>;
  def AGSI : BinarySIY<"agsi", 0xEB7A, add, imm64sx8>;
}
defm : SXB<z_sadd, GR64, AGFR>;

// Addition producing a carry.
let Defs = [CC], CCValues = 0xF, IsLogical = 1 in {
  // Addition of a register.
  let isCommutable = 1 in {
    defm ALR : BinaryRRAndK<"alr", 0x1E, 0xB9FA, z_uadd, GR32, GR32>;
    defm ALGR : BinaryRREAndK<"algr", 0xB90A, 0xB9EA, z_uadd, GR64, GR64>;
  }
  def ALGFR : BinaryRRE<"algfr", 0xB91A, null_frag, GR64, GR32>;

  // Addition to a high register.
  def ALHHHR : BinaryRRFa<"alhhhr", 0xB9CA, null_frag, GRH32, GRH32, GRH32>,
               Requires<[FeatureHighWord]>;
  def ALHHLR : BinaryRRFa<"alhhlr", 0xB9DA, null_frag, GRH32, GRH32, GR32>,
               Requires<[FeatureHighWord]>;

  // Addition of signed 16-bit immediates.
  def ALHSIK  : BinaryRIE<"alhsik",  0xECDA, z_uadd, GR32, imm32sx16>,
                Requires<[FeatureDistinctOps]>;
  def ALGHSIK : BinaryRIE<"alghsik", 0xECDB, z_uadd, GR64, imm64sx16>,
                Requires<[FeatureDistinctOps]>;

  // Addition of unsigned 32-bit immediates.
  def ALFI  : BinaryRIL<"alfi",  0xC2B, z_uadd, GR32, uimm32>;
  def ALGFI : BinaryRIL<"algfi", 0xC2A, z_uadd, GR64, imm64zx32>;

  // Addition of signed 32-bit immediates.
  def ALSIH : BinaryRIL<"alsih", 0xCCA, null_frag, GRH32, simm32>,
              Requires<[FeatureHighWord]>;

  // Addition of memory.
  defm AL   : BinaryRXPairAndPseudo<"al", 0x5E, 0xE35E, z_uadd, GR32, z_load, 4>;
  def  ALGF : BinaryRXY<"algf", 0xE31A, z_uadd, GR64, z_azextloadi32, 4>;
  defm ALG  : BinaryRXYAndPseudo<"alg",  0xE30A, z_uadd, GR64, z_load, 8>;

  // Addition to memory.
  def ALSI  : BinarySIY<"alsi",  0xEB6E, null_frag, imm32sx8>;
  def ALGSI : BinarySIY<"algsi", 0xEB7E, null_frag, imm64sx8>;
}
defm : ZXB<z_uadd, GR64, ALGFR>;

// Addition producing and using a carry.
let Defs = [CC], Uses = [CC], CCValues = 0xF, IsLogical = 1 in {
  // Addition of a register.
  def ALCR  : BinaryRRE<"alcr",  0xB998, z_addcarry, GR32, GR32>;
  def ALCGR : BinaryRRE<"alcgr", 0xB988, z_addcarry, GR64, GR64>;

  // Addition of memory.
  def ALC  : BinaryRXY<"alc",  0xE398, z_addcarry, GR32, z_load, 4>;
  def ALCG : BinaryRXY<"alcg", 0xE388, z_addcarry, GR64, z_load, 8>;
}

// Addition that does not modify the condition code.
def ALSIHN : BinaryRIL<"alsihn", 0xCCB, null_frag, GRH32, simm32>,
             Requires<[FeatureHighWord]>;


//===----------------------------------------------------------------------===//
// Subtraction
//===----------------------------------------------------------------------===//

// Subtraction producing a signed overflow flag.
let Defs = [CC], CCValues = 0xF, CompareZeroCCMask = 0x8,
    CCIfNoSignedWrap = 1 in {
  // Subtraction of a register.
  defm SR : BinaryRRAndK<"sr", 0x1B, 0xB9F9, z_ssub, GR32, GR32>;
  def SGFR : BinaryRRE<"sgfr", 0xB919, null_frag, GR64, GR32>;
  defm SGR : BinaryRREAndK<"sgr", 0xB909, 0xB9E9, z_ssub, GR64, GR64>;

  // Subtraction from a high register.
  def SHHHR : BinaryRRFa<"shhhr", 0xB9C9, null_frag, GRH32, GRH32, GRH32>,
              Requires<[FeatureHighWord]>;
  def SHHLR : BinaryRRFa<"shhlr", 0xB9D9, null_frag, GRH32, GRH32, GR32>,
              Requires<[FeatureHighWord]>;

  // Subtraction of memory.
  defm SH  : BinaryRXPair<"sh", 0x4B, 0xE37B, z_ssub, GR32, z_asextloadi16, 2>;
  defm S   : BinaryRXPairAndPseudo<"s", 0x5B, 0xE35B, z_ssub, GR32, z_load, 4>;
  def  SGH : BinaryRXY<"sgh", 0xE339, z_ssub, GR64, z_asextloadi16, 2>,
             Requires<[FeatureMiscellaneousExtensions2]>;
  def  SGF : BinaryRXY<"sgf", 0xE319, z_ssub, GR64, z_asextloadi32, 4>;
  defm SG  : BinaryRXYAndPseudo<"sg",  0xE309, z_ssub, GR64, z_load, 8>;
}
defm : SXB<z_ssub, GR64, SGFR>;

// Subtracting an immediate is the same as adding the negated immediate.
let AddedComplexity = 1 in {
  def : Pat<(z_ssub GR32:$src1, imm32sx16n:$src2),
            (AHIMux GR32:$src1, imm32sx16n:$src2)>,
        Requires<[FeatureHighWord]>;
  def : Pat<(z_ssub GR32:$src1, simm32n:$src2),
            (AFIMux GR32:$src1, simm32n:$src2)>,
        Requires<[FeatureHighWord]>;
  def : Pat<(z_ssub GR32:$src1, imm32sx16n:$src2),
            (AHI GR32:$src1, imm32sx16n:$src2)>;
  def : Pat<(z_ssub GR32:$src1, simm32n:$src2),
            (AFI GR32:$src1, simm32n:$src2)>;
  def : Pat<(z_ssub GR64:$src1, imm64sx16n:$src2),
            (AGHI GR64:$src1, imm64sx16n:$src2)>;
  def : Pat<(z_ssub GR64:$src1, imm64sx32n:$src2),
            (AGFI GR64:$src1, imm64sx32n:$src2)>;
}

// And vice versa in one special case, where we need to load a
// constant into a register in any case, but the negated constant
// requires fewer instructions to load.
def : Pat<(z_saddo GR64:$src1, imm64lh16n:$src2),
          (SGR GR64:$src1, (LLILH imm64lh16n:$src2))>;
def : Pat<(z_saddo GR64:$src1, imm64lf32n:$src2),
          (SGR GR64:$src1, (LLILF imm64lf32n:$src2))>;

// Subtraction producing a carry.
let Defs = [CC], CCValues = 0x7, IsLogical = 1 in {
  // Subtraction of a register.
  defm SLR : BinaryRRAndK<"slr", 0x1F, 0xB9FB, z_usub, GR32, GR32>;
  def SLGFR : BinaryRRE<"slgfr", 0xB91B, null_frag, GR64, GR32>;
  defm SLGR : BinaryRREAndK<"slgr", 0xB90B, 0xB9EB, z_usub, GR64, GR64>;

  // Subtraction from a high register.
  def SLHHHR : BinaryRRFa<"slhhhr", 0xB9CB, null_frag, GRH32, GRH32, GRH32>,
               Requires<[FeatureHighWord]>;
  def SLHHLR : BinaryRRFa<"slhhlr", 0xB9DB, null_frag, GRH32, GRH32, GR32>,
               Requires<[FeatureHighWord]>;

  // Subtraction of unsigned 32-bit immediates.
  def SLFI  : BinaryRIL<"slfi",  0xC25, z_usub, GR32, uimm32>;
  def SLGFI : BinaryRIL<"slgfi", 0xC24, z_usub, GR64, imm64zx32>;

  // Subtraction of memory.
  defm SL   : BinaryRXPairAndPseudo<"sl", 0x5F, 0xE35F, z_usub, GR32, z_load, 4>;
  def  SLGF : BinaryRXY<"slgf", 0xE31B, z_usub, GR64, z_azextloadi32, 4>;
  defm SLG  : BinaryRXYAndPseudo<"slg",  0xE30B, z_usub, GR64, z_load, 8>;
}
defm : ZXB<z_usub, GR64, SLGFR>;

// Subtracting an immediate is the same as adding the negated immediate.
let AddedComplexity = 1 in {
  def : Pat<(z_usub GR32:$src1, imm32sx16n:$src2),
            (ALHSIK GR32:$src1, imm32sx16n:$src2)>,
        Requires<[FeatureDistinctOps]>;
  def : Pat<(z_usub GR64:$src1, imm64sx16n:$src2),
            (ALGHSIK GR64:$src1, imm64sx16n:$src2)>,
        Requires<[FeatureDistinctOps]>;
}

// And vice versa in one special case (but we prefer addition).
def : Pat<(add GR64:$src1, imm64zx32n:$src2),
          (SLGFI GR64:$src1, imm64zx32n:$src2)>;

// Subtraction producing and using a carry.
let Defs = [CC], Uses = [CC], CCValues = 0xF, IsLogical = 1 in {
  // Subtraction of a register.
  def SLBR  : BinaryRRE<"slbr",  0xB999, z_subcarry, GR32, GR32>;
  def SLBGR : BinaryRRE<"slbgr", 0xB989, z_subcarry, GR64, GR64>;

  // Subtraction of memory.
  def SLB  : BinaryRXY<"slb",  0xE399, z_subcarry, GR32, z_load, 4>;
  def SLBG : BinaryRXY<"slbg", 0xE389, z_subcarry, GR64, z_load, 8>;
}


//===----------------------------------------------------------------------===//
// AND
//===----------------------------------------------------------------------===//

let Defs = [CC] in {
  // ANDs of a register.
  let isCommutable = 1, CCValues = 0xC, CompareZeroCCMask = 0x8 in {
    defm NR : BinaryRRAndK<"nr", 0x14, 0xB9F4, and, GR32, GR32>;
    defm NGR : BinaryRREAndK<"ngr", 0xB980, 0xB9E4, and, GR64, GR64>;
  }

  let isConvertibleToThreeAddress = 1 in {
    // ANDs of a 16-bit immediate, leaving other bits unaffected.
    // The CC result only reflects the 16-bit field, not the full register.
    //
    // NIxMux expands to NI[LH]x, depending on the choice of register.
    def NILMux : BinaryRIPseudo<and, GRX32, imm32ll16c>,
                 Requires<[FeatureHighWord]>;
    def NIHMux : BinaryRIPseudo<and, GRX32, imm32lh16c>,
                 Requires<[FeatureHighWord]>;
    def NILL : BinaryRI<"nill", 0xA57, and, GR32, imm32ll16c>;
    def NILH : BinaryRI<"nilh", 0xA56, and, GR32, imm32lh16c>;
    def NIHL : BinaryRI<"nihl", 0xA55, and, GRH32, imm32ll16c>;
    def NIHH : BinaryRI<"nihh", 0xA54, and, GRH32, imm32lh16c>;
    def NILL64 : BinaryAliasRI<and, GR64, imm64ll16c>;
    def NILH64 : BinaryAliasRI<and, GR64, imm64lh16c>;
    def NIHL64 : BinaryAliasRI<and, GR64, imm64hl16c>;
    def NIHH64 : BinaryAliasRI<and, GR64, imm64hh16c>;

    // ANDs of a 32-bit immediate, leaving other bits unaffected.
    // The CC result only reflects the 32-bit field, which means we can
    // use it as a zero indicator for i32 operations but not otherwise.
    let CCValues = 0xC, CompareZeroCCMask = 0x8 in {
      // Expands to NILF or NIHF, depending on the choice of register.
      def NIFMux : BinaryRIPseudo<and, GRX32, uimm32>,
                   Requires<[FeatureHighWord]>;
      def NILF : BinaryRIL<"nilf", 0xC0B, and, GR32, uimm32>;
      def NIHF : BinaryRIL<"nihf", 0xC0A, and, GRH32, uimm32>;
    }
    def NILF64 : BinaryAliasRIL<and, GR64, imm64lf32c>;
    def NIHF64 : BinaryAliasRIL<and, GR64, imm64hf32c>;
  }

  // ANDs of memory.
  let CCValues = 0xC, CompareZeroCCMask = 0x8 in {
    defm N  : BinaryRXPairAndPseudo<"n", 0x54, 0xE354, and, GR32, z_load, 4>;
    defm NG : BinaryRXYAndPseudo<"ng", 0xE380, and, GR64, z_load, 8>;
  }

  // AND to memory
  defm NI : BinarySIPair<"ni", 0x94, 0xEB54, null_frag, imm32zx8>;

  // Block AND.
  let mayLoad = 1, mayStore = 1 in
    defm NC : MemorySS<"nc", 0xD4, z_nc>;
}
defm : RMWIByte<and, bdaddr12pair, NI>;
defm : RMWIByte<and, bdaddr20pair, NIY>;

//===----------------------------------------------------------------------===//
// OR
//===----------------------------------------------------------------------===//

let Defs = [CC] in {
  // ORs of a register.
  let isCommutable = 1, CCValues = 0xC, CompareZeroCCMask = 0x8 in {
    defm OR : BinaryRRAndK<"or", 0x16, 0xB9F6, or, GR32, GR32>;
    defm OGR : BinaryRREAndK<"ogr", 0xB981, 0xB9E6, or, GR64, GR64>;
  }

  // ORs of a 16-bit immediate, leaving other bits unaffected.
  // The CC result only reflects the 16-bit field, not the full register.
  //
  // OIxMux expands to OI[LH]x, depending on the choice of register.
  def OILMux : BinaryRIPseudo<or, GRX32, imm32ll16>,
               Requires<[FeatureHighWord]>;
  def OIHMux : BinaryRIPseudo<or, GRX32, imm32lh16>,
               Requires<[FeatureHighWord]>;
  def OILL : BinaryRI<"oill", 0xA5B, or, GR32, imm32ll16>;
  def OILH : BinaryRI<"oilh", 0xA5A, or, GR32, imm32lh16>;
  def OIHL : BinaryRI<"oihl", 0xA59, or, GRH32, imm32ll16>;
  def OIHH : BinaryRI<"oihh", 0xA58, or, GRH32, imm32lh16>;
  def OILL64 : BinaryAliasRI<or, GR64, imm64ll16>;
  def OILH64 : BinaryAliasRI<or, GR64, imm64lh16>;
  def OIHL64 : BinaryAliasRI<or, GR64, imm64hl16>;
  def OIHH64 : BinaryAliasRI<or, GR64, imm64hh16>;

  // ORs of a 32-bit immediate, leaving other bits unaffected.
  // The CC result only reflects the 32-bit field, which means we can
  // use it as a zero indicator for i32 operations but not otherwise.
  let CCValues = 0xC, CompareZeroCCMask = 0x8 in {
    // Expands to OILF or OIHF, depending on the choice of register.
    def OIFMux : BinaryRIPseudo<or, GRX32, uimm32>,
                 Requires<[FeatureHighWord]>;
    def OILF : BinaryRIL<"oilf", 0xC0D, or, GR32, uimm32>;
    def OIHF : BinaryRIL<"oihf", 0xC0C, or, GRH32, uimm32>;
  }
  def OILF64 : BinaryAliasRIL<or, GR64, imm64lf32>;
  def OIHF64 : BinaryAliasRIL<or, GR64, imm64hf32>;

  // ORs of memory.
  let CCValues = 0xC, CompareZeroCCMask = 0x8 in {
    defm O  : BinaryRXPairAndPseudo<"o", 0x56, 0xE356, or, GR32, z_load, 4>;
    defm OG : BinaryRXYAndPseudo<"og", 0xE381, or, GR64, z_load, 8>;
  }

  // OR to memory
  defm OI : BinarySIPair<"oi", 0x96, 0xEB56, null_frag, imm32zx8>;

  // Block OR.
  let mayLoad = 1, mayStore = 1 in
    defm OC : MemorySS<"oc", 0xD6, z_oc>;
}
defm : RMWIByte<or, bdaddr12pair, OI>;
defm : RMWIByte<or, bdaddr20pair, OIY>;

//===----------------------------------------------------------------------===//
// XOR
//===----------------------------------------------------------------------===//

let Defs = [CC] in {
  // XORs of a register.
  let isCommutable = 1, CCValues = 0xC, CompareZeroCCMask = 0x8 in {
    defm XR : BinaryRRAndK<"xr", 0x17, 0xB9F7, xor, GR32, GR32>;
    defm XGR : BinaryRREAndK<"xgr", 0xB982, 0xB9E7, xor, GR64, GR64>;
  }

  // XORs of a 32-bit immediate, leaving other bits unaffected.
  // The CC result only reflects the 32-bit field, which means we can
  // use it as a zero indicator for i32 operations but not otherwise.
  let CCValues = 0xC, CompareZeroCCMask = 0x8 in {
    // Expands to XILF or XIHF, depending on the choice of register.
    def XIFMux : BinaryRIPseudo<xor, GRX32, uimm32>,
                 Requires<[FeatureHighWord]>;
    def XILF : BinaryRIL<"xilf", 0xC07, xor, GR32, uimm32>;
    def XIHF : BinaryRIL<"xihf", 0xC06, xor, GRH32, uimm32>;
  }
  def XILF64 : BinaryAliasRIL<xor, GR64, imm64lf32>;
  def XIHF64 : BinaryAliasRIL<xor, GR64, imm64hf32>;

  // XORs of memory.
  let CCValues = 0xC, CompareZeroCCMask = 0x8 in {
    defm X  : BinaryRXPairAndPseudo<"x",0x57, 0xE357, xor, GR32, z_load, 4>;
    defm XG : BinaryRXYAndPseudo<"xg", 0xE382, xor, GR64, z_load, 8>;
  }

  // XOR to memory
  defm XI : BinarySIPair<"xi", 0x97, 0xEB57, null_frag, imm32zx8>;

  // Block XOR.
  let mayLoad = 1, mayStore = 1 in
    defm XC : MemorySS<"xc", 0xD7, z_xc>;
}
defm : RMWIByte<xor, bdaddr12pair, XI>;
defm : RMWIByte<xor, bdaddr20pair, XIY>;

//===----------------------------------------------------------------------===//
// Combined logical operations
//===----------------------------------------------------------------------===//

let Predicates = [FeatureMiscellaneousExtensions3],
    Defs = [CC] in {
  // AND with complement.
  let CCValues = 0xC, CompareZeroCCMask = 0x8 in {
    def NCRK : BinaryRRFa<"ncrk", 0xB9F5, andc, GR32, GR32, GR32>;
    def NCGRK : BinaryRRFa<"ncgrk", 0xB9E5, andc, GR64, GR64, GR64>;
  }

  // OR with complement.
  let CCValues = 0xC, CompareZeroCCMask = 0x8 in {
    def OCRK : BinaryRRFa<"ocrk", 0xB975, orc, GR32, GR32, GR32>;
    def OCGRK : BinaryRRFa<"ocgrk", 0xB965, orc, GR64, GR64, GR64>;
  }

  // NAND.
  let isCommutable = 1, CCValues = 0xC, CompareZeroCCMask = 0x8 in {
    def NNRK : BinaryRRFa<"nnrk", 0xB974, nand, GR32, GR32, GR32>;
    def NNGRK : BinaryRRFa<"nngrk", 0xB964, nand, GR64, GR64, GR64>;
  }

  // NOR.
  let isCommutable = 1, CCValues = 0xC, CompareZeroCCMask = 0x8 in {
    def NORK : BinaryRRFa<"nork", 0xB976, nor, GR32, GR32, GR32>;
    def NOGRK : BinaryRRFa<"nogrk", 0xB966, nor, GR64, GR64, GR64>;
    let isAsmParserOnly = 1 in {
      def NOTR : UnaryRRFa<"notr", 0xB976, nor, GR32, GR32>;
      def NOTGR : UnaryRRFa<"notgr", 0xB966, nor, GR64, GR64>;
    }
  }

  // NXOR.
  let isCommutable = 1, CCValues = 0xC, CompareZeroCCMask = 0x8 in {
    def NXRK : BinaryRRFa<"nxrk", 0xB977, nxor, GR32, GR32, GR32>;
    def NXGRK : BinaryRRFa<"nxgrk", 0xB967, nxor, GR64, GR64, GR64>;
  }
}

//===----------------------------------------------------------------------===//
// Multiplication
//===----------------------------------------------------------------------===//

// Multiplication of a register, setting the condition code.  We prefer these
// over MS(G)R if available, even though we cannot use the condition code,
// since they are three-operand instructions.
let Predicates = [FeatureMiscellaneousExtensions2],
    Defs = [CC], isCommutable = 1 in {
  def MSRKC  : BinaryRRFa<"msrkc",  0xB9FD, mul, GR32, GR32, GR32>;
  def MSGRKC : BinaryRRFa<"msgrkc", 0xB9ED, mul, GR64, GR64, GR64>;
}

// Multiplication of a register.
let isCommutable = 1 in {
  def MSR  : BinaryRRE<"msr",  0xB252, mul, GR32, GR32>;
  def MSGR : BinaryRRE<"msgr", 0xB90C, mul, GR64, GR64>;
}
def MSGFR : BinaryRRE<"msgfr", 0xB91C, null_frag, GR64, GR32>;
defm : SXB<mul, GR64, MSGFR>;

// Multiplication of a signed 16-bit immediate.
def MHI  : BinaryRI<"mhi",  0xA7C, mul, GR32, imm32sx16>;
def MGHI : BinaryRI<"mghi", 0xA7D, mul, GR64, imm64sx16>;

// Multiplication of a signed 32-bit immediate.
def MSFI  : BinaryRIL<"msfi",  0xC21, mul, GR32, simm32>;
def MSGFI : BinaryRIL<"msgfi", 0xC20, mul, GR64, imm64sx32>;

// Multiplication of memory.
defm MH   : BinaryRXPair<"mh", 0x4C, 0xE37C, mul, GR32, z_asextloadi16, 2>;
defm MS   : BinaryRXPair<"ms", 0x71, 0xE351, mul, GR32, z_load, 4>;
def  MGH  : BinaryRXY<"mgh", 0xE33C, mul, GR64, z_asextloadi16, 2>,
            Requires<[FeatureMiscellaneousExtensions2]>;
def  MSGF : BinaryRXY<"msgf", 0xE31C, mul, GR64, z_asextloadi32, 4>;
def  MSG  : BinaryRXY<"msg",  0xE30C, mul, GR64, z_load, 8>;

// Multiplication of memory, setting the condition code.
let Predicates = [FeatureMiscellaneousExtensions2], Defs = [CC] in {
  defm MSC  : BinaryRXYAndPseudo<"msc",  0xE353, null_frag, GR32, z_load, 4>;
  defm MSGC : BinaryRXYAndPseudo<"msgc", 0xE383, null_frag, GR64, z_load, 8>;
}

// Multiplication of a register, producing two results.
def MR   : BinaryRR <"mr",    0x1C,   null_frag, GR128, GR32>;
def MGRK : BinaryRRFa<"mgrk", 0xB9EC, null_frag, GR128, GR64, GR64>,
           Requires<[FeatureMiscellaneousExtensions2]>;
def MLR  : BinaryRRE<"mlr",  0xB996, null_frag, GR128, GR32>;
def MLGR : BinaryRRE<"mlgr", 0xB986, null_frag, GR128, GR64>;

def : Pat<(z_smul_lohi GR64:$src1, GR64:$src2),
          (MGRK GR64:$src1, GR64:$src2)>;
def : Pat<(z_umul_lohi GR64:$src1, GR64:$src2),
          (MLGR (AEXT128 GR64:$src1), GR64:$src2)>;

// Multiplication of memory, producing two results.
def M   : BinaryRX <"m",   0x5C,   null_frag, GR128, z_load, 4>;
def MFY : BinaryRXY<"mfy", 0xE35C, null_frag, GR128, z_load, 4>;
def MG  : BinaryRXY<"mg",  0xE384, null_frag, GR128, z_load, 8>,
          Requires<[FeatureMiscellaneousExtensions2]>;
def ML  : BinaryRXY<"ml",  0xE396, null_frag, GR128, z_load, 4>;
def MLG : BinaryRXY<"mlg", 0xE386, null_frag, GR128, z_load, 8>;

def : Pat<(z_smul_lohi GR64:$src1, (i64 (z_load bdxaddr20only:$src2))),
          (MG (AEXT128 GR64:$src1), bdxaddr20only:$src2)>;
def : Pat<(z_umul_lohi GR64:$src1, (i64 (z_load bdxaddr20only:$src2))),
          (MLG (AEXT128 GR64:$src1), bdxaddr20only:$src2)>;

//===----------------------------------------------------------------------===//
// Division and remainder
//===----------------------------------------------------------------------===//

let hasSideEffects = 1 in {  // Do not speculatively execute.
  // Division and remainder, from registers.
  def DR    : BinaryRR <"dr",    0x1D,   null_frag, GR128, GR32>;
  def DSGFR : BinaryRRE<"dsgfr", 0xB91D, null_frag, GR128, GR32>;
  def DSGR  : BinaryRRE<"dsgr",  0xB90D, null_frag, GR128, GR64>;
  def DLR   : BinaryRRE<"dlr",   0xB997, null_frag, GR128, GR32>;
  def DLGR  : BinaryRRE<"dlgr",  0xB987, null_frag, GR128, GR64>;

  // Division and remainder, from memory.
  def D    : BinaryRX <"d",    0x5D,   null_frag, GR128, z_load, 4>;
  def DSGF : BinaryRXY<"dsgf", 0xE31D, null_frag, GR128, z_load, 4>;
  def DSG  : BinaryRXY<"dsg",  0xE30D, null_frag, GR128, z_load, 8>;
  def DL   : BinaryRXY<"dl",   0xE397, null_frag, GR128, z_load, 4>;
  def DLG  : BinaryRXY<"dlg",  0xE387, null_frag, GR128, z_load, 8>;
}
def : Pat<(z_sdivrem GR64:$src1, GR32:$src2),
          (DSGFR (AEXT128 GR64:$src1), GR32:$src2)>;
def : Pat<(z_sdivrem GR64:$src1, (i32 (z_load bdxaddr20only:$src2))),
          (DSGF (AEXT128 GR64:$src1), bdxaddr20only:$src2)>;
def : Pat<(z_sdivrem GR64:$src1, GR64:$src2),
          (DSGR (AEXT128 GR64:$src1), GR64:$src2)>;
def : Pat<(z_sdivrem GR64:$src1, (i64 (z_load bdxaddr20only:$src2))),
          (DSG (AEXT128 GR64:$src1), bdxaddr20only:$src2)>;

def : Pat<(z_udivrem GR32:$src1, GR32:$src2),
          (DLR (ZEXT128 (INSERT_SUBREG (i64 (IMPLICIT_DEF)), GR32:$src1,
                                       subreg_l32)), GR32:$src2)>;
def : Pat<(z_udivrem GR32:$src1, (i32 (z_load bdxaddr20only:$src2))),
          (DL (ZEXT128 (INSERT_SUBREG (i64 (IMPLICIT_DEF)), GR32:$src1,
                                      subreg_l32)), bdxaddr20only:$src2)>;
def : Pat<(z_udivrem GR64:$src1, GR64:$src2),
          (DLGR (ZEXT128 GR64:$src1), GR64:$src2)>;
def : Pat<(z_udivrem GR64:$src1, (i64 (z_load bdxaddr20only:$src2))),
          (DLG (ZEXT128 GR64:$src1), bdxaddr20only:$src2)>;

//===----------------------------------------------------------------------===//
// Shifts
//===----------------------------------------------------------------------===//

// Logical shift left.
defm SLL : BinaryRSAndK<"sll", 0x89, 0xEBDF, shiftop<shl>, GR32>;
def SLLG : BinaryRSY<"sllg", 0xEB0D, shiftop<shl>, GR64>;
def SLDL : BinaryRS<"sldl", 0x8D, null_frag, GR128>;

// Arithmetic shift left.
let Defs = [CC] in {
  defm SLA : BinaryRSAndK<"sla", 0x8B, 0xEBDD, null_frag, GR32>;
  def SLAG : BinaryRSY<"slag", 0xEB0B, null_frag, GR64>;
  def SLDA : BinaryRS<"slda", 0x8F, null_frag, GR128>;
}

// Logical shift right.
defm SRL : BinaryRSAndK<"srl", 0x88, 0xEBDE, shiftop<srl>, GR32>;
def SRLG : BinaryRSY<"srlg", 0xEB0C, shiftop<srl>, GR64>;
def SRDL : BinaryRS<"srdl", 0x8C, null_frag, GR128>;

// Arithmetic shift right.
let Defs = [CC], CCValues = 0xE, CompareZeroCCMask = 0xE in {
  defm SRA : BinaryRSAndK<"sra", 0x8A, 0xEBDC, shiftop<sra>, GR32>;
  def SRAG : BinaryRSY<"srag", 0xEB0A, shiftop<sra>, GR64>;
  def SRDA : BinaryRS<"srda", 0x8E, null_frag, GR128>;
}

// Rotate left.
def RLL  : BinaryRSY<"rll",  0xEB1D, shiftop<rotl>, GR32>;
def RLLG : BinaryRSY<"rllg", 0xEB1C, shiftop<rotl>, GR64>;

// Rotate second operand left and inserted selected bits into first operand.
// These can act like 32-bit operands provided that the constant start and
// end bits (operands 2 and 3) are in the range [32, 64).
let Defs = [CC] in {
  let isCodeGenOnly = 1 in
    def RISBG32 : RotateSelectRIEf<"risbg", 0xEC55, GR32, GR32>;
  let CCValues = 0xE, CompareZeroCCMask = 0xE in {
    def RISBG : RotateSelectRIEf<"risbg", 0xEC55, GR64, GR64>;
    def RISBGZ : RotateSelectRIEf<"risbgz", 0xEC55, GR64, GR64, 0, 128>;
  }
}

// On zEC12 we have a variant of RISBG that does not set CC.
let Predicates = [FeatureMiscellaneousExtensions] in {
  def RISBGN : RotateSelectRIEf<"risbgn", 0xEC59, GR64, GR64>;
  def RISBGNZ : RotateSelectRIEf<"risbgnz", 0xEC59, GR64, GR64, 0, 128>;
}

// Forms of RISBG that only affect one word of the destination register.
// They do not set CC.
let Predicates = [FeatureHighWord] in {
  def RISBMux : RotateSelectRIEfPseudo<GRX32, GRX32>;
  def RISBLL  : RotateSelectAliasRIEf<GR32,  GR32>;
  def RISBLH  : RotateSelectAliasRIEf<GR32,  GRH32>;
  def RISBHL  : RotateSelectAliasRIEf<GRH32, GR32>;
  def RISBHH  : RotateSelectAliasRIEf<GRH32, GRH32>;
  def RISBLG  : RotateSelectRIEf<"risblg", 0xEC51, GR32, GR64>;
  def RISBHG  : RotateSelectRIEf<"risbhg", 0xEC5D, GRH32, GR64>;
}

// Rotate second operand left and perform a logical operation with selected
// bits of the first operand.  The CC result only describes the selected bits,
// so isn't useful for a full comparison against zero.
let Defs = [CC] in {
  def RNSBG : RotateSelectRIEf<"rnsbg", 0xEC54, GR64, GR64>;
  def ROSBG : RotateSelectRIEf<"rosbg", 0xEC56, GR64, GR64>;
  def RXSBG : RotateSelectRIEf<"rxsbg", 0xEC57, GR64, GR64>;
}

//===----------------------------------------------------------------------===//
// Comparison
//===----------------------------------------------------------------------===//

// Signed comparisons.  We put these before the unsigned comparisons because
// some of the signed forms have COMPARE AND BRANCH equivalents whereas none
// of the unsigned forms do.
let Defs = [CC], CCValues = 0xE in {
  // Comparison with a register.
  def CR   : CompareRR <"cr",   0x19,   z_scmp,    GR32, GR32>;
  def CGFR : CompareRRE<"cgfr", 0xB930, null_frag, GR64, GR32>;
  def CGR  : CompareRRE<"cgr",  0xB920, z_scmp,    GR64, GR64>;

  // Comparison with a high register.
  def CHHR : CompareRRE<"chhr", 0xB9CD, null_frag, GRH32, GRH32>,
             Requires<[FeatureHighWord]>;
  def CHLR : CompareRRE<"chlr", 0xB9DD, null_frag, GRH32, GR32>,
             Requires<[FeatureHighWord]>;

  // Comparison with a signed 16-bit immediate.  CHIMux expands to CHI or CIH,
  // depending on the choice of register.
  def CHIMux : CompareRIPseudo<z_scmp, GRX32, imm32sx16>,
               Requires<[FeatureHighWord]>;
  def CHI  : CompareRI<"chi",  0xA7E, z_scmp, GR32, imm32sx16>;
  def CGHI : CompareRI<"cghi", 0xA7F, z_scmp, GR64, imm64sx16>;

  // Comparison with a signed 32-bit immediate.  CFIMux expands to CFI or CIH,
  // depending on the choice of register.
  def CFIMux : CompareRIPseudo<z_scmp, GRX32, simm32>,
               Requires<[FeatureHighWord]>;
  def CFI  : CompareRIL<"cfi",  0xC2D, z_scmp, GR32, simm32>;
  def CIH  : CompareRIL<"cih",  0xCCD, z_scmp, GRH32, simm32>,
             Requires<[FeatureHighWord]>;
  def CGFI : CompareRIL<"cgfi", 0xC2C, z_scmp, GR64, imm64sx32>;

  // Comparison with memory.
  defm CH    : CompareRXPair<"ch", 0x49, 0xE379, z_scmp, GR32, z_asextloadi16, 2>;
  def  CMux  : CompareRXYPseudo<z_scmp, GRX32, z_load, 4>,
               Requires<[FeatureHighWord]>;
  defm C     : CompareRXPair<"c",  0x59, 0xE359, z_scmp, GR32, z_load, 4>;
  def  CHF   : CompareRXY<"chf", 0xE3CD, z_scmp, GRH32, z_load, 4>,
               Requires<[FeatureHighWord]>;
  def  CGH   : CompareRXY<"cgh", 0xE334, z_scmp, GR64, z_asextloadi16, 2>;
  def  CGF   : CompareRXY<"cgf", 0xE330, z_scmp, GR64, z_asextloadi32, 4>;
  def  CG    : CompareRXY<"cg",  0xE320, z_scmp, GR64, z_load, 8>;
  def  CHRL  : CompareRILPC<"chrl",  0xC65, z_scmp, GR32, aligned_z_asextloadi16>;
  def  CRL   : CompareRILPC<"crl",   0xC6D, z_scmp, GR32, aligned_z_load>;
  def  CGHRL : CompareRILPC<"cghrl", 0xC64, z_scmp, GR64, aligned_z_asextloadi16>;
  def  CGFRL : CompareRILPC<"cgfrl", 0xC6C, z_scmp, GR64, aligned_z_asextloadi32>;
  def  CGRL  : CompareRILPC<"cgrl",  0xC68, z_scmp, GR64, aligned_z_load>;

  // Comparison between memory and a signed 16-bit immediate.
  def CHHSI : CompareSIL<"chhsi", 0xE554, z_scmp, z_asextloadi16, imm32sx16>;
  def CHSI  : CompareSIL<"chsi",  0xE55C, z_scmp, z_load, imm32sx16>;
  def CGHSI : CompareSIL<"cghsi", 0xE558, z_scmp, z_load, imm64sx16>;
}
defm : SXB<z_scmp, GR64, CGFR>;

// Unsigned comparisons.
let Defs = [CC], CCValues = 0xE, IsLogical = 1 in {
  // Comparison with a register.
  def CLR   : CompareRR <"clr",   0x15,   z_ucmp,    GR32, GR32>;
  def CLGFR : CompareRRE<"clgfr", 0xB931, null_frag, GR64, GR32>;
  def CLGR  : CompareRRE<"clgr",  0xB921, z_ucmp,    GR64, GR64>;

  // Comparison with a high register.
  def CLHHR : CompareRRE<"clhhr", 0xB9CF, null_frag, GRH32, GRH32>,
              Requires<[FeatureHighWord]>;
  def CLHLR : CompareRRE<"clhlr", 0xB9DF, null_frag, GRH32, GR32>,
              Requires<[FeatureHighWord]>;

  // Comparison with an unsigned 32-bit immediate.  CLFIMux expands to CLFI
  // or CLIH, depending on the choice of register.
  def CLFIMux : CompareRIPseudo<z_ucmp, GRX32, uimm32>,
                Requires<[FeatureHighWord]>;
  def CLFI  : CompareRIL<"clfi",  0xC2F, z_ucmp, GR32, uimm32>;
  def CLIH  : CompareRIL<"clih",  0xCCF, z_ucmp, GRH32, uimm32>,
              Requires<[FeatureHighWord]>;
  def CLGFI : CompareRIL<"clgfi", 0xC2E, z_ucmp, GR64, imm64zx32>;

  // Comparison with memory.
  def  CLMux  : CompareRXYPseudo<z_ucmp, GRX32, z_load, 4>,
                Requires<[FeatureHighWord]>;
  defm CL     : CompareRXPair<"cl", 0x55, 0xE355, z_ucmp, GR32, z_load, 4>;
  def  CLHF   : CompareRXY<"clhf", 0xE3CF, z_ucmp, GRH32, z_load, 4>,
                Requires<[FeatureHighWord]>;
  def  CLGF   : CompareRXY<"clgf", 0xE331, z_ucmp, GR64, z_azextloadi32, 4>;
  def  CLG    : CompareRXY<"clg",  0xE321, z_ucmp, GR64, z_load, 8>;
  def  CLHRL  : CompareRILPC<"clhrl",  0xC67, z_ucmp, GR32,
                             aligned_z_azextloadi16>;
  def  CLRL   : CompareRILPC<"clrl",   0xC6F, z_ucmp, GR32,
                             aligned_z_load>;
  def  CLGHRL : CompareRILPC<"clghrl", 0xC66, z_ucmp, GR64,
                             aligned_z_azextloadi16>;
  def  CLGFRL : CompareRILPC<"clgfrl", 0xC6E, z_ucmp, GR64,
                             aligned_z_azextloadi32>;
  def  CLGRL  : CompareRILPC<"clgrl",  0xC6A, z_ucmp, GR64,
                             aligned_z_load>;

  // Comparison between memory and an unsigned 8-bit immediate.
  defm CLI : CompareSIPair<"cli", 0x95, 0xEB55, z_ucmp, z_azextloadi8, imm32zx8>;

  // Comparison between memory and an unsigned 16-bit immediate.
  def CLHHSI : CompareSIL<"clhhsi", 0xE555, z_ucmp, z_azextloadi16, imm32zx16>;
  def CLFHSI : CompareSIL<"clfhsi", 0xE55D, z_ucmp, z_load, imm32zx16>;
  def CLGHSI : CompareSIL<"clghsi", 0xE559, z_ucmp, z_load, imm64zx16>;
}
defm : ZXB<z_ucmp, GR64, CLGFR>;

// Memory-to-memory comparison.
let mayLoad = 1, Defs = [CC] in {
  defm CLC : CompareMemorySS<"clc", 0xD5, z_clc>;
  def CLCL  : SideEffectBinaryMemMemRR<"clcl", 0x0F, GR128, GR128>;
  def CLCLE : SideEffectTernaryMemMemRS<"clcle", 0xA9, GR128, GR128>;
  def CLCLU : SideEffectTernaryMemMemRSY<"clclu", 0xEB8F, GR128, GR128>;
}

// String comparison.
let mayLoad = 1, Defs = [CC] in
  defm CLST : StringRRE<"clst", 0xB25D, z_strcmp>;

// Test under mask.
let Defs = [CC] in {
  // TMxMux expands to TM[LH]x, depending on the choice of register.
  def TMLMux : CompareRIPseudo<z_tm_reg, GRX32, imm32ll16>,
               Requires<[FeatureHighWord]>;
  def TMHMux : CompareRIPseudo<z_tm_reg, GRX32, imm32lh16>,
               Requires<[FeatureHighWord]>;
  def TMLL : CompareRI<"tmll", 0xA71, z_tm_reg, GR32, imm32ll16>;
  def TMLH : CompareRI<"tmlh", 0xA70, z_tm_reg, GR32, imm32lh16>;
  def TMHL : CompareRI<"tmhl", 0xA73, z_tm_reg, GRH32, imm32ll16>;
  def TMHH : CompareRI<"tmhh", 0xA72, z_tm_reg, GRH32, imm32lh16>;

  def TMLL64 : CompareAliasRI<z_tm_reg, GR64, imm64ll16>;
  def TMLH64 : CompareAliasRI<z_tm_reg, GR64, imm64lh16>;
  def TMHL64 : CompareAliasRI<z_tm_reg, GR64, imm64hl16>;
  def TMHH64 : CompareAliasRI<z_tm_reg, GR64, imm64hh16>;

  defm TM : CompareSIPair<"tm", 0x91, 0xEB51, z_tm_mem, z_anyextloadi8, imm32zx8>;
}

def TML : InstAlias<"tml\t$R, $I", (TMLL GR32:$R, imm32ll16:$I), 0>;
def TMH : InstAlias<"tmh\t$R, $I", (TMLH GR32:$R, imm32lh16:$I), 0>;

// Compare logical characters under mask -- not (yet) used for codegen.
let Defs = [CC] in {
  defm CLM : CompareRSPair<"clm", 0xBD, 0xEB21, GR32, 0>;
  def CLMH : CompareRSY<"clmh", 0xEB20, GRH32, 0>;
}

//===----------------------------------------------------------------------===//
// Prefetch and execution hint
//===----------------------------------------------------------------------===//

let mayLoad = 1, mayStore = 1 in {
  def PFD : PrefetchRXY<"pfd", 0xE336, z_prefetch>;
  def PFDRL : PrefetchRILPC<"pfdrl", 0xC62, z_prefetch>;
}

let Predicates = [FeatureExecutionHint], hasSideEffects = 1 in {
  // Branch Prediction Preload
  def BPP : BranchPreloadSMI<"bpp", 0xC7>;
  def BPRP : BranchPreloadMII<"bprp", 0xC5>;

  // Next Instruction Access Intent
  def NIAI : SideEffectBinaryIE<"niai", 0xB2FA, imm32zx4, imm32zx4>;
}

//===----------------------------------------------------------------------===//
// Atomic operations
//===----------------------------------------------------------------------===//

// A serialization instruction that acts as a barrier for all memory
// accesses, which expands to "bcr 14, 0".
let hasSideEffects = 1 in
def Serialize : Alias<2, (outs), (ins), []>;

let Predicates = [FeatureInterlockedAccess1], Defs = [CC] in {
  def LAA   : LoadAndOpRSY<"laa",   0xEBF8, atomic_load_add_i32, GR32>;
  def LAAG  : LoadAndOpRSY<"laag",  0xEBE8, atomic_load_add_i64, GR64>;
  def LAAL  : LoadAndOpRSY<"laal",  0xEBFA, null_frag, GR32>;
  def LAALG : LoadAndOpRSY<"laalg", 0xEBEA, null_frag, GR64>;
  def LAN   : LoadAndOpRSY<"lan",   0xEBF4, atomic_load_and_i32, GR32>;
  def LANG  : LoadAndOpRSY<"lang",  0xEBE4, atomic_load_and_i64, GR64>;
  def LAO   : LoadAndOpRSY<"lao",   0xEBF6, atomic_load_or_i32, GR32>;
  def LAOG  : LoadAndOpRSY<"laog",  0xEBE6, atomic_load_or_i64, GR64>;
  def LAX   : LoadAndOpRSY<"lax",   0xEBF7, atomic_load_xor_i32, GR32>;
  def LAXG  : LoadAndOpRSY<"laxg",  0xEBE7, atomic_load_xor_i64, GR64>;
}

def ATOMIC_SWAPW   : AtomicLoadWBinaryReg<z_atomic_swapw>;

def ATOMIC_LOADW_AR  : AtomicLoadWBinaryReg<z_atomic_loadw_add>;
def ATOMIC_LOADW_AFI : AtomicLoadWBinaryImm<z_atomic_loadw_add, simm32>;

def ATOMIC_LOADW_SR : AtomicLoadWBinaryReg<z_atomic_loadw_sub>;

def ATOMIC_LOADW_NR   : AtomicLoadWBinaryReg<z_atomic_loadw_and>;
def ATOMIC_LOADW_NILH : AtomicLoadWBinaryImm<z_atomic_loadw_and, imm32lh16c>;

def ATOMIC_LOADW_OR     : AtomicLoadWBinaryReg<z_atomic_loadw_or>;
def ATOMIC_LOADW_OILH   : AtomicLoadWBinaryImm<z_atomic_loadw_or, imm32lh16>;

def ATOMIC_LOADW_XR     : AtomicLoadWBinaryReg<z_atomic_loadw_xor>;
def ATOMIC_LOADW_XILF   : AtomicLoadWBinaryImm<z_atomic_loadw_xor, uimm32>;

def ATOMIC_LOADW_NRi    : AtomicLoadWBinaryReg<z_atomic_loadw_nand>;
def ATOMIC_LOADW_NILHi  : AtomicLoadWBinaryImm<z_atomic_loadw_nand,
                                               imm32lh16c>;

def ATOMIC_LOADW_MIN    : AtomicLoadWBinaryReg<z_atomic_loadw_min>;
def ATOMIC_LOADW_MAX    : AtomicLoadWBinaryReg<z_atomic_loadw_max>;
def ATOMIC_LOADW_UMIN   : AtomicLoadWBinaryReg<z_atomic_loadw_umin>;
def ATOMIC_LOADW_UMAX   : AtomicLoadWBinaryReg<z_atomic_loadw_umax>;

def ATOMIC_CMP_SWAPW
  : Pseudo<(outs GR32:$dst), (ins bdaddr20only:$addr, GR32:$cmp, GR32:$swap,
                                  ADDR32:$bitshift, ADDR32:$negbitshift,
                                  uimm32:$bitsize),
           [(set GR32:$dst,
                 (z_atomic_cmp_swapw bdaddr20only:$addr, GR32:$cmp, GR32:$swap,
                                     ADDR32:$bitshift, ADDR32:$negbitshift,
                                     uimm32:$bitsize))]> {
  let Defs = [CC];
  let mayLoad = 1;
  let mayStore = 1;
  let usesCustomInserter = 1;
  let hasNoSchedulingInfo = 1;
}

// Test and set.
let mayLoad = 1, Defs = [CC] in
  def TS : StoreInherentS<"ts", 0x9300, null_frag, 1>;

// Compare and swap.
let Defs = [CC] in {
  defm CS  : CmpSwapRSPair<"cs", 0xBA, 0xEB14, z_atomic_cmp_swap, GR32>;
  def  CSG : CmpSwapRSY<"csg", 0xEB30, z_atomic_cmp_swap, GR64>;
}

// Compare double and swap.
let Defs = [CC] in {
  defm CDS  : CmpSwapRSPair<"cds", 0xBB, 0xEB31, null_frag, GR128>;
  def  CDSG : CmpSwapRSY<"cdsg", 0xEB3E, z_atomic_cmp_swap_128, GR128>;
}

// Compare and swap and store.
let Uses = [R0L, R1D], Defs = [CC], mayStore = 1, mayLoad = 1 in
  def CSST : SideEffectTernarySSF<"csst", 0xC82, GR64>;

// Perform locked operation.
let Uses = [R0L, R1D], Defs = [CC], mayStore = 1, mayLoad =1 in
  def PLO : SideEffectQuaternarySSe<"plo", 0xEE, GR64>;

// Load/store pair from/to quadword.
def LPQ  : UnaryRXY<"lpq", 0xE38F, z_atomic_load_128, GR128, 16>;
def STPQ : StoreRXY<"stpq", 0xE38E, z_atomic_store_128, GR128, 16>;

// Load pair disjoint.
let Predicates = [FeatureInterlockedAccess1], Defs = [CC] in {
  def LPD  : BinarySSF<"lpd", 0xC84, GR128>;
  def LPDG : BinarySSF<"lpdg", 0xC85, GR128>;
}

//===----------------------------------------------------------------------===//
// Translate and convert
//===----------------------------------------------------------------------===//

let mayLoad = 1, mayStore = 1 in
  def TR : SideEffectBinarySSa<"tr", 0xDC>;

let mayLoad = 1, Defs = [CC, R0L, R1D] in {
  def TRT  : SideEffectBinarySSa<"trt", 0xDD>;
  def TRTR : SideEffectBinarySSa<"trtr", 0xD0>;
}

let mayLoad = 1, mayStore = 1, Uses = [R0L] in
  def TRE : SideEffectBinaryMemMemRRE<"tre", 0xB2A5, GR128, GR64>;

let mayLoad = 1, Uses = [R1D], Defs = [CC] in {
  defm TRTE  : BinaryMemRRFcOpt<"trte",  0xB9BF, GR128, GR64>;
  defm TRTRE : BinaryMemRRFcOpt<"trtre", 0xB9BD, GR128, GR64>;
}

let mayLoad = 1, mayStore = 1, Uses = [R0L, R1D], Defs = [CC] in {
  defm TROO : SideEffectTernaryMemMemRRFcOpt<"troo", 0xB993, GR128, GR64>;
  defm TROT : SideEffectTernaryMemMemRRFcOpt<"trot", 0xB992, GR128, GR64>;
  defm TRTO : SideEffectTernaryMemMemRRFcOpt<"trto", 0xB991, GR128, GR64>;
  defm TRTT : SideEffectTernaryMemMemRRFcOpt<"trtt", 0xB990, GR128, GR64>;
}

let mayLoad = 1, mayStore = 1, Defs = [CC] in {
  defm CU12 : SideEffectTernaryMemMemRRFcOpt<"cu12", 0xB2A7, GR128, GR128>;
  defm CU14 : SideEffectTernaryMemMemRRFcOpt<"cu14", 0xB9B0, GR128, GR128>;
  defm CU21 : SideEffectTernaryMemMemRRFcOpt<"cu21", 0xB2A6, GR128, GR128>;
  defm CU24 : SideEffectTernaryMemMemRRFcOpt<"cu24", 0xB9B1, GR128, GR128>;
  def  CU41 : SideEffectBinaryMemMemRRE<"cu41", 0xB9B2, GR128, GR128>;
  def  CU42 : SideEffectBinaryMemMemRRE<"cu42", 0xB9B3, GR128, GR128>;

  let isAsmParserOnly = 1 in {
    defm CUUTF : SideEffectTernaryMemMemRRFcOpt<"cuutf", 0xB2A6, GR128, GR128>;
    defm CUTFU : SideEffectTernaryMemMemRRFcOpt<"cutfu", 0xB2A7, GR128, GR128>;
  }
}

//===----------------------------------------------------------------------===//
// Message-security assist
//===----------------------------------------------------------------------===//

let mayLoad = 1, mayStore = 1, Uses = [R0L, R1D], Defs = [CC] in {
  def KM  : SideEffectBinaryMemMemRRE<"km",  0xB92E, GR128, GR128>;
  def KMC : SideEffectBinaryMemMemRRE<"kmc", 0xB92F, GR128, GR128>;

  def KIMD : SideEffectBinaryMemRRE<"kimd", 0xB93E, GR64, GR128>;
  def KLMD : SideEffectBinaryMemRRE<"klmd", 0xB93F, GR64, GR128>;
  def KMAC : SideEffectBinaryMemRRE<"kmac", 0xB91E, GR64, GR128>;

  let Predicates = [FeatureMessageSecurityAssist4] in {
    def KMF   : SideEffectBinaryMemMemRRE<"kmf", 0xB92A, GR128, GR128>;
    def KMO   : SideEffectBinaryMemMemRRE<"kmo", 0xB92B, GR128, GR128>;
    def KMCTR : SideEffectTernaryMemMemMemRRFb<"kmctr", 0xB92D,
                                               GR128, GR128, GR128>;
    def PCC   : SideEffectInherentRRE<"pcc", 0xB92C>;
  }

  let Predicates = [FeatureMessageSecurityAssist5] in
    def PPNO : SideEffectBinaryMemMemRRE<"ppno", 0xB93C, GR128, GR128>;
  let Predicates = [FeatureMessageSecurityAssist7], isAsmParserOnly = 1 in
    def PRNO : SideEffectBinaryMemMemRRE<"prno", 0xB93C, GR128, GR128>;

  let Predicates = [FeatureMessageSecurityAssist8] in
    def KMA : SideEffectTernaryMemMemMemRRFb<"kma", 0xB929,
                                              GR128, GR128, GR128>;

  let Predicates = [FeatureMessageSecurityAssist9] in
    def KDSA : SideEffectBinaryMemRRE<"kdsa", 0xB93A, GR64, GR128>;
}

//===----------------------------------------------------------------------===//
// Guarded storage
//===----------------------------------------------------------------------===//

// These instructions use and/or modify the guarded storage control
// registers, which we do not otherwise model, so they should have
// hasSideEffects.
let Predicates = [FeatureGuardedStorage], hasSideEffects = 1 in {
  def LGG : UnaryRXY<"lgg", 0xE34C, null_frag, GR64, 8>;
  def LLGFSG : UnaryRXY<"llgfsg", 0xE348, null_frag, GR64, 4>;

  let mayLoad = 1 in
    def LGSC : SideEffectBinaryRXY<"lgsc", 0xE34D, GR64>;
  let mayStore = 1 in
    def STGSC : SideEffectBinaryRXY<"stgsc", 0xE349, GR64>;
}

//===----------------------------------------------------------------------===//
// Decimal arithmetic
//===----------------------------------------------------------------------===//

defm CVB  : BinaryRXPair<"cvb",0x4F, 0xE306, null_frag, GR32, z_load, 4>;
def  CVBG : BinaryRXY<"cvbg", 0xE30E, null_frag, GR64, z_load, 8>;

defm CVD  : StoreRXPair<"cvd", 0x4E, 0xE326, null_frag, GR32, 4>;
def  CVDG : StoreRXY<"cvdg", 0xE32E, null_frag, GR64, 8>;

let mayLoad = 1, mayStore = 1 in {
  def MVN : SideEffectBinarySSa<"mvn", 0xD1>;
  def MVZ : SideEffectBinarySSa<"mvz", 0xD3>;
  def MVO : SideEffectBinarySSb<"mvo", 0xF1>;

  def PACK : SideEffectBinarySSb<"pack", 0xF2>;
  def PKA  : SideEffectBinarySSf<"pka", 0xE9>;
  def PKU  : SideEffectBinarySSf<"pku", 0xE1>;
  def UNPK : SideEffectBinarySSb<"unpk", 0xF3>;
  let Defs = [CC] in {
    def UNPKA : SideEffectBinarySSa<"unpka", 0xEA>;
    def UNPKU : SideEffectBinarySSa<"unpku", 0xE2>;
  }
}

let mayLoad = 1, mayStore = 1 in {
  let Defs = [CC] in {
    def AP : SideEffectBinarySSb<"ap", 0xFA>;
    def SP : SideEffectBinarySSb<"sp", 0xFB>;
    def ZAP : SideEffectBinarySSb<"zap", 0xF8>;
    def SRP : SideEffectTernarySSc<"srp", 0xF0>;
  }
  def MP : SideEffectBinarySSb<"mp", 0xFC>;
  def DP : SideEffectBinarySSb<"dp", 0xFD>;
  let Defs = [CC] in {
    def ED : SideEffectBinarySSa<"ed", 0xDE>;
    def EDMK : SideEffectBinarySSa<"edmk", 0xDF>;
  }
}

let Defs = [CC] in {
  def CP : CompareSSb<"cp", 0xF9>;
  def TP : TestRSL<"tp", 0xEBC0>;
}

//===----------------------------------------------------------------------===//
// Access registers
//===----------------------------------------------------------------------===//

// Read a 32-bit access register into a GR32.  As with all GR32 operations,
// the upper 32 bits of the enclosing GR64 remain unchanged, which is useful
// when a 64-bit address is stored in a pair of access registers.
def EAR : UnaryRRE<"ear", 0xB24F, null_frag, GR32, AR32>;

// Set access register.
def SAR : UnaryRRE<"sar", 0xB24E, null_frag, AR32, GR32>;

// Copy access register.
def CPYA : UnaryRRE<"cpya", 0xB24D, null_frag, AR32, AR32>;

// Load address extended.
defm LAE : LoadAddressRXPair<"lae", 0x51, 0xE375, null_frag>;

// Load access multiple.
defm LAM : LoadMultipleRSPair<"lam", 0x9A, 0xEB9A, AR32>;

// Store access multiple.
defm STAM : StoreMultipleRSPair<"stam", 0x9B, 0xEB9B, AR32>;

//===----------------------------------------------------------------------===//
// Program mask and addressing mode
//===----------------------------------------------------------------------===//

// Extract CC and program mask into a register.  CC ends up in bits 29 and 28.
let Uses = [CC] in
  def IPM : InherentRRE<"ipm", 0xB222, GR32, z_ipm>;

// Set CC and program mask from a register.
let hasSideEffects = 1, Defs = [CC] in
  def SPM : SideEffectUnaryRR<"spm", 0x04, GR32>;

// Branch and link - like BAS, but also extracts CC and program mask.
let isCall = 1, Uses = [CC], Defs = [CC] in {
  def BAL  : CallRX<"bal", 0x45>;
  def BALR : CallRR<"balr", 0x05>;
}

// Test addressing mode.
let Defs = [CC] in
  def TAM : SideEffectInherentE<"tam", 0x010B>;

// Set addressing mode.
let hasSideEffects = 1 in {
  def SAM24 : SideEffectInherentE<"sam24", 0x010C>;
  def SAM31 : SideEffectInherentE<"sam31", 0x010D>;
  def SAM64 : SideEffectInherentE<"sam64", 0x010E>;
}

// Branch and set mode.  Not really a call, but also sets an output register.
let isBranch = 1, isTerminator = 1, isBarrier = 1 in
  def BSM : CallRR<"bsm", 0x0B>;

// Branch and save and set mode.
let isCall = 1, Defs = [CC] in
  def BASSM : CallRR<"bassm", 0x0C>;

//===----------------------------------------------------------------------===//
// Transactional execution
//===----------------------------------------------------------------------===//

let hasSideEffects = 1, Predicates = [FeatureTransactionalExecution] in {
  // Transaction Begin
  let mayStore = 1, usesCustomInserter = 1, Defs = [CC] in {
    def TBEGIN : TestBinarySIL<"tbegin", 0xE560, z_tbegin, imm32zx16>;
    let hasNoSchedulingInfo = 1 in
     def TBEGIN_nofloat : TestBinarySILPseudo<z_tbegin_nofloat, imm32zx16>;
    def TBEGINC : SideEffectBinarySIL<"tbeginc", 0xE561,
                                      int_s390_tbeginc, imm32zx16>;
  }

  // Transaction End
  let Defs = [CC] in
    def TEND : TestInherentS<"tend", 0xB2F8, z_tend>;

  // Transaction Abort
  let isTerminator = 1, isBarrier = 1, mayStore = 1,
      hasSideEffects = 1 in
    def TABORT : SideEffectAddressS<"tabort", 0xB2FC, int_s390_tabort>;

  // Nontransactional Store
  def NTSTG : StoreRXY<"ntstg", 0xE325, int_s390_ntstg, GR64, 8>;

  // Extract Transaction Nesting Depth
  def ETND : InherentRRE<"etnd", 0xB2EC, GR32, int_s390_etnd>;
}

//===----------------------------------------------------------------------===//
// Processor assist
//===----------------------------------------------------------------------===//

let Predicates = [FeatureProcessorAssist] in {
  let hasSideEffects = 1 in
    def PPA : SideEffectTernaryRRFc<"ppa", 0xB2E8, GR64, GR64, imm32zx4>;
  def : Pat<(int_s390_ppa_txassist GR32:$src),
            (PPA (INSERT_SUBREG (i64 (IMPLICIT_DEF)), GR32:$src, subreg_l32),
                 zero_reg, 1)>;
}

//===----------------------------------------------------------------------===//
// Miscellaneous Instructions.
//===----------------------------------------------------------------------===//

// Find leftmost one, AKA count leading zeros.  The instruction actually
// returns a pair of GR64s, the first giving the number of leading zeros
// and the second giving a copy of the source with the leftmost one bit
// cleared.  We only use the first result here.
let Defs = [CC] in
  def FLOGR : UnaryRRE<"flogr", 0xB983, null_frag, GR128, GR64>;
def : Pat<(i64 (ctlz GR64:$src)),
          (EXTRACT_SUBREG (FLOGR GR64:$src), subreg_h64)>;

// Population count.  Counts bits set per byte or doubleword.
let Predicates = [FeatureMiscellaneousExtensions3] in {
  let Defs = [CC] in
    def POPCNTOpt : BinaryRRFc<"popcnt", 0xB9E1, GR64, GR64>;
  def : Pat<(ctpop GR64:$src), (POPCNTOpt GR64:$src, 8)>;
}
let Predicates = [FeaturePopulationCount], Defs = [CC] in
  def POPCNT : UnaryRRE<"popcnt", 0xB9E1, z_popcnt, GR64, GR64>;

// Search a block of memory for a character.
let mayLoad = 1, Defs = [CC] in
  defm SRST : StringRRE<"srst", 0xB25E, z_search_string>;
let mayLoad = 1, Defs = [CC], Uses = [R0L] in
  def SRSTU : SideEffectBinaryMemMemRRE<"srstu", 0xB9BE, GR64, GR64>;

// Compare until substring equal.
let mayLoad = 1, Defs = [CC], Uses = [R0L, R1L] in
  def CUSE : SideEffectBinaryMemMemRRE<"cuse", 0xB257, GR128, GR128>;

// Compare and form codeword.
let mayLoad = 1, Defs = [CC, R1D, R2D, R3D], Uses = [R1D, R2D, R3D] in
  def CFC : SideEffectAddressS<"cfc", 0xB21A, null_frag>;

// Update tree.
let mayLoad = 1, mayStore = 1, Defs = [CC, R0D, R1D, R2D, R3D, R5D],
    Uses = [R0D, R1D, R2D, R3D, R4D, R5D] in
  def UPT : SideEffectInherentE<"upt", 0x0102>;

// Checksum.
let mayLoad = 1, Defs = [CC] in
  def CKSM : SideEffectBinaryMemMemRRE<"cksm", 0xB241, GR64, GR128>;

// Compression call.
let mayLoad = 1, mayStore = 1, Defs = [CC, R1D], Uses = [R0L, R1D] in
  def CMPSC : SideEffectBinaryMemMemRRE<"cmpsc", 0xB263, GR128, GR128>;

// Sort lists.
let Predicates = [FeatureEnhancedSort],
    mayLoad = 1, mayStore = 1, Defs = [CC], Uses = [R0L, R1D] in
  def SORTL : SideEffectBinaryMemMemRRE<"sortl", 0xB938, GR128, GR128>;

// Deflate conversion call.
let Predicates = [FeatureDeflateConversion],
    mayLoad = 1, mayStore = 1, Defs = [CC], Uses = [R0L, R1D] in
  def DFLTCC : SideEffectTernaryMemMemRRFa<"dfltcc", 0xB939,
                                           GR128, GR128, GR64>;

// NNPA.
let Predicates = [FeatureNNPAssist],
    mayLoad = 1, mayStore = 1, Defs = [R0D, CC], Uses = [R0D, R1D] in
  def NNPA : SideEffectInherentRRE<"nnpa", 0xB93B>;

// Execute.
let hasSideEffects = 1 in {
  def EX   : SideEffectBinaryRX<"ex", 0x44, ADDR64>;
  def EXRL : SideEffectBinaryRILPC<"exrl", 0xC60, ADDR64>;
  let hasNoSchedulingInfo = 1 in
    def EXRL_Pseudo : Alias<6, (outs), (ins i64imm:$TargetOpc, ADDR64:$lenMinus1,
                                          bdaddr12only:$bdl1, bdaddr12only:$bd2),
                                          []>;
}

//===----------------------------------------------------------------------===//
// .insn directive instructions
//===----------------------------------------------------------------------===//

let isCodeGenOnly = 1, hasSideEffects = 1 in {
  def InsnE   : DirectiveInsnE<(outs), (ins imm64zx16:$enc), ".insn e,$enc", []>;
  def InsnRI  : DirectiveInsnRI<(outs), (ins imm64zx32:$enc, AnyReg:$R1,
                                             imm32sx16:$I2),
                                ".insn ri,$enc,$R1,$I2", []>;
  def InsnRIE : DirectiveInsnRIE<(outs), (ins imm64zx48:$enc, AnyReg:$R1,
                                              AnyReg:$R3, brtarget16:$I2),
                                 ".insn rie,$enc,$R1,$R3,$I2", []>;
  def InsnRIL : DirectiveInsnRIL<(outs), (ins imm64zx48:$enc, AnyReg:$R1,
                                              brtarget32:$I2),
                                 ".insn ril,$enc,$R1,$I2", []>;
  def InsnRILU : DirectiveInsnRIL<(outs), (ins imm64zx48:$enc, AnyReg:$R1,
                                               uimm32:$I2),
                                  ".insn rilu,$enc,$R1,$I2", []>;
  def InsnRIS : DirectiveInsnRIS<(outs),
                                 (ins imm64zx48:$enc, AnyReg:$R1,
                                      imm32sx8:$I2, imm32zx4:$M3,
                                      (bdaddr12only $B4, $D4):$BD4),
                                 ".insn ris,$enc,$R1,$I2,$M3,$BD4", []>;
  def InsnRR : DirectiveInsnRR<(outs),
                               (ins imm64zx16:$enc, AnyReg:$R1, AnyReg:$R2),
                               ".insn rr,$enc,$R1,$R2", []>;
  def InsnRRE : DirectiveInsnRRE<(outs), (ins imm64zx32:$enc,
                                              AnyReg:$R1, AnyReg:$R2),
                                 ".insn rre,$enc,$R1,$R2", []>;
  def InsnRRF : DirectiveInsnRRF<(outs),
                                 (ins imm64zx32:$enc, AnyReg:$R1, AnyReg:$R2,
                                      AnyReg:$R3, imm32zx4:$M4),
                                 ".insn rrf,$enc,$R1,$R2,$R3,$M4", []>;
  def InsnRRS : DirectiveInsnRRS<(outs),
                                 (ins imm64zx48:$enc, AnyReg:$R1,
                                      AnyReg:$R2, imm32zx4:$M3,
                                      (bdaddr12only $B4, $D4):$BD4),
                                 ".insn rrs,$enc,$R1,$R2,$M3,$BD4", []>;
  def InsnRS  : DirectiveInsnRS<(outs),
                                (ins imm64zx32:$enc, AnyReg:$R1,
                                     AnyReg:$R3, (bdaddr12only $B2, $D2):$BD2),
                                ".insn rs,$enc,$R1,$R3,$BD2", []>;
  def InsnRSE : DirectiveInsnRSE<(outs),
                                 (ins imm64zx48:$enc, AnyReg:$R1,
                                      AnyReg:$R3, (bdaddr12only $B2, $D2):$BD2),
                                 ".insn rse,$enc,$R1,$R3,$BD2", []>;
  def InsnRSI : DirectiveInsnRSI<(outs),
                                 (ins imm64zx48:$enc, AnyReg:$R1,
                                      AnyReg:$R3, brtarget16:$RI2),
                                 ".insn rsi,$enc,$R1,$R3,$RI2", []>;
  def InsnRSY : DirectiveInsnRSY<(outs),
                                 (ins imm64zx48:$enc, AnyReg:$R1,
                                      AnyReg:$R3, (bdaddr20only $B2, $D2):$BD2),
                                 ".insn rsy,$enc,$R1,$R3,$BD2", []>;
  def InsnRX  : DirectiveInsnRX<(outs), (ins imm64zx32:$enc, AnyReg:$R1,
                                             (bdxaddr12only $B2, $D2, $X2):$XBD2),
                                ".insn rx,$enc,$R1,$XBD2", []>;
  def InsnRXE : DirectiveInsnRXE<(outs), (ins imm64zx48:$enc, AnyReg:$R1,
                                              (bdxaddr12only $B2, $D2, $X2):$XBD2),
                                 ".insn rxe,$enc,$R1,$XBD2", []>;
  def InsnRXF : DirectiveInsnRXF<(outs),
                                 (ins imm64zx48:$enc, AnyReg:$R1,
                                      AnyReg:$R3, (bdxaddr12only $B2, $D2, $X2):$XBD2),
                                 ".insn rxf,$enc,$R1,$R3,$XBD2", []>;
  def InsnRXY : DirectiveInsnRXY<(outs), (ins imm64zx48:$enc, AnyReg:$R1,
                                              (bdxaddr20only $B2, $D2, $X2):$XBD2),
                                 ".insn rxy,$enc,$R1,$XBD2", []>;
  def InsnS : DirectiveInsnS<(outs),
                             (ins imm64zx32:$enc, (bdaddr12only $B2, $D2):$BD2),
                             ".insn s,$enc,$BD2", []>;
  def InsnSI : DirectiveInsnSI<(outs),
                               (ins imm64zx32:$enc, (bdaddr12only $B1, $D1):$BD1,
                                    imm32sx8:$I2),
                               ".insn si,$enc,$BD1,$I2", []>;
  def InsnSIY : DirectiveInsnSIY<(outs),
                                 (ins imm64zx48:$enc,
                                      (bdaddr20only $B1, $D1):$BD1, imm32zx8:$I2),
                                 ".insn siy,$enc,$BD1,$I2", []>;
  def InsnSIL : DirectiveInsnSIL<(outs),
                                 (ins imm64zx48:$enc, (bdaddr12only $B1, $D1):$BD1,
                                      imm32zx16:$I2),
                                 ".insn sil,$enc,$BD1,$I2", []>;
  def InsnSS : DirectiveInsnSS<(outs),
                               (ins imm64zx48:$enc, (bdraddr12only $B1, $D1, $R1):$RBD1,
                                    (bdaddr12only $B2, $D2):$BD2, AnyReg:$R3),
                               ".insn ss,$enc,$RBD1,$BD2,$R3", []>;
  def InsnSSE : DirectiveInsnSSE<(outs),
                                 (ins imm64zx48:$enc,
                                      (bdaddr12only $B1, $D1):$BD1,(bdaddr12only $B2, $D2):$BD2),
                                 ".insn sse,$enc,$BD1,$BD2", []>;
  def InsnSSF : DirectiveInsnSSF<(outs),
                                 (ins imm64zx48:$enc, (bdaddr12only $B1, $D1):$BD1,
                                      (bdaddr12only $B2, $D2):$BD2, AnyReg:$R3),
                                 ".insn ssf,$enc,$BD1,$BD2,$R3", []>;
  def InsnVRI : DirectiveInsnVRI<(outs),
                                 (ins imm64zx48:$enc, VR128:$V1, VR128:$V2,
                                  imm32zx12:$I3, imm32zx4:$M4, imm32zx4:$M5),
                                 ".insn vri,$enc,$V1,$V2,$I3,$M4,$M5", []>;
  def InsnVRR : DirectiveInsnVRR<(outs),
                                 (ins imm64zx48:$enc, VR128:$V1, VR128:$V2,
                                  VR128:$V3, imm32zx4:$M4, imm32zx4:$M5,
                                  imm32zx4:$M6),
                                  ".insn vrr,$enc,$V1,$V2,$V3,$M4,$M5,$M6", []>;
  def InsnVRS : DirectiveInsnVRS<(outs),
                                 (ins imm64zx48:$enc, AnyReg:$R1, VR128:$V3,
                                  (bdaddr12only $B2, $D2):$BD2, imm32zx4:$M4),
                                 ".insn vrs,$enc,$BD2,$M4", []>;
  def InsnVRV : DirectiveInsnVRV<(outs),
                                 (ins imm64zx48:$enc, VR128:$V1,
                                      (bdvaddr12only $B2, $D2, $V2):$VBD2, imm32zx4:$M3),
                                 ".insn vrv,$enc,$V1,$VBD2,$M3", []>;
  def InsnVRX : DirectiveInsnVRX<(outs),
                                 (ins imm64zx48:$enc, VR128:$V1,
                                  (bdxaddr12only $B2, $D2, $X2):$XBD2, imm32zx4:$M3),
                                 ".insn vrx,$enc,$V1,$XBD2,$M3", []>;
  def InsnVSI : DirectiveInsnVSI<(outs),
                                 (ins imm64zx48:$enc, VR128:$V1,
                                  (bdaddr12only $B2, $D2):$BD2, imm32zx8:$I3),
                                  ".insn vsi,$enc,$V1,$BD2,$I3", []>;
}

//===----------------------------------------------------------------------===//
// Peepholes.
//===----------------------------------------------------------------------===//

// Avoid generating 2 XOR instructions. (xor (and x, y), y) is
// equivalent to (and (xor x, -1), y)
def : Pat<(and (xor GR64:$x, (i64 -1)), GR64:$y),
                          (XGR GR64:$y, (NGR GR64:$y, GR64:$x))>;

// Use LCGR/AGHI for i64 xor with -1.
def : Pat<(xor GR64:$x, (i64 -1)),
          (AGHI (LCGR GR64:$x), (i64 -1))>;

// Shift/rotate instructions only use the last 6 bits of the second operand
// register, so we can safely use NILL (16 fewer bits than NILF) to only AND the
// last 16 bits.
// Complexity is added so that we match this before we match NILF on the AND
// operation alone.
let AddedComplexity = 4 in {
  def : Pat<(shl GR32:$val, (and GR32:$shift, imm32zx16trunc:$imm)),
            (SLL GR32:$val, (NILL GR32:$shift, imm32zx16trunc:$imm), 0)>;

  def : Pat<(sra GR32:$val, (and GR32:$shift, imm32zx16trunc:$imm)),
            (SRA GR32:$val, (NILL GR32:$shift, imm32zx16trunc:$imm), 0)>;

  def : Pat<(srl GR32:$val, (and GR32:$shift, imm32zx16trunc:$imm)),
            (SRL GR32:$val, (NILL GR32:$shift, imm32zx16trunc:$imm), 0)>;

  def : Pat<(shl GR64:$val, (and GR32:$shift, imm32zx16trunc:$imm)),
            (SLLG GR64:$val, (NILL GR32:$shift, imm32zx16trunc:$imm), 0)>;

  def : Pat<(sra GR64:$val, (and GR32:$shift, imm32zx16trunc:$imm)),
            (SRAG GR64:$val, (NILL GR32:$shift, imm32zx16trunc:$imm), 0)>;

  def : Pat<(srl GR64:$val, (and GR32:$shift, imm32zx16trunc:$imm)),
            (SRLG GR64:$val, (NILL GR32:$shift, imm32zx16trunc:$imm), 0)>;

  def : Pat<(rotl GR32:$val, (and GR32:$shift, imm32zx16trunc:$imm)),
            (RLL GR32:$val, (NILL GR32:$shift, imm32zx16trunc:$imm), 0)>;

  def : Pat<(rotl GR64:$val, (and GR32:$shift, imm32zx16trunc:$imm)),
            (RLLG GR64:$val, (NILL GR32:$shift, imm32zx16trunc:$imm), 0)>;
}

// Substitute (x*64-s) with (-s), since shift/rotate instructions only
// use the last 6 bits of the second operand register (making it modulo 64).
let AddedComplexity = 4 in {
  def : Pat<(shl GR64:$val, (sub imm32mod64,  GR32:$shift)),
            (SLLG GR64:$val, (LCR GR32:$shift), 0)>;

  def : Pat<(sra GR64:$val, (sub imm32mod64,  GR32:$shift)),
            (SRAG GR64:$val, (LCR GR32:$shift), 0)>;

  def : Pat<(srl GR64:$val, (sub imm32mod64,  GR32:$shift)),
            (SRLG GR64:$val, (LCR GR32:$shift), 0)>;

  def : Pat<(rotl GR64:$val, (sub imm32mod64,  GR32:$shift)),
            (RLLG GR64:$val, (LCR GR32:$shift), 0)>;
}

// Peepholes for turning scalar operations into block operations.  The length
// is given as one less for these pseudos.
defm : BlockLoadStore<anyextloadi8, i32, MVCImm, NCImm, OCImm, XCImm, 0>;
defm : BlockLoadStore<anyextloadi16, i32, MVCImm, NCImm, OCImm, XCImm, 1>;
defm : BlockLoadStore<load, i32, MVCImm, NCImm, OCImm, XCImm, 3>;
defm : BlockLoadStore<anyextloadi8, i64, MVCImm, NCImm, OCImm, XCImm, 0>;
defm : BlockLoadStore<anyextloadi16, i64, MVCImm, NCImm, OCImm, XCImm, 1>;
defm : BlockLoadStore<anyextloadi32, i64, MVCImm, NCImm, OCImm, XCImm, 3>;
defm : BlockLoadStore<load, i64, MVCImm, NCImm, OCImm, XCImm, 7>;

//===----------------------------------------------------------------------===//
// Mnemonic Aliases
//===----------------------------------------------------------------------===//

def JCT   : MnemonicAlias<"jct", "brct">;
def JCTG  : MnemonicAlias<"jctg", "brctg">;
def JC    : MnemonicAlias<"jc", "brc">;
def JCTH  : MnemonicAlias<"jcth", "brcth">;
def JAS   : MnemonicAlias<"jas", "bras">;
def JASL  : MnemonicAlias<"jasl", "brasl">;
def JXH   : MnemonicAlias<"jxh", "brxh">;
def JXLE  : MnemonicAlias<"jxle", "brxle">;
def JXHG  : MnemonicAlias<"jxhg", "brxhg">;
def JXLEG : MnemonicAlias<"jxleg", "brxlg">;

def BRU   : MnemonicAlias<"bru", "j">;
def BRUL  : MnemonicAlias<"brul", "jg", "att">;
def BRUL_HLASM  : MnemonicAlias<"brul", "jlu", "hlasm">;

foreach V = [ "E", "NE", "H", "NH", "L", "NL", "HE", "NHE", "LE", "NLE",
              "Z", "NZ", "P", "NP", "M", "NM", "LH", "NLH", "O", "NO" ] in {
  defm BRUAsm#V  : MnemonicCondBranchAlias <CV<V>, "br#", "j#">;
  defm BRULAsm#V : MnemonicCondBranchAlias <CV<V>, "br#l", "jg#", "att">;
  defm BRUL_HLASMAsm#V : MnemonicCondBranchAlias <CV<V>, "br#l", "jl#", "hlasm">;
}

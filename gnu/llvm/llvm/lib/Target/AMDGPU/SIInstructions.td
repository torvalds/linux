//===-- SIInstructions.td - SI Instruction Definitions --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This file was originally auto-generated from a GPU register header file and
// all the instruction definitions were originally commented out.  Instructions
// that are not yet supported remain commented out.
//===----------------------------------------------------------------------===//

class GCNPat<dag pattern, dag result> : Pat<pattern, result>, PredicateControl;

class UniformSextInreg<ValueType VT> : PatFrag<
  (ops node:$src),
  (sext_inreg $src, VT),
  [{ return !N->isDivergent(); }]>;

class DivergentSextInreg<ValueType VT> : PatFrag<
  (ops node:$src),
  (sext_inreg $src, VT),
  [{ return N->isDivergent(); }]>;

include "SOPInstructions.td"
include "VOPInstructions.td"
include "SMInstructions.td"
include "FLATInstructions.td"
include "BUFInstructions.td"
include "EXPInstructions.td"
include "DSDIRInstructions.td"
include "VINTERPInstructions.td"

//===----------------------------------------------------------------------===//
// VINTRP Instructions
//===----------------------------------------------------------------------===//

// Used to inject printing of "_e32" suffix for VI (there are "_e64" variants for VI)
def VINTRPDst : VINTRPDstOperand <VGPR_32>;

let Uses = [MODE, M0, EXEC] in {

// FIXME: Specify SchedRW for VINTRP instructions.

multiclass V_INTERP_P1_F32_m : VINTRP_m <
  0x00000000,
  (outs VINTRPDst:$vdst),
  (ins VGPR_32:$vsrc, InterpAttr:$attr, InterpAttrChan:$attrchan),
  "v_interp_p1_f32$vdst, $vsrc, $attr$attrchan",
  [(set f32:$vdst, (int_amdgcn_interp_p1 f32:$vsrc,
                   (i32 timm:$attrchan), (i32 timm:$attr), M0))]
>;

let OtherPredicates = [has32BankLDS, isNotGFX90APlus] in {

defm V_INTERP_P1_F32 : V_INTERP_P1_F32_m;

} // End OtherPredicates = [has32BankLDS, isNotGFX90APlus]

let OtherPredicates = [has16BankLDS, isNotGFX90APlus],
    Constraints = "@earlyclobber $vdst", isAsmParserOnly=1 in {

defm V_INTERP_P1_F32_16bank : V_INTERP_P1_F32_m;

} // End OtherPredicates = [has32BankLDS, isNotGFX90APlus],
  //     Constraints = "@earlyclobber $vdst", isAsmParserOnly=1

let OtherPredicates = [isNotGFX90APlus] in {
let DisableEncoding = "$src0", Constraints = "$src0 = $vdst" in {

defm V_INTERP_P2_F32 : VINTRP_m <
  0x00000001,
  (outs VINTRPDst:$vdst),
  (ins VGPR_32:$src0, VGPR_32:$vsrc, InterpAttr:$attr,
       InterpAttrChan:$attrchan),
  "v_interp_p2_f32$vdst, $vsrc, $attr$attrchan",
  [(set f32:$vdst, (int_amdgcn_interp_p2 f32:$src0, f32:$vsrc,
                   (i32 timm:$attrchan), (i32 timm:$attr), M0))]>;

} // End DisableEncoding = "$src0", Constraints = "$src0 = $vdst"

defm V_INTERP_MOV_F32 : VINTRP_m <
  0x00000002,
  (outs VINTRPDst:$vdst),
  (ins InterpSlot:$vsrc, InterpAttr:$attr, InterpAttrChan:$attrchan),
  "v_interp_mov_f32$vdst, $vsrc, $attr$attrchan",
  [(set f32:$vdst, (int_amdgcn_interp_mov (i32 timm:$vsrc),
                   (i32 timm:$attrchan), (i32 timm:$attr), M0))]>;

} // End OtherPredicates = [isNotGFX90APlus]

} // End Uses = [MODE, M0, EXEC]

//===----------------------------------------------------------------------===//
// Pseudo Instructions
//===----------------------------------------------------------------------===//

// Insert a branch to an endpgm block to use as a fallback trap.
def ENDPGM_TRAP : SPseudoInstSI<
  (outs), (ins),
  [(AMDGPUendpgm_trap)],
  "ENDPGM_TRAP"> {
  let hasSideEffects = 1;
  let usesCustomInserter = 1;
}

def SIMULATED_TRAP : SPseudoInstSI<(outs), (ins), [(AMDGPUsimulated_trap)],
                                   "SIMULATED_TRAP"> {
  let hasSideEffects = 1;
  let usesCustomInserter = 1;
}

def ATOMIC_FENCE : SPseudoInstSI<
  (outs), (ins i32imm:$ordering, i32imm:$scope),
  [(atomic_fence (i32 timm:$ordering), (i32 timm:$scope))],
  "ATOMIC_FENCE $ordering, $scope"> {
  let hasSideEffects = 1;
}

let hasSideEffects = 0, mayLoad = 0, mayStore = 0, Uses = [EXEC] in {

// For use in patterns
def V_CNDMASK_B64_PSEUDO : VOP3Common <(outs VReg_64:$vdst),
  (ins VSrc_b64:$src0, VSrc_b64:$src1, SSrc_b64:$src2), "", []> {
  let isPseudo = 1;
  let isCodeGenOnly = 1;
  let usesCustomInserter = 1;
}

// 64-bit vector move instruction. This is mainly used by the
// SIFoldOperands pass to enable folding of inline immediates.
def V_MOV_B64_PSEUDO : VPseudoInstSI <(outs VReg_64:$vdst),
                                      (ins VSrc_b64:$src0)> {
  let isReMaterializable = 1;
  let isAsCheapAsAMove = 1;
  let isMoveImm = 1;
  let SchedRW = [Write64Bit];
  let Size = 4;
  let UseNamedOperandTable = 1;
}

// 64-bit vector move with dpp. Expanded post-RA.
def V_MOV_B64_DPP_PSEUDO : VOP_DPP_Pseudo <"v_mov_b64_dpp", VOP_I64_I64> {
  let Size = 16; // Requires two 8-byte v_mov_b32_dpp to complete.
}

// 64-bit scalar move immediate instruction. This is used to avoid subregs
// initialization and allow rematerialization.
def S_MOV_B64_IMM_PSEUDO : SPseudoInstSI <(outs SReg_64:$sdst),
                                          (ins i64imm:$src0)> {
  let isReMaterializable = 1;
  let isAsCheapAsAMove = 1;
  let isMoveImm = 1;
  let SchedRW = [WriteSALU, Write64Bit];
  let Size = 4;
  let Uses = [];
  let UseNamedOperandTable = 1;
}

// Pseudoinstruction for @llvm.amdgcn.wqm. It is turned into a copy after the
// WQM pass processes it.
def WQM : PseudoInstSI <(outs unknown:$vdst), (ins unknown:$src0)>;

// Pseudoinstruction for @llvm.amdgcn.softwqm. Like @llvm.amdgcn.wqm it is
// turned into a copy by WQM pass, but does not seed WQM requirements.
def SOFT_WQM : PseudoInstSI <(outs unknown:$vdst), (ins unknown:$src0)>;

// Pseudoinstruction for @llvm.amdgcn.strict.wwm. It is turned into a copy post-RA, so
// that the @earlyclobber is respected. The @earlyclobber is to make sure that
// the instruction that defines $src0 (which is run in Whole Wave Mode) doesn't
// accidentally clobber inactive channels of $vdst.
let Constraints = "@earlyclobber $vdst" in {
def STRICT_WWM : PseudoInstSI <(outs unknown:$vdst), (ins unknown:$src0)>;
def STRICT_WQM : PseudoInstSI <(outs unknown:$vdst), (ins unknown:$src0)>;
}

} // End let hasSideEffects = 0, mayLoad = 0, mayStore = 0, Uses = [EXEC]

def WWM_COPY : SPseudoInstSI <
  (outs unknown:$dst), (ins unknown:$src)> {
  let hasSideEffects = 0;
  let isAsCheapAsAMove = 1;
  let isConvergent = 1;
}

def ENTER_STRICT_WWM : SPseudoInstSI <(outs SReg_1:$sdst), (ins i64imm:$src0)> {
  let Uses = [EXEC];
  let Defs = [EXEC, SCC];
  let hasSideEffects = 0;
  let mayLoad = 0;
  let mayStore = 0;
}

def EXIT_STRICT_WWM : SPseudoInstSI <(outs SReg_1:$sdst), (ins SReg_1:$src0)> {
  let hasSideEffects = 0;
  let mayLoad = 0;
  let mayStore = 0;
}

def ENTER_STRICT_WQM : SPseudoInstSI <(outs SReg_1:$sdst), (ins i64imm:$src0)> {
  let Uses = [EXEC];
  let Defs = [EXEC, SCC];
  let hasSideEffects = 0;
  let mayLoad = 0;
  let mayStore = 0;
}

def EXIT_STRICT_WQM : SPseudoInstSI <(outs SReg_1:$sdst), (ins SReg_1:$src0)> {
  let hasSideEffects = 0;
  let mayLoad = 0;
  let mayStore = 0;
}

let usesCustomInserter = 1 in {
let WaveSizePredicate = isWave32 in
def S_INVERSE_BALLOT_U32 : SPseudoInstSI<
  (outs SReg_32:$sdst), (ins SSrc_b32:$mask),
  [(set i1:$sdst, (int_amdgcn_inverse_ballot i32:$mask))]
>;

let WaveSizePredicate = isWave64 in
def S_INVERSE_BALLOT_U64 : SPseudoInstSI<
  (outs SReg_64:$sdst), (ins SSrc_b64:$mask),
  [(set i1:$sdst, (int_amdgcn_inverse_ballot i64:$mask))]
>;
} // End usesCustomInserter = 1

// Pseudo instructions used for @llvm.fptrunc.round upward
// and @llvm.fptrunc.round downward.
// These intrinsics will be legalized to G_FPTRUNC_ROUND_UPWARD
// and G_FPTRUNC_ROUND_DOWNWARD before being lowered to
// FPTRUNC_UPWARD_PSEUDO and FPTRUNC_DOWNWARD_PSEUDO.
// The final codegen is done in the ModeRegister pass.
let Uses = [MODE, EXEC] in {
def FPTRUNC_UPWARD_PSEUDO : VPseudoInstSI <(outs VGPR_32:$vdst),
  (ins VGPR_32:$src0),
  [(set f16:$vdst, (SIfptrunc_round_upward f32:$src0))]>;

def FPTRUNC_DOWNWARD_PSEUDO : VPseudoInstSI <(outs VGPR_32:$vdst),
  (ins VGPR_32:$src0),
  [(set f16:$vdst, (SIfptrunc_round_downward f32:$src0))]>;
} // End Uses = [MODE, EXEC]

// Invert the exec mask and overwrite the inactive lanes of dst with inactive,
// restoring it after we're done.
let Defs = [SCC], isConvergent = 1 in {
def V_SET_INACTIVE_B32 : VPseudoInstSI <(outs VGPR_32:$vdst),
  (ins VSrc_b32: $src, VSrc_b32:$inactive), []>;

def V_SET_INACTIVE_B64 : VPseudoInstSI <(outs VReg_64:$vdst),
  (ins VSrc_b64: $src, VSrc_b64:$inactive), []>;
} // End Defs = [SCC]

foreach vt = Reg32Types.types in {
def : GCNPat <(vt (int_amdgcn_set_inactive vt:$src, vt:$inactive)),
     (V_SET_INACTIVE_B32 VSrc_b32:$src, VSrc_b32:$inactive)>;
}

foreach vt = Reg64Types.types in {
def : GCNPat <(vt (int_amdgcn_set_inactive vt:$src, vt:$inactive)),
     (V_SET_INACTIVE_B64 VSrc_b64:$src, VSrc_b64:$inactive)>;
}

def : GCNPat<(i32 (int_amdgcn_set_inactive_chain_arg i32:$src, i32:$inactive)),
    (V_SET_INACTIVE_B32 VGPR_32:$src, VGPR_32:$inactive)>;

def : GCNPat<(i64 (int_amdgcn_set_inactive_chain_arg i64:$src, i64:$inactive)),
    (V_SET_INACTIVE_B64 VReg_64:$src, VReg_64:$inactive)>;

let usesCustomInserter = 1, hasSideEffects = 0, mayLoad = 0, mayStore = 0, Uses = [EXEC] in {
  def WAVE_REDUCE_UMIN_PSEUDO_U32 : VPseudoInstSI <(outs SGPR_32:$sdst),
    (ins VSrc_b32: $src, VSrc_b32:$strategy),
    [(set i32:$sdst, (int_amdgcn_wave_reduce_umin i32:$src, i32:$strategy))]> {
  }

  def WAVE_REDUCE_UMAX_PSEUDO_U32 : VPseudoInstSI <(outs SGPR_32:$sdst),
    (ins VSrc_b32: $src, VSrc_b32:$strategy),
    [(set i32:$sdst, (int_amdgcn_wave_reduce_umax i32:$src, i32:$strategy))]> {
  }
}

let usesCustomInserter = 1, Defs = [VCC] in {
def V_ADD_U64_PSEUDO : VPseudoInstSI <
  (outs VReg_64:$vdst), (ins VSrc_b64:$src0, VSrc_b64:$src1),
  [(set VReg_64:$vdst, (DivergentBinFrag<add> i64:$src0, i64:$src1))]
>;

def V_SUB_U64_PSEUDO : VPseudoInstSI <
  (outs VReg_64:$vdst), (ins VSrc_b64:$src0, VSrc_b64:$src1),
  [(set VReg_64:$vdst, (DivergentBinFrag<sub> i64:$src0, i64:$src1))]
>;
} // End usesCustomInserter = 1, Defs = [VCC]

let usesCustomInserter = 1, Defs = [SCC] in {
def S_ADD_U64_PSEUDO : SPseudoInstSI <
  (outs SReg_64:$sdst), (ins SSrc_b64:$src0, SSrc_b64:$src1),
  [(set SReg_64:$sdst, (UniformBinFrag<add> i64:$src0, i64:$src1))]
>;

def S_SUB_U64_PSEUDO : SPseudoInstSI <
  (outs SReg_64:$sdst), (ins SSrc_b64:$src0, SSrc_b64:$src1),
  [(set SReg_64:$sdst, (UniformBinFrag<sub> i64:$src0, i64:$src1))]
>;

def S_ADD_CO_PSEUDO : SPseudoInstSI <
  (outs SReg_32:$sdst, SSrc_i1:$scc_out), (ins SSrc_b32:$src0, SSrc_b32:$src1, SSrc_i1:$scc_in)
>;

def S_SUB_CO_PSEUDO : SPseudoInstSI <
  (outs SReg_32:$sdst, SSrc_i1:$scc_out), (ins SSrc_b32:$src0, SSrc_b32:$src1, SSrc_i1:$scc_in)
>;

def S_UADDO_PSEUDO : SPseudoInstSI <
  (outs SReg_32:$sdst, SSrc_i1:$scc_out), (ins SSrc_b32:$src0, SSrc_b32:$src1)
>;

def S_USUBO_PSEUDO : SPseudoInstSI <
  (outs SReg_32:$sdst, SSrc_i1:$scc_out), (ins SSrc_b32:$src0, SSrc_b32:$src1)
>;

let OtherPredicates = [HasShaderCyclesHiLoRegisters] in
def GET_SHADERCYCLESHILO : SPseudoInstSI<
  (outs SReg_64:$sdst), (ins),
  [(set SReg_64:$sdst, (i64 (readcyclecounter)))]
>;

} // End usesCustomInserter = 1, Defs = [SCC]

let usesCustomInserter = 1 in {
def GET_GROUPSTATICSIZE : SPseudoInstSI <(outs SReg_32:$sdst), (ins),
  [(set SReg_32:$sdst, (int_amdgcn_groupstaticsize))]>;
} // End let usesCustomInserter = 1, SALU = 1

// Wrap an instruction by duplicating it, except for setting isTerminator.
class WrapTerminatorInst<SOP_Pseudo base_inst> : SPseudoInstSI<
      base_inst.OutOperandList,
      base_inst.InOperandList> {
  let Uses = base_inst.Uses;
  let Defs = base_inst.Defs;
  let isTerminator = 1;
  let isAsCheapAsAMove = base_inst.isAsCheapAsAMove;
  let hasSideEffects = base_inst.hasSideEffects;
  let UseNamedOperandTable = base_inst.UseNamedOperandTable;
  let CodeSize = base_inst.CodeSize;
  let SchedRW = base_inst.SchedRW;
}

let WaveSizePredicate = isWave64 in {
def S_MOV_B64_term : WrapTerminatorInst<S_MOV_B64>;
def S_XOR_B64_term : WrapTerminatorInst<S_XOR_B64>;
def S_OR_B64_term : WrapTerminatorInst<S_OR_B64>;
def S_ANDN2_B64_term : WrapTerminatorInst<S_ANDN2_B64>;
def S_AND_B64_term : WrapTerminatorInst<S_AND_B64>;
def S_AND_SAVEEXEC_B64_term : WrapTerminatorInst<S_AND_SAVEEXEC_B64>;
}

let WaveSizePredicate = isWave32 in {
def S_MOV_B32_term : WrapTerminatorInst<S_MOV_B32>;
def S_XOR_B32_term : WrapTerminatorInst<S_XOR_B32>;
def S_OR_B32_term : WrapTerminatorInst<S_OR_B32>;
def S_ANDN2_B32_term : WrapTerminatorInst<S_ANDN2_B32>;
def S_AND_B32_term : WrapTerminatorInst<S_AND_B32>;
def S_AND_SAVEEXEC_B32_term : WrapTerminatorInst<S_AND_SAVEEXEC_B32>;
}


def WAVE_BARRIER : SPseudoInstSI<(outs), (ins),
  [(int_amdgcn_wave_barrier)]> {
  let SchedRW = [];
  let hasNoSchedulingInfo = 1;
  let hasSideEffects = 1;
  let mayLoad = 0;
  let mayStore = 0;
  let isConvergent = 1;
  let FixedSize = 1;
  let Size = 0;
  let isMeta = 1;
}

def SCHED_BARRIER : SPseudoInstSI<(outs), (ins i32imm:$mask),
  [(int_amdgcn_sched_barrier (i32 timm:$mask))]> {
  let SchedRW = [];
  let hasNoSchedulingInfo = 1;
  let hasSideEffects = 1;
  let mayLoad = 0;
  let mayStore = 0;
  let isConvergent = 1;
  let FixedSize = 1;
  let Size = 0;
  let isMeta = 1;
}

def SCHED_GROUP_BARRIER : SPseudoInstSI<
  (outs),
  (ins i32imm:$mask, i32imm:$size, i32imm:$syncid),
  [(int_amdgcn_sched_group_barrier (i32 timm:$mask), (i32 timm:$size), (i32 timm:$syncid))]> {
  let SchedRW = [];
  let hasNoSchedulingInfo = 1;
  let hasSideEffects = 1;
  let mayLoad = 0;
  let mayStore = 0;
  let isConvergent = 1;
  let FixedSize = 1;
  let Size = 0;
  let isMeta = 1;
}

def IGLP_OPT : SPseudoInstSI<(outs), (ins i32imm:$mask),
  [(int_amdgcn_iglp_opt (i32 timm:$mask))]> {
  let SchedRW = [];
  let hasNoSchedulingInfo = 1;
  let hasSideEffects = 1;
  let mayLoad = 0;
  let mayStore = 0;
  let isConvergent = 1;
  let FixedSize = 1;
  let Size = 0;
  let isMeta = 1;
}

// SI pseudo instructions. These are used by the CFG structurizer pass
// and should be lowered to ISA instructions prior to codegen.

// As we have enhanced control flow intrinsics to work under unstructured CFG,
// duplicating such intrinsics can be actually treated as legal. On the contrary,
// by making them non-duplicable, we are observing better code generation result.
// So we choose to mark them non-duplicable in hope of getting better code
// generation as well as simplied CFG during Machine IR optimization stage.

let isTerminator = 1, isNotDuplicable = 1 in {

let OtherPredicates = [EnableLateCFGStructurize] in {
 def SI_NON_UNIFORM_BRCOND_PSEUDO : CFPseudoInstSI <
  (outs),
  (ins SReg_1:$vcc, brtarget:$target),
  [(brcond i1:$vcc, bb:$target)]> {
    let Size = 12;
}
}

def SI_IF: CFPseudoInstSI <
  (outs SReg_1:$dst), (ins SReg_1:$vcc, brtarget:$target),
  [(set i1:$dst, (AMDGPUif i1:$vcc, bb:$target))], 1, 1> {
  let Constraints = "";
  let Size = 12;
  let hasSideEffects = 1;
  let IsNeverUniform = 1;
}

def SI_ELSE : CFPseudoInstSI <
  (outs SReg_1:$dst),
  (ins SReg_1:$src, brtarget:$target), [], 1, 1> {
  let Size = 12;
  let hasSideEffects = 1;
  let IsNeverUniform = 1;
}

def SI_WATERFALL_LOOP : CFPseudoInstSI <
  (outs),
  (ins brtarget:$target), [], 1> {
  let Size = 8;
  let isBranch = 1;
  let Defs = [];
}

def SI_LOOP : CFPseudoInstSI <
  (outs), (ins SReg_1:$saved, brtarget:$target),
  [(AMDGPUloop i1:$saved, bb:$target)], 1, 1> {
  let Size = 8;
  let isBranch = 1;
  let hasSideEffects = 1;
  let IsNeverUniform = 1;
}

} // End isTerminator = 1

def SI_END_CF : CFPseudoInstSI <
  (outs), (ins SReg_1:$saved), [], 1, 1> {
  let Size = 4;
  let isAsCheapAsAMove = 1;
  let isReMaterializable = 1;
  let hasSideEffects = 1;
  let isNotDuplicable = 1; // Not a hard requirement, see long comments above for details.
  let mayLoad = 1; // FIXME: Should not need memory flags
  let mayStore = 1;
}

def SI_IF_BREAK : CFPseudoInstSI <
  (outs SReg_1:$dst), (ins SReg_1:$vcc, SReg_1:$src), []> {
  let Size = 4;
  let isNotDuplicable = 1; // Not a hard requirement, see long comments above for details.
  let isAsCheapAsAMove = 1;
  let isReMaterializable = 1;
}

// Branch to the early termination block of the shader if SCC is 0.
// This uses SCC from a previous SALU operation, i.e. the update of
// a mask of live lanes after a kill/demote operation.
// Only valid in pixel shaders.
def SI_EARLY_TERMINATE_SCC0 : SPseudoInstSI <(outs), (ins)> {
  let Uses = [EXEC,SCC];
}

let Uses = [EXEC] in {

multiclass PseudoInstKill <dag ins> {
  // Even though this pseudo can usually be expanded without an SCC def, we
  // conservatively assume that it has an SCC def, both because it is sometimes
  // required in degenerate cases (when V_CMPX cannot be used due to constant
  // bus limitations) and because it allows us to avoid having to track SCC
  // liveness across basic blocks.
  let Defs = [EXEC,SCC] in
  def _PSEUDO : PseudoInstSI <(outs), ins> {
    let isConvergent = 1;
    let usesCustomInserter = 1;
  }

  let Defs = [EXEC,SCC] in
  def _TERMINATOR : SPseudoInstSI <(outs), ins> {
    let isTerminator = 1;
  }
}

defm SI_KILL_I1 : PseudoInstKill <(ins SCSrc_i1:$src, i1imm:$killvalue)>;
let Defs = [VCC] in
defm SI_KILL_F32_COND_IMM : PseudoInstKill <(ins VSrc_b32:$src0, i32imm:$src1, i32imm:$cond)>;

let Defs = [EXEC,VCC] in
def SI_ILLEGAL_COPY : SPseudoInstSI <
  (outs unknown:$dst), (ins unknown:$src),
  [], " ; illegal copy $src to $dst">;

} // End Uses = [EXEC], Defs = [EXEC,VCC]

// Branch on undef scc. Used to avoid intermediate copy from
// IMPLICIT_DEF to SCC.
def SI_BR_UNDEF : SPseudoInstSI <(outs), (ins SOPPBrTarget:$simm16)> {
  let isTerminator = 1;
  let usesCustomInserter = 1;
  let isBranch = 1;
}

def SI_PS_LIVE : PseudoInstSI <
  (outs SReg_1:$dst), (ins),
  [(set i1:$dst, (int_amdgcn_ps_live))]> {
  let SALU = 1;
}

let Uses = [EXEC] in {
def SI_LIVE_MASK : PseudoInstSI <
  (outs SReg_1:$dst), (ins),
  [(set i1:$dst, (int_amdgcn_live_mask))]> {
  let SALU = 1;
}
let Defs = [EXEC,SCC] in {
// Demote: Turn a pixel shader thread into a helper lane.
def SI_DEMOTE_I1 : SPseudoInstSI <(outs), (ins SCSrc_i1:$src, i1imm:$killvalue)>;
} // End Defs = [EXEC,SCC]
} // End Uses = [EXEC]

def SI_MASKED_UNREACHABLE : SPseudoInstSI <(outs), (ins),
  [(int_amdgcn_unreachable)],
  "; divergent unreachable"> {
  let Size = 0;
  let hasNoSchedulingInfo = 1;
  let FixedSize = 1;
  let isMeta = 1;
  let maybeAtomic = 0;
}

// Used as an isel pseudo to directly emit initialization with an
// s_mov_b32 rather than a copy of another initialized
// register. MachineCSE skips copies, and we don't want to have to
// fold operands before it runs.
def SI_INIT_M0 : SPseudoInstSI <(outs), (ins SSrc_b32:$src)> {
  let Defs = [M0];
  let usesCustomInserter = 1;
  let isAsCheapAsAMove = 1;
  let isReMaterializable = 1;
}

def SI_INIT_EXEC : SPseudoInstSI <
  (outs), (ins i64imm:$src),
  [(int_amdgcn_init_exec (i64 timm:$src))]> {
  let Defs = [EXEC];
  let isAsCheapAsAMove = 1;
}

def SI_INIT_EXEC_FROM_INPUT : SPseudoInstSI <
  (outs), (ins SSrc_b32:$input, i32imm:$shift),
  [(int_amdgcn_init_exec_from_input i32:$input, (i32 timm:$shift))]> {
  let Defs = [EXEC];
}

// Return for returning shaders to a shader variant epilog.
def SI_RETURN_TO_EPILOG : SPseudoInstSI <
  (outs), (ins variable_ops), [(AMDGPUreturn_to_epilog)]> {
  let isTerminator = 1;
  let isBarrier = 1;
  let isReturn = 1;
  let hasNoSchedulingInfo = 1;
  let DisableWQM = 1;
  let FixedSize = 1;

  // TODO: Should this be true?
  let isMeta = 0;
}

// Return for returning function calls.
def SI_RETURN : SPseudoInstSI <
  (outs), (ins), [(AMDGPUret_glue)],
  "; return"> {
  let isTerminator = 1;
  let isBarrier = 1;
  let isReturn = 1;
  let SchedRW = [WriteBranch];
}

// Return for returning function calls without output register.
//
// This version is only needed so we can fill in the output register
// in the custom inserter.
def SI_CALL_ISEL : SPseudoInstSI <
  (outs), (ins SSrc_b64:$src0, unknown:$callee),
  [(AMDGPUcall i64:$src0, tglobaladdr:$callee)]> {
  let Size = 4;
  let isCall = 1;
  let SchedRW = [WriteBranch];
  let usesCustomInserter = 1;
  // TODO: Should really base this on the call target
  let isConvergent = 1;
}

def : GCNPat<
  (AMDGPUcall i64:$src0, (i64 0)),
  (SI_CALL_ISEL $src0, (i64 0))
>;

// Wrapper around s_swappc_b64 with extra $callee parameter to track
// the called function after regalloc.
def SI_CALL : SPseudoInstSI <
  (outs SReg_64:$dst), (ins SSrc_b64:$src0, unknown:$callee)> {
  let Size = 4;
  let FixedSize = 1;
  let isCall = 1;
  let UseNamedOperandTable = 1;
  let SchedRW = [WriteBranch];
  // TODO: Should really base this on the call target
  let isConvergent = 1;
}

class SI_TCRETURN_Pseudo<RegisterClass rc, SDNode sd> : SPseudoInstSI <(outs),
  (ins rc:$src0, unknown:$callee, i32imm:$fpdiff),
  [(sd i64:$src0, tglobaladdr:$callee, i32:$fpdiff)]> {
  let Size = 4;
  let FixedSize = 1;
  let isCall = 1;
  let isTerminator = 1;
  let isReturn = 1;
  let isBarrier = 1;
  let UseNamedOperandTable = 1;
  let SchedRW = [WriteBranch];
  // TODO: Should really base this on the call target
  let isConvergent = 1;
}

// Tail call handling pseudo
def SI_TCRETURN :     SI_TCRETURN_Pseudo<CCR_SGPR_64, AMDGPUtc_return>;
def SI_TCRETURN_GFX : SI_TCRETURN_Pseudo<Gfx_CCR_SGPR_64, AMDGPUtc_return_gfx>;

// Handle selecting indirect tail calls
def : GCNPat<
  (AMDGPUtc_return i64:$src0, (i64 0), (i32 timm:$fpdiff)),
  (SI_TCRETURN CCR_SGPR_64:$src0, (i64 0), i32imm:$fpdiff)
>;

// Handle selecting indirect tail calls for AMDGPU_gfx
def : GCNPat<
  (AMDGPUtc_return_gfx i64:$src0, (i64 0), (i32 timm:$fpdiff)),
  (SI_TCRETURN_GFX Gfx_CCR_SGPR_64:$src0, (i64 0), i32imm:$fpdiff)
>;

// Pseudo for the llvm.amdgcn.cs.chain intrinsic.
// This is essentially a tail call, but it also takes a mask to put in EXEC
// right before jumping to the callee.
class SI_CS_CHAIN_TC<
    ValueType execvt, Predicate wavesizepred,
    RegisterOperand execrc = getSOPSrcForVT<execvt>.ret>
    : SPseudoInstSI <(outs),
      (ins CCR_SGPR_64:$src0, unknown:$callee, i32imm:$fpdiff, execrc:$exec)> {
  let FixedSize = 0;
  let isCall = 1;
  let isTerminator = 1;
  let isBarrier = 1;
  let isReturn = 1;
  let UseNamedOperandTable = 1;
  let SchedRW = [WriteBranch];
  let isConvergent = 1;

  let WaveSizePredicate = wavesizepred;
}

def SI_CS_CHAIN_TC_W32 : SI_CS_CHAIN_TC<i32, isWave32>;
def SI_CS_CHAIN_TC_W64 : SI_CS_CHAIN_TC<i64, isWave64>;

// Handle selecting direct & indirect calls via SI_CS_CHAIN_TC_W32/64
multiclass si_cs_chain_tc_pattern<
  dag callee, ValueType execvt, RegisterOperand execrc, Instruction tc> {
def : GCNPat<
  (AMDGPUtc_return_chain i64:$src0, callee, (i32 timm:$fpdiff), execvt:$exec),
  (tc CCR_SGPR_64:$src0, callee, i32imm:$fpdiff, execrc:$exec)
>;
}

multiclass si_cs_chain_tc_patterns<
  ValueType execvt,
  RegisterOperand execrc = getSOPSrcForVT<execvt>.ret,
  Instruction tc = !if(!eq(execvt, i32), SI_CS_CHAIN_TC_W32, SI_CS_CHAIN_TC_W64)
  > {
  defm direct: si_cs_chain_tc_pattern<(tglobaladdr:$callee), execvt, execrc, tc>;
  defm indirect: si_cs_chain_tc_pattern<(i64 0), execvt, execrc, tc>;
}

defm : si_cs_chain_tc_patterns<i32>;
defm : si_cs_chain_tc_patterns<i64>;

def ADJCALLSTACKUP : SPseudoInstSI<
  (outs), (ins i32imm:$amt0, i32imm:$amt1),
  [(callseq_start timm:$amt0, timm:$amt1)],
  "; adjcallstackup $amt0 $amt1"> {
  let Size = 8; // Worst case. (s_add_u32 + constant)
  let FixedSize = 1;
  let hasSideEffects = 1;
  let usesCustomInserter = 1;
  let SchedRW = [WriteSALU];
  let Defs = [SCC];
}

def ADJCALLSTACKDOWN : SPseudoInstSI<
  (outs), (ins i32imm:$amt1, i32imm:$amt2),
  [(callseq_end timm:$amt1, timm:$amt2)],
  "; adjcallstackdown $amt1"> {
  let Size = 8; // Worst case. (s_add_u32 + constant)
  let hasSideEffects = 1;
  let usesCustomInserter = 1;
  let SchedRW = [WriteSALU];
  let Defs = [SCC];
}

let Defs = [M0, EXEC, SCC],
  UseNamedOperandTable = 1 in {

// SI_INDIRECT_SRC/DST are only used by legacy SelectionDAG indirect
// addressing implementation.
class SI_INDIRECT_SRC<RegisterClass rc> : VPseudoInstSI <
  (outs VGPR_32:$vdst),
  (ins rc:$src, VS_32:$idx, i32imm:$offset)> {
  let usesCustomInserter = 1;
}

class SI_INDIRECT_DST<RegisterClass rc> : VPseudoInstSI <
  (outs rc:$vdst),
  (ins rc:$src, VS_32:$idx, i32imm:$offset, VGPR_32:$val)> {
  let Constraints = "$src = $vdst";
  let usesCustomInserter = 1;
}

def SI_INDIRECT_SRC_V1 : SI_INDIRECT_SRC<VGPR_32>;
def SI_INDIRECT_SRC_V2 : SI_INDIRECT_SRC<VReg_64>;
def SI_INDIRECT_SRC_V4 : SI_INDIRECT_SRC<VReg_128>;
def SI_INDIRECT_SRC_V8 : SI_INDIRECT_SRC<VReg_256>;
def SI_INDIRECT_SRC_V9 : SI_INDIRECT_SRC<VReg_288>;
def SI_INDIRECT_SRC_V10 : SI_INDIRECT_SRC<VReg_320>;
def SI_INDIRECT_SRC_V11 : SI_INDIRECT_SRC<VReg_352>;
def SI_INDIRECT_SRC_V12 : SI_INDIRECT_SRC<VReg_384>;
def SI_INDIRECT_SRC_V16 : SI_INDIRECT_SRC<VReg_512>;
def SI_INDIRECT_SRC_V32 : SI_INDIRECT_SRC<VReg_1024>;

def SI_INDIRECT_DST_V1 : SI_INDIRECT_DST<VGPR_32>;
def SI_INDIRECT_DST_V2 : SI_INDIRECT_DST<VReg_64>;
def SI_INDIRECT_DST_V4 : SI_INDIRECT_DST<VReg_128>;
def SI_INDIRECT_DST_V8 : SI_INDIRECT_DST<VReg_256>;
def SI_INDIRECT_DST_V9 : SI_INDIRECT_DST<VReg_288>;
def SI_INDIRECT_DST_V10 : SI_INDIRECT_DST<VReg_320>;
def SI_INDIRECT_DST_V11 : SI_INDIRECT_DST<VReg_352>;
def SI_INDIRECT_DST_V12 : SI_INDIRECT_DST<VReg_384>;
def SI_INDIRECT_DST_V16 : SI_INDIRECT_DST<VReg_512>;
def SI_INDIRECT_DST_V32 : SI_INDIRECT_DST<VReg_1024>;

} // End Uses = [EXEC], Defs = [M0, EXEC]

// This is a pseudo variant of the v_movreld_b32 instruction in which the
// vector operand appears only twice, once as def and once as use. Using this
// pseudo avoids problems with the Two Address instructions pass.
class INDIRECT_REG_WRITE_MOVREL_pseudo<RegisterClass rc,
                                RegisterOperand val_ty> : PseudoInstSI <
  (outs rc:$vdst), (ins rc:$vsrc, val_ty:$val, i32imm:$subreg)> {
  let Constraints = "$vsrc = $vdst";
  let Uses = [M0];
}

class V_INDIRECT_REG_WRITE_MOVREL_B32_pseudo<RegisterClass rc> :
  INDIRECT_REG_WRITE_MOVREL_pseudo<rc, VSrc_b32> {
  let VALU = 1;
  let VOP1 = 1;
  let Uses = [M0, EXEC];
}

class S_INDIRECT_REG_WRITE_MOVREL_pseudo<RegisterClass rc,
                                  RegisterOperand val_ty> :
  INDIRECT_REG_WRITE_MOVREL_pseudo<rc, val_ty> {
  let SALU = 1;
  let SOP1 = 1;
  let Uses = [M0];
}

class S_INDIRECT_REG_WRITE_MOVREL_B32_pseudo<RegisterClass rc> :
  S_INDIRECT_REG_WRITE_MOVREL_pseudo<rc, SSrc_b32>;
class S_INDIRECT_REG_WRITE_MOVREL_B64_pseudo<RegisterClass rc> :
  S_INDIRECT_REG_WRITE_MOVREL_pseudo<rc, SSrc_b64>;

def V_INDIRECT_REG_WRITE_MOVREL_B32_V1 : V_INDIRECT_REG_WRITE_MOVREL_B32_pseudo<VGPR_32>;
def V_INDIRECT_REG_WRITE_MOVREL_B32_V2 : V_INDIRECT_REG_WRITE_MOVREL_B32_pseudo<VReg_64>;
def V_INDIRECT_REG_WRITE_MOVREL_B32_V3 : V_INDIRECT_REG_WRITE_MOVREL_B32_pseudo<VReg_96>;
def V_INDIRECT_REG_WRITE_MOVREL_B32_V4 : V_INDIRECT_REG_WRITE_MOVREL_B32_pseudo<VReg_128>;
def V_INDIRECT_REG_WRITE_MOVREL_B32_V5 : V_INDIRECT_REG_WRITE_MOVREL_B32_pseudo<VReg_160>;
def V_INDIRECT_REG_WRITE_MOVREL_B32_V8 : V_INDIRECT_REG_WRITE_MOVREL_B32_pseudo<VReg_256>;
def V_INDIRECT_REG_WRITE_MOVREL_B32_V9 : V_INDIRECT_REG_WRITE_MOVREL_B32_pseudo<VReg_288>;
def V_INDIRECT_REG_WRITE_MOVREL_B32_V10 : V_INDIRECT_REG_WRITE_MOVREL_B32_pseudo<VReg_320>;
def V_INDIRECT_REG_WRITE_MOVREL_B32_V11 : V_INDIRECT_REG_WRITE_MOVREL_B32_pseudo<VReg_352>;
def V_INDIRECT_REG_WRITE_MOVREL_B32_V12 : V_INDIRECT_REG_WRITE_MOVREL_B32_pseudo<VReg_384>;
def V_INDIRECT_REG_WRITE_MOVREL_B32_V16 : V_INDIRECT_REG_WRITE_MOVREL_B32_pseudo<VReg_512>;
def V_INDIRECT_REG_WRITE_MOVREL_B32_V32 : V_INDIRECT_REG_WRITE_MOVREL_B32_pseudo<VReg_1024>;

def S_INDIRECT_REG_WRITE_MOVREL_B32_V1 : S_INDIRECT_REG_WRITE_MOVREL_B32_pseudo<SReg_32>;
def S_INDIRECT_REG_WRITE_MOVREL_B32_V2 : S_INDIRECT_REG_WRITE_MOVREL_B32_pseudo<SReg_64>;
def S_INDIRECT_REG_WRITE_MOVREL_B32_V3 : S_INDIRECT_REG_WRITE_MOVREL_B32_pseudo<SReg_96>;
def S_INDIRECT_REG_WRITE_MOVREL_B32_V4 : S_INDIRECT_REG_WRITE_MOVREL_B32_pseudo<SReg_128>;
def S_INDIRECT_REG_WRITE_MOVREL_B32_V5 : S_INDIRECT_REG_WRITE_MOVREL_B32_pseudo<SReg_160>;
def S_INDIRECT_REG_WRITE_MOVREL_B32_V8 : S_INDIRECT_REG_WRITE_MOVREL_B32_pseudo<SReg_256>;
def S_INDIRECT_REG_WRITE_MOVREL_B32_V9 : S_INDIRECT_REG_WRITE_MOVREL_B32_pseudo<SReg_288>;
def S_INDIRECT_REG_WRITE_MOVREL_B32_V10 : S_INDIRECT_REG_WRITE_MOVREL_B32_pseudo<SReg_320>;
def S_INDIRECT_REG_WRITE_MOVREL_B32_V11 : S_INDIRECT_REG_WRITE_MOVREL_B32_pseudo<SReg_352>;
def S_INDIRECT_REG_WRITE_MOVREL_B32_V12 : S_INDIRECT_REG_WRITE_MOVREL_B32_pseudo<SReg_384>;
def S_INDIRECT_REG_WRITE_MOVREL_B32_V16 : S_INDIRECT_REG_WRITE_MOVREL_B32_pseudo<SReg_512>;
def S_INDIRECT_REG_WRITE_MOVREL_B32_V32 : S_INDIRECT_REG_WRITE_MOVREL_B32_pseudo<SReg_1024>;

def S_INDIRECT_REG_WRITE_MOVREL_B64_V1 : S_INDIRECT_REG_WRITE_MOVREL_B64_pseudo<SReg_64>;
def S_INDIRECT_REG_WRITE_MOVREL_B64_V2 : S_INDIRECT_REG_WRITE_MOVREL_B64_pseudo<SReg_128>;
def S_INDIRECT_REG_WRITE_MOVREL_B64_V4 : S_INDIRECT_REG_WRITE_MOVREL_B64_pseudo<SReg_256>;
def S_INDIRECT_REG_WRITE_MOVREL_B64_V8 : S_INDIRECT_REG_WRITE_MOVREL_B64_pseudo<SReg_512>;
def S_INDIRECT_REG_WRITE_MOVREL_B64_V16 : S_INDIRECT_REG_WRITE_MOVREL_B64_pseudo<SReg_1024>;

// These variants of V_INDIRECT_REG_READ/WRITE use VGPR indexing. By using these
// pseudos we avoid spills or copies being inserted within indirect sequences
// that switch the VGPR indexing mode. Spills to accvgprs could be effected by
// this mode switching.

class V_INDIRECT_REG_WRITE_GPR_IDX_pseudo<RegisterClass rc> : PseudoInstSI <
  (outs rc:$vdst), (ins rc:$vsrc, VSrc_b32:$val, SSrc_b32:$idx, i32imm:$subreg)> {
  let Constraints = "$vsrc = $vdst";
  let VALU = 1;
  let Uses = [M0, EXEC];
  let Defs = [M0];
}

def V_INDIRECT_REG_WRITE_GPR_IDX_B32_V1 : V_INDIRECT_REG_WRITE_GPR_IDX_pseudo<VGPR_32>;
def V_INDIRECT_REG_WRITE_GPR_IDX_B32_V2 : V_INDIRECT_REG_WRITE_GPR_IDX_pseudo<VReg_64>;
def V_INDIRECT_REG_WRITE_GPR_IDX_B32_V3 : V_INDIRECT_REG_WRITE_GPR_IDX_pseudo<VReg_96>;
def V_INDIRECT_REG_WRITE_GPR_IDX_B32_V4 : V_INDIRECT_REG_WRITE_GPR_IDX_pseudo<VReg_128>;
def V_INDIRECT_REG_WRITE_GPR_IDX_B32_V5 : V_INDIRECT_REG_WRITE_GPR_IDX_pseudo<VReg_160>;
def V_INDIRECT_REG_WRITE_GPR_IDX_B32_V8 : V_INDIRECT_REG_WRITE_GPR_IDX_pseudo<VReg_256>;
def V_INDIRECT_REG_WRITE_GPR_IDX_B32_V9 : V_INDIRECT_REG_WRITE_GPR_IDX_pseudo<VReg_288>;
def V_INDIRECT_REG_WRITE_GPR_IDX_B32_V10 : V_INDIRECT_REG_WRITE_GPR_IDX_pseudo<VReg_320>;
def V_INDIRECT_REG_WRITE_GPR_IDX_B32_V11 : V_INDIRECT_REG_WRITE_GPR_IDX_pseudo<VReg_352>;
def V_INDIRECT_REG_WRITE_GPR_IDX_B32_V12 : V_INDIRECT_REG_WRITE_GPR_IDX_pseudo<VReg_384>;
def V_INDIRECT_REG_WRITE_GPR_IDX_B32_V16 : V_INDIRECT_REG_WRITE_GPR_IDX_pseudo<VReg_512>;
def V_INDIRECT_REG_WRITE_GPR_IDX_B32_V32 : V_INDIRECT_REG_WRITE_GPR_IDX_pseudo<VReg_1024>;

class V_INDIRECT_REG_READ_GPR_IDX_pseudo<RegisterClass rc> : PseudoInstSI <
  (outs VGPR_32:$vdst), (ins rc:$vsrc, SSrc_b32:$idx, i32imm:$subreg)> {
  let VALU = 1;
  let Uses = [M0, EXEC];
  let Defs = [M0];
}

def V_INDIRECT_REG_READ_GPR_IDX_B32_V1 : V_INDIRECT_REG_READ_GPR_IDX_pseudo<VGPR_32>;
def V_INDIRECT_REG_READ_GPR_IDX_B32_V2 : V_INDIRECT_REG_READ_GPR_IDX_pseudo<VReg_64>;
def V_INDIRECT_REG_READ_GPR_IDX_B32_V3 : V_INDIRECT_REG_READ_GPR_IDX_pseudo<VReg_96>;
def V_INDIRECT_REG_READ_GPR_IDX_B32_V4 : V_INDIRECT_REG_READ_GPR_IDX_pseudo<VReg_128>;
def V_INDIRECT_REG_READ_GPR_IDX_B32_V5 : V_INDIRECT_REG_READ_GPR_IDX_pseudo<VReg_160>;
def V_INDIRECT_REG_READ_GPR_IDX_B32_V8 : V_INDIRECT_REG_READ_GPR_IDX_pseudo<VReg_256>;
def V_INDIRECT_REG_READ_GPR_IDX_B32_V9 : V_INDIRECT_REG_READ_GPR_IDX_pseudo<VReg_288>;
def V_INDIRECT_REG_READ_GPR_IDX_B32_V10 : V_INDIRECT_REG_READ_GPR_IDX_pseudo<VReg_320>;
def V_INDIRECT_REG_READ_GPR_IDX_B32_V11 : V_INDIRECT_REG_READ_GPR_IDX_pseudo<VReg_352>;
def V_INDIRECT_REG_READ_GPR_IDX_B32_V12 : V_INDIRECT_REG_READ_GPR_IDX_pseudo<VReg_384>;
def V_INDIRECT_REG_READ_GPR_IDX_B32_V16 : V_INDIRECT_REG_READ_GPR_IDX_pseudo<VReg_512>;
def V_INDIRECT_REG_READ_GPR_IDX_B32_V32 : V_INDIRECT_REG_READ_GPR_IDX_pseudo<VReg_1024>;

multiclass SI_SPILL_SGPR <RegisterClass sgpr_class> {
  let UseNamedOperandTable = 1, Spill = 1, SALU = 1, Uses = [EXEC] in {
    def _SAVE : PseudoInstSI <
      (outs),
      (ins sgpr_class:$data, i32imm:$addr)> {
      let mayStore = 1;
      let mayLoad = 0;
    }

    def _RESTORE : PseudoInstSI <
      (outs sgpr_class:$data),
      (ins i32imm:$addr)> {
      let mayStore = 0;
      let mayLoad = 1;
    }
  } // End UseNamedOperandTable = 1
}

// You cannot use M0 as the output of v_readlane_b32 instructions or
// use it in the sdata operand of SMEM instructions. We still need to
// be able to spill the physical register m0, so allow it for
// SI_SPILL_32_* instructions.
defm SI_SPILL_S32  : SI_SPILL_SGPR <SReg_32>;
defm SI_SPILL_S64  : SI_SPILL_SGPR <SReg_64>;
defm SI_SPILL_S96  : SI_SPILL_SGPR <SReg_96>;
defm SI_SPILL_S128 : SI_SPILL_SGPR <SReg_128>;
defm SI_SPILL_S160 : SI_SPILL_SGPR <SReg_160>;
defm SI_SPILL_S192 : SI_SPILL_SGPR <SReg_192>;
defm SI_SPILL_S224 : SI_SPILL_SGPR <SReg_224>;
defm SI_SPILL_S256 : SI_SPILL_SGPR <SReg_256>;
defm SI_SPILL_S288 : SI_SPILL_SGPR <SReg_288>;
defm SI_SPILL_S320 : SI_SPILL_SGPR <SReg_320>;
defm SI_SPILL_S352 : SI_SPILL_SGPR <SReg_352>;
defm SI_SPILL_S384 : SI_SPILL_SGPR <SReg_384>;
defm SI_SPILL_S512 : SI_SPILL_SGPR <SReg_512>;
defm SI_SPILL_S1024 : SI_SPILL_SGPR <SReg_1024>;

let Spill = 1, VALU = 1, isConvergent = 1 in {
def SI_SPILL_S32_TO_VGPR : PseudoInstSI <(outs VGPR_32:$vdst),
  (ins SReg_32:$src0, i32imm:$src1, VGPR_32:$vdst_in)> {
  let Size = 4;
  let FixedSize = 1;
  let IsNeverUniform = 1;
  let hasSideEffects = 0;
  let mayLoad = 0;
  let mayStore = 0;
  let Constraints = "$vdst = $vdst_in";
}

def SI_RESTORE_S32_FROM_VGPR : PseudoInstSI <(outs SReg_32:$sdst),
  (ins VGPR_32:$src0, i32imm:$src1)> {
  let Size = 4;
  let FixedSize = 1;
  let hasSideEffects = 0;
  let mayLoad = 0;
  let mayStore = 0;
}
} // End Spill = 1, VALU = 1, isConvergent = 1

// VGPR or AGPR spill instructions. In case of AGPR spilling a temp register
// needs to be used and an extra instruction to move between VGPR and AGPR.
// UsesTmp adds to the total size of an expanded spill in this case.
multiclass SI_SPILL_VGPR <RegisterClass vgpr_class, bit UsesTmp = 0> {
  let UseNamedOperandTable = 1, Spill = 1, VALU = 1,
       SchedRW = [WriteVMEM] in {
    def _SAVE : VPseudoInstSI <
      (outs),
      (ins vgpr_class:$vdata, i32imm:$vaddr,
           SReg_32:$soffset, i32imm:$offset)> {
      let mayStore = 1;
      let mayLoad = 0;
      // (2 * 4) + (8 * num_subregs) bytes maximum
      int MaxSize = !add(!shl(!srl(vgpr_class.Size, 5), !add(UsesTmp, 3)), 8);
      // Size field is unsigned char and cannot fit more.
      let Size = !if(!le(MaxSize, 256), MaxSize, 252);
    }

    def _RESTORE : VPseudoInstSI <
      (outs vgpr_class:$vdata),
      (ins i32imm:$vaddr,
           SReg_32:$soffset, i32imm:$offset)> {
      let mayStore = 0;
      let mayLoad = 1;

      // (2 * 4) + (8 * num_subregs) bytes maximum
      int MaxSize = !add(!shl(!srl(vgpr_class.Size, 5), !add(UsesTmp, 3)), 8);
      // Size field is unsigned char and cannot fit more.
      let Size = !if(!le(MaxSize, 256), MaxSize, 252);
    }
  } // End UseNamedOperandTable = 1, Spill = 1, VALU = 1, SchedRW = [WriteVMEM]
}

defm SI_SPILL_V32  : SI_SPILL_VGPR <VGPR_32>;
defm SI_SPILL_V64  : SI_SPILL_VGPR <VReg_64>;
defm SI_SPILL_V96  : SI_SPILL_VGPR <VReg_96>;
defm SI_SPILL_V128 : SI_SPILL_VGPR <VReg_128>;
defm SI_SPILL_V160 : SI_SPILL_VGPR <VReg_160>;
defm SI_SPILL_V192 : SI_SPILL_VGPR <VReg_192>;
defm SI_SPILL_V224 : SI_SPILL_VGPR <VReg_224>;
defm SI_SPILL_V256 : SI_SPILL_VGPR <VReg_256>;
defm SI_SPILL_V288 : SI_SPILL_VGPR <VReg_288>;
defm SI_SPILL_V320 : SI_SPILL_VGPR <VReg_320>;
defm SI_SPILL_V352 : SI_SPILL_VGPR <VReg_352>;
defm SI_SPILL_V384 : SI_SPILL_VGPR <VReg_384>;
defm SI_SPILL_V512 : SI_SPILL_VGPR <VReg_512>;
defm SI_SPILL_V1024 : SI_SPILL_VGPR <VReg_1024>;

defm SI_SPILL_A32  : SI_SPILL_VGPR <AGPR_32, 1>;
defm SI_SPILL_A64  : SI_SPILL_VGPR <AReg_64, 1>;
defm SI_SPILL_A96  : SI_SPILL_VGPR <AReg_96, 1>;
defm SI_SPILL_A128 : SI_SPILL_VGPR <AReg_128, 1>;
defm SI_SPILL_A160 : SI_SPILL_VGPR <AReg_160, 1>;
defm SI_SPILL_A192 : SI_SPILL_VGPR <AReg_192, 1>;
defm SI_SPILL_A224 : SI_SPILL_VGPR <AReg_224, 1>;
defm SI_SPILL_A256 : SI_SPILL_VGPR <AReg_256, 1>;
defm SI_SPILL_A288 : SI_SPILL_VGPR <AReg_288, 1>;
defm SI_SPILL_A320 : SI_SPILL_VGPR <AReg_320, 1>;
defm SI_SPILL_A352 : SI_SPILL_VGPR <AReg_352, 1>;
defm SI_SPILL_A384 : SI_SPILL_VGPR <AReg_384, 1>;
defm SI_SPILL_A512 : SI_SPILL_VGPR <AReg_512, 1>;
defm SI_SPILL_A1024 : SI_SPILL_VGPR <AReg_1024, 1>;

defm SI_SPILL_AV32  : SI_SPILL_VGPR <AV_32, 1>;
defm SI_SPILL_AV64  : SI_SPILL_VGPR <AV_64, 1>;
defm SI_SPILL_AV96  : SI_SPILL_VGPR <AV_96, 1>;
defm SI_SPILL_AV128 : SI_SPILL_VGPR <AV_128, 1>;
defm SI_SPILL_AV160 : SI_SPILL_VGPR <AV_160, 1>;
defm SI_SPILL_AV192 : SI_SPILL_VGPR <AV_192, 1>;
defm SI_SPILL_AV224 : SI_SPILL_VGPR <AV_224, 1>;
defm SI_SPILL_AV256 : SI_SPILL_VGPR <AV_256, 1>;
defm SI_SPILL_AV288 : SI_SPILL_VGPR <AV_288, 1>;
defm SI_SPILL_AV320 : SI_SPILL_VGPR <AV_320, 1>;
defm SI_SPILL_AV352 : SI_SPILL_VGPR <AV_352, 1>;
defm SI_SPILL_AV384 : SI_SPILL_VGPR <AV_384, 1>;
defm SI_SPILL_AV512 : SI_SPILL_VGPR <AV_512, 1>;
defm SI_SPILL_AV1024 : SI_SPILL_VGPR <AV_1024, 1>;

let isConvergent = 1 in {
  defm SI_SPILL_WWM_V32  : SI_SPILL_VGPR <VGPR_32>;
  defm SI_SPILL_WWM_AV32 : SI_SPILL_VGPR <AV_32, 1>;
}

let isReMaterializable = 1, isAsCheapAsAMove = 1 in
def SI_PC_ADD_REL_OFFSET : SPseudoInstSI <
  (outs SReg_64:$dst),
  (ins si_ga:$ptr_lo, si_ga:$ptr_hi),
  [(set SReg_64:$dst,
      (i64 (SIpc_add_rel_offset tglobaladdr:$ptr_lo, tglobaladdr:$ptr_hi)))]> {
  let Defs = [SCC];
}

def : GCNPat <
  (SIpc_add_rel_offset tglobaladdr:$ptr_lo, 0),
  (SI_PC_ADD_REL_OFFSET $ptr_lo, (i32 0))
>;

def : GCNPat<
  (AMDGPUtrap timm:$trapid),
  (S_TRAP $trapid)
>;

def : GCNPat<
  (AMDGPUelse i1:$src, bb:$target),
  (SI_ELSE $src, $target)
>;

def : Pat <
  (int_amdgcn_kill i1:$src),
  (SI_KILL_I1_PSEUDO SCSrc_i1:$src, 0)
>;

def : Pat <
  (int_amdgcn_kill (i1 (not i1:$src))),
  (SI_KILL_I1_PSEUDO SCSrc_i1:$src, -1)
>;

def : Pat <
  (int_amdgcn_kill (i1 (setcc f32:$src, InlineImmFP32:$imm, cond:$cond))),
  (SI_KILL_F32_COND_IMM_PSEUDO VSrc_b32:$src, (bitcast_fpimm_to_i32 $imm), (cond_as_i32imm $cond))
>;

def : Pat <
  (int_amdgcn_wqm_demote i1:$src),
  (SI_DEMOTE_I1 SCSrc_i1:$src, 0)
>;

def : Pat <
  (int_amdgcn_wqm_demote (i1 (not i1:$src))),
  (SI_DEMOTE_I1 SCSrc_i1:$src, -1)
>;

  // TODO: we could add more variants for other types of conditionals

def : Pat <
  (i64 (int_amdgcn_icmp i1:$src, (i1 0), (i32 33))),
  (COPY $src) // Return the SGPRs representing i1 src
>;

def : Pat <
  (i32 (int_amdgcn_icmp i1:$src, (i1 0), (i32 33))),
  (COPY $src) // Return the SGPRs representing i1 src
>;

//===----------------------------------------------------------------------===//
// VOP1 Patterns
//===----------------------------------------------------------------------===//

multiclass f16_fp_Pats<Instruction cvt_f16_f32_inst_e64, Instruction cvt_f32_f16_inst_e64> {
  // f16_to_fp patterns
  def : GCNPat <
    (f32 (any_f16_to_fp i32:$src0)),
    (cvt_f32_f16_inst_e64 SRCMODS.NONE, $src0)
  >;

  def : GCNPat <
    (f32 (f16_to_fp (and_oneuse i32:$src0, 0x7fff))),
    (cvt_f32_f16_inst_e64 SRCMODS.ABS, $src0)
  >;

  def : GCNPat <
    (f32 (f16_to_fp (i32 (srl_oneuse (and_oneuse i32:$src0, 0x7fff0000), (i32 16))))),
    (cvt_f32_f16_inst_e64 SRCMODS.ABS, (i32 (V_LSHRREV_B32_e64 (i32 16), i32:$src0)))
  >;

  def : GCNPat <
    (f32 (f16_to_fp (or_oneuse i32:$src0, 0x8000))),
    (cvt_f32_f16_inst_e64 SRCMODS.NEG_ABS, $src0)
  >;

  def : GCNPat <
    (f32 (f16_to_fp (xor_oneuse i32:$src0, 0x8000))),
    (cvt_f32_f16_inst_e64 SRCMODS.NEG, $src0)
  >;

  def : GCNPat <
    (f64 (any_fpextend f16:$src)),
    (V_CVT_F64_F32_e32 (cvt_f32_f16_inst_e64 SRCMODS.NONE, $src))
  >;

  // fp_to_fp16 patterns
  def : GCNPat <
    (i32 (AMDGPUfp_to_f16 (f32 (VOP3Mods f32:$src0, i32:$src0_modifiers)))),
    (cvt_f16_f32_inst_e64 $src0_modifiers, f32:$src0)
  >;

  def : GCNPat <
    (i32 (fp_to_sint f16:$src)),
    (V_CVT_I32_F32_e32 (cvt_f32_f16_inst_e64 SRCMODS.NONE, VSrc_b32:$src))
  >;

  def : GCNPat <
    (i32 (fp_to_uint f16:$src)),
    (V_CVT_U32_F32_e32 (cvt_f32_f16_inst_e64 SRCMODS.NONE, VSrc_b32:$src))
  >;

  def : GCNPat <
    (f16 (sint_to_fp i32:$src)),
    (cvt_f16_f32_inst_e64 SRCMODS.NONE, (V_CVT_F32_I32_e32 VSrc_b32:$src))
  >;

  def : GCNPat <
    (f16 (uint_to_fp i32:$src)),
    (cvt_f16_f32_inst_e64 SRCMODS.NONE, (V_CVT_F32_U32_e32 VSrc_b32:$src))
  >;

  // This is only used on targets without half support
  // TODO: Introduce strict variant of AMDGPUfp_to_f16 and share custom lowering
  def : GCNPat <
    (i32 (strict_fp_to_f16 (f32 (VOP3Mods f32:$src0, i32:$src0_modifiers)))),
    (cvt_f16_f32_inst_e64 $src0_modifiers, f32:$src0)
  >;
}

let SubtargetPredicate = NotHasTrue16BitInsts in
defm : f16_fp_Pats<V_CVT_F16_F32_e64, V_CVT_F32_F16_e64>;

let SubtargetPredicate = HasTrue16BitInsts in
defm : f16_fp_Pats<V_CVT_F16_F32_t16_e64, V_CVT_F32_F16_t16_e64>;

//===----------------------------------------------------------------------===//
// VOP2 Patterns
//===----------------------------------------------------------------------===//

// NoMods pattern used for mac. If there are any source modifiers then it's
// better to select mad instead of mac.
class FMADPat <ValueType vt, Instruction inst>
  : GCNPat <(vt (any_fmad (vt (VOP3NoMods vt:$src0)),
                          (vt (VOP3NoMods vt:$src1)),
                          (vt (VOP3NoMods vt:$src2)))),
    (inst SRCMODS.NONE, $src0, SRCMODS.NONE, $src1,
          SRCMODS.NONE, $src2, DSTCLAMP.NONE, DSTOMOD.NONE)
>;

// Prefer mac form when there are no modifiers.
let AddedComplexity = 9 in {
let OtherPredicates = [HasMadMacF32Insts] in
def : FMADPat <f32, V_MAC_F32_e64>;

// Don't allow source modifiers. If there are any source modifiers then it's
// better to select mad instead of mac.
let SubtargetPredicate = isGFX6GFX7GFX10,
    OtherPredicates = [HasMadMacF32Insts, NoFP32Denormals] in
def : GCNPat <
      (f32 (fadd (AMDGPUfmul_legacy (VOP3NoMods f32:$src0),
                                    (VOP3NoMods f32:$src1)),
                 (VOP3NoMods f32:$src2))),
      (V_MAC_LEGACY_F32_e64 SRCMODS.NONE, $src0, SRCMODS.NONE, $src1,
                            SRCMODS.NONE, $src2, DSTCLAMP.NONE, DSTOMOD.NONE)
>;

// Don't allow source modifiers. If there are any source modifiers then it's
// better to select fma instead of fmac.
let SubtargetPredicate = HasFmaLegacy32 in
def : GCNPat <
      (f32 (int_amdgcn_fma_legacy (VOP3NoMods f32:$src0),
                                  (VOP3NoMods f32:$src1),
                                  (VOP3NoMods f32:$src2))),
      (V_FMAC_LEGACY_F32_e64 SRCMODS.NONE, $src0, SRCMODS.NONE, $src1,
                             SRCMODS.NONE, $src2, DSTCLAMP.NONE, DSTOMOD.NONE)
>;

let SubtargetPredicate = Has16BitInsts in
def : FMADPat <f16, V_MAC_F16_e64>;
} // AddedComplexity = 9

let OtherPredicates = [HasMadMacF32Insts, NoFP32Denormals] in
def : GCNPat <
      (f32 (fadd (AMDGPUfmul_legacy (VOP3Mods f32:$src0, i32:$src0_mod),
                                    (VOP3Mods f32:$src1, i32:$src1_mod)),
                 (VOP3Mods f32:$src2, i32:$src2_mod))),
      (V_MAD_LEGACY_F32_e64 $src0_mod, $src0, $src1_mod, $src1,
                        $src2_mod, $src2, DSTCLAMP.NONE, DSTOMOD.NONE)
>;

class VOPSelectModsPat <ValueType vt> : GCNPat <
  (vt (select i1:$src0, (VOP3ModsNonCanonicalizing vt:$src1, i32:$src1_mods),
                        (VOP3ModsNonCanonicalizing vt:$src2, i32:$src2_mods))),
  (V_CNDMASK_B32_e64 FP32InputMods:$src2_mods, VSrc_b32:$src2,
                     FP32InputMods:$src1_mods, VSrc_b32:$src1, SSrc_i1:$src0)
>;

class VOPSelectPat <ValueType vt> : GCNPat <
  (vt (select i1:$src0, vt:$src1, vt:$src2)),
  (V_CNDMASK_B32_e64 0, VSrc_b32:$src2, 0, VSrc_b32:$src1, SSrc_i1:$src0)
>;

def : VOPSelectModsPat <i32>;
def : VOPSelectModsPat <f32>;
def : VOPSelectPat <f16>;
def : VOPSelectPat <i16>;

let AddedComplexity = 1 in {
def : GCNPat <
  (i32 (add (i32 (DivergentUnaryFrag<ctpop> i32:$popcnt)), i32:$val)),
  (V_BCNT_U32_B32_e64 $popcnt, $val)
>;
}

def : GCNPat <
  (i32 (DivergentUnaryFrag<ctpop> i32:$popcnt)),
  (V_BCNT_U32_B32_e64 VSrc_b32:$popcnt, (i32 0))
>;

def : GCNPat <
  (i16 (add (i16 (trunc (i32 (DivergentUnaryFrag<ctpop> i32:$popcnt)))), i16:$val)),
  (V_BCNT_U32_B32_e64 $popcnt, $val)
>;

def : GCNPat <
  (i64 (DivergentUnaryFrag<ctpop> i64:$src)),
  (REG_SEQUENCE VReg_64,
    (V_BCNT_U32_B32_e64 (i32 (EXTRACT_SUBREG i64:$src, sub1)),
      (i32 (V_BCNT_U32_B32_e64 (i32 (EXTRACT_SUBREG i64:$src, sub0)), (i32 0)))), sub0,
      (i32 (V_MOV_B32_e32 (i32 0))), sub1)
>;

/********** ============================================ **********/
/********** Extraction, Insertion, Building and Casting  **********/
/********** ============================================ **********/

// Special case for 2 element vectors. REQ_SEQUENCE produces better code
// than an INSERT_SUBREG.
multiclass Insert_Element_V2<RegisterClass RC, ValueType elem_type, ValueType vec_type> {
  def : GCNPat <
    (insertelt vec_type:$vec, elem_type:$elem, 0),
    (REG_SEQUENCE RC, $elem, sub0, (elem_type (EXTRACT_SUBREG $vec, sub1)), sub1)
  >;

  def : GCNPat <
    (insertelt vec_type:$vec, elem_type:$elem, 1),
    (REG_SEQUENCE RC, (elem_type (EXTRACT_SUBREG $vec, sub0)), sub0, $elem, sub1)
  >;
}

foreach Index = 0-1 in {
  def Extract_Element_v2i32_#Index : Extract_Element <
    i32, v2i32, Index, !cast<SubRegIndex>(sub#Index)
  >;

  def Extract_Element_v2f32_#Index : Extract_Element <
    f32, v2f32, Index, !cast<SubRegIndex>(sub#Index)
  >;
}

defm : Insert_Element_V2 <SReg_64, i32, v2i32>;
defm : Insert_Element_V2 <SReg_64, f32, v2f32>;

foreach Index = 0-2 in {
  def Extract_Element_v3i32_#Index : Extract_Element <
    i32, v3i32, Index, !cast<SubRegIndex>(sub#Index)
  >;
  def Insert_Element_v3i32_#Index : Insert_Element <
    i32, v3i32, Index, !cast<SubRegIndex>(sub#Index)
  >;

  def Extract_Element_v3f32_#Index : Extract_Element <
    f32, v3f32, Index, !cast<SubRegIndex>(sub#Index)
  >;
  def Insert_Element_v3f32_#Index : Insert_Element <
    f32, v3f32, Index, !cast<SubRegIndex>(sub#Index)
  >;
}

foreach Index = 0-3 in {
  def Extract_Element_v4i32_#Index : Extract_Element <
    i32, v4i32, Index, !cast<SubRegIndex>(sub#Index)
  >;
  def Insert_Element_v4i32_#Index : Insert_Element <
    i32, v4i32, Index, !cast<SubRegIndex>(sub#Index)
  >;

  def Extract_Element_v4f32_#Index : Extract_Element <
    f32, v4f32, Index, !cast<SubRegIndex>(sub#Index)
  >;
  def Insert_Element_v4f32_#Index : Insert_Element <
    f32, v4f32, Index, !cast<SubRegIndex>(sub#Index)
  >;
}

foreach Index = 0-4 in {
  def Extract_Element_v5i32_#Index : Extract_Element <
    i32, v5i32, Index, !cast<SubRegIndex>(sub#Index)
  >;
  def Insert_Element_v5i32_#Index : Insert_Element <
    i32, v5i32, Index, !cast<SubRegIndex>(sub#Index)
  >;

  def Extract_Element_v5f32_#Index : Extract_Element <
    f32, v5f32, Index, !cast<SubRegIndex>(sub#Index)
  >;
  def Insert_Element_v5f32_#Index : Insert_Element <
    f32, v5f32, Index, !cast<SubRegIndex>(sub#Index)
  >;
}

foreach Index = 0-5 in {
  def Extract_Element_v6i32_#Index : Extract_Element <
    i32, v6i32, Index, !cast<SubRegIndex>(sub#Index)
  >;
  def Insert_Element_v6i32_#Index : Insert_Element <
    i32, v6i32, Index, !cast<SubRegIndex>(sub#Index)
  >;

  def Extract_Element_v6f32_#Index : Extract_Element <
    f32, v6f32, Index, !cast<SubRegIndex>(sub#Index)
  >;
  def Insert_Element_v6f32_#Index : Insert_Element <
    f32, v6f32, Index, !cast<SubRegIndex>(sub#Index)
  >;
}

foreach Index = 0-6 in {
  def Extract_Element_v7i32_#Index : Extract_Element <
    i32, v7i32, Index, !cast<SubRegIndex>(sub#Index)
  >;
  def Insert_Element_v7i32_#Index : Insert_Element <
    i32, v7i32, Index, !cast<SubRegIndex>(sub#Index)
  >;

  def Extract_Element_v7f32_#Index : Extract_Element <
    f32, v7f32, Index, !cast<SubRegIndex>(sub#Index)
  >;
  def Insert_Element_v7f32_#Index : Insert_Element <
    f32, v7f32, Index, !cast<SubRegIndex>(sub#Index)
  >;
}

foreach Index = 0-7 in {
  def Extract_Element_v8i32_#Index : Extract_Element <
    i32, v8i32, Index, !cast<SubRegIndex>(sub#Index)
  >;
  def Insert_Element_v8i32_#Index : Insert_Element <
    i32, v8i32, Index, !cast<SubRegIndex>(sub#Index)
  >;

  def Extract_Element_v8f32_#Index : Extract_Element <
    f32, v8f32, Index, !cast<SubRegIndex>(sub#Index)
  >;
  def Insert_Element_v8f32_#Index : Insert_Element <
    f32, v8f32, Index, !cast<SubRegIndex>(sub#Index)
  >;
}

foreach Index = 0-8 in {
  def Extract_Element_v9i32_#Index : Extract_Element <
    i32, v9i32, Index, !cast<SubRegIndex>(sub#Index)
  >;
  def Insert_Element_v9i32_#Index : Insert_Element <
    i32, v9i32, Index, !cast<SubRegIndex>(sub#Index)
  >;

  def Extract_Element_v9f32_#Index : Extract_Element <
    f32, v9f32, Index, !cast<SubRegIndex>(sub#Index)
  >;
  def Insert_Element_v9f32_#Index : Insert_Element <
    f32, v9f32, Index, !cast<SubRegIndex>(sub#Index)
  >;
}

foreach Index = 0-9 in {
  def Extract_Element_v10i32_#Index : Extract_Element <
    i32, v10i32, Index, !cast<SubRegIndex>(sub#Index)
  >;
  def Insert_Element_v10i32_#Index : Insert_Element <
    i32, v10i32, Index, !cast<SubRegIndex>(sub#Index)
  >;

  def Extract_Element_v10f32_#Index : Extract_Element <
    f32, v10f32, Index, !cast<SubRegIndex>(sub#Index)
  >;
  def Insert_Element_v10f32_#Index : Insert_Element <
    f32, v10f32, Index, !cast<SubRegIndex>(sub#Index)
  >;
}

foreach Index = 0-10 in {
  def Extract_Element_v11i32_#Index : Extract_Element <
    i32, v11i32, Index, !cast<SubRegIndex>(sub#Index)
  >;
  def Insert_Element_v11i32_#Index : Insert_Element <
    i32, v11i32, Index, !cast<SubRegIndex>(sub#Index)
  >;

  def Extract_Element_v11f32_#Index : Extract_Element <
    f32, v11f32, Index, !cast<SubRegIndex>(sub#Index)
  >;
  def Insert_Element_v11f32_#Index : Insert_Element <
    f32, v11f32, Index, !cast<SubRegIndex>(sub#Index)
  >;
}

foreach Index = 0-11 in {
  def Extract_Element_v12i32_#Index : Extract_Element <
    i32, v12i32, Index, !cast<SubRegIndex>(sub#Index)
  >;
  def Insert_Element_v12i32_#Index : Insert_Element <
    i32, v12i32, Index, !cast<SubRegIndex>(sub#Index)
  >;

  def Extract_Element_v12f32_#Index : Extract_Element <
    f32, v12f32, Index, !cast<SubRegIndex>(sub#Index)
  >;
  def Insert_Element_v12f32_#Index : Insert_Element <
    f32, v12f32, Index, !cast<SubRegIndex>(sub#Index)
  >;
}

foreach Index = 0-15 in {
  def Extract_Element_v16i32_#Index : Extract_Element <
    i32, v16i32, Index, !cast<SubRegIndex>(sub#Index)
  >;
  def Insert_Element_v16i32_#Index : Insert_Element <
    i32, v16i32, Index, !cast<SubRegIndex>(sub#Index)
  >;

  def Extract_Element_v16f32_#Index : Extract_Element <
    f32, v16f32, Index, !cast<SubRegIndex>(sub#Index)
  >;
  def Insert_Element_v16f32_#Index : Insert_Element <
    f32, v16f32, Index, !cast<SubRegIndex>(sub#Index)
  >;
}


foreach Index = 0-31 in {
  def Extract_Element_v32i32_#Index : Extract_Element <
    i32, v32i32, Index, !cast<SubRegIndex>(sub#Index)
  >;

  def Insert_Element_v32i32_#Index : Insert_Element <
    i32, v32i32, Index, !cast<SubRegIndex>(sub#Index)
  >;

  def Extract_Element_v32f32_#Index : Extract_Element <
    f32, v32f32, Index, !cast<SubRegIndex>(sub#Index)
  >;

  def Insert_Element_v32f32_#Index : Insert_Element <
    f32, v32f32, Index, !cast<SubRegIndex>(sub#Index)
  >;
}

// FIXME: Why do only some of these type combinations for SReg and
// VReg?
// 16-bit bitcast
def : BitConvert <i16, f16, VGPR_32>;
def : BitConvert <f16, i16, VGPR_32>;
def : BitConvert <f16, bf16, VGPR_32>;
def : BitConvert <bf16, f16, VGPR_32>;

def : BitConvert <i16, f16, SReg_32>;
def : BitConvert <f16, i16, SReg_32>;
def : BitConvert <f16, bf16, SReg_32>;
def : BitConvert <bf16, f16, SReg_32>;

def : BitConvert <i16, bf16, VGPR_32>;
def : BitConvert <bf16, i16, VGPR_32>;
def : BitConvert <i16, bf16, SReg_32>;
def : BitConvert <bf16, i16, SReg_32>;

// 32-bit bitcast
def : BitConvert <i32, f32, VGPR_32>;
def : BitConvert <f32, i32, VGPR_32>;
def : BitConvert <i32, f32, SReg_32>;
def : BitConvert <f32, i32, SReg_32>;
def : BitConvert <v2i16, i32, SReg_32>;
def : BitConvert <i32, v2i16, SReg_32>;
def : BitConvert <v2f16, i32, SReg_32>;
def : BitConvert <i32, v2f16, SReg_32>;
def : BitConvert <v2i16, v2f16, SReg_32>;
def : BitConvert <v2f16, v2i16, SReg_32>;
def : BitConvert <v2f16, f32, SReg_32>;
def : BitConvert <f32, v2f16, SReg_32>;
def : BitConvert <v2i16, f32, SReg_32>;
def : BitConvert <f32, v2i16, SReg_32>;
def : BitConvert <v2bf16, i32, SReg_32>;
def : BitConvert <i32, v2bf16, SReg_32>;
def : BitConvert <v2bf16, i32, VGPR_32>;
def : BitConvert <i32, v2bf16, VGPR_32>;
def : BitConvert <v2bf16, v2i16, SReg_32>;
def : BitConvert <v2i16, v2bf16, SReg_32>;
def : BitConvert <v2bf16, v2i16, VGPR_32>;
def : BitConvert <v2i16, v2bf16, VGPR_32>;
def : BitConvert <v2bf16, v2f16, SReg_32>;
def : BitConvert <v2f16, v2bf16, SReg_32>;
def : BitConvert <v2bf16, v2f16, VGPR_32>;
def : BitConvert <v2f16, v2bf16, VGPR_32>;
def : BitConvert <f32, v2bf16, VGPR_32>;
def : BitConvert <v2bf16, f32, VGPR_32>;
def : BitConvert <f32, v2bf16, SReg_32>;
def : BitConvert <v2bf16, f32, SReg_32>;


// 64-bit bitcast
def : BitConvert <i64, f64, VReg_64>;
def : BitConvert <f64, i64, VReg_64>;
def : BitConvert <v2i32, v2f32, VReg_64>;
def : BitConvert <v2f32, v2i32, VReg_64>;
def : BitConvert <i64, v2i32, VReg_64>;
def : BitConvert <v2i32, i64, VReg_64>;
def : BitConvert <i64, v2f32, VReg_64>;
def : BitConvert <v2f32, i64, VReg_64>;
def : BitConvert <f64, v2f32, VReg_64>;
def : BitConvert <v2f32, f64, VReg_64>;
def : BitConvert <f64, v2i32, VReg_64>;
def : BitConvert <v2i32, f64, VReg_64>;
def : BitConvert <v4i16, v4f16, VReg_64>;
def : BitConvert <v4f16, v4i16, VReg_64>;
def : BitConvert <v4bf16, v2i32, VReg_64>;
def : BitConvert <v2i32, v4bf16, VReg_64>;
def : BitConvert <v4bf16, i64, VReg_64>;
def : BitConvert <i64, v4bf16, VReg_64>;
def : BitConvert <v4bf16, v4i16, VReg_64>;
def : BitConvert <v4i16, v4bf16, VReg_64>;
def : BitConvert <v4bf16, v4f16, VReg_64>;
def : BitConvert <v4f16, v4bf16, VReg_64>;
def : BitConvert <v4bf16, v2f32, VReg_64>;
def : BitConvert <v2f32, v4bf16, VReg_64>;
def : BitConvert <v4bf16, f64, VReg_64>;
def : BitConvert <f64, v4bf16, VReg_64>;


// FIXME: Make SGPR
def : BitConvert <v2i32, v4f16, VReg_64>;
def : BitConvert <v4f16, v2i32, VReg_64>;
def : BitConvert <v2i32, v4f16, VReg_64>;
def : BitConvert <v2i32, v4i16, VReg_64>;
def : BitConvert <v4i16, v2i32, VReg_64>;
def : BitConvert <v2f32, v4f16, VReg_64>;
def : BitConvert <v4f16, v2f32, VReg_64>;
def : BitConvert <v2f32, v4i16, VReg_64>;
def : BitConvert <v4i16, v2f32, VReg_64>;
def : BitConvert <v4i16, f64, VReg_64>;
def : BitConvert <v4f16, f64, VReg_64>;
def : BitConvert <f64, v4i16, VReg_64>;
def : BitConvert <f64, v4f16, VReg_64>;
def : BitConvert <v4i16, i64, VReg_64>;
def : BitConvert <v4f16, i64, VReg_64>;
def : BitConvert <i64, v4i16, VReg_64>;
def : BitConvert <i64, v4f16, VReg_64>;

def : BitConvert <v4i32, v4f32, VReg_128>;
def : BitConvert <v4f32, v4i32, VReg_128>;

// 96-bit bitcast
def : BitConvert <v3i32, v3f32, SGPR_96>;
def : BitConvert <v3f32, v3i32, SGPR_96>;

// 128-bit bitcast
def : BitConvert <v2i64, v4i32, SReg_128>;
def : BitConvert <v4i32, v2i64, SReg_128>;
def : BitConvert <v2f64, v4f32, VReg_128>;
def : BitConvert <v2f64, v4i32, VReg_128>;
def : BitConvert <v4f32, v2f64, VReg_128>;
def : BitConvert <v4i32, v2f64, VReg_128>;
def : BitConvert <v2i64, v2f64, VReg_128>;
def : BitConvert <v2f64, v2i64, VReg_128>;
def : BitConvert <v4f32, v2i64, VReg_128>;
def : BitConvert <v2i64, v4f32, VReg_128>;
def : BitConvert <v8i16, v4i32, SReg_128>;
def : BitConvert <v4i32, v8i16, SReg_128>;
def : BitConvert <v8f16, v4f32, VReg_128>;
def : BitConvert <v8f16, v4i32, VReg_128>;
def : BitConvert <v4f32, v8f16, VReg_128>;
def : BitConvert <v4i32, v8f16, VReg_128>;
def : BitConvert <v8i16, v8f16, VReg_128>;
def : BitConvert <v8f16, v8i16, VReg_128>;
def : BitConvert <v4f32, v8i16, VReg_128>;
def : BitConvert <v8i16, v4f32, VReg_128>;
def : BitConvert <v8i16, v8f16, SReg_128>;
def : BitConvert <v8i16, v2i64, SReg_128>;
def : BitConvert <v8i16, v2f64, SReg_128>;
def : BitConvert <v8f16, v2i64, SReg_128>;
def : BitConvert <v8f16, v2f64, SReg_128>;
def : BitConvert <v8f16, v8i16, SReg_128>;
def : BitConvert <v2i64, v8i16, SReg_128>;
def : BitConvert <v2f64, v8i16, SReg_128>;
def : BitConvert <v2i64, v8f16, SReg_128>;
def : BitConvert <v2f64, v8f16, SReg_128>;

def : BitConvert <v4i32, v8bf16, SReg_128>;
def : BitConvert <v8bf16, v4i32, SReg_128>;
def : BitConvert <v4i32, v8bf16, VReg_128>;
def : BitConvert <v8bf16, v4i32, VReg_128>;

def : BitConvert <v4f32, v8bf16, SReg_128>;
def : BitConvert <v8bf16, v4f32, SReg_128>;
def : BitConvert <v4f32, v8bf16, VReg_128>;
def : BitConvert <v8bf16, v4f32, VReg_128>;

def : BitConvert <v8i16, v8bf16, SReg_128>;
def : BitConvert <v8bf16, v8i16, SReg_128>;
def : BitConvert <v8i16, v8bf16, VReg_128>;
def : BitConvert <v8bf16, v8i16, VReg_128>;

def : BitConvert <v8f16, v8bf16, SReg_128>;
def : BitConvert <v8bf16, v8f16, SReg_128>;
def : BitConvert <v8f16, v8bf16, VReg_128>;
def : BitConvert <v8bf16, v8f16, VReg_128>;

def : BitConvert <v2f64, v8bf16, SReg_128>;
def : BitConvert <v8bf16, v2f64, SReg_128>;
def : BitConvert <v2f64, v8bf16, VReg_128>;
def : BitConvert <v8bf16, v2f64, VReg_128>;

def : BitConvert <v2i64, v8bf16, SReg_128>;
def : BitConvert <v8bf16, v2i64, SReg_128>;
def : BitConvert <v2i64, v8bf16, VReg_128>;
def : BitConvert <v8bf16, v2i64, VReg_128>;


// 160-bit bitcast
def : BitConvert <v5i32, v5f32, SReg_160>;
def : BitConvert <v5f32, v5i32, SReg_160>;
def : BitConvert <v5i32, v5f32, VReg_160>;
def : BitConvert <v5f32, v5i32, VReg_160>;

// 192-bit bitcast
def : BitConvert <v6i32, v6f32, SReg_192>;
def : BitConvert <v6f32, v6i32, SReg_192>;
def : BitConvert <v6i32, v6f32, VReg_192>;
def : BitConvert <v6f32, v6i32, VReg_192>;
def : BitConvert <v3i64, v3f64, VReg_192>;
def : BitConvert <v3f64, v3i64, VReg_192>;
def : BitConvert <v3i64, v6i32, VReg_192>;
def : BitConvert <v3i64, v6f32, VReg_192>;
def : BitConvert <v3f64, v6i32, VReg_192>;
def : BitConvert <v3f64, v6f32, VReg_192>;
def : BitConvert <v6i32, v3i64, VReg_192>;
def : BitConvert <v6f32, v3i64, VReg_192>;
def : BitConvert <v6i32, v3f64, VReg_192>;
def : BitConvert <v6f32, v3f64, VReg_192>;

// 224-bit bitcast
def : BitConvert <v7i32, v7f32, SReg_224>;
def : BitConvert <v7f32, v7i32, SReg_224>;
def : BitConvert <v7i32, v7f32, VReg_224>;
def : BitConvert <v7f32, v7i32, VReg_224>;

// 256-bit bitcast
def : BitConvert <v8i32, v8f32, SReg_256>;
def : BitConvert <v8f32, v8i32, SReg_256>;
def : BitConvert <v8i32, v8f32, VReg_256>;
def : BitConvert <v8f32, v8i32, VReg_256>;
def : BitConvert <v4i64, v4f64, VReg_256>;
def : BitConvert <v4f64, v4i64, VReg_256>;
def : BitConvert <v4i64, v8i32, VReg_256>;
def : BitConvert <v4i64, v8f32, VReg_256>;
def : BitConvert <v4f64, v8i32, VReg_256>;
def : BitConvert <v4f64, v8f32, VReg_256>;
def : BitConvert <v8i32, v4i64, VReg_256>;
def : BitConvert <v8f32, v4i64, VReg_256>;
def : BitConvert <v8i32, v4f64, VReg_256>;
def : BitConvert <v8f32, v4f64, VReg_256>;
def : BitConvert <v16i16, v16f16, SReg_256>;
def : BitConvert <v16f16, v16i16, SReg_256>;
def : BitConvert <v16i16, v16f16, VReg_256>;
def : BitConvert <v16f16, v16i16, VReg_256>;
def : BitConvert <v16f16, v8i32, VReg_256>;
def : BitConvert <v16i16, v8i32, VReg_256>;
def : BitConvert <v16f16, v8f32, VReg_256>;
def : BitConvert <v16i16, v8f32, VReg_256>;
def : BitConvert <v8i32, v16f16, VReg_256>;
def : BitConvert <v8i32, v16i16, VReg_256>;
def : BitConvert <v8f32, v16f16, VReg_256>;
def : BitConvert <v8f32, v16i16, VReg_256>;
def : BitConvert <v16f16, v4i64, VReg_256>;
def : BitConvert <v16i16, v4i64, VReg_256>;
def : BitConvert <v16f16, v4f64, VReg_256>;
def : BitConvert <v16i16, v4f64, VReg_256>;
def : BitConvert <v4i64, v16f16, VReg_256>;
def : BitConvert <v4i64, v16i16, VReg_256>;
def : BitConvert <v4f64, v16f16, VReg_256>;
def : BitConvert <v4f64, v16i16, VReg_256>;


def : BitConvert <v8i32, v16bf16, VReg_256>;
def : BitConvert <v16bf16, v8i32, VReg_256>;
def : BitConvert <v8f32, v16bf16, VReg_256>;
def : BitConvert <v16bf16, v8f32, VReg_256>;
def : BitConvert <v4i64, v16bf16, VReg_256>;
def : BitConvert <v16bf16, v4i64, VReg_256>;
def : BitConvert <v4f64, v16bf16, VReg_256>;
def : BitConvert <v16bf16, v4f64, VReg_256>;



def : BitConvert <v16i16, v16bf16, SReg_256>;
def : BitConvert <v16bf16, v16i16, SReg_256>;
def : BitConvert <v16i16, v16bf16, VReg_256>;
def : BitConvert <v16bf16, v16i16, VReg_256>;

def : BitConvert <v16f16, v16bf16, SReg_256>;
def : BitConvert <v16bf16, v16f16, SReg_256>;
def : BitConvert <v16f16, v16bf16, VReg_256>;
def : BitConvert <v16bf16, v16f16, VReg_256>;




// 288-bit bitcast
def : BitConvert <v9i32, v9f32, SReg_288>;
def : BitConvert <v9f32, v9i32, SReg_288>;
def : BitConvert <v9i32, v9f32, VReg_288>;
def : BitConvert <v9f32, v9i32, VReg_288>;

// 320-bit bitcast
def : BitConvert <v10i32, v10f32, SReg_320>;
def : BitConvert <v10f32, v10i32, SReg_320>;
def : BitConvert <v10i32, v10f32, VReg_320>;
def : BitConvert <v10f32, v10i32, VReg_320>;

// 320-bit bitcast
def : BitConvert <v11i32, v11f32, SReg_352>;
def : BitConvert <v11f32, v11i32, SReg_352>;
def : BitConvert <v11i32, v11f32, VReg_352>;
def : BitConvert <v11f32, v11i32, VReg_352>;

// 384-bit bitcast
def : BitConvert <v12i32, v12f32, SReg_384>;
def : BitConvert <v12f32, v12i32, SReg_384>;
def : BitConvert <v12i32, v12f32, VReg_384>;
def : BitConvert <v12f32, v12i32, VReg_384>;

// 512-bit bitcast
def : BitConvert <v32f16, v32i16, VReg_512>;
def : BitConvert <v32i16, v32f16, VReg_512>;
def : BitConvert <v32f16, v16i32, VReg_512>;
def : BitConvert <v32f16, v16f32, VReg_512>;
def : BitConvert <v16f32, v32f16, VReg_512>;
def : BitConvert <v16i32, v32f16, VReg_512>;
def : BitConvert <v32i16, v16i32, VReg_512>;
def : BitConvert <v32i16, v16f32, VReg_512>;
def : BitConvert <v16f32, v32i16, VReg_512>;
def : BitConvert <v16i32, v32i16, VReg_512>;
def : BitConvert <v16i32, v16f32, VReg_512>;
def : BitConvert <v16f32, v16i32, VReg_512>;
def : BitConvert <v8i64,  v8f64,  VReg_512>;
def : BitConvert <v8f64,  v8i64,  VReg_512>;
def : BitConvert <v8i64,  v16i32, VReg_512>;
def : BitConvert <v8f64,  v16i32, VReg_512>;
def : BitConvert <v16i32, v8i64,  VReg_512>;
def : BitConvert <v16i32, v8f64,  VReg_512>;
def : BitConvert <v8i64,  v16f32, VReg_512>;
def : BitConvert <v8f64,  v16f32, VReg_512>;
def : BitConvert <v16f32, v8i64,  VReg_512>;
def : BitConvert <v16f32, v8f64,  VReg_512>;



def : BitConvert <v32bf16, v32i16, VReg_512>;
def : BitConvert <v32i16, v32bf16, VReg_512>;
def : BitConvert <v32bf16, v32i16, SReg_512>;
def : BitConvert <v32i16, v32bf16, SReg_512>;

def : BitConvert <v32bf16, v32f16, VReg_512>;
def : BitConvert <v32f16, v32bf16, VReg_512>;
def : BitConvert <v32bf16, v32f16, SReg_512>;
def : BitConvert <v32f16, v32bf16, SReg_512>;

def : BitConvert <v32bf16, v16i32, VReg_512>;
def : BitConvert <v16i32, v32bf16, VReg_512>;
def : BitConvert <v32bf16, v16i32, SReg_512>;
def : BitConvert <v16i32, v32bf16, SReg_512>;

def : BitConvert <v32bf16, v16f32, VReg_512>;
def : BitConvert <v16f32, v32bf16, VReg_512>;
def : BitConvert <v32bf16, v16f32, SReg_512>;
def : BitConvert <v16f32, v32bf16, SReg_512>;

def : BitConvert <v32bf16, v8f64, VReg_512>;
def : BitConvert <v8f64, v32bf16, VReg_512>;
def : BitConvert <v32bf16, v8f64, SReg_512>;
def : BitConvert <v8f64, v32bf16, SReg_512>;

def : BitConvert <v32bf16, v8i64, VReg_512>;
def : BitConvert <v8i64, v32bf16, VReg_512>;
def : BitConvert <v32bf16, v8i64, SReg_512>;
def : BitConvert <v8i64, v32bf16, SReg_512>;

// 1024-bit bitcast
def : BitConvert <v32i32, v32f32, VReg_1024>;
def : BitConvert <v32f32, v32i32, VReg_1024>;
def : BitConvert <v16i64, v16f64, VReg_1024>;
def : BitConvert <v16f64, v16i64, VReg_1024>;
def : BitConvert <v16i64, v32i32, VReg_1024>;
def : BitConvert <v32i32, v16i64, VReg_1024>;
def : BitConvert <v16f64, v32f32, VReg_1024>;
def : BitConvert <v32f32, v16f64, VReg_1024>;
def : BitConvert <v16i64, v32f32, VReg_1024>;
def : BitConvert <v32i32, v16f64, VReg_1024>;
def : BitConvert <v16f64, v32i32, VReg_1024>;
def : BitConvert <v32f32, v16i64, VReg_1024>;


/********** =================== **********/
/********** Src & Dst modifiers **********/
/********** =================== **********/


// If denormals are not enabled, it only impacts the compare of the
// inputs. The output result is not flushed.
class ClampPat<Instruction inst, ValueType vt> : GCNPat <
  (vt (AMDGPUclamp (VOP3Mods vt:$src0, i32:$src0_modifiers))),
  (inst i32:$src0_modifiers, vt:$src0,
        i32:$src0_modifiers, vt:$src0, DSTCLAMP.ENABLE, DSTOMOD.NONE)
>;

def : ClampPat<V_MAX_F32_e64, f32>;
let SubtargetPredicate = isNotGFX12Plus in
def : ClampPat<V_MAX_F64_e64, f64>;
let SubtargetPredicate = isGFX12Plus in
def : ClampPat<V_MAX_NUM_F64_e64, f64>;
let SubtargetPredicate = NotHasTrue16BitInsts in
def : ClampPat<V_MAX_F16_e64, f16>;
let SubtargetPredicate = UseRealTrue16Insts in
def : ClampPat<V_MAX_F16_t16_e64, f16>;
let SubtargetPredicate = UseFakeTrue16Insts in
def : ClampPat<V_MAX_F16_fake16_e64, f16>;

let SubtargetPredicate = HasVOP3PInsts in {
def : GCNPat <
  (v2f16 (AMDGPUclamp (VOP3PMods v2f16:$src0, i32:$src0_modifiers))),
  (V_PK_MAX_F16 $src0_modifiers, $src0,
                $src0_modifiers, $src0, DSTCLAMP.ENABLE)
>;
}


/********** ================================ **********/
/********** Floating point absolute/negative **********/
/********** ================================ **********/

def : GCNPat <
  (UniformUnaryFrag<fneg> (fabs (f32 SReg_32:$src))),
  (S_OR_B32 SReg_32:$src, (S_MOV_B32 (i32 0x80000000))) // Set sign bit
>;

def : GCNPat <
  (UniformUnaryFrag<fabs> (f32 SReg_32:$src)),
  (S_AND_B32 SReg_32:$src, (S_MOV_B32 (i32 0x7fffffff)))
>;

def : GCNPat <
  (UniformUnaryFrag<fneg> (f32 SReg_32:$src)),
  (S_XOR_B32 SReg_32:$src, (S_MOV_B32 (i32 0x80000000)))
>;

foreach fp16vt = [f16, bf16] in {
def : GCNPat <
  (UniformUnaryFrag<fneg> (fp16vt SReg_32:$src)),
  (S_XOR_B32 SReg_32:$src, (S_MOV_B32 (i32 0x00008000)))
>;

def : GCNPat <
  (UniformUnaryFrag<fabs> (fp16vt SReg_32:$src)),
  (S_AND_B32 SReg_32:$src, (S_MOV_B32 (i32 0x00007fff)))
>;

def : GCNPat <
  (UniformUnaryFrag<fneg> (fabs (fp16vt SReg_32:$src))),
  (S_OR_B32 SReg_32:$src, (S_MOV_B32 (i32 0x00008000))) // Set sign bit
>;
} // End foreach fp16vt = ...

def : GCNPat <
  (UniformUnaryFrag<fneg> (v2f16 SReg_32:$src)),
  (S_XOR_B32 SReg_32:$src, (S_MOV_B32 (i32 0x80008000)))
>;

def : GCNPat <
  (UniformUnaryFrag<fabs> (v2f16 SReg_32:$src)),
  (S_AND_B32 SReg_32:$src, (S_MOV_B32 (i32 0x7fff7fff)))
>;

// This is really (fneg (fabs v2f16:$src))
//
// fabs is not reported as free because there is modifier for it in
// VOP3P instructions, so it is turned into the bit op.
def : GCNPat <
  (UniformUnaryFrag<fneg> (v2f16 (bitconvert (and_oneuse (i32 SReg_32:$src), 0x7fff7fff)))),
  (S_OR_B32 SReg_32:$src, (S_MOV_B32 (i32 0x80008000))) // Set sign bit
>;

def : GCNPat <
  (UniformUnaryFrag<fneg> (v2f16 (fabs SReg_32:$src))),
  (S_OR_B32 SReg_32:$src, (S_MOV_B32 (i32 0x80008000))) // Set sign bit
>;


// COPY_TO_REGCLASS is needed to avoid using SCC from S_XOR_B32 instead
// of the real value.
def : GCNPat <
  (UniformUnaryFrag<fneg> (v2f32 SReg_64:$src)),
  (v2f32 (REG_SEQUENCE SReg_64,
         (f32 (COPY_TO_REGCLASS (S_XOR_B32 (i32 (EXTRACT_SUBREG $src, sub0)),
                                           (i32 (S_MOV_B32 (i32 0x80000000)))),
                                 SReg_32)), sub0,
         (f32 (COPY_TO_REGCLASS (S_XOR_B32 (i32 (EXTRACT_SUBREG $src, sub1)),
                                           (i32 (S_MOV_B32 (i32 0x80000000)))),
                                 SReg_32)), sub1))
>;

def : GCNPat <
  (UniformUnaryFrag<fabs> (v2f32 SReg_64:$src)),
  (v2f32 (REG_SEQUENCE SReg_64,
         (f32 (COPY_TO_REGCLASS (S_AND_B32 (i32 (EXTRACT_SUBREG $src, sub0)),
                                           (i32 (S_MOV_B32 (i32 0x7fffffff)))),
                                 SReg_32)), sub0,
         (f32 (COPY_TO_REGCLASS (S_AND_B32 (i32 (EXTRACT_SUBREG $src, sub1)),
                                           (i32 (S_MOV_B32 (i32 0x7fffffff)))),
                                 SReg_32)), sub1))
>;

def : GCNPat <
  (UniformUnaryFrag<fneg> (fabs (v2f32 SReg_64:$src))),
  (v2f32 (REG_SEQUENCE SReg_64,
         (f32 (COPY_TO_REGCLASS (S_OR_B32 (i32 (EXTRACT_SUBREG $src, sub0)),
                                           (i32 (S_MOV_B32 (i32 0x80000000)))),
                                 SReg_32)), sub0,
         (f32 (COPY_TO_REGCLASS (S_OR_B32 (i32 (EXTRACT_SUBREG $src, sub1)),
                                           (i32 (S_MOV_B32 (i32 0x80000000)))),
                                 SReg_32)), sub1))
>;

// FIXME: Use S_BITSET0_B32/B64?
def : GCNPat <
  (UniformUnaryFrag<fabs> (f64 SReg_64:$src)),
  (REG_SEQUENCE SReg_64,
    (i32 (EXTRACT_SUBREG SReg_64:$src, sub0)),
    sub0,
    (i32 (COPY_TO_REGCLASS (S_AND_B32 (i32 (EXTRACT_SUBREG SReg_64:$src, sub1)),
                   (S_MOV_B32 (i32 0x7fffffff))), SReg_32)), // Set sign bit.
     sub1)
>;

def : GCNPat <
  (UniformUnaryFrag<fneg> (f64 SReg_64:$src)),
  (REG_SEQUENCE SReg_64,
    (i32 (EXTRACT_SUBREG SReg_64:$src, sub0)),
    sub0,
    (i32 (COPY_TO_REGCLASS (S_XOR_B32 (i32 (EXTRACT_SUBREG SReg_64:$src, sub1)),
                   (i32 (S_MOV_B32 (i32 0x80000000)))), SReg_32)),
    sub1)
>;

def : GCNPat <
  (UniformUnaryFrag<fneg> (fabs (f64 SReg_64:$src))),
  (REG_SEQUENCE SReg_64,
    (i32 (EXTRACT_SUBREG SReg_64:$src, sub0)),
    sub0,
    (i32 (COPY_TO_REGCLASS (S_OR_B32 (i32 (EXTRACT_SUBREG SReg_64:$src, sub1)),
                  (S_MOV_B32 (i32 0x80000000))), SReg_32)),// Set sign bit.
    sub1)
>;


def : GCNPat <
  (fneg (fabs (f32 VGPR_32:$src))),
  (V_OR_B32_e64 (S_MOV_B32 (i32 0x80000000)), VGPR_32:$src) // Set sign bit
>;

def : GCNPat <
  (fabs (f32 VGPR_32:$src)),
  (V_AND_B32_e64 (S_MOV_B32 (i32 0x7fffffff)), VGPR_32:$src)
>;

def : GCNPat <
  (fneg (f32 VGPR_32:$src)),
  (V_XOR_B32_e64 (S_MOV_B32 (i32 0x80000000)), VGPR_32:$src)
>;

foreach fp16vt = [f16, bf16] in {
def : GCNPat <
  (fabs (fp16vt VGPR_32:$src)),
  (V_AND_B32_e64 (S_MOV_B32 (i32 0x00007fff)), VGPR_32:$src)
>;

def : GCNPat <
  (fneg (fp16vt VGPR_32:$src)),
  (V_XOR_B32_e64 (S_MOV_B32 (i32 0x00008000)), VGPR_32:$src)
>;

def : GCNPat <
  (fneg (fabs (fp16vt VGPR_32:$src))),
  (V_OR_B32_e64 (S_MOV_B32 (i32 0x00008000)), VGPR_32:$src) // Set sign bit
>;
} // End foreach fp16vt = ...

def : GCNPat <
  (fneg (v2f16 VGPR_32:$src)),
  (V_XOR_B32_e64 (S_MOV_B32 (i32 0x80008000)), VGPR_32:$src)
>;

def : GCNPat <
  (fabs (v2f16 VGPR_32:$src)),
  (V_AND_B32_e64 (S_MOV_B32 (i32 0x7fff7fff)), VGPR_32:$src)
>;

def : GCNPat <
  (fneg (v2f16 (fabs VGPR_32:$src))),
  (V_OR_B32_e64 (S_MOV_B32 (i32 0x80008000)), VGPR_32:$src)
>;

def : GCNPat <
  (fabs (f64 VReg_64:$src)),
  (REG_SEQUENCE VReg_64,
    (i32 (EXTRACT_SUBREG VReg_64:$src, sub0)),
    sub0,
    (V_AND_B32_e64 (i32 (S_MOV_B32 (i32 0x7fffffff))),
        (i32 (EXTRACT_SUBREG VReg_64:$src, sub1))),
     sub1)
>;

def : GCNPat <
  (fneg (f64 VReg_64:$src)),
  (REG_SEQUENCE VReg_64,
    (i32 (EXTRACT_SUBREG VReg_64:$src, sub0)),
    sub0,
    (V_XOR_B32_e64 (i32 (S_MOV_B32 (i32 0x80000000))),
        (i32 (EXTRACT_SUBREG VReg_64:$src, sub1))),
    sub1)
>;

def : GCNPat <
  (fneg (fabs (f64 VReg_64:$src))),
  (REG_SEQUENCE VReg_64,
    (i32 (EXTRACT_SUBREG VReg_64:$src, sub0)),
    sub0,
    (V_OR_B32_e64 (i32 (S_MOV_B32 (i32 0x80000000))),
        (i32 (EXTRACT_SUBREG VReg_64:$src, sub1))),
    sub1)
>;

def : GCNPat <
  (DivergentUnaryFrag<fneg> (v2f32 VReg_64:$src)),
  (V_PK_ADD_F32 11 /* OP_SEL_1 | NEG_LO | HEG_HI */, VReg_64:$src,
                11 /* OP_SEL_1 | NEG_LO | HEG_HI */, (i64 0),
                0, 0, 0, 0, 0)
> {
  let SubtargetPredicate = HasPackedFP32Ops;
}

foreach fp16vt = [f16, bf16] in {

def : GCNPat <
  (fcopysign fp16vt:$src0, fp16vt:$src1),
  (V_BFI_B32_e64 (S_MOV_B32 (i32 0x00007fff)), $src0, $src1)
>;

def : GCNPat <
  (fcopysign f32:$src0, fp16vt:$src1),
  (V_BFI_B32_e64 (S_MOV_B32 (i32 0x7fffffff)), $src0,
             (V_LSHLREV_B32_e64 (i32 16), $src1))
>;

def : GCNPat <
  (fcopysign f64:$src0, fp16vt:$src1),
  (REG_SEQUENCE SReg_64,
    (i32 (EXTRACT_SUBREG $src0, sub0)), sub0,
    (V_BFI_B32_e64 (S_MOV_B32 (i32 0x7fffffff)), (i32 (EXTRACT_SUBREG $src0, sub1)),
               (V_LSHLREV_B32_e64 (i32 16), $src1)), sub1)
>;

def : GCNPat <
  (fcopysign fp16vt:$src0, f32:$src1),
  (V_BFI_B32_e64 (S_MOV_B32 (i32 0x00007fff)), $src0,
             (V_LSHRREV_B32_e64 (i32 16), $src1))
>;

def : GCNPat <
  (fcopysign fp16vt:$src0, f64:$src1),
  (V_BFI_B32_e64 (S_MOV_B32 (i32 0x00007fff)), $src0,
             (V_LSHRREV_B32_e64 (i32 16), (EXTRACT_SUBREG $src1, sub1)))
>;
} // End foreach fp16vt = [f16, bf16]

/********** ================== **********/
/********** Immediate Patterns **********/
/********** ================== **********/

def : GCNPat <
  (VGPRImm<(i32 imm)>:$imm),
  (V_MOV_B32_e32 imm:$imm)
>;

def : GCNPat <
  (VGPRImm<(f32 fpimm)>:$imm),
  (V_MOV_B32_e32 (f32 (bitcast_fpimm_to_i32 $imm)))
>;

def : GCNPat <
  (i32 imm:$imm),
  (S_MOV_B32 imm:$imm)
>;

def : GCNPat <
  (VGPRImm<(SIlds tglobaladdr:$ga)>),
  (V_MOV_B32_e32 $ga)
>;

def : GCNPat <
  (SIlds tglobaladdr:$ga),
  (S_MOV_B32 $ga)
>;

// FIXME: Workaround for ordering issue with peephole optimizer where
// a register class copy interferes with immediate folding.  Should
// use s_mov_b32, which can be shrunk to s_movk_i32
def : GCNPat <
  (VGPRImm<(f16 fpimm)>:$imm),
  (V_MOV_B32_e32 (f16 (bitcast_fpimm_to_i32 $imm)))
>;

def : GCNPat <
  (VGPRImm<(bf16 fpimm)>:$imm),
  (V_MOV_B32_e32 (bf16 (bitcast_fpimm_to_i32 $imm)))
>;

// V_MOV_B64_PSEUDO and S_MOV_B64_IMM_PSEUDO can be used with any 64-bit
// immediate and wil be expanded as needed, but we will only use these patterns
// for values which can be encoded.
def : GCNPat <
  (VGPRImm<(i64 imm)>:$imm),
  (V_MOV_B64_PSEUDO imm:$imm)
>;

def : GCNPat <
  (VGPRImm<(f64 fpimm)>:$imm),
  (V_MOV_B64_PSEUDO (f64 (bitcast_fpimm_to_i64 $imm)))
>;

def : GCNPat <
  (i64 imm:$imm),
  (S_MOV_B64_IMM_PSEUDO imm:$imm)
>;

def : GCNPat <
  (f64 fpimm:$imm),
  (S_MOV_B64_IMM_PSEUDO (i64 (bitcast_fpimm_to_i64 fpimm:$imm)))
>;

def : GCNPat <
  (f32 fpimm:$imm),
  (S_MOV_B32 (f32 (bitcast_fpimm_to_i32 $imm)))
>;

def : GCNPat <
  (f16 fpimm:$imm),
  (S_MOV_B32 (i32 (bitcast_fpimm_to_i32 $imm)))
>;

def : GCNPat <
  (bf16 fpimm:$imm),
  (S_MOV_B32 (i32 (bitcast_fpimm_to_i32 $imm)))
>;

def : GCNPat <
  (p5 frameindex:$fi),
  (V_MOV_B32_e32 (p5 (frameindex_to_targetframeindex $fi)))
>;

def : GCNPat <
  (p5 frameindex:$fi),
  (S_MOV_B32 (p5 (frameindex_to_targetframeindex $fi)))
>;

def : GCNPat <
  (i64 InlineImm64:$imm),
  (S_MOV_B64 InlineImm64:$imm)
>;

// XXX - Should this use a s_cmp to set SCC?

// Set to sign-extended 64-bit value (true = -1, false = 0)
def : GCNPat <
  (i1 imm:$imm),
  (S_MOV_B64 (i64 (as_i64imm $imm)))
> {
  let WaveSizePredicate = isWave64;
}

def : GCNPat <
  (i1 imm:$imm),
  (S_MOV_B32 (i32 (as_i32imm $imm)))
> {
  let WaveSizePredicate = isWave32;
}

def : GCNPat <
  (f64 InlineImmFP64:$imm),
  (S_MOV_B64 (f64 (bitcast_fpimm_to_i64 InlineImmFP64:$imm)))
>;

/********** ================== **********/
/********** Intrinsic Patterns **********/
/********** ================== **********/

def : GCNPat <
  (f32 (fpow (VOP3Mods f32:$src0, i32:$src0_mods), (VOP3Mods f32:$src1, i32:$src1_mods))),
  (V_EXP_F32_e64 SRCMODS.NONE, (V_MUL_LEGACY_F32_e64 $src1_mods, $src1, SRCMODS.NONE, (V_LOG_F32_e64 $src0_mods, $src0), 0, 0))
>;

def : GCNPat <
  (i32 (sext i1:$src0)),
  (V_CNDMASK_B32_e64 /*src0mod*/(i32 0), /*src0*/(i32 0),
                     /*src1mod*/(i32 0), /*src1*/(i32 -1), i1:$src0)
>;

class Ext32Pat <SDNode ext> : GCNPat <
  (i32 (ext i1:$src0)),
  (V_CNDMASK_B32_e64 /*src0mod*/(i32 0), /*src0*/(i32 0),
                     /*src1mod*/(i32 0), /*src1*/(i32 1), i1:$src0)
>;

def : Ext32Pat <zext>;
def : Ext32Pat <anyext>;

// The multiplication scales from [0,1) to the unsigned integer range,
// rounding down a bit to avoid unwanted overflow.
def : GCNPat <
  (AMDGPUurecip i32:$src0),
  (V_CVT_U32_F32_e32
    (V_MUL_F32_e32 (i32 CONST.FP_4294966784),
                   (V_RCP_IFLAG_F32_e32 (V_CVT_F32_U32_e32 $src0))))
>;

//===----------------------------------------------------------------------===//
// VOP3 Patterns
//===----------------------------------------------------------------------===//

def : IMad24Pat<V_MAD_I32_I24_e64, 1>;
def : UMad24Pat<V_MAD_U32_U24_e64, 1>;

// BFI patterns

def BFIImm32 : PatFrag<
  (ops node:$x, node:$y, node:$z),
  (i32 (DivergentBinFrag<or> (and node:$y, node:$x), (and node:$z, imm))),
  [{
    auto *X = dyn_cast<ConstantSDNode>(N->getOperand(0)->getOperand(1));
    auto *NotX = dyn_cast<ConstantSDNode>(N->getOperand(1)->getOperand(1));
    return X && NotX &&
      ~(unsigned)X->getZExtValue() == (unsigned)NotX->getZExtValue();
  }]
>;


// Definition from ISA doc:
// (y & x) | (z & ~x)
def : AMDGPUPatIgnoreCopies <
  (DivergentBinFrag<or> (and i32:$y, i32:$x), (and i32:$z, (not i32:$x))),
  (V_BFI_B32_e64 (COPY_TO_REGCLASS VSrc_b32:$x, VGPR_32),
                (COPY_TO_REGCLASS VSrc_b32:$y, VGPR_32),
                (COPY_TO_REGCLASS VSrc_b32:$z, VGPR_32))
>;

// (y & C) | (z & ~C)
def : AMDGPUPatIgnoreCopies <
  (BFIImm32 i32:$x, i32:$y, i32:$z),
  (V_BFI_B32_e64 VSrc_b32:$x, VSrc_b32:$y, VSrc_b32:$z)
>;

// 64-bit version
def : AMDGPUPatIgnoreCopies <
  (DivergentBinFrag<or> (and i64:$y, i64:$x), (and i64:$z, (not i64:$x))),
  (REG_SEQUENCE VReg_64,
    (V_BFI_B32_e64 (i32 (EXTRACT_SUBREG VReg_64:$x, sub0)),
              (i32 (EXTRACT_SUBREG VReg_64:$y, sub0)),
              (i32 (EXTRACT_SUBREG VReg_64:$z, sub0))), sub0,
    (V_BFI_B32_e64 (i32 (EXTRACT_SUBREG VReg_64:$x, sub1)),
              (i32 (EXTRACT_SUBREG VReg_64:$y, sub1)),
              (i32 (EXTRACT_SUBREG VReg_64:$z, sub1))), sub1)
>;

// SHA-256 Ch function
// z ^ (x & (y ^ z))
def : AMDGPUPatIgnoreCopies <
  (DivergentBinFrag<xor> i32:$z, (and i32:$x, (xor i32:$y, i32:$z))),
  (V_BFI_B32_e64 (COPY_TO_REGCLASS VSrc_b32:$x, VGPR_32),
                (COPY_TO_REGCLASS VSrc_b32:$y, VGPR_32),
                (COPY_TO_REGCLASS VSrc_b32:$z, VGPR_32))
>;

// 64-bit version
def : AMDGPUPatIgnoreCopies <
  (DivergentBinFrag<xor> i64:$z, (and i64:$x, (xor i64:$y, i64:$z))),
  (REG_SEQUENCE VReg_64,
    (V_BFI_B32_e64 (i32 (EXTRACT_SUBREG VReg_64:$x, sub0)),
              (i32 (EXTRACT_SUBREG VReg_64:$y, sub0)),
              (i32 (EXTRACT_SUBREG VReg_64:$z, sub0))), sub0,
    (V_BFI_B32_e64 (i32 (EXTRACT_SUBREG VReg_64:$x, sub1)),
              (i32 (EXTRACT_SUBREG VReg_64:$y, sub1)),
              (i32 (EXTRACT_SUBREG VReg_64:$z, sub1))), sub1)
>;

def : AMDGPUPat <
  (fcopysign f32:$src0, f32:$src1),
  (V_BFI_B32_e64 (S_MOV_B32 (i32 0x7fffffff)), $src0, $src1)
>;

def : AMDGPUPat <
  (fcopysign f32:$src0, f64:$src1),
  (V_BFI_B32_e64 (S_MOV_B32 (i32 0x7fffffff)), $src0,
             (i32 (EXTRACT_SUBREG SReg_64:$src1, sub1)))
>;

def : AMDGPUPat <
  (fcopysign f64:$src0, f64:$src1),
  (REG_SEQUENCE SReg_64,
    (i32 (EXTRACT_SUBREG $src0, sub0)), sub0,
    (V_BFI_B32_e64 (S_MOV_B32 (i32 0x7fffffff)),
               (i32 (EXTRACT_SUBREG SReg_64:$src0, sub1)),
               (i32 (EXTRACT_SUBREG SReg_64:$src1, sub1))), sub1)
>;

def : AMDGPUPat <
  (fcopysign f64:$src0, f32:$src1),
  (REG_SEQUENCE SReg_64,
    (i32 (EXTRACT_SUBREG $src0, sub0)), sub0,
    (V_BFI_B32_e64 (S_MOV_B32 (i32 0x7fffffff)),
               (i32 (EXTRACT_SUBREG SReg_64:$src0, sub1)),
               $src1), sub1)
>;

def : ROTRPattern <V_ALIGNBIT_B32_e64>;

def : GCNPat<(i32 (trunc (srl i64:$src0, (and i32:$src1, (i32 31))))),
          (V_ALIGNBIT_B32_e64 (i32 (EXTRACT_SUBREG (i64 $src0), sub1)),
                          (i32 (EXTRACT_SUBREG (i64 $src0), sub0)), $src1)>;

def : GCNPat<(i32 (trunc (srl i64:$src0, (i32 ShiftAmt32Imm:$src1)))),
          (V_ALIGNBIT_B32_e64 (i32 (EXTRACT_SUBREG (i64 $src0), sub1)),
                          (i32 (EXTRACT_SUBREG (i64 $src0), sub0)), $src1)>;

/********** ====================== **********/
/**********   Indirect addressing  **********/
/********** ====================== **********/

multiclass SI_INDIRECT_Pattern <ValueType vt, ValueType eltvt, string VecSize> {
  // Extract with offset
  def : GCNPat<
    (eltvt (extractelt vt:$src, (MOVRELOffset i32:$idx, (i32 imm:$offset)))),
    (!cast<Instruction>("SI_INDIRECT_SRC_"#VecSize) $src, $idx, imm:$offset)
  >;

  // Insert with offset
  def : GCNPat<
    (insertelt vt:$src, eltvt:$val, (MOVRELOffset i32:$idx, (i32 imm:$offset))),
    (!cast<Instruction>("SI_INDIRECT_DST_"#VecSize) $src, $idx, imm:$offset, $val)
  >;
}

defm : SI_INDIRECT_Pattern <v2f32, f32, "V2">;
defm : SI_INDIRECT_Pattern <v4f32, f32, "V4">;
defm : SI_INDIRECT_Pattern <v8f32, f32, "V8">;
defm : SI_INDIRECT_Pattern <v9f32, f32, "V9">;
defm : SI_INDIRECT_Pattern <v10f32, f32, "V10">;
defm : SI_INDIRECT_Pattern <v11f32, f32, "V11">;
defm : SI_INDIRECT_Pattern <v12f32, f32, "V12">;
defm : SI_INDIRECT_Pattern <v16f32, f32, "V16">;
defm : SI_INDIRECT_Pattern <v32f32, f32, "V32">;

defm : SI_INDIRECT_Pattern <v2i32, i32, "V2">;
defm : SI_INDIRECT_Pattern <v4i32, i32, "V4">;
defm : SI_INDIRECT_Pattern <v8i32, i32, "V8">;
defm : SI_INDIRECT_Pattern <v9i32, i32, "V9">;
defm : SI_INDIRECT_Pattern <v10i32, i32, "V10">;
defm : SI_INDIRECT_Pattern <v11i32, i32, "V11">;
defm : SI_INDIRECT_Pattern <v12i32, i32, "V12">;
defm : SI_INDIRECT_Pattern <v16i32, i32, "V16">;
defm : SI_INDIRECT_Pattern <v32i32, i32, "V32">;

//===----------------------------------------------------------------------===//
// SAD Patterns
//===----------------------------------------------------------------------===//

def : GCNPat <
  (add (sub_oneuse (umax i32:$src0, i32:$src1),
                   (umin i32:$src0, i32:$src1)),
       i32:$src2),
  (V_SAD_U32_e64 $src0, $src1, $src2, (i1 0))
>;

def : GCNPat <
  (add (select_oneuse (i1 (setugt i32:$src0, i32:$src1)),
                      (sub i32:$src0, i32:$src1),
                      (sub i32:$src1, i32:$src0)),
       i32:$src2),
  (V_SAD_U32_e64 $src0, $src1, $src2, (i1 0))
>;

//===----------------------------------------------------------------------===//
// Conversion Patterns
//===----------------------------------------------------------------------===//
def : GCNPat<(i32 (UniformSextInreg<i1> i32:$src)),
  (S_BFE_I32 i32:$src, (i32 65536))>; // 0 | 1 << 16

// Handle sext_inreg in i64
def : GCNPat <
  (i64 (UniformSextInreg<i1> i64:$src)),
  (S_BFE_I64 i64:$src, (i32 0x10000)) // 0 | 1 << 16
>;

def : GCNPat <
  (i16 (UniformSextInreg<i1> i16:$src)),
  (S_BFE_I32 $src, (i32 0x00010000)) // 0 | 1 << 16
>;

def : GCNPat <
  (i16 (UniformSextInreg<i8> i16:$src)),
  (S_BFE_I32 $src, (i32 0x80000)) // 0 | 8 << 16
>;

def : GCNPat <
  (i64 (UniformSextInreg<i8> i64:$src)),
  (S_BFE_I64 i64:$src, (i32 0x80000)) // 0 | 8 << 16
>;

def : GCNPat <
  (i64 (UniformSextInreg<i16> i64:$src)),
  (S_BFE_I64 i64:$src, (i32 0x100000)) // 0 | 16 << 16
>;

def : GCNPat <
  (i64 (UniformSextInreg<i32> i64:$src)),
  (S_BFE_I64 i64:$src, (i32 0x200000)) // 0 | 32 << 16
>;

def : GCNPat<
  (i32 (DivergentSextInreg<i1> i32:$src)),
  (V_BFE_I32_e64 i32:$src, (i32 0), (i32 1))>;

def : GCNPat <
  (i16 (DivergentSextInreg<i1> i16:$src)),
  (V_BFE_I32_e64 $src, (i32 0), (i32 1))
>;

def : GCNPat <
  (i16 (DivergentSextInreg<i8> i16:$src)),
  (V_BFE_I32_e64 $src, (i32 0), (i32 8))
>;

def : GCNPat<
  (i32 (DivergentSextInreg<i8> i32:$src)),
  (V_BFE_I32_e64 i32:$src, (i32 0), (i32 8))
>;

def : GCNPat <
  (i32 (DivergentSextInreg<i16> i32:$src)),
  (V_BFE_I32_e64 $src, (i32 0), (i32 16))
>;

def : GCNPat <
  (i64 (DivergentSextInreg<i1> i64:$src)),
  (REG_SEQUENCE VReg_64,
    (V_BFE_I32_e64 (i32 (EXTRACT_SUBREG i64:$src, sub0)), (i32 0), (i32 1)), sub0,
    (V_ASHRREV_I32_e32  (i32 31), (V_BFE_I32_e64 (i32 (EXTRACT_SUBREG i64:$src, sub0)), (i32 0), (i32 1))), sub1)
>;

def : GCNPat <
  (i64 (DivergentSextInreg<i8> i64:$src)),
  (REG_SEQUENCE VReg_64,
    (V_BFE_I32_e64 (i32 (EXTRACT_SUBREG i64:$src, sub0)), (i32 0), (i32 8)), sub0,
    (V_ASHRREV_I32_e32 (i32 31), (V_BFE_I32_e64 (i32 (EXTRACT_SUBREG i64:$src, sub0)), (i32 0), (i32 8))), sub1)
>;

def : GCNPat <
  (i64 (DivergentSextInreg<i16> i64:$src)),
  (REG_SEQUENCE VReg_64,
    (V_BFE_I32_e64 (i32 (EXTRACT_SUBREG i64:$src, sub0)), (i32 0), (i32 16)), sub0,
    (V_ASHRREV_I32_e32 (i32 31), (V_BFE_I32_e64 (i32 (EXTRACT_SUBREG i64:$src, sub0)), (i32 0), (i32 16))), sub1)
>;

def : GCNPat <
  (i64 (DivergentSextInreg<i32> i64:$src)),
  (REG_SEQUENCE VReg_64,
    (i32 (EXTRACT_SUBREG i64:$src, sub0)), sub0,
    (V_ASHRREV_I32_e32 (i32 31), (i32 (EXTRACT_SUBREG i64:$src, sub0))), sub1)
>;

def : GCNPat <
  (i64 (zext i32:$src)),
  (REG_SEQUENCE SReg_64, $src, sub0, (S_MOV_B32 (i32 0)), sub1)
>;

def : GCNPat <
  (i64 (anyext i32:$src)),
  (REG_SEQUENCE SReg_64, $src, sub0, (i32 (IMPLICIT_DEF)), sub1)
>;

class ZExt_i64_i1_Pat <SDNode ext> : GCNPat <
  (i64 (ext i1:$src)),
    (REG_SEQUENCE VReg_64,
      (V_CNDMASK_B32_e64 /*src0mod*/(i32 0), /*src0*/(i32 0),
                         /*src1mod*/(i32 0), /*src1*/(i32 1), $src),
      sub0, (S_MOV_B32 (i32 0)), sub1)
>;


def : ZExt_i64_i1_Pat<zext>;
def : ZExt_i64_i1_Pat<anyext>;

// FIXME: We need to use COPY_TO_REGCLASS to work-around the fact that
// REG_SEQUENCE patterns don't support instructions with multiple outputs.
def : GCNPat <
  (i64 (UniformUnaryFrag<sext> i32:$src)),
    (REG_SEQUENCE SReg_64, $src, sub0,
    (i32 (COPY_TO_REGCLASS (S_ASHR_I32 $src, (i32 31)), SReg_32_XM0)), sub1)
>;

def : GCNPat <
  (i64 (DivergentUnaryFrag<sext> i32:$src)),
    (REG_SEQUENCE VReg_64, $src, sub0,
    (i32 (COPY_TO_REGCLASS (V_ASHRREV_I32_e64 (i32 31), $src), VGPR_32)), sub1)
>;

def : GCNPat <
  (i64 (sext i1:$src)),
  (REG_SEQUENCE VReg_64,
    (V_CNDMASK_B32_e64 /*src0mod*/(i32 0), /*src0*/(i32 0),
                       /*src1mod*/(i32 0), /*src1*/(i32 -1), $src), sub0,
    (V_CNDMASK_B32_e64 /*src0mod*/(i32 0), /*src0*/(i32 0),
                       /*src1mod*/(i32 0), /*src1*/(i32 -1), $src), sub1)
>;

class FPToI1Pat<Instruction Inst, int KOne, ValueType kone_type, ValueType vt, SDPatternOperator fp_to_int> : GCNPat <
  (i1 (fp_to_int (vt (VOP3Mods vt:$src0, i32:$src0_modifiers)))),
  (i1 (Inst 0, (kone_type KOne), $src0_modifiers, $src0, DSTCLAMP.NONE))
>;

let OtherPredicates = [NotHasTrue16BitInsts] in {
  def : FPToI1Pat<V_CMP_EQ_F16_e64, CONST.FP16_ONE, i16, f16, fp_to_uint>;
  def : FPToI1Pat<V_CMP_EQ_F16_e64, CONST.FP16_NEG_ONE, i16, f16, fp_to_sint>;
} // end OtherPredicates = [NotHasTrue16BitInsts]

let OtherPredicates = [HasTrue16BitInsts] in {
  def : FPToI1Pat<V_CMP_EQ_F16_t16_e64, CONST.FP16_ONE, i16, f16, fp_to_uint>;
  def : FPToI1Pat<V_CMP_EQ_F16_t16_e64, CONST.FP16_NEG_ONE, i16, f16, fp_to_sint>;
} // end OtherPredicates = [HasTrue16BitInsts]

def : FPToI1Pat<V_CMP_EQ_F32_e64, CONST.FP32_ONE, i32, f32, fp_to_uint>;
def : FPToI1Pat<V_CMP_EQ_F32_e64, CONST.FP32_NEG_ONE, i32, f32, fp_to_sint>;
def : FPToI1Pat<V_CMP_EQ_F64_e64, CONST.FP64_ONE, i64, f64, fp_to_uint>;
def : FPToI1Pat<V_CMP_EQ_F64_e64, CONST.FP64_NEG_ONE, i64, f64, fp_to_sint>;

// If we need to perform a logical operation on i1 values, we need to
// use vector comparisons since there is only one SCC register. Vector
// comparisons may write to a pair of SGPRs or a single SGPR, so treat
// these as 32 or 64-bit comparisons. When legalizing SGPR copies,
// instructions resulting in the copies from SCC to these instructions
// will be moved to the VALU.

let WaveSizePredicate = isWave64 in {
def : GCNPat <
  (i1 (and i1:$src0, i1:$src1)),
  (S_AND_B64 $src0, $src1)
>;

def : GCNPat <
  (i1 (or i1:$src0, i1:$src1)),
  (S_OR_B64 $src0, $src1)
>;

def : GCNPat <
  (i1 (xor i1:$src0, i1:$src1)),
  (S_XOR_B64 $src0, $src1)
>;

def : GCNPat <
  (i1 (add i1:$src0, i1:$src1)),
  (S_XOR_B64 $src0, $src1)
>;

def : GCNPat <
  (i1 (sub i1:$src0, i1:$src1)),
  (S_XOR_B64 $src0, $src1)
>;

let AddedComplexity = 1 in {
def : GCNPat <
  (i1 (add i1:$src0, (i1 -1))),
  (S_NOT_B64 $src0)
>;

def : GCNPat <
  (i1 (sub i1:$src0, (i1 -1))),
  (S_NOT_B64 $src0)
>;
}
} // end isWave64

let WaveSizePredicate = isWave32 in {
def : GCNPat <
  (i1 (and i1:$src0, i1:$src1)),
  (S_AND_B32 $src0, $src1)
>;

def : GCNPat <
  (i1 (or i1:$src0, i1:$src1)),
  (S_OR_B32 $src0, $src1)
>;

def : GCNPat <
  (i1 (xor i1:$src0, i1:$src1)),
  (S_XOR_B32 $src0, $src1)
>;

def : GCNPat <
  (i1 (add i1:$src0, i1:$src1)),
  (S_XOR_B32 $src0, $src1)
>;

def : GCNPat <
  (i1 (sub i1:$src0, i1:$src1)),
  (S_XOR_B32 $src0, $src1)
>;

let AddedComplexity = 1 in {
def : GCNPat <
  (i1 (add i1:$src0, (i1 -1))),
  (S_NOT_B32 $src0)
>;

def : GCNPat <
  (i1 (sub i1:$src0, (i1 -1))),
  (S_NOT_B32 $src0)
>;
}
} // end isWave32

def : GCNPat <
  (i32 (DivergentBinFrag<xor> i32:$src0, (i32 -1))),
  (V_NOT_B32_e32 $src0)
>;

def : GCNPat <
  (i64 (DivergentBinFrag<xor> i64:$src0, (i64 -1))),
    (REG_SEQUENCE VReg_64,
      (V_NOT_B32_e32 (i32 (EXTRACT_SUBREG i64:$src0, sub0))), sub0,
      (V_NOT_B32_e32 (i32 (EXTRACT_SUBREG i64:$src0, sub1))), sub1
    )
>;

let SubtargetPredicate = NotHasTrue16BitInsts in
def : GCNPat <
  (f16 (sint_to_fp i1:$src)),
  (V_CVT_F16_F32_e32 (
      V_CNDMASK_B32_e64 /*src0mod*/(i32 0), /*src0*/(i32 0),
                        /*src1mod*/(i32 0), /*src1*/(i32 CONST.FP32_NEG_ONE),
                        SSrc_i1:$src))
>;

let SubtargetPredicate = HasTrue16BitInsts in
def : GCNPat <
  (f16 (sint_to_fp i1:$src)),
  (V_CVT_F16_F32_t16_e32 (
      V_CNDMASK_B32_e64 /*src0mod*/(i32 0), /*src0*/(i32 0),
                        /*src1mod*/(i32 0), /*src1*/(i32 CONST.FP32_NEG_ONE),
                        SSrc_i1:$src))
>;

let SubtargetPredicate = NotHasTrue16BitInsts in
def : GCNPat <
  (f16 (uint_to_fp i1:$src)),
  (V_CVT_F16_F32_e32 (
      V_CNDMASK_B32_e64 /*src0mod*/(i32 0), /*src0*/(i32 0),
                        /*src1mod*/(i32 0), /*src1*/(i32 CONST.FP32_ONE),
                        SSrc_i1:$src))
>;
let SubtargetPredicate = HasTrue16BitInsts in
def : GCNPat <
  (f16 (uint_to_fp i1:$src)),
  (V_CVT_F16_F32_t16_e32 (
      V_CNDMASK_B32_e64 /*src0mod*/(i32 0), /*src0*/(i32 0),
                        /*src1mod*/(i32 0), /*src1*/(i32 CONST.FP32_ONE),
                        SSrc_i1:$src))
>;

def : GCNPat <
  (f32 (sint_to_fp i1:$src)),
  (V_CNDMASK_B32_e64 /*src0mod*/(i32 0), /*src0*/(i32 0),
                        /*src1mod*/(i32 0), /*src1*/(i32 CONST.FP32_NEG_ONE),
                        SSrc_i1:$src)
>;

def : GCNPat <
  (f32 (uint_to_fp i1:$src)),
  (V_CNDMASK_B32_e64 /*src0mod*/(i32 0), /*src0*/(i32 0),
                        /*src1mod*/(i32 0), /*src1*/(i32 CONST.FP32_ONE),
                        SSrc_i1:$src)
>;

def : GCNPat <
  (f64 (sint_to_fp i1:$src)),
  (V_CVT_F64_I32_e32 (V_CNDMASK_B32_e64 /*src0mod*/(i32 0), /*src0*/(i32 0),
                                        /*src1mod*/(i32 0), /*src1*/(i32 -1),
                                        SSrc_i1:$src))
>;

def : GCNPat <
  (f64 (uint_to_fp i1:$src)),
  (V_CVT_F64_U32_e32 (V_CNDMASK_B32_e64 /*src0mod*/(i32 0), /*src0*/(i32 0),
                                        /*src1mod*/(i32 0), /*src1*/(i32 1),
                                        SSrc_i1:$src))
>;

//===----------------------------------------------------------------------===//
// Miscellaneous Patterns
//===----------------------------------------------------------------------===//

// Eliminate a zero extension from an fp16 operation if it already
// zeros the high bits of the 32-bit register.
//
// This is complicated on gfx9+. Some instructions maintain the legacy
// zeroing behavior, but others preserve the high bits. Some have a
// control bit to change the behavior. We can't simply say with
// certainty what the source behavior is without more context on how
// the src is lowered. e.g. fptrunc + fma may be lowered to a
// v_fma_mix* instruction which does not zero, or may not.
def : GCNPat<
  (i32 (DivergentUnaryFrag<abs> i32:$src)),
  (V_MAX_I32_e64 (V_SUB_CO_U32_e32 (i32 0), $src), $src)>;

let AddedComplexity = 1 in {
def : GCNPat<
  (i32 (DivergentUnaryFrag<abs> i32:$src)),
  (V_MAX_I32_e64 (V_SUB_U32_e32 (i32 0), $src), $src)>{
  let SubtargetPredicate = HasAddNoCarryInsts;
}
}  // AddedComplexity = 1

def : GCNPat<
  (i32 (DivergentUnaryFrag<zext> i16:$src)),
  (V_AND_B32_e64 (S_MOV_B32 (i32 0xffff)), $src)
>;

def : GCNPat<
  (i64 (DivergentUnaryFrag<zext> i16:$src)),
  (REG_SEQUENCE VReg_64,
    (V_AND_B32_e64 (S_MOV_B32 (i32 0xffff)), $src), sub0,
    (S_MOV_B32 (i32 0)), sub1)
>;

def : GCNPat<
  (i32 (zext (i16 (bitconvert fp16_zeros_high_16bits:$src)))),
  (COPY VSrc_b16:$src)>;

def : GCNPat <
  (i32 (trunc i64:$a)),
  (EXTRACT_SUBREG $a, sub0)
>;

def : GCNPat <
  (i1 (UniformUnaryFrag<trunc> i32:$a)),
  (S_CMP_EQ_U32 (S_AND_B32 (i32 1), $a), (i32 1))
>;

def : GCNPat <
  (i1 (UniformUnaryFrag<trunc> i16:$a)),
  (S_CMP_EQ_U32 (S_AND_B32 (i32 1), $a), (i32 1))
>;

def : GCNPat <
  (i1 (UniformUnaryFrag<trunc> i64:$a)),
  (S_CMP_EQ_U32 (S_AND_B32 (i32 1),
                    (i32 (EXTRACT_SUBREG $a, sub0))), (i32 1))
>;

def : GCNPat <
  (i1 (DivergentUnaryFrag<trunc> i32:$a)),
  (V_CMP_EQ_U32_e64 (V_AND_B32_e64 (i32 1), $a), (i32 1))
>;

def : GCNPat <
  (i1 (DivergentUnaryFrag<trunc> i16:$a)),
  (V_CMP_EQ_U32_e64 (V_AND_B32_e64 (i32 1), $a), (i32 1))
>;

def IMMBitSelConst : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(1ULL << N->getZExtValue(), SDLoc(N),
                                   MVT::i32);
}]>;

// Matching separate SRL and TRUNC instructions
// with dependent operands (SRL dest is source of TRUNC)
// generates three instructions. However, by using bit shifts,
// the V_LSHRREV_B32_e64 result can be directly used in the
// operand of the V_AND_B32_e64 instruction:
// (trunc i32 (srl i32 $a, i32 $b)) ->
// v_and_b32_e64 $a, (1 << $b), $a
// v_cmp_ne_u32_e64 $a, 0, $a

// Handle the VALU case.
def : GCNPat <
  (i1 (DivergentUnaryFrag<trunc> (i32 (srl i32:$a, (i32 imm:$b))))),
  (V_CMP_NE_U32_e64 (V_AND_B32_e64 (i32 (IMMBitSelConst $b)), $a),
    (i32 0))
>;

// Handle the scalar case.
def : GCNPat <
  (i1 (UniformUnaryFrag<trunc> (i32 (srl i32:$a, (i32 imm:$b))))),
  (S_CMP_LG_U32 (S_AND_B32 (i32 (IMMBitSelConst $b)), $a),
    (i32 0))
>;

def : GCNPat <
  (i1 (DivergentUnaryFrag<trunc> i64:$a)),
  (V_CMP_EQ_U32_e64 (V_AND_B32_e64 (i32 1),
                    (i32 (EXTRACT_SUBREG $a, sub0))), (i32 1))
>;

def : GCNPat <
  (i32 (bswap i32:$a)),
  (V_BFI_B32_e64 (S_MOV_B32 (i32 0x00ff00ff)),
             (V_ALIGNBIT_B32_e64 VSrc_b32:$a, VSrc_b32:$a, (i32 24)),
             (V_ALIGNBIT_B32_e64 VSrc_b32:$a, VSrc_b32:$a, (i32 8)))
>;

// FIXME: This should have been narrowed to i32 during legalization.
// This pattern should also be skipped for GlobalISel
def : GCNPat <
  (i64 (bswap i64:$a)),
  (REG_SEQUENCE VReg_64,
  (V_BFI_B32_e64 (S_MOV_B32 (i32 0x00ff00ff)),
             (V_ALIGNBIT_B32_e64 (i32 (EXTRACT_SUBREG VReg_64:$a, sub1)),
                             (i32 (EXTRACT_SUBREG VReg_64:$a, sub1)),
                             (i32 24)),
             (V_ALIGNBIT_B32_e64 (i32 (EXTRACT_SUBREG VReg_64:$a, sub1)),
                             (i32 (EXTRACT_SUBREG VReg_64:$a, sub1)),
                             (i32 8))),
  sub0,
  (V_BFI_B32_e64 (S_MOV_B32 (i32 0x00ff00ff)),
             (V_ALIGNBIT_B32_e64 (i32 (EXTRACT_SUBREG VReg_64:$a, sub0)),
                             (i32 (EXTRACT_SUBREG VReg_64:$a, sub0)),
                             (i32 24)),
             (V_ALIGNBIT_B32_e64 (i32 (EXTRACT_SUBREG VReg_64:$a, sub0)),
                             (i32 (EXTRACT_SUBREG VReg_64:$a, sub0)),
                             (i32 8))),
  sub1)
>;

// FIXME: The AddedComplexity should not be needed, but in GlobalISel
// the BFI pattern ends up taking precedence without it.
let SubtargetPredicate = isGFX8Plus, AddedComplexity = 1 in {
// Magic number: 3 | (2 << 8) | (1 << 16) | (0 << 24)
//
// My reading of the manual suggests we should be using src0 for the
// register value, but this is what seems to work.
def : GCNPat <
  (i32 (bswap i32:$a)),
  (V_PERM_B32_e64 (i32 0), VSrc_b32:$a, (S_MOV_B32 (i32 0x00010203)))
>;

// FIXME: This should have been narrowed to i32 during legalization.
// This pattern should also be skipped for GlobalISel
def : GCNPat <
  (i64 (bswap i64:$a)),
  (REG_SEQUENCE VReg_64,
  (V_PERM_B32_e64  (i32 0), (EXTRACT_SUBREG VReg_64:$a, sub1),
              (S_MOV_B32 (i32 0x00010203))),
  sub0,
  (V_PERM_B32_e64  (i32 0), (EXTRACT_SUBREG VReg_64:$a, sub0),
              (S_MOV_B32 (i32 0x00010203))),
  sub1)
>;

// Magic number: 1 | (0 << 8) | (12 << 16) | (12 << 24)
// The 12s emit 0s.
def : GCNPat <
  (i16 (bswap i16:$a)),
  (V_PERM_B32_e64  (i32 0), VSrc_b32:$a, (S_MOV_B32 (i32 0x0c0c0001)))
>;

def : GCNPat <
  (i32 (zext (bswap i16:$a))),
  (V_PERM_B32_e64  (i32 0), VSrc_b32:$a, (S_MOV_B32 (i32 0x0c0c0001)))
>;

// Magic number: 1 | (0 << 8) | (3 << 16) | (2 << 24)
def : GCNPat <
  (v2i16 (bswap v2i16:$a)),
  (V_PERM_B32_e64  (i32 0), VSrc_b32:$a, (S_MOV_B32 (i32 0x02030001)))
>;

}

def : GCNPat<
  (i64 (DivergentUnaryFrag<bitreverse> i64:$a)),
  (REG_SEQUENCE VReg_64,
   (V_BFREV_B32_e64 (i32 (EXTRACT_SUBREG VReg_64:$a, sub1))), sub0,
   (V_BFREV_B32_e64 (i32 (EXTRACT_SUBREG VReg_64:$a, sub0))), sub1)>;

// If fcanonicalize's operand is implicitly canonicalized, we only need a copy.
let AddedComplexity = 1000 in {
foreach vt = [f16, v2f16, f32, v2f32, f64] in {
  def : GCNPat<
    (fcanonicalize (vt is_canonicalized:$src)),
    (COPY vt:$src)
  >;
}
}

// Prefer selecting to max when legal, but using mul is always valid.
let AddedComplexity = -5 in {

let OtherPredicates = [NotHasTrue16BitInsts] in {
def : GCNPat<
  (fcanonicalize (f16 (VOP3Mods f16:$src, i32:$src_mods))),
  (V_MUL_F16_e64 0, (i32 CONST.FP16_ONE), $src_mods, $src)
>;

def : GCNPat<
  (fcanonicalize (f16 (fneg (VOP3Mods f16:$src, i32:$src_mods)))),
  (V_MUL_F16_e64 0, (i32 CONST.FP16_NEG_ONE), $src_mods, $src)
>;
} // End OtherPredicates

let OtherPredicates = [HasTrue16BitInsts] in {
def : GCNPat<
  (fcanonicalize (f16 (VOP3Mods f16:$src, i32:$src_mods))),
  (V_MUL_F16_fake16_e64 0, (i32 CONST.FP16_ONE), $src_mods, $src)
>;

def : GCNPat<
  (fcanonicalize (f16 (fneg (VOP3Mods f16:$src, i32:$src_mods)))),
  (V_MUL_F16_fake16_e64 0, (i32 CONST.FP16_NEG_ONE), $src_mods, $src)
>;
} // End OtherPredicates

def : GCNPat<
  (fcanonicalize (v2f16 (VOP3PMods v2f16:$src, i32:$src_mods))),
  (V_PK_MUL_F16 0, (i32 CONST.FP16_ONE), $src_mods, $src, DSTCLAMP.NONE)
>;

def : GCNPat<
  (fcanonicalize (f32 (VOP3Mods f32:$src, i32:$src_mods))),
  (V_MUL_F32_e64 0, (i32 CONST.FP32_ONE), $src_mods, $src)
>;

def : GCNPat<
  (fcanonicalize (f32 (fneg (VOP3Mods f32:$src, i32:$src_mods)))),
  (V_MUL_F32_e64 0, (i32 CONST.FP32_NEG_ONE), $src_mods, $src)
>;

let SubtargetPredicate = HasPackedFP32Ops in {
def : GCNPat<
  (fcanonicalize (v2f32 (VOP3PMods v2f32:$src, i32:$src_mods))),
  (V_PK_MUL_F32 0, (i64 CONST.FP32_ONE), $src_mods, $src)
>;
}

// TODO: Handle fneg like other types.
let SubtargetPredicate = isNotGFX12Plus in {
def : GCNPat<
  (fcanonicalize (f64 (VOP3Mods f64:$src, i32:$src_mods))),
  (V_MUL_F64_e64  0, (i64 CONST.FP64_ONE), $src_mods, $src)
>;
}
} // End AddedComplexity = -5

multiclass SelectCanonicalizeAsMax<
  list<Predicate> f32_preds = [],
  list<Predicate> f64_preds = [],
  list<Predicate> f16_preds = []> {
  def : GCNPat<
    (fcanonicalize (f32 (VOP3Mods f32:$src, i32:$src_mods))),
    (V_MAX_F32_e64 $src_mods, $src, $src_mods, $src)> {
    let OtherPredicates = f32_preds;
  }

  def : GCNPat<
    (fcanonicalize (f64 (VOP3Mods f64:$src, i32:$src_mods))),
    (V_MAX_F64_e64  $src_mods, $src, $src_mods, $src)> {
    let OtherPredicates = !listconcat(f64_preds, [isNotGFX12Plus]);
  }

  def : GCNPat<
    (fcanonicalize (f64 (VOP3Mods f64:$src, i32:$src_mods))),
    (V_MAX_NUM_F64_e64  $src_mods, $src, $src_mods, $src)> {
    let OtherPredicates = !listconcat(f64_preds, [isGFX12Plus]);
  }

  def : GCNPat<
    (fcanonicalize (f16 (VOP3Mods f16:$src, i32:$src_mods))),
    (V_MAX_F16_e64 $src_mods, $src, $src_mods, $src, 0, 0)> {
    let OtherPredicates = !listconcat(f16_preds, [Has16BitInsts, NotHasTrue16BitInsts]);
  }

  def : GCNPat<
    (fcanonicalize (f16 (VOP3Mods f16:$src, i32:$src_mods))),
    (V_MAX_F16_fake16_e64 $src_mods, $src, $src_mods, $src, 0, 0)> {
    let OtherPredicates = !listconcat(f16_preds, [Has16BitInsts, HasTrue16BitInsts]);
  }

  def : GCNPat<
    (fcanonicalize (v2f16 (VOP3PMods v2f16:$src, i32:$src_mods))),
    (V_PK_MAX_F16 $src_mods, $src, $src_mods, $src, DSTCLAMP.NONE)> {
    // FIXME: Should have VOP3P subtarget predicate
    let OtherPredicates = f16_preds;
  }
}

// On pre-gfx9 targets, v_max_*/v_min_* did not respect the denormal
// mode, and would never flush. For f64, it's faster to do implement
// this with a max. For f16/f32 it's a wash, but prefer max when
// valid.
//
// FIXME: Lowering f32/f16 with max is worse since we can use a
// smaller encoding if the input is fneg'd. It also adds an extra
// register use.
let SubtargetPredicate = HasMinMaxDenormModes in {
  defm : SelectCanonicalizeAsMax<[], [], []>;
} // End SubtargetPredicate = HasMinMaxDenormModes

let SubtargetPredicate = NotHasMinMaxDenormModes in {
  // Use the max lowering if we don't need to flush.

  // FIXME: We don't do use this for f32 as a workaround for the
  // library being compiled with the default ieee mode, but
  // potentially being called from flushing kernels. Really we should
  // not be mixing code expecting different default FP modes, but mul
  // works in any FP environment.
  defm : SelectCanonicalizeAsMax<[FalsePredicate], [FP64Denormals], [FP16Denormals]>;
} // End SubtargetPredicate = NotHasMinMaxDenormModes


let OtherPredicates = [HasDLInsts] in {
// Don't allow source modifiers. If there are any source modifiers then it's
// better to select fma instead of fmac.
def : GCNPat <
  (fma (f32 (VOP3NoMods f32:$src0)),
       (f32 (VOP3NoMods f32:$src1)),
       (f32 (VOP3NoMods f32:$src2))),
  (V_FMAC_F32_e64 SRCMODS.NONE, $src0, SRCMODS.NONE, $src1,
                  SRCMODS.NONE, $src2)
>;
} // End OtherPredicates = [HasDLInsts]

let SubtargetPredicate = isGFX10Plus in {
// Don't allow source modifiers. If there are any source modifiers then it's
// better to select fma instead of fmac.
let OtherPredicates = [NotHasTrue16BitInsts] in
def : GCNPat <
  (fma (f16 (VOP3NoMods f32:$src0)),
       (f16 (VOP3NoMods f32:$src1)),
       (f16 (VOP3NoMods f32:$src2))),
  (V_FMAC_F16_e64 SRCMODS.NONE, $src0, SRCMODS.NONE, $src1,
                  SRCMODS.NONE, $src2)
>;
let OtherPredicates = [HasTrue16BitInsts] in
def : GCNPat <
  (fma (f16 (VOP3NoMods f32:$src0)),
       (f16 (VOP3NoMods f32:$src1)),
       (f16 (VOP3NoMods f32:$src2))),
  (V_FMAC_F16_t16_e64 SRCMODS.NONE, $src0, SRCMODS.NONE, $src1,
                  SRCMODS.NONE, $src2)
>;
}

let OtherPredicates = [HasFmacF64Inst] in
// Don't allow source modifiers. If there are any source modifiers then it's
// better to select fma instead of fmac.
def : GCNPat <
  (fma (f64 (VOP3NoMods f64:$src0)),
       (f64 (VOP3NoMods f64:$src1)),
       (f64 (VOP3NoMods f64:$src2))),
  (V_FMAC_F64_e64 SRCMODS.NONE, $src0, SRCMODS.NONE, $src1,
                  SRCMODS.NONE, $src2)
>;

// COPY is workaround tablegen bug from multiple outputs
// from S_LSHL_B32's multiple outputs from implicit scc def.
let AddedComplexity = 1 in {
def : GCNPat <
  (v2i16 (UniformBinFrag<build_vector> (i16 0), (i16 SReg_32:$src1))),
  (S_LSHL_B32 SReg_32:$src1, (i16 16))
>;

def : GCNPat <
  (v2i16 (DivergentBinFrag<build_vector> (i16 0), (i16 VGPR_32:$src1))),
  (v2i16 (V_LSHLREV_B32_e64 (i16 16), VGPR_32:$src1))
>;


def : GCNPat <
  (v2i16 (UniformBinFrag<build_vector> (i16 SReg_32:$src1), (i16 0))),
  (S_AND_B32 (S_MOV_B32 (i32 0xffff)), SReg_32:$src1)
>;

def : GCNPat <
  (v2i16 (DivergentBinFrag<build_vector> (i16 VGPR_32:$src1), (i16 0))),
  (v2i16 (V_AND_B32_e64 (i32 (V_MOV_B32_e32 (i32 0xffff))), VGPR_32:$src1))
>;

def : GCNPat <
  (v2f16 (UniformBinFrag<build_vector> (f16 SReg_32:$src1), (f16 FP_ZERO))),
  (S_AND_B32 (S_MOV_B32 (i32 0xffff)), SReg_32:$src1)
>;

def : GCNPat <
  (v2f16 (DivergentBinFrag<build_vector> (f16 VGPR_32:$src1), (f16 FP_ZERO))),
  (v2f16 (V_AND_B32_e64 (i32 (V_MOV_B32_e32 (i32 0xffff))), VGPR_32:$src1))
>;

foreach vecTy = [v2i16, v2f16, v2bf16] in {

defvar Ty = vecTy.ElementType;

def : GCNPat <
  (vecTy (UniformBinFrag<build_vector> (Ty SReg_32:$src0), (Ty undef))),
  (COPY_TO_REGCLASS SReg_32:$src0, SReg_32)
>;

def : GCNPat <
  (vecTy (DivergentBinFrag<build_vector> (Ty VGPR_32:$src0), (Ty undef))),
  (COPY_TO_REGCLASS VGPR_32:$src0, VGPR_32)
>;

def : GCNPat <
  (vecTy (UniformBinFrag<build_vector> (Ty undef), (Ty SReg_32:$src1))),
  (S_LSHL_B32 SReg_32:$src1, (i32 16))
>;

def : GCNPat <
  (vecTy (DivergentBinFrag<build_vector> (Ty undef), (Ty VGPR_32:$src1))),
  (vecTy (V_LSHLREV_B32_e64 (i32 16), VGPR_32:$src1))
>;
} // End foreach Ty = ...
}

let SubtargetPredicate = HasVOP3PInsts in {
def : GCNPat <
  (v2i16 (DivergentBinFrag<build_vector> (i16 VGPR_32:$src0), (i16 VGPR_32:$src1))),
  (v2i16 (V_LSHL_OR_B32_e64 $src1, (i32 16), (i32 (V_AND_B32_e64 (i32 (V_MOV_B32_e32 (i32 0xffff))), $src0))))
>;

// With multiple uses of the shift, this will duplicate the shift and
// increase register pressure.
def : GCNPat <
  (v2i16 (UniformBinFrag<build_vector> (i16 SReg_32:$src0), (i16 (trunc (srl_oneuse SReg_32:$src1, (i32 16)))))),
  (v2i16 (S_PACK_LH_B32_B16 SReg_32:$src0, SReg_32:$src1))
>;

def : GCNPat <
  (v2i16 (UniformBinFrag<build_vector> (i16 (trunc (srl_oneuse SReg_32:$src0, (i32 16)))),
                       (i16 (trunc (srl_oneuse SReg_32:$src1, (i32 16)))))),
  (S_PACK_HH_B32_B16 SReg_32:$src0, SReg_32:$src1)
>;


foreach vecTy = [v2i16, v2f16, v2bf16] in {

defvar Ty = vecTy.ElementType;
defvar immzeroTy = !if(!eq(Ty, i16), immzero, fpimmzero);

def : GCNPat <
  (vecTy (UniformBinFrag<build_vector> (Ty SReg_32:$src0), (Ty SReg_32:$src1))),
  (S_PACK_LL_B32_B16 SReg_32:$src0, SReg_32:$src1)
>;

// Take the lower 16 bits from each VGPR_32 and concat them
def : GCNPat <
  (vecTy (DivergentBinFrag<build_vector> (Ty VGPR_32:$a), (Ty VGPR_32:$b))),
  (V_PERM_B32_e64 VGPR_32:$b, VGPR_32:$a, (S_MOV_B32 (i32 0x05040100)))
>;


// Take the lower 16 bits from V[0] and the upper 16 bits from V[1]
// Special case, can use V_BFI (0xffff literal likely more reusable than 0x70601000)
def : GCNPat <
  (vecTy (DivergentBinFrag<build_vector> (Ty (immzeroTy)),
    (Ty !if(!eq(Ty, i16),
      (Ty (trunc (srl VGPR_32:$b, (i32 16)))),
      (Ty (bitconvert (i16 (trunc (srl VGPR_32:$b, (i32 16)))))))))),
  (V_AND_B32_e64 (S_MOV_B32 (i32 0xffff0000)), VGPR_32:$b)
>;


// Take the lower 16 bits from V[0] and the upper 16 bits from V[1]
// Special case, can use V_BFI (0xffff literal likely more reusable than 0x70601000)
def : GCNPat <
  (vecTy (DivergentBinFrag<build_vector> (Ty VGPR_32:$a),
    (Ty !if(!eq(Ty, i16),
      (Ty (trunc (srl VGPR_32:$b, (i32 16)))),
      (Ty (bitconvert (i16 (trunc (srl VGPR_32:$b, (i32 16)))))))))),
  (V_BFI_B32_e64 (S_MOV_B32 (i32 0x0000ffff)),  VGPR_32:$a, VGPR_32:$b)
>;


// Take the upper 16 bits from V[0] and the lower 16 bits from V[1]
// Special case, can use V_ALIGNBIT (always uses encoded literal)
def : GCNPat <
  (vecTy (DivergentBinFrag<build_vector>
    (Ty !if(!eq(Ty, i16),
      (Ty (trunc (srl VGPR_32:$a, (i32 16)))),
      (Ty (bitconvert (i16 (trunc (srl VGPR_32:$a, (i32 16)))))))),
    (Ty VGPR_32:$b))),
    (V_ALIGNBIT_B32_e64 VGPR_32:$b, VGPR_32:$a, (i32 16))
>;

// Take the upper 16 bits from each VGPR_32 and concat them
def : GCNPat <
  (vecTy (DivergentBinFrag<build_vector>
    (Ty !if(!eq(Ty, i16),
      (Ty (trunc (srl VGPR_32:$a, (i32 16)))),
      (Ty (bitconvert (i16 (trunc (srl VGPR_32:$a, (i32 16)))))))),
    (Ty !if(!eq(Ty, i16),
      (Ty (trunc (srl VGPR_32:$b, (i32 16)))),
      (Ty (bitconvert (i16 (trunc (srl VGPR_32:$b, (i32 16)))))))))),
  (V_PERM_B32_e64 VGPR_32:$b, VGPR_32:$a, (S_MOV_B32 (i32 0x07060302)))
>;


} // end foreach Ty


let AddedComplexity = 5 in {
def : GCNPat <
  (v2f16 (is_canonicalized_2<build_vector> (f16 (VOP3Mods (f16 VGPR_32:$src0), i32:$src0_mods)),
                                           (f16 (VOP3Mods (f16 VGPR_32:$src1), i32:$src1_mods)))),
  (V_PACK_B32_F16_e64 $src0_mods, VGPR_32:$src0, $src1_mods, VGPR_32:$src1)
>;
}
} // End SubtargetPredicate = HasVOP3PInsts

// With multiple uses of the shift, this will duplicate the shift and
// increase register pressure.
let SubtargetPredicate = isGFX11Plus in
def : GCNPat <
  (v2i16 (build_vector (i16 (trunc (srl_oneuse SReg_32:$src0, (i32 16)))), (i16 SReg_32:$src1))),
  (v2i16 (S_PACK_HL_B32_B16 SReg_32:$src0, SReg_32:$src1))
>;


def : GCNPat <
  (v2f16 (scalar_to_vector f16:$src0)),
  (COPY $src0)
>;

def : GCNPat <
  (v2i16 (scalar_to_vector i16:$src0)),
  (COPY $src0)
>;

def : GCNPat <
  (v4i16 (scalar_to_vector i16:$src0)),
  (INSERT_SUBREG (IMPLICIT_DEF), $src0, sub0)
>;

def : GCNPat <
  (v4f16 (scalar_to_vector f16:$src0)),
  (INSERT_SUBREG (IMPLICIT_DEF), $src0, sub0)
>;

def : GCNPat <
  (i64 (int_amdgcn_mov_dpp i64:$src, timm:$dpp_ctrl, timm:$row_mask,
                           timm:$bank_mask, timm:$bound_ctrl)),
  (V_MOV_B64_DPP_PSEUDO VReg_64_Align2:$src, VReg_64_Align2:$src,
                        (as_i32timm $dpp_ctrl), (as_i32timm $row_mask),
                        (as_i32timm $bank_mask),
                        (as_i1timm $bound_ctrl))
>;

foreach vt = Reg64Types.types in {
def : GCNPat <
  (vt (int_amdgcn_update_dpp vt:$old, vt:$src, timm:$dpp_ctrl, timm:$row_mask,
                              timm:$bank_mask, timm:$bound_ctrl)),
  (V_MOV_B64_DPP_PSEUDO VReg_64_Align2:$old, VReg_64_Align2:$src, (as_i32timm $dpp_ctrl),
                        (as_i32timm $row_mask), (as_i32timm $bank_mask),
                        (as_i1timm $bound_ctrl))
>;
}

//===----------------------------------------------------------------------===//
// Fract Patterns
//===----------------------------------------------------------------------===//

let SubtargetPredicate = isGFX6 in {

// V_FRACT is buggy on SI, so the F32 version is never used and (x-floor(x)) is
// used instead. However, SI doesn't have V_FLOOR_F64, so the most efficient
// way to implement it is using V_FRACT_F64.
// The workaround for the V_FRACT bug is:
//    fract(x) = isnan(x) ? x : min(V_FRACT(x), 0.99999999999999999)

// Convert floor(x) to (x - fract(x))

// Don't bother handling this for GlobalISel, it's handled during
// lowering.
//
// FIXME: DAG should also custom lower this.
def : GCNPat <
  (f64 (ffloor (f64 (VOP3Mods f64:$x, i32:$mods)))),
  (V_ADD_F64_e64
      $mods,
      $x,
      SRCMODS.NEG,
      (V_CNDMASK_B64_PSEUDO
         (V_MIN_F64_e64
             SRCMODS.NONE,
             (V_FRACT_F64_e64 $mods, $x),
             SRCMODS.NONE,
             (V_MOV_B64_PSEUDO (i64 0x3fefffffffffffff))),
         $x,
         (V_CMP_CLASS_F64_e64 SRCMODS.NONE, $x, (i32 3 /*NaN*/))))
>;

} // End SubtargetPredicates = isGFX6

//============================================================================//
// Miscellaneous Optimization Patterns
//============================================================================//

// Undo sub x, c -> add x, -c canonicalization since c is more likely
// an inline immediate than -c.
// TODO: Also do for 64-bit.
def : GCNPat<
  (UniformBinFrag<add> i32:$src0, (i32 NegSubInlineConst32:$src1)),
  (S_SUB_I32 SReg_32:$src0, NegSubInlineConst32:$src1)
>;

def : GCNPat<
  (DivergentBinFrag<add> i32:$src0, (i32 NegSubInlineConst32:$src1)),
  (V_SUB_U32_e64 VS_32:$src0, NegSubInlineConst32:$src1)> {
  let SubtargetPredicate = HasAddNoCarryInsts;
}

def : GCNPat<
  (DivergentBinFrag<add> i32:$src0, (i32 NegSubInlineConst32:$src1)),
  (V_SUB_CO_U32_e64 VS_32:$src0, NegSubInlineConst32:$src1)> {
  let SubtargetPredicate = NotHasAddNoCarryInsts;
}


// Avoid pointlessly materializing a constant in VGPR.
// FIXME: Should also do this for readlane, but tablegen crashes on
// the ignored src1.
def : GCNPat<
  (i32 (int_amdgcn_readfirstlane (i32 imm:$src))),
  (S_MOV_B32 SReg_32:$src)
>;

multiclass BFMPatterns <ValueType vt, PatFrag SHL, PatFrag ADD, InstSI BFM> {
  def : GCNPat <
    (vt (SHL (vt (add (vt (shl 1, vt:$a)), -1)), vt:$b)),
    (BFM $a, $b)
  >;

  def : GCNPat <
    (vt (ADD (vt (shl 1, vt:$a)), -1)),
    (BFM $a, (i32 0))
  >;
}

defm : BFMPatterns <i32, UniformBinFrag<shl>, UniformBinFrag<add>, S_BFM_B32>;
// FIXME: defm : BFMPatterns <i64, UniformBinFrag<shl>, UniformBinFrag<add>, S_BFM_B64>;
defm : BFMPatterns <i32, DivergentBinFrag<shl>, DivergentBinFrag<add>, V_BFM_B32_e64>;

// Bitfield extract patterns

def IMMZeroBasedBitfieldMask : ImmLeaf <i32, [{
  return isMask_32(Imm);
}]>;

def IMMPopCount : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(llvm::popcount(N->getZExtValue()), SDLoc(N),
                                   MVT::i32);
}]>;

def : AMDGPUPat <
  (DivergentBinFrag<and> (i32 (srl i32:$src, i32:$rshift)),
                         IMMZeroBasedBitfieldMask:$mask),
  (V_BFE_U32_e64 $src, $rshift, (i32 (IMMPopCount $mask)))
>;

// x & ((1 << y) - 1)
def : AMDGPUPat <
  (DivergentBinFrag<and> i32:$src, (add_oneuse (shl_oneuse 1, i32:$width), -1)),
  (V_BFE_U32_e64 $src, (i32 0), $width)
>;

// x & ~(-1 << y)
def : AMDGPUPat <
  (DivergentBinFrag<and> i32:$src,
                         (xor_oneuse (shl_oneuse -1, i32:$width), -1)),
  (V_BFE_U32_e64 $src, (i32 0), $width)
>;

// x & (-1 >> (bitwidth - y))
def : AMDGPUPat <
  (DivergentBinFrag<and> i32:$src, (srl_oneuse -1, (sub 32, i32:$width))),
  (V_BFE_U32_e64 $src, (i32 0), $width)
>;

// x << (bitwidth - y) >> (bitwidth - y)
def : AMDGPUPat <
  (DivergentBinFrag<srl> (shl_oneuse i32:$src, (sub 32, i32:$width)),
                         (sub 32, i32:$width)),
  (V_BFE_U32_e64 $src, (i32 0), $width)
>;

def : AMDGPUPat <
  (DivergentBinFrag<sra> (shl_oneuse i32:$src, (sub 32, i32:$width)),
                         (sub 32, i32:$width)),
  (V_BFE_I32_e64 $src, (i32 0), $width)
>;

// SHA-256 Ma patterns

// ((x & z) | (y & (x | z))) -> BFI (XOR x, y), z, y
def : AMDGPUPatIgnoreCopies <
  (DivergentBinFrag<or> (and i32:$x, i32:$z),
                        (and i32:$y, (or i32:$x, i32:$z))),
  (V_BFI_B32_e64 (V_XOR_B32_e64 (COPY_TO_REGCLASS VSrc_b32:$x, VGPR_32),
                                (COPY_TO_REGCLASS VSrc_b32:$y, VGPR_32)),
                (COPY_TO_REGCLASS VSrc_b32:$z, VGPR_32),
                (COPY_TO_REGCLASS VSrc_b32:$y, VGPR_32))
>;

def : AMDGPUPatIgnoreCopies <
  (DivergentBinFrag<or> (and i64:$x, i64:$z),
                        (and i64:$y, (or i64:$x, i64:$z))),
  (REG_SEQUENCE VReg_64,
    (V_BFI_B32_e64 (V_XOR_B32_e64 (i32 (EXTRACT_SUBREG VReg_64:$x, sub0)),
                    (i32 (EXTRACT_SUBREG VReg_64:$y, sub0))),
              (i32 (EXTRACT_SUBREG VReg_64:$z, sub0)),
              (i32 (EXTRACT_SUBREG VReg_64:$y, sub0))), sub0,
    (V_BFI_B32_e64 (V_XOR_B32_e64 (i32 (EXTRACT_SUBREG VReg_64:$x, sub1)),
                    (i32 (EXTRACT_SUBREG VReg_64:$y, sub1))),
              (i32 (EXTRACT_SUBREG VReg_64:$z, sub1)),
              (i32 (EXTRACT_SUBREG VReg_64:$y, sub1))), sub1)
>;

multiclass IntMed3Pat<Instruction med3Inst,
                 SDPatternOperator min,
                 SDPatternOperator max> {

  // This matches 16 permutations of
  // min(max(a, b), max(min(a, b), c))
  def : AMDGPUPat <
  (min (max i32:$src0, i32:$src1),
       (max (min i32:$src0, i32:$src1), i32:$src2)),
  (med3Inst VSrc_b32:$src0, VSrc_b32:$src1, VSrc_b32:$src2)
>;

  // This matches 16 permutations of
  // max(min(x, y), min(max(x, y), z))
  def : AMDGPUPat <
  (max (min i32:$src0, i32:$src1),
       (min (max i32:$src0, i32:$src1), i32:$src2)),
  (med3Inst VSrc_b32:$src0, VSrc_b32:$src1, VSrc_b32:$src2)
>;
}

defm : IntMed3Pat<V_MED3_I32_e64, smin, smax>;
defm : IntMed3Pat<V_MED3_U32_e64, umin, umax>;

multiclass FPMed3Pat<ValueType vt,
                Instruction med3Inst> {
  // This matches 16 permutations of max(min(x, y), min(max(x, y), z))
  def : GCNPat<
    (fmaxnum_like_nnan
      (fminnum_like (VOP3Mods vt:$src0, i32:$src0_mods),
                    (VOP3Mods vt:$src1, i32:$src1_mods)),
      (fminnum_like (fmaxnum_like (VOP3Mods vt:$src0, i32:$src0_mods),
                                  (VOP3Mods vt:$src1, i32:$src1_mods)),
                    (vt (VOP3Mods vt:$src2, i32:$src2_mods)))),
    (med3Inst $src0_mods, $src0, $src1_mods, $src1, $src2_mods, $src2,
              DSTCLAMP.NONE, DSTOMOD.NONE)>;


  // This matches 16 permutations of min(max(x, y), max(min(x, y), z))
  def : GCNPat<
    (fminnum_like_nnan
      (fmaxnum_like (VOP3Mods vt:$src0, i32:$src0_mods),
                    (VOP3Mods vt:$src1, i32:$src1_mods)),
      (fmaxnum_like (fminnum_like (VOP3Mods vt:$src0, i32:$src0_mods),
                                  (VOP3Mods vt:$src1, i32:$src1_mods)),
                    (vt (VOP3Mods vt:$src2, i32:$src2_mods)))),
    (med3Inst $src0_mods, $src0, $src1_mods, $src1, $src2_mods, $src2,
              DSTCLAMP.NONE, DSTOMOD.NONE)>;
}

multiclass Int16Med3Pat<Instruction med3Inst,
                        SDPatternOperator min,
                        SDPatternOperator max> {
  // This matches 16 permutations of
  // max(min(x, y), min(max(x, y), z))
  def : GCNPat <
  (max (min i16:$src0, i16:$src1),
       (min (max i16:$src0, i16:$src1), i16:$src2)),
  (med3Inst SRCMODS.NONE, VSrc_b16:$src0, SRCMODS.NONE, VSrc_b16:$src1, SRCMODS.NONE, VSrc_b16:$src2, DSTCLAMP.NONE)
>;

  // This matches 16 permutations of
  // min(max(a, b), max(min(a, b), c))
  def : GCNPat <
  (min (max i16:$src0, i16:$src1),
       (max (min i16:$src0, i16:$src1), i16:$src2)),
  (med3Inst SRCMODS.NONE, VSrc_b16:$src0, SRCMODS.NONE, VSrc_b16:$src1, SRCMODS.NONE, VSrc_b16:$src2, DSTCLAMP.NONE)
>;
}

defm : FPMed3Pat<f32, V_MED3_F32_e64>;

let SubtargetPredicate = HasMed3_16 in {
defm : FPMed3Pat<f16, V_MED3_F16_e64>;
}

class
IntMinMaxPat<Instruction minmaxInst, SDPatternOperator min_or_max,
             SDPatternOperator max_or_min_oneuse> : AMDGPUPat <
  (DivergentBinFrag<min_or_max> (max_or_min_oneuse i32:$src0, i32:$src1),
                                i32:$src2),
  (minmaxInst VSrc_b32:$src0, VSrc_b32:$src1, VSrc_b32:$src2)
>;

class
FPMinMaxPat<Instruction minmaxInst, ValueType vt, SDPatternOperator min_or_max,
            SDPatternOperator max_or_min_oneuse> : GCNPat <
  (min_or_max (max_or_min_oneuse (VOP3Mods vt:$src0, i32:$src0_mods),
                                 (VOP3Mods vt:$src1, i32:$src1_mods)),
               (vt (VOP3Mods vt:$src2, i32:$src2_mods))),
  (minmaxInst $src0_mods, $src0, $src1_mods, $src1, $src2_mods, $src2,
              DSTCLAMP.NONE, DSTOMOD.NONE)
>;

class
FPMinCanonMaxPat<Instruction minmaxInst, ValueType vt, SDPatternOperator min_or_max,
                 SDPatternOperator max_or_min_oneuse> : GCNPat <
  (min_or_max (is_canonicalized_1<fcanonicalize>
                  (max_or_min_oneuse (VOP3Mods vt:$src0, i32:$src0_mods),
                                     (VOP3Mods vt:$src1, i32:$src1_mods))),
               (vt (VOP3Mods vt:$src2, i32:$src2_mods))),
  (minmaxInst $src0_mods, $src0, $src1_mods, $src1, $src2_mods, $src2,
              DSTCLAMP.NONE, DSTOMOD.NONE)
>;

let OtherPredicates = [isGFX11Plus] in {
def : IntMinMaxPat<V_MAXMIN_I32_e64, smin, smax_oneuse>;
def : IntMinMaxPat<V_MINMAX_I32_e64, smax, smin_oneuse>;
def : IntMinMaxPat<V_MAXMIN_U32_e64, umin, umax_oneuse>;
def : IntMinMaxPat<V_MINMAX_U32_e64, umax, umin_oneuse>;
def : FPMinMaxPat<V_MINMAX_F32_e64, f32, fmaxnum_like, fminnum_like_oneuse>;
def : FPMinMaxPat<V_MAXMIN_F32_e64, f32, fminnum_like, fmaxnum_like_oneuse>;
def : FPMinMaxPat<V_MINMAX_F16_e64, f16, fmaxnum_like, fminnum_like_oneuse>;
def : FPMinMaxPat<V_MAXMIN_F16_e64, f16, fminnum_like, fmaxnum_like_oneuse>;
def : FPMinCanonMaxPat<V_MINMAX_F32_e64, f32, fmaxnum_like, fminnum_like_oneuse>;
def : FPMinCanonMaxPat<V_MAXMIN_F32_e64, f32, fminnum_like, fmaxnum_like_oneuse>;
def : FPMinCanonMaxPat<V_MINMAX_F16_e64, f16, fmaxnum_like, fminnum_like_oneuse>;
def : FPMinCanonMaxPat<V_MAXMIN_F16_e64, f16, fminnum_like, fmaxnum_like_oneuse>;
}

let OtherPredicates = [isGFX9Plus] in {
defm : Int16Med3Pat<V_MED3_I16_e64, smin, smax>;
defm : Int16Med3Pat<V_MED3_U16_e64, umin, umax>;
} // End Predicates = [isGFX9Plus]

let OtherPredicates = [isGFX12Plus] in {
def : FPMinMaxPat<V_MINIMUMMAXIMUM_F32_e64, f32, DivergentBinFrag<fmaximum>, fminimum_oneuse>;
def : FPMinMaxPat<V_MAXIMUMMINIMUM_F32_e64, f32, DivergentBinFrag<fminimum>, fmaximum_oneuse>;
def : FPMinMaxPat<V_MINIMUMMAXIMUM_F16_e64, f16, DivergentBinFrag<fmaximum>, fminimum_oneuse>;
def : FPMinMaxPat<V_MAXIMUMMINIMUM_F16_e64, f16, DivergentBinFrag<fminimum>, fmaximum_oneuse>;
def : FPMinCanonMaxPat<V_MINIMUMMAXIMUM_F32_e64, f32, DivergentBinFrag<fmaximum>, fminimum_oneuse>;
def : FPMinCanonMaxPat<V_MAXIMUMMINIMUM_F32_e64, f32, DivergentBinFrag<fminimum>, fmaximum_oneuse>;
def : FPMinCanonMaxPat<V_MINIMUMMAXIMUM_F16_e64, f16, DivergentBinFrag<fmaximum>, fminimum_oneuse>;
def : FPMinCanonMaxPat<V_MAXIMUMMINIMUM_F16_e64, f16, DivergentBinFrag<fminimum>, fmaximum_oneuse>;
}

// Convert a floating-point power of 2 to the integer exponent.
def FPPow2ToExponentXForm : SDNodeXForm<fpimm, [{
  const auto &APF = N->getValueAPF();
  int Log2 = APF.getExactLog2Abs();
  assert(Log2 != INT_MIN);
  return CurDAG->getTargetConstant(Log2, SDLoc(N), MVT::i32);
}]>;

// Check if a floating point value is a power of 2 floating-point
// immediate where it's preferable to emit a multiply by as an
// ldexp. We skip over 0.5 to 4.0 as those are inline immediates
// anyway.
def fpimm_pos_pow2_prefer_ldexp_f64 : FPImmLeaf<f64, [{
    if (Imm.isNegative())
      return false;

    int Exp = Imm.getExactLog2Abs();
    // Prefer leaving the FP inline immediates as they are.
    // 0.5, 1.0, 2.0, 4.0

    // For f64 ldexp is always better than materializing a 64-bit
    // constant.
    return Exp != INT_MIN && (Exp < -1 || Exp > 2);
  }], FPPow2ToExponentXForm
>;

def fpimm_neg_pow2_prefer_ldexp_f64 : FPImmLeaf<f64, [{
    if (!Imm.isNegative())
      return false;
    int Exp = Imm.getExactLog2Abs();
    // Prefer leaving the FP inline immediates as they are.
    // 0.5, 1.0, 2.0, 4.0

    // For f64 ldexp is always better than materializing a 64-bit
    // constant.
    return Exp != INT_MIN && (Exp < -1 || Exp > 2);
  }], FPPow2ToExponentXForm
>;

// f64 is different because we also want to handle cases that may
// require materialization of the exponent.
// TODO: If we know f64 ops are fast, prefer add (ldexp x, N), y over fma
// TODO: For f32/f16, it's not a clear win on code size to use ldexp
// in place of mul since we have to use the vop3 form. Are there power
// savings or some other reason to prefer ldexp over mul?
def : GCNPat<
  (any_fmul (f64 (VOP3Mods f64:$src0, i32:$src0_mods)),
            fpimm_pos_pow2_prefer_ldexp_f64:$src1),
  (V_LDEXP_F64_e64 i32:$src0_mods, VSrc_b64:$src0,
                   0, (S_MOV_B32 (i32 (FPPow2ToExponentXForm $src1))))
>;

def : GCNPat<
  (any_fmul f64:$src0, fpimm_neg_pow2_prefer_ldexp_f64:$src1),
  (V_LDEXP_F64_e64 SRCMODS.NEG, VSrc_b64:$src0,
                   0, (S_MOV_B32 (i32 (FPPow2ToExponentXForm $src1))))
>;

// We want to avoid using VOP3Mods which could pull in another fneg
// which we would need to be re-negated (which should never happen in
// practice). I don't see a way to apply an SDNodeXForm that accounts
// for a second operand.
def : GCNPat<
  (any_fmul (fabs f64:$src0), fpimm_neg_pow2_prefer_ldexp_f64:$src1),
  (V_LDEXP_F64_e64 SRCMODS.NEG_ABS, VSrc_b64:$src0,
                   0, (S_MOV_B32 (i32 (FPPow2ToExponentXForm $src1))))
>;

class AMDGPUGenericInstruction : GenericInstruction {
  let Namespace = "AMDGPU";
}

// Convert a wave address to a swizzled vector address (i.e. this is
// for copying the stack pointer to a vector address appropriate to
// use in the offset field of mubuf instructions).
def G_AMDGPU_WAVE_ADDRESS : AMDGPUGenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src);
  let hasSideEffects = 0;
}

// Returns -1 if the input is zero.
def G_AMDGPU_FFBH_U32 : AMDGPUGenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src);
  let hasSideEffects = 0;
}

// Returns -1 if the input is zero.
def G_AMDGPU_FFBL_B32 : AMDGPUGenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src);
  let hasSideEffects = 0;
}

def G_AMDGPU_RCP_IFLAG : AMDGPUGenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src);
  let hasSideEffects = 0;
}

class BufferLoadGenericInstruction : AMDGPUGenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$rsrc, type2:$vindex, type2:$voffset,
                           type2:$soffset, untyped_imm_0:$offset,
                           untyped_imm_0:$cachepolicy, untyped_imm_0:$idxen);
  let hasSideEffects = 0;
  let mayLoad = 1;
}

class TBufferLoadGenericInstruction : AMDGPUGenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$rsrc, type2:$vindex, type2:$voffset,
                           type2:$soffset, untyped_imm_0:$offset, untyped_imm_0:$format,
                           untyped_imm_0:$cachepolicy, untyped_imm_0:$idxen);
  let hasSideEffects = 0;
  let mayLoad = 1;
}

def G_AMDGPU_BUFFER_LOAD_UBYTE : BufferLoadGenericInstruction;
def G_AMDGPU_BUFFER_LOAD_SBYTE : BufferLoadGenericInstruction;
def G_AMDGPU_BUFFER_LOAD_USHORT : BufferLoadGenericInstruction;
def G_AMDGPU_BUFFER_LOAD_SSHORT : BufferLoadGenericInstruction;
def G_AMDGPU_BUFFER_LOAD : BufferLoadGenericInstruction;
def G_AMDGPU_BUFFER_LOAD_UBYTE_TFE : BufferLoadGenericInstruction;
def G_AMDGPU_BUFFER_LOAD_SBYTE_TFE : BufferLoadGenericInstruction;
def G_AMDGPU_BUFFER_LOAD_USHORT_TFE : BufferLoadGenericInstruction;
def G_AMDGPU_BUFFER_LOAD_SSHORT_TFE : BufferLoadGenericInstruction;
def G_AMDGPU_BUFFER_LOAD_TFE : BufferLoadGenericInstruction;
def G_AMDGPU_BUFFER_LOAD_FORMAT : BufferLoadGenericInstruction;
def G_AMDGPU_BUFFER_LOAD_FORMAT_TFE : BufferLoadGenericInstruction;
def G_AMDGPU_BUFFER_LOAD_FORMAT_D16 : BufferLoadGenericInstruction;
def G_AMDGPU_TBUFFER_LOAD_FORMAT : TBufferLoadGenericInstruction;
def G_AMDGPU_TBUFFER_LOAD_FORMAT_D16 : TBufferLoadGenericInstruction;

class BufferStoreGenericInstruction : AMDGPUGenericInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins type0:$vdata, type1:$rsrc, type2:$vindex, type2:$voffset,
                           type2:$soffset, untyped_imm_0:$offset,
                           untyped_imm_0:$cachepolicy, untyped_imm_0:$idxen);
  let hasSideEffects = 0;
  let mayStore = 1;
}

class TBufferStoreGenericInstruction : AMDGPUGenericInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins type0:$vdata, type1:$rsrc, type2:$vindex, type2:$voffset,
                           type2:$soffset, untyped_imm_0:$offset,
                           untyped_imm_0:$format,
                           untyped_imm_0:$cachepolicy, untyped_imm_0:$idxen);
  let hasSideEffects = 0;
  let mayStore = 1;
}

def G_AMDGPU_BUFFER_STORE : BufferStoreGenericInstruction;
def G_AMDGPU_BUFFER_STORE_BYTE : BufferStoreGenericInstruction;
def G_AMDGPU_BUFFER_STORE_SHORT : BufferStoreGenericInstruction;
def G_AMDGPU_BUFFER_STORE_FORMAT : BufferStoreGenericInstruction;
def G_AMDGPU_BUFFER_STORE_FORMAT_D16 : BufferStoreGenericInstruction;
def G_AMDGPU_TBUFFER_STORE_FORMAT : TBufferStoreGenericInstruction;
def G_AMDGPU_TBUFFER_STORE_FORMAT_D16 : TBufferStoreGenericInstruction;

def G_AMDGPU_FMIN_LEGACY : AMDGPUGenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src0, type0:$src1);
  let hasSideEffects = 0;
}

def G_AMDGPU_FMAX_LEGACY : AMDGPUGenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src0, type0:$src1);
  let hasSideEffects = 0;
}

foreach N = 0-3 in {
def G_AMDGPU_CVT_F32_UBYTE#N : AMDGPUGenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src0);
  let hasSideEffects = 0;
}
}

def G_AMDGPU_CVT_PK_I16_I32 : AMDGPUGenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src0, type0:$src1);
  let hasSideEffects = 0;
}

def G_AMDGPU_SMED3 : AMDGPUGenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src0, type0:$src1, type0:$src2);
  let hasSideEffects = 0;
}

def G_AMDGPU_UMED3 : AMDGPUGenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src0, type0:$src1, type0:$src2);
  let hasSideEffects = 0;
}

def G_AMDGPU_FMED3 : AMDGPUGenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src0, type0:$src1, type0:$src2);
  let hasSideEffects = 0;
}

def G_AMDGPU_CLAMP : AMDGPUGenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src);
  let hasSideEffects = 0;
}

// Integer multiply-add: arg0 * arg1 + arg2.
//
// arg0 and arg1 are 32-bit integers (interpreted as signed or unsigned),
// arg2 is a 64-bit integer. Result is a 64-bit integer and a 1-bit carry-out.
class G_AMDGPU_MAD_64_32 : AMDGPUGenericInstruction {
  let OutOperandList = (outs type0:$dst, type1:$carry_out);
  let InOperandList = (ins type2:$arg0, type2:$arg1, type0:$arg2);
  let hasSideEffects = 0;
}

def G_AMDGPU_MAD_U64_U32 : G_AMDGPU_MAD_64_32;
def G_AMDGPU_MAD_I64_I32 : G_AMDGPU_MAD_64_32;

// Atomic cmpxchg. $cmpval ad $newval are packed in a single vector
// operand Expects a MachineMemOperand in addition to explicit
// operands.
def G_AMDGPU_ATOMIC_CMPXCHG : AMDGPUGenericInstruction {
  let OutOperandList = (outs type0:$oldval);
  let InOperandList = (ins ptype1:$addr, type0:$cmpval_newval);
  let hasSideEffects = 0;
  let mayLoad = 1;
  let mayStore = 1;
}

class BufferAtomicGenericInstruction : AMDGPUGenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$vdata, type1:$rsrc, type2:$vindex, type2:$voffset,
                           type2:$soffset, untyped_imm_0:$offset,
                           untyped_imm_0:$cachepolicy, untyped_imm_0:$idxen);
  let hasSideEffects = 0;
  let mayLoad = 1;
  let mayStore = 1;
}

def G_AMDGPU_BUFFER_ATOMIC_SWAP : BufferAtomicGenericInstruction;
def G_AMDGPU_BUFFER_ATOMIC_ADD : BufferAtomicGenericInstruction;
def G_AMDGPU_BUFFER_ATOMIC_SUB : BufferAtomicGenericInstruction;
def G_AMDGPU_BUFFER_ATOMIC_SMIN : BufferAtomicGenericInstruction;
def G_AMDGPU_BUFFER_ATOMIC_UMIN : BufferAtomicGenericInstruction;
def G_AMDGPU_BUFFER_ATOMIC_SMAX : BufferAtomicGenericInstruction;
def G_AMDGPU_BUFFER_ATOMIC_UMAX : BufferAtomicGenericInstruction;
def G_AMDGPU_BUFFER_ATOMIC_AND : BufferAtomicGenericInstruction;
def G_AMDGPU_BUFFER_ATOMIC_COND_SUB_U32 : BufferAtomicGenericInstruction;
def G_AMDGPU_BUFFER_ATOMIC_OR : BufferAtomicGenericInstruction;
def G_AMDGPU_BUFFER_ATOMIC_XOR : BufferAtomicGenericInstruction;
def G_AMDGPU_BUFFER_ATOMIC_INC : BufferAtomicGenericInstruction;
def G_AMDGPU_BUFFER_ATOMIC_DEC : BufferAtomicGenericInstruction;
def G_AMDGPU_BUFFER_ATOMIC_FADD : BufferAtomicGenericInstruction;
def G_AMDGPU_BUFFER_ATOMIC_FMIN : BufferAtomicGenericInstruction;
def G_AMDGPU_BUFFER_ATOMIC_FMAX : BufferAtomicGenericInstruction;

def G_AMDGPU_BUFFER_ATOMIC_CMPSWAP : AMDGPUGenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$vdata, type0:$cmp, type1:$rsrc, type2:$vindex,
                           type2:$voffset, type2:$soffset, untyped_imm_0:$offset,
                           untyped_imm_0:$cachepolicy, untyped_imm_0:$idxen);
  let hasSideEffects = 0;
  let mayLoad = 1;
  let mayStore = 1;
}

// Wrapper around llvm.amdgcn.s.buffer.load. This is mostly needed as
// a workaround for the intrinsic being defined as readnone, but
// really needs a memory operand.

class SBufferLoadInstruction : AMDGPUGenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$rsrc, type2:$offset, untyped_imm_0:$cachepolicy);
  let hasSideEffects = 0;
  let mayLoad = 1;
  let mayStore = 0;
}

def G_AMDGPU_S_BUFFER_LOAD : SBufferLoadInstruction;
def G_AMDGPU_S_BUFFER_LOAD_SBYTE : SBufferLoadInstruction;
def G_AMDGPU_S_BUFFER_LOAD_UBYTE : SBufferLoadInstruction;
def G_AMDGPU_S_BUFFER_LOAD_SSHORT : SBufferLoadInstruction;
def G_AMDGPU_S_BUFFER_LOAD_USHORT : SBufferLoadInstruction;

def G_AMDGPU_S_MUL_U64_U32 : AMDGPUGenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src0, type0:$src1);
  let hasSideEffects = 0;
}

def G_AMDGPU_S_MUL_I64_I32 : AMDGPUGenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src0, type0:$src1);
  let hasSideEffects = 0;
}

// This is equivalent to the G_INTRINSIC*, but the operands may have
// been legalized depending on the subtarget requirements.
def G_AMDGPU_INTRIN_IMAGE_LOAD : AMDGPUGenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins unknown:$intrin, variable_ops);
  let hasSideEffects = 0;
  let mayLoad = 1;

  // FIXME: Use separate opcode for atomics.
  let mayStore = 1;
}

def G_AMDGPU_INTRIN_IMAGE_LOAD_D16 : AMDGPUGenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins unknown:$intrin, variable_ops);
  let hasSideEffects = 0;
  let mayLoad = 1;

  // FIXME: Use separate opcode for atomics.
  let mayStore = 1;
}

def G_AMDGPU_INTRIN_IMAGE_LOAD_NORET : AMDGPUGenericInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins unknown:$intrin, variable_ops);
  let hasSideEffects = 0;
  let mayLoad = 1;
  let mayStore = 1;
}

// This is equivalent to the G_INTRINSIC*, but the operands may have
// been legalized depending on the subtarget requirements.
def G_AMDGPU_INTRIN_IMAGE_STORE : AMDGPUGenericInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins unknown:$intrin, variable_ops);
  let hasSideEffects = 0;
  let mayStore = 1;
}

def G_AMDGPU_INTRIN_IMAGE_STORE_D16 : AMDGPUGenericInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins unknown:$intrin, variable_ops);
  let hasSideEffects = 0;
  let mayStore = 1;
}

def G_AMDGPU_INTRIN_BVH_INTERSECT_RAY : AMDGPUGenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins unknown:$intrin, variable_ops);
  let hasSideEffects = 0;
  let mayLoad = 1;
  let mayStore = 0;
}

// Generic instruction for SI_CALL, so we can select the register bank and insert a waterfall loop
// if necessary.
def G_SI_CALL : AMDGPUGenericInstruction {
  let OutOperandList = (outs SReg_64:$dst);
  let InOperandList = (ins type0:$src0, unknown:$callee);
  let Size = 4;
  let isCall = 1;
  let UseNamedOperandTable = 1;
  let SchedRW = [WriteBranch];
  // TODO: Should really base this on the call target
  let isConvergent = 1;
}

def G_FPTRUNC_ROUND_UPWARD : AMDGPUGenericInstruction {
  let OutOperandList = (outs type0:$vdst);
  let InOperandList = (ins type1:$src0);
  let hasSideEffects = 0;
}

def G_FPTRUNC_ROUND_DOWNWARD : AMDGPUGenericInstruction {
  let OutOperandList = (outs type0:$vdst);
  let InOperandList = (ins type1:$src0);
  let hasSideEffects = 0;
}

//============================================================================//
// Dummy Instructions
//============================================================================//

def V_ILLEGAL : Enc32, InstSI<(outs), (ins), "v_illegal"> {
  let Inst{31-0} = 0x00000000;
  let FixedSize = 1;
  let Size = 4;
  let Uses = [EXEC];
  let hasSideEffects = 1;
  let SubtargetPredicate = isGFX10Plus;
}

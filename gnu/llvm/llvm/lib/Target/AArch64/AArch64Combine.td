//=- AArch64Combine.td - Define AArch64 Combine Rules ---------*-tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

include "llvm/Target/GlobalISel/Combine.td"

def fconstant_to_constant : GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_FCONSTANT):$root,
         [{ return matchFConstantToConstant(*${root}, MRI); }]),
  (apply [{ applyFConstantToConstant(*${root}); }])>;

def icmp_redundant_trunc_matchdata : GIDefMatchData<"Register">;
def icmp_redundant_trunc : GICombineRule<
  (defs root:$root, icmp_redundant_trunc_matchdata:$matchinfo),
  (match (wip_match_opcode G_ICMP):$root,
         [{ return matchICmpRedundantTrunc(*${root}, MRI, Helper.getKnownBits(), ${matchinfo}); }]),
  (apply [{ applyICmpRedundantTrunc(*${root}, MRI, B, Observer, ${matchinfo}); }])>;

// AArch64-specific offset folding for G_GLOBAL_VALUE.
def fold_global_offset_matchdata : GIDefMatchData<"std::pair<uint64_t, uint64_t>">;
def fold_global_offset : GICombineRule<
  (defs root:$root, fold_global_offset_matchdata:$matchinfo),
  (match (wip_match_opcode G_GLOBAL_VALUE):$root,
          [{ return matchFoldGlobalOffset(*${root}, MRI, ${matchinfo}); }]),
  (apply [{ applyFoldGlobalOffset(*${root}, MRI, B, Observer, ${matchinfo});}])
>;

// Boolean: 0 = G_ZEXT, 1 = G_SEXT
def ext_addv_to_udot_addv_matchinfo : GIDefMatchData<"std::tuple<Register, Register, bool>">;
let Predicates = [HasDotProd] in {
def ext_addv_to_udot_addv : GICombineRule<
  (defs root:$root, ext_addv_to_udot_addv_matchinfo:$matchinfo),
  (match (wip_match_opcode G_VECREDUCE_ADD):$root,
         [{ return matchExtAddvToUdotAddv(*${root}, MRI, STI, ${matchinfo}); }]),
  (apply [{ applyExtAddvToUdotAddv(*${root}, MRI, B, Observer, STI, ${matchinfo}); }])
>;
}

def ext_uaddv_to_uaddlv_matchinfo : GIDefMatchData<"std::pair<Register, bool>">;
def ext_uaddv_to_uaddlv : GICombineRule<
  (defs root:$root, ext_uaddv_to_uaddlv_matchinfo:$matchinfo),
  (match (wip_match_opcode G_VECREDUCE_ADD):$root,
         [{ return matchExtUaddvToUaddlv(*${root}, MRI, ${matchinfo}); }]),
  (apply [{ applyExtUaddvToUaddlv(*${root}, MRI, B, Observer, ${matchinfo}); }])
>;

class push_opcode_through_ext<Instruction opcode, Instruction extOpcode> : GICombineRule <
  (defs root:$root),
  (match (extOpcode $ext1, $src1):$ExtMI,
         (extOpcode $ext2, $src2),
         (opcode $dst, $ext1, $ext2):$root,
         [{ return matchPushAddSubExt(*${root}, MRI, ${dst}.getReg(), ${src1}.getReg(), ${src2}.getReg()); }]),
  (apply [{ applyPushAddSubExt(*${root}, MRI, B, ${ExtMI}->getOpcode() == TargetOpcode::G_SEXT, ${dst}.getReg(), ${src1}.getReg(), ${src2}.getReg()); }])>;

def push_sub_through_zext : push_opcode_through_ext<G_SUB, G_ZEXT>;
def push_add_through_zext : push_opcode_through_ext<G_ADD, G_ZEXT>;
def push_sub_through_sext : push_opcode_through_ext<G_SUB, G_SEXT>;
def push_add_through_sext : push_opcode_through_ext<G_ADD, G_SEXT>;

def AArch64PreLegalizerCombiner: GICombiner<
  "AArch64PreLegalizerCombinerImpl", [all_combines,
                                      fconstant_to_constant,
                                      icmp_redundant_trunc,
                                      fold_global_offset,
                                      shuffle_to_extract,
                                      ext_addv_to_udot_addv,
                                      ext_uaddv_to_uaddlv,
                                      push_sub_through_zext,
                                      push_add_through_zext,
                                      push_sub_through_sext,
                                      push_add_through_sext]> {
  let CombineAllMethodName = "tryCombineAllImpl";
}

def AArch64O0PreLegalizerCombiner: GICombiner<
  "AArch64O0PreLegalizerCombinerImpl", [optnone_combines]> {
  let CombineAllMethodName = "tryCombineAllImpl";
}

// Matchdata for combines which replace a G_SHUFFLE_VECTOR with a
// target-specific opcode.
def shuffle_matchdata : GIDefMatchData<"ShuffleVectorPseudo">;

def rev : GICombineRule<
  (defs root:$root, shuffle_matchdata:$matchinfo),
  (match (wip_match_opcode G_SHUFFLE_VECTOR):$root,
         [{ return matchREV(*${root}, MRI, ${matchinfo}); }]),
  (apply [{ applyShuffleVectorPseudo(*${root}, ${matchinfo}); }])
>;

def zip : GICombineRule<
  (defs root:$root, shuffle_matchdata:$matchinfo),
  (match (wip_match_opcode G_SHUFFLE_VECTOR):$root,
         [{ return matchZip(*${root}, MRI, ${matchinfo}); }]),
  (apply [{ applyShuffleVectorPseudo(*${root}, ${matchinfo}); }])
>;

def uzp : GICombineRule<
  (defs root:$root, shuffle_matchdata:$matchinfo),
  (match (wip_match_opcode G_SHUFFLE_VECTOR):$root,
         [{ return matchUZP(*${root}, MRI, ${matchinfo}); }]),
  (apply [{ applyShuffleVectorPseudo(*${root}, ${matchinfo}); }])
>;

def dup: GICombineRule <
  (defs root:$root, shuffle_matchdata:$matchinfo),
  (match (wip_match_opcode G_SHUFFLE_VECTOR):$root,
         [{ return matchDup(*${root}, MRI, ${matchinfo}); }]),
  (apply [{ applyShuffleVectorPseudo(*${root}, ${matchinfo}); }])
>;

def trn : GICombineRule<
  (defs root:$root, shuffle_matchdata:$matchinfo),
  (match (wip_match_opcode G_SHUFFLE_VECTOR):$root,
         [{ return matchTRN(*${root}, MRI, ${matchinfo}); }]),
  (apply [{ applyShuffleVectorPseudo(*${root}, ${matchinfo}); }])
>;

def ext: GICombineRule <
  (defs root:$root, shuffle_matchdata:$matchinfo),
  (match (wip_match_opcode G_SHUFFLE_VECTOR):$root,
         [{ return matchEXT(*${root}, MRI, ${matchinfo}); }]),
  (apply [{ applyEXT(*${root}, ${matchinfo}); }])
>;

def insertelt_nonconst: GICombineRule <
  (defs root:$root, shuffle_matchdata:$matchinfo),
  (match (wip_match_opcode G_INSERT_VECTOR_ELT):$root,
         [{ return matchNonConstInsert(*${root}, MRI); }]),
  (apply [{ applyNonConstInsert(*${root}, MRI, B); }])
>;

def shuf_to_ins_matchdata : GIDefMatchData<"std::tuple<Register, int, Register, int>">;
def shuf_to_ins: GICombineRule <
  (defs root:$root, shuf_to_ins_matchdata:$matchinfo),
  (match (wip_match_opcode G_SHUFFLE_VECTOR):$root,
         [{ return matchINS(*${root}, MRI, ${matchinfo}); }]),
  (apply [{ applyINS(*${root}, MRI, B, ${matchinfo}); }])
>;

def vashr_vlshr_imm_matchdata : GIDefMatchData<"int64_t">;
def vashr_vlshr_imm : GICombineRule<
  (defs root:$root, vashr_vlshr_imm_matchdata:$matchinfo),
  (match (wip_match_opcode G_ASHR, G_LSHR):$root,
          [{ return matchVAshrLshrImm(*${root}, MRI, ${matchinfo}); }]),
  (apply [{ applyVAshrLshrImm(*${root}, MRI, ${matchinfo}); }])
>;

def form_duplane_matchdata :
  GIDefMatchData<"std::pair<unsigned, int>">;
def form_duplane : GICombineRule <
  (defs root:$root, form_duplane_matchdata:$matchinfo),
  (match (wip_match_opcode G_SHUFFLE_VECTOR):$root,
          [{ return matchDupLane(*${root}, MRI, ${matchinfo}); }]),
  (apply [{ applyDupLane(*${root}, MRI, B, ${matchinfo}); }])
>;

def shuffle_vector_lowering : GICombineGroup<[dup, rev, ext, zip, uzp, trn,
                                              form_duplane, shuf_to_ins]>;

// Turn G_UNMERGE_VALUES -> G_EXTRACT_VECTOR_ELT's
def vector_unmerge_lowering : GICombineRule <
  (defs root:$root),
  (match (wip_match_opcode G_UNMERGE_VALUES):$root,
          [{ return matchScalarizeVectorUnmerge(*${root}, MRI); }]),
  (apply [{ applyScalarizeVectorUnmerge(*${root}, MRI, B); }])
>;

def adjust_icmp_imm_matchdata :
  GIDefMatchData<"std::pair<uint64_t, CmpInst::Predicate>">;
def adjust_icmp_imm : GICombineRule <
  (defs root:$root, adjust_icmp_imm_matchdata:$matchinfo),
  (match (wip_match_opcode G_ICMP):$root,
          [{ return matchAdjustICmpImmAndPred(*${root}, MRI, ${matchinfo}); }]),
  (apply [{ applyAdjustICmpImmAndPred(*${root}, ${matchinfo}, B, Observer); }])
>;

def swap_icmp_operands : GICombineRule <
  (defs root:$root),
  (match (wip_match_opcode G_ICMP):$root,
          [{ return trySwapICmpOperands(*${root}, MRI); }]),
  (apply [{ applySwapICmpOperands(*${root}, Observer); }])
>;

def icmp_lowering : GICombineGroup<[adjust_icmp_imm, swap_icmp_operands]>;

def extractvecelt_pairwise_add_matchdata : GIDefMatchData<"std::tuple<unsigned, LLT, Register>">;
def extractvecelt_pairwise_add : GICombineRule<
  (defs root:$root, extractvecelt_pairwise_add_matchdata:$matchinfo),
  (match (wip_match_opcode G_EXTRACT_VECTOR_ELT):$root,
          [{ return matchExtractVecEltPairwiseAdd(*${root}, MRI, ${matchinfo}); }]),
  (apply [{ applyExtractVecEltPairwiseAdd(*${root}, MRI, B, ${matchinfo}); }])
>;

def mul_const_matchdata : GIDefMatchData<"std::function<void(MachineIRBuilder&, Register)>">;
def mul_const : GICombineRule<
  (defs root:$root, mul_const_matchdata:$matchinfo),
  (match (wip_match_opcode G_MUL):$root,
          [{ return matchAArch64MulConstCombine(*${root}, MRI, ${matchinfo}); }]),
  (apply [{ applyAArch64MulConstCombine(*${root}, MRI, B, ${matchinfo}); }])
>;

def lower_mull : GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_MUL):$root,
          [{ return matchExtMulToMULL(*${root}, MRI); }]),
  (apply [{ applyExtMulToMULL(*${root}, MRI, B, Observer); }])
>;

def build_vector_to_dup : GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_BUILD_VECTOR):$root,
          [{ return matchBuildVectorToDup(*${root}, MRI); }]),
  (apply [{ applyBuildVectorToDup(*${root}, MRI, B); }])
>;

def build_vector_lowering : GICombineGroup<[build_vector_to_dup]>;

def lower_vector_fcmp : GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_FCMP):$root,
    [{ return matchLowerVectorFCMP(*${root}, MRI, B); }]),
  (apply [{ applyLowerVectorFCMP(*${root}, MRI, B); }])>;

def form_truncstore_matchdata : GIDefMatchData<"Register">;
def form_truncstore : GICombineRule<
  (defs root:$root, form_truncstore_matchdata:$matchinfo),
  (match (wip_match_opcode G_STORE):$root,
          [{ return matchFormTruncstore(*${root}, MRI, ${matchinfo}); }]),
  (apply [{ applyFormTruncstore(*${root}, MRI, B, Observer, ${matchinfo}); }])
>;

def fold_merge_to_zext : GICombineRule<
  (defs root:$d),
  (match (wip_match_opcode G_MERGE_VALUES):$d,
          [{ return matchFoldMergeToZext(*${d}, MRI); }]),
  (apply [{ applyFoldMergeToZext(*${d}, MRI, B, Observer); }])
>;

def mutate_anyext_to_zext : GICombineRule<
  (defs root:$d),
  (match (wip_match_opcode G_ANYEXT):$d,
          [{ return matchMutateAnyExtToZExt(*${d}, MRI); }]),
  (apply [{ applyMutateAnyExtToZExt(*${d}, MRI, B, Observer); }])
>;

def split_store_zero_128 : GICombineRule<
  (defs root:$d),
  (match (wip_match_opcode G_STORE):$d,
          [{ return matchSplitStoreZero128(*${d}, MRI); }]),
  (apply [{ applySplitStoreZero128(*${d}, MRI, B, Observer); }])
>;

def vector_sext_inreg_to_shift : GICombineRule<
  (defs root:$d),
  (match (wip_match_opcode G_SEXT_INREG):$d,
          [{ return matchVectorSextInReg(*${d}, MRI); }]),
  (apply [{ applyVectorSextInReg(*${d}, MRI, B, Observer); }])
>;

def unmerge_ext_to_unmerge_matchdata : GIDefMatchData<"Register">;
def unmerge_ext_to_unmerge : GICombineRule<
  (defs root:$d, unmerge_ext_to_unmerge_matchdata:$matchinfo),
  (match (wip_match_opcode G_UNMERGE_VALUES):$d,
          [{ return matchUnmergeExtToUnmerge(*${d}, MRI, ${matchinfo}); }]),
  (apply [{ applyUnmergeExtToUnmerge(*${d}, MRI, B, Observer, ${matchinfo}); }])
>;

def regtriple_matchdata : GIDefMatchData<"std::tuple<Register, Register, Register>">;
def or_to_bsp: GICombineRule <
  (defs root:$root, regtriple_matchdata:$matchinfo),
  (match (wip_match_opcode G_OR):$root,
         [{ return matchOrToBSP(*${root}, MRI, ${matchinfo}); }]),
  (apply [{ applyOrToBSP(*${root}, MRI, B, ${matchinfo}); }])
>;

// Combines Mul(And(Srl(X, 15), 0x10001), 0xffff) into CMLTz
def combine_mul_cmlt : GICombineRule<
  (defs root:$root, register_matchinfo:$matchinfo),
  (match (wip_match_opcode G_MUL):$root,
        [{ return matchCombineMulCMLT(*${root}, MRI, ${matchinfo}); }]),
  (apply [{ applyCombineMulCMLT(*${root}, MRI, B, ${matchinfo}); }])
>;

// Post-legalization combines which should happen at all optimization levels.
// (E.g. ones that facilitate matching for the selector) For example, matching
// pseudos.
def AArch64PostLegalizerLowering
    : GICombiner<"AArch64PostLegalizerLoweringImpl",
                       [shuffle_vector_lowering, vashr_vlshr_imm,
                        icmp_lowering, build_vector_lowering,
                        lower_vector_fcmp, form_truncstore,
                        vector_sext_inreg_to_shift,
                        unmerge_ext_to_unmerge, lower_mull,
                        vector_unmerge_lowering, insertelt_nonconst]> {
}

// Post-legalization combines which are primarily optimizations.
def AArch64PostLegalizerCombiner
    : GICombiner<"AArch64PostLegalizerCombinerImpl",
                       [copy_prop, combines_for_extload,
                        combine_indexed_load_store,
                        sext_trunc_sextload, mutate_anyext_to_zext,
                        hoist_logic_op_with_same_opcode_hands,
                        redundant_and, xor_of_and_with_same_reg,
                        extractvecelt_pairwise_add, redundant_or,
                        mul_const, redundant_sext_inreg,
                        form_bitfield_extract, rotate_out_of_range,
                        icmp_to_true_false_known_bits, merge_unmerge,
                        select_combines, fold_merge_to_zext,
                        constant_fold_binops, identity_combines,
                        ptr_add_immed_chain, overlapping_and,
                        split_store_zero_128, undef_combines,
                        select_to_minmax, or_to_bsp, combine_concat_vector,
                        commute_constant_to_rhs,
                        push_freeze_to_prevent_poison_from_propagating,
                        combine_mul_cmlt]> {
}

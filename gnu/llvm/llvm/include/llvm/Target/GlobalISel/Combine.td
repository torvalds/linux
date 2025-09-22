//===- Combine.td - Combine rule definitions ---------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declare GlobalISel combine rules and provide mechanisms to opt-out.
//
//===----------------------------------------------------------------------===//


//===----------------------------------------------------------------------===//
// Base Classes
//
// These are the core classes that the combiner backend relies on.
//===----------------------------------------------------------------------===//

/// All arguments of the defs operator must be subclasses of GIDefKind or
/// sub-dags whose operator is GIDefKindWithArgs.
class GIDefKind;
class GIDefKindWithArgs;

/// Declare a root node. There must be at least one of these in every combine
/// rule.
def root : GIDefKind;

def defs;

def pattern;
def match;
def apply;

def wip_match_opcode;

// Common base class for GICombineRule and GICombineGroup.
class GICombine {
  // See GICombineGroup. We only declare it here to make the tablegen pass
  // simpler.
  list<GICombine> Rules = ?;
}

// A group of combine rules that can be added to a GICombiner or another group.
class GICombineGroup<list<GICombine> rules> : GICombine {
  // The rules contained in this group. The rules in a group are flattened into
  // a single list and sorted into whatever order is most efficient. However,
  // they will never be re-ordered such that behaviour differs from the
  // specified order. It is therefore possible to use the order of rules in this
  // list to describe priorities.
  let Rules = rules;
}

// Declares a combiner implementation class
class GICombiner<string classname, list<GICombine> rules>
    : GICombineGroup<rules> {
  // The class name to use in the generated output.
  string Classname = classname;
  // Combiners can use this so they're free to define tryCombineAll themselves
  // and do extra work before/after calling the TableGen-erated code.
  string CombineAllMethodName = "tryCombineAll";
}

/// Declares data that is passed from the match stage to the apply stage.
class GIDefMatchData<string type>  {
  /// A C++ type name indicating the storage type.
  string Type = type;
}

class GICombineRule<dag defs, dag match, dag apply> : GICombine {
  /// Defines the external interface of the match rule. This includes:
  /// * The names of the root nodes (requires at least one)
  /// See GIDefKind for details.
  dag Defs = defs;

  /// Defines the things which must be true for the pattern to match
  dag Match = match;

  /// Defines the things which happen after the decision is made to apply a
  /// combine rule.
  dag Apply = apply;

  /// Defines the predicates that are checked before the match function
  /// is called. Targets can use this to, for instance, check Subtarget
  /// features.
  list<Predicate> Predicates = [];

  // Maximum number of permutations of this rule that can be emitted.
  // Set to -1 to disable the limit.
  int MaxPermutations = 16;
}

def gi_mo;
def gi_imm;

// This is an equivalent of PatFrags but for MIR Patterns.
//
// GICombinePatFrags can be used in place of instructions for 'match' patterns.
// Much like normal instructions, the defs (outs) come first, and the ins second
//
// Out operands can only be of type "root" or "gi_mo", and they must be defined
// by an instruction pattern in all alternatives.
//
// In operands can be gi_imm or gi_mo. They cannot be redefined in any alternative
// pattern and may only appear in the C++ code, or in the output operand of an
// instruction pattern.
class GICombinePatFrag<dag outs, dag ins, list<dag> alts> {
  dag InOperands = ins;
  dag OutOperands = outs;
  list<dag> Alternatives = alts;
}

//===----------------------------------------------------------------------===//
// Pattern Special Types
//===----------------------------------------------------------------------===//

class GISpecialType;

// In an apply pattern, GITypeOf can be used to set the type of a new temporary
// register to match the type of a matched register.
//
// This can only be used on temporary registers defined by the apply pattern.
//
// TODO: Make this work in matchers as well?
//
// FIXME: Syntax is very ugly.
class GITypeOf<string opName> : GISpecialType {
  string OpName = opName;
}

//===----------------------------------------------------------------------===//
// Pattern Builtins
//===----------------------------------------------------------------------===//

// "Magic" Builtin instructions for MIR patterns.
// The definitions that implement
class GIBuiltinInst;

// Replace all references to a register with another one.
//
// Usage:
//    (apply (GIReplaceReg $old, $new))
//
// Operands:
// - $old (out) register defined by a matched instruction
// - $new (in)  register
//
// Semantics:
// - Can only appear in an 'apply' pattern.
// - If both old/new are operands of matched instructions,
//   "canReplaceReg" is checked before applying the rule.
def GIReplaceReg : GIBuiltinInst;

// Apply action that erases the match root.
//
// Usage:
//    (apply (GIEraseRoot))
//
// Semantics:
// - Can only appear as the only pattern of an 'apply' pattern list.
// - The root cannot have any output operands.
// - The root must be a CodeGenInstruction
//
// TODO: Allow using this directly, like (apply GIEraseRoot)
def GIEraseRoot : GIBuiltinInst;

//===----------------------------------------------------------------------===//
// Pattern MIFlags
//===----------------------------------------------------------------------===//

class MIFlagEnum<string enumName> {
  string EnumName = "MachineInstr::" # enumName;
}

def FmNoNans    : MIFlagEnum<"FmNoNans">;
def FmNoInfs    : MIFlagEnum<"FmNoInfs">;
def FmNsz       : MIFlagEnum<"FmNsz">;
def FmArcp      : MIFlagEnum<"FmArcp">;
def FmContract  : MIFlagEnum<"FmContract">;
def FmAfn       : MIFlagEnum<"FmAfn">;
def FmReassoc   : MIFlagEnum<"FmReassoc">;
def IsExact     : MIFlagEnum<"IsExact">;
def NoSWrap     : MIFlagEnum<"NoSWrap">;
def NoUWrap     : MIFlagEnum<"NoUWrap">;
def NonNeg      : MIFlagEnum<"NonNeg">;

def MIFlags;
// def not; -> Already defined as a SDNode

//===----------------------------------------------------------------------===//

def extending_load_matchdata : GIDefMatchData<"PreferredTuple">;
def indexed_load_store_matchdata : GIDefMatchData<"IndexedLoadStoreMatchInfo">;
def instruction_steps_matchdata: GIDefMatchData<"InstructionStepsMatchInfo">;

def register_matchinfo: GIDefMatchData<"Register">;
def int64_matchinfo: GIDefMatchData<"int64_t">;
def apint_matchinfo : GIDefMatchData<"APInt">;
def constantfp_matchinfo : GIDefMatchData<"ConstantFP*">;
def build_fn_matchinfo :
GIDefMatchData<"std::function<void(MachineIRBuilder &)>">;
def unsigned_matchinfo: GIDefMatchData<"unsigned">;

def copy_prop : GICombineRule<
  (defs root:$d),
  (match (COPY $d, $s):$mi,
         [{ return Helper.matchCombineCopy(*${mi}); }]),
  (apply [{ Helper.applyCombineCopy(*${mi}); }])>;

// idempotent operations
// Fold (freeze (freeze x)) -> (freeze x).
// Fold (fabs (fabs x)) -> (fabs x).
// Fold (fcanonicalize (fcanonicalize x)) -> (fcanonicalize x).
def idempotent_prop_frags : GICombinePatFrag<
  (outs root:$dst, $src), (ins),
  !foreach(op, [G_FREEZE, G_FABS, G_FCANONICALIZE],
           (pattern (op $dst, $src), (op $src, $x)))>;

def idempotent_prop : GICombineRule<
   (defs root:$dst),
   (match (idempotent_prop_frags $dst, $src)),
   (apply (GIReplaceReg $dst, $src))>;

// Convert freeze(Op(Op0, NonPoisonOps...)) to Op(freeze(Op0), NonPoisonOps...)
// when Op0 is not guaranteed non-poison
def push_freeze_to_prevent_poison_from_propagating : GICombineRule<
  (defs root:$root, build_fn_matchinfo:$matchinfo),
  (match (G_FREEZE $dst, $src):$root,
         [{ return !isGuaranteedNotToBePoison(${src}.getReg(), MRI) && Helper.matchFreezeOfSingleMaybePoisonOperand(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.applyBuildFn(*${root}, ${matchinfo}); }])>;

def extending_loads : GICombineRule<
  (defs root:$root, extending_load_matchdata:$matchinfo),
  (match (wip_match_opcode G_LOAD, G_SEXTLOAD, G_ZEXTLOAD):$root,
         [{ return Helper.matchCombineExtendingLoads(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.applyCombineExtendingLoads(*${root}, ${matchinfo}); }])>;

def load_and_mask : GICombineRule<
  (defs root:$root, build_fn_matchinfo:$matchinfo),
  (match (wip_match_opcode G_AND):$root,
        [{ return Helper.matchCombineLoadWithAndMask(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.applyBuildFn(*${root}, ${matchinfo}); }])>;
def combines_for_extload: GICombineGroup<[extending_loads, load_and_mask]>;

def sext_trunc_sextload : GICombineRule<
  (defs root:$d),
  (match (wip_match_opcode G_SEXT_INREG):$d,
         [{ return Helper.matchSextTruncSextLoad(*${d}); }]),
  (apply [{ Helper.applySextTruncSextLoad(*${d}); }])>;

def sext_inreg_of_load_matchdata : GIDefMatchData<"std::tuple<Register, unsigned>">;
def sext_inreg_of_load : GICombineRule<
  (defs root:$root, sext_inreg_of_load_matchdata:$matchinfo),
  (match (wip_match_opcode G_SEXT_INREG):$root,
         [{ return Helper.matchSextInRegOfLoad(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.applySextInRegOfLoad(*${root}, ${matchinfo}); }])>;

def sext_inreg_to_zext_inreg : GICombineRule<
  (defs root:$dst),
  (match
    (G_SEXT_INREG $dst, $src, $imm):$root,
      [{
        unsigned BitWidth = MRI.getType(${src}.getReg()).getScalarSizeInBits();
        return Helper.getKnownBits()->maskedValueIsZero(${src}.getReg(),
                 APInt::getOneBitSet(BitWidth, ${imm}.getImm() - 1)); }]),
    (apply [{
      Helper.getBuilder().setInstrAndDebugLoc(*${root});
      Helper.getBuilder().buildZExtInReg(${dst}, ${src}, ${imm}.getImm());
      ${root}->eraseFromParent();
  }])
>;

def combine_extracted_vector_load : GICombineRule<
  (defs root:$root, build_fn_matchinfo:$matchinfo),
  (match (wip_match_opcode G_EXTRACT_VECTOR_ELT):$root,
        [{ return Helper.matchCombineExtractedVectorLoad(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.applyBuildFn(*${root}, ${matchinfo}); }])>;

def combine_indexed_load_store : GICombineRule<
  (defs root:$root, indexed_load_store_matchdata:$matchinfo),
  (match (wip_match_opcode G_LOAD, G_SEXTLOAD, G_ZEXTLOAD, G_STORE):$root,
         [{ return Helper.matchCombineIndexedLoadStore(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.applyCombineIndexedLoadStore(*${root}, ${matchinfo}); }])>;

def opt_brcond_by_inverting_cond_matchdata : GIDefMatchData<"MachineInstr *">;
def opt_brcond_by_inverting_cond : GICombineRule<
  (defs root:$root, opt_brcond_by_inverting_cond_matchdata:$matchinfo),
  (match (wip_match_opcode G_BR):$root,
         [{ return Helper.matchOptBrCondByInvertingCond(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.applyOptBrCondByInvertingCond(*${root}, ${matchinfo}); }])>;

def ptr_add_immed_matchdata : GIDefMatchData<"PtrAddChain">;
def ptr_add_immed_chain : GICombineRule<
  (defs root:$d, ptr_add_immed_matchdata:$matchinfo),
  (match (wip_match_opcode G_PTR_ADD):$d,
         [{ return Helper.matchPtrAddImmedChain(*${d}, ${matchinfo}); }]),
  (apply [{ Helper.applyPtrAddImmedChain(*${d}, ${matchinfo}); }])>;

def shifts_too_big : GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_SHL, G_ASHR, G_LSHR):$root,
         [{ return Helper.matchShiftsTooBig(*${root}); }]),
  (apply [{ Helper.replaceInstWithUndef(*${root}); }])>;

// Fold shift (shift base x), y -> shift base, (x+y), if shifts are same
def shift_immed_matchdata : GIDefMatchData<"RegisterImmPair">;
def shift_immed_chain : GICombineRule<
  (defs root:$d, shift_immed_matchdata:$matchinfo),
  (match (wip_match_opcode G_SHL, G_ASHR, G_LSHR, G_SSHLSAT, G_USHLSAT):$d,
         [{ return Helper.matchShiftImmedChain(*${d}, ${matchinfo}); }]),
  (apply [{ Helper.applyShiftImmedChain(*${d}, ${matchinfo}); }])>;

// Transform shift (logic (shift X, C0), Y), C1
//        -> logic (shift X, (C0+C1)), (shift Y, C1), if shifts are same
def shift_of_shifted_logic_matchdata : GIDefMatchData<"ShiftOfShiftedLogic">;
def shift_of_shifted_logic_chain : GICombineRule<
  (defs root:$d, shift_of_shifted_logic_matchdata:$matchinfo),
  (match (wip_match_opcode G_SHL, G_ASHR, G_LSHR, G_USHLSAT, G_SSHLSAT):$d,
         [{ return Helper.matchShiftOfShiftedLogic(*${d}, ${matchinfo}); }]),
  (apply [{ Helper.applyShiftOfShiftedLogic(*${d}, ${matchinfo}); }])>;

def mul_to_shl : GICombineRule<
  (defs root:$d, unsigned_matchinfo:$matchinfo),
  (match (G_MUL $d, $op1, $op2):$mi,
         [{ return Helper.matchCombineMulToShl(*${mi}, ${matchinfo}); }]),
  (apply [{ Helper.applyCombineMulToShl(*${mi}, ${matchinfo}); }])>;

// shl ([asz]ext x), y => zext (shl x, y), if shift does not overflow int
def reduce_shl_of_extend_matchdata : GIDefMatchData<"RegisterImmPair">;
def reduce_shl_of_extend : GICombineRule<
  (defs root:$dst, reduce_shl_of_extend_matchdata:$matchinfo),
  (match (G_SHL $dst, $src0, $src1):$mi,
         [{ return Helper.matchCombineShlOfExtend(*${mi}, ${matchinfo}); }]),
  (apply [{ Helper.applyCombineShlOfExtend(*${mi}, ${matchinfo}); }])>;

// Combine bitreverse(shl (bitreverse x), y)) -> (lshr x, y)
def bitreverse_shl : GICombineRule<
  (defs root:$d),
  (match (G_BITREVERSE $rev, $val),
         (G_SHL $src, $rev, $amt):$mi,
         (G_BITREVERSE $d, $src),
         [{ return Helper.isLegalOrBeforeLegalizer({TargetOpcode::G_LSHR,
                                                   {MRI.getType(${val}.getReg()),
                                                    MRI.getType(${amt}.getReg())}}); }]),
  (apply (G_LSHR $d, $val, $amt))>;

// Combine bitreverse(lshr (bitreverse x), y)) -> (shl x, y)
def bitreverse_lshr : GICombineRule<
  (defs root:$d),
  (match (G_BITREVERSE $rev, $val),
         (G_LSHR $src, $rev, $amt):$mi,
         (G_BITREVERSE $d, $src),
         [{ return Helper.isLegalOrBeforeLegalizer({TargetOpcode::G_SHL,
                                                   {MRI.getType(${val}.getReg()),
                                                    MRI.getType(${amt}.getReg())}}); }]),
  (apply (G_SHL $d, $val, $amt))>;

// Combine (shl (add x, c1), c2) -> (add (shl x, c2), c1 << c2)
// Combine (shl (or x, c1), c2) -> (or (shl x, c2), c1 << c2)
def commute_shift : GICombineRule<
  (defs root:$d, build_fn_matchinfo:$matchinfo),
  (match (wip_match_opcode G_SHL):$d,
         [{ return Helper.matchCommuteShift(*${d}, ${matchinfo}); }]),
  (apply [{ Helper.applyBuildFn(*${d}, ${matchinfo}); }])>;

def narrow_binop_feeding_and : GICombineRule<
  (defs root:$root, build_fn_matchinfo:$matchinfo),
  (match (wip_match_opcode G_AND):$root,
         [{ return Helper.matchNarrowBinopFeedingAnd(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.applyBuildFnNoErase(*${root}, ${matchinfo}); }])>;

// [us]itofp(undef) = 0, because the result value is bounded.
def undef_to_fp_zero : GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_UITOFP, G_SITOFP):$root,
         [{ return Helper.matchAnyExplicitUseIsUndef(*${root}); }]),
  (apply [{ Helper.replaceInstWithFConstant(*${root}, 0.0); }])>;

def undef_to_int_zero: GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_AND, G_MUL):$root,
         [{ return Helper.matchAnyExplicitUseIsUndef(*${root}); }]),
  (apply [{ Helper.replaceInstWithConstant(*${root}, 0); }])>;

def undef_to_negative_one: GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_OR):$root,
         [{ return Helper.matchAnyExplicitUseIsUndef(*${root}); }]),
  (apply [{ Helper.replaceInstWithConstant(*${root}, -1); }])>;

def binop_left_undef_to_zero: GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_SHL, G_UDIV, G_UREM):$root,
         [{ return Helper.matchOperandIsUndef(*${root}, 1); }]),
  (apply [{ Helper.replaceInstWithConstant(*${root}, 0); }])>;

def binop_right_undef_to_undef: GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_SHL, G_ASHR, G_LSHR):$root,
         [{ return Helper.matchOperandIsUndef(*${root}, 2); }]),
  (apply [{ Helper.replaceInstWithUndef(*${root}); }])>;

def unary_undef_to_zero: GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_ABS):$root,
         [{ return Helper.matchOperandIsUndef(*${root}, 1); }]),
  (apply [{ Helper.replaceInstWithConstant(*${root}, 0); }])>;

// Instructions where if any source operand is undef, the instruction can be
// replaced with undef.
def propagate_undef_any_op: GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_ADD, G_FPTOSI, G_FPTOUI, G_SUB, G_XOR, G_TRUNC):$root,
         [{ return Helper.matchAnyExplicitUseIsUndef(*${root}); }]),
  (apply [{ Helper.replaceInstWithUndef(*${root}); }])>;

// Instructions where if all source operands are undef, the instruction can be
// replaced with undef.
def propagate_undef_all_ops: GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_SHUFFLE_VECTOR):$root,
          [{ return Helper.matchAllExplicitUsesAreUndef(*${root}); }]),
  (apply [{ Helper.replaceInstWithUndef(*${root}); }])>;

// Replace a G_SHUFFLE_VECTOR with an undef mask with a G_IMPLICIT_DEF.
def propagate_undef_shuffle_mask: GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_SHUFFLE_VECTOR):$root,
         [{ return Helper.matchUndefShuffleVectorMask(*${root}); }]),
  (apply [{ Helper.replaceInstWithUndef(*${root}); }])>;

// Replace a G_SHUFFLE_VECTOR with a G_EXTRACT_VECTOR_ELT.
def shuffle_to_extract: GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_SHUFFLE_VECTOR):$root,
         [{ return Helper.matchShuffleToExtract(*${root}); }]),
  (apply [{ Helper.applyShuffleToExtract(*${root}); }])>;

  // Replace an insert/extract element of an out of bounds index with undef.
  def insert_extract_vec_elt_out_of_bounds : GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_INSERT_VECTOR_ELT, G_EXTRACT_VECTOR_ELT):$root,
         [{ return Helper.matchInsertExtractVecEltOutOfBounds(*${root}); }]),
  (apply [{ Helper.replaceInstWithUndef(*${root}); }])>;

// Fold (cond ? x : x) -> x
def select_same_val: GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_SELECT):$root,
    [{ return Helper.matchSelectSameVal(*${root}); }]),
  (apply [{ Helper.replaceSingleDefInstWithOperand(*${root}, 2); }])
>;

// Fold (undef ? x : y) -> y
def select_undef_cmp: GICombineRule<
  (defs root:$dst),
  (match (G_IMPLICIT_DEF $undef),
         (G_SELECT $dst, $undef, $x, $y)),
  (apply (GIReplaceReg $dst, $y))
>;

// Fold (true ? x : y) -> x
// Fold (false ? x : y) -> y
def select_constant_cmp: GICombineRule<
  (defs root:$root, unsigned_matchinfo:$matchinfo),
  (match (wip_match_opcode G_SELECT):$root,
    [{ return Helper.matchConstantSelectCmp(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.replaceSingleDefInstWithOperand(*${root}, ${matchinfo}); }])
>;

// Fold (C op x) -> (x op C)
// TODO: handle more isCommutable opcodes
// TODO: handle compares (currently not marked as isCommutable)
def commute_int_constant_to_rhs : GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_ADD, G_MUL, G_AND, G_OR, G_XOR,
                           G_SMIN, G_SMAX, G_UMIN, G_UMAX, G_UADDO, G_SADDO,
                           G_UMULO, G_SMULO, G_UMULH, G_SMULH,
                           G_UADDSAT, G_SADDSAT, G_SMULFIX, G_UMULFIX,
                           G_SMULFIXSAT, G_UMULFIXSAT):$root,
    [{ return Helper.matchCommuteConstantToRHS(*${root}); }]),
  (apply [{ Helper.applyCommuteBinOpOperands(*${root}); }])
>;

def commute_fp_constant_to_rhs : GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_FADD, G_FMUL, G_FMINNUM, G_FMAXNUM,
                           G_FMINNUM_IEEE, G_FMAXNUM_IEEE,
                           G_FMINIMUM, G_FMAXIMUM):$root,
    [{ return Helper.matchCommuteFPConstantToRHS(*${root}); }]),
  (apply [{ Helper.applyCommuteBinOpOperands(*${root}); }])
>;

def commute_constant_to_rhs : GICombineGroup<[
  commute_int_constant_to_rhs,
  commute_fp_constant_to_rhs
]>;

// Fold x op 0 -> x
def right_identity_zero_frags : GICombinePatFrag<
  (outs root:$dst), (ins $x),
  !foreach(op,
           [G_SUB, G_ADD, G_OR, G_XOR, G_SHL, G_ASHR,
            G_LSHR, G_PTR_ADD, G_ROTL, G_ROTR],
           (pattern (op $dst, $x, 0)))>;
def right_identity_zero: GICombineRule<
  (defs root:$dst),
  (match (right_identity_zero_frags $dst, $lhs)),
  (apply (GIReplaceReg $dst, $lhs))
>;

def right_identity_neg_zero_fp: GICombineRule<
  (defs root:$dst),
  (match (G_FADD $dst, $x, $y):$root,
    [{ return Helper.matchConstantFPOp(${y}, -0.0); }]),
  (apply (GIReplaceReg $dst, $x))
>;

// Fold x op 1 -> x
def right_identity_one_int: GICombineRule<
  (defs root:$dst),
  (match (G_MUL $dst, $x, 1)),
  (apply (GIReplaceReg $dst, $x))
>;

def right_identity_one_fp: GICombineRule<
  (defs root:$dst),
  (match (G_FMUL $dst, $x, $y):$root,
    [{ return Helper.matchConstantFPOp(${y}, 1.0); }]),
  (apply (GIReplaceReg $dst, $x))
>;

def right_identity_neg_one_fp: GICombineRule<
  (defs root:$dst),
  (match (G_FMUL $dst, $x, $y):$root,
    [{ return Helper.matchConstantFPOp(${y}, -1.0); }]),
  (apply (G_FNEG $dst, $x))
>;

def right_identity_one : GICombineGroup<[right_identity_one_int, right_identity_one_fp]>;

// Fold (x op x) - > x
def binop_same_val_frags : GICombinePatFrag<
  (outs root:$dst), (ins $x),
  [
    (pattern (G_AND $dst, $x, $x)),
    (pattern (G_OR $dst, $x, $x)),
  ]
>;
def binop_same_val: GICombineRule<
  (defs root:$dst),
  (match (binop_same_val_frags $dst, $src)),
  (apply (GIReplaceReg $dst, $src))
>;

// Fold (0 op x) - > 0
def binop_left_to_zero: GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_SDIV, G_UDIV, G_SREM, G_UREM):$root,
    [{ return Helper.matchOperandIsZero(*${root}, 1); }]),
  (apply [{ Helper.replaceSingleDefInstWithOperand(*${root}, 1); }])
>;

def urem_pow2_to_mask : GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_UREM):$root,
    [{ return Helper.matchOperandIsKnownToBeAPowerOfTwo(*${root}, 2); }]),
  (apply [{ Helper.applySimplifyURemByPow2(*${root}); }])
>;

// Push a binary operator through a select on constants.
//
// binop (select cond, K0, K1), K2 ->
//   select cond, (binop K0, K2), (binop K1, K2)

// Every binary operator that has constant folding. We currently do
// not have constant folding for G_FPOW, G_FMAXNUM_IEEE or
// G_FMINNUM_IEEE.
def fold_binop_into_select : GICombineRule<
  (defs root:$root, unsigned_matchinfo:$select_op_no),
  (match (wip_match_opcode
    G_ADD, G_SUB, G_PTR_ADD, G_AND, G_OR, G_XOR,
    G_SDIV, G_SREM, G_UDIV, G_UREM, G_LSHR, G_ASHR, G_SHL,
    G_SMIN, G_SMAX, G_UMIN, G_UMAX,
    G_FMUL, G_FADD, G_FSUB, G_FDIV, G_FREM,
    G_FMINNUM, G_FMAXNUM, G_FMINIMUM, G_FMAXIMUM):$root,
    [{ return Helper.matchFoldBinOpIntoSelect(*${root}, ${select_op_no}); }]),
  (apply [{ Helper.applyFoldBinOpIntoSelect(*${root}, ${select_op_no}); }])
>;

// Transform d = [su]div(x, y) and r = [su]rem(x, y) - > d, r = [su]divrem(x, y)
def div_rem_to_divrem_matchdata : GIDefMatchData<"MachineInstr *">;
def div_rem_to_divrem : GICombineRule<
  (defs root:$root, div_rem_to_divrem_matchdata:$matchinfo),
  (match (wip_match_opcode G_SDIV, G_UDIV, G_SREM, G_UREM):$root,
    [{ return Helper.matchCombineDivRem(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.applyCombineDivRem(*${root}, ${matchinfo}); }])
>;

// Fold (x op 0) - > 0
def binop_right_to_zero: GICombineRule<
  (defs root:$dst),
  (match (G_MUL $dst, $lhs, 0:$zero)),
  (apply (GIReplaceReg $dst, $zero))
>;

// Erase stores of undef values.
def erase_undef_store : GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_STORE):$root,
    [{ return Helper.matchUndefStore(*${root}); }]),
  (apply [{ Helper.eraseInst(*${root}); }])
>;

def simplify_add_to_sub_matchinfo: GIDefMatchData<"std::tuple<Register, Register>">;
def simplify_add_to_sub: GICombineRule <
  (defs root:$root, simplify_add_to_sub_matchinfo:$info),
  (match (wip_match_opcode G_ADD):$root,
    [{ return Helper.matchSimplifyAddToSub(*${root}, ${info}); }]),
  (apply [{ Helper.applySimplifyAddToSub(*${root}, ${info});}])
>;

// Fold fp_op(cst) to the constant result of the floating point operation.
class constant_fold_unary_fp_op_rule<Instruction opcode> : GICombineRule <
  (defs root:$dst),
  (match (opcode $dst, $src0):$root, (G_FCONSTANT $src0, $cst)),
  (apply [{ Helper.applyCombineConstantFoldFpUnary(*${root}, ${cst}.getFPImm()); }])
>;

def constant_fold_fneg : constant_fold_unary_fp_op_rule<G_FNEG>;
def constant_fold_fabs : constant_fold_unary_fp_op_rule<G_FABS>;
def constant_fold_fsqrt : constant_fold_unary_fp_op_rule<G_FSQRT>;
def constant_fold_flog2 : constant_fold_unary_fp_op_rule<G_FLOG2>;
def constant_fold_fptrunc : constant_fold_unary_fp_op_rule<G_FPTRUNC>;

// Fold constant zero int to fp conversions.
class itof_const_zero_fold_rule<Instruction opcode> : GICombineRule <
  (defs root:$dst),
  (match (opcode $dst, 0)),
  // Can't use COPY $dst, 0 here because the 0 operand may be a smaller type
  // than the destination for itofp.
  (apply [{ Helper.replaceInstWithFConstant(*${dst}.getParent(), 0.0); }])
>;
def itof_const_zero_fold_si : itof_const_zero_fold_rule<G_SITOFP>;
def itof_const_zero_fold_ui : itof_const_zero_fold_rule<G_UITOFP>;

def constant_fold_fp_ops : GICombineGroup<[
  constant_fold_fneg,
  constant_fold_fabs,
  constant_fold_fsqrt,
  constant_fold_flog2,
  constant_fold_fptrunc,
  itof_const_zero_fold_si,
  itof_const_zero_fold_ui
]>;

// Fold int2ptr(ptr2int(x)) -> x
def p2i_to_i2p: GICombineRule<
  (defs root:$root, register_matchinfo:$info),
  (match (wip_match_opcode G_INTTOPTR):$root,
    [{ return Helper.matchCombineI2PToP2I(*${root}, ${info}); }]),
  (apply [{ Helper.applyCombineI2PToP2I(*${root}, ${info}); }])
>;

// Fold ptr2int(int2ptr(x)) -> x
def i2p_to_p2i: GICombineRule<
  (defs root:$dst, register_matchinfo:$info),
  (match (G_INTTOPTR $t, $ptr),
         (G_PTRTOINT $dst, $t):$mi,
    [{ ${info} = ${ptr}.getReg(); return true; }]),
  (apply [{ Helper.applyCombineP2IToI2P(*${mi}, ${info}); }])
>;

// Fold add ptrtoint(x), y -> ptrtoint (ptr_add x), y
def add_p2i_to_ptradd_matchinfo : GIDefMatchData<"std::pair<Register, bool>">;
def add_p2i_to_ptradd : GICombineRule<
  (defs root:$root, add_p2i_to_ptradd_matchinfo:$info),
  (match (wip_match_opcode G_ADD):$root,
    [{ return Helper.matchCombineAddP2IToPtrAdd(*${root}, ${info}); }]),
  (apply [{ Helper.applyCombineAddP2IToPtrAdd(*${root}, ${info}); }])
>;

// Fold (ptr_add (int2ptr C1), C2) -> C1 + C2
def const_ptradd_to_i2p: GICombineRule<
  (defs root:$root, apint_matchinfo:$info),
  (match (wip_match_opcode G_PTR_ADD):$root,
    [{ return Helper.matchCombineConstPtrAddToI2P(*${root}, ${info}); }]),
  (apply [{ Helper.applyCombineConstPtrAddToI2P(*${root}, ${info}); }])
>;

// Simplify: (logic_op (op x...), (op y...)) -> (op (logic_op x, y))
def hoist_logic_op_with_same_opcode_hands: GICombineRule <
  (defs root:$root, instruction_steps_matchdata:$info),
  (match (wip_match_opcode G_AND, G_OR, G_XOR):$root,
    [{ return Helper.matchHoistLogicOpWithSameOpcodeHands(*${root}, ${info}); }]),
  (apply [{ Helper.applyBuildInstructionSteps(*${root}, ${info});}])
>;

// Fold ashr (shl x, C), C -> sext_inreg (C)
def shl_ashr_to_sext_inreg_matchinfo : GIDefMatchData<"std::tuple<Register, int64_t>">;
def shl_ashr_to_sext_inreg : GICombineRule<
  (defs root:$root, shl_ashr_to_sext_inreg_matchinfo:$info),
  (match (wip_match_opcode G_ASHR): $root,
    [{ return Helper.matchAshrShlToSextInreg(*${root}, ${info}); }]),
  (apply [{ Helper.applyAshShlToSextInreg(*${root}, ${info});}])
>;

// Fold and(and(x, C1), C2) -> C1&C2 ? and(x, C1&C2) : 0
def overlapping_and: GICombineRule <
  (defs root:$root, build_fn_matchinfo:$info),
  (match (wip_match_opcode G_AND):$root,
         [{ return Helper.matchOverlappingAnd(*${root}, ${info}); }]),
  (apply [{ Helper.applyBuildFn(*${root}, ${info}); }])
>;

// Fold (x & y) -> x or (x & y) -> y when (x & y) is known to equal x or equal y.
def redundant_and: GICombineRule <
  (defs root:$root, register_matchinfo:$matchinfo),
  (match (wip_match_opcode G_AND):$root,
         [{ return Helper.matchRedundantAnd(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.replaceSingleDefInstWithReg(*${root}, ${matchinfo}); }])
>;

// Fold (x | y) -> x or (x | y) -> y when (x | y) is known to equal x or equal y.
def redundant_or: GICombineRule <
  (defs root:$root, register_matchinfo:$matchinfo),
  (match (wip_match_opcode G_OR):$root,
         [{ return Helper.matchRedundantOr(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.replaceSingleDefInstWithReg(*${root}, ${matchinfo}); }])
>;

// If the input is already sign extended, just drop the extension.
// sext_inreg x, K ->
//   if computeNumSignBits(x) >= (x.getScalarSizeInBits() - K + 1)
def redundant_sext_inreg: GICombineRule <
  (defs root:$root),
  (match (wip_match_opcode G_SEXT_INREG):$root,
         [{ return Helper.matchRedundantSExtInReg(*${root}); }]),
     (apply [{ Helper.replaceSingleDefInstWithOperand(*${root}, 1); }])
>;

// Fold (anyext (trunc x)) -> x if the source type is same as
// the destination type.
def anyext_trunc_fold: GICombineRule <
  (defs root:$root, register_matchinfo:$matchinfo),
  (match (wip_match_opcode G_ANYEXT):$root,
         [{ return Helper.matchCombineAnyExtTrunc(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.replaceSingleDefInstWithReg(*${root}, ${matchinfo}); }])
>;

// Fold (zext (trunc x)) -> x if the source type is same as the destination type
// and truncated bits are known to be zero.
def zext_trunc_fold: GICombineRule <
  (defs root:$root, register_matchinfo:$matchinfo),
  (match (wip_match_opcode G_ZEXT):$root,
         [{ return Helper.matchCombineZextTrunc(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.replaceSingleDefInstWithReg(*${root}, ${matchinfo}); }])
>;

// Fold ([asz]ext ([asz]ext x)) -> ([asz]ext x).
def ext_ext_fold_matchinfo : GIDefMatchData<"std::tuple<Register, unsigned>">;
def ext_ext_fold: GICombineRule <
  (defs root:$root, ext_ext_fold_matchinfo:$matchinfo),
  (match (wip_match_opcode G_ANYEXT, G_SEXT, G_ZEXT):$root,
         [{ return Helper.matchCombineExtOfExt(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.applyCombineExtOfExt(*${root}, ${matchinfo}); }])
>;

def not_cmp_fold_matchinfo : GIDefMatchData<"SmallVector<Register, 4>">;
def not_cmp_fold : GICombineRule<
  (defs root:$d, not_cmp_fold_matchinfo:$info),
  (match (wip_match_opcode G_XOR): $d,
  [{ return Helper.matchNotCmp(*${d}, ${info}); }]),
  (apply [{ Helper.applyNotCmp(*${d}, ${info}); }])
>;

// Fold (fneg (fneg x)) -> x.
def fneg_fneg_fold: GICombineRule <
  (defs root:$dst),
  (match (G_FNEG $t, $src),
         (G_FNEG $dst, $t)),
  (apply (GIReplaceReg $dst, $src))
>;

// Fold (unmerge(merge x, y, z)) -> z, y, z.
def unmerge_merge_matchinfo : GIDefMatchData<"SmallVector<Register, 8>">;
def unmerge_merge : GICombineRule<
  (defs root:$d, unmerge_merge_matchinfo:$info),
  (match (wip_match_opcode G_UNMERGE_VALUES): $d,
  [{ return Helper.matchCombineUnmergeMergeToPlainValues(*${d}, ${info}); }]),
  (apply [{ Helper.applyCombineUnmergeMergeToPlainValues(*${d}, ${info}); }])
>;

// Fold merge(unmerge).
def merge_unmerge : GICombineRule<
  (defs root:$d, register_matchinfo:$matchinfo),
  (match (wip_match_opcode G_MERGE_VALUES):$d,
  [{ return Helper.matchCombineMergeUnmerge(*${d}, ${matchinfo}); }]),
  (apply [{ Helper.replaceSingleDefInstWithReg(*${d}, ${matchinfo}); }])
>;

// Fold (fabs (fneg x)) -> (fabs x).
def fabs_fneg_fold: GICombineRule <
  (defs root:$dst),
  (match  (G_FNEG $tmp, $x),
          (G_FABS $dst, $tmp)),
  (apply (G_FABS $dst, $x))>;

// Fold (unmerge cst) -> cst1, cst2, ...
def unmerge_cst_matchinfo : GIDefMatchData<"SmallVector<APInt, 8>">;
def unmerge_cst : GICombineRule<
  (defs root:$d, unmerge_cst_matchinfo:$info),
  (match (wip_match_opcode G_UNMERGE_VALUES): $d,
  [{ return Helper.matchCombineUnmergeConstant(*${d}, ${info}); }]),
  (apply [{ Helper.applyCombineUnmergeConstant(*${d}, ${info}); }])
>;

// Fold (unmerge undef) -> undef, undef, ...
def unmerge_undef : GICombineRule<
  (defs root:$root, build_fn_matchinfo:$info),
  (match (wip_match_opcode G_UNMERGE_VALUES): $root,
         [{ return Helper.matchCombineUnmergeUndef(*${root}, ${info}); }]),
  (apply [{ Helper.applyBuildFn(*${root}, ${info}); }])
>;

// Transform x,y<dead> = unmerge z -> x = trunc z.
def unmerge_dead_to_trunc : GICombineRule<
  (defs root:$d),
  (match (wip_match_opcode G_UNMERGE_VALUES): $d,
  [{ return Helper.matchCombineUnmergeWithDeadLanesToTrunc(*${d}); }]),
  (apply [{ Helper.applyCombineUnmergeWithDeadLanesToTrunc(*${d}); }])
>;

// Transform x,y = unmerge(zext(z)) -> x = zext z; y = 0.
def unmerge_zext_to_zext : GICombineRule<
  (defs root:$d),
  (match (wip_match_opcode G_UNMERGE_VALUES): $d,
  [{ return Helper.matchCombineUnmergeZExtToZExt(*${d}); }]),
  (apply [{ Helper.applyCombineUnmergeZExtToZExt(*${d}); }])
>;

// Fold trunc ([asz]ext x) -> x or ([asz]ext x) or (trunc x).
def trunc_ext_fold_matchinfo : GIDefMatchData<"std::pair<Register, unsigned>">;
def trunc_ext_fold: GICombineRule <
  (defs root:$root, trunc_ext_fold_matchinfo:$matchinfo),
  (match (wip_match_opcode G_TRUNC):$root,
         [{ return Helper.matchCombineTruncOfExt(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.applyCombineTruncOfExt(*${root}, ${matchinfo}); }])
>;

// Under certain conditions, transform:
//  trunc (shl x, K)     -> shl (trunc x), K//
//  trunc ([al]shr x, K) -> (trunc ([al]shr (trunc x), K))
def trunc_shift_matchinfo : GIDefMatchData<"std::pair<MachineInstr*, LLT>">;
def trunc_shift: GICombineRule <
  (defs root:$root, trunc_shift_matchinfo:$matchinfo),
  (match (wip_match_opcode G_TRUNC):$root,
         [{ return Helper.matchCombineTruncOfShift(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.applyCombineTruncOfShift(*${root}, ${matchinfo}); }])
>;

// Transform (mul x, -1) -> (sub 0, x)
def mul_by_neg_one: GICombineRule <
  (defs root:$dst),
  (match (G_MUL $dst, $x, -1)),
  (apply (G_SUB $dst, 0, $x))
>;

// Fold (xor (and x, y), y) -> (and (not x), y)
def xor_of_and_with_same_reg_matchinfo :
    GIDefMatchData<"std::pair<Register, Register>">;
def xor_of_and_with_same_reg: GICombineRule <
  (defs root:$root, xor_of_and_with_same_reg_matchinfo:$matchinfo),
  (match (wip_match_opcode G_XOR):$root,
         [{ return Helper.matchXorOfAndWithSameReg(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.applyXorOfAndWithSameReg(*${root}, ${matchinfo}); }])
>;

// Transform (ptr_add 0, x) -> (int_to_ptr x)
def ptr_add_with_zero: GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_PTR_ADD):$root,
         [{ return Helper.matchPtrAddZero(*${root}); }]),
  (apply [{ Helper.applyPtrAddZero(*${root}); }])>;

def regs_small_vec : GIDefMatchData<"SmallVector<Register, 4>">;
def combine_insert_vec_elts_build_vector : GICombineRule<
  (defs root:$root, regs_small_vec:$info),
  (match (wip_match_opcode G_INSERT_VECTOR_ELT):$root,
    [{ return Helper.matchCombineInsertVecElts(*${root}, ${info}); }]),
  (apply [{ Helper.applyCombineInsertVecElts(*${root}, ${info}); }])>;

def load_or_combine : GICombineRule<
  (defs root:$root, build_fn_matchinfo:$info),
  (match (wip_match_opcode G_OR):$root,
    [{ return Helper.matchLoadOrCombine(*${root}, ${info}); }]),
  (apply [{ Helper.applyBuildFn(*${root}, ${info}); }])>;

def extend_through_phis_matchdata: GIDefMatchData<"MachineInstr*">;
def extend_through_phis : GICombineRule<
  (defs root:$root, extend_through_phis_matchdata:$matchinfo),
  (match (wip_match_opcode G_PHI):$root,
    [{ return Helper.matchExtendThroughPhis(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.applyExtendThroughPhis(*${root}, ${matchinfo}); }])>;

// Currently only the one combine above.
def insert_vec_elt_combines : GICombineGroup<
                            [combine_insert_vec_elts_build_vector]>;

def extract_vec_elt_build_vec : GICombineRule<
  (defs root:$root, register_matchinfo:$matchinfo),
  (match (wip_match_opcode G_EXTRACT_VECTOR_ELT):$root,
    [{ return Helper.matchExtractVecEltBuildVec(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.applyExtractVecEltBuildVec(*${root}, ${matchinfo}); }])>;

// Fold away full elt extracts from a build_vector.
def extract_all_elts_from_build_vector_matchinfo :
  GIDefMatchData<"SmallVector<std::pair<Register, MachineInstr*>>">;
def extract_all_elts_from_build_vector : GICombineRule<
  (defs root:$root, extract_all_elts_from_build_vector_matchinfo:$matchinfo),
  (match (wip_match_opcode G_BUILD_VECTOR):$root,
    [{ return Helper.matchExtractAllEltsFromBuildVector(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.applyExtractAllEltsFromBuildVector(*${root}, ${matchinfo}); }])>;

def extract_vec_elt_combines : GICombineGroup<[
  extract_vec_elt_build_vec,
  extract_all_elts_from_build_vector]>;

def funnel_shift_from_or_shift : GICombineRule<
  (defs root:$root, build_fn_matchinfo:$info),
  (match (wip_match_opcode G_OR):$root,
    [{ return Helper.matchOrShiftToFunnelShift(*${root}, ${info}); }]),
  (apply [{ Helper.applyBuildFn(*${root}, ${info}); }])
>;

def funnel_shift_to_rotate : GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_FSHL, G_FSHR):$root,
    [{ return Helper.matchFunnelShiftToRotate(*${root}); }]),
  (apply [{ Helper.applyFunnelShiftToRotate(*${root}); }])
>;

// Fold fshr x, y, 0 -> y
def funnel_shift_right_zero: GICombineRule<
  (defs root:$root),
  (match (G_FSHR $x, $y, $z, 0):$root),
  (apply (COPY $x, $z))
>;

// Fold fshl x, y, 0 -> x
def funnel_shift_left_zero: GICombineRule<
  (defs root:$root),
  (match (G_FSHL $x, $y, $z, 0):$root),
  (apply (COPY $x, $y))
>;

// Fold fsh(l/r) x, y, C -> fsh(l/r) x, y, C % bw
def funnel_shift_overshift: GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_FSHL, G_FSHR):$root,
    [{ return Helper.matchConstantLargerBitWidth(*${root}, 3); }]),
  (apply [{ Helper.applyFunnelShiftConstantModulo(*${root}); }])
>;

def rotate_out_of_range : GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_ROTR, G_ROTL):$root,
    [{ return Helper.matchRotateOutOfRange(*${root}); }]),
  (apply [{ Helper.applyRotateOutOfRange(*${root}); }])
>;

def icmp_to_true_false_known_bits : GICombineRule<
  (defs root:$d, int64_matchinfo:$matchinfo),
  (match (wip_match_opcode G_ICMP):$d,
         [{ return Helper.matchICmpToTrueFalseKnownBits(*${d}, ${matchinfo}); }]),
  (apply [{ Helper.replaceInstWithConstant(*${d}, ${matchinfo}); }])>;

def icmp_to_lhs_known_bits : GICombineRule<
  (defs root:$root, build_fn_matchinfo:$info),
  (match (wip_match_opcode G_ICMP):$root,
         [{ return Helper.matchICmpToLHSKnownBits(*${root}, ${info}); }]),
  (apply [{ Helper.applyBuildFn(*${root}, ${info}); }])>;

def redundant_binop_in_equality : GICombineRule<
  (defs root:$root, build_fn_matchinfo:$info),
  (match (wip_match_opcode G_ICMP):$root,
         [{ return Helper.matchRedundantBinOpInEquality(*${root}, ${info}); }]),
  (apply [{ Helper.applyBuildFn(*${root}, ${info}); }])>;

// Transform: (X == 0 & Y == 0) -> (X | Y) == 0
def double_icmp_zero_and_combine: GICombineRule<
  (defs root:$root),
  (match (G_ICMP $d1, $p, $s1, 0),
         (G_ICMP $d2, $p, $s2, 0),
         (G_AND $root, $d1, $d2),
         [{ return ${p}.getPredicate() == CmpInst::ICMP_EQ &&
                       !MRI.getType(${s1}.getReg()).getScalarType().isPointer() &&
                       (MRI.getType(${s1}.getReg()) ==
                           MRI.getType(${s2}.getReg())); }]),
  (apply (G_OR $ordst, $s1, $s2),
         (G_ICMP $root, $p, $ordst, 0))
>;

// Transform: (X != 0 | Y != 0) -> (X | Y) != 0
def double_icmp_zero_or_combine: GICombineRule<
  (defs root:$root),
  (match (G_ICMP $d1, $p, $s1, 0),
         (G_ICMP $d2, $p, $s2, 0),
         (G_OR $root, $d1, $d2),
         [{ return ${p}.getPredicate() == CmpInst::ICMP_NE &&
                       !MRI.getType(${s1}.getReg()).getScalarType().isPointer() &&
                       (MRI.getType(${s1}.getReg()) ==
                           MRI.getType(${s2}.getReg())); }]),
  (apply (G_OR $ordst, $s1, $s2),
         (G_ICMP $root, $p, $ordst, 0))
>;

def double_icmp_zero_and_or_combine : GICombineGroup<[double_icmp_zero_and_combine,
                                                      double_icmp_zero_or_combine]>;

def and_or_disjoint_mask : GICombineRule<
  (defs root:$root, build_fn_matchinfo:$info),
  (match (wip_match_opcode G_AND):$root,
         [{ return Helper.matchAndOrDisjointMask(*${root}, ${info}); }]),
  (apply [{ Helper.applyBuildFnNoErase(*${root}, ${info}); }])>;

def bitfield_extract_from_and : GICombineRule<
  (defs root:$root, build_fn_matchinfo:$info),
  (match (G_CONSTANT $mask, $imm2),
         (G_CONSTANT $lsb, $imm1),
         (G_LSHR $shift, $x, $lsb),
         (G_AND $root, $shift, $mask):$root,
    [{ return Helper.matchBitfieldExtractFromAnd(*${root}, ${info}); }]),
  (apply [{ Helper.applyBuildFn(*${root}, ${info}); }])>;

def funnel_shift_combines : GICombineGroup<[funnel_shift_from_or_shift,
                                            funnel_shift_to_rotate,
                                            funnel_shift_right_zero,
                                            funnel_shift_left_zero,
                                            funnel_shift_overshift]>;

def bitfield_extract_from_sext_inreg : GICombineRule<
  (defs root:$root, build_fn_matchinfo:$info),
  (match (wip_match_opcode G_SEXT_INREG):$root,
    [{ return Helper.matchBitfieldExtractFromSExtInReg(*${root}, ${info}); }]),
  (apply [{ Helper.applyBuildFn(*${root}, ${info}); }])>;

def bitfield_extract_from_shr : GICombineRule<
  (defs root:$root, build_fn_matchinfo:$info),
  (match (wip_match_opcode G_ASHR, G_LSHR):$root,
    [{ return Helper.matchBitfieldExtractFromShr(*${root}, ${info}); }]),
  (apply [{ Helper.applyBuildFn(*${root}, ${info}); }])>;

def bitfield_extract_from_shr_and : GICombineRule<
  (defs root:$root, build_fn_matchinfo:$info),
  (match (wip_match_opcode G_ASHR, G_LSHR):$root,
    [{ return Helper.matchBitfieldExtractFromShrAnd(*${root}, ${info}); }]),
  (apply [{ Helper.applyBuildFn(*${root}, ${info}); }])>;

def form_bitfield_extract : GICombineGroup<[bitfield_extract_from_sext_inreg,
                                            bitfield_extract_from_and,
                                            bitfield_extract_from_shr,
                                            bitfield_extract_from_shr_and]>;

def udiv_by_const : GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_UDIV):$root,
   [{ return Helper.matchUDivByConst(*${root}); }]),
  (apply [{ Helper.applyUDivByConst(*${root}); }])>;

def sdiv_by_const : GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_SDIV):$root,
   [{ return Helper.matchSDivByConst(*${root}); }]),
  (apply [{ Helper.applySDivByConst(*${root}); }])>;

def sdiv_by_pow2 : GICombineRule<
  (defs root:$root),
  (match (G_SDIV $dst, $x, $y, (MIFlags (not IsExact))):$root,
   [{ return Helper.matchDivByPow2(*${root}, /*IsSigned=*/true); }]),
  (apply [{ Helper.applySDivByPow2(*${root}); }])>;

def udiv_by_pow2 : GICombineRule<
  (defs root:$root),
  (match (G_UDIV $dst, $x, $y, (MIFlags (not IsExact))):$root,
   [{ return Helper.matchDivByPow2(*${root}, /*IsSigned=*/false); }]),
  (apply [{ Helper.applyUDivByPow2(*${root}); }])>;

def intdiv_combines : GICombineGroup<[udiv_by_const, sdiv_by_const,
                                      sdiv_by_pow2, udiv_by_pow2]>;

def reassoc_ptradd : GICombineRule<
  (defs root:$root, build_fn_matchinfo:$matchinfo),
  (match (wip_match_opcode G_PTR_ADD):$root,
    [{ return Helper.matchReassocPtrAdd(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.applyBuildFnNoErase(*${root}, ${matchinfo}); }])>;

def reassoc_comm_binops : GICombineRule<
  (defs root:$root, build_fn_matchinfo:$matchinfo),
  (match (G_ADD $root, $src1, $src2):$root,
    [{ return Helper.matchReassocCommBinOp(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.applyBuildFn(*${root}, ${matchinfo}); }])>;

def reassocs : GICombineGroup<[reassoc_ptradd, reassoc_comm_binops]>;

// Constant fold operations.
def constant_fold_binop : GICombineRule<
  (defs root:$d, apint_matchinfo:$matchinfo),
  (match (wip_match_opcode G_ADD, G_SUB, G_MUL, G_AND, G_OR, G_XOR, G_SHL, G_LSHR, G_ASHR):$d,
   [{ return Helper.matchConstantFoldBinOp(*${d}, ${matchinfo}); }]),
  (apply [{ Helper.replaceInstWithConstant(*${d}, ${matchinfo}); }])>;

def constant_fold_fp_binop : GICombineRule<
  (defs root:$d, constantfp_matchinfo:$matchinfo),
  (match (wip_match_opcode G_FADD, G_FSUB, G_FMUL, G_FDIV):$d,
   [{ return Helper.matchConstantFoldFPBinOp(*${d}, ${matchinfo}); }]),
  (apply [{ Helper.replaceInstWithFConstant(*${d}, ${matchinfo}); }])>;


def constant_fold_fma : GICombineRule<
  (defs root:$d, constantfp_matchinfo:$matchinfo),
  (match (wip_match_opcode G_FMAD, G_FMA):$d,
   [{ return Helper.matchConstantFoldFMA(*${d}, ${matchinfo}); }]),
  (apply [{ Helper.replaceInstWithFConstant(*${d}, ${matchinfo}); }])>;

def constant_fold_cast_op : GICombineRule<
  (defs root:$d, apint_matchinfo:$matchinfo),
  (match (wip_match_opcode G_ZEXT, G_SEXT, G_ANYEXT):$d,
   [{ return Helper.matchConstantFoldCastOp(*${d}, ${matchinfo}); }]),
  (apply [{ Helper.replaceInstWithConstant(*${d}, ${matchinfo}); }])>;

def mulo_by_2: GICombineRule<
  (defs root:$root, build_fn_matchinfo:$matchinfo),
  (match (wip_match_opcode G_UMULO, G_SMULO):$root,
         [{ return Helper.matchMulOBy2(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.applyBuildFnNoErase(*${root}, ${matchinfo}); }])>;

def mulo_by_0: GICombineRule<
  (defs root:$root, build_fn_matchinfo:$matchinfo),
  (match (wip_match_opcode G_UMULO, G_SMULO):$root,
         [{ return Helper.matchMulOBy0(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.applyBuildFn(*${root}, ${matchinfo}); }])>;

// Transform (uadde x, y, 0) -> (uaddo x, y)
//           (sadde x, y, 0) -> (saddo x, y)
//           (usube x, y, 0) -> (usubo x, y)
//           (ssube x, y, 0) -> (ssubo x, y)
def adde_to_addo: GICombineRule<
  (defs root:$root, build_fn_matchinfo:$matchinfo),
  (match (wip_match_opcode G_UADDE, G_SADDE, G_USUBE, G_SSUBE):$root,
         [{ return Helper.matchAddEToAddO(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.applyBuildFnNoErase(*${root}, ${matchinfo}); }])>;

def mulh_to_lshr : GICombineRule<
  (defs root:$root),
  (match (wip_match_opcode G_UMULH):$root,
         [{ return Helper.matchUMulHToLShr(*${root}); }]),
  (apply [{ Helper.applyUMulHToLShr(*${root}); }])>;

def mulh_combines : GICombineGroup<[mulh_to_lshr]>;

def redundant_neg_operands: GICombineRule<
  (defs root:$root, build_fn_matchinfo:$matchinfo),
  (match (wip_match_opcode G_FADD, G_FSUB, G_FMUL, G_FDIV, G_FMAD, G_FMA):$root,
    [{ return Helper.matchRedundantNegOperands(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.applyBuildFnNoErase(*${root}, ${matchinfo}); }])>;

// Transform (fsub +-0.0, X) -> (fneg X)
def fsub_to_fneg: GICombineRule<
  (defs root:$root, register_matchinfo:$matchinfo),
  (match (wip_match_opcode G_FSUB):$root,
    [{ return Helper.matchFsubToFneg(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.applyFsubToFneg(*${root}, ${matchinfo}); }])>;

// Transform (fadd x, (fmul y, z)) -> (fma y, z, x)
//           (fadd x, (fmul y, z)) -> (fmad y, z, x)
// Transform (fadd (fmul x, y), z) -> (fma x, y, z)
//           (fadd (fmul x, y), z) -> (fmad x, y, z)
def combine_fadd_fmul_to_fmad_or_fma: GICombineRule<
  (defs root:$root, build_fn_matchinfo:$info),
  (match (wip_match_opcode G_FADD):$root,
         [{ return Helper.matchCombineFAddFMulToFMadOrFMA(*${root},
                                                          ${info}); }]),
  (apply [{ Helper.applyBuildFn(*${root}, ${info}); }])>;

// Transform (fadd (fpext (fmul x, y)), z) -> (fma (fpext x), (fpext y), z)
//                                         -> (fmad (fpext x), (fpext y), z)
// Transform (fadd x, (fpext (fmul y, z))) -> (fma (fpext y), (fpext z), x)
//                                         -> (fmad (fpext y), (fpext z), x)
def combine_fadd_fpext_fmul_to_fmad_or_fma: GICombineRule<
  (defs root:$root, build_fn_matchinfo:$info),
  (match (wip_match_opcode G_FADD):$root,
         [{ return Helper.matchCombineFAddFpExtFMulToFMadOrFMA(*${root},
                                                               ${info}); }]),
  (apply [{ Helper.applyBuildFn(*${root}, ${info}); }])>;

// Transform (fadd (fma x, y, (fmul z, u)), v)  -> (fma x, y, (fma z, u, v))
//           (fadd (fmad x, y, (fmul z, u)), v) -> (fmad x, y, (fmad z, u, v))
// Transform (fadd v, (fma x, y, (fmul z, u)))  -> (fma x, y, (fma z, u, v))
//           (fadd v, (fmad x, y, (fmul z, u))) -> (fmad x, y, (fmad z, u, v))
def combine_fadd_fma_fmul_to_fmad_or_fma: GICombineRule<
  (defs root:$root, build_fn_matchinfo:$info),
  (match (wip_match_opcode G_FADD):$root,
         [{ return Helper.matchCombineFAddFMAFMulToFMadOrFMA(*${root},
                                                             ${info}); }]),
  (apply [{ Helper.applyBuildFn(*${root}, ${info}); }])>;

// Transform (fadd (fma x, y, (fpext (fmul u, v))), z) ->
//           (fma x, y, (fma (fpext u), (fpext v), z))
def combine_fadd_fpext_fma_fmul_to_fmad_or_fma: GICombineRule<
  (defs root:$root, build_fn_matchinfo:$info),
  (match (wip_match_opcode G_FADD):$root,
         [{ return Helper.matchCombineFAddFpExtFMulToFMadOrFMAAggressive(
                                                  *${root}, ${info}); }]),
  (apply [{ Helper.applyBuildFn(*${root}, ${info}); }])>;

// Transform (fsub (fmul x, y), z) -> (fma x, y, -z)
//                                 -> (fmad x, y, -z)
def combine_fsub_fmul_to_fmad_or_fma: GICombineRule<
  (defs root:$root, build_fn_matchinfo:$info),
  (match (wip_match_opcode G_FSUB):$root,
         [{ return Helper.matchCombineFSubFMulToFMadOrFMA(*${root},
                                                          ${info}); }]),
  (apply [{ Helper.applyBuildFn(*${root}, ${info}); }])>;

// Transform (fsub (fneg (fmul, x, y)), z) -> (fma (fneg x), y, (fneg z))
//           (fsub x, (fneg (fmul, y, z))) -> (fma y, z, x)
def combine_fsub_fneg_fmul_to_fmad_or_fma: GICombineRule<
  (defs root:$root, build_fn_matchinfo:$info),
  (match (wip_match_opcode G_FSUB):$root,
         [{ return Helper.matchCombineFSubFNegFMulToFMadOrFMA(*${root},
                                                              ${info}); }]),
  (apply [{ Helper.applyBuildFn(*${root}, ${info}); }])>;

// Transform (fsub (fpext (fmul x, y)), z) ->
//           (fma (fpext x), (fpext y), (fneg z))
def combine_fsub_fpext_fmul_to_fmad_or_fma: GICombineRule<
  (defs root:$root, build_fn_matchinfo:$info),
  (match (wip_match_opcode G_FSUB):$root,
         [{ return Helper.matchCombineFSubFpExtFMulToFMadOrFMA(*${root},
                                                               ${info}); }]),
  (apply [{ Helper.applyBuildFn(*${root}, ${info}); }])>;

// Transform (fsub (fneg (fpext (fmul x, y))), z) ->
//           (fneg (fma (fpext x), (fpext y), z))
def combine_fsub_fpext_fneg_fmul_to_fmad_or_fma: GICombineRule<
  (defs root:$root, build_fn_matchinfo:$info),
  (match (wip_match_opcode G_FSUB):$root,
         [{ return Helper.matchCombineFSubFpExtFNegFMulToFMadOrFMA(
                                            *${root}, ${info}); }]),
  (apply [{ Helper.applyBuildFn(*${root}, ${info}); }])>;

def combine_minmax_nan: GICombineRule<
  (defs root:$root, unsigned_matchinfo:$info),
  (match (wip_match_opcode G_FMINNUM, G_FMAXNUM, G_FMINIMUM, G_FMAXIMUM):$root,
         [{ return Helper.matchCombineFMinMaxNaN(*${root}, ${info}); }]),
  (apply [{ Helper.replaceSingleDefInstWithOperand(*${root}, ${info}); }])>;

// Transform (add x, (sub y, x)) -> y
// Transform (add (sub y, x), x) -> y
def add_sub_reg_frags : GICombinePatFrag<
  (outs root:$dst), (ins $src),
  [
    (pattern (G_ADD $dst, $x, $tmp), (G_SUB $tmp, $src, $x)),
    (pattern (G_ADD $dst, $tmp, $x), (G_SUB $tmp, $src, $x))
  ]>;
def add_sub_reg: GICombineRule <
  (defs root:$dst),
  (match (add_sub_reg_frags $dst, $src)),
  (apply (GIReplaceReg $dst, $src))>;

def buildvector_identity_fold : GICombineRule<
  (defs root:$build_vector, register_matchinfo:$matchinfo),
  (match (wip_match_opcode G_BUILD_VECTOR_TRUNC, G_BUILD_VECTOR):$build_vector,
         [{ return Helper.matchBuildVectorIdentityFold(*${build_vector}, ${matchinfo}); }]),
  (apply [{ Helper.replaceSingleDefInstWithReg(*${build_vector}, ${matchinfo}); }])>;

def trunc_buildvector_fold : GICombineRule<
  (defs root:$op, register_matchinfo:$matchinfo),
  (match (wip_match_opcode G_TRUNC):$op,
      [{ return Helper.matchTruncBuildVectorFold(*${op}, ${matchinfo}); }]),
  (apply [{ Helper.replaceSingleDefInstWithReg(*${op}, ${matchinfo}); }])>;

def trunc_lshr_buildvector_fold : GICombineRule<
  (defs root:$op, register_matchinfo:$matchinfo),
  (match (wip_match_opcode G_TRUNC):$op,
      [{ return Helper.matchTruncLshrBuildVectorFold(*${op}, ${matchinfo}); }]),
  (apply [{ Helper.replaceSingleDefInstWithReg(*${op}, ${matchinfo}); }])>;

// Transform:
//   (x + y) - y -> x
//   (x + y) - x -> y
//   x - (y + x) -> 0 - y
//   x - (x + z) -> 0 - z
def sub_add_reg: GICombineRule <
  (defs root:$root, build_fn_matchinfo:$matchinfo),
  (match (wip_match_opcode G_SUB):$root,
         [{ return Helper.matchSubAddSameReg(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.applyBuildFn(*${root}, ${matchinfo}); }])>;

def bitcast_bitcast_fold : GICombineRule<
  (defs root:$dst),
  (match (G_BITCAST $dst, $src1):$op, (G_BITCAST $src1, $src0),
      [{ return MRI.getType(${src0}.getReg()) == MRI.getType(${dst}.getReg()); }]),
  (apply [{ Helper.replaceSingleDefInstWithReg(*${op}, ${src0}.getReg()); }])>;


def fptrunc_fpext_fold : GICombineRule<
  (defs root:$dst),
  (match (G_FPTRUNC $dst, $src1):$op, (G_FPEXT $src1, $src0),
      [{ return MRI.getType(${src0}.getReg()) == MRI.getType(${dst}.getReg()); }]),
  (apply [{ Helper.replaceSingleDefInstWithReg(*${op}, ${src0}.getReg()); }])>;


def select_to_minmax: GICombineRule<
  (defs root:$root, build_fn_matchinfo:$info),
  (match (wip_match_opcode G_SELECT):$root,
         [{ return Helper.matchSimplifySelectToMinMax(*${root}, ${info}); }]),
  (apply [{ Helper.applyBuildFn(*${root}, ${info}); }])>;

def select_to_iminmax: GICombineRule<
  (defs root:$root, build_fn_matchinfo:$info),
  (match (G_ICMP $tst, $tst1, $a, $b),
         (G_SELECT $root, $tst, $x, $y),
         [{ return Helper.matchSelectIMinMax(${root}, ${info}); }]),
  (apply [{ Helper.applyBuildFnMO(${root}, ${info}); }])>;

def match_selects : GICombineRule<
  (defs root:$root, build_fn_matchinfo:$matchinfo),
  (match (wip_match_opcode G_SELECT):$root,
        [{ return Helper.matchSelect(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.applyBuildFn(*${root}, ${matchinfo}); }])>;

def match_ands : GICombineRule<
  (defs root:$root, build_fn_matchinfo:$matchinfo),
  (match (wip_match_opcode G_AND):$root,
        [{ return Helper.matchAnd(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.applyBuildFn(*${root}, ${matchinfo}); }])>;

def match_ors : GICombineRule<
  (defs root:$root, build_fn_matchinfo:$matchinfo),
  (match (wip_match_opcode G_OR):$root,
        [{ return Helper.matchOr(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.applyBuildFn(*${root}, ${matchinfo}); }])>;

def match_addos : GICombineRule<
  (defs root:$root, build_fn_matchinfo:$matchinfo),
  (match (wip_match_opcode G_SADDO, G_UADDO):$root,
        [{ return Helper.matchAddOverflow(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.applyBuildFn(*${root}, ${matchinfo}); }])>;

def match_extract_of_element_undef_vector: GICombineRule <
  (defs root:$root),
  (match (G_IMPLICIT_DEF $vector),
         (G_EXTRACT_VECTOR_ELT $root, $vector, $idx)),
  (apply (G_IMPLICIT_DEF $root))
>;

def match_extract_of_element_undef_index: GICombineRule <
  (defs root:$root),
  (match (G_IMPLICIT_DEF $idx),
         (G_EXTRACT_VECTOR_ELT $root, $vector, $idx)),
  (apply (G_IMPLICIT_DEF $root))
>;

def match_extract_of_element : GICombineRule<
  (defs root:$root, build_fn_matchinfo:$matchinfo),
  (match (wip_match_opcode G_EXTRACT_VECTOR_ELT):$root,
        [{ return Helper.matchExtractVectorElement(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.applyBuildFn(*${root}, ${matchinfo}); }])>;

def extract_vector_element_not_const : GICombineRule<
   (defs root:$root),
   (match (G_INSERT_VECTOR_ELT $src, $x, $value, $idx),
          (G_EXTRACT_VECTOR_ELT $root, $src, $idx)),
   (apply (GIReplaceReg $root, $value))>;

def extract_vector_element_different_indices : GICombineRule<
   (defs root:$root, build_fn_matchinfo:$matchinfo),
   (match (G_INSERT_VECTOR_ELT $src, $x, $value, $idx2),
          (G_EXTRACT_VECTOR_ELT $root, $src, $idx1),
   [{ return Helper.matchExtractVectorElementWithDifferentIndices(${root}, ${matchinfo}); }]),
   (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;

def extract_vector_element_build_vector2 : GICombineRule<
   (defs root:$root, build_fn_matchinfo:$matchinfo),
   (match (G_BUILD_VECTOR $src, $x, $y),
          (G_EXTRACT_VECTOR_ELT $root, $src, $idx),
   [{ return Helper.matchExtractVectorElementWithBuildVector(${root}, ${matchinfo}); }]),
   (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;

def extract_vector_element_build_vector3 : GICombineRule<
   (defs root:$root, build_fn_matchinfo:$matchinfo),
   (match (G_BUILD_VECTOR $src, $x, $y, $z),
          (G_EXTRACT_VECTOR_ELT $root, $src, $idx),
   [{ return Helper.matchExtractVectorElementWithBuildVector(${root}, ${matchinfo}); }]),
   (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;

def extract_vector_element_build_vector4 : GICombineRule<
   (defs root:$root, build_fn_matchinfo:$matchinfo),
   (match (G_BUILD_VECTOR $src, $x, $y, $z, $a),
          (G_EXTRACT_VECTOR_ELT $root, $src, $idx),
   [{ return Helper.matchExtractVectorElementWithBuildVector(${root}, ${matchinfo}); }]),
   (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;

def extract_vector_element_build_vector5 : GICombineRule<
   (defs root:$root, build_fn_matchinfo:$matchinfo),
   (match (G_BUILD_VECTOR $src, $x, $y, $z, $a, $b),
          (G_EXTRACT_VECTOR_ELT $root, $src, $idx),
   [{ return Helper.matchExtractVectorElementWithBuildVector(${root}, ${matchinfo}); }]),
   (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;

def extract_vector_element_build_vector6 : GICombineRule<
   (defs root:$root, build_fn_matchinfo:$matchinfo),
   (match (G_BUILD_VECTOR $src, $x, $y, $z, $a, $b, $c),
          (G_EXTRACT_VECTOR_ELT $root, $src, $idx),
   [{ return Helper.matchExtractVectorElementWithBuildVector(${root}, ${matchinfo}); }]),
   (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;

def extract_vector_element_build_vector7 : GICombineRule<
   (defs root:$root, build_fn_matchinfo:$matchinfo),
   (match (G_BUILD_VECTOR $src, $x, $y, $z, $a, $b, $c, $d),
          (G_EXTRACT_VECTOR_ELT $root, $src, $idx),
   [{ return Helper.matchExtractVectorElementWithBuildVector(${root}, ${matchinfo}); }]),
   (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;

def extract_vector_element_build_vector8 : GICombineRule<
   (defs root:$root, build_fn_matchinfo:$matchinfo),
   (match (G_BUILD_VECTOR $src, $x, $y, $z, $a, $b, $c, $d, $e),
          (G_EXTRACT_VECTOR_ELT $root, $src, $idx),
   [{ return Helper.matchExtractVectorElementWithBuildVector(${root}, ${matchinfo}); }]),
   (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;

def extract_vector_element_build_vector9 : GICombineRule<
   (defs root:$root, build_fn_matchinfo:$matchinfo),
   (match (G_BUILD_VECTOR $src, $x, $y, $z, $a, $b, $c, $d, $e, $f),
          (G_EXTRACT_VECTOR_ELT $root, $src, $idx),
   [{ return Helper.matchExtractVectorElementWithBuildVector(${root}, ${matchinfo}); }]),
   (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;

def extract_vector_element_build_vector10 : GICombineRule<
   (defs root:$root, build_fn_matchinfo:$matchinfo),
   (match (G_BUILD_VECTOR $src, $x, $y, $z, $a, $b, $c, $d, $e, $f, $g),
          (G_EXTRACT_VECTOR_ELT $root, $src, $idx),
   [{ return Helper.matchExtractVectorElementWithBuildVector(${root}, ${matchinfo}); }]),
   (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;

def extract_vector_element_build_vector11 : GICombineRule<
   (defs root:$root, build_fn_matchinfo:$matchinfo),
   (match (G_BUILD_VECTOR $src, $x, $y, $z, $a, $b, $c, $d, $e, $f, $g, $h),
          (G_EXTRACT_VECTOR_ELT $root, $src, $idx),
   [{ return Helper.matchExtractVectorElementWithBuildVector(${root}, ${matchinfo}); }]),
   (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;

def extract_vector_element_build_vector12 : GICombineRule<
   (defs root:$root, build_fn_matchinfo:$matchinfo),
   (match (G_BUILD_VECTOR $src, $x, $y, $z, $a, $b, $c, $d, $e, $f, $g, $h, $i),
          (G_EXTRACT_VECTOR_ELT $root, $src, $idx),
   [{ return Helper.matchExtractVectorElementWithBuildVector(${root}, ${matchinfo}); }]),
   (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;

def extract_vector_element_build_vector13 : GICombineRule<
   (defs root:$root, build_fn_matchinfo:$matchinfo),
   (match (G_BUILD_VECTOR $src, $x, $y, $z, $a, $b, $c, $d, $e, $f, $g, $h, $i, $j),
          (G_EXTRACT_VECTOR_ELT $root, $src, $idx),
   [{ return Helper.matchExtractVectorElementWithBuildVector(${root}, ${matchinfo}); }]),
   (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;

def extract_vector_element_build_vector14 : GICombineRule<
   (defs root:$root, build_fn_matchinfo:$matchinfo),
   (match (G_BUILD_VECTOR $src, $x, $y, $z, $a, $b, $c, $d, $e, $f, $g, $h, $i, $j, $k),
          (G_EXTRACT_VECTOR_ELT $root, $src, $idx),
   [{ return Helper.matchExtractVectorElementWithBuildVector(${root}, ${matchinfo}); }]),
   (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;

def extract_vector_element_build_vector15 : GICombineRule<
   (defs root:$root, build_fn_matchinfo:$matchinfo),
   (match (G_BUILD_VECTOR $src, $x, $y, $z, $a, $b, $c, $d, $e, $f, $g, $h, $i, $j, $k, $l),
          (G_EXTRACT_VECTOR_ELT $root, $src, $idx),
   [{ return Helper.matchExtractVectorElementWithBuildVector(${root}, ${matchinfo}); }]),
   (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;

def extract_vector_element_build_vector16 : GICombineRule<
   (defs root:$root, build_fn_matchinfo:$matchinfo),
   (match (G_BUILD_VECTOR $src, $x, $y, $z, $a, $b, $c, $d, $e, $f, $g, $h, $i, $j, $k, $l, $m),
          (G_EXTRACT_VECTOR_ELT $root, $src, $idx),
   [{ return Helper.matchExtractVectorElementWithBuildVector(${root}, ${matchinfo}); }]),
   (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;

def extract_vector_element_build_vector_trunc2 : GICombineRule<
   (defs root:$root, build_fn_matchinfo:$matchinfo),
   (match (G_BUILD_VECTOR_TRUNC $src, $x, $y),
          (G_EXTRACT_VECTOR_ELT $root, $src, $idx),
   [{ return Helper.matchExtractVectorElementWithBuildVectorTrunc(${root}, ${matchinfo}); }]),
   (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;

def extract_vector_element_build_vector_trunc3 : GICombineRule<
   (defs root:$root, build_fn_matchinfo:$matchinfo),
   (match (G_BUILD_VECTOR_TRUNC $src, $x, $y, $z),
          (G_EXTRACT_VECTOR_ELT $root, $src, $idx),
   [{ return Helper.matchExtractVectorElementWithBuildVectorTrunc(${root}, ${matchinfo}); }]),
   (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;

def extract_vector_element_build_vector_trunc4 : GICombineRule<
   (defs root:$root, build_fn_matchinfo:$matchinfo),
   (match (G_BUILD_VECTOR_TRUNC $src, $x, $y, $z, $a),
          (G_EXTRACT_VECTOR_ELT $root, $src, $idx),
   [{ return Helper.matchExtractVectorElementWithBuildVectorTrunc(${root}, ${matchinfo}); }]),
   (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;

def extract_vector_element_build_vector_trunc5 : GICombineRule<
   (defs root:$root, build_fn_matchinfo:$matchinfo),
   (match (G_BUILD_VECTOR_TRUNC $src, $x, $y, $z, $a, $b),
          (G_EXTRACT_VECTOR_ELT $root, $src, $idx),
   [{ return Helper.matchExtractVectorElementWithBuildVectorTrunc(${root}, ${matchinfo}); }]),
   (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;

def extract_vector_element_build_vector_trunc6 : GICombineRule<
   (defs root:$root, build_fn_matchinfo:$matchinfo),
   (match (G_BUILD_VECTOR_TRUNC $src, $x, $y, $z, $a, $b, $c),
          (G_EXTRACT_VECTOR_ELT $root, $src, $idx),
   [{ return Helper.matchExtractVectorElementWithBuildVectorTrunc(${root}, ${matchinfo}); }]),
   (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;

def extract_vector_element_build_vector_trunc7 : GICombineRule<
   (defs root:$root, build_fn_matchinfo:$matchinfo),
   (match (G_BUILD_VECTOR_TRUNC $src, $x, $y, $z, $a, $b, $c, $d),
          (G_EXTRACT_VECTOR_ELT $root, $src, $idx),
   [{ return Helper.matchExtractVectorElementWithBuildVectorTrunc(${root}, ${matchinfo}); }]),
   (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;

def extract_vector_element_build_vector_trunc8 : GICombineRule<
   (defs root:$root, build_fn_matchinfo:$matchinfo),
   (match (G_BUILD_VECTOR_TRUNC $src, $x, $y, $z, $a, $b, $c, $d, $e),
          (G_EXTRACT_VECTOR_ELT $root, $src, $idx),
   [{ return Helper.matchExtractVectorElementWithBuildVectorTrunc(${root}, ${matchinfo}); }]),
   (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;

def sext_trunc : GICombineRule<
   (defs root:$root, build_fn_matchinfo:$matchinfo),
   (match (G_TRUNC $src, $x, (MIFlags NoSWrap)),
          (G_SEXT $root, $src),
   [{ return Helper.matchSextOfTrunc(${root}, ${matchinfo}); }]),
   (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;

def zext_trunc : GICombineRule<
   (defs root:$root, build_fn_matchinfo:$matchinfo),
   (match (G_TRUNC $src, $x, (MIFlags NoUWrap)),
          (G_ZEXT $root, $src),
   [{ return Helper.matchZextOfTrunc(${root}, ${matchinfo}); }]),
   (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;

def nneg_zext : GICombineRule<
   (defs root:$root, build_fn_matchinfo:$matchinfo),
   (match (G_ZEXT $root, $x, (MIFlags NonNeg)),
   [{ return Helper.matchNonNegZext(${root}, ${matchinfo}); }]),
   (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;

def extract_vector_element_shuffle_vector : GICombineRule<
   (defs root:$root, build_fn_matchinfo:$matchinfo),
   (match (G_SHUFFLE_VECTOR $src, $src1, $src2, $mask),
          (G_EXTRACT_VECTOR_ELT $root, $src, $idx),
   [{ return Helper.matchExtractVectorElementWithShuffleVector(${root}, ${matchinfo}); }]),
   (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;

// Combines concat operations
def concat_matchinfo : GIDefMatchData<"SmallVector<Register>">;
def combine_concat_vector : GICombineRule<
  (defs root:$root, concat_matchinfo:$matchinfo),
  (match (wip_match_opcode G_CONCAT_VECTORS):$root,
        [{ return Helper.matchCombineConcatVectors(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.applyCombineConcatVectors(*${root}, ${matchinfo}); }])>;

// Combines Shuffles of Concats
// a = G_CONCAT_VECTORS x, y, undef, undef
// b = G_CONCAT_VECTORS z, undef, undef, undef
// c = G_SHUFFLE_VECTORS a, b, <0, 1, 4, undef>
// ===>
// c = G_CONCAT_VECTORS x, y, z, undef
def combine_shuffle_concat : GICombineRule<
  (defs root:$root, concat_matchinfo:$matchinfo),
  (match (wip_match_opcode G_SHUFFLE_VECTOR):$root,
        [{ return Helper.matchCombineShuffleConcat(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.applyCombineShuffleConcat(*${root}, ${matchinfo}); }])>;

def insert_vector_element_idx_undef : GICombineRule<
   (defs root:$root),
   (match (G_IMPLICIT_DEF $idx),
          (G_INSERT_VECTOR_ELT $root, $src, $elt, $idx)),
   (apply (G_IMPLICIT_DEF $root))>;

def insert_vector_element_elt_undef : GICombineRule<
   (defs root:$root),
   (match (G_IMPLICIT_DEF $elt),
          (G_INSERT_VECTOR_ELT $root, $src, $elt, $idx),
          [{ return isGuaranteedNotToBePoison(${src}.getReg(), MRI); }]),
   (apply (GIReplaceReg $root, $src))>;

def insert_vector_element_extract_vector_element : GICombineRule<
   (defs root:$root),
   (match (G_EXTRACT_VECTOR_ELT $elt, $src, $idx),
          (G_INSERT_VECTOR_ELT $root, $src, $elt, $idx)),
   (apply (GIReplaceReg $root, $src))>;

def insert_vector_elt_oob : GICombineRule<
  (defs root:$root, build_fn_matchinfo:$matchinfo),
  (match (wip_match_opcode G_INSERT_VECTOR_ELT):$root,
         [{ return Helper.matchInsertVectorElementOOB(*${root}, ${matchinfo}); }]),
  (apply [{ Helper.applyBuildFn(*${root}, ${matchinfo}); }])>;

def add_of_vscale : GICombineRule<
   (defs root:$root, build_fn_matchinfo:$matchinfo),
   (match (G_VSCALE $left, $imm1),
          (G_VSCALE $right, $imm2),
          (G_ADD $root, $left, $right, (MIFlags NoSWrap)),
   [{ return Helper.matchAddOfVScale(${root}, ${matchinfo}); }]),
   (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;

def mul_of_vscale : GICombineRule<
   (defs root:$root, build_fn_matchinfo:$matchinfo),
   (match (G_VSCALE $left, $scale),
          (G_CONSTANT $x, $imm1),
          (G_MUL $root, $left, $x, (MIFlags NoSWrap)),
   [{ return Helper.matchMulOfVScale(${root}, ${matchinfo}); }]),
   (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;

def shl_of_vscale : GICombineRule<
   (defs root:$root, build_fn_matchinfo:$matchinfo),
   (match (G_VSCALE $left, $imm),
          (G_CONSTANT $x, $imm1),
          (G_SHL $root, $left, $x, (MIFlags NoSWrap)),
   [{ return Helper.matchShlOfVScale(${root}, ${matchinfo}); }]),
   (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;

def sub_of_vscale : GICombineRule<
   (defs root:$root, build_fn_matchinfo:$matchinfo),
   (match (G_VSCALE $right, $imm),
          (G_SUB $root, $x, $right, (MIFlags NoSWrap)),
   [{ return Helper.matchSubOfVScale(${root}, ${matchinfo}); }]),
   (apply [{ Helper.applyBuildFnMO(${root}, ${matchinfo}); }])>;

def expand_const_fpowi : GICombineRule<
   (defs root:$root),
   (match (G_CONSTANT $int, $imm),
          (G_FPOWI $dst, $float, $int):$root,
          [{ return Helper.matchFPowIExpansion(*${root}, ${imm}.getCImm()->getSExtValue()); }]),
   (apply [{ Helper.applyExpandFPowI(*${root}, ${imm}.getCImm()->getSExtValue()); }])>;

// match_extract_of_element and insert_vector_elt_oob must be the first!
def vector_ops_combines: GICombineGroup<[
match_extract_of_element_undef_vector,
match_extract_of_element_undef_index,
insert_vector_element_idx_undef,
insert_vector_element_elt_undef,
match_extract_of_element,
insert_vector_elt_oob,
extract_vector_element_not_const,
extract_vector_element_different_indices,
extract_vector_element_build_vector2,
extract_vector_element_build_vector3,
extract_vector_element_build_vector4,
extract_vector_element_build_vector5,
extract_vector_element_build_vector7,
extract_vector_element_build_vector8,
extract_vector_element_build_vector9,
extract_vector_element_build_vector10,
extract_vector_element_build_vector11,
extract_vector_element_build_vector12,
extract_vector_element_build_vector13,
extract_vector_element_build_vector14,
extract_vector_element_build_vector15,
extract_vector_element_build_vector16,
extract_vector_element_build_vector_trunc2,
extract_vector_element_build_vector_trunc3,
extract_vector_element_build_vector_trunc4,
extract_vector_element_build_vector_trunc5,
extract_vector_element_build_vector_trunc6,
extract_vector_element_build_vector_trunc7,
extract_vector_element_build_vector_trunc8,
extract_vector_element_shuffle_vector,
insert_vector_element_extract_vector_element,
add_of_vscale,
mul_of_vscale,
shl_of_vscale,
sub_of_vscale,
]>;


// fold ((0-A) + B) -> B-A
def ZeroMinusAPlusB : GICombineRule<
   (defs root:$root),
   (match (G_SUB $sub, 0, $A),
          (G_ADD $root, $sub, $B)),
   (apply (G_SUB $root, $B, $A))>;

// fold (A + (0-B)) -> A-B
def APlusZeroMinusB : GICombineRule<
   (defs root:$root),
   (match (G_SUB $sub, 0, $B),
          (G_ADD $root, $A, $sub)),
   (apply (G_SUB $root, $A, $B))>;

 // fold (A+(B-A)) -> B
 def APlusBMinusB : GICombineRule<
   (defs root:$root),
   (match (G_SUB $sub, $B, $A),
          (G_ADD $root, $A, $sub)),
   (apply (GIReplaceReg $root, $B))>;

// fold ((B-A)+A) -> B
 def BMinusAPlusA : GICombineRule<
   (defs root:$root),
   (match (G_SUB $sub, $B, $A),
          (G_ADD $root, $sub, $A)),
   (apply (GIReplaceReg $root, $B))>;

// fold ((A-B)+(C-A)) -> (C-B)
def AMinusBPlusCMinusA : GICombineRule<
   (defs root:$root),
   (match (G_SUB $sub1, $A, $B),
          (G_SUB $sub2, $C, $A),
          (G_ADD $root, $sub1, $sub2)),
   (apply (G_SUB $root, $C, $B))>;

// fold ((A-B)+(B-C)) -> (A-C)
def AMinusBPlusBMinusC : GICombineRule<
   (defs root:$root),
   (match (G_SUB $sub1, $A, $B),
          (G_SUB $sub2, $B, $C),
          (G_ADD $root, $sub1, $sub2)),
   (apply (G_SUB $root, $A, $C))>;

// fold (A+(B-(A+C))) to (B-C)
def APlusBMinusAplusC : GICombineRule<
   (defs root:$root),
   (match (G_ADD $add1, $A, $C),
          (G_SUB $sub1, $B, $add1),
          (G_ADD $root, $A, $sub1)),
   (apply (G_SUB $root, $B, $C))>;

// fold (A+(B-(C+A))) to (B-C)
def APlusBMinusCPlusA : GICombineRule<
   (defs root:$root),
   (match (G_ADD $add1, $C, $A),
          (G_SUB $sub1, $B, $add1),
          (G_ADD $root, $A, $sub1)),
   (apply (G_SUB $root, $B, $C))>;

def integer_reassoc_combines: GICombineGroup<[
  ZeroMinusAPlusB,
  APlusZeroMinusB,
  APlusBMinusB,
  BMinusAPlusA,
  AMinusBPlusCMinusA,
  AMinusBPlusBMinusC,
  APlusBMinusAplusC,
  APlusBMinusCPlusA
]>;

def freeze_of_non_undef_non_poison : GICombineRule<
   (defs root:$root),
   (match (G_FREEZE $root, $src),
          [{ return isGuaranteedNotToBeUndefOrPoison(${src}.getReg(), MRI); }]),
   (apply (GIReplaceReg $root, $src))>;

def freeze_combines: GICombineGroup<[
  freeze_of_non_undef_non_poison,
  push_freeze_to_prevent_poison_from_propagating
]>;

// FIXME: These should use the custom predicate feature once it lands.
def undef_combines : GICombineGroup<[undef_to_fp_zero, undef_to_int_zero,
                                     undef_to_negative_one,
                                     binop_left_undef_to_zero,
                                     binop_right_undef_to_undef,
                                     unary_undef_to_zero,
                                     propagate_undef_any_op,
                                     propagate_undef_all_ops,
                                     propagate_undef_shuffle_mask,
                                     erase_undef_store,
                                     unmerge_undef,
                                     insert_extract_vec_elt_out_of_bounds]>;

def identity_combines : GICombineGroup<[select_same_val, right_identity_zero,
                                        binop_same_val, binop_left_to_zero,
                                        binop_right_to_zero, p2i_to_i2p,
                                        i2p_to_p2i, anyext_trunc_fold,
                                        fneg_fneg_fold, right_identity_one,
                                        add_sub_reg, buildvector_identity_fold,
                                        trunc_buildvector_fold,
                                        trunc_lshr_buildvector_fold,
                                        bitcast_bitcast_fold, fptrunc_fpext_fold,
                                        right_identity_neg_zero_fp,
                                        right_identity_neg_one_fp]>;

def const_combines : GICombineGroup<[constant_fold_fp_ops, const_ptradd_to_i2p,
                                     overlapping_and, mulo_by_2, mulo_by_0,
                                     adde_to_addo,
                                     combine_minmax_nan, expand_const_fpowi]>;

def known_bits_simplifications : GICombineGroup<[
  redundant_and, redundant_sext_inreg, redundant_or, urem_pow2_to_mask,
  zext_trunc_fold, icmp_to_true_false_known_bits, icmp_to_lhs_known_bits,
  sext_inreg_to_zext_inreg]>;

def width_reduction_combines : GICombineGroup<[reduce_shl_of_extend,
                                               narrow_binop_feeding_and]>;

def phi_combines : GICombineGroup<[extend_through_phis]>;

def bitreverse_shift : GICombineGroup<[bitreverse_shl, bitreverse_lshr]>;

def select_combines : GICombineGroup<[select_undef_cmp, select_constant_cmp,
                                      select_to_iminmax, match_selects]>;

def trivial_combines : GICombineGroup<[copy_prop, mul_to_shl, add_p2i_to_ptradd,
                                       mul_by_neg_one, idempotent_prop]>;

def fma_combines : GICombineGroup<[combine_fadd_fmul_to_fmad_or_fma,
  combine_fadd_fpext_fmul_to_fmad_or_fma, combine_fadd_fma_fmul_to_fmad_or_fma,
  combine_fadd_fpext_fma_fmul_to_fmad_or_fma, combine_fsub_fmul_to_fmad_or_fma,
  combine_fsub_fneg_fmul_to_fmad_or_fma, combine_fsub_fpext_fmul_to_fmad_or_fma,
  combine_fsub_fpext_fneg_fmul_to_fmad_or_fma]>;

def constant_fold_binops : GICombineGroup<[constant_fold_binop,
                                           constant_fold_fp_binop]>;

def prefer_sign_combines : GICombineGroup<[nneg_zext]>;

def all_combines : GICombineGroup<[integer_reassoc_combines, trivial_combines,
    vector_ops_combines, freeze_combines,
    insert_vec_elt_combines, extract_vec_elt_combines, combines_for_extload,
    combine_extracted_vector_load,
    undef_combines, identity_combines, phi_combines,
    simplify_add_to_sub, hoist_logic_op_with_same_opcode_hands, shifts_too_big,
    reassocs, ptr_add_immed_chain,
    shl_ashr_to_sext_inreg, sext_inreg_of_load,
    width_reduction_combines, select_combines,
    known_bits_simplifications, ext_ext_fold,
    not_cmp_fold, opt_brcond_by_inverting_cond,
    unmerge_merge, unmerge_cst, unmerge_dead_to_trunc,
    unmerge_zext_to_zext, merge_unmerge, trunc_ext_fold, trunc_shift,
    const_combines, xor_of_and_with_same_reg, ptr_add_with_zero,
    shift_immed_chain, shift_of_shifted_logic_chain, load_or_combine,
    div_rem_to_divrem, funnel_shift_combines, bitreverse_shift, commute_shift,
    form_bitfield_extract, constant_fold_binops, constant_fold_fma,
    constant_fold_cast_op, fabs_fneg_fold,
    intdiv_combines, mulh_combines, redundant_neg_operands,
    and_or_disjoint_mask, fma_combines, fold_binop_into_select,
    sub_add_reg, select_to_minmax, redundant_binop_in_equality,
    fsub_to_fneg, commute_constant_to_rhs, match_ands, match_ors,
    combine_concat_vector, double_icmp_zero_and_or_combine, match_addos,
    sext_trunc, zext_trunc, prefer_sign_combines, combine_shuffle_concat]>;

// A combine group used to for prelegalizer combiners at -O0. The combines in
// this group have been selected based on experiments to balance code size and
// compile time performance.
def optnone_combines : GICombineGroup<[trivial_combines,
    ptr_add_immed_chain, combines_for_extload,
    not_cmp_fold, opt_brcond_by_inverting_cond, combine_concat_vector]>;

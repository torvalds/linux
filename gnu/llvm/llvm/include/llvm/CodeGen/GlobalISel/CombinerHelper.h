//===-- llvm/CodeGen/GlobalISel/CombinerHelper.h --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===--------------------------------------------------------------------===//
/// \file
/// This contains common combine transformations that may be used in a combine
/// pass,or by the target elsewhere.
/// Targets can pick individual opcode transformations from the helper or use
/// tryCombine which invokes all transformations. All of the transformations
/// return true if the MachineInstruction changed and false otherwise.
///
//===--------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_COMBINERHELPER_H
#define LLVM_CODEGEN_GLOBALISEL_COMBINERHELPER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/GlobalISel/GenericMachineInstrs.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGenTypes/LowLevelType.h"
#include "llvm/IR/InstrTypes.h"
#include <functional>

namespace llvm {

class GISelChangeObserver;
class APInt;
class ConstantFP;
class GPtrAdd;
class GZExtLoad;
class MachineIRBuilder;
class MachineInstrBuilder;
class MachineRegisterInfo;
class MachineInstr;
class MachineOperand;
class GISelKnownBits;
class MachineDominatorTree;
class LegalizerInfo;
struct LegalityQuery;
class RegisterBank;
class RegisterBankInfo;
class TargetLowering;
class TargetRegisterInfo;

struct PreferredTuple {
  LLT Ty;                // The result type of the extend.
  unsigned ExtendOpcode; // G_ANYEXT/G_SEXT/G_ZEXT
  MachineInstr *MI;
};

struct IndexedLoadStoreMatchInfo {
  Register Addr;
  Register Base;
  Register Offset;
  bool RematOffset = false; // True if Offset is a constant that needs to be
                            // rematerialized before the new load/store.
  bool IsPre = false;
};

struct PtrAddChain {
  int64_t Imm;
  Register Base;
  const RegisterBank *Bank;
};

struct RegisterImmPair {
  Register Reg;
  int64_t Imm;
};

struct ShiftOfShiftedLogic {
  MachineInstr *Logic;
  MachineInstr *Shift2;
  Register LogicNonShiftReg;
  uint64_t ValSum;
};

using BuildFnTy = std::function<void(MachineIRBuilder &)>;

using OperandBuildSteps =
    SmallVector<std::function<void(MachineInstrBuilder &)>, 4>;
struct InstructionBuildSteps {
  unsigned Opcode = 0;          /// The opcode for the produced instruction.
  OperandBuildSteps OperandFns; /// Operands to be added to the instruction.
  InstructionBuildSteps() = default;
  InstructionBuildSteps(unsigned Opcode, const OperandBuildSteps &OperandFns)
      : Opcode(Opcode), OperandFns(OperandFns) {}
};

struct InstructionStepsMatchInfo {
  /// Describes instructions to be built during a combine.
  SmallVector<InstructionBuildSteps, 2> InstrsToBuild;
  InstructionStepsMatchInfo() = default;
  InstructionStepsMatchInfo(
      std::initializer_list<InstructionBuildSteps> InstrsToBuild)
      : InstrsToBuild(InstrsToBuild) {}
};

class CombinerHelper {
protected:
  MachineIRBuilder &Builder;
  MachineRegisterInfo &MRI;
  GISelChangeObserver &Observer;
  GISelKnownBits *KB;
  MachineDominatorTree *MDT;
  bool IsPreLegalize;
  const LegalizerInfo *LI;
  const RegisterBankInfo *RBI;
  const TargetRegisterInfo *TRI;

public:
  CombinerHelper(GISelChangeObserver &Observer, MachineIRBuilder &B,
                 bool IsPreLegalize,
                 GISelKnownBits *KB = nullptr,
                 MachineDominatorTree *MDT = nullptr,
                 const LegalizerInfo *LI = nullptr);

  GISelKnownBits *getKnownBits() const {
    return KB;
  }

  MachineIRBuilder &getBuilder() const {
    return Builder;
  }

  const TargetLowering &getTargetLowering() const;

  /// \returns true if the combiner is running pre-legalization.
  bool isPreLegalize() const;

  /// \returns true if \p Query is legal on the target.
  bool isLegal(const LegalityQuery &Query) const;

  /// \return true if the combine is running prior to legalization, or if \p
  /// Query is legal on the target.
  bool isLegalOrBeforeLegalizer(const LegalityQuery &Query) const;

  /// \return true if the combine is running prior to legalization, or if \p Ty
  /// is a legal integer constant type on the target.
  bool isConstantLegalOrBeforeLegalizer(const LLT Ty) const;

  /// MachineRegisterInfo::replaceRegWith() and inform the observer of the changes
  void replaceRegWith(MachineRegisterInfo &MRI, Register FromReg, Register ToReg) const;

  /// Replace a single register operand with a new register and inform the
  /// observer of the changes.
  void replaceRegOpWith(MachineRegisterInfo &MRI, MachineOperand &FromRegOp,
                        Register ToReg) const;

  /// Replace the opcode in instruction with a new opcode and inform the
  /// observer of the changes.
  void replaceOpcodeWith(MachineInstr &FromMI, unsigned ToOpcode) const;

  /// Get the register bank of \p Reg.
  /// If Reg has not been assigned a register, a register class,
  /// or a register bank, then this returns nullptr.
  ///
  /// \pre Reg.isValid()
  const RegisterBank *getRegBank(Register Reg) const;

  /// Set the register bank of \p Reg.
  /// Does nothing if the RegBank is null.
  /// This is the counterpart to getRegBank.
  void setRegBank(Register Reg, const RegisterBank *RegBank);

  /// If \p MI is COPY, try to combine it.
  /// Returns true if MI changed.
  bool tryCombineCopy(MachineInstr &MI);
  bool matchCombineCopy(MachineInstr &MI);
  void applyCombineCopy(MachineInstr &MI);

  /// Returns true if \p DefMI precedes \p UseMI or they are the same
  /// instruction. Both must be in the same basic block.
  bool isPredecessor(const MachineInstr &DefMI, const MachineInstr &UseMI);

  /// Returns true if \p DefMI dominates \p UseMI. By definition an
  /// instruction dominates itself.
  ///
  /// If we haven't been provided with a MachineDominatorTree during
  /// construction, this function returns a conservative result that tracks just
  /// a single basic block.
  bool dominates(const MachineInstr &DefMI, const MachineInstr &UseMI);

  /// If \p MI is extend that consumes the result of a load, try to combine it.
  /// Returns true if MI changed.
  bool tryCombineExtendingLoads(MachineInstr &MI);
  bool matchCombineExtendingLoads(MachineInstr &MI, PreferredTuple &MatchInfo);
  void applyCombineExtendingLoads(MachineInstr &MI, PreferredTuple &MatchInfo);

  /// Match (and (load x), mask) -> zextload x
  bool matchCombineLoadWithAndMask(MachineInstr &MI, BuildFnTy &MatchInfo);

  /// Combine a G_EXTRACT_VECTOR_ELT of a load into a narrowed
  /// load.
  bool matchCombineExtractedVectorLoad(MachineInstr &MI, BuildFnTy &MatchInfo);

  bool matchCombineIndexedLoadStore(MachineInstr &MI, IndexedLoadStoreMatchInfo &MatchInfo);
  void applyCombineIndexedLoadStore(MachineInstr &MI, IndexedLoadStoreMatchInfo &MatchInfo);

  bool matchSextTruncSextLoad(MachineInstr &MI);
  void applySextTruncSextLoad(MachineInstr &MI);

  /// Match sext_inreg(load p), imm -> sextload p
  bool matchSextInRegOfLoad(MachineInstr &MI, std::tuple<Register, unsigned> &MatchInfo);
  void applySextInRegOfLoad(MachineInstr &MI, std::tuple<Register, unsigned> &MatchInfo);

  /// Try to combine G_[SU]DIV and G_[SU]REM into a single G_[SU]DIVREM
  /// when their source operands are identical.
  bool matchCombineDivRem(MachineInstr &MI, MachineInstr *&OtherMI);
  void applyCombineDivRem(MachineInstr &MI, MachineInstr *&OtherMI);

  /// If a brcond's true block is not the fallthrough, make it so by inverting
  /// the condition and swapping operands.
  bool matchOptBrCondByInvertingCond(MachineInstr &MI, MachineInstr *&BrCond);
  void applyOptBrCondByInvertingCond(MachineInstr &MI, MachineInstr *&BrCond);

  /// If \p MI is G_CONCAT_VECTORS, try to combine it.
  /// Returns true if MI changed.
  /// Right now, we support:
  /// - concat_vector(undef, undef) => undef
  /// - concat_vector(build_vector(A, B), build_vector(C, D)) =>
  ///   build_vector(A, B, C, D)
  /// ==========================================================
  /// Check if the G_CONCAT_VECTORS \p MI is undef or if it
  /// can be flattened into a build_vector.
  /// In the first case \p Ops will be empty
  /// In the second case \p Ops will contain the operands
  /// needed to produce the flattened build_vector.
  ///
  /// \pre MI.getOpcode() == G_CONCAT_VECTORS.
  bool matchCombineConcatVectors(MachineInstr &MI, SmallVector<Register> &Ops);
  /// Replace \p MI with a flattened build_vector with \p Ops
  /// or an implicit_def if \p Ops is empty.
  void applyCombineConcatVectors(MachineInstr &MI, SmallVector<Register> &Ops);

  bool matchCombineShuffleConcat(MachineInstr &MI, SmallVector<Register> &Ops);
  /// Replace \p MI with a flattened build_vector with \p Ops
  /// or an implicit_def if \p Ops is empty.
  void applyCombineShuffleConcat(MachineInstr &MI, SmallVector<Register> &Ops);

  /// Try to combine G_SHUFFLE_VECTOR into G_CONCAT_VECTORS.
  /// Returns true if MI changed.
  ///
  /// \pre MI.getOpcode() == G_SHUFFLE_VECTOR.
  bool tryCombineShuffleVector(MachineInstr &MI);
  /// Check if the G_SHUFFLE_VECTOR \p MI can be replaced by a
  /// concat_vectors.
  /// \p Ops will contain the operands needed to produce the flattened
  /// concat_vectors.
  ///
  /// \pre MI.getOpcode() == G_SHUFFLE_VECTOR.
  bool matchCombineShuffleVector(MachineInstr &MI,
                                 SmallVectorImpl<Register> &Ops);
  /// Replace \p MI with a concat_vectors with \p Ops.
  void applyCombineShuffleVector(MachineInstr &MI,
                                 const ArrayRef<Register> Ops);
  bool matchShuffleToExtract(MachineInstr &MI);
  void applyShuffleToExtract(MachineInstr &MI);

  /// Optimize memcpy intrinsics et al, e.g. constant len calls.
  /// /p MaxLen if non-zero specifies the max length of a mem libcall to inline.
  ///
  /// For example (pre-indexed):
  ///
  ///     $addr = G_PTR_ADD $base, $offset
  ///     [...]
  ///     $val = G_LOAD $addr
  ///     [...]
  ///     $whatever = COPY $addr
  ///
  /// -->
  ///
  ///     $val, $addr = G_INDEXED_LOAD $base, $offset, 1 (IsPre)
  ///     [...]
  ///     $whatever = COPY $addr
  ///
  /// or (post-indexed):
  ///
  ///     G_STORE $val, $base
  ///     [...]
  ///     $addr = G_PTR_ADD $base, $offset
  ///     [...]
  ///     $whatever = COPY $addr
  ///
  /// -->
  ///
  ///     $addr = G_INDEXED_STORE $val, $base, $offset
  ///     [...]
  ///     $whatever = COPY $addr
  bool tryCombineMemCpyFamily(MachineInstr &MI, unsigned MaxLen = 0);

  bool matchPtrAddImmedChain(MachineInstr &MI, PtrAddChain &MatchInfo);
  void applyPtrAddImmedChain(MachineInstr &MI, PtrAddChain &MatchInfo);

  /// Fold (shift (shift base, x), y) -> (shift base (x+y))
  bool matchShiftImmedChain(MachineInstr &MI, RegisterImmPair &MatchInfo);
  void applyShiftImmedChain(MachineInstr &MI, RegisterImmPair &MatchInfo);

  /// If we have a shift-by-constant of a bitwise logic op that itself has a
  /// shift-by-constant operand with identical opcode, we may be able to convert
  /// that into 2 independent shifts followed by the logic op.
  bool matchShiftOfShiftedLogic(MachineInstr &MI,
                                ShiftOfShiftedLogic &MatchInfo);
  void applyShiftOfShiftedLogic(MachineInstr &MI,
                                ShiftOfShiftedLogic &MatchInfo);

  bool matchCommuteShift(MachineInstr &MI, BuildFnTy &MatchInfo);

  /// Transform a multiply by a power-of-2 value to a left shift.
  bool matchCombineMulToShl(MachineInstr &MI, unsigned &ShiftVal);
  void applyCombineMulToShl(MachineInstr &MI, unsigned &ShiftVal);

  // Transform a G_SHL with an extended source into a narrower shift if
  // possible.
  bool matchCombineShlOfExtend(MachineInstr &MI, RegisterImmPair &MatchData);
  void applyCombineShlOfExtend(MachineInstr &MI,
                               const RegisterImmPair &MatchData);

  /// Fold away a merge of an unmerge of the corresponding values.
  bool matchCombineMergeUnmerge(MachineInstr &MI, Register &MatchInfo);

  /// Reduce a shift by a constant to an unmerge and a shift on a half sized
  /// type. This will not produce a shift smaller than \p TargetShiftSize.
  bool matchCombineShiftToUnmerge(MachineInstr &MI, unsigned TargetShiftSize,
                                 unsigned &ShiftVal);
  void applyCombineShiftToUnmerge(MachineInstr &MI, const unsigned &ShiftVal);
  bool tryCombineShiftToUnmerge(MachineInstr &MI, unsigned TargetShiftAmount);

  /// Transform <ty,...> G_UNMERGE(G_MERGE ty X, Y, Z) -> ty X, Y, Z.
  bool
  matchCombineUnmergeMergeToPlainValues(MachineInstr &MI,
                                        SmallVectorImpl<Register> &Operands);
  void
  applyCombineUnmergeMergeToPlainValues(MachineInstr &MI,
                                        SmallVectorImpl<Register> &Operands);

  /// Transform G_UNMERGE Constant -> Constant1, Constant2, ...
  bool matchCombineUnmergeConstant(MachineInstr &MI,
                                   SmallVectorImpl<APInt> &Csts);
  void applyCombineUnmergeConstant(MachineInstr &MI,
                                   SmallVectorImpl<APInt> &Csts);

  /// Transform G_UNMERGE G_IMPLICIT_DEF -> G_IMPLICIT_DEF, G_IMPLICIT_DEF, ...
  bool
  matchCombineUnmergeUndef(MachineInstr &MI,
                           std::function<void(MachineIRBuilder &)> &MatchInfo);

  /// Transform X, Y<dead> = G_UNMERGE Z -> X = G_TRUNC Z.
  bool matchCombineUnmergeWithDeadLanesToTrunc(MachineInstr &MI);
  void applyCombineUnmergeWithDeadLanesToTrunc(MachineInstr &MI);

  /// Transform X, Y = G_UNMERGE(G_ZEXT(Z)) -> X = G_ZEXT(Z); Y = G_CONSTANT 0
  bool matchCombineUnmergeZExtToZExt(MachineInstr &MI);
  void applyCombineUnmergeZExtToZExt(MachineInstr &MI);

  /// Transform fp_instr(cst) to constant result of the fp operation.
  void applyCombineConstantFoldFpUnary(MachineInstr &MI, const ConstantFP *Cst);

  /// Transform IntToPtr(PtrToInt(x)) to x if cast is in the same address space.
  bool matchCombineI2PToP2I(MachineInstr &MI, Register &Reg);
  void applyCombineI2PToP2I(MachineInstr &MI, Register &Reg);

  /// Transform PtrToInt(IntToPtr(x)) to x.
  void applyCombineP2IToI2P(MachineInstr &MI, Register &Reg);

  /// Transform G_ADD (G_PTRTOINT x), y -> G_PTRTOINT (G_PTR_ADD x, y)
  /// Transform G_ADD y, (G_PTRTOINT x) -> G_PTRTOINT (G_PTR_ADD x, y)
  bool matchCombineAddP2IToPtrAdd(MachineInstr &MI,
                                  std::pair<Register, bool> &PtrRegAndCommute);
  void applyCombineAddP2IToPtrAdd(MachineInstr &MI,
                                  std::pair<Register, bool> &PtrRegAndCommute);

  // Transform G_PTR_ADD (G_PTRTOINT C1), C2 -> C1 + C2
  bool matchCombineConstPtrAddToI2P(MachineInstr &MI, APInt &NewCst);
  void applyCombineConstPtrAddToI2P(MachineInstr &MI, APInt &NewCst);

  /// Transform anyext(trunc(x)) to x.
  bool matchCombineAnyExtTrunc(MachineInstr &MI, Register &Reg);

  /// Transform zext(trunc(x)) to x.
  bool matchCombineZextTrunc(MachineInstr &MI, Register &Reg);

  /// Transform [asz]ext([asz]ext(x)) to [asz]ext x.
  bool matchCombineExtOfExt(MachineInstr &MI,
                            std::tuple<Register, unsigned> &MatchInfo);
  void applyCombineExtOfExt(MachineInstr &MI,
                            std::tuple<Register, unsigned> &MatchInfo);

  /// Transform trunc ([asz]ext x) to x or ([asz]ext x) or (trunc x).
  bool matchCombineTruncOfExt(MachineInstr &MI,
                              std::pair<Register, unsigned> &MatchInfo);
  void applyCombineTruncOfExt(MachineInstr &MI,
                              std::pair<Register, unsigned> &MatchInfo);

  /// Transform trunc (shl x, K) to shl (trunc x), K
  ///    if K < VT.getScalarSizeInBits().
  ///
  /// Transforms trunc ([al]shr x, K) to (trunc ([al]shr (MidVT (trunc x)), K))
  ///    if K <= (MidVT.getScalarSizeInBits() - VT.getScalarSizeInBits())
  /// MidVT is obtained by finding a legal type between the trunc's src and dst
  /// types.
  bool matchCombineTruncOfShift(MachineInstr &MI,
                                std::pair<MachineInstr *, LLT> &MatchInfo);
  void applyCombineTruncOfShift(MachineInstr &MI,
                                std::pair<MachineInstr *, LLT> &MatchInfo);

  /// Return true if any explicit use operand on \p MI is defined by a
  /// G_IMPLICIT_DEF.
  bool matchAnyExplicitUseIsUndef(MachineInstr &MI);

  /// Return true if all register explicit use operands on \p MI are defined by
  /// a G_IMPLICIT_DEF.
  bool matchAllExplicitUsesAreUndef(MachineInstr &MI);

  /// Return true if a G_SHUFFLE_VECTOR instruction \p MI has an undef mask.
  bool matchUndefShuffleVectorMask(MachineInstr &MI);

  /// Return true if a G_STORE instruction \p MI is storing an undef value.
  bool matchUndefStore(MachineInstr &MI);

  /// Return true if a G_SELECT instruction \p MI has an undef comparison.
  bool matchUndefSelectCmp(MachineInstr &MI);

  /// Return true if a G_{EXTRACT,INSERT}_VECTOR_ELT has an out of range index.
  bool matchInsertExtractVecEltOutOfBounds(MachineInstr &MI);

  /// Return true if a G_SELECT instruction \p MI has a constant comparison. If
  /// true, \p OpIdx will store the operand index of the known selected value.
  bool matchConstantSelectCmp(MachineInstr &MI, unsigned &OpIdx);

  /// Replace an instruction with a G_FCONSTANT with value \p C.
  void replaceInstWithFConstant(MachineInstr &MI, double C);

  /// Replace an instruction with an G_FCONSTANT with value \p CFP.
  void replaceInstWithFConstant(MachineInstr &MI, ConstantFP *CFP);

  /// Replace an instruction with a G_CONSTANT with value \p C.
  void replaceInstWithConstant(MachineInstr &MI, int64_t C);

  /// Replace an instruction with a G_CONSTANT with value \p C.
  void replaceInstWithConstant(MachineInstr &MI, APInt C);

  /// Replace an instruction with a G_IMPLICIT_DEF.
  void replaceInstWithUndef(MachineInstr &MI);

  /// Delete \p MI and replace all of its uses with its \p OpIdx-th operand.
  void replaceSingleDefInstWithOperand(MachineInstr &MI, unsigned OpIdx);

  /// Delete \p MI and replace all of its uses with \p Replacement.
  void replaceSingleDefInstWithReg(MachineInstr &MI, Register Replacement);

  /// @brief Replaces the shift amount in \p MI with ShiftAmt % BW
  /// @param MI
  void applyFunnelShiftConstantModulo(MachineInstr &MI);

  /// Return true if \p MOP1 and \p MOP2 are register operands are defined by
  /// equivalent instructions.
  bool matchEqualDefs(const MachineOperand &MOP1, const MachineOperand &MOP2);

  /// Return true if \p MOP is defined by a G_CONSTANT or splat with a value equal to
  /// \p C.
  bool matchConstantOp(const MachineOperand &MOP, int64_t C);

  /// Return true if \p MOP is defined by a G_FCONSTANT or splat with a value exactly
  /// equal to \p C.
  bool matchConstantFPOp(const MachineOperand &MOP, double C);

  /// @brief Checks if constant at \p ConstIdx is larger than \p MI 's bitwidth
  /// @param ConstIdx Index of the constant
  bool matchConstantLargerBitWidth(MachineInstr &MI, unsigned ConstIdx);

  /// Optimize (cond ? x : x) -> x
  bool matchSelectSameVal(MachineInstr &MI);

  /// Optimize (x op x) -> x
  bool matchBinOpSameVal(MachineInstr &MI);

  /// Check if operand \p OpIdx is zero.
  bool matchOperandIsZero(MachineInstr &MI, unsigned OpIdx);

  /// Check if operand \p OpIdx is undef.
  bool matchOperandIsUndef(MachineInstr &MI, unsigned OpIdx);

  /// Check if operand \p OpIdx is known to be a power of 2.
  bool matchOperandIsKnownToBeAPowerOfTwo(MachineInstr &MI, unsigned OpIdx);

  /// Erase \p MI
  void eraseInst(MachineInstr &MI);

  /// Return true if MI is a G_ADD which can be simplified to a G_SUB.
  bool matchSimplifyAddToSub(MachineInstr &MI,
                             std::tuple<Register, Register> &MatchInfo);
  void applySimplifyAddToSub(MachineInstr &MI,
                             std::tuple<Register, Register> &MatchInfo);

  /// Match (logic_op (op x...), (op y...)) -> (op (logic_op x, y))
  bool
  matchHoistLogicOpWithSameOpcodeHands(MachineInstr &MI,
                                       InstructionStepsMatchInfo &MatchInfo);

  /// Replace \p MI with a series of instructions described in \p MatchInfo.
  void applyBuildInstructionSteps(MachineInstr &MI,
                                  InstructionStepsMatchInfo &MatchInfo);

  /// Match ashr (shl x, C), C -> sext_inreg (C)
  bool matchAshrShlToSextInreg(MachineInstr &MI,
                               std::tuple<Register, int64_t> &MatchInfo);
  void applyAshShlToSextInreg(MachineInstr &MI,
                              std::tuple<Register, int64_t> &MatchInfo);

  /// Fold and(and(x, C1), C2) -> C1&C2 ? and(x, C1&C2) : 0
  bool matchOverlappingAnd(MachineInstr &MI,
                           BuildFnTy &MatchInfo);

  /// \return true if \p MI is a G_AND instruction whose operands are x and y
  /// where x & y == x or x & y == y. (E.g., one of operands is all-ones value.)
  ///
  /// \param [in] MI - The G_AND instruction.
  /// \param [out] Replacement - A register the G_AND should be replaced with on
  /// success.
  bool matchRedundantAnd(MachineInstr &MI, Register &Replacement);

  /// \return true if \p MI is a G_OR instruction whose operands are x and y
  /// where x | y == x or x | y == y. (E.g., one of operands is all-zeros
  /// value.)
  ///
  /// \param [in] MI - The G_OR instruction.
  /// \param [out] Replacement - A register the G_OR should be replaced with on
  /// success.
  bool matchRedundantOr(MachineInstr &MI, Register &Replacement);

  /// \return true if \p MI is a G_SEXT_INREG that can be erased.
  bool matchRedundantSExtInReg(MachineInstr &MI);

  /// Combine inverting a result of a compare into the opposite cond code.
  bool matchNotCmp(MachineInstr &MI, SmallVectorImpl<Register> &RegsToNegate);
  void applyNotCmp(MachineInstr &MI, SmallVectorImpl<Register> &RegsToNegate);

  /// Fold (xor (and x, y), y) -> (and (not x), y)
  ///{
  bool matchXorOfAndWithSameReg(MachineInstr &MI,
                                std::pair<Register, Register> &MatchInfo);
  void applyXorOfAndWithSameReg(MachineInstr &MI,
                                std::pair<Register, Register> &MatchInfo);
  ///}

  /// Combine G_PTR_ADD with nullptr to G_INTTOPTR
  bool matchPtrAddZero(MachineInstr &MI);
  void applyPtrAddZero(MachineInstr &MI);

  /// Combine G_UREM x, (known power of 2) to an add and bitmasking.
  void applySimplifyURemByPow2(MachineInstr &MI);

  /// Push a binary operator through a select on constants.
  ///
  /// binop (select cond, K0, K1), K2 ->
  ///   select cond, (binop K0, K2), (binop K1, K2)
  bool matchFoldBinOpIntoSelect(MachineInstr &MI, unsigned &SelectOpNo);
  void applyFoldBinOpIntoSelect(MachineInstr &MI, const unsigned &SelectOpNo);

  bool matchCombineInsertVecElts(MachineInstr &MI,
                                 SmallVectorImpl<Register> &MatchInfo);

  void applyCombineInsertVecElts(MachineInstr &MI,
                             SmallVectorImpl<Register> &MatchInfo);

  /// Match expression trees of the form
  ///
  /// \code
  ///  sN *a = ...
  ///  sM val = a[0] | (a[1] << N) | (a[2] << 2N) | (a[3] << 3N) ...
  /// \endcode
  ///
  /// And check if the tree can be replaced with a M-bit load + possibly a
  /// bswap.
  bool matchLoadOrCombine(MachineInstr &MI, BuildFnTy &MatchInfo);

  bool matchExtendThroughPhis(MachineInstr &MI, MachineInstr *&ExtMI);
  void applyExtendThroughPhis(MachineInstr &MI, MachineInstr *&ExtMI);

  bool matchExtractVecEltBuildVec(MachineInstr &MI, Register &Reg);
  void applyExtractVecEltBuildVec(MachineInstr &MI, Register &Reg);

  bool matchExtractAllEltsFromBuildVector(
      MachineInstr &MI,
      SmallVectorImpl<std::pair<Register, MachineInstr *>> &MatchInfo);
  void applyExtractAllEltsFromBuildVector(
      MachineInstr &MI,
      SmallVectorImpl<std::pair<Register, MachineInstr *>> &MatchInfo);

  /// Use a function which takes in a MachineIRBuilder to perform a combine.
  /// By default, it erases the instruction \p MI from the function.
  void applyBuildFn(MachineInstr &MI, BuildFnTy &MatchInfo);
  /// Use a function which takes in a MachineIRBuilder to perform a combine.
  /// This variant does not erase \p MI after calling the build function.
  void applyBuildFnNoErase(MachineInstr &MI, BuildFnTy &MatchInfo);

  bool matchOrShiftToFunnelShift(MachineInstr &MI, BuildFnTy &MatchInfo);
  bool matchFunnelShiftToRotate(MachineInstr &MI);
  void applyFunnelShiftToRotate(MachineInstr &MI);
  bool matchRotateOutOfRange(MachineInstr &MI);
  void applyRotateOutOfRange(MachineInstr &MI);

  /// \returns true if a G_ICMP instruction \p MI can be replaced with a true
  /// or false constant based off of KnownBits information.
  bool matchICmpToTrueFalseKnownBits(MachineInstr &MI, int64_t &MatchInfo);

  /// \returns true if a G_ICMP \p MI can be replaced with its LHS based off of
  /// KnownBits information.
  bool
  matchICmpToLHSKnownBits(MachineInstr &MI,
                          BuildFnTy &MatchInfo);

  /// \returns true if (and (or x, c1), c2) can be replaced with (and x, c2)
  bool matchAndOrDisjointMask(MachineInstr &MI, BuildFnTy &MatchInfo);

  bool matchBitfieldExtractFromSExtInReg(MachineInstr &MI,
                                         BuildFnTy &MatchInfo);
  /// Match: and (lshr x, cst), mask -> ubfx x, cst, width
  bool matchBitfieldExtractFromAnd(MachineInstr &MI, BuildFnTy &MatchInfo);

  /// Match: shr (shl x, n), k -> sbfx/ubfx x, pos, width
  bool matchBitfieldExtractFromShr(MachineInstr &MI, BuildFnTy &MatchInfo);

  /// Match: shr (and x, n), k -> ubfx x, pos, width
  bool matchBitfieldExtractFromShrAnd(MachineInstr &MI, BuildFnTy &MatchInfo);

  // Helpers for reassociation:
  bool matchReassocConstantInnerRHS(GPtrAdd &MI, MachineInstr *RHS,
                                    BuildFnTy &MatchInfo);
  bool matchReassocFoldConstantsInSubTree(GPtrAdd &MI, MachineInstr *LHS,
                                          MachineInstr *RHS,
                                          BuildFnTy &MatchInfo);
  bool matchReassocConstantInnerLHS(GPtrAdd &MI, MachineInstr *LHS,
                                    MachineInstr *RHS, BuildFnTy &MatchInfo);
  /// Reassociate pointer calculations with G_ADD involved, to allow better
  /// addressing mode usage.
  bool matchReassocPtrAdd(MachineInstr &MI, BuildFnTy &MatchInfo);

  /// Try to reassociate to reassociate operands of a commutative binop.
  bool tryReassocBinOp(unsigned Opc, Register DstReg, Register Op0,
                       Register Op1, BuildFnTy &MatchInfo);
  /// Reassociate commutative binary operations like G_ADD.
  bool matchReassocCommBinOp(MachineInstr &MI, BuildFnTy &MatchInfo);

  /// Do constant folding when opportunities are exposed after MIR building.
  bool matchConstantFoldCastOp(MachineInstr &MI, APInt &MatchInfo);

  /// Do constant folding when opportunities are exposed after MIR building.
  bool matchConstantFoldBinOp(MachineInstr &MI, APInt &MatchInfo);

  /// Do constant FP folding when opportunities are exposed after MIR building.
  bool matchConstantFoldFPBinOp(MachineInstr &MI, ConstantFP* &MatchInfo);

  /// Constant fold G_FMA/G_FMAD.
  bool matchConstantFoldFMA(MachineInstr &MI, ConstantFP *&MatchInfo);

  /// \returns true if it is possible to narrow the width of a scalar binop
  /// feeding a G_AND instruction \p MI.
  bool matchNarrowBinopFeedingAnd(MachineInstr &MI, BuildFnTy &MatchInfo);

  /// Given an G_UDIV \p MI expressing a divide by constant, return an
  /// expression that implements it by multiplying by a magic number.
  /// Ref: "Hacker's Delight" or "The PowerPC Compiler Writer's Guide".
  MachineInstr *buildUDivUsingMul(MachineInstr &MI);
  /// Combine G_UDIV by constant into a multiply by magic constant.
  bool matchUDivByConst(MachineInstr &MI);
  void applyUDivByConst(MachineInstr &MI);

  /// Given an G_SDIV \p MI expressing a signed divide by constant, return an
  /// expression that implements it by multiplying by a magic number.
  /// Ref: "Hacker's Delight" or "The PowerPC Compiler Writer's Guide".
  MachineInstr *buildSDivUsingMul(MachineInstr &MI);
  bool matchSDivByConst(MachineInstr &MI);
  void applySDivByConst(MachineInstr &MI);

  /// Given an G_SDIV \p MI expressing a signed divided by a pow2 constant,
  /// return expressions that implements it by shifting.
  bool matchDivByPow2(MachineInstr &MI, bool IsSigned);
  void applySDivByPow2(MachineInstr &MI);
  /// Given an G_UDIV \p MI expressing an unsigned divided by a pow2 constant,
  /// return expressions that implements it by shifting.
  void applyUDivByPow2(MachineInstr &MI);

  // G_UMULH x, (1 << c)) -> x >> (bitwidth - c)
  bool matchUMulHToLShr(MachineInstr &MI);
  void applyUMulHToLShr(MachineInstr &MI);

  /// Try to transform \p MI by using all of the above
  /// combine functions. Returns true if changed.
  bool tryCombine(MachineInstr &MI);

  /// Emit loads and stores that perform the given memcpy.
  /// Assumes \p MI is a G_MEMCPY_INLINE
  /// TODO: implement dynamically sized inline memcpy,
  ///       and rename: s/bool tryEmit/void emit/
  bool tryEmitMemcpyInline(MachineInstr &MI);

  /// Match:
  ///   (G_UMULO x, 2) -> (G_UADDO x, x)
  ///   (G_SMULO x, 2) -> (G_SADDO x, x)
  bool matchMulOBy2(MachineInstr &MI, BuildFnTy &MatchInfo);

  /// Match:
  /// (G_*MULO x, 0) -> 0 + no carry out
  bool matchMulOBy0(MachineInstr &MI, BuildFnTy &MatchInfo);

  /// Match:
  /// (G_*ADDE x, y, 0) -> (G_*ADDO x, y)
  /// (G_*SUBE x, y, 0) -> (G_*SUBO x, y)
  bool matchAddEToAddO(MachineInstr &MI, BuildFnTy &MatchInfo);

  /// Transform (fadd x, fneg(y)) -> (fsub x, y)
  ///           (fadd fneg(x), y) -> (fsub y, x)
  ///           (fsub x, fneg(y)) -> (fadd x, y)
  ///           (fmul fneg(x), fneg(y)) -> (fmul x, y)
  ///           (fdiv fneg(x), fneg(y)) -> (fdiv x, y)
  ///           (fmad fneg(x), fneg(y), z) -> (fmad x, y, z)
  ///           (fma fneg(x), fneg(y), z) -> (fma x, y, z)
  bool matchRedundantNegOperands(MachineInstr &MI, BuildFnTy &MatchInfo);

  bool matchFsubToFneg(MachineInstr &MI, Register &MatchInfo);
  void applyFsubToFneg(MachineInstr &MI, Register &MatchInfo);

  bool canCombineFMadOrFMA(MachineInstr &MI, bool &AllowFusionGlobally,
                           bool &HasFMAD, bool &Aggressive,
                           bool CanReassociate = false);

  /// Transform (fadd (fmul x, y), z) -> (fma x, y, z)
  ///           (fadd (fmul x, y), z) -> (fmad x, y, z)
  bool matchCombineFAddFMulToFMadOrFMA(MachineInstr &MI, BuildFnTy &MatchInfo);

  /// Transform (fadd (fpext (fmul x, y)), z) -> (fma (fpext x), (fpext y), z)
  ///           (fadd (fpext (fmul x, y)), z) -> (fmad (fpext x), (fpext y), z)
  bool matchCombineFAddFpExtFMulToFMadOrFMA(MachineInstr &MI,
                                            BuildFnTy &MatchInfo);

  /// Transform (fadd (fma x, y, (fmul u, v)), z) -> (fma x, y, (fma u, v, z))
  ///          (fadd (fmad x, y, (fmul u, v)), z) -> (fmad x, y, (fmad u, v, z))
  bool matchCombineFAddFMAFMulToFMadOrFMA(MachineInstr &MI,
                                          BuildFnTy &MatchInfo);

  // Transform (fadd (fma x, y, (fpext (fmul u, v))), z)
  //            -> (fma x, y, (fma (fpext u), (fpext v), z))
  //           (fadd (fmad x, y, (fpext (fmul u, v))), z)
  //            -> (fmad x, y, (fmad (fpext u), (fpext v), z))
  bool matchCombineFAddFpExtFMulToFMadOrFMAAggressive(MachineInstr &MI,
                                                      BuildFnTy &MatchInfo);

  /// Transform (fsub (fmul x, y), z) -> (fma x, y, -z)
  ///           (fsub (fmul x, y), z) -> (fmad x, y, -z)
  bool matchCombineFSubFMulToFMadOrFMA(MachineInstr &MI, BuildFnTy &MatchInfo);

  /// Transform (fsub (fneg (fmul, x, y)), z) -> (fma (fneg x), y, (fneg z))
  ///           (fsub (fneg (fmul, x, y)), z) -> (fmad (fneg x), y, (fneg z))
  bool matchCombineFSubFNegFMulToFMadOrFMA(MachineInstr &MI,
                                           BuildFnTy &MatchInfo);

  /// Transform (fsub (fpext (fmul x, y)), z)
  ///           -> (fma (fpext x), (fpext y), (fneg z))
  ///           (fsub (fpext (fmul x, y)), z)
  ///           -> (fmad (fpext x), (fpext y), (fneg z))
  bool matchCombineFSubFpExtFMulToFMadOrFMA(MachineInstr &MI,
                                            BuildFnTy &MatchInfo);

  /// Transform (fsub (fpext (fneg (fmul x, y))), z)
  ///           -> (fneg (fma (fpext x), (fpext y), z))
  ///           (fsub (fpext (fneg (fmul x, y))), z)
  ///           -> (fneg (fmad (fpext x), (fpext y), z))
  bool matchCombineFSubFpExtFNegFMulToFMadOrFMA(MachineInstr &MI,
                                                BuildFnTy &MatchInfo);

  bool matchCombineFMinMaxNaN(MachineInstr &MI, unsigned &Info);

  /// Transform G_ADD(x, G_SUB(y, x)) to y.
  /// Transform G_ADD(G_SUB(y, x), x) to y.
  bool matchAddSubSameReg(MachineInstr &MI, Register &Src);

  bool matchBuildVectorIdentityFold(MachineInstr &MI, Register &MatchInfo);
  bool matchTruncBuildVectorFold(MachineInstr &MI, Register &MatchInfo);
  bool matchTruncLshrBuildVectorFold(MachineInstr &MI, Register &MatchInfo);

  /// Transform:
  ///   (x + y) - y -> x
  ///   (x + y) - x -> y
  ///   x - (y + x) -> 0 - y
  ///   x - (x + z) -> 0 - z
  bool matchSubAddSameReg(MachineInstr &MI, BuildFnTy &MatchInfo);

  /// \returns true if it is possible to simplify a select instruction \p MI
  /// to a min/max instruction of some sort.
  bool matchSimplifySelectToMinMax(MachineInstr &MI, BuildFnTy &MatchInfo);

  /// Transform:
  ///   (X + Y) == X -> Y == 0
  ///   (X - Y) == X -> Y == 0
  ///   (X ^ Y) == X -> Y == 0
  ///   (X + Y) != X -> Y != 0
  ///   (X - Y) != X -> Y != 0
  ///   (X ^ Y) != X -> Y != 0
  bool matchRedundantBinOpInEquality(MachineInstr &MI, BuildFnTy &MatchInfo);

  /// Match shifts greater or equal to the bitwidth of the operation.
  bool matchShiftsTooBig(MachineInstr &MI);

  /// Match constant LHS ops that should be commuted.
  bool matchCommuteConstantToRHS(MachineInstr &MI);

  /// Combine sext of trunc.
  bool matchSextOfTrunc(const MachineOperand &MO, BuildFnTy &MatchInfo);

  /// Combine zext of trunc.
  bool matchZextOfTrunc(const MachineOperand &MO, BuildFnTy &MatchInfo);

  /// Combine zext nneg to sext.
  bool matchNonNegZext(const MachineOperand &MO, BuildFnTy &MatchInfo);

  /// Match constant LHS FP ops that should be commuted.
  bool matchCommuteFPConstantToRHS(MachineInstr &MI);

  // Given a binop \p MI, commute operands 1 and 2.
  void applyCommuteBinOpOperands(MachineInstr &MI);

  /// Combine select to integer min/max.
  bool matchSelectIMinMax(const MachineOperand &MO, BuildFnTy &MatchInfo);

  /// Combine selects.
  bool matchSelect(MachineInstr &MI, BuildFnTy &MatchInfo);

  /// Combine ands.
  bool matchAnd(MachineInstr &MI, BuildFnTy &MatchInfo);

  /// Combine ors.
  bool matchOr(MachineInstr &MI, BuildFnTy &MatchInfo);

  /// Combine addos.
  bool matchAddOverflow(MachineInstr &MI, BuildFnTy &MatchInfo);

  /// Combine extract vector element.
  bool matchExtractVectorElement(MachineInstr &MI, BuildFnTy &MatchInfo);

  /// Combine extract vector element with a build vector on the vector register.
  bool matchExtractVectorElementWithBuildVector(const MachineOperand &MO,
                                                BuildFnTy &MatchInfo);

  /// Combine extract vector element with a build vector trunc on the vector
  /// register.
  bool matchExtractVectorElementWithBuildVectorTrunc(const MachineOperand &MO,
                                                     BuildFnTy &MatchInfo);

  /// Combine extract vector element with a shuffle vector on the vector
  /// register.
  bool matchExtractVectorElementWithShuffleVector(const MachineOperand &MO,
                                                  BuildFnTy &MatchInfo);

  /// Combine extract vector element with a insert vector element on the vector
  /// register and different indices.
  bool matchExtractVectorElementWithDifferentIndices(const MachineOperand &MO,
                                                     BuildFnTy &MatchInfo);
  /// Use a function which takes in a MachineIRBuilder to perform a combine.
  /// By default, it erases the instruction def'd on \p MO from the function.
  void applyBuildFnMO(const MachineOperand &MO, BuildFnTy &MatchInfo);

  /// Match FPOWI if it's safe to extend it into a series of multiplications.
  bool matchFPowIExpansion(MachineInstr &MI, int64_t Exponent);

  /// Expands FPOWI into a series of multiplications and a division if the
  /// exponent is negative.
  void applyExpandFPowI(MachineInstr &MI, int64_t Exponent);

  /// Combine insert vector element OOB.
  bool matchInsertVectorElementOOB(MachineInstr &MI, BuildFnTy &MatchInfo);

  bool matchFreezeOfSingleMaybePoisonOperand(MachineInstr &MI,
                                             BuildFnTy &MatchInfo);

  bool matchAddOfVScale(const MachineOperand &MO, BuildFnTy &MatchInfo);

  bool matchMulOfVScale(const MachineOperand &MO, BuildFnTy &MatchInfo);

  bool matchSubOfVScale(const MachineOperand &MO, BuildFnTy &MatchInfo);

  bool matchShlOfVScale(const MachineOperand &MO, BuildFnTy &MatchInfo);

private:
  /// Checks for legality of an indexed variant of \p LdSt.
  bool isIndexedLoadStoreLegal(GLoadStore &LdSt) const;
  /// Given a non-indexed load or store instruction \p MI, find an offset that
  /// can be usefully and legally folded into it as a post-indexing operation.
  ///
  /// \returns true if a candidate is found.
  bool findPostIndexCandidate(GLoadStore &MI, Register &Addr, Register &Base,
                              Register &Offset, bool &RematOffset);

  /// Given a non-indexed load or store instruction \p MI, find an offset that
  /// can be usefully and legally folded into it as a pre-indexing operation.
  ///
  /// \returns true if a candidate is found.
  bool findPreIndexCandidate(GLoadStore &MI, Register &Addr, Register &Base,
                             Register &Offset);

  /// Helper function for matchLoadOrCombine. Searches for Registers
  /// which may have been produced by a load instruction + some arithmetic.
  ///
  /// \param [in] Root - The search root.
  ///
  /// \returns The Registers found during the search.
  std::optional<SmallVector<Register, 8>>
  findCandidatesForLoadOrCombine(const MachineInstr *Root) const;

  /// Helper function for matchLoadOrCombine.
  ///
  /// Checks if every register in \p RegsToVisit is defined by a load
  /// instruction + some arithmetic.
  ///
  /// \param [out] MemOffset2Idx - Maps the byte positions each load ends up
  /// at to the index of the load.
  /// \param [in] MemSizeInBits - The number of bits each load should produce.
  ///
  /// \returns On success, a 3-tuple containing lowest-index load found, the
  /// lowest index, and the last load in the sequence.
  std::optional<std::tuple<GZExtLoad *, int64_t, GZExtLoad *>>
  findLoadOffsetsForLoadOrCombine(
      SmallDenseMap<int64_t, int64_t, 8> &MemOffset2Idx,
      const SmallVector<Register, 8> &RegsToVisit,
      const unsigned MemSizeInBits);

  /// Examines the G_PTR_ADD instruction \p PtrAdd and determines if performing
  /// a re-association of its operands would break an existing legal addressing
  /// mode that the address computation currently represents.
  bool reassociationCanBreakAddressingModePattern(MachineInstr &PtrAdd);

  /// Behavior when a floating point min/max is given one NaN and one
  /// non-NaN as input.
  enum class SelectPatternNaNBehaviour {
    NOT_APPLICABLE = 0, /// NaN behavior not applicable.
    RETURNS_NAN,        /// Given one NaN input, returns the NaN.
    RETURNS_OTHER,      /// Given one NaN input, returns the non-NaN.
    RETURNS_ANY /// Given one NaN input, can return either (or both operands are
                /// known non-NaN.)
  };

  /// \returns which of \p LHS and \p RHS would be the result of a non-equality
  /// floating point comparison where one of \p LHS and \p RHS may be NaN.
  ///
  /// If both \p LHS and \p RHS may be NaN, returns
  /// SelectPatternNaNBehaviour::NOT_APPLICABLE.
  SelectPatternNaNBehaviour
  computeRetValAgainstNaN(Register LHS, Register RHS,
                          bool IsOrderedComparison) const;

  /// Determines the floating point min/max opcode which should be used for
  /// a G_SELECT fed by a G_FCMP with predicate \p Pred.
  ///
  /// \returns 0 if this G_SELECT should not be combined to a floating point
  /// min or max. If it should be combined, returns one of
  ///
  /// * G_FMAXNUM
  /// * G_FMAXIMUM
  /// * G_FMINNUM
  /// * G_FMINIMUM
  ///
  /// Helper function for matchFPSelectToMinMax.
  unsigned getFPMinMaxOpcForSelect(CmpInst::Predicate Pred, LLT DstTy,
                                   SelectPatternNaNBehaviour VsNaNRetVal) const;

  /// Handle floating point cases for matchSimplifySelectToMinMax.
  ///
  /// E.g.
  ///
  /// select (fcmp uge x, 1.0) x, 1.0 -> fmax x, 1.0
  /// select (fcmp uge x, 1.0) 1.0, x -> fminnm x, 1.0
  bool matchFPSelectToMinMax(Register Dst, Register Cond, Register TrueVal,
                             Register FalseVal, BuildFnTy &MatchInfo);

  /// Try to fold selects to logical operations.
  bool tryFoldBoolSelectToLogic(GSelect *Select, BuildFnTy &MatchInfo);

  bool tryFoldSelectOfConstants(GSelect *Select, BuildFnTy &MatchInfo);

  bool isOneOrOneSplat(Register Src, bool AllowUndefs);
  bool isZeroOrZeroSplat(Register Src, bool AllowUndefs);
  bool isConstantSplatVector(Register Src, int64_t SplatValue,
                             bool AllowUndefs);
  bool isConstantOrConstantVectorI(Register Src) const;

  std::optional<APInt> getConstantOrConstantSplatVector(Register Src);

  /// Fold (icmp Pred1 V1, C1) && (icmp Pred2 V2, C2)
  /// or   (icmp Pred1 V1, C1) || (icmp Pred2 V2, C2)
  /// into a single comparison using range-based reasoning.
  bool tryFoldAndOrOrICmpsUsingRanges(GLogicalBinOp *Logic,
                                      BuildFnTy &MatchInfo);

  // Simplify (cmp cc0 x, y) (&& or ||) (cmp cc1 x, y) -> cmp cc2 x, y.
  bool tryFoldLogicOfFCmps(GLogicalBinOp *Logic, BuildFnTy &MatchInfo);
};
} // namespace llvm

#endif

//==-- llvm/CodeGen/GlobalISel/Utils.h ---------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file This file declares the API of helper functions used throughout the
/// GlobalISel pipeline.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_UTILS_H
#define LLVM_CODEGEN_GLOBALISEL_UTILS_H

#include "GISelWorkList.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGenTypes/LowLevelType.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Casting.h"
#include <cstdint>

namespace llvm {

class AnalysisUsage;
class LostDebugLocObserver;
class MachineBasicBlock;
class BlockFrequencyInfo;
class GISelKnownBits;
class MachineFunction;
class MachineInstr;
class MachineIRBuilder;
class MachineOperand;
class MachineOptimizationRemarkEmitter;
class MachineOptimizationRemarkMissed;
struct MachinePointerInfo;
class MachineRegisterInfo;
class MCInstrDesc;
class ProfileSummaryInfo;
class RegisterBankInfo;
class TargetInstrInfo;
class TargetLowering;
class TargetPassConfig;
class TargetRegisterInfo;
class TargetRegisterClass;
class ConstantFP;
class APFloat;

// Convenience macros for dealing with vector reduction opcodes.
#define GISEL_VECREDUCE_CASES_ALL                                              \
  case TargetOpcode::G_VECREDUCE_SEQ_FADD:                                     \
  case TargetOpcode::G_VECREDUCE_SEQ_FMUL:                                     \
  case TargetOpcode::G_VECREDUCE_FADD:                                         \
  case TargetOpcode::G_VECREDUCE_FMUL:                                         \
  case TargetOpcode::G_VECREDUCE_FMAX:                                         \
  case TargetOpcode::G_VECREDUCE_FMIN:                                         \
  case TargetOpcode::G_VECREDUCE_FMAXIMUM:                                     \
  case TargetOpcode::G_VECREDUCE_FMINIMUM:                                     \
  case TargetOpcode::G_VECREDUCE_ADD:                                          \
  case TargetOpcode::G_VECREDUCE_MUL:                                          \
  case TargetOpcode::G_VECREDUCE_AND:                                          \
  case TargetOpcode::G_VECREDUCE_OR:                                           \
  case TargetOpcode::G_VECREDUCE_XOR:                                          \
  case TargetOpcode::G_VECREDUCE_SMAX:                                         \
  case TargetOpcode::G_VECREDUCE_SMIN:                                         \
  case TargetOpcode::G_VECREDUCE_UMAX:                                         \
  case TargetOpcode::G_VECREDUCE_UMIN:

#define GISEL_VECREDUCE_CASES_NONSEQ                                           \
  case TargetOpcode::G_VECREDUCE_FADD:                                         \
  case TargetOpcode::G_VECREDUCE_FMUL:                                         \
  case TargetOpcode::G_VECREDUCE_FMAX:                                         \
  case TargetOpcode::G_VECREDUCE_FMIN:                                         \
  case TargetOpcode::G_VECREDUCE_FMAXIMUM:                                     \
  case TargetOpcode::G_VECREDUCE_FMINIMUM:                                     \
  case TargetOpcode::G_VECREDUCE_ADD:                                          \
  case TargetOpcode::G_VECREDUCE_MUL:                                          \
  case TargetOpcode::G_VECREDUCE_AND:                                          \
  case TargetOpcode::G_VECREDUCE_OR:                                           \
  case TargetOpcode::G_VECREDUCE_XOR:                                          \
  case TargetOpcode::G_VECREDUCE_SMAX:                                         \
  case TargetOpcode::G_VECREDUCE_SMIN:                                         \
  case TargetOpcode::G_VECREDUCE_UMAX:                                         \
  case TargetOpcode::G_VECREDUCE_UMIN:

/// Try to constrain Reg to the specified register class. If this fails,
/// create a new virtual register in the correct class.
///
/// \return The virtual register constrained to the right register class.
Register constrainRegToClass(MachineRegisterInfo &MRI,
                             const TargetInstrInfo &TII,
                             const RegisterBankInfo &RBI, Register Reg,
                             const TargetRegisterClass &RegClass);

/// Constrain the Register operand OpIdx, so that it is now constrained to the
/// TargetRegisterClass passed as an argument (RegClass).
/// If this fails, create a new virtual register in the correct class and insert
/// a COPY before \p InsertPt if it is a use or after if it is a definition.
/// In both cases, the function also updates the register of RegMo. The debug
/// location of \p InsertPt is used for the new copy.
///
/// \return The virtual register constrained to the right register class.
Register constrainOperandRegClass(const MachineFunction &MF,
                                  const TargetRegisterInfo &TRI,
                                  MachineRegisterInfo &MRI,
                                  const TargetInstrInfo &TII,
                                  const RegisterBankInfo &RBI,
                                  MachineInstr &InsertPt,
                                  const TargetRegisterClass &RegClass,
                                  MachineOperand &RegMO);

/// Try to constrain Reg so that it is usable by argument OpIdx of the provided
/// MCInstrDesc \p II. If this fails, create a new virtual register in the
/// correct class and insert a COPY before \p InsertPt if it is a use or after
/// if it is a definition. In both cases, the function also updates the register
/// of RegMo.
/// This is equivalent to constrainOperandRegClass(..., RegClass, ...)
/// with RegClass obtained from the MCInstrDesc. The debug location of \p
/// InsertPt is used for the new copy.
///
/// \return The virtual register constrained to the right register class.
Register constrainOperandRegClass(const MachineFunction &MF,
                                  const TargetRegisterInfo &TRI,
                                  MachineRegisterInfo &MRI,
                                  const TargetInstrInfo &TII,
                                  const RegisterBankInfo &RBI,
                                  MachineInstr &InsertPt, const MCInstrDesc &II,
                                  MachineOperand &RegMO, unsigned OpIdx);

/// Mutate the newly-selected instruction \p I to constrain its (possibly
/// generic) virtual register operands to the instruction's register class.
/// This could involve inserting COPYs before (for uses) or after (for defs).
/// This requires the number of operands to match the instruction description.
/// \returns whether operand regclass constraining succeeded.
///
// FIXME: Not all instructions have the same number of operands. We should
// probably expose a constrain helper per operand and let the target selector
// constrain individual registers, like fast-isel.
bool constrainSelectedInstRegOperands(MachineInstr &I,
                                      const TargetInstrInfo &TII,
                                      const TargetRegisterInfo &TRI,
                                      const RegisterBankInfo &RBI);

/// Check if DstReg can be replaced with SrcReg depending on the register
/// constraints.
bool canReplaceReg(Register DstReg, Register SrcReg, MachineRegisterInfo &MRI);

/// Check whether an instruction \p MI is dead: it only defines dead virtual
/// registers, and doesn't have other side effects.
bool isTriviallyDead(const MachineInstr &MI, const MachineRegisterInfo &MRI);

/// Report an ISel error as a missed optimization remark to the LLVMContext's
/// diagnostic stream.  Set the FailedISel MachineFunction property.
void reportGISelFailure(MachineFunction &MF, const TargetPassConfig &TPC,
                        MachineOptimizationRemarkEmitter &MORE,
                        MachineOptimizationRemarkMissed &R);

void reportGISelFailure(MachineFunction &MF, const TargetPassConfig &TPC,
                        MachineOptimizationRemarkEmitter &MORE,
                        const char *PassName, StringRef Msg,
                        const MachineInstr &MI);

/// Report an ISel warning as a missed optimization remark to the LLVMContext's
/// diagnostic stream.
void reportGISelWarning(MachineFunction &MF, const TargetPassConfig &TPC,
                        MachineOptimizationRemarkEmitter &MORE,
                        MachineOptimizationRemarkMissed &R);

/// If \p VReg is defined by a G_CONSTANT, return the corresponding value.
std::optional<APInt> getIConstantVRegVal(Register VReg,
                                         const MachineRegisterInfo &MRI);

/// If \p VReg is defined by a G_CONSTANT fits in int64_t returns it.
std::optional<int64_t> getIConstantVRegSExtVal(Register VReg,
                                               const MachineRegisterInfo &MRI);

/// Simple struct used to hold a constant integer value and a virtual
/// register.
struct ValueAndVReg {
  APInt Value;
  Register VReg;
};

/// If \p VReg is defined by a statically evaluable chain of instructions rooted
/// on a G_CONSTANT returns its APInt value and def register.
std::optional<ValueAndVReg>
getIConstantVRegValWithLookThrough(Register VReg,
                                   const MachineRegisterInfo &MRI,
                                   bool LookThroughInstrs = true);

/// If \p VReg is defined by a statically evaluable chain of instructions rooted
/// on a G_CONSTANT or G_FCONSTANT returns its value as APInt and def register.
std::optional<ValueAndVReg> getAnyConstantVRegValWithLookThrough(
    Register VReg, const MachineRegisterInfo &MRI,
    bool LookThroughInstrs = true, bool LookThroughAnyExt = false);

struct FPValueAndVReg {
  APFloat Value;
  Register VReg;
};

/// If \p VReg is defined by a statically evaluable chain of instructions rooted
/// on a G_FCONSTANT returns its APFloat value and def register.
std::optional<FPValueAndVReg>
getFConstantVRegValWithLookThrough(Register VReg,
                                   const MachineRegisterInfo &MRI,
                                   bool LookThroughInstrs = true);

const ConstantFP* getConstantFPVRegVal(Register VReg,
                                       const MachineRegisterInfo &MRI);

/// See if Reg is defined by an single def instruction that is
/// Opcode. Also try to do trivial folding if it's a COPY with
/// same types. Returns null otherwise.
MachineInstr *getOpcodeDef(unsigned Opcode, Register Reg,
                           const MachineRegisterInfo &MRI);

/// Simple struct used to hold a Register value and the instruction which
/// defines it.
struct DefinitionAndSourceRegister {
  MachineInstr *MI;
  Register Reg;
};

/// Find the def instruction for \p Reg, and underlying value Register folding
/// away any copies.
///
/// Also walks through hints such as G_ASSERT_ZEXT.
std::optional<DefinitionAndSourceRegister>
getDefSrcRegIgnoringCopies(Register Reg, const MachineRegisterInfo &MRI);

/// Find the def instruction for \p Reg, folding away any trivial copies. May
/// return nullptr if \p Reg is not a generic virtual register.
///
/// Also walks through hints such as G_ASSERT_ZEXT.
MachineInstr *getDefIgnoringCopies(Register Reg,
                                   const MachineRegisterInfo &MRI);

/// Find the source register for \p Reg, folding away any trivial copies. It
/// will be an output register of the instruction that getDefIgnoringCopies
/// returns. May return an invalid register if \p Reg is not a generic virtual
/// register.
///
/// Also walks through hints such as G_ASSERT_ZEXT.
Register getSrcRegIgnoringCopies(Register Reg, const MachineRegisterInfo &MRI);

/// Helper function to split a wide generic register into bitwise blocks with
/// the given Type (which implies the number of blocks needed). The generic
/// registers created are appended to Ops, starting at bit 0 of Reg.
void extractParts(Register Reg, LLT Ty, int NumParts,
                  SmallVectorImpl<Register> &VRegs,
                  MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI);

/// Version which handles irregular splits.
bool extractParts(Register Reg, LLT RegTy, LLT MainTy, LLT &LeftoverTy,
                  SmallVectorImpl<Register> &VRegs,
                  SmallVectorImpl<Register> &LeftoverVRegs,
                  MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI);

/// Version which handles irregular sub-vector splits.
void extractVectorParts(Register Reg, unsigned NumElts,
                        SmallVectorImpl<Register> &VRegs,
                        MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI);

// Templated variant of getOpcodeDef returning a MachineInstr derived T.
/// See if Reg is defined by an single def instruction of type T
/// Also try to do trivial folding if it's a COPY with
/// same types. Returns null otherwise.
template <class T>
T *getOpcodeDef(Register Reg, const MachineRegisterInfo &MRI) {
  MachineInstr *DefMI = getDefIgnoringCopies(Reg, MRI);
  return dyn_cast_or_null<T>(DefMI);
}

/// Returns an APFloat from Val converted to the appropriate size.
APFloat getAPFloatFromSize(double Val, unsigned Size);

/// Modify analysis usage so it preserves passes required for the SelectionDAG
/// fallback.
void getSelectionDAGFallbackAnalysisUsage(AnalysisUsage &AU);

std::optional<APInt> ConstantFoldBinOp(unsigned Opcode, const Register Op1,
                                       const Register Op2,
                                       const MachineRegisterInfo &MRI);
std::optional<APFloat> ConstantFoldFPBinOp(unsigned Opcode, const Register Op1,
                                           const Register Op2,
                                           const MachineRegisterInfo &MRI);

/// Tries to constant fold a vector binop with sources \p Op1 and \p Op2.
/// Returns an empty vector on failure.
SmallVector<APInt> ConstantFoldVectorBinop(unsigned Opcode, const Register Op1,
                                           const Register Op2,
                                           const MachineRegisterInfo &MRI);

std::optional<APInt> ConstantFoldCastOp(unsigned Opcode, LLT DstTy,
                                        const Register Op0,
                                        const MachineRegisterInfo &MRI);

std::optional<APInt> ConstantFoldExtOp(unsigned Opcode, const Register Op1,
                                       uint64_t Imm,
                                       const MachineRegisterInfo &MRI);

std::optional<APFloat> ConstantFoldIntToFloat(unsigned Opcode, LLT DstTy,
                                              Register Src,
                                              const MachineRegisterInfo &MRI);

/// Tries to constant fold a counting-zero operation (G_CTLZ or G_CTTZ) on \p
/// Src. If \p Src is a vector then it tries to do an element-wise constant
/// fold.
std::optional<SmallVector<unsigned>>
ConstantFoldCountZeros(Register Src, const MachineRegisterInfo &MRI,
                       std::function<unsigned(APInt)> CB);

std::optional<SmallVector<APInt>>
ConstantFoldICmp(unsigned Pred, const Register Op1, const Register Op2,
                 const MachineRegisterInfo &MRI);

/// Test if the given value is known to have exactly one bit set. This differs
/// from computeKnownBits in that it doesn't necessarily determine which bit is
/// set.
bool isKnownToBeAPowerOfTwo(Register Val, const MachineRegisterInfo &MRI,
                            GISelKnownBits *KnownBits = nullptr);

/// Returns true if \p Val can be assumed to never be a NaN. If \p SNaN is true,
/// this returns if \p Val can be assumed to never be a signaling NaN.
bool isKnownNeverNaN(Register Val, const MachineRegisterInfo &MRI,
                     bool SNaN = false);

/// Returns true if \p Val can be assumed to never be a signaling NaN.
inline bool isKnownNeverSNaN(Register Val, const MachineRegisterInfo &MRI) {
  return isKnownNeverNaN(Val, MRI, true);
}

Align inferAlignFromPtrInfo(MachineFunction &MF, const MachinePointerInfo &MPO);

/// Return a virtual register corresponding to the incoming argument register \p
/// PhysReg. This register is expected to have class \p RC, and optional type \p
/// RegTy. This assumes all references to the register will use the same type.
///
/// If there is an existing live-in argument register, it will be returned.
/// This will also ensure there is a valid copy
Register getFunctionLiveInPhysReg(MachineFunction &MF,
                                  const TargetInstrInfo &TII,
                                  MCRegister PhysReg,
                                  const TargetRegisterClass &RC,
                                  const DebugLoc &DL, LLT RegTy = LLT());

/// Return the least common multiple type of \p OrigTy and \p TargetTy, by
/// changing the number of vector elements or scalar bitwidth. The intent is a
/// G_MERGE_VALUES, G_BUILD_VECTOR, or G_CONCAT_VECTORS can be constructed from
/// \p OrigTy elements, and unmerged into \p TargetTy. It is an error to call
/// this function where one argument is a fixed vector and the other is a
/// scalable vector, since it is illegal to build a G_{MERGE|UNMERGE}_VALUES
/// between fixed and scalable vectors.
LLVM_READNONE
LLT getLCMType(LLT OrigTy, LLT TargetTy);

LLVM_READNONE
/// Return smallest type that covers both \p OrigTy and \p TargetTy and is
/// multiple of TargetTy.
LLT getCoverTy(LLT OrigTy, LLT TargetTy);

/// Return a type where the total size is the greatest common divisor of \p
/// OrigTy and \p TargetTy. This will try to either change the number of vector
/// elements, or bitwidth of scalars. The intent is the result type can be used
/// as the result of a G_UNMERGE_VALUES from \p OrigTy, and then some
/// combination of G_MERGE_VALUES, G_BUILD_VECTOR and G_CONCAT_VECTORS (possibly
/// with intermediate casts) can re-form \p TargetTy.
///
/// If these are vectors with different element types, this will try to produce
/// a vector with a compatible total size, but the element type of \p OrigTy. If
/// this can't be satisfied, this will produce a scalar smaller than the
/// original vector elements. It is an error to call this function where
/// one argument is a fixed vector and the other is a scalable vector, since it
/// is illegal to build a G_{MERGE|UNMERGE}_VALUES between fixed and scalable
/// vectors.
///
/// In the worst case, this returns LLT::scalar(1)
LLVM_READNONE
LLT getGCDType(LLT OrigTy, LLT TargetTy);

/// Represents a value which can be a Register or a constant.
///
/// This is useful in situations where an instruction may have an interesting
/// register operand or interesting constant operand. For a concrete example,
/// \see getVectorSplat.
class RegOrConstant {
  int64_t Cst;
  Register Reg;
  bool IsReg;

public:
  explicit RegOrConstant(Register Reg) : Reg(Reg), IsReg(true) {}
  explicit RegOrConstant(int64_t Cst) : Cst(Cst), IsReg(false) {}
  bool isReg() const { return IsReg; }
  bool isCst() const { return !IsReg; }
  Register getReg() const {
    assert(isReg() && "Expected a register!");
    return Reg;
  }
  int64_t getCst() const {
    assert(isCst() && "Expected a constant!");
    return Cst;
  }
};

/// \returns The splat index of a G_SHUFFLE_VECTOR \p MI when \p MI is a splat.
/// If \p MI is not a splat, returns std::nullopt.
std::optional<int> getSplatIndex(MachineInstr &MI);

/// \returns the scalar integral splat value of \p Reg if possible.
std::optional<APInt> getIConstantSplatVal(const Register Reg,
                                          const MachineRegisterInfo &MRI);

/// \returns the scalar integral splat value defined by \p MI if possible.
std::optional<APInt> getIConstantSplatVal(const MachineInstr &MI,
                                          const MachineRegisterInfo &MRI);

/// \returns the scalar sign extended integral splat value of \p Reg if
/// possible.
std::optional<int64_t> getIConstantSplatSExtVal(const Register Reg,
                                                const MachineRegisterInfo &MRI);

/// \returns the scalar sign extended integral splat value defined by \p MI if
/// possible.
std::optional<int64_t> getIConstantSplatSExtVal(const MachineInstr &MI,
                                                const MachineRegisterInfo &MRI);

/// Returns a floating point scalar constant of a build vector splat if it
/// exists. When \p AllowUndef == true some elements can be undef but not all.
std::optional<FPValueAndVReg> getFConstantSplat(Register VReg,
                                                const MachineRegisterInfo &MRI,
                                                bool AllowUndef = true);

/// Return true if the specified register is defined by G_BUILD_VECTOR or
/// G_BUILD_VECTOR_TRUNC where all of the elements are \p SplatValue or undef.
bool isBuildVectorConstantSplat(const Register Reg,
                                const MachineRegisterInfo &MRI,
                                int64_t SplatValue, bool AllowUndef);

/// Return true if the specified instruction is a G_BUILD_VECTOR or
/// G_BUILD_VECTOR_TRUNC where all of the elements are \p SplatValue or undef.
bool isBuildVectorConstantSplat(const MachineInstr &MI,
                                const MachineRegisterInfo &MRI,
                                int64_t SplatValue, bool AllowUndef);

/// Return true if the specified instruction is a G_BUILD_VECTOR or
/// G_BUILD_VECTOR_TRUNC where all of the elements are 0 or undef.
bool isBuildVectorAllZeros(const MachineInstr &MI,
                           const MachineRegisterInfo &MRI,
                           bool AllowUndef = false);

/// Return true if the specified instruction is a G_BUILD_VECTOR or
/// G_BUILD_VECTOR_TRUNC where all of the elements are ~0 or undef.
bool isBuildVectorAllOnes(const MachineInstr &MI,
                          const MachineRegisterInfo &MRI,
                          bool AllowUndef = false);

/// Return true if the specified instruction is known to be a constant, or a
/// vector of constants.
///
/// If \p AllowFP is true, this will consider G_FCONSTANT in addition to
/// G_CONSTANT. If \p AllowOpaqueConstants is true, constant-like instructions
/// such as G_GLOBAL_VALUE will also be considered.
bool isConstantOrConstantVector(const MachineInstr &MI,
                                const MachineRegisterInfo &MRI,
                                bool AllowFP = true,
                                bool AllowOpaqueConstants = true);

/// Return true if the value is a constant 0 integer or a splatted vector of a
/// constant 0 integer (with no undefs if \p AllowUndefs is false). This will
/// handle G_BUILD_VECTOR and G_BUILD_VECTOR_TRUNC as truncation is not an issue
/// for null values.
bool isNullOrNullSplat(const MachineInstr &MI, const MachineRegisterInfo &MRI,
                       bool AllowUndefs = false);

/// Return true if the value is a constant -1 integer or a splatted vector of a
/// constant -1 integer (with no undefs if \p AllowUndefs is false).
bool isAllOnesOrAllOnesSplat(const MachineInstr &MI,
                             const MachineRegisterInfo &MRI,
                             bool AllowUndefs = false);

/// \returns a value when \p MI is a vector splat. The splat can be either a
/// Register or a constant.
///
/// Examples:
///
/// \code
///   %reg = COPY $physreg
///   %reg_splat = G_BUILD_VECTOR %reg, %reg, ..., %reg
/// \endcode
///
/// If called on the G_BUILD_VECTOR above, this will return a RegOrConstant
/// containing %reg.
///
/// \code
///   %cst = G_CONSTANT iN 4
///   %constant_splat = G_BUILD_VECTOR %cst, %cst, ..., %cst
/// \endcode
///
/// In the above case, this will return a RegOrConstant containing 4.
std::optional<RegOrConstant> getVectorSplat(const MachineInstr &MI,
                                            const MachineRegisterInfo &MRI);

/// Determines if \p MI defines a constant integer or a build vector of
/// constant integers. Treats undef values as constants.
bool isConstantOrConstantVector(MachineInstr &MI,
                                const MachineRegisterInfo &MRI);

/// Determines if \p MI defines a constant integer or a splat vector of
/// constant integers.
/// \returns the scalar constant or std::nullopt.
std::optional<APInt>
isConstantOrConstantSplatVector(MachineInstr &MI,
                                const MachineRegisterInfo &MRI);

/// Attempt to match a unary predicate against a scalar/splat constant or every
/// element of a constant G_BUILD_VECTOR. If \p ConstVal is null, the source
/// value was undef.
bool matchUnaryPredicate(const MachineRegisterInfo &MRI, Register Reg,
                         std::function<bool(const Constant *ConstVal)> Match,
                         bool AllowUndefs = false);

/// Returns true if given the TargetLowering's boolean contents information,
/// the value \p Val contains a true value.
bool isConstTrueVal(const TargetLowering &TLI, int64_t Val, bool IsVector,
                    bool IsFP);
/// \returns true if given the TargetLowering's boolean contents information,
/// the value \p Val contains a false value.
bool isConstFalseVal(const TargetLowering &TLI, int64_t Val, bool IsVector,
                    bool IsFP);

/// Returns an integer representing true, as defined by the
/// TargetBooleanContents.
int64_t getICmpTrueVal(const TargetLowering &TLI, bool IsVector, bool IsFP);

/// Returns true if the given block should be optimized for size.
bool shouldOptForSize(const MachineBasicBlock &MBB, ProfileSummaryInfo *PSI,
                      BlockFrequencyInfo *BFI);

using SmallInstListTy = GISelWorkList<4>;
void saveUsesAndErase(MachineInstr &MI, MachineRegisterInfo &MRI,
                      LostDebugLocObserver *LocObserver,
                      SmallInstListTy &DeadInstChain);
void eraseInstrs(ArrayRef<MachineInstr *> DeadInstrs, MachineRegisterInfo &MRI,
                 LostDebugLocObserver *LocObserver = nullptr);
void eraseInstr(MachineInstr &MI, MachineRegisterInfo &MRI,
                LostDebugLocObserver *LocObserver = nullptr);

/// Assuming the instruction \p MI is going to be deleted, attempt to salvage
/// debug users of \p MI by writing the effect of \p MI in a DIExpression.
void salvageDebugInfo(const MachineRegisterInfo &MRI, MachineInstr &MI);

/// Returns whether opcode \p Opc is a pre-isel generic floating-point opcode,
/// having only floating-point operands.
bool isPreISelGenericFloatingPointOpcode(unsigned Opc);

/// Returns true if \p Reg can create undef or poison from non-undef &
/// non-poison operands. \p ConsiderFlagsAndMetadata controls whether poison
/// producing flags and metadata on the instruction are considered. This can be
/// used to see if the instruction could still introduce undef or poison even
/// without poison generating flags and metadata which might be on the
/// instruction.
bool canCreateUndefOrPoison(Register Reg, const MachineRegisterInfo &MRI,
                            bool ConsiderFlagsAndMetadata = true);

/// Returns true if \p Reg can create poison from non-poison operands.
bool canCreatePoison(Register Reg, const MachineRegisterInfo &MRI,
                     bool ConsiderFlagsAndMetadata = true);

/// Returns true if \p Reg cannot be poison and undef.
bool isGuaranteedNotToBeUndefOrPoison(Register Reg,
                                      const MachineRegisterInfo &MRI,
                                      unsigned Depth = 0);

/// Returns true if \p Reg cannot be poison, but may be undef.
bool isGuaranteedNotToBePoison(Register Reg, const MachineRegisterInfo &MRI,
                               unsigned Depth = 0);

/// Returns true if \p Reg cannot be undef, but may be poison.
bool isGuaranteedNotToBeUndef(Register Reg, const MachineRegisterInfo &MRI,
                              unsigned Depth = 0);

/// Get the type back from LLT. It won't be 100 percent accurate but returns an
/// estimate of the type.
Type *getTypeForLLT(LLT Ty, LLVMContext &C);

} // End namespace llvm.
#endif

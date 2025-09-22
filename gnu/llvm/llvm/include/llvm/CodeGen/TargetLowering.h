//===- llvm/CodeGen/TargetLowering.h - Target Lowering Info -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file describes how to lower LLVM code to machine code.  This has two
/// main components:
///
///  1. Which ValueTypes are natively supported by the target.
///  2. Which operations are supported for supported ValueTypes.
///  3. Cost thresholds for alternative implementations of certain operations.
///
/// In addition it has a few other components, like information about FP
/// immediates.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_TARGETLOWERING_H
#define LLVM_CODEGEN_TARGETLOWERING_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/DAGCombine.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/LowLevelTypeUtils.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RuntimeLibcallUtil.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/TargetCallingConv.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/RuntimeLibcalls.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdint>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

class AssumptionCache;
class CCState;
class CCValAssign;
enum class ComplexDeinterleavingOperation;
enum class ComplexDeinterleavingRotation;
class Constant;
class FastISel;
class FunctionLoweringInfo;
class GlobalValue;
class Loop;
class GISelKnownBits;
class IntrinsicInst;
class IRBuilderBase;
struct KnownBits;
class LLVMContext;
class MachineBasicBlock;
class MachineFunction;
class MachineInstr;
class MachineJumpTableInfo;
class MachineLoop;
class MachineRegisterInfo;
class MCContext;
class MCExpr;
class Module;
class ProfileSummaryInfo;
class TargetLibraryInfo;
class TargetMachine;
class TargetRegisterClass;
class TargetRegisterInfo;
class TargetTransformInfo;
class Value;

namespace Sched {

enum Preference : uint8_t {
  None,            // No preference
  Source,          // Follow source order.
  RegPressure,     // Scheduling for lowest register pressure.
  Hybrid,          // Scheduling for both latency and register pressure.
  ILP,             // Scheduling for ILP in low register pressure mode.
  VLIW,            // Scheduling for VLIW targets.
  Fast,            // Fast suboptimal list scheduling
  Linearize,       // Linearize DAG, no scheduling
  Last = Linearize // Marker for the last Sched::Preference
};

} // end namespace Sched

// MemOp models a memory operation, either memset or memcpy/memmove.
struct MemOp {
private:
  // Shared
  uint64_t Size;
  bool DstAlignCanChange; // true if destination alignment can satisfy any
                          // constraint.
  Align DstAlign;         // Specified alignment of the memory operation.

  bool AllowOverlap;
  // memset only
  bool IsMemset;   // If setthis memory operation is a memset.
  bool ZeroMemset; // If set clears out memory with zeros.
  // memcpy only
  bool MemcpyStrSrc; // Indicates whether the memcpy source is an in-register
                     // constant so it does not need to be loaded.
  Align SrcAlign;    // Inferred alignment of the source or default value if the
                     // memory operation does not need to load the value.
public:
  static MemOp Copy(uint64_t Size, bool DstAlignCanChange, Align DstAlign,
                    Align SrcAlign, bool IsVolatile,
                    bool MemcpyStrSrc = false) {
    MemOp Op;
    Op.Size = Size;
    Op.DstAlignCanChange = DstAlignCanChange;
    Op.DstAlign = DstAlign;
    Op.AllowOverlap = !IsVolatile;
    Op.IsMemset = false;
    Op.ZeroMemset = false;
    Op.MemcpyStrSrc = MemcpyStrSrc;
    Op.SrcAlign = SrcAlign;
    return Op;
  }

  static MemOp Set(uint64_t Size, bool DstAlignCanChange, Align DstAlign,
                   bool IsZeroMemset, bool IsVolatile) {
    MemOp Op;
    Op.Size = Size;
    Op.DstAlignCanChange = DstAlignCanChange;
    Op.DstAlign = DstAlign;
    Op.AllowOverlap = !IsVolatile;
    Op.IsMemset = true;
    Op.ZeroMemset = IsZeroMemset;
    Op.MemcpyStrSrc = false;
    return Op;
  }

  uint64_t size() const { return Size; }
  Align getDstAlign() const {
    assert(!DstAlignCanChange);
    return DstAlign;
  }
  bool isFixedDstAlign() const { return !DstAlignCanChange; }
  bool allowOverlap() const { return AllowOverlap; }
  bool isMemset() const { return IsMemset; }
  bool isMemcpy() const { return !IsMemset; }
  bool isMemcpyWithFixedDstAlign() const {
    return isMemcpy() && !DstAlignCanChange;
  }
  bool isZeroMemset() const { return isMemset() && ZeroMemset; }
  bool isMemcpyStrSrc() const {
    assert(isMemcpy() && "Must be a memcpy");
    return MemcpyStrSrc;
  }
  Align getSrcAlign() const {
    assert(isMemcpy() && "Must be a memcpy");
    return SrcAlign;
  }
  bool isSrcAligned(Align AlignCheck) const {
    return isMemset() || llvm::isAligned(AlignCheck, SrcAlign.value());
  }
  bool isDstAligned(Align AlignCheck) const {
    return DstAlignCanChange || llvm::isAligned(AlignCheck, DstAlign.value());
  }
  bool isAligned(Align AlignCheck) const {
    return isSrcAligned(AlignCheck) && isDstAligned(AlignCheck);
  }
};

/// This base class for TargetLowering contains the SelectionDAG-independent
/// parts that can be used from the rest of CodeGen.
class TargetLoweringBase {
public:
  /// This enum indicates whether operations are valid for a target, and if not,
  /// what action should be used to make them valid.
  enum LegalizeAction : uint8_t {
    Legal,      // The target natively supports this operation.
    Promote,    // This operation should be executed in a larger type.
    Expand,     // Try to expand this to other ops, otherwise use a libcall.
    LibCall,    // Don't try to expand this to other ops, always use a libcall.
    Custom      // Use the LowerOperation hook to implement custom lowering.
  };

  /// This enum indicates whether a types are legal for a target, and if not,
  /// what action should be used to make them valid.
  enum LegalizeTypeAction : uint8_t {
    TypeLegal,           // The target natively supports this type.
    TypePromoteInteger,  // Replace this integer with a larger one.
    TypeExpandInteger,   // Split this integer into two of half the size.
    TypeSoftenFloat,     // Convert this float to a same size integer type.
    TypeExpandFloat,     // Split this float into two of half the size.
    TypeScalarizeVector, // Replace this one-element vector with its element.
    TypeSplitVector,     // Split this vector into two of half the size.
    TypeWidenVector,     // This vector should be widened into a larger vector.
    TypePromoteFloat,    // Replace this float with a larger one.
    TypeSoftPromoteHalf, // Soften half to i16 and use float to do arithmetic.
    TypeScalarizeScalableVector, // This action is explicitly left unimplemented.
                                 // While it is theoretically possible to
                                 // legalize operations on scalable types with a
                                 // loop that handles the vscale * #lanes of the
                                 // vector, this is non-trivial at SelectionDAG
                                 // level and these types are better to be
                                 // widened or promoted.
  };

  /// LegalizeKind holds the legalization kind that needs to happen to EVT
  /// in order to type-legalize it.
  using LegalizeKind = std::pair<LegalizeTypeAction, EVT>;

  /// Enum that describes how the target represents true/false values.
  enum BooleanContent {
    UndefinedBooleanContent,    // Only bit 0 counts, the rest can hold garbage.
    ZeroOrOneBooleanContent,        // All bits zero except for bit 0.
    ZeroOrNegativeOneBooleanContent // All bits equal to bit 0.
  };

  /// Enum that describes what type of support for selects the target has.
  enum SelectSupportKind {
    ScalarValSelect,      // The target supports scalar selects (ex: cmov).
    ScalarCondVectorVal,  // The target supports selects with a scalar condition
                          // and vector values (ex: cmov).
    VectorMaskSelect      // The target supports vector selects with a vector
                          // mask (ex: x86 blends).
  };

  /// Enum that specifies what an atomic load/AtomicRMWInst is expanded
  /// to, if at all. Exists because different targets have different levels of
  /// support for these atomic instructions, and also have different options
  /// w.r.t. what they should expand to.
  enum class AtomicExpansionKind {
    None,    // Don't expand the instruction.
    CastToInteger,    // Cast the atomic instruction to another type, e.g. from
                      // floating-point to integer type.
    LLSC,    // Expand the instruction into loadlinked/storeconditional; used
             // by ARM/AArch64.
    LLOnly,  // Expand the (load) instruction into just a load-linked, which has
             // greater atomic guarantees than a normal load.
    CmpXChg, // Expand the instruction into cmpxchg; used by at least X86.
    MaskedIntrinsic,  // Use a target-specific intrinsic for the LL/SC loop.
    BitTestIntrinsic, // Use a target-specific intrinsic for special bit
                      // operations; used by X86.
    CmpArithIntrinsic,// Use a target-specific intrinsic for special compare
                      // operations; used by X86.
    Expand,           // Generic expansion in terms of other atomic operations.

    // Rewrite to a non-atomic form for use in a known non-preemptible
    // environment.
    NotAtomic
  };

  /// Enum that specifies when a multiplication should be expanded.
  enum class MulExpansionKind {
    Always,            // Always expand the instruction.
    OnlyLegalOrCustom, // Only expand when the resulting instructions are legal
                       // or custom.
  };

  /// Enum that specifies when a float negation is beneficial.
  enum class NegatibleCost {
    Cheaper = 0,    // Negated expression is cheaper.
    Neutral = 1,    // Negated expression has the same cost.
    Expensive = 2   // Negated expression is more expensive.
  };

  /// Enum of different potentially desirable ways to fold (and/or (setcc ...),
  /// (setcc ...)).
  enum AndOrSETCCFoldKind : uint8_t {
    None = 0,   // No fold is preferable.
    AddAnd = 1, // Fold with `Add` op and `And` op is preferable.
    NotAnd = 2, // Fold with `Not` op and `And` op is preferable.
    ABS = 4,    // Fold with `llvm.abs` op is preferable.
  };

  class ArgListEntry {
  public:
    Value *Val = nullptr;
    SDValue Node = SDValue();
    Type *Ty = nullptr;
    bool IsSExt : 1;
    bool IsZExt : 1;
    bool IsInReg : 1;
    bool IsSRet : 1;
    bool IsNest : 1;
    bool IsByVal : 1;
    bool IsByRef : 1;
    bool IsInAlloca : 1;
    bool IsPreallocated : 1;
    bool IsReturned : 1;
    bool IsSwiftSelf : 1;
    bool IsSwiftAsync : 1;
    bool IsSwiftError : 1;
    bool IsCFGuardTarget : 1;
    MaybeAlign Alignment = std::nullopt;
    Type *IndirectType = nullptr;

    ArgListEntry()
        : IsSExt(false), IsZExt(false), IsInReg(false), IsSRet(false),
          IsNest(false), IsByVal(false), IsByRef(false), IsInAlloca(false),
          IsPreallocated(false), IsReturned(false), IsSwiftSelf(false),
          IsSwiftAsync(false), IsSwiftError(false), IsCFGuardTarget(false) {}

    void setAttributes(const CallBase *Call, unsigned ArgIdx);
  };
  using ArgListTy = std::vector<ArgListEntry>;

  virtual void markLibCallAttributes(MachineFunction *MF, unsigned CC,
                                     ArgListTy &Args) const {};

  static ISD::NodeType getExtendForContent(BooleanContent Content) {
    switch (Content) {
    case UndefinedBooleanContent:
      // Extend by adding rubbish bits.
      return ISD::ANY_EXTEND;
    case ZeroOrOneBooleanContent:
      // Extend by adding zero bits.
      return ISD::ZERO_EXTEND;
    case ZeroOrNegativeOneBooleanContent:
      // Extend by copying the sign bit.
      return ISD::SIGN_EXTEND;
    }
    llvm_unreachable("Invalid content kind");
  }

  explicit TargetLoweringBase(const TargetMachine &TM);
  TargetLoweringBase(const TargetLoweringBase &) = delete;
  TargetLoweringBase &operator=(const TargetLoweringBase &) = delete;
  virtual ~TargetLoweringBase() = default;

  /// Return true if the target support strict float operation
  bool isStrictFPEnabled() const {
    return IsStrictFPEnabled;
  }

protected:
  /// Initialize all of the actions to default values.
  void initActions();

public:
  const TargetMachine &getTargetMachine() const { return TM; }

  virtual bool useSoftFloat() const { return false; }

  /// Return the pointer type for the given address space, defaults to
  /// the pointer type from the data layout.
  /// FIXME: The default needs to be removed once all the code is updated.
  virtual MVT getPointerTy(const DataLayout &DL, uint32_t AS = 0) const {
    return MVT::getIntegerVT(DL.getPointerSizeInBits(AS));
  }

  /// Return the in-memory pointer type for the given address space, defaults to
  /// the pointer type from the data layout.
  /// FIXME: The default needs to be removed once all the code is updated.
  virtual MVT getPointerMemTy(const DataLayout &DL, uint32_t AS = 0) const {
    return MVT::getIntegerVT(DL.getPointerSizeInBits(AS));
  }

  /// Return the type for frame index, which is determined by
  /// the alloca address space specified through the data layout.
  MVT getFrameIndexTy(const DataLayout &DL) const {
    return getPointerTy(DL, DL.getAllocaAddrSpace());
  }

  /// Return the type for code pointers, which is determined by the program
  /// address space specified through the data layout.
  MVT getProgramPointerTy(const DataLayout &DL) const {
    return getPointerTy(DL, DL.getProgramAddressSpace());
  }

  /// Return the type for operands of fence.
  /// TODO: Let fence operands be of i32 type and remove this.
  virtual MVT getFenceOperandTy(const DataLayout &DL) const {
    return getPointerTy(DL);
  }

  /// Return the type to use for a scalar shift opcode, given the shifted amount
  /// type. Targets should return a legal type if the input type is legal.
  /// Targets can return a type that is too small if the input type is illegal.
  virtual MVT getScalarShiftAmountTy(const DataLayout &, EVT) const;

  /// Returns the type for the shift amount of a shift opcode. For vectors,
  /// returns the input type. For scalars, calls getScalarShiftAmountTy.
  /// If getScalarShiftAmountTy type cannot represent all possible shift
  /// amounts, returns MVT::i32.
  EVT getShiftAmountTy(EVT LHSTy, const DataLayout &DL) const;

  /// Return the preferred type to use for a shift opcode, given the shifted
  /// amount type is \p ShiftValueTy.
  LLVM_READONLY
  virtual LLT getPreferredShiftAmountTy(LLT ShiftValueTy) const {
    return ShiftValueTy;
  }

  /// Returns the type to be used for the index operand of:
  /// ISD::INSERT_VECTOR_ELT, ISD::EXTRACT_VECTOR_ELT,
  /// ISD::INSERT_SUBVECTOR, and ISD::EXTRACT_SUBVECTOR
  virtual MVT getVectorIdxTy(const DataLayout &DL) const {
    return getPointerTy(DL);
  }

  /// Returns the type to be used for the EVL/AVL operand of VP nodes:
  /// ISD::VP_ADD, ISD::VP_SUB, etc. It must be a legal scalar integer type,
  /// and must be at least as large as i32. The EVL is implicitly zero-extended
  /// to any larger type.
  virtual MVT getVPExplicitVectorLengthTy() const { return MVT::i32; }

  /// This callback is used to inspect load/store instructions and add
  /// target-specific MachineMemOperand flags to them.  The default
  /// implementation does nothing.
  virtual MachineMemOperand::Flags getTargetMMOFlags(const Instruction &I) const {
    return MachineMemOperand::MONone;
  }

  /// This callback is used to inspect load/store SDNode.
  /// The default implementation does nothing.
  virtual MachineMemOperand::Flags
  getTargetMMOFlags(const MemSDNode &Node) const {
    return MachineMemOperand::MONone;
  }

  MachineMemOperand::Flags
  getLoadMemOperandFlags(const LoadInst &LI, const DataLayout &DL,
                         AssumptionCache *AC = nullptr,
                         const TargetLibraryInfo *LibInfo = nullptr) const;
  MachineMemOperand::Flags getStoreMemOperandFlags(const StoreInst &SI,
                                                   const DataLayout &DL) const;
  MachineMemOperand::Flags getAtomicMemOperandFlags(const Instruction &AI,
                                                    const DataLayout &DL) const;

  virtual bool isSelectSupported(SelectSupportKind /*kind*/) const {
    return true;
  }

  /// Return true if the @llvm.get.active.lane.mask intrinsic should be expanded
  /// using generic code in SelectionDAGBuilder.
  virtual bool shouldExpandGetActiveLaneMask(EVT VT, EVT OpVT) const {
    return true;
  }

  virtual bool shouldExpandGetVectorLength(EVT CountVT, unsigned VF,
                                           bool IsScalable) const {
    return true;
  }

  /// Return true if the @llvm.experimental.cttz.elts intrinsic should be
  /// expanded using generic code in SelectionDAGBuilder.
  virtual bool shouldExpandCttzElements(EVT VT) const { return true; }

  /// Return the minimum number of bits required to hold the maximum possible
  /// number of trailing zero vector elements.
  unsigned getBitWidthForCttzElements(Type *RetTy, ElementCount EC,
                                      bool ZeroIsPoison,
                                      const ConstantRange *VScaleRange) const;

  // Return true if op(vecreduce(x), vecreduce(y)) should be reassociated to
  // vecreduce(op(x, y)) for the reduction opcode RedOpc.
  virtual bool shouldReassociateReduction(unsigned RedOpc, EVT VT) const {
    return true;
  }

  /// Return true if it is profitable to convert a select of FP constants into
  /// a constant pool load whose address depends on the select condition. The
  /// parameter may be used to differentiate a select with FP compare from
  /// integer compare.
  virtual bool reduceSelectOfFPConstantLoads(EVT CmpOpVT) const {
    return true;
  }

  /// Return true if multiple condition registers are available.
  bool hasMultipleConditionRegisters() const {
    return HasMultipleConditionRegisters;
  }

  /// Return true if the target has BitExtract instructions.
  bool hasExtractBitsInsn() const { return HasExtractBitsInsn; }

  /// Return the preferred vector type legalization action.
  virtual TargetLoweringBase::LegalizeTypeAction
  getPreferredVectorAction(MVT VT) const {
    // The default action for one element vectors is to scalarize
    if (VT.getVectorElementCount().isScalar())
      return TypeScalarizeVector;
    // The default action for an odd-width vector is to widen.
    if (!VT.isPow2VectorType())
      return TypeWidenVector;
    // The default action for other vectors is to promote
    return TypePromoteInteger;
  }

  // Return true if the half type should be promoted using soft promotion rules
  // where each operation is promoted to f32 individually, then converted to
  // fp16. The default behavior is to promote chains of operations, keeping
  // intermediate results in f32 precision and range.
  virtual bool softPromoteHalfType() const { return false; }

  // Return true if, for soft-promoted half, the half type should be passed
  // passed to and returned from functions as f32. The default behavior is to
  // pass as i16. If soft-promoted half is not used, this function is ignored
  // and values are always passed and returned as f32.
  virtual bool useFPRegsForHalfType() const { return false; }

  // There are two general methods for expanding a BUILD_VECTOR node:
  //  1. Use SCALAR_TO_VECTOR on the defined scalar values and then shuffle
  //     them together.
  //  2. Build the vector on the stack and then load it.
  // If this function returns true, then method (1) will be used, subject to
  // the constraint that all of the necessary shuffles are legal (as determined
  // by isShuffleMaskLegal). If this function returns false, then method (2) is
  // always used. The vector type, and the number of defined values, are
  // provided.
  virtual bool
  shouldExpandBuildVectorWithShuffles(EVT /* VT */,
                                      unsigned DefinedValues) const {
    return DefinedValues < 3;
  }

  /// Return true if integer divide is usually cheaper than a sequence of
  /// several shifts, adds, and multiplies for this target.
  /// The definition of "cheaper" may depend on whether we're optimizing
  /// for speed or for size.
  virtual bool isIntDivCheap(EVT VT, AttributeList Attr) const { return false; }

  /// Return true if the target can handle a standalone remainder operation.
  virtual bool hasStandaloneRem(EVT VT) const {
    return true;
  }

  /// Return true if SQRT(X) shouldn't be replaced with X*RSQRT(X).
  virtual bool isFsqrtCheap(SDValue X, SelectionDAG &DAG) const {
    // Default behavior is to replace SQRT(X) with X*RSQRT(X).
    return false;
  }

  /// Reciprocal estimate status values used by the functions below.
  enum ReciprocalEstimate : int {
    Unspecified = -1,
    Disabled = 0,
    Enabled = 1
  };

  /// Return a ReciprocalEstimate enum value for a square root of the given type
  /// based on the function's attributes. If the operation is not overridden by
  /// the function's attributes, "Unspecified" is returned and target defaults
  /// are expected to be used for instruction selection.
  int getRecipEstimateSqrtEnabled(EVT VT, MachineFunction &MF) const;

  /// Return a ReciprocalEstimate enum value for a division of the given type
  /// based on the function's attributes. If the operation is not overridden by
  /// the function's attributes, "Unspecified" is returned and target defaults
  /// are expected to be used for instruction selection.
  int getRecipEstimateDivEnabled(EVT VT, MachineFunction &MF) const;

  /// Return the refinement step count for a square root of the given type based
  /// on the function's attributes. If the operation is not overridden by
  /// the function's attributes, "Unspecified" is returned and target defaults
  /// are expected to be used for instruction selection.
  int getSqrtRefinementSteps(EVT VT, MachineFunction &MF) const;

  /// Return the refinement step count for a division of the given type based
  /// on the function's attributes. If the operation is not overridden by
  /// the function's attributes, "Unspecified" is returned and target defaults
  /// are expected to be used for instruction selection.
  int getDivRefinementSteps(EVT VT, MachineFunction &MF) const;

  /// Returns true if target has indicated at least one type should be bypassed.
  bool isSlowDivBypassed() const { return !BypassSlowDivWidths.empty(); }

  /// Returns map of slow types for division or remainder with corresponding
  /// fast types
  const DenseMap<unsigned int, unsigned int> &getBypassSlowDivWidths() const {
    return BypassSlowDivWidths;
  }

  /// Return true only if vscale must be a power of two.
  virtual bool isVScaleKnownToBeAPowerOfTwo() const { return false; }

  /// Return true if Flow Control is an expensive operation that should be
  /// avoided.
  bool isJumpExpensive() const { return JumpIsExpensive; }

  // Costs parameters used by
  // SelectionDAGBuilder::shouldKeepJumpConditionsTogether.
  // shouldKeepJumpConditionsTogether will use these parameter value to
  // determine if two conditions in the form `br (and/or cond1, cond2)` should
  // be split into two branches or left as one.
  //
  // BaseCost is the cost threshold (in latency). If the estimated latency of
  // computing both `cond1` and `cond2` is below the cost of just computing
  // `cond1` + BaseCost, the two conditions will be kept together. Otherwise
  // they will be split.
  //
  // LikelyBias increases BaseCost if branch probability info indicates that it
  // is likely that both `cond1` and `cond2` will be computed.
  //
  // UnlikelyBias decreases BaseCost if branch probability info indicates that
  // it is likely that both `cond1` and `cond2` will be computed.
  //
  // Set any field to -1 to make it ignored (setting BaseCost to -1 results in
  // `shouldKeepJumpConditionsTogether` always returning false).
  struct CondMergingParams {
    int BaseCost;
    int LikelyBias;
    int UnlikelyBias;
  };
  // Return params for deciding if we should keep two branch conditions merged
  // or split them into two separate branches.
  // Arg0: The binary op joining the two conditions (and/or).
  // Arg1: The first condition (cond1)
  // Arg2: The second condition (cond2)
  virtual CondMergingParams
  getJumpConditionMergingParams(Instruction::BinaryOps, const Value *,
                                const Value *) const {
    // -1 will always result in splitting.
    return {-1, -1, -1};
  }

  /// Return true if selects are only cheaper than branches if the branch is
  /// unlikely to be predicted right.
  bool isPredictableSelectExpensive() const {
    return PredictableSelectIsExpensive;
  }

  virtual bool fallBackToDAGISel(const Instruction &Inst) const {
    return false;
  }

  /// Return true if the following transform is beneficial:
  /// fold (conv (load x)) -> (load (conv*)x)
  /// On architectures that don't natively support some vector loads
  /// efficiently, casting the load to a smaller vector of larger types and
  /// loading is more efficient, however, this can be undone by optimizations in
  /// dag combiner.
  virtual bool isLoadBitCastBeneficial(EVT LoadVT, EVT BitcastVT,
                                       const SelectionDAG &DAG,
                                       const MachineMemOperand &MMO) const;

  /// Return true if the following transform is beneficial:
  /// (store (y (conv x)), y*)) -> (store x, (x*))
  virtual bool isStoreBitCastBeneficial(EVT StoreVT, EVT BitcastVT,
                                        const SelectionDAG &DAG,
                                        const MachineMemOperand &MMO) const {
    // Default to the same logic as loads.
    return isLoadBitCastBeneficial(StoreVT, BitcastVT, DAG, MMO);
  }

  /// Return true if it is expected to be cheaper to do a store of vector
  /// constant with the given size and type for the address space than to
  /// store the individual scalar element constants.
  virtual bool storeOfVectorConstantIsCheap(bool IsZero, EVT MemVT,
                                            unsigned NumElem,
                                            unsigned AddrSpace) const {
    return IsZero;
  }

  /// Allow store merging for the specified type after legalization in addition
  /// to before legalization. This may transform stores that do not exist
  /// earlier (for example, stores created from intrinsics).
  virtual bool mergeStoresAfterLegalization(EVT MemVT) const {
    return true;
  }

  /// Returns if it's reasonable to merge stores to MemVT size.
  virtual bool canMergeStoresTo(unsigned AS, EVT MemVT,
                                const MachineFunction &MF) const {
    return true;
  }

  /// Return true if it is cheap to speculate a call to intrinsic cttz.
  virtual bool isCheapToSpeculateCttz(Type *Ty) const {
    return false;
  }

  /// Return true if it is cheap to speculate a call to intrinsic ctlz.
  virtual bool isCheapToSpeculateCtlz(Type *Ty) const {
    return false;
  }

  /// Return true if ctlz instruction is fast.
  virtual bool isCtlzFast() const {
    return false;
  }

  /// Return true if ctpop instruction is fast.
  virtual bool isCtpopFast(EVT VT) const {
    return isOperationLegal(ISD::CTPOP, VT);
  }

  /// Return the maximum number of "x & (x - 1)" operations that can be done
  /// instead of deferring to a custom CTPOP.
  virtual unsigned getCustomCtpopCost(EVT VT, ISD::CondCode Cond) const {
    return 1;
  }

  /// Return true if instruction generated for equality comparison is folded
  /// with instruction generated for signed comparison.
  virtual bool isEqualityCmpFoldedWithSignedCmp() const { return true; }

  /// Return true if the heuristic to prefer icmp eq zero should be used in code
  /// gen prepare.
  virtual bool preferZeroCompareBranch() const { return false; }

  /// Return true if it is cheaper to split the store of a merged int val
  /// from a pair of smaller values into multiple stores.
  virtual bool isMultiStoresCheaperThanBitsMerge(EVT LTy, EVT HTy) const {
    return false;
  }

  /// Return if the target supports combining a
  /// chain like:
  /// \code
  ///   %andResult = and %val1, #mask
  ///   %icmpResult = icmp %andResult, 0
  /// \endcode
  /// into a single machine instruction of a form like:
  /// \code
  ///   cc = test %register, #mask
  /// \endcode
  virtual bool isMaskAndCmp0FoldingBeneficial(const Instruction &AndI) const {
    return false;
  }

  /// Return true if it is valid to merge the TargetMMOFlags in two SDNodes.
  virtual bool
  areTwoSDNodeTargetMMOFlagsMergeable(const MemSDNode &NodeX,
                                      const MemSDNode &NodeY) const {
    return true;
  }

  /// Use bitwise logic to make pairs of compares more efficient. For example:
  /// and (seteq A, B), (seteq C, D) --> seteq (or (xor A, B), (xor C, D)), 0
  /// This should be true when it takes more than one instruction to lower
  /// setcc (cmp+set on x86 scalar), when bitwise ops are faster than logic on
  /// condition bits (crand on PowerPC), and/or when reducing cmp+br is a win.
  virtual bool convertSetCCLogicToBitwiseLogic(EVT VT) const {
    return false;
  }

  /// Return the preferred operand type if the target has a quick way to compare
  /// integer values of the given size. Assume that any legal integer type can
  /// be compared efficiently. Targets may override this to allow illegal wide
  /// types to return a vector type if there is support to compare that type.
  virtual MVT hasFastEqualityCompare(unsigned NumBits) const {
    MVT VT = MVT::getIntegerVT(NumBits);
    return isTypeLegal(VT) ? VT : MVT::INVALID_SIMPLE_VALUE_TYPE;
  }

  /// Return true if the target should transform:
  /// (X & Y) == Y ---> (~X & Y) == 0
  /// (X & Y) != Y ---> (~X & Y) != 0
  ///
  /// This may be profitable if the target has a bitwise and-not operation that
  /// sets comparison flags. A target may want to limit the transformation based
  /// on the type of Y or if Y is a constant.
  ///
  /// Note that the transform will not occur if Y is known to be a power-of-2
  /// because a mask and compare of a single bit can be handled by inverting the
  /// predicate, for example:
  /// (X & 8) == 8 ---> (X & 8) != 0
  virtual bool hasAndNotCompare(SDValue Y) const {
    return false;
  }

  /// Return true if the target has a bitwise and-not operation:
  /// X = ~A & B
  /// This can be used to simplify select or other instructions.
  virtual bool hasAndNot(SDValue X) const {
    // If the target has the more complex version of this operation, assume that
    // it has this operation too.
    return hasAndNotCompare(X);
  }

  /// Return true if the target has a bit-test instruction:
  ///   (X & (1 << Y)) ==/!= 0
  /// This knowledge can be used to prevent breaking the pattern,
  /// or creating it if it could be recognized.
  virtual bool hasBitTest(SDValue X, SDValue Y) const { return false; }

  /// There are two ways to clear extreme bits (either low or high):
  /// Mask:    x &  (-1 << y)  (the instcombine canonical form)
  /// Shifts:  x >> y << y
  /// Return true if the variant with 2 variable shifts is preferred.
  /// Return false if there is no preference.
  virtual bool shouldFoldMaskToVariableShiftPair(SDValue X) const {
    // By default, let's assume that no one prefers shifts.
    return false;
  }

  /// Return true if it is profitable to fold a pair of shifts into a mask.
  /// This is usually true on most targets. But some targets, like Thumb1,
  /// have immediate shift instructions, but no immediate "and" instruction;
  /// this makes the fold unprofitable.
  virtual bool shouldFoldConstantShiftPairToMask(const SDNode *N,
                                                 CombineLevel Level) const {
    return true;
  }

  /// Should we tranform the IR-optimal check for whether given truncation
  /// down into KeptBits would be truncating or not:
  ///   (add %x, (1 << (KeptBits-1))) srccond (1 << KeptBits)
  /// Into it's more traditional form:
  ///   ((%x << C) a>> C) dstcond %x
  /// Return true if we should transform.
  /// Return false if there is no preference.
  virtual bool shouldTransformSignedTruncationCheck(EVT XVT,
                                                    unsigned KeptBits) const {
    // By default, let's assume that no one prefers shifts.
    return false;
  }

  /// Given the pattern
  ///   (X & (C l>>/<< Y)) ==/!= 0
  /// return true if it should be transformed into:
  ///   ((X <</l>> Y) & C) ==/!= 0
  /// WARNING: if 'X' is a constant, the fold may deadlock!
  /// FIXME: we could avoid passing XC, but we can't use isConstOrConstSplat()
  ///        here because it can end up being not linked in.
  virtual bool shouldProduceAndByConstByHoistingConstFromShiftsLHSOfAnd(
      SDValue X, ConstantSDNode *XC, ConstantSDNode *CC, SDValue Y,
      unsigned OldShiftOpcode, unsigned NewShiftOpcode,
      SelectionDAG &DAG) const {
    if (hasBitTest(X, Y)) {
      // One interesting pattern that we'd want to form is 'bit test':
      //   ((1 << Y) & C) ==/!= 0
      // But we also need to be careful not to try to reverse that fold.

      // Is this '1 << Y' ?
      if (OldShiftOpcode == ISD::SHL && CC->isOne())
        return false; // Keep the 'bit test' pattern.

      // Will it be '1 << Y' after the transform ?
      if (XC && NewShiftOpcode == ISD::SHL && XC->isOne())
        return true; // Do form the 'bit test' pattern.
    }

    // If 'X' is a constant, and we transform, then we will immediately
    // try to undo the fold, thus causing endless combine loop.
    // So by default, let's assume everyone prefers the fold
    // iff 'X' is not a constant.
    return !XC;
  }

  // Return true if its desirable to perform the following transform:
  // (fmul C, (uitofp Pow2))
  //     -> (bitcast_to_FP (add (bitcast_to_INT C), Log2(Pow2) << mantissa))
  // (fdiv C, (uitofp Pow2))
  //     -> (bitcast_to_FP (sub (bitcast_to_INT C), Log2(Pow2) << mantissa))
  //
  // This is only queried after we have verified the transform will be bitwise
  // equals.
  //
  // SDNode *N      : The FDiv/FMul node we want to transform.
  // SDValue FPConst: The Float constant operand in `N`.
  // SDValue IntPow2: The Integer power of 2 operand in `N`.
  virtual bool optimizeFMulOrFDivAsShiftAddBitcast(SDNode *N, SDValue FPConst,
                                                   SDValue IntPow2) const {
    // Default to avoiding fdiv which is often very expensive.
    return N->getOpcode() == ISD::FDIV;
  }

  // Given:
  //    (icmp eq/ne (and X, C0), (shift X, C1))
  // or
  //    (icmp eq/ne X, (rotate X, CPow2))

  // If C0 is a mask or shifted mask and the shift amt (C1) isolates the
  // remaining bits (i.e something like `(x64 & UINT32_MAX) == (x64 >> 32)`)
  // Do we prefer the shift to be shift-right, shift-left, or rotate.
  // Note: Its only valid to convert the rotate version to the shift version iff
  // the shift-amt (`C1`) is a power of 2 (including 0).
  // If ShiftOpc (current Opcode) is returned, do nothing.
  virtual unsigned preferedOpcodeForCmpEqPiecesOfOperand(
      EVT VT, unsigned ShiftOpc, bool MayTransformRotate,
      const APInt &ShiftOrRotateAmt,
      const std::optional<APInt> &AndMask) const {
    return ShiftOpc;
  }

  /// These two forms are equivalent:
  ///   sub %y, (xor %x, -1)
  ///   add (add %x, 1), %y
  /// The variant with two add's is IR-canonical.
  /// Some targets may prefer one to the other.
  virtual bool preferIncOfAddToSubOfNot(EVT VT) const {
    // By default, let's assume that everyone prefers the form with two add's.
    return true;
  }

  // By default prefer folding (abs (sub nsw x, y)) -> abds(x, y). Some targets
  // may want to avoid this to prevent loss of sub_nsw pattern.
  virtual bool preferABDSToABSWithNSW(EVT VT) const {
    return true;
  }

  // Return true if the target wants to transform Op(Splat(X)) -> Splat(Op(X))
  virtual bool preferScalarizeSplat(SDNode *N) const { return true; }

  // Return true if the target wants to transform:
  // (TruncVT truncate(sext_in_reg(VT X, ExtVT))
  //  -> (TruncVT sext_in_reg(truncate(VT X), ExtVT))
  // Some targets might prefer pre-sextinreg to improve truncation/saturation.
  virtual bool preferSextInRegOfTruncate(EVT TruncVT, EVT VT, EVT ExtVT) const {
    return true;
  }

  /// Return true if the target wants to use the optimization that
  /// turns ext(promotableInst1(...(promotableInstN(load)))) into
  /// promotedInst1(...(promotedInstN(ext(load)))).
  bool enableExtLdPromotion() const { return EnableExtLdPromotion; }

  /// Return true if the target can combine store(extractelement VectorTy,
  /// Idx).
  /// \p Cost[out] gives the cost of that transformation when this is true.
  virtual bool canCombineStoreAndExtract(Type *VectorTy, Value *Idx,
                                         unsigned &Cost) const {
    return false;
  }

  /// Return true if the target shall perform extract vector element and store
  /// given that the vector is known to be splat of constant.
  /// \p Index[out] gives the index of the vector element to be extracted when
  /// this is true.
  virtual bool shallExtractConstSplatVectorElementToStore(
      Type *VectorTy, unsigned ElemSizeInBits, unsigned &Index) const {
    return false;
  }

  /// Return true if inserting a scalar into a variable element of an undef
  /// vector is more efficiently handled by splatting the scalar instead.
  virtual bool shouldSplatInsEltVarIndex(EVT) const {
    return false;
  }

  /// Return true if target always benefits from combining into FMA for a
  /// given value type. This must typically return false on targets where FMA
  /// takes more cycles to execute than FADD.
  virtual bool enableAggressiveFMAFusion(EVT VT) const { return false; }

  /// Return true if target always benefits from combining into FMA for a
  /// given value type. This must typically return false on targets where FMA
  /// takes more cycles to execute than FADD.
  virtual bool enableAggressiveFMAFusion(LLT Ty) const { return false; }

  /// Return the ValueType of the result of SETCC operations.
  virtual EVT getSetCCResultType(const DataLayout &DL, LLVMContext &Context,
                                 EVT VT) const;

  /// Return the ValueType for comparison libcalls. Comparison libcalls include
  /// floating point comparison calls, and Ordered/Unordered check calls on
  /// floating point numbers.
  virtual
  MVT::SimpleValueType getCmpLibcallReturnType() const;

  /// For targets without i1 registers, this gives the nature of the high-bits
  /// of boolean values held in types wider than i1.
  ///
  /// "Boolean values" are special true/false values produced by nodes like
  /// SETCC and consumed (as the condition) by nodes like SELECT and BRCOND.
  /// Not to be confused with general values promoted from i1.  Some cpus
  /// distinguish between vectors of boolean and scalars; the isVec parameter
  /// selects between the two kinds.  For example on X86 a scalar boolean should
  /// be zero extended from i1, while the elements of a vector of booleans
  /// should be sign extended from i1.
  ///
  /// Some cpus also treat floating point types the same way as they treat
  /// vectors instead of the way they treat scalars.
  BooleanContent getBooleanContents(bool isVec, bool isFloat) const {
    if (isVec)
      return BooleanVectorContents;
    return isFloat ? BooleanFloatContents : BooleanContents;
  }

  BooleanContent getBooleanContents(EVT Type) const {
    return getBooleanContents(Type.isVector(), Type.isFloatingPoint());
  }

  /// Promote the given target boolean to a target boolean of the given type.
  /// A target boolean is an integer value, not necessarily of type i1, the bits
  /// of which conform to getBooleanContents.
  ///
  /// ValVT is the type of values that produced the boolean.
  SDValue promoteTargetBoolean(SelectionDAG &DAG, SDValue Bool,
                               EVT ValVT) const {
    SDLoc dl(Bool);
    EVT BoolVT =
        getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), ValVT);
    ISD::NodeType ExtendCode = getExtendForContent(getBooleanContents(ValVT));
    return DAG.getNode(ExtendCode, dl, BoolVT, Bool);
  }

  /// Return target scheduling preference.
  Sched::Preference getSchedulingPreference() const {
    return SchedPreferenceInfo;
  }

  /// Some scheduler, e.g. hybrid, can switch to different scheduling heuristics
  /// for different nodes. This function returns the preference (or none) for
  /// the given node.
  virtual Sched::Preference getSchedulingPreference(SDNode *) const {
    return Sched::None;
  }

  /// Return the register class that should be used for the specified value
  /// type.
  virtual const TargetRegisterClass *getRegClassFor(MVT VT, bool isDivergent = false) const {
    (void)isDivergent;
    const TargetRegisterClass *RC = RegClassForVT[VT.SimpleTy];
    assert(RC && "This value type is not natively supported!");
    return RC;
  }

  /// Allows target to decide about the register class of the
  /// specific value that is live outside the defining block.
  /// Returns true if the value needs uniform register class.
  virtual bool requiresUniformRegister(MachineFunction &MF,
                                       const Value *) const {
    return false;
  }

  /// Return the 'representative' register class for the specified value
  /// type.
  ///
  /// The 'representative' register class is the largest legal super-reg
  /// register class for the register class of the value type.  For example, on
  /// i386 the rep register class for i8, i16, and i32 are GR32; while the rep
  /// register class is GR64 on x86_64.
  virtual const TargetRegisterClass *getRepRegClassFor(MVT VT) const {
    const TargetRegisterClass *RC = RepRegClassForVT[VT.SimpleTy];
    return RC;
  }

  /// Return the cost of the 'representative' register class for the specified
  /// value type.
  virtual uint8_t getRepRegClassCostFor(MVT VT) const {
    return RepRegClassCostForVT[VT.SimpleTy];
  }

  /// Return the preferred strategy to legalize tihs SHIFT instruction, with
  /// \p ExpansionFactor being the recursion depth - how many expansion needed.
  enum class ShiftLegalizationStrategy {
    ExpandToParts,
    ExpandThroughStack,
    LowerToLibcall
  };
  virtual ShiftLegalizationStrategy
  preferredShiftLegalizationStrategy(SelectionDAG &DAG, SDNode *N,
                                     unsigned ExpansionFactor) const {
    if (ExpansionFactor == 1)
      return ShiftLegalizationStrategy::ExpandToParts;
    return ShiftLegalizationStrategy::ExpandThroughStack;
  }

  /// Return true if the target has native support for the specified value type.
  /// This means that it has a register that directly holds it without
  /// promotions or expansions.
  bool isTypeLegal(EVT VT) const {
    assert(!VT.isSimple() ||
           (unsigned)VT.getSimpleVT().SimpleTy < std::size(RegClassForVT));
    return VT.isSimple() && RegClassForVT[VT.getSimpleVT().SimpleTy] != nullptr;
  }

  class ValueTypeActionImpl {
    /// ValueTypeActions - For each value type, keep a LegalizeTypeAction enum
    /// that indicates how instruction selection should deal with the type.
    LegalizeTypeAction ValueTypeActions[MVT::VALUETYPE_SIZE];

  public:
    ValueTypeActionImpl() {
      std::fill(std::begin(ValueTypeActions), std::end(ValueTypeActions),
                TypeLegal);
    }

    LegalizeTypeAction getTypeAction(MVT VT) const {
      return ValueTypeActions[VT.SimpleTy];
    }

    void setTypeAction(MVT VT, LegalizeTypeAction Action) {
      ValueTypeActions[VT.SimpleTy] = Action;
    }
  };

  const ValueTypeActionImpl &getValueTypeActions() const {
    return ValueTypeActions;
  }

  /// Return pair that represents the legalization kind (first) that needs to
  /// happen to EVT (second) in order to type-legalize it.
  ///
  /// First: how we should legalize values of this type, either it is already
  /// legal (return 'Legal') or we need to promote it to a larger type (return
  /// 'Promote'), or we need to expand it into multiple registers of smaller
  /// integer type (return 'Expand').  'Custom' is not an option.
  ///
  /// Second: for types supported by the target, this is an identity function.
  /// For types that must be promoted to larger types, this returns the larger
  /// type to promote to.  For integer types that are larger than the largest
  /// integer register, this contains one step in the expansion to get to the
  /// smaller register. For illegal floating point types, this returns the
  /// integer type to transform to.
  LegalizeKind getTypeConversion(LLVMContext &Context, EVT VT) const;

  /// Return how we should legalize values of this type, either it is already
  /// legal (return 'Legal') or we need to promote it to a larger type (return
  /// 'Promote'), or we need to expand it into multiple registers of smaller
  /// integer type (return 'Expand').  'Custom' is not an option.
  LegalizeTypeAction getTypeAction(LLVMContext &Context, EVT VT) const {
    return getTypeConversion(Context, VT).first;
  }
  LegalizeTypeAction getTypeAction(MVT VT) const {
    return ValueTypeActions.getTypeAction(VT);
  }

  /// For types supported by the target, this is an identity function.  For
  /// types that must be promoted to larger types, this returns the larger type
  /// to promote to.  For integer types that are larger than the largest integer
  /// register, this contains one step in the expansion to get to the smaller
  /// register. For illegal floating point types, this returns the integer type
  /// to transform to.
  virtual EVT getTypeToTransformTo(LLVMContext &Context, EVT VT) const {
    return getTypeConversion(Context, VT).second;
  }

  /// For types supported by the target, this is an identity function.  For
  /// types that must be expanded (i.e. integer types that are larger than the
  /// largest integer register or illegal floating point types), this returns
  /// the largest legal type it will be expanded to.
  EVT getTypeToExpandTo(LLVMContext &Context, EVT VT) const {
    assert(!VT.isVector());
    while (true) {
      switch (getTypeAction(Context, VT)) {
      case TypeLegal:
        return VT;
      case TypeExpandInteger:
        VT = getTypeToTransformTo(Context, VT);
        break;
      default:
        llvm_unreachable("Type is not legal nor is it to be expanded!");
      }
    }
  }

  /// Vector types are broken down into some number of legal first class types.
  /// For example, EVT::v8f32 maps to 2 EVT::v4f32 with Altivec or SSE1, or 8
  /// promoted EVT::f64 values with the X86 FP stack.  Similarly, EVT::v2i64
  /// turns into 4 EVT::i32 values with both PPC and X86.
  ///
  /// This method returns the number of registers needed, and the VT for each
  /// register.  It also returns the VT and quantity of the intermediate values
  /// before they are promoted/expanded.
  unsigned getVectorTypeBreakdown(LLVMContext &Context, EVT VT,
                                  EVT &IntermediateVT,
                                  unsigned &NumIntermediates,
                                  MVT &RegisterVT) const;

  /// Certain targets such as MIPS require that some types such as vectors are
  /// always broken down into scalars in some contexts. This occurs even if the
  /// vector type is legal.
  virtual unsigned getVectorTypeBreakdownForCallingConv(
      LLVMContext &Context, CallingConv::ID CC, EVT VT, EVT &IntermediateVT,
      unsigned &NumIntermediates, MVT &RegisterVT) const {
    return getVectorTypeBreakdown(Context, VT, IntermediateVT, NumIntermediates,
                                  RegisterVT);
  }

  struct IntrinsicInfo {
    unsigned     opc = 0;          // target opcode
    EVT          memVT;            // memory VT

    // value representing memory location
    PointerUnion<const Value *, const PseudoSourceValue *> ptrVal;

    // Fallback address space for use if ptrVal is nullptr. std::nullopt means
    // unknown address space.
    std::optional<unsigned> fallbackAddressSpace;

    int          offset = 0;       // offset off of ptrVal
    uint64_t     size = 0;         // the size of the memory location
                                   // (taken from memVT if zero)
    MaybeAlign align = Align(1);   // alignment

    MachineMemOperand::Flags flags = MachineMemOperand::MONone;
    IntrinsicInfo() = default;
  };

  /// Given an intrinsic, checks if on the target the intrinsic will need to map
  /// to a MemIntrinsicNode (touches memory). If this is the case, it returns
  /// true and store the intrinsic information into the IntrinsicInfo that was
  /// passed to the function.
  virtual bool getTgtMemIntrinsic(IntrinsicInfo &, const CallInst &,
                                  MachineFunction &,
                                  unsigned /*Intrinsic*/) const {
    return false;
  }

  /// Returns true if the target can instruction select the specified FP
  /// immediate natively. If false, the legalizer will materialize the FP
  /// immediate as a load from a constant pool.
  virtual bool isFPImmLegal(const APFloat & /*Imm*/, EVT /*VT*/,
                            bool ForCodeSize = false) const {
    return false;
  }

  /// Targets can use this to indicate that they only support *some*
  /// VECTOR_SHUFFLE operations, those with specific masks.  By default, if a
  /// target supports the VECTOR_SHUFFLE node, all mask values are assumed to be
  /// legal.
  virtual bool isShuffleMaskLegal(ArrayRef<int> /*Mask*/, EVT /*VT*/) const {
    return true;
  }

  /// Returns true if the operation can trap for the value type.
  ///
  /// VT must be a legal type. By default, we optimistically assume most
  /// operations don't trap except for integer divide and remainder.
  virtual bool canOpTrap(unsigned Op, EVT VT) const;

  /// Similar to isShuffleMaskLegal. Targets can use this to indicate if there
  /// is a suitable VECTOR_SHUFFLE that can be used to replace a VAND with a
  /// constant pool entry.
  virtual bool isVectorClearMaskLegal(ArrayRef<int> /*Mask*/,
                                      EVT /*VT*/) const {
    return false;
  }

  /// How to legalize this custom operation?
  virtual LegalizeAction getCustomOperationAction(SDNode &Op) const {
    return Legal;
  }

  /// Return how this operation should be treated: either it is legal, needs to
  /// be promoted to a larger size, needs to be expanded to some other code
  /// sequence, or the target has a custom expander for it.
  LegalizeAction getOperationAction(unsigned Op, EVT VT) const {
    // If a target-specific SDNode requires legalization, require the target
    // to provide custom legalization for it.
    if (Op >= std::size(OpActions[0]))
      return Custom;
    if (VT.isExtended())
      return Expand;
    return OpActions[(unsigned)VT.getSimpleVT().SimpleTy][Op];
  }

  /// Custom method defined by each target to indicate if an operation which
  /// may require a scale is supported natively by the target.
  /// If not, the operation is illegal.
  virtual bool isSupportedFixedPointOperation(unsigned Op, EVT VT,
                                              unsigned Scale) const {
    return false;
  }

  /// Some fixed point operations may be natively supported by the target but
  /// only for specific scales. This method allows for checking
  /// if the width is supported by the target for a given operation that may
  /// depend on scale.
  LegalizeAction getFixedPointOperationAction(unsigned Op, EVT VT,
                                              unsigned Scale) const {
    auto Action = getOperationAction(Op, VT);
    if (Action != Legal)
      return Action;

    // This operation is supported in this type but may only work on specific
    // scales.
    bool Supported;
    switch (Op) {
    default:
      llvm_unreachable("Unexpected fixed point operation.");
    case ISD::SMULFIX:
    case ISD::SMULFIXSAT:
    case ISD::UMULFIX:
    case ISD::UMULFIXSAT:
    case ISD::SDIVFIX:
    case ISD::SDIVFIXSAT:
    case ISD::UDIVFIX:
    case ISD::UDIVFIXSAT:
      Supported = isSupportedFixedPointOperation(Op, VT, Scale);
      break;
    }

    return Supported ? Action : Expand;
  }

  // If Op is a strict floating-point operation, return the result
  // of getOperationAction for the equivalent non-strict operation.
  LegalizeAction getStrictFPOperationAction(unsigned Op, EVT VT) const {
    unsigned EqOpc;
    switch (Op) {
      default: llvm_unreachable("Unexpected FP pseudo-opcode");
#define DAG_INSTRUCTION(NAME, NARG, ROUND_MODE, INTRINSIC, DAGN)               \
      case ISD::STRICT_##DAGN: EqOpc = ISD::DAGN; break;
#define CMP_INSTRUCTION(NAME, NARG, ROUND_MODE, INTRINSIC, DAGN)               \
      case ISD::STRICT_##DAGN: EqOpc = ISD::SETCC; break;
#include "llvm/IR/ConstrainedOps.def"
    }

    return getOperationAction(EqOpc, VT);
  }

  /// Return true if the specified operation is legal on this target or can be
  /// made legal with custom lowering. This is used to help guide high-level
  /// lowering decisions. LegalOnly is an optional convenience for code paths
  /// traversed pre and post legalisation.
  bool isOperationLegalOrCustom(unsigned Op, EVT VT,
                                bool LegalOnly = false) const {
    if (LegalOnly)
      return isOperationLegal(Op, VT);

    return (VT == MVT::Other || isTypeLegal(VT)) &&
      (getOperationAction(Op, VT) == Legal ||
       getOperationAction(Op, VT) == Custom);
  }

  /// Return true if the specified operation is legal on this target or can be
  /// made legal using promotion. This is used to help guide high-level lowering
  /// decisions. LegalOnly is an optional convenience for code paths traversed
  /// pre and post legalisation.
  bool isOperationLegalOrPromote(unsigned Op, EVT VT,
                                 bool LegalOnly = false) const {
    if (LegalOnly)
      return isOperationLegal(Op, VT);

    return (VT == MVT::Other || isTypeLegal(VT)) &&
      (getOperationAction(Op, VT) == Legal ||
       getOperationAction(Op, VT) == Promote);
  }

  /// Return true if the specified operation is legal on this target or can be
  /// made legal with custom lowering or using promotion. This is used to help
  /// guide high-level lowering decisions. LegalOnly is an optional convenience
  /// for code paths traversed pre and post legalisation.
  bool isOperationLegalOrCustomOrPromote(unsigned Op, EVT VT,
                                         bool LegalOnly = false) const {
    if (LegalOnly)
      return isOperationLegal(Op, VT);

    return (VT == MVT::Other || isTypeLegal(VT)) &&
      (getOperationAction(Op, VT) == Legal ||
       getOperationAction(Op, VT) == Custom ||
       getOperationAction(Op, VT) == Promote);
  }

  /// Return true if the operation uses custom lowering, regardless of whether
  /// the type is legal or not.
  bool isOperationCustom(unsigned Op, EVT VT) const {
    return getOperationAction(Op, VT) == Custom;
  }

  /// Return true if lowering to a jump table is allowed.
  virtual bool areJTsAllowed(const Function *Fn) const {
    if (Fn->getFnAttribute("no-jump-tables").getValueAsBool())
      return false;

    return isOperationLegalOrCustom(ISD::BR_JT, MVT::Other) ||
           isOperationLegalOrCustom(ISD::BRIND, MVT::Other);
  }

  /// Check whether the range [Low,High] fits in a machine word.
  bool rangeFitsInWord(const APInt &Low, const APInt &High,
                       const DataLayout &DL) const {
    // FIXME: Using the pointer type doesn't seem ideal.
    uint64_t BW = DL.getIndexSizeInBits(0u);
    uint64_t Range = (High - Low).getLimitedValue(UINT64_MAX - 1) + 1;
    return Range <= BW;
  }

  /// Return true if lowering to a jump table is suitable for a set of case
  /// clusters which may contain \p NumCases cases, \p Range range of values.
  virtual bool isSuitableForJumpTable(const SwitchInst *SI, uint64_t NumCases,
                                      uint64_t Range, ProfileSummaryInfo *PSI,
                                      BlockFrequencyInfo *BFI) const;

  /// Returns preferred type for switch condition.
  virtual MVT getPreferredSwitchConditionType(LLVMContext &Context,
                                              EVT ConditionVT) const;

  /// Return true if lowering to a bit test is suitable for a set of case
  /// clusters which contains \p NumDests unique destinations, \p Low and
  /// \p High as its lowest and highest case values, and expects \p NumCmps
  /// case value comparisons. Check if the number of destinations, comparison
  /// metric, and range are all suitable.
  bool isSuitableForBitTests(unsigned NumDests, unsigned NumCmps,
                             const APInt &Low, const APInt &High,
                             const DataLayout &DL) const {
    // FIXME: I don't think NumCmps is the correct metric: a single case and a
    // range of cases both require only one branch to lower. Just looking at the
    // number of clusters and destinations should be enough to decide whether to
    // build bit tests.

    // To lower a range with bit tests, the range must fit the bitwidth of a
    // machine word.
    if (!rangeFitsInWord(Low, High, DL))
      return false;

    // Decide whether it's profitable to lower this range with bit tests. Each
    // destination requires a bit test and branch, and there is an overall range
    // check branch. For a small number of clusters, separate comparisons might
    // be cheaper, and for many destinations, splitting the range might be
    // better.
    return (NumDests == 1 && NumCmps >= 3) || (NumDests == 2 && NumCmps >= 5) ||
           (NumDests == 3 && NumCmps >= 6);
  }

  /// Return true if the specified operation is illegal on this target or
  /// unlikely to be made legal with custom lowering. This is used to help guide
  /// high-level lowering decisions.
  bool isOperationExpand(unsigned Op, EVT VT) const {
    return (!isTypeLegal(VT) || getOperationAction(Op, VT) == Expand);
  }

  /// Return true if the specified operation is legal on this target.
  bool isOperationLegal(unsigned Op, EVT VT) const {
    return (VT == MVT::Other || isTypeLegal(VT)) &&
           getOperationAction(Op, VT) == Legal;
  }

  /// Return how this load with extension should be treated: either it is legal,
  /// needs to be promoted to a larger size, needs to be expanded to some other
  /// code sequence, or the target has a custom expander for it.
  LegalizeAction getLoadExtAction(unsigned ExtType, EVT ValVT,
                                  EVT MemVT) const {
    if (ValVT.isExtended() || MemVT.isExtended()) return Expand;
    unsigned ValI = (unsigned) ValVT.getSimpleVT().SimpleTy;
    unsigned MemI = (unsigned) MemVT.getSimpleVT().SimpleTy;
    assert(ExtType < ISD::LAST_LOADEXT_TYPE && ValI < MVT::VALUETYPE_SIZE &&
           MemI < MVT::VALUETYPE_SIZE && "Table isn't big enough!");
    unsigned Shift = 4 * ExtType;
    return (LegalizeAction)((LoadExtActions[ValI][MemI] >> Shift) & 0xf);
  }

  /// Return true if the specified load with extension is legal on this target.
  bool isLoadExtLegal(unsigned ExtType, EVT ValVT, EVT MemVT) const {
    return getLoadExtAction(ExtType, ValVT, MemVT) == Legal;
  }

  /// Return true if the specified load with extension is legal or custom
  /// on this target.
  bool isLoadExtLegalOrCustom(unsigned ExtType, EVT ValVT, EVT MemVT) const {
    return getLoadExtAction(ExtType, ValVT, MemVT) == Legal ||
           getLoadExtAction(ExtType, ValVT, MemVT) == Custom;
  }

  /// Same as getLoadExtAction, but for atomic loads.
  LegalizeAction getAtomicLoadExtAction(unsigned ExtType, EVT ValVT,
                                        EVT MemVT) const {
    if (ValVT.isExtended() || MemVT.isExtended()) return Expand;
    unsigned ValI = (unsigned)ValVT.getSimpleVT().SimpleTy;
    unsigned MemI = (unsigned)MemVT.getSimpleVT().SimpleTy;
    assert(ExtType < ISD::LAST_LOADEXT_TYPE && ValI < MVT::VALUETYPE_SIZE &&
           MemI < MVT::VALUETYPE_SIZE && "Table isn't big enough!");
    unsigned Shift = 4 * ExtType;
    LegalizeAction Action =
        (LegalizeAction)((AtomicLoadExtActions[ValI][MemI] >> Shift) & 0xf);
    assert((Action == Legal || Action == Expand) &&
           "Unsupported atomic load extension action.");
    return Action;
  }

  /// Return true if the specified atomic load with extension is legal on
  /// this target.
  bool isAtomicLoadExtLegal(unsigned ExtType, EVT ValVT, EVT MemVT) const {
    return getAtomicLoadExtAction(ExtType, ValVT, MemVT) == Legal;
  }

  /// Return how this store with truncation should be treated: either it is
  /// legal, needs to be promoted to a larger size, needs to be expanded to some
  /// other code sequence, or the target has a custom expander for it.
  LegalizeAction getTruncStoreAction(EVT ValVT, EVT MemVT) const {
    if (ValVT.isExtended() || MemVT.isExtended()) return Expand;
    unsigned ValI = (unsigned) ValVT.getSimpleVT().SimpleTy;
    unsigned MemI = (unsigned) MemVT.getSimpleVT().SimpleTy;
    assert(ValI < MVT::VALUETYPE_SIZE && MemI < MVT::VALUETYPE_SIZE &&
           "Table isn't big enough!");
    return TruncStoreActions[ValI][MemI];
  }

  /// Return true if the specified store with truncation is legal on this
  /// target.
  bool isTruncStoreLegal(EVT ValVT, EVT MemVT) const {
    return isTypeLegal(ValVT) && getTruncStoreAction(ValVT, MemVT) == Legal;
  }

  /// Return true if the specified store with truncation has solution on this
  /// target.
  bool isTruncStoreLegalOrCustom(EVT ValVT, EVT MemVT) const {
    return isTypeLegal(ValVT) &&
      (getTruncStoreAction(ValVT, MemVT) == Legal ||
       getTruncStoreAction(ValVT, MemVT) == Custom);
  }

  virtual bool canCombineTruncStore(EVT ValVT, EVT MemVT,
                                    bool LegalOnly) const {
    if (LegalOnly)
      return isTruncStoreLegal(ValVT, MemVT);

    return isTruncStoreLegalOrCustom(ValVT, MemVT);
  }

  /// Return how the indexed load should be treated: either it is legal, needs
  /// to be promoted to a larger size, needs to be expanded to some other code
  /// sequence, or the target has a custom expander for it.
  LegalizeAction getIndexedLoadAction(unsigned IdxMode, MVT VT) const {
    return getIndexedModeAction(IdxMode, VT, IMAB_Load);
  }

  /// Return true if the specified indexed load is legal on this target.
  bool isIndexedLoadLegal(unsigned IdxMode, EVT VT) const {
    return VT.isSimple() &&
      (getIndexedLoadAction(IdxMode, VT.getSimpleVT()) == Legal ||
       getIndexedLoadAction(IdxMode, VT.getSimpleVT()) == Custom);
  }

  /// Return how the indexed store should be treated: either it is legal, needs
  /// to be promoted to a larger size, needs to be expanded to some other code
  /// sequence, or the target has a custom expander for it.
  LegalizeAction getIndexedStoreAction(unsigned IdxMode, MVT VT) const {
    return getIndexedModeAction(IdxMode, VT, IMAB_Store);
  }

  /// Return true if the specified indexed load is legal on this target.
  bool isIndexedStoreLegal(unsigned IdxMode, EVT VT) const {
    return VT.isSimple() &&
      (getIndexedStoreAction(IdxMode, VT.getSimpleVT()) == Legal ||
       getIndexedStoreAction(IdxMode, VT.getSimpleVT()) == Custom);
  }

  /// Return how the indexed load should be treated: either it is legal, needs
  /// to be promoted to a larger size, needs to be expanded to some other code
  /// sequence, or the target has a custom expander for it.
  LegalizeAction getIndexedMaskedLoadAction(unsigned IdxMode, MVT VT) const {
    return getIndexedModeAction(IdxMode, VT, IMAB_MaskedLoad);
  }

  /// Return true if the specified indexed load is legal on this target.
  bool isIndexedMaskedLoadLegal(unsigned IdxMode, EVT VT) const {
    return VT.isSimple() &&
           (getIndexedMaskedLoadAction(IdxMode, VT.getSimpleVT()) == Legal ||
            getIndexedMaskedLoadAction(IdxMode, VT.getSimpleVT()) == Custom);
  }

  /// Return how the indexed store should be treated: either it is legal, needs
  /// to be promoted to a larger size, needs to be expanded to some other code
  /// sequence, or the target has a custom expander for it.
  LegalizeAction getIndexedMaskedStoreAction(unsigned IdxMode, MVT VT) const {
    return getIndexedModeAction(IdxMode, VT, IMAB_MaskedStore);
  }

  /// Return true if the specified indexed load is legal on this target.
  bool isIndexedMaskedStoreLegal(unsigned IdxMode, EVT VT) const {
    return VT.isSimple() &&
           (getIndexedMaskedStoreAction(IdxMode, VT.getSimpleVT()) == Legal ||
            getIndexedMaskedStoreAction(IdxMode, VT.getSimpleVT()) == Custom);
  }

  /// Returns true if the index type for a masked gather/scatter requires
  /// extending
  virtual bool shouldExtendGSIndex(EVT VT, EVT &EltTy) const { return false; }

  // Returns true if Extend can be folded into the index of a masked gathers/scatters
  // on this target.
  virtual bool shouldRemoveExtendFromGSIndex(SDValue Extend, EVT DataVT) const {
    return false;
  }

  // Return true if the target supports a scatter/gather instruction with
  // indices which are scaled by the particular value.  Note that all targets
  // must by definition support scale of 1.
  virtual bool isLegalScaleForGatherScatter(uint64_t Scale,
                                            uint64_t ElemSize) const {
    // MGATHER/MSCATTER are only required to support scaling by one or by the
    // element size.
    if (Scale != ElemSize && Scale != 1)
      return false;
    return true;
  }

  /// Return how the condition code should be treated: either it is legal, needs
  /// to be expanded to some other code sequence, or the target has a custom
  /// expander for it.
  LegalizeAction
  getCondCodeAction(ISD::CondCode CC, MVT VT) const {
    assert((unsigned)CC < std::size(CondCodeActions) &&
           ((unsigned)VT.SimpleTy >> 3) < std::size(CondCodeActions[0]) &&
           "Table isn't big enough!");
    // See setCondCodeAction for how this is encoded.
    uint32_t Shift = 4 * (VT.SimpleTy & 0x7);
    uint32_t Value = CondCodeActions[CC][VT.SimpleTy >> 3];
    LegalizeAction Action = (LegalizeAction) ((Value >> Shift) & 0xF);
    assert(Action != Promote && "Can't promote condition code!");
    return Action;
  }

  /// Return true if the specified condition code is legal on this target.
  bool isCondCodeLegal(ISD::CondCode CC, MVT VT) const {
    return getCondCodeAction(CC, VT) == Legal;
  }

  /// Return true if the specified condition code is legal or custom on this
  /// target.
  bool isCondCodeLegalOrCustom(ISD::CondCode CC, MVT VT) const {
    return getCondCodeAction(CC, VT) == Legal ||
           getCondCodeAction(CC, VT) == Custom;
  }

  /// If the action for this operation is to promote, this method returns the
  /// ValueType to promote to.
  MVT getTypeToPromoteTo(unsigned Op, MVT VT) const {
    assert(getOperationAction(Op, VT) == Promote &&
           "This operation isn't promoted!");

    // See if this has an explicit type specified.
    std::map<std::pair<unsigned, MVT::SimpleValueType>,
             MVT::SimpleValueType>::const_iterator PTTI =
      PromoteToType.find(std::make_pair(Op, VT.SimpleTy));
    if (PTTI != PromoteToType.end()) return PTTI->second;

    assert((VT.isInteger() || VT.isFloatingPoint()) &&
           "Cannot autopromote this type, add it with AddPromotedToType.");

    uint64_t VTBits = VT.getScalarSizeInBits();
    MVT NVT = VT;
    do {
      NVT = (MVT::SimpleValueType)(NVT.SimpleTy+1);
      assert(NVT.isInteger() == VT.isInteger() && NVT != MVT::isVoid &&
             "Didn't find type to promote to!");
    } while (VTBits >= NVT.getScalarSizeInBits() || !isTypeLegal(NVT) ||
             getOperationAction(Op, NVT) == Promote);
    return NVT;
  }

  virtual EVT getAsmOperandValueType(const DataLayout &DL, Type *Ty,
                                     bool AllowUnknown = false) const {
    return getValueType(DL, Ty, AllowUnknown);
  }

  /// Return the EVT corresponding to this LLVM type.  This is fixed by the LLVM
  /// operations except for the pointer size.  If AllowUnknown is true, this
  /// will return MVT::Other for types with no EVT counterpart (e.g. structs),
  /// otherwise it will assert.
  EVT getValueType(const DataLayout &DL, Type *Ty,
                   bool AllowUnknown = false) const {
    // Lower scalar pointers to native pointer types.
    if (auto *PTy = dyn_cast<PointerType>(Ty))
      return getPointerTy(DL, PTy->getAddressSpace());

    if (auto *VTy = dyn_cast<VectorType>(Ty)) {
      Type *EltTy = VTy->getElementType();
      // Lower vectors of pointers to native pointer types.
      if (auto *PTy = dyn_cast<PointerType>(EltTy)) {
        EVT PointerTy(getPointerTy(DL, PTy->getAddressSpace()));
        EltTy = PointerTy.getTypeForEVT(Ty->getContext());
      }
      return EVT::getVectorVT(Ty->getContext(), EVT::getEVT(EltTy, false),
                              VTy->getElementCount());
    }

    return EVT::getEVT(Ty, AllowUnknown);
  }

  EVT getMemValueType(const DataLayout &DL, Type *Ty,
                      bool AllowUnknown = false) const {
    // Lower scalar pointers to native pointer types.
    if (auto *PTy = dyn_cast<PointerType>(Ty))
      return getPointerMemTy(DL, PTy->getAddressSpace());

    if (auto *VTy = dyn_cast<VectorType>(Ty)) {
      Type *EltTy = VTy->getElementType();
      if (auto *PTy = dyn_cast<PointerType>(EltTy)) {
        EVT PointerTy(getPointerMemTy(DL, PTy->getAddressSpace()));
        EltTy = PointerTy.getTypeForEVT(Ty->getContext());
      }
      return EVT::getVectorVT(Ty->getContext(), EVT::getEVT(EltTy, false),
                              VTy->getElementCount());
    }

    return getValueType(DL, Ty, AllowUnknown);
  }


  /// Return the MVT corresponding to this LLVM type. See getValueType.
  MVT getSimpleValueType(const DataLayout &DL, Type *Ty,
                         bool AllowUnknown = false) const {
    return getValueType(DL, Ty, AllowUnknown).getSimpleVT();
  }

  /// Return the desired alignment for ByVal or InAlloca aggregate function
  /// arguments in the caller parameter area.  This is the actual alignment, not
  /// its logarithm.
  virtual uint64_t getByValTypeAlignment(Type *Ty, const DataLayout &DL) const;

  /// Return the type of registers that this ValueType will eventually require.
  MVT getRegisterType(MVT VT) const {
    assert((unsigned)VT.SimpleTy < std::size(RegisterTypeForVT));
    return RegisterTypeForVT[VT.SimpleTy];
  }

  /// Return the type of registers that this ValueType will eventually require.
  MVT getRegisterType(LLVMContext &Context, EVT VT) const {
    if (VT.isSimple())
      return getRegisterType(VT.getSimpleVT());
    if (VT.isVector()) {
      EVT VT1;
      MVT RegisterVT;
      unsigned NumIntermediates;
      (void)getVectorTypeBreakdown(Context, VT, VT1,
                                   NumIntermediates, RegisterVT);
      return RegisterVT;
    }
    if (VT.isInteger()) {
      return getRegisterType(Context, getTypeToTransformTo(Context, VT));
    }
    llvm_unreachable("Unsupported extended type!");
  }

  /// Return the number of registers that this ValueType will eventually
  /// require.
  ///
  /// This is one for any types promoted to live in larger registers, but may be
  /// more than one for types (like i64) that are split into pieces.  For types
  /// like i140, which are first promoted then expanded, it is the number of
  /// registers needed to hold all the bits of the original type.  For an i140
  /// on a 32 bit machine this means 5 registers.
  ///
  /// RegisterVT may be passed as a way to override the default settings, for
  /// instance with i128 inline assembly operands on SystemZ.
  virtual unsigned
  getNumRegisters(LLVMContext &Context, EVT VT,
                  std::optional<MVT> RegisterVT = std::nullopt) const {
    if (VT.isSimple()) {
      assert((unsigned)VT.getSimpleVT().SimpleTy <
             std::size(NumRegistersForVT));
      return NumRegistersForVT[VT.getSimpleVT().SimpleTy];
    }
    if (VT.isVector()) {
      EVT VT1;
      MVT VT2;
      unsigned NumIntermediates;
      return getVectorTypeBreakdown(Context, VT, VT1, NumIntermediates, VT2);
    }
    if (VT.isInteger()) {
      unsigned BitWidth = VT.getSizeInBits();
      unsigned RegWidth = getRegisterType(Context, VT).getSizeInBits();
      return (BitWidth + RegWidth - 1) / RegWidth;
    }
    llvm_unreachable("Unsupported extended type!");
  }

  /// Certain combinations of ABIs, Targets and features require that types
  /// are legal for some operations and not for other operations.
  /// For MIPS all vector types must be passed through the integer register set.
  virtual MVT getRegisterTypeForCallingConv(LLVMContext &Context,
                                            CallingConv::ID CC, EVT VT) const {
    return getRegisterType(Context, VT);
  }

  /// Certain targets require unusual breakdowns of certain types. For MIPS,
  /// this occurs when a vector type is used, as vector are passed through the
  /// integer register set.
  virtual unsigned getNumRegistersForCallingConv(LLVMContext &Context,
                                                 CallingConv::ID CC,
                                                 EVT VT) const {
    return getNumRegisters(Context, VT);
  }

  /// Certain targets have context sensitive alignment requirements, where one
  /// type has the alignment requirement of another type.
  virtual Align getABIAlignmentForCallingConv(Type *ArgTy,
                                              const DataLayout &DL) const {
    return DL.getABITypeAlign(ArgTy);
  }

  /// If true, then instruction selection should seek to shrink the FP constant
  /// of the specified type to a smaller type in order to save space and / or
  /// reduce runtime.
  virtual bool ShouldShrinkFPConstant(EVT) const { return true; }

  /// Return true if it is profitable to reduce a load to a smaller type.
  /// Example: (i16 (trunc (i32 (load x))) -> i16 load x
  virtual bool shouldReduceLoadWidth(SDNode *Load, ISD::LoadExtType ExtTy,
                                     EVT NewVT) const {
    // By default, assume that it is cheaper to extract a subvector from a wide
    // vector load rather than creating multiple narrow vector loads.
    if (NewVT.isVector() && !Load->hasOneUse())
      return false;

    return true;
  }

  /// Return true (the default) if it is profitable to remove a sext_inreg(x)
  /// where the sext is redundant, and use x directly.
  virtual bool shouldRemoveRedundantExtend(SDValue Op) const { return true; }

  /// Indicates if any padding is guaranteed to go at the most significant bits
  /// when storing the type to memory and the type size isn't equal to the store
  /// size.
  bool isPaddedAtMostSignificantBitsWhenStored(EVT VT) const {
    return VT.isScalarInteger() && !VT.isByteSized();
  }

  /// When splitting a value of the specified type into parts, does the Lo
  /// or Hi part come first?  This usually follows the endianness, except
  /// for ppcf128, where the Hi part always comes first.
  bool hasBigEndianPartOrdering(EVT VT, const DataLayout &DL) const {
    return DL.isBigEndian() || VT == MVT::ppcf128;
  }

  /// If true, the target has custom DAG combine transformations that it can
  /// perform for the specified node.
  bool hasTargetDAGCombine(ISD::NodeType NT) const {
    assert(unsigned(NT >> 3) < std::size(TargetDAGCombineArray));
    return TargetDAGCombineArray[NT >> 3] & (1 << (NT&7));
  }

  unsigned getGatherAllAliasesMaxDepth() const {
    return GatherAllAliasesMaxDepth;
  }

  /// Returns the size of the platform's va_list object.
  virtual unsigned getVaListSizeInBits(const DataLayout &DL) const {
    return getPointerTy(DL).getSizeInBits();
  }

  /// Get maximum # of store operations permitted for llvm.memset
  ///
  /// This function returns the maximum number of store operations permitted
  /// to replace a call to llvm.memset. The value is set by the target at the
  /// performance threshold for such a replacement. If OptSize is true,
  /// return the limit for functions that have OptSize attribute.
  unsigned getMaxStoresPerMemset(bool OptSize) const {
    return OptSize ? MaxStoresPerMemsetOptSize : MaxStoresPerMemset;
  }

  /// Get maximum # of store operations permitted for llvm.memcpy
  ///
  /// This function returns the maximum number of store operations permitted
  /// to replace a call to llvm.memcpy. The value is set by the target at the
  /// performance threshold for such a replacement. If OptSize is true,
  /// return the limit for functions that have OptSize attribute.
  unsigned getMaxStoresPerMemcpy(bool OptSize) const {
    return OptSize ? MaxStoresPerMemcpyOptSize : MaxStoresPerMemcpy;
  }

  /// \brief Get maximum # of store operations to be glued together
  ///
  /// This function returns the maximum number of store operations permitted
  /// to glue together during lowering of llvm.memcpy. The value is set by
  //  the target at the performance threshold for such a replacement.
  virtual unsigned getMaxGluedStoresPerMemcpy() const {
    return MaxGluedStoresPerMemcpy;
  }

  /// Get maximum # of load operations permitted for memcmp
  ///
  /// This function returns the maximum number of load operations permitted
  /// to replace a call to memcmp. The value is set by the target at the
  /// performance threshold for such a replacement. If OptSize is true,
  /// return the limit for functions that have OptSize attribute.
  unsigned getMaxExpandSizeMemcmp(bool OptSize) const {
    return OptSize ? MaxLoadsPerMemcmpOptSize : MaxLoadsPerMemcmp;
  }

  /// Get maximum # of store operations permitted for llvm.memmove
  ///
  /// This function returns the maximum number of store operations permitted
  /// to replace a call to llvm.memmove. The value is set by the target at the
  /// performance threshold for such a replacement. If OptSize is true,
  /// return the limit for functions that have OptSize attribute.
  unsigned getMaxStoresPerMemmove(bool OptSize) const {
    return OptSize ? MaxStoresPerMemmoveOptSize : MaxStoresPerMemmove;
  }

  /// Determine if the target supports unaligned memory accesses.
  ///
  /// This function returns true if the target allows unaligned memory accesses
  /// of the specified type in the given address space. If true, it also returns
  /// a relative speed of the unaligned memory access in the last argument by
  /// reference. The higher the speed number the faster the operation comparing
  /// to a number returned by another such call. This is used, for example, in
  /// situations where an array copy/move/set is converted to a sequence of
  /// store operations. Its use helps to ensure that such replacements don't
  /// generate code that causes an alignment error (trap) on the target machine.
  virtual bool allowsMisalignedMemoryAccesses(
      EVT, unsigned AddrSpace = 0, Align Alignment = Align(1),
      MachineMemOperand::Flags Flags = MachineMemOperand::MONone,
      unsigned * /*Fast*/ = nullptr) const {
    return false;
  }

  /// LLT handling variant.
  virtual bool allowsMisalignedMemoryAccesses(
      LLT, unsigned AddrSpace = 0, Align Alignment = Align(1),
      MachineMemOperand::Flags Flags = MachineMemOperand::MONone,
      unsigned * /*Fast*/ = nullptr) const {
    return false;
  }

  /// This function returns true if the memory access is aligned or if the
  /// target allows this specific unaligned memory access. If the access is
  /// allowed, the optional final parameter returns a relative speed of the
  /// access (as defined by the target).
  bool allowsMemoryAccessForAlignment(
      LLVMContext &Context, const DataLayout &DL, EVT VT,
      unsigned AddrSpace = 0, Align Alignment = Align(1),
      MachineMemOperand::Flags Flags = MachineMemOperand::MONone,
      unsigned *Fast = nullptr) const;

  /// Return true if the memory access of this type is aligned or if the target
  /// allows this specific unaligned access for the given MachineMemOperand.
  /// If the access is allowed, the optional final parameter returns a relative
  /// speed of the access (as defined by the target).
  bool allowsMemoryAccessForAlignment(LLVMContext &Context,
                                      const DataLayout &DL, EVT VT,
                                      const MachineMemOperand &MMO,
                                      unsigned *Fast = nullptr) const;

  /// Return true if the target supports a memory access of this type for the
  /// given address space and alignment. If the access is allowed, the optional
  /// final parameter returns the relative speed of the access (as defined by
  /// the target).
  virtual bool
  allowsMemoryAccess(LLVMContext &Context, const DataLayout &DL, EVT VT,
                     unsigned AddrSpace = 0, Align Alignment = Align(1),
                     MachineMemOperand::Flags Flags = MachineMemOperand::MONone,
                     unsigned *Fast = nullptr) const;

  /// Return true if the target supports a memory access of this type for the
  /// given MachineMemOperand. If the access is allowed, the optional
  /// final parameter returns the relative access speed (as defined by the
  /// target).
  bool allowsMemoryAccess(LLVMContext &Context, const DataLayout &DL, EVT VT,
                          const MachineMemOperand &MMO,
                          unsigned *Fast = nullptr) const;

  /// LLT handling variant.
  bool allowsMemoryAccess(LLVMContext &Context, const DataLayout &DL, LLT Ty,
                          const MachineMemOperand &MMO,
                          unsigned *Fast = nullptr) const;

  /// Returns the target specific optimal type for load and store operations as
  /// a result of memset, memcpy, and memmove lowering.
  /// It returns EVT::Other if the type should be determined using generic
  /// target-independent logic.
  virtual EVT
  getOptimalMemOpType(const MemOp &Op,
                      const AttributeList & /*FuncAttributes*/) const {
    return MVT::Other;
  }

  /// LLT returning variant.
  virtual LLT
  getOptimalMemOpLLT(const MemOp &Op,
                     const AttributeList & /*FuncAttributes*/) const {
    return LLT();
  }

  /// Returns true if it's safe to use load / store of the specified type to
  /// expand memcpy / memset inline.
  ///
  /// This is mostly true for all types except for some special cases. For
  /// example, on X86 targets without SSE2 f64 load / store are done with fldl /
  /// fstpl which also does type conversion. Note the specified type doesn't
  /// have to be legal as the hook is used before type legalization.
  virtual bool isSafeMemOpType(MVT /*VT*/) const { return true; }

  /// Return lower limit for number of blocks in a jump table.
  virtual unsigned getMinimumJumpTableEntries() const;

  /// Return lower limit of the density in a jump table.
  unsigned getMinimumJumpTableDensity(bool OptForSize) const;

  /// Return upper limit for number of entries in a jump table.
  /// Zero if no limit.
  unsigned getMaximumJumpTableSize() const;

  virtual bool isJumpTableRelative() const;

  /// If a physical register, this specifies the register that
  /// llvm.savestack/llvm.restorestack should save and restore.
  Register getStackPointerRegisterToSaveRestore() const {
    return StackPointerRegisterToSaveRestore;
  }

  /// If a physical register, this returns the register that receives the
  /// exception address on entry to an EH pad.
  virtual Register
  getExceptionPointerRegister(const Constant *PersonalityFn) const {
    return Register();
  }

  /// If a physical register, this returns the register that receives the
  /// exception typeid on entry to a landing pad.
  virtual Register
  getExceptionSelectorRegister(const Constant *PersonalityFn) const {
    return Register();
  }

  virtual bool needsFixedCatchObjects() const {
    report_fatal_error("Funclet EH is not implemented for this target");
  }

  /// Return the minimum stack alignment of an argument.
  Align getMinStackArgumentAlignment() const {
    return MinStackArgumentAlignment;
  }

  /// Return the minimum function alignment.
  Align getMinFunctionAlignment() const { return MinFunctionAlignment; }

  /// Return the preferred function alignment.
  Align getPrefFunctionAlignment() const { return PrefFunctionAlignment; }

  /// Return the preferred loop alignment.
  virtual Align getPrefLoopAlignment(MachineLoop *ML = nullptr) const;

  /// Return the maximum amount of bytes allowed to be emitted when padding for
  /// alignment
  virtual unsigned
  getMaxPermittedBytesForAlignment(MachineBasicBlock *MBB) const;

  /// Should loops be aligned even when the function is marked OptSize (but not
  /// MinSize).
  virtual bool alignLoopsWithOptSize() const { return false; }

  /// If the target has a standard location for the stack protector guard,
  /// returns the address of that location. Otherwise, returns nullptr.
  /// DEPRECATED: please override useLoadStackGuardNode and customize
  ///             LOAD_STACK_GUARD, or customize \@llvm.stackguard().
  virtual Value *getIRStackGuard(IRBuilderBase &IRB) const;

  /// Inserts necessary declarations for SSP (stack protection) purpose.
  /// Should be used only when getIRStackGuard returns nullptr.
  virtual void insertSSPDeclarations(Module &M) const;

  /// Return the variable that's previously inserted by insertSSPDeclarations,
  /// if any, otherwise return nullptr. Should be used only when
  /// getIRStackGuard returns nullptr.
  virtual Value *getSDagStackGuard(const Module &M) const;

  /// If this function returns true, stack protection checks should XOR the
  /// frame pointer (or whichever pointer is used to address locals) into the
  /// stack guard value before checking it. getIRStackGuard must return nullptr
  /// if this returns true.
  virtual bool useStackGuardXorFP() const { return false; }

  /// If the target has a standard stack protection check function that
  /// performs validation and error handling, returns the function. Otherwise,
  /// returns nullptr. Must be previously inserted by insertSSPDeclarations.
  /// Should be used only when getIRStackGuard returns nullptr.
  virtual Function *getSSPStackGuardCheck(const Module &M) const;

protected:
  Value *getDefaultSafeStackPointerLocation(IRBuilderBase &IRB,
                                            bool UseTLS) const;

public:
  /// Returns the target-specific address of the unsafe stack pointer.
  virtual Value *getSafeStackPointerLocation(IRBuilderBase &IRB) const;

  /// Returns the name of the symbol used to emit stack probes or the empty
  /// string if not applicable.
  virtual bool hasStackProbeSymbol(const MachineFunction &MF) const { return false; }

  virtual bool hasInlineStackProbe(const MachineFunction &MF) const { return false; }

  virtual StringRef getStackProbeSymbolName(const MachineFunction &MF) const {
    return "";
  }

  /// Returns true if a cast from SrcAS to DestAS is "cheap", such that e.g. we
  /// are happy to sink it into basic blocks. A cast may be free, but not
  /// necessarily a no-op. e.g. a free truncate from a 64-bit to 32-bit pointer.
  virtual bool isFreeAddrSpaceCast(unsigned SrcAS, unsigned DestAS) const;

  /// Return true if the pointer arguments to CI should be aligned by aligning
  /// the object whose address is being passed. If so then MinSize is set to the
  /// minimum size the object must be to be aligned and PrefAlign is set to the
  /// preferred alignment.
  virtual bool shouldAlignPointerArgs(CallInst * /*CI*/, unsigned & /*MinSize*/,
                                      Align & /*PrefAlign*/) const {
    return false;
  }

  //===--------------------------------------------------------------------===//
  /// \name Helpers for TargetTransformInfo implementations
  /// @{

  /// Get the ISD node that corresponds to the Instruction class opcode.
  int InstructionOpcodeToISD(unsigned Opcode) const;

  /// @}

  //===--------------------------------------------------------------------===//
  /// \name Helpers for atomic expansion.
  /// @{

  /// Returns the maximum atomic operation size (in bits) supported by
  /// the backend. Atomic operations greater than this size (as well
  /// as ones that are not naturally aligned), will be expanded by
  /// AtomicExpandPass into an __atomic_* library call.
  unsigned getMaxAtomicSizeInBitsSupported() const {
    return MaxAtomicSizeInBitsSupported;
  }

  /// Returns the size in bits of the maximum div/rem the backend supports.
  /// Larger operations will be expanded by ExpandLargeDivRem.
  unsigned getMaxDivRemBitWidthSupported() const {
    return MaxDivRemBitWidthSupported;
  }

  /// Returns the size in bits of the maximum larget fp convert the backend
  /// supports. Larger operations will be expanded by ExpandLargeFPConvert.
  unsigned getMaxLargeFPConvertBitWidthSupported() const {
    return MaxLargeFPConvertBitWidthSupported;
  }

  /// Returns the size of the smallest cmpxchg or ll/sc instruction
  /// the backend supports.  Any smaller operations are widened in
  /// AtomicExpandPass.
  ///
  /// Note that *unlike* operations above the maximum size, atomic ops
  /// are still natively supported below the minimum; they just
  /// require a more complex expansion.
  unsigned getMinCmpXchgSizeInBits() const { return MinCmpXchgSizeInBits; }

  /// Whether the target supports unaligned atomic operations.
  bool supportsUnalignedAtomics() const { return SupportsUnalignedAtomics; }

  /// Whether AtomicExpandPass should automatically insert fences and reduce
  /// ordering for this atomic. This should be true for most architectures with
  /// weak memory ordering. Defaults to false.
  virtual bool shouldInsertFencesForAtomic(const Instruction *I) const {
    return false;
  }

  /// Whether AtomicExpandPass should automatically insert a trailing fence
  /// without reducing the ordering for this atomic. Defaults to false.
  virtual bool
  shouldInsertTrailingFenceForAtomicStore(const Instruction *I) const {
    return false;
  }

  /// Perform a load-linked operation on Addr, returning a "Value *" with the
  /// corresponding pointee type. This may entail some non-trivial operations to
  /// truncate or reconstruct types that will be illegal in the backend. See
  /// ARMISelLowering for an example implementation.
  virtual Value *emitLoadLinked(IRBuilderBase &Builder, Type *ValueTy,
                                Value *Addr, AtomicOrdering Ord) const {
    llvm_unreachable("Load linked unimplemented on this target");
  }

  /// Perform a store-conditional operation to Addr. Return the status of the
  /// store. This should be 0 if the store succeeded, non-zero otherwise.
  virtual Value *emitStoreConditional(IRBuilderBase &Builder, Value *Val,
                                      Value *Addr, AtomicOrdering Ord) const {
    llvm_unreachable("Store conditional unimplemented on this target");
  }

  /// Perform a masked atomicrmw using a target-specific intrinsic. This
  /// represents the core LL/SC loop which will be lowered at a late stage by
  /// the backend. The target-specific intrinsic returns the loaded value and
  /// is not responsible for masking and shifting the result.
  virtual Value *emitMaskedAtomicRMWIntrinsic(IRBuilderBase &Builder,
                                              AtomicRMWInst *AI,
                                              Value *AlignedAddr, Value *Incr,
                                              Value *Mask, Value *ShiftAmt,
                                              AtomicOrdering Ord) const {
    llvm_unreachable("Masked atomicrmw expansion unimplemented on this target");
  }

  /// Perform a atomicrmw expansion using a target-specific way. This is
  /// expected to be called when masked atomicrmw and bit test atomicrmw don't
  /// work, and the target supports another way to lower atomicrmw.
  virtual void emitExpandAtomicRMW(AtomicRMWInst *AI) const {
    llvm_unreachable(
        "Generic atomicrmw expansion unimplemented on this target");
  }

  /// Perform a bit test atomicrmw using a target-specific intrinsic. This
  /// represents the combined bit test intrinsic which will be lowered at a late
  /// stage by the backend.
  virtual void emitBitTestAtomicRMWIntrinsic(AtomicRMWInst *AI) const {
    llvm_unreachable(
        "Bit test atomicrmw expansion unimplemented on this target");
  }

  /// Perform a atomicrmw which the result is only used by comparison, using a
  /// target-specific intrinsic. This represents the combined atomic and compare
  /// intrinsic which will be lowered at a late stage by the backend.
  virtual void emitCmpArithAtomicRMWIntrinsic(AtomicRMWInst *AI) const {
    llvm_unreachable(
        "Compare arith atomicrmw expansion unimplemented on this target");
  }

  /// Perform a masked cmpxchg using a target-specific intrinsic. This
  /// represents the core LL/SC loop which will be lowered at a late stage by
  /// the backend. The target-specific intrinsic returns the loaded value and
  /// is not responsible for masking and shifting the result.
  virtual Value *emitMaskedAtomicCmpXchgIntrinsic(
      IRBuilderBase &Builder, AtomicCmpXchgInst *CI, Value *AlignedAddr,
      Value *CmpVal, Value *NewVal, Value *Mask, AtomicOrdering Ord) const {
    llvm_unreachable("Masked cmpxchg expansion unimplemented on this target");
  }

  //===--------------------------------------------------------------------===//
  /// \name KCFI check lowering.
  /// @{

  virtual MachineInstr *EmitKCFICheck(MachineBasicBlock &MBB,
                                      MachineBasicBlock::instr_iterator &MBBI,
                                      const TargetInstrInfo *TII) const {
    llvm_unreachable("KCFI is not supported on this target");
  }

  /// @}

  /// Inserts in the IR a target-specific intrinsic specifying a fence.
  /// It is called by AtomicExpandPass before expanding an
  ///   AtomicRMW/AtomicCmpXchg/AtomicStore/AtomicLoad
  ///   if shouldInsertFencesForAtomic returns true.
  ///
  /// Inst is the original atomic instruction, prior to other expansions that
  /// may be performed.
  ///
  /// This function should either return a nullptr, or a pointer to an IR-level
  ///   Instruction*. Even complex fence sequences can be represented by a
  ///   single Instruction* through an intrinsic to be lowered later.
  ///
  /// The default implementation emits an IR fence before any release (or
  ///   stronger) operation that stores, and after any acquire (or stronger)
  ///   operation. This is generally a correct implementation, but backends may
  ///   override if they wish to use alternative schemes (e.g. the PowerPC
  ///   standard ABI uses a fence before a seq_cst load instead of after a
  ///   seq_cst store).
  /// @{
  virtual Instruction *emitLeadingFence(IRBuilderBase &Builder,
                                        Instruction *Inst,
                                        AtomicOrdering Ord) const;

  virtual Instruction *emitTrailingFence(IRBuilderBase &Builder,
                                         Instruction *Inst,
                                         AtomicOrdering Ord) const;
  /// @}

  // Emits code that executes when the comparison result in the ll/sc
  // expansion of a cmpxchg instruction is such that the store-conditional will
  // not execute.  This makes it possible to balance out the load-linked with
  // a dedicated instruction, if desired.
  // E.g., on ARM, if ldrex isn't followed by strex, the exclusive monitor would
  // be unnecessarily held, except if clrex, inserted by this hook, is executed.
  virtual void emitAtomicCmpXchgNoStoreLLBalance(IRBuilderBase &Builder) const {}

  /// Returns true if arguments should be sign-extended in lib calls.
  virtual bool shouldSignExtendTypeInLibCall(EVT Type, bool IsSigned) const {
    return IsSigned;
  }

  /// Returns true if arguments should be extended in lib calls.
  virtual bool shouldExtendTypeInLibCall(EVT Type) const {
    return true;
  }

  /// Returns how the given (atomic) load should be expanded by the
  /// IR-level AtomicExpand pass.
  virtual AtomicExpansionKind shouldExpandAtomicLoadInIR(LoadInst *LI) const {
    return AtomicExpansionKind::None;
  }

  /// Returns how the given (atomic) load should be cast by the IR-level
  /// AtomicExpand pass.
  virtual AtomicExpansionKind shouldCastAtomicLoadInIR(LoadInst *LI) const {
    if (LI->getType()->isFloatingPointTy())
      return AtomicExpansionKind::CastToInteger;
    return AtomicExpansionKind::None;
  }

  /// Returns how the given (atomic) store should be expanded by the IR-level
  /// AtomicExpand pass into. For instance AtomicExpansionKind::Expand will try
  /// to use an atomicrmw xchg.
  virtual AtomicExpansionKind shouldExpandAtomicStoreInIR(StoreInst *SI) const {
    return AtomicExpansionKind::None;
  }

  /// Returns how the given (atomic) store should be cast by the IR-level
  /// AtomicExpand pass into. For instance AtomicExpansionKind::CastToInteger
  /// will try to cast the operands to integer values.
  virtual AtomicExpansionKind shouldCastAtomicStoreInIR(StoreInst *SI) const {
    if (SI->getValueOperand()->getType()->isFloatingPointTy())
      return AtomicExpansionKind::CastToInteger;
    return AtomicExpansionKind::None;
  }

  /// Returns how the given atomic cmpxchg should be expanded by the IR-level
  /// AtomicExpand pass.
  virtual AtomicExpansionKind
  shouldExpandAtomicCmpXchgInIR(AtomicCmpXchgInst *AI) const {
    return AtomicExpansionKind::None;
  }

  /// Returns how the IR-level AtomicExpand pass should expand the given
  /// AtomicRMW, if at all. Default is to never expand.
  virtual AtomicExpansionKind shouldExpandAtomicRMWInIR(AtomicRMWInst *RMW) const {
    return RMW->isFloatingPointOperation() ?
      AtomicExpansionKind::CmpXChg : AtomicExpansionKind::None;
  }

  /// Returns how the given atomic atomicrmw should be cast by the IR-level
  /// AtomicExpand pass.
  virtual AtomicExpansionKind
  shouldCastAtomicRMWIInIR(AtomicRMWInst *RMWI) const {
    if (RMWI->getOperation() == AtomicRMWInst::Xchg &&
        (RMWI->getValOperand()->getType()->isFloatingPointTy() ||
         RMWI->getValOperand()->getType()->isPointerTy()))
      return AtomicExpansionKind::CastToInteger;

    return AtomicExpansionKind::None;
  }

  /// On some platforms, an AtomicRMW that never actually modifies the value
  /// (such as fetch_add of 0) can be turned into a fence followed by an
  /// atomic load. This may sound useless, but it makes it possible for the
  /// processor to keep the cacheline shared, dramatically improving
  /// performance. And such idempotent RMWs are useful for implementing some
  /// kinds of locks, see for example (justification + benchmarks):
  /// http://www.hpl.hp.com/techreports/2012/HPL-2012-68.pdf
  /// This method tries doing that transformation, returning the atomic load if
  /// it succeeds, and nullptr otherwise.
  /// If shouldExpandAtomicLoadInIR returns true on that load, it will undergo
  /// another round of expansion.
  virtual LoadInst *
  lowerIdempotentRMWIntoFencedLoad(AtomicRMWInst *RMWI) const {
    return nullptr;
  }

  /// Returns how the platform's atomic operations are extended (ZERO_EXTEND,
  /// SIGN_EXTEND, or ANY_EXTEND).
  virtual ISD::NodeType getExtendForAtomicOps() const {
    return ISD::ZERO_EXTEND;
  }

  /// Returns how the platform's atomic compare and swap expects its comparison
  /// value to be extended (ZERO_EXTEND, SIGN_EXTEND, or ANY_EXTEND). This is
  /// separate from getExtendForAtomicOps, which is concerned with the
  /// sign-extension of the instruction's output, whereas here we are concerned
  /// with the sign-extension of the input. For targets with compare-and-swap
  /// instructions (or sub-word comparisons in their LL/SC loop expansions),
  /// the input can be ANY_EXTEND, but the output will still have a specific
  /// extension.
  virtual ISD::NodeType getExtendForAtomicCmpSwapArg() const {
    return ISD::ANY_EXTEND;
  }

  /// @}

  /// Returns true if we should normalize
  /// select(N0&N1, X, Y) => select(N0, select(N1, X, Y), Y) and
  /// select(N0|N1, X, Y) => select(N0, select(N1, X, Y, Y)) if it is likely
  /// that it saves us from materializing N0 and N1 in an integer register.
  /// Targets that are able to perform and/or on flags should return false here.
  virtual bool shouldNormalizeToSelectSequence(LLVMContext &Context,
                                               EVT VT) const {
    // If a target has multiple condition registers, then it likely has logical
    // operations on those registers.
    if (hasMultipleConditionRegisters())
      return false;
    // Only do the transform if the value won't be split into multiple
    // registers.
    LegalizeTypeAction Action = getTypeAction(Context, VT);
    return Action != TypeExpandInteger && Action != TypeExpandFloat &&
      Action != TypeSplitVector;
  }

  virtual bool isProfitableToCombineMinNumMaxNum(EVT VT) const { return true; }

  /// Return true if a select of constants (select Cond, C1, C2) should be
  /// transformed into simple math ops with the condition value. For example:
  /// select Cond, C1, C1-1 --> add (zext Cond), C1-1
  virtual bool convertSelectOfConstantsToMath(EVT VT) const {
    return false;
  }

  /// Return true if it is profitable to transform an integer
  /// multiplication-by-constant into simpler operations like shifts and adds.
  /// This may be true if the target does not directly support the
  /// multiplication operation for the specified type or the sequence of simpler
  /// ops is faster than the multiply.
  virtual bool decomposeMulByConstant(LLVMContext &Context,
                                      EVT VT, SDValue C) const {
    return false;
  }

  /// Return true if it may be profitable to transform
  /// (mul (add x, c1), c2) -> (add (mul x, c2), c1*c2).
  /// This may not be true if c1 and c2 can be represented as immediates but
  /// c1*c2 cannot, for example.
  /// The target should check if c1, c2 and c1*c2 can be represented as
  /// immediates, or have to be materialized into registers. If it is not sure
  /// about some cases, a default true can be returned to let the DAGCombiner
  /// decide.
  /// AddNode is (add x, c1), and ConstNode is c2.
  virtual bool isMulAddWithConstProfitable(SDValue AddNode,
                                           SDValue ConstNode) const {
    return true;
  }

  /// Return true if it is more correct/profitable to use strict FP_TO_INT
  /// conversion operations - canonicalizing the FP source value instead of
  /// converting all cases and then selecting based on value.
  /// This may be true if the target throws exceptions for out of bounds
  /// conversions or has fast FP CMOV.
  virtual bool shouldUseStrictFP_TO_INT(EVT FpVT, EVT IntVT,
                                        bool IsSigned) const {
    return false;
  }

  /// Return true if it is beneficial to expand an @llvm.powi.* intrinsic.
  /// If not optimizing for size, expanding @llvm.powi.* intrinsics is always
  /// considered beneficial.
  /// If optimizing for size, expansion is only considered beneficial for upto
  /// 5 multiplies and a divide (if the exponent is negative).
  bool isBeneficialToExpandPowI(int64_t Exponent, bool OptForSize) const {
    if (Exponent < 0)
      Exponent = -Exponent;
    uint64_t E = static_cast<uint64_t>(Exponent);
    return !OptForSize || (llvm::popcount(E) + Log2_64(E) < 7);
  }

  //===--------------------------------------------------------------------===//
  // TargetLowering Configuration Methods - These methods should be invoked by
  // the derived class constructor to configure this object for the target.
  //
protected:
  /// Specify how the target extends the result of integer and floating point
  /// boolean values from i1 to a wider type.  See getBooleanContents.
  void setBooleanContents(BooleanContent Ty) {
    BooleanContents = Ty;
    BooleanFloatContents = Ty;
  }

  /// Specify how the target extends the result of integer and floating point
  /// boolean values from i1 to a wider type.  See getBooleanContents.
  void setBooleanContents(BooleanContent IntTy, BooleanContent FloatTy) {
    BooleanContents = IntTy;
    BooleanFloatContents = FloatTy;
  }

  /// Specify how the target extends the result of a vector boolean value from a
  /// vector of i1 to a wider type.  See getBooleanContents.
  void setBooleanVectorContents(BooleanContent Ty) {
    BooleanVectorContents = Ty;
  }

  /// Specify the target scheduling preference.
  void setSchedulingPreference(Sched::Preference Pref) {
    SchedPreferenceInfo = Pref;
  }

  /// Indicate the minimum number of blocks to generate jump tables.
  void setMinimumJumpTableEntries(unsigned Val);

  /// Indicate the maximum number of entries in jump tables.
  /// Set to zero to generate unlimited jump tables.
  void setMaximumJumpTableSize(unsigned);

  /// If set to a physical register, this specifies the register that
  /// llvm.savestack/llvm.restorestack should save and restore.
  void setStackPointerRegisterToSaveRestore(Register R) {
    StackPointerRegisterToSaveRestore = R;
  }

  /// Tells the code generator that the target has multiple (allocatable)
  /// condition registers that can be used to store the results of comparisons
  /// for use by selects and conditional branches. With multiple condition
  /// registers, the code generator will not aggressively sink comparisons into
  /// the blocks of their users.
  void setHasMultipleConditionRegisters(bool hasManyRegs = true) {
    HasMultipleConditionRegisters = hasManyRegs;
  }

  /// Tells the code generator that the target has BitExtract instructions.
  /// The code generator will aggressively sink "shift"s into the blocks of
  /// their users if the users will generate "and" instructions which can be
  /// combined with "shift" to BitExtract instructions.
  void setHasExtractBitsInsn(bool hasExtractInsn = true) {
    HasExtractBitsInsn = hasExtractInsn;
  }

  /// Tells the code generator not to expand logic operations on comparison
  /// predicates into separate sequences that increase the amount of flow
  /// control.
  void setJumpIsExpensive(bool isExpensive = true);

  /// Tells the code generator which bitwidths to bypass.
  void addBypassSlowDiv(unsigned int SlowBitWidth, unsigned int FastBitWidth) {
    BypassSlowDivWidths[SlowBitWidth] = FastBitWidth;
  }

  /// Add the specified register class as an available regclass for the
  /// specified value type. This indicates the selector can handle values of
  /// that class natively.
  void addRegisterClass(MVT VT, const TargetRegisterClass *RC) {
    assert((unsigned)VT.SimpleTy < std::size(RegClassForVT));
    RegClassForVT[VT.SimpleTy] = RC;
  }

  /// Return the largest legal super-reg register class of the register class
  /// for the specified type and its associated "cost".
  virtual std::pair<const TargetRegisterClass *, uint8_t>
  findRepresentativeClass(const TargetRegisterInfo *TRI, MVT VT) const;

  /// Once all of the register classes are added, this allows us to compute
  /// derived properties we expose.
  void computeRegisterProperties(const TargetRegisterInfo *TRI);

  /// Indicate that the specified operation does not work with the specified
  /// type and indicate what to do about it. Note that VT may refer to either
  /// the type of a result or that of an operand of Op.
  void setOperationAction(unsigned Op, MVT VT, LegalizeAction Action) {
    assert(Op < std::size(OpActions[0]) && "Table isn't big enough!");
    OpActions[(unsigned)VT.SimpleTy][Op] = Action;
  }
  void setOperationAction(ArrayRef<unsigned> Ops, MVT VT,
                          LegalizeAction Action) {
    for (auto Op : Ops)
      setOperationAction(Op, VT, Action);
  }
  void setOperationAction(ArrayRef<unsigned> Ops, ArrayRef<MVT> VTs,
                          LegalizeAction Action) {
    for (auto VT : VTs)
      setOperationAction(Ops, VT, Action);
  }

  /// Indicate that the specified load with extension does not work with the
  /// specified type and indicate what to do about it.
  void setLoadExtAction(unsigned ExtType, MVT ValVT, MVT MemVT,
                        LegalizeAction Action) {
    assert(ExtType < ISD::LAST_LOADEXT_TYPE && ValVT.isValid() &&
           MemVT.isValid() && "Table isn't big enough!");
    assert((unsigned)Action < 0x10 && "too many bits for bitfield array");
    unsigned Shift = 4 * ExtType;
    LoadExtActions[ValVT.SimpleTy][MemVT.SimpleTy] &= ~((uint16_t)0xF << Shift);
    LoadExtActions[ValVT.SimpleTy][MemVT.SimpleTy] |= (uint16_t)Action << Shift;
  }
  void setLoadExtAction(ArrayRef<unsigned> ExtTypes, MVT ValVT, MVT MemVT,
                        LegalizeAction Action) {
    for (auto ExtType : ExtTypes)
      setLoadExtAction(ExtType, ValVT, MemVT, Action);
  }
  void setLoadExtAction(ArrayRef<unsigned> ExtTypes, MVT ValVT,
                        ArrayRef<MVT> MemVTs, LegalizeAction Action) {
    for (auto MemVT : MemVTs)
      setLoadExtAction(ExtTypes, ValVT, MemVT, Action);
  }

  /// Let target indicate that an extending atomic load of the specified type
  /// is legal.
  void setAtomicLoadExtAction(unsigned ExtType, MVT ValVT, MVT MemVT,
                              LegalizeAction Action) {
    assert(ExtType < ISD::LAST_LOADEXT_TYPE && ValVT.isValid() &&
           MemVT.isValid() && "Table isn't big enough!");
    assert((unsigned)Action < 0x10 && "too many bits for bitfield array");
    unsigned Shift = 4 * ExtType;
    AtomicLoadExtActions[ValVT.SimpleTy][MemVT.SimpleTy] &=
        ~((uint16_t)0xF << Shift);
    AtomicLoadExtActions[ValVT.SimpleTy][MemVT.SimpleTy] |=
        ((uint16_t)Action << Shift);
  }
  void setAtomicLoadExtAction(ArrayRef<unsigned> ExtTypes, MVT ValVT, MVT MemVT,
                              LegalizeAction Action) {
    for (auto ExtType : ExtTypes)
      setAtomicLoadExtAction(ExtType, ValVT, MemVT, Action);
  }
  void setAtomicLoadExtAction(ArrayRef<unsigned> ExtTypes, MVT ValVT,
                              ArrayRef<MVT> MemVTs, LegalizeAction Action) {
    for (auto MemVT : MemVTs)
      setAtomicLoadExtAction(ExtTypes, ValVT, MemVT, Action);
  }

  /// Indicate that the specified truncating store does not work with the
  /// specified type and indicate what to do about it.
  void setTruncStoreAction(MVT ValVT, MVT MemVT, LegalizeAction Action) {
    assert(ValVT.isValid() && MemVT.isValid() && "Table isn't big enough!");
    TruncStoreActions[(unsigned)ValVT.SimpleTy][MemVT.SimpleTy] = Action;
  }

  /// Indicate that the specified indexed load does or does not work with the
  /// specified type and indicate what to do abort it.
  ///
  /// NOTE: All indexed mode loads are initialized to Expand in
  /// TargetLowering.cpp
  void setIndexedLoadAction(ArrayRef<unsigned> IdxModes, MVT VT,
                            LegalizeAction Action) {
    for (auto IdxMode : IdxModes)
      setIndexedModeAction(IdxMode, VT, IMAB_Load, Action);
  }

  void setIndexedLoadAction(ArrayRef<unsigned> IdxModes, ArrayRef<MVT> VTs,
                            LegalizeAction Action) {
    for (auto VT : VTs)
      setIndexedLoadAction(IdxModes, VT, Action);
  }

  /// Indicate that the specified indexed store does or does not work with the
  /// specified type and indicate what to do about it.
  ///
  /// NOTE: All indexed mode stores are initialized to Expand in
  /// TargetLowering.cpp
  void setIndexedStoreAction(ArrayRef<unsigned> IdxModes, MVT VT,
                             LegalizeAction Action) {
    for (auto IdxMode : IdxModes)
      setIndexedModeAction(IdxMode, VT, IMAB_Store, Action);
  }

  void setIndexedStoreAction(ArrayRef<unsigned> IdxModes, ArrayRef<MVT> VTs,
                             LegalizeAction Action) {
    for (auto VT : VTs)
      setIndexedStoreAction(IdxModes, VT, Action);
  }

  /// Indicate that the specified indexed masked load does or does not work with
  /// the specified type and indicate what to do about it.
  ///
  /// NOTE: All indexed mode masked loads are initialized to Expand in
  /// TargetLowering.cpp
  void setIndexedMaskedLoadAction(unsigned IdxMode, MVT VT,
                                  LegalizeAction Action) {
    setIndexedModeAction(IdxMode, VT, IMAB_MaskedLoad, Action);
  }

  /// Indicate that the specified indexed masked store does or does not work
  /// with the specified type and indicate what to do about it.
  ///
  /// NOTE: All indexed mode masked stores are initialized to Expand in
  /// TargetLowering.cpp
  void setIndexedMaskedStoreAction(unsigned IdxMode, MVT VT,
                                   LegalizeAction Action) {
    setIndexedModeAction(IdxMode, VT, IMAB_MaskedStore, Action);
  }

  /// Indicate that the specified condition code is or isn't supported on the
  /// target and indicate what to do about it.
  void setCondCodeAction(ArrayRef<ISD::CondCode> CCs, MVT VT,
                         LegalizeAction Action) {
    for (auto CC : CCs) {
      assert(VT.isValid() && (unsigned)CC < std::size(CondCodeActions) &&
             "Table isn't big enough!");
      assert((unsigned)Action < 0x10 && "too many bits for bitfield array");
      /// The lower 3 bits of the SimpleTy index into Nth 4bit set from the
      /// 32-bit value and the upper 29 bits index into the second dimension of
      /// the array to select what 32-bit value to use.
      uint32_t Shift = 4 * (VT.SimpleTy & 0x7);
      CondCodeActions[CC][VT.SimpleTy >> 3] &= ~((uint32_t)0xF << Shift);
      CondCodeActions[CC][VT.SimpleTy >> 3] |= (uint32_t)Action << Shift;
    }
  }
  void setCondCodeAction(ArrayRef<ISD::CondCode> CCs, ArrayRef<MVT> VTs,
                         LegalizeAction Action) {
    for (auto VT : VTs)
      setCondCodeAction(CCs, VT, Action);
  }

  /// If Opc/OrigVT is specified as being promoted, the promotion code defaults
  /// to trying a larger integer/fp until it can find one that works. If that
  /// default is insufficient, this method can be used by the target to override
  /// the default.
  void AddPromotedToType(unsigned Opc, MVT OrigVT, MVT DestVT) {
    PromoteToType[std::make_pair(Opc, OrigVT.SimpleTy)] = DestVT.SimpleTy;
  }

  /// Convenience method to set an operation to Promote and specify the type
  /// in a single call.
  void setOperationPromotedToType(unsigned Opc, MVT OrigVT, MVT DestVT) {
    setOperationAction(Opc, OrigVT, Promote);
    AddPromotedToType(Opc, OrigVT, DestVT);
  }
  void setOperationPromotedToType(ArrayRef<unsigned> Ops, MVT OrigVT,
                                  MVT DestVT) {
    for (auto Op : Ops) {
      setOperationAction(Op, OrigVT, Promote);
      AddPromotedToType(Op, OrigVT, DestVT);
    }
  }

  /// Targets should invoke this method for each target independent node that
  /// they want to provide a custom DAG combiner for by implementing the
  /// PerformDAGCombine virtual method.
  void setTargetDAGCombine(ArrayRef<ISD::NodeType> NTs) {
    for (auto NT : NTs) {
      assert(unsigned(NT >> 3) < std::size(TargetDAGCombineArray));
      TargetDAGCombineArray[NT >> 3] |= 1 << (NT & 7);
    }
  }

  /// Set the target's minimum function alignment.
  void setMinFunctionAlignment(Align Alignment) {
    MinFunctionAlignment = Alignment;
  }

  /// Set the target's preferred function alignment.  This should be set if
  /// there is a performance benefit to higher-than-minimum alignment
  void setPrefFunctionAlignment(Align Alignment) {
    PrefFunctionAlignment = Alignment;
  }

  /// Set the target's preferred loop alignment. Default alignment is one, it
  /// means the target does not care about loop alignment. The target may also
  /// override getPrefLoopAlignment to provide per-loop values.
  void setPrefLoopAlignment(Align Alignment) { PrefLoopAlignment = Alignment; }
  void setMaxBytesForAlignment(unsigned MaxBytes) {
    MaxBytesForAlignment = MaxBytes;
  }

  /// Set the minimum stack alignment of an argument.
  void setMinStackArgumentAlignment(Align Alignment) {
    MinStackArgumentAlignment = Alignment;
  }

  /// Set the maximum atomic operation size supported by the
  /// backend. Atomic operations greater than this size (as well as
  /// ones that are not naturally aligned), will be expanded by
  /// AtomicExpandPass into an __atomic_* library call.
  void setMaxAtomicSizeInBitsSupported(unsigned SizeInBits) {
    MaxAtomicSizeInBitsSupported = SizeInBits;
  }

  /// Set the size in bits of the maximum div/rem the backend supports.
  /// Larger operations will be expanded by ExpandLargeDivRem.
  void setMaxDivRemBitWidthSupported(unsigned SizeInBits) {
    MaxDivRemBitWidthSupported = SizeInBits;
  }

  /// Set the size in bits of the maximum fp convert the backend supports.
  /// Larger operations will be expanded by ExpandLargeFPConvert.
  void setMaxLargeFPConvertBitWidthSupported(unsigned SizeInBits) {
    MaxLargeFPConvertBitWidthSupported = SizeInBits;
  }

  /// Sets the minimum cmpxchg or ll/sc size supported by the backend.
  void setMinCmpXchgSizeInBits(unsigned SizeInBits) {
    MinCmpXchgSizeInBits = SizeInBits;
  }

  /// Sets whether unaligned atomic operations are supported.
  void setSupportsUnalignedAtomics(bool UnalignedSupported) {
    SupportsUnalignedAtomics = UnalignedSupported;
  }

public:
  //===--------------------------------------------------------------------===//
  // Addressing mode description hooks (used by LSR etc).
  //

  /// CodeGenPrepare sinks address calculations into the same BB as Load/Store
  /// instructions reading the address. This allows as much computation as
  /// possible to be done in the address mode for that operand. This hook lets
  /// targets also pass back when this should be done on intrinsics which
  /// load/store.
  virtual bool getAddrModeArguments(IntrinsicInst * /*I*/,
                                    SmallVectorImpl<Value*> &/*Ops*/,
                                    Type *&/*AccessTy*/) const {
    return false;
  }

  /// This represents an addressing mode of:
  ///    BaseGV + BaseOffs + BaseReg + Scale*ScaleReg + ScalableOffset*vscale
  /// If BaseGV is null,  there is no BaseGV.
  /// If BaseOffs is zero, there is no base offset.
  /// If HasBaseReg is false, there is no base register.
  /// If Scale is zero, there is no ScaleReg.  Scale of 1 indicates a reg with
  /// no scale.
  /// If ScalableOffset is zero, there is no scalable offset.
  struct AddrMode {
    GlobalValue *BaseGV = nullptr;
    int64_t      BaseOffs = 0;
    bool         HasBaseReg = false;
    int64_t      Scale = 0;
    int64_t ScalableOffset = 0;
    AddrMode() = default;
  };

  /// Return true if the addressing mode represented by AM is legal for this
  /// target, for a load/store of the specified type.
  ///
  /// The type may be VoidTy, in which case only return true if the addressing
  /// mode is legal for a load/store of any legal type.  TODO: Handle
  /// pre/postinc as well.
  ///
  /// If the address space cannot be determined, it will be -1.
  ///
  /// TODO: Remove default argument
  virtual bool isLegalAddressingMode(const DataLayout &DL, const AddrMode &AM,
                                     Type *Ty, unsigned AddrSpace,
                                     Instruction *I = nullptr) const;

  /// Returns true if the targets addressing mode can target thread local
  /// storage (TLS).
  virtual bool addressingModeSupportsTLS(const GlobalValue &) const {
    return false;
  }

  /// Return the prefered common base offset.
  virtual int64_t getPreferredLargeGEPBaseOffset(int64_t MinOffset,
                                                 int64_t MaxOffset) const {
    return 0;
  }

  /// Return true if the specified immediate is legal icmp immediate, that is
  /// the target has icmp instructions which can compare a register against the
  /// immediate without having to materialize the immediate into a register.
  virtual bool isLegalICmpImmediate(int64_t) const {
    return true;
  }

  /// Return true if the specified immediate is legal add immediate, that is the
  /// target has add instructions which can add a register with the immediate
  /// without having to materialize the immediate into a register.
  virtual bool isLegalAddImmediate(int64_t) const {
    return true;
  }

  /// Return true if adding the specified scalable immediate is legal, that is
  /// the target has add instructions which can add a register with the
  /// immediate (multiplied by vscale) without having to materialize the
  /// immediate into a register.
  virtual bool isLegalAddScalableImmediate(int64_t) const { return false; }

  /// Return true if the specified immediate is legal for the value input of a
  /// store instruction.
  virtual bool isLegalStoreImmediate(int64_t Value) const {
    // Default implementation assumes that at least 0 works since it is likely
    // that a zero register exists or a zero immediate is allowed.
    return Value == 0;
  }

  /// Return true if it's significantly cheaper to shift a vector by a uniform
  /// scalar than by an amount which will vary across each lane. On x86 before
  /// AVX2 for example, there is a "psllw" instruction for the former case, but
  /// no simple instruction for a general "a << b" operation on vectors.
  /// This should also apply to lowering for vector funnel shifts (rotates).
  virtual bool isVectorShiftByScalarCheap(Type *Ty) const {
    return false;
  }

  /// Given a shuffle vector SVI representing a vector splat, return a new
  /// scalar type of size equal to SVI's scalar type if the new type is more
  /// profitable. Returns nullptr otherwise. For example under MVE float splats
  /// are converted to integer to prevent the need to move from SPR to GPR
  /// registers.
  virtual Type* shouldConvertSplatType(ShuffleVectorInst* SVI) const {
    return nullptr;
  }

  /// Given a set in interconnected phis of type 'From' that are loaded/stored
  /// or bitcast to type 'To', return true if the set should be converted to
  /// 'To'.
  virtual bool shouldConvertPhiType(Type *From, Type *To) const {
    return (From->isIntegerTy() || From->isFloatingPointTy()) &&
           (To->isIntegerTy() || To->isFloatingPointTy());
  }

  /// Returns true if the opcode is a commutative binary operation.
  virtual bool isCommutativeBinOp(unsigned Opcode) const {
    // FIXME: This should get its info from the td file.
    switch (Opcode) {
    case ISD::ADD:
    case ISD::SMIN:
    case ISD::SMAX:
    case ISD::UMIN:
    case ISD::UMAX:
    case ISD::MUL:
    case ISD::MULHU:
    case ISD::MULHS:
    case ISD::SMUL_LOHI:
    case ISD::UMUL_LOHI:
    case ISD::FADD:
    case ISD::FMUL:
    case ISD::AND:
    case ISD::OR:
    case ISD::XOR:
    case ISD::SADDO:
    case ISD::UADDO:
    case ISD::ADDC:
    case ISD::ADDE:
    case ISD::SADDSAT:
    case ISD::UADDSAT:
    case ISD::FMINNUM:
    case ISD::FMAXNUM:
    case ISD::FMINNUM_IEEE:
    case ISD::FMAXNUM_IEEE:
    case ISD::FMINIMUM:
    case ISD::FMAXIMUM:
    case ISD::AVGFLOORS:
    case ISD::AVGFLOORU:
    case ISD::AVGCEILS:
    case ISD::AVGCEILU:
    case ISD::ABDS:
    case ISD::ABDU:
      return true;
    default: return false;
    }
  }

  /// Return true if the node is a math/logic binary operator.
  virtual bool isBinOp(unsigned Opcode) const {
    // A commutative binop must be a binop.
    if (isCommutativeBinOp(Opcode))
      return true;
    // These are non-commutative binops.
    switch (Opcode) {
    case ISD::SUB:
    case ISD::SHL:
    case ISD::SRL:
    case ISD::SRA:
    case ISD::ROTL:
    case ISD::ROTR:
    case ISD::SDIV:
    case ISD::UDIV:
    case ISD::SREM:
    case ISD::UREM:
    case ISD::SSUBSAT:
    case ISD::USUBSAT:
    case ISD::FSUB:
    case ISD::FDIV:
    case ISD::FREM:
      return true;
    default:
      return false;
    }
  }

  /// Return true if it's free to truncate a value of type FromTy to type
  /// ToTy. e.g. On x86 it's free to truncate a i32 value in register EAX to i16
  /// by referencing its sub-register AX.
  /// Targets must return false when FromTy <= ToTy.
  virtual bool isTruncateFree(Type *FromTy, Type *ToTy) const {
    return false;
  }

  /// Return true if a truncation from FromTy to ToTy is permitted when deciding
  /// whether a call is in tail position. Typically this means that both results
  /// would be assigned to the same register or stack slot, but it could mean
  /// the target performs adequate checks of its own before proceeding with the
  /// tail call.  Targets must return false when FromTy <= ToTy.
  virtual bool allowTruncateForTailCall(Type *FromTy, Type *ToTy) const {
    return false;
  }

  virtual bool isTruncateFree(EVT FromVT, EVT ToVT) const { return false; }
  virtual bool isTruncateFree(LLT FromTy, LLT ToTy, const DataLayout &DL,
                              LLVMContext &Ctx) const {
    return isTruncateFree(getApproximateEVTForLLT(FromTy, DL, Ctx),
                          getApproximateEVTForLLT(ToTy, DL, Ctx));
  }

  /// Return true if truncating the specific node Val to type VT2 is free.
  virtual bool isTruncateFree(SDValue Val, EVT VT2) const {
    // Fallback to type matching.
    return isTruncateFree(Val.getValueType(), VT2);
  }

  virtual bool isProfitableToHoist(Instruction *I) const { return true; }

  /// Return true if the extension represented by \p I is free.
  /// Unlikely the is[Z|FP]ExtFree family which is based on types,
  /// this method can use the context provided by \p I to decide
  /// whether or not \p I is free.
  /// This method extends the behavior of the is[Z|FP]ExtFree family.
  /// In other words, if is[Z|FP]Free returns true, then this method
  /// returns true as well. The converse is not true.
  /// The target can perform the adequate checks by overriding isExtFreeImpl.
  /// \pre \p I must be a sign, zero, or fp extension.
  bool isExtFree(const Instruction *I) const {
    switch (I->getOpcode()) {
    case Instruction::FPExt:
      if (isFPExtFree(EVT::getEVT(I->getType()),
                      EVT::getEVT(I->getOperand(0)->getType())))
        return true;
      break;
    case Instruction::ZExt:
      if (isZExtFree(I->getOperand(0)->getType(), I->getType()))
        return true;
      break;
    case Instruction::SExt:
      break;
    default:
      llvm_unreachable("Instruction is not an extension");
    }
    return isExtFreeImpl(I);
  }

  /// Return true if \p Load and \p Ext can form an ExtLoad.
  /// For example, in AArch64
  ///   %L = load i8, i8* %ptr
  ///   %E = zext i8 %L to i32
  /// can be lowered into one load instruction
  ///   ldrb w0, [x0]
  bool isExtLoad(const LoadInst *Load, const Instruction *Ext,
                 const DataLayout &DL) const {
    EVT VT = getValueType(DL, Ext->getType());
    EVT LoadVT = getValueType(DL, Load->getType());

    // If the load has other users and the truncate is not free, the ext
    // probably isn't free.
    if (!Load->hasOneUse() && (isTypeLegal(LoadVT) || !isTypeLegal(VT)) &&
        !isTruncateFree(Ext->getType(), Load->getType()))
      return false;

    // Check whether the target supports casts folded into loads.
    unsigned LType;
    if (isa<ZExtInst>(Ext))
      LType = ISD::ZEXTLOAD;
    else {
      assert(isa<SExtInst>(Ext) && "Unexpected ext type!");
      LType = ISD::SEXTLOAD;
    }

    return isLoadExtLegal(LType, VT, LoadVT);
  }

  /// Return true if any actual instruction that defines a value of type FromTy
  /// implicitly zero-extends the value to ToTy in the result register.
  ///
  /// The function should return true when it is likely that the truncate can
  /// be freely folded with an instruction defining a value of FromTy. If
  /// the defining instruction is unknown (because you're looking at a
  /// function argument, PHI, etc.) then the target may require an
  /// explicit truncate, which is not necessarily free, but this function
  /// does not deal with those cases.
  /// Targets must return false when FromTy >= ToTy.
  virtual bool isZExtFree(Type *FromTy, Type *ToTy) const {
    return false;
  }

  virtual bool isZExtFree(EVT FromTy, EVT ToTy) const { return false; }
  virtual bool isZExtFree(LLT FromTy, LLT ToTy, const DataLayout &DL,
                          LLVMContext &Ctx) const {
    return isZExtFree(getApproximateEVTForLLT(FromTy, DL, Ctx),
                      getApproximateEVTForLLT(ToTy, DL, Ctx));
  }

  /// Return true if zero-extending the specific node Val to type VT2 is free
  /// (either because it's implicitly zero-extended such as ARM ldrb / ldrh or
  /// because it's folded such as X86 zero-extending loads).
  virtual bool isZExtFree(SDValue Val, EVT VT2) const {
    return isZExtFree(Val.getValueType(), VT2);
  }

  /// Return true if sign-extension from FromTy to ToTy is cheaper than
  /// zero-extension.
  virtual bool isSExtCheaperThanZExt(EVT FromTy, EVT ToTy) const {
    return false;
  }

  /// Return true if this constant should be sign extended when promoting to
  /// a larger type.
  virtual bool signExtendConstant(const ConstantInt *C) const { return false; }

  /// Return true if sinking I's operands to the same basic block as I is
  /// profitable, e.g. because the operands can be folded into a target
  /// instruction during instruction selection. After calling the function
  /// \p Ops contains the Uses to sink ordered by dominance (dominating users
  /// come first).
  virtual bool shouldSinkOperands(Instruction *I,
                                  SmallVectorImpl<Use *> &Ops) const {
    return false;
  }

  /// Try to optimize extending or truncating conversion instructions (like
  /// zext, trunc, fptoui, uitofp) for the target.
  virtual bool
  optimizeExtendOrTruncateConversion(Instruction *I, Loop *L,
                                     const TargetTransformInfo &TTI) const {
    return false;
  }

  /// Return true if the target supplies and combines to a paired load
  /// two loaded values of type LoadedType next to each other in memory.
  /// RequiredAlignment gives the minimal alignment constraints that must be met
  /// to be able to select this paired load.
  ///
  /// This information is *not* used to generate actual paired loads, but it is
  /// used to generate a sequence of loads that is easier to combine into a
  /// paired load.
  /// For instance, something like this:
  /// a = load i64* addr
  /// b = trunc i64 a to i32
  /// c = lshr i64 a, 32
  /// d = trunc i64 c to i32
  /// will be optimized into:
  /// b = load i32* addr1
  /// d = load i32* addr2
  /// Where addr1 = addr2 +/- sizeof(i32).
  ///
  /// In other words, unless the target performs a post-isel load combining,
  /// this information should not be provided because it will generate more
  /// loads.
  virtual bool hasPairedLoad(EVT /*LoadedType*/,
                             Align & /*RequiredAlignment*/) const {
    return false;
  }

  /// Return true if the target has a vector blend instruction.
  virtual bool hasVectorBlend() const { return false; }

  /// Get the maximum supported factor for interleaved memory accesses.
  /// Default to be the minimum interleave factor: 2.
  virtual unsigned getMaxSupportedInterleaveFactor() const { return 2; }

  /// Lower an interleaved load to target specific intrinsics. Return
  /// true on success.
  ///
  /// \p LI is the vector load instruction.
  /// \p Shuffles is the shufflevector list to DE-interleave the loaded vector.
  /// \p Indices is the corresponding indices for each shufflevector.
  /// \p Factor is the interleave factor.
  virtual bool lowerInterleavedLoad(LoadInst *LI,
                                    ArrayRef<ShuffleVectorInst *> Shuffles,
                                    ArrayRef<unsigned> Indices,
                                    unsigned Factor) const {
    return false;
  }

  /// Lower an interleaved store to target specific intrinsics. Return
  /// true on success.
  ///
  /// \p SI is the vector store instruction.
  /// \p SVI is the shufflevector to RE-interleave the stored vector.
  /// \p Factor is the interleave factor.
  virtual bool lowerInterleavedStore(StoreInst *SI, ShuffleVectorInst *SVI,
                                     unsigned Factor) const {
    return false;
  }

  /// Lower a deinterleave intrinsic to a target specific load intrinsic.
  /// Return true on success. Currently only supports
  /// llvm.vector.deinterleave2
  ///
  /// \p DI is the deinterleave intrinsic.
  /// \p LI is the accompanying load instruction
  virtual bool lowerDeinterleaveIntrinsicToLoad(IntrinsicInst *DI,
                                                LoadInst *LI) const {
    return false;
  }

  /// Lower an interleave intrinsic to a target specific store intrinsic.
  /// Return true on success. Currently only supports
  /// llvm.vector.interleave2
  ///
  /// \p II is the interleave intrinsic.
  /// \p SI is the accompanying store instruction
  virtual bool lowerInterleaveIntrinsicToStore(IntrinsicInst *II,
                                               StoreInst *SI) const {
    return false;
  }

  /// Return true if an fpext operation is free (for instance, because
  /// single-precision floating-point numbers are implicitly extended to
  /// double-precision).
  virtual bool isFPExtFree(EVT DestVT, EVT SrcVT) const {
    assert(SrcVT.isFloatingPoint() && DestVT.isFloatingPoint() &&
           "invalid fpext types");
    return false;
  }

  /// Return true if an fpext operation input to an \p Opcode operation is free
  /// (for instance, because half-precision floating-point numbers are
  /// implicitly extended to float-precision) for an FMA instruction.
  virtual bool isFPExtFoldable(const MachineInstr &MI, unsigned Opcode,
                               LLT DestTy, LLT SrcTy) const {
    return false;
  }

  /// Return true if an fpext operation input to an \p Opcode operation is free
  /// (for instance, because half-precision floating-point numbers are
  /// implicitly extended to float-precision) for an FMA instruction.
  virtual bool isFPExtFoldable(const SelectionDAG &DAG, unsigned Opcode,
                               EVT DestVT, EVT SrcVT) const {
    assert(DestVT.isFloatingPoint() && SrcVT.isFloatingPoint() &&
           "invalid fpext types");
    return isFPExtFree(DestVT, SrcVT);
  }

  /// Return true if folding a vector load into ExtVal (a sign, zero, or any
  /// extend node) is profitable.
  virtual bool isVectorLoadExtDesirable(SDValue ExtVal) const { return false; }

  /// Return true if an fneg operation is free to the point where it is never
  /// worthwhile to replace it with a bitwise operation.
  virtual bool isFNegFree(EVT VT) const {
    assert(VT.isFloatingPoint());
    return false;
  }

  /// Return true if an fabs operation is free to the point where it is never
  /// worthwhile to replace it with a bitwise operation.
  virtual bool isFAbsFree(EVT VT) const {
    assert(VT.isFloatingPoint());
    return false;
  }

  /// Return true if an FMA operation is faster than a pair of fmul and fadd
  /// instructions. fmuladd intrinsics will be expanded to FMAs when this method
  /// returns true, otherwise fmuladd is expanded to fmul + fadd.
  ///
  /// NOTE: This may be called before legalization on types for which FMAs are
  /// not legal, but should return true if those types will eventually legalize
  /// to types that support FMAs. After legalization, it will only be called on
  /// types that support FMAs (via Legal or Custom actions)
  virtual bool isFMAFasterThanFMulAndFAdd(const MachineFunction &MF,
                                          EVT) const {
    return false;
  }

  /// Return true if an FMA operation is faster than a pair of fmul and fadd
  /// instructions. fmuladd intrinsics will be expanded to FMAs when this method
  /// returns true, otherwise fmuladd is expanded to fmul + fadd.
  ///
  /// NOTE: This may be called before legalization on types for which FMAs are
  /// not legal, but should return true if those types will eventually legalize
  /// to types that support FMAs. After legalization, it will only be called on
  /// types that support FMAs (via Legal or Custom actions)
  virtual bool isFMAFasterThanFMulAndFAdd(const MachineFunction &MF,
                                          LLT) const {
    return false;
  }

  /// IR version
  virtual bool isFMAFasterThanFMulAndFAdd(const Function &F, Type *) const {
    return false;
  }

  /// Returns true if \p MI can be combined with another instruction to
  /// form TargetOpcode::G_FMAD. \p N may be an TargetOpcode::G_FADD,
  /// TargetOpcode::G_FSUB, or an TargetOpcode::G_FMUL which will be
  /// distributed into an fadd/fsub.
  virtual bool isFMADLegal(const MachineInstr &MI, LLT Ty) const {
    assert((MI.getOpcode() == TargetOpcode::G_FADD ||
            MI.getOpcode() == TargetOpcode::G_FSUB ||
            MI.getOpcode() == TargetOpcode::G_FMUL) &&
           "unexpected node in FMAD forming combine");
    switch (Ty.getScalarSizeInBits()) {
    case 16:
      return isOperationLegal(TargetOpcode::G_FMAD, MVT::f16);
    case 32:
      return isOperationLegal(TargetOpcode::G_FMAD, MVT::f32);
    case 64:
      return isOperationLegal(TargetOpcode::G_FMAD, MVT::f64);
    default:
      break;
    }

    return false;
  }

  /// Returns true if be combined with to form an ISD::FMAD. \p N may be an
  /// ISD::FADD, ISD::FSUB, or an ISD::FMUL which will be distributed into an
  /// fadd/fsub.
  virtual bool isFMADLegal(const SelectionDAG &DAG, const SDNode *N) const {
    assert((N->getOpcode() == ISD::FADD || N->getOpcode() == ISD::FSUB ||
            N->getOpcode() == ISD::FMUL) &&
           "unexpected node in FMAD forming combine");
    return isOperationLegal(ISD::FMAD, N->getValueType(0));
  }

  // Return true when the decision to generate FMA's (or FMS, FMLA etc) rather
  // than FMUL and ADD is delegated to the machine combiner.
  virtual bool generateFMAsInMachineCombiner(EVT VT,
                                             CodeGenOptLevel OptLevel) const {
    return false;
  }

  /// Return true if it's profitable to narrow operations of type SrcVT to
  /// DestVT. e.g. on x86, it's profitable to narrow from i32 to i8 but not from
  /// i32 to i16.
  virtual bool isNarrowingProfitable(EVT SrcVT, EVT DestVT) const {
    return false;
  }

  /// Return true if pulling a binary operation into a select with an identity
  /// constant is profitable. This is the inverse of an IR transform.
  /// Example: X + (Cond ? Y : 0) --> Cond ? (X + Y) : X
  virtual bool shouldFoldSelectWithIdentityConstant(unsigned BinOpcode,
                                                    EVT VT) const {
    return false;
  }

  /// Return true if it is beneficial to convert a load of a constant to
  /// just the constant itself.
  /// On some targets it might be more efficient to use a combination of
  /// arithmetic instructions to materialize the constant instead of loading it
  /// from a constant pool.
  virtual bool shouldConvertConstantLoadToIntImm(const APInt &Imm,
                                                 Type *Ty) const {
    return false;
  }

  /// Return true if EXTRACT_SUBVECTOR is cheap for extracting this result type
  /// from this source type with this index. This is needed because
  /// EXTRACT_SUBVECTOR usually has custom lowering that depends on the index of
  /// the first element, and only the target knows which lowering is cheap.
  virtual bool isExtractSubvectorCheap(EVT ResVT, EVT SrcVT,
                                       unsigned Index) const {
    return false;
  }

  /// Try to convert an extract element of a vector binary operation into an
  /// extract element followed by a scalar operation.
  virtual bool shouldScalarizeBinop(SDValue VecOp) const {
    return false;
  }

  /// Return true if extraction of a scalar element from the given vector type
  /// at the given index is cheap. For example, if scalar operations occur on
  /// the same register file as vector operations, then an extract element may
  /// be a sub-register rename rather than an actual instruction.
  virtual bool isExtractVecEltCheap(EVT VT, unsigned Index) const {
    return false;
  }

  /// Try to convert math with an overflow comparison into the corresponding DAG
  /// node operation. Targets may want to override this independently of whether
  /// the operation is legal/custom for the given type because it may obscure
  /// matching of other patterns.
  virtual bool shouldFormOverflowOp(unsigned Opcode, EVT VT,
                                    bool MathUsed) const {
    // TODO: The default logic is inherited from code in CodeGenPrepare.
    // The opcode should not make a difference by default?
    if (Opcode != ISD::UADDO)
      return false;

    // Allow the transform as long as we have an integer type that is not
    // obviously illegal and unsupported and if the math result is used
    // besides the overflow check. On some targets (e.g. SPARC), it is
    // not profitable to form on overflow op if the math result has no
    // concrete users.
    if (VT.isVector())
      return false;
    return MathUsed && (VT.isSimple() || !isOperationExpand(Opcode, VT));
  }

  // Return true if it is profitable to use a scalar input to a BUILD_VECTOR
  // even if the vector itself has multiple uses.
  virtual bool aggressivelyPreferBuildVectorSources(EVT VecVT) const {
    return false;
  }

  // Return true if CodeGenPrepare should consider splitting large offset of a
  // GEP to make the GEP fit into the addressing mode and can be sunk into the
  // same blocks of its users.
  virtual bool shouldConsiderGEPOffsetSplit() const { return false; }

  /// Return true if creating a shift of the type by the given
  /// amount is not profitable.
  virtual bool shouldAvoidTransformToShift(EVT VT, unsigned Amount) const {
    return false;
  }

  // Should we fold (select_cc seteq (and x, y), 0, 0, A) -> (and (sra (shl x))
  // A) where y has a single bit set?
  virtual bool shouldFoldSelectWithSingleBitTest(EVT VT,
                                                 const APInt &AndMask) const {
    unsigned ShCt = AndMask.getBitWidth() - 1;
    return !shouldAvoidTransformToShift(VT, ShCt);
  }

  /// Does this target require the clearing of high-order bits in a register
  /// passed to the fp16 to fp conversion library function.
  virtual bool shouldKeepZExtForFP16Conv() const { return false; }

  /// Should we generate fp_to_si_sat and fp_to_ui_sat from type FPVT to type VT
  /// from min(max(fptoi)) saturation patterns.
  virtual bool shouldConvertFpToSat(unsigned Op, EVT FPVT, EVT VT) const {
    return isOperationLegalOrCustom(Op, VT);
  }

  /// Should we expand [US]CMP nodes using two selects and two compares, or by
  /// doing arithmetic on boolean types
  virtual bool shouldExpandCmpUsingSelects() const { return false; }

  /// Does this target support complex deinterleaving
  virtual bool isComplexDeinterleavingSupported() const { return false; }

  /// Does this target support complex deinterleaving with the given operation
  /// and type
  virtual bool isComplexDeinterleavingOperationSupported(
      ComplexDeinterleavingOperation Operation, Type *Ty) const {
    return false;
  }

  /// Create the IR node for the given complex deinterleaving operation.
  /// If one cannot be created using all the given inputs, nullptr should be
  /// returned.
  virtual Value *createComplexDeinterleavingIR(
      IRBuilderBase &B, ComplexDeinterleavingOperation OperationType,
      ComplexDeinterleavingRotation Rotation, Value *InputA, Value *InputB,
      Value *Accumulator = nullptr) const {
    return nullptr;
  }

  /// Rename the default libcall routine name for the specified libcall.
  void setLibcallName(RTLIB::Libcall Call, const char *Name) {
    Libcalls.setLibcallName(Call, Name);
  }

  void setLibcallName(ArrayRef<RTLIB::Libcall> Calls, const char *Name) {
    Libcalls.setLibcallName(Calls, Name);
  }

  /// Get the libcall routine name for the specified libcall.
  const char *getLibcallName(RTLIB::Libcall Call) const {
    return Libcalls.getLibcallName(Call);
  }

  /// Override the default CondCode to be used to test the result of the
  /// comparison libcall against zero.
  /// FIXME: This can't be merged with 'RuntimeLibcallsInfo' because of the ISD.
  void setCmpLibcallCC(RTLIB::Libcall Call, ISD::CondCode CC) {
    CmpLibcallCCs[Call] = CC;
  }


  /// Get the CondCode that's to be used to test the result of the comparison
  /// libcall against zero.
  /// FIXME: This can't be merged with 'RuntimeLibcallsInfo' because of the ISD.
  ISD::CondCode getCmpLibcallCC(RTLIB::Libcall Call) const {
    return CmpLibcallCCs[Call];
  }


  /// Set the CallingConv that should be used for the specified libcall.
  void setLibcallCallingConv(RTLIB::Libcall Call, CallingConv::ID CC) {
    Libcalls.setLibcallCallingConv(Call, CC);
  }

  /// Get the CallingConv that should be used for the specified libcall.
  CallingConv::ID getLibcallCallingConv(RTLIB::Libcall Call) const {
    return Libcalls.getLibcallCallingConv(Call);
  }

  /// Execute target specific actions to finalize target lowering.
  /// This is used to set extra flags in MachineFrameInformation and freezing
  /// the set of reserved registers.
  /// The default implementation just freezes the set of reserved registers.
  virtual void finalizeLowering(MachineFunction &MF) const;

  //===----------------------------------------------------------------------===//
  //  GlobalISel Hooks
  //===----------------------------------------------------------------------===//
  /// Check whether or not \p MI needs to be moved close to its uses.
  virtual bool shouldLocalize(const MachineInstr &MI, const TargetTransformInfo *TTI) const;


private:
  const TargetMachine &TM;

  /// Tells the code generator that the target has multiple (allocatable)
  /// condition registers that can be used to store the results of comparisons
  /// for use by selects and conditional branches. With multiple condition
  /// registers, the code generator will not aggressively sink comparisons into
  /// the blocks of their users.
  bool HasMultipleConditionRegisters;

  /// Tells the code generator that the target has BitExtract instructions.
  /// The code generator will aggressively sink "shift"s into the blocks of
  /// their users if the users will generate "and" instructions which can be
  /// combined with "shift" to BitExtract instructions.
  bool HasExtractBitsInsn;

  /// Tells the code generator to bypass slow divide or remainder
  /// instructions. For example, BypassSlowDivWidths[32,8] tells the code
  /// generator to bypass 32-bit integer div/rem with an 8-bit unsigned integer
  /// div/rem when the operands are positive and less than 256.
  DenseMap <unsigned int, unsigned int> BypassSlowDivWidths;

  /// Tells the code generator that it shouldn't generate extra flow control
  /// instructions and should attempt to combine flow control instructions via
  /// predication.
  bool JumpIsExpensive;

  /// Information about the contents of the high-bits in boolean values held in
  /// a type wider than i1. See getBooleanContents.
  BooleanContent BooleanContents;

  /// Information about the contents of the high-bits in boolean values held in
  /// a type wider than i1. See getBooleanContents.
  BooleanContent BooleanFloatContents;

  /// Information about the contents of the high-bits in boolean vector values
  /// when the element type is wider than i1. See getBooleanContents.
  BooleanContent BooleanVectorContents;

  /// The target scheduling preference: shortest possible total cycles or lowest
  /// register usage.
  Sched::Preference SchedPreferenceInfo;

  /// The minimum alignment that any argument on the stack needs to have.
  Align MinStackArgumentAlignment;

  /// The minimum function alignment (used when optimizing for size, and to
  /// prevent explicitly provided alignment from leading to incorrect code).
  Align MinFunctionAlignment;

  /// The preferred function alignment (used when alignment unspecified and
  /// optimizing for speed).
  Align PrefFunctionAlignment;

  /// The preferred loop alignment (in log2 bot in bytes).
  Align PrefLoopAlignment;
  /// The maximum amount of bytes permitted to be emitted for alignment.
  unsigned MaxBytesForAlignment;

  /// Size in bits of the maximum atomics size the backend supports.
  /// Accesses larger than this will be expanded by AtomicExpandPass.
  unsigned MaxAtomicSizeInBitsSupported;

  /// Size in bits of the maximum div/rem size the backend supports.
  /// Larger operations will be expanded by ExpandLargeDivRem.
  unsigned MaxDivRemBitWidthSupported;

  /// Size in bits of the maximum larget fp convert size the backend
  /// supports. Larger operations will be expanded by ExpandLargeFPConvert.
  unsigned MaxLargeFPConvertBitWidthSupported;

  /// Size in bits of the minimum cmpxchg or ll/sc operation the
  /// backend supports.
  unsigned MinCmpXchgSizeInBits;

  /// This indicates if the target supports unaligned atomic operations.
  bool SupportsUnalignedAtomics;

  /// If set to a physical register, this specifies the register that
  /// llvm.savestack/llvm.restorestack should save and restore.
  Register StackPointerRegisterToSaveRestore;

  /// This indicates the default register class to use for each ValueType the
  /// target supports natively.
  const TargetRegisterClass *RegClassForVT[MVT::VALUETYPE_SIZE];
  uint16_t NumRegistersForVT[MVT::VALUETYPE_SIZE];
  MVT RegisterTypeForVT[MVT::VALUETYPE_SIZE];

  /// This indicates the "representative" register class to use for each
  /// ValueType the target supports natively. This information is used by the
  /// scheduler to track register pressure. By default, the representative
  /// register class is the largest legal super-reg register class of the
  /// register class of the specified type. e.g. On x86, i8, i16, and i32's
  /// representative class would be GR32.
  const TargetRegisterClass *RepRegClassForVT[MVT::VALUETYPE_SIZE] = {0};

  /// This indicates the "cost" of the "representative" register class for each
  /// ValueType. The cost is used by the scheduler to approximate register
  /// pressure.
  uint8_t RepRegClassCostForVT[MVT::VALUETYPE_SIZE];

  /// For any value types we are promoting or expanding, this contains the value
  /// type that we are changing to.  For Expanded types, this contains one step
  /// of the expand (e.g. i64 -> i32), even if there are multiple steps required
  /// (e.g. i64 -> i16).  For types natively supported by the system, this holds
  /// the same type (e.g. i32 -> i32).
  MVT TransformToType[MVT::VALUETYPE_SIZE];

  /// For each operation and each value type, keep a LegalizeAction that
  /// indicates how instruction selection should deal with the operation.  Most
  /// operations are Legal (aka, supported natively by the target), but
  /// operations that are not should be described.  Note that operations on
  /// non-legal value types are not described here.
  LegalizeAction OpActions[MVT::VALUETYPE_SIZE][ISD::BUILTIN_OP_END];

  /// For each load extension type and each value type, keep a LegalizeAction
  /// that indicates how instruction selection should deal with a load of a
  /// specific value type and extension type. Uses 4-bits to store the action
  /// for each of the 4 load ext types.
  uint16_t LoadExtActions[MVT::VALUETYPE_SIZE][MVT::VALUETYPE_SIZE];

  /// Similar to LoadExtActions, but for atomic loads. Only Legal or Expand
  /// (default) values are supported.
  uint16_t AtomicLoadExtActions[MVT::VALUETYPE_SIZE][MVT::VALUETYPE_SIZE];

  /// For each value type pair keep a LegalizeAction that indicates whether a
  /// truncating store of a specific value type and truncating type is legal.
  LegalizeAction TruncStoreActions[MVT::VALUETYPE_SIZE][MVT::VALUETYPE_SIZE];

  /// For each indexed mode and each value type, keep a quad of LegalizeAction
  /// that indicates how instruction selection should deal with the load /
  /// store / maskedload / maskedstore.
  ///
  /// The first dimension is the value_type for the reference. The second
  /// dimension represents the various modes for load store.
  uint16_t IndexedModeActions[MVT::VALUETYPE_SIZE][ISD::LAST_INDEXED_MODE];

  /// For each condition code (ISD::CondCode) keep a LegalizeAction that
  /// indicates how instruction selection should deal with the condition code.
  ///
  /// Because each CC action takes up 4 bits, we need to have the array size be
  /// large enough to fit all of the value types. This can be done by rounding
  /// up the MVT::VALUETYPE_SIZE value to the next multiple of 8.
  uint32_t CondCodeActions[ISD::SETCC_INVALID][(MVT::VALUETYPE_SIZE + 7) / 8];

  ValueTypeActionImpl ValueTypeActions;

private:
  /// Targets can specify ISD nodes that they would like PerformDAGCombine
  /// callbacks for by calling setTargetDAGCombine(), which sets a bit in this
  /// array.
  unsigned char
  TargetDAGCombineArray[(ISD::BUILTIN_OP_END+CHAR_BIT-1)/CHAR_BIT];

  /// For operations that must be promoted to a specific type, this holds the
  /// destination type.  This map should be sparse, so don't hold it as an
  /// array.
  ///
  /// Targets add entries to this map with AddPromotedToType(..), clients access
  /// this with getTypeToPromoteTo(..).
  std::map<std::pair<unsigned, MVT::SimpleValueType>, MVT::SimpleValueType>
    PromoteToType;

  /// The list of libcalls that the target will use.
  RTLIB::RuntimeLibcallsInfo Libcalls;

  /// The ISD::CondCode that should be used to test the result of each of the
  /// comparison libcall against zero.
  ISD::CondCode CmpLibcallCCs[RTLIB::UNKNOWN_LIBCALL];

  /// The bits of IndexedModeActions used to store the legalisation actions
  /// We store the data as   | ML | MS |  L |  S | each taking 4 bits.
  enum IndexedModeActionsBits {
    IMAB_Store = 0,
    IMAB_Load = 4,
    IMAB_MaskedStore = 8,
    IMAB_MaskedLoad = 12
  };

  void setIndexedModeAction(unsigned IdxMode, MVT VT, unsigned Shift,
                            LegalizeAction Action) {
    assert(VT.isValid() && IdxMode < ISD::LAST_INDEXED_MODE &&
           (unsigned)Action < 0xf && "Table isn't big enough!");
    unsigned Ty = (unsigned)VT.SimpleTy;
    IndexedModeActions[Ty][IdxMode] &= ~(0xf << Shift);
    IndexedModeActions[Ty][IdxMode] |= ((uint16_t)Action) << Shift;
  }

  LegalizeAction getIndexedModeAction(unsigned IdxMode, MVT VT,
                                      unsigned Shift) const {
    assert(IdxMode < ISD::LAST_INDEXED_MODE && VT.isValid() &&
           "Table isn't big enough!");
    unsigned Ty = (unsigned)VT.SimpleTy;
    return (LegalizeAction)((IndexedModeActions[Ty][IdxMode] >> Shift) & 0xf);
  }

protected:
  /// Return true if the extension represented by \p I is free.
  /// \pre \p I is a sign, zero, or fp extension and
  ///      is[Z|FP]ExtFree of the related types is not true.
  virtual bool isExtFreeImpl(const Instruction *I) const { return false; }

  /// Depth that GatherAllAliases should continue looking for chain
  /// dependencies when trying to find a more preferable chain. As an
  /// approximation, this should be more than the number of consecutive stores
  /// expected to be merged.
  unsigned GatherAllAliasesMaxDepth;

  /// \brief Specify maximum number of store instructions per memset call.
  ///
  /// When lowering \@llvm.memset this field specifies the maximum number of
  /// store operations that may be substituted for the call to memset. Targets
  /// must set this value based on the cost threshold for that target. Targets
  /// should assume that the memset will be done using as many of the largest
  /// store operations first, followed by smaller ones, if necessary, per
  /// alignment restrictions. For example, storing 9 bytes on a 32-bit machine
  /// with 16-bit alignment would result in four 2-byte stores and one 1-byte
  /// store.  This only applies to setting a constant array of a constant size.
  unsigned MaxStoresPerMemset;
  /// Likewise for functions with the OptSize attribute.
  unsigned MaxStoresPerMemsetOptSize;

  /// \brief Specify maximum number of store instructions per memcpy call.
  ///
  /// When lowering \@llvm.memcpy this field specifies the maximum number of
  /// store operations that may be substituted for a call to memcpy. Targets
  /// must set this value based on the cost threshold for that target. Targets
  /// should assume that the memcpy will be done using as many of the largest
  /// store operations first, followed by smaller ones, if necessary, per
  /// alignment restrictions. For example, storing 7 bytes on a 32-bit machine
  /// with 32-bit alignment would result in one 4-byte store, a one 2-byte store
  /// and one 1-byte store. This only applies to copying a constant array of
  /// constant size.
  unsigned MaxStoresPerMemcpy;
  /// Likewise for functions with the OptSize attribute.
  unsigned MaxStoresPerMemcpyOptSize;
  /// \brief Specify max number of store instructions to glue in inlined memcpy.
  ///
  /// When memcpy is inlined based on MaxStoresPerMemcpy, specify maximum number
  /// of store instructions to keep together. This helps in pairing and
  //  vectorization later on.
  unsigned MaxGluedStoresPerMemcpy = 0;

  /// \brief Specify maximum number of load instructions per memcmp call.
  ///
  /// When lowering \@llvm.memcmp this field specifies the maximum number of
  /// pairs of load operations that may be substituted for a call to memcmp.
  /// Targets must set this value based on the cost threshold for that target.
  /// Targets should assume that the memcmp will be done using as many of the
  /// largest load operations first, followed by smaller ones, if necessary, per
  /// alignment restrictions. For example, loading 7 bytes on a 32-bit machine
  /// with 32-bit alignment would result in one 4-byte load, a one 2-byte load
  /// and one 1-byte load. This only applies to copying a constant array of
  /// constant size.
  unsigned MaxLoadsPerMemcmp;
  /// Likewise for functions with the OptSize attribute.
  unsigned MaxLoadsPerMemcmpOptSize;

  /// \brief Specify maximum number of store instructions per memmove call.
  ///
  /// When lowering \@llvm.memmove this field specifies the maximum number of
  /// store instructions that may be substituted for a call to memmove. Targets
  /// must set this value based on the cost threshold for that target. Targets
  /// should assume that the memmove will be done using as many of the largest
  /// store operations first, followed by smaller ones, if necessary, per
  /// alignment restrictions. For example, moving 9 bytes on a 32-bit machine
  /// with 8-bit alignment would result in nine 1-byte stores.  This only
  /// applies to copying a constant array of constant size.
  unsigned MaxStoresPerMemmove;
  /// Likewise for functions with the OptSize attribute.
  unsigned MaxStoresPerMemmoveOptSize;

  /// Tells the code generator that select is more expensive than a branch if
  /// the branch is usually predicted right.
  bool PredictableSelectIsExpensive;

  /// \see enableExtLdPromotion.
  bool EnableExtLdPromotion;

  /// Return true if the value types that can be represented by the specified
  /// register class are all legal.
  bool isLegalRC(const TargetRegisterInfo &TRI,
                 const TargetRegisterClass &RC) const;

  /// Replace/modify any TargetFrameIndex operands with a targte-dependent
  /// sequence of memory operands that is recognized by PrologEpilogInserter.
  MachineBasicBlock *emitPatchPoint(MachineInstr &MI,
                                    MachineBasicBlock *MBB) const;

  bool IsStrictFPEnabled;
};

/// This class defines information used to lower LLVM code to legal SelectionDAG
/// operators that the target instruction selector can accept natively.
///
/// This class also defines callbacks that targets must implement to lower
/// target-specific constructs to SelectionDAG operators.
class TargetLowering : public TargetLoweringBase {
public:
  struct DAGCombinerInfo;
  struct MakeLibCallOptions;

  TargetLowering(const TargetLowering &) = delete;
  TargetLowering &operator=(const TargetLowering &) = delete;

  explicit TargetLowering(const TargetMachine &TM);

  bool isPositionIndependent() const;

  virtual bool isSDNodeSourceOfDivergence(const SDNode *N,
                                          FunctionLoweringInfo *FLI,
                                          UniformityInfo *UA) const {
    return false;
  }

  // Lets target to control the following reassociation of operands: (op (op x,
  // c1), y) -> (op (op x, y), c1) where N0 is (op x, c1) and N1 is y. By
  // default consider profitable any case where N0 has single use.  This
  // behavior reflects the condition replaced by this target hook call in the
  // DAGCombiner.  Any particular target can implement its own heuristic to
  // restrict common combiner.
  virtual bool isReassocProfitable(SelectionDAG &DAG, SDValue N0,
                                   SDValue N1) const {
    return N0.hasOneUse();
  }

  // Lets target to control the following reassociation of operands: (op (op x,
  // c1), y) -> (op (op x, y), c1) where N0 is (op x, c1) and N1 is y. By
  // default consider profitable any case where N0 has single use.  This
  // behavior reflects the condition replaced by this target hook call in the
  // combiner.  Any particular target can implement its own heuristic to
  // restrict common combiner.
  virtual bool isReassocProfitable(MachineRegisterInfo &MRI, Register N0,
                                   Register N1) const {
    return MRI.hasOneNonDBGUse(N0);
  }

  virtual bool isSDNodeAlwaysUniform(const SDNode * N) const {
    return false;
  }

  /// Returns true by value, base pointer and offset pointer and addressing mode
  /// by reference if the node's address can be legally represented as
  /// pre-indexed load / store address.
  virtual bool getPreIndexedAddressParts(SDNode * /*N*/, SDValue &/*Base*/,
                                         SDValue &/*Offset*/,
                                         ISD::MemIndexedMode &/*AM*/,
                                         SelectionDAG &/*DAG*/) const {
    return false;
  }

  /// Returns true by value, base pointer and offset pointer and addressing mode
  /// by reference if this node can be combined with a load / store to form a
  /// post-indexed load / store.
  virtual bool getPostIndexedAddressParts(SDNode * /*N*/, SDNode * /*Op*/,
                                          SDValue &/*Base*/,
                                          SDValue &/*Offset*/,
                                          ISD::MemIndexedMode &/*AM*/,
                                          SelectionDAG &/*DAG*/) const {
    return false;
  }

  /// Returns true if the specified base+offset is a legal indexed addressing
  /// mode for this target. \p MI is the load or store instruction that is being
  /// considered for transformation.
  virtual bool isIndexingLegal(MachineInstr &MI, Register Base, Register Offset,
                               bool IsPre, MachineRegisterInfo &MRI) const {
    return false;
  }

  /// Return the entry encoding for a jump table in the current function.  The
  /// returned value is a member of the MachineJumpTableInfo::JTEntryKind enum.
  virtual unsigned getJumpTableEncoding() const;

  virtual const MCExpr *
  LowerCustomJumpTableEntry(const MachineJumpTableInfo * /*MJTI*/,
                            const MachineBasicBlock * /*MBB*/, unsigned /*uid*/,
                            MCContext &/*Ctx*/) const {
    llvm_unreachable("Need to implement this hook if target has custom JTIs");
  }

  /// Returns relocation base for the given PIC jumptable.
  virtual SDValue getPICJumpTableRelocBase(SDValue Table,
                                           SelectionDAG &DAG) const;

  /// This returns the relocation base for the given PIC jumptable, the same as
  /// getPICJumpTableRelocBase, but as an MCExpr.
  virtual const MCExpr *
  getPICJumpTableRelocBaseExpr(const MachineFunction *MF,
                               unsigned JTI, MCContext &Ctx) const;

  /// Return true if folding a constant offset with the given GlobalAddress is
  /// legal.  It is frequently not legal in PIC relocation models.
  virtual bool isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const;

  /// On x86, return true if the operand with index OpNo is a CALL or JUMP
  /// instruction, which can use either a memory constraint or an address
  /// constraint. -fasm-blocks "__asm call foo" lowers to
  /// call void asm sideeffect inteldialect "call ${0:P}", "*m..."
  ///
  /// This function is used by a hack to choose the address constraint,
  /// lowering to a direct call.
  virtual bool
  isInlineAsmTargetBranch(const SmallVectorImpl<StringRef> &AsmStrs,
                          unsigned OpNo) const {
    return false;
  }

  bool isInTailCallPosition(SelectionDAG &DAG, SDNode *Node,
                            SDValue &Chain) const;

  void softenSetCCOperands(SelectionDAG &DAG, EVT VT, SDValue &NewLHS,
                           SDValue &NewRHS, ISD::CondCode &CCCode,
                           const SDLoc &DL, const SDValue OldLHS,
                           const SDValue OldRHS) const;

  void softenSetCCOperands(SelectionDAG &DAG, EVT VT, SDValue &NewLHS,
                           SDValue &NewRHS, ISD::CondCode &CCCode,
                           const SDLoc &DL, const SDValue OldLHS,
                           const SDValue OldRHS, SDValue &Chain,
                           bool IsSignaling = false) const;

  virtual SDValue visitMaskedLoad(SelectionDAG &DAG, const SDLoc &DL,
                                  SDValue Chain, MachineMemOperand *MMO,
                                  SDValue &NewLoad, SDValue Ptr,
                                  SDValue PassThru, SDValue Mask) const {
    llvm_unreachable("Not Implemented");
  }

  virtual SDValue visitMaskedStore(SelectionDAG &DAG, const SDLoc &DL,
                                   SDValue Chain, MachineMemOperand *MMO,
                                   SDValue Ptr, SDValue Val,
                                   SDValue Mask) const {
    llvm_unreachable("Not Implemented");
  }

  /// Returns a pair of (return value, chain).
  /// It is an error to pass RTLIB::UNKNOWN_LIBCALL as \p LC.
  std::pair<SDValue, SDValue> makeLibCall(SelectionDAG &DAG, RTLIB::Libcall LC,
                                          EVT RetVT, ArrayRef<SDValue> Ops,
                                          MakeLibCallOptions CallOptions,
                                          const SDLoc &dl,
                                          SDValue Chain = SDValue()) const;

  /// Check whether parameters to a call that are passed in callee saved
  /// registers are the same as from the calling function.  This needs to be
  /// checked for tail call eligibility.
  bool parametersInCSRMatch(const MachineRegisterInfo &MRI,
      const uint32_t *CallerPreservedMask,
      const SmallVectorImpl<CCValAssign> &ArgLocs,
      const SmallVectorImpl<SDValue> &OutVals) const;

  //===--------------------------------------------------------------------===//
  // TargetLowering Optimization Methods
  //

  /// A convenience struct that encapsulates a DAG, and two SDValues for
  /// returning information from TargetLowering to its clients that want to
  /// combine.
  struct TargetLoweringOpt {
    SelectionDAG &DAG;
    bool LegalTys;
    bool LegalOps;
    SDValue Old;
    SDValue New;

    explicit TargetLoweringOpt(SelectionDAG &InDAG,
                               bool LT, bool LO) :
      DAG(InDAG), LegalTys(LT), LegalOps(LO) {}

    bool LegalTypes() const { return LegalTys; }
    bool LegalOperations() const { return LegalOps; }

    bool CombineTo(SDValue O, SDValue N) {
      Old = O;
      New = N;
      return true;
    }
  };

  /// Determines the optimal series of memory ops to replace the memset / memcpy.
  /// Return true if the number of memory ops is below the threshold (Limit).
  /// Note that this is always the case when Limit is ~0.
  /// It returns the types of the sequence of memory ops to perform
  /// memset / memcpy by reference.
  virtual bool
  findOptimalMemOpLowering(std::vector<EVT> &MemOps, unsigned Limit,
                           const MemOp &Op, unsigned DstAS, unsigned SrcAS,
                           const AttributeList &FuncAttributes) const;

  /// Check to see if the specified operand of the specified instruction is a
  /// constant integer.  If so, check to see if there are any bits set in the
  /// constant that are not demanded.  If so, shrink the constant and return
  /// true.
  bool ShrinkDemandedConstant(SDValue Op, const APInt &DemandedBits,
                              const APInt &DemandedElts,
                              TargetLoweringOpt &TLO) const;

  /// Helper wrapper around ShrinkDemandedConstant, demanding all elements.
  bool ShrinkDemandedConstant(SDValue Op, const APInt &DemandedBits,
                              TargetLoweringOpt &TLO) const;

  // Target hook to do target-specific const optimization, which is called by
  // ShrinkDemandedConstant. This function should return true if the target
  // doesn't want ShrinkDemandedConstant to further optimize the constant.
  virtual bool targetShrinkDemandedConstant(SDValue Op,
                                            const APInt &DemandedBits,
                                            const APInt &DemandedElts,
                                            TargetLoweringOpt &TLO) const {
    return false;
  }

  /// Convert x+y to (VT)((SmallVT)x+(SmallVT)y) if the casts are free.
  /// This uses isTruncateFree/isZExtFree and ANY_EXTEND for the widening cast,
  /// but it could be generalized for targets with other types of implicit
  /// widening casts.
  bool ShrinkDemandedOp(SDValue Op, unsigned BitWidth,
                        const APInt &DemandedBits,
                        TargetLoweringOpt &TLO) const;

  /// Look at Op.  At this point, we know that only the DemandedBits bits of the
  /// result of Op are ever used downstream.  If we can use this information to
  /// simplify Op, create a new simplified DAG node and return true, returning
  /// the original and new nodes in Old and New.  Otherwise, analyze the
  /// expression and return a mask of KnownOne and KnownZero bits for the
  /// expression (used to simplify the caller).  The KnownZero/One bits may only
  /// be accurate for those bits in the Demanded masks.
  /// \p AssumeSingleUse When this parameter is true, this function will
  ///    attempt to simplify \p Op even if there are multiple uses.
  ///    Callers are responsible for correctly updating the DAG based on the
  ///    results of this function, because simply replacing TLO.Old
  ///    with TLO.New will be incorrect when this parameter is true and TLO.Old
  ///    has multiple uses.
  bool SimplifyDemandedBits(SDValue Op, const APInt &DemandedBits,
                            const APInt &DemandedElts, KnownBits &Known,
                            TargetLoweringOpt &TLO, unsigned Depth = 0,
                            bool AssumeSingleUse = false) const;

  /// Helper wrapper around SimplifyDemandedBits, demanding all elements.
  /// Adds Op back to the worklist upon success.
  bool SimplifyDemandedBits(SDValue Op, const APInt &DemandedBits,
                            KnownBits &Known, TargetLoweringOpt &TLO,
                            unsigned Depth = 0,
                            bool AssumeSingleUse = false) const;

  /// Helper wrapper around SimplifyDemandedBits.
  /// Adds Op back to the worklist upon success.
  bool SimplifyDemandedBits(SDValue Op, const APInt &DemandedBits,
                            DAGCombinerInfo &DCI) const;

  /// Helper wrapper around SimplifyDemandedBits.
  /// Adds Op back to the worklist upon success.
  bool SimplifyDemandedBits(SDValue Op, const APInt &DemandedBits,
                            const APInt &DemandedElts,
                            DAGCombinerInfo &DCI) const;

  /// More limited version of SimplifyDemandedBits that can be used to "look
  /// through" ops that don't contribute to the DemandedBits/DemandedElts -
  /// bitwise ops etc.
  SDValue SimplifyMultipleUseDemandedBits(SDValue Op, const APInt &DemandedBits,
                                          const APInt &DemandedElts,
                                          SelectionDAG &DAG,
                                          unsigned Depth = 0) const;

  /// Helper wrapper around SimplifyMultipleUseDemandedBits, demanding all
  /// elements.
  SDValue SimplifyMultipleUseDemandedBits(SDValue Op, const APInt &DemandedBits,
                                          SelectionDAG &DAG,
                                          unsigned Depth = 0) const;

  /// Helper wrapper around SimplifyMultipleUseDemandedBits, demanding all
  /// bits from only some vector elements.
  SDValue SimplifyMultipleUseDemandedVectorElts(SDValue Op,
                                                const APInt &DemandedElts,
                                                SelectionDAG &DAG,
                                                unsigned Depth = 0) const;

  /// Look at Vector Op. At this point, we know that only the DemandedElts
  /// elements of the result of Op are ever used downstream.  If we can use
  /// this information to simplify Op, create a new simplified DAG node and
  /// return true, storing the original and new nodes in TLO.
  /// Otherwise, analyze the expression and return a mask of KnownUndef and
  /// KnownZero elements for the expression (used to simplify the caller).
  /// The KnownUndef/Zero elements may only be accurate for those bits
  /// in the DemandedMask.
  /// \p AssumeSingleUse When this parameter is true, this function will
  ///    attempt to simplify \p Op even if there are multiple uses.
  ///    Callers are responsible for correctly updating the DAG based on the
  ///    results of this function, because simply replacing TLO.Old
  ///    with TLO.New will be incorrect when this parameter is true and TLO.Old
  ///    has multiple uses.
  bool SimplifyDemandedVectorElts(SDValue Op, const APInt &DemandedEltMask,
                                  APInt &KnownUndef, APInt &KnownZero,
                                  TargetLoweringOpt &TLO, unsigned Depth = 0,
                                  bool AssumeSingleUse = false) const;

  /// Helper wrapper around SimplifyDemandedVectorElts.
  /// Adds Op back to the worklist upon success.
  bool SimplifyDemandedVectorElts(SDValue Op, const APInt &DemandedElts,
                                  DAGCombinerInfo &DCI) const;

  /// Return true if the target supports simplifying demanded vector elements by
  /// converting them to undefs.
  virtual bool
  shouldSimplifyDemandedVectorElts(SDValue Op,
                                   const TargetLoweringOpt &TLO) const {
    return true;
  }

  /// Determine which of the bits specified in Mask are known to be either zero
  /// or one and return them in the KnownZero/KnownOne bitsets. The DemandedElts
  /// argument allows us to only collect the known bits that are shared by the
  /// requested vector elements.
  virtual void computeKnownBitsForTargetNode(const SDValue Op,
                                             KnownBits &Known,
                                             const APInt &DemandedElts,
                                             const SelectionDAG &DAG,
                                             unsigned Depth = 0) const;

  /// Determine which of the bits specified in Mask are known to be either zero
  /// or one and return them in the KnownZero/KnownOne bitsets. The DemandedElts
  /// argument allows us to only collect the known bits that are shared by the
  /// requested vector elements. This is for GISel.
  virtual void computeKnownBitsForTargetInstr(GISelKnownBits &Analysis,
                                              Register R, KnownBits &Known,
                                              const APInt &DemandedElts,
                                              const MachineRegisterInfo &MRI,
                                              unsigned Depth = 0) const;

  /// Determine the known alignment for the pointer value \p R. This is can
  /// typically be inferred from the number of low known 0 bits. However, for a
  /// pointer with a non-integral address space, the alignment value may be
  /// independent from the known low bits.
  virtual Align computeKnownAlignForTargetInstr(GISelKnownBits &Analysis,
                                                Register R,
                                                const MachineRegisterInfo &MRI,
                                                unsigned Depth = 0) const;

  /// Determine which of the bits of FrameIndex \p FIOp are known to be 0.
  /// Default implementation computes low bits based on alignment
  /// information. This should preserve known bits passed into it.
  virtual void computeKnownBitsForFrameIndex(int FIOp,
                                             KnownBits &Known,
                                             const MachineFunction &MF) const;

  /// This method can be implemented by targets that want to expose additional
  /// information about sign bits to the DAG Combiner. The DemandedElts
  /// argument allows us to only collect the minimum sign bits that are shared
  /// by the requested vector elements.
  virtual unsigned ComputeNumSignBitsForTargetNode(SDValue Op,
                                                   const APInt &DemandedElts,
                                                   const SelectionDAG &DAG,
                                                   unsigned Depth = 0) const;

  /// This method can be implemented by targets that want to expose additional
  /// information about sign bits to GlobalISel combiners. The DemandedElts
  /// argument allows us to only collect the minimum sign bits that are shared
  /// by the requested vector elements.
  virtual unsigned computeNumSignBitsForTargetInstr(GISelKnownBits &Analysis,
                                                    Register R,
                                                    const APInt &DemandedElts,
                                                    const MachineRegisterInfo &MRI,
                                                    unsigned Depth = 0) const;

  /// Attempt to simplify any target nodes based on the demanded vector
  /// elements, returning true on success. Otherwise, analyze the expression and
  /// return a mask of KnownUndef and KnownZero elements for the expression
  /// (used to simplify the caller). The KnownUndef/Zero elements may only be
  /// accurate for those bits in the DemandedMask.
  virtual bool SimplifyDemandedVectorEltsForTargetNode(
      SDValue Op, const APInt &DemandedElts, APInt &KnownUndef,
      APInt &KnownZero, TargetLoweringOpt &TLO, unsigned Depth = 0) const;

  /// Attempt to simplify any target nodes based on the demanded bits/elts,
  /// returning true on success. Otherwise, analyze the
  /// expression and return a mask of KnownOne and KnownZero bits for the
  /// expression (used to simplify the caller).  The KnownZero/One bits may only
  /// be accurate for those bits in the Demanded masks.
  virtual bool SimplifyDemandedBitsForTargetNode(SDValue Op,
                                                 const APInt &DemandedBits,
                                                 const APInt &DemandedElts,
                                                 KnownBits &Known,
                                                 TargetLoweringOpt &TLO,
                                                 unsigned Depth = 0) const;

  /// More limited version of SimplifyDemandedBits that can be used to "look
  /// through" ops that don't contribute to the DemandedBits/DemandedElts -
  /// bitwise ops etc.
  virtual SDValue SimplifyMultipleUseDemandedBitsForTargetNode(
      SDValue Op, const APInt &DemandedBits, const APInt &DemandedElts,
      SelectionDAG &DAG, unsigned Depth) const;

  /// Return true if this function can prove that \p Op is never poison
  /// and, if \p PoisonOnly is false, does not have undef bits. The DemandedElts
  /// argument limits the check to the requested vector elements.
  virtual bool isGuaranteedNotToBeUndefOrPoisonForTargetNode(
      SDValue Op, const APInt &DemandedElts, const SelectionDAG &DAG,
      bool PoisonOnly, unsigned Depth) const;

  /// Return true if Op can create undef or poison from non-undef & non-poison
  /// operands. The DemandedElts argument limits the check to the requested
  /// vector elements.
  virtual bool
  canCreateUndefOrPoisonForTargetNode(SDValue Op, const APInt &DemandedElts,
                                      const SelectionDAG &DAG, bool PoisonOnly,
                                      bool ConsiderFlags, unsigned Depth) const;

  /// Tries to build a legal vector shuffle using the provided parameters
  /// or equivalent variations. The Mask argument maybe be modified as the
  /// function tries different variations.
  /// Returns an empty SDValue if the operation fails.
  SDValue buildLegalVectorShuffle(EVT VT, const SDLoc &DL, SDValue N0,
                                  SDValue N1, MutableArrayRef<int> Mask,
                                  SelectionDAG &DAG) const;

  /// This method returns the constant pool value that will be loaded by LD.
  /// NOTE: You must check for implicit extensions of the constant by LD.
  virtual const Constant *getTargetConstantFromLoad(LoadSDNode *LD) const;

  /// If \p SNaN is false, \returns true if \p Op is known to never be any
  /// NaN. If \p sNaN is true, returns if \p Op is known to never be a signaling
  /// NaN.
  virtual bool isKnownNeverNaNForTargetNode(SDValue Op,
                                            const SelectionDAG &DAG,
                                            bool SNaN = false,
                                            unsigned Depth = 0) const;

  /// Return true if vector \p Op has the same value across all \p DemandedElts,
  /// indicating any elements which may be undef in the output \p UndefElts.
  virtual bool isSplatValueForTargetNode(SDValue Op, const APInt &DemandedElts,
                                         APInt &UndefElts,
                                         const SelectionDAG &DAG,
                                         unsigned Depth = 0) const;

  /// Returns true if the given Opc is considered a canonical constant for the
  /// target, which should not be transformed back into a BUILD_VECTOR.
  virtual bool isTargetCanonicalConstantNode(SDValue Op) const {
    return Op.getOpcode() == ISD::SPLAT_VECTOR ||
           Op.getOpcode() == ISD::SPLAT_VECTOR_PARTS;
  }

  struct DAGCombinerInfo {
    void *DC;  // The DAG Combiner object.
    CombineLevel Level;
    bool CalledByLegalizer;

  public:
    SelectionDAG &DAG;

    DAGCombinerInfo(SelectionDAG &dag, CombineLevel level,  bool cl, void *dc)
      : DC(dc), Level(level), CalledByLegalizer(cl), DAG(dag) {}

    bool isBeforeLegalize() const { return Level == BeforeLegalizeTypes; }
    bool isBeforeLegalizeOps() const { return Level < AfterLegalizeVectorOps; }
    bool isAfterLegalizeDAG() const { return Level >= AfterLegalizeDAG; }
    CombineLevel getDAGCombineLevel() { return Level; }
    bool isCalledByLegalizer() const { return CalledByLegalizer; }

    void AddToWorklist(SDNode *N);
    SDValue CombineTo(SDNode *N, ArrayRef<SDValue> To, bool AddTo = true);
    SDValue CombineTo(SDNode *N, SDValue Res, bool AddTo = true);
    SDValue CombineTo(SDNode *N, SDValue Res0, SDValue Res1, bool AddTo = true);

    bool recursivelyDeleteUnusedNodes(SDNode *N);

    void CommitTargetLoweringOpt(const TargetLoweringOpt &TLO);
  };

  /// Return if the N is a constant or constant vector equal to the true value
  /// from getBooleanContents().
  bool isConstTrueVal(SDValue N) const;

  /// Return if the N is a constant or constant vector equal to the false value
  /// from getBooleanContents().
  bool isConstFalseVal(SDValue N) const;

  /// Return if \p N is a True value when extended to \p VT.
  bool isExtendedTrueVal(const ConstantSDNode *N, EVT VT, bool SExt) const;

  /// Try to simplify a setcc built with the specified operands and cc. If it is
  /// unable to simplify it, return a null SDValue.
  SDValue SimplifySetCC(EVT VT, SDValue N0, SDValue N1, ISD::CondCode Cond,
                        bool foldBooleans, DAGCombinerInfo &DCI,
                        const SDLoc &dl) const;

  // For targets which wrap address, unwrap for analysis.
  virtual SDValue unwrapAddress(SDValue N) const { return N; }

  /// Returns true (and the GlobalValue and the offset) if the node is a
  /// GlobalAddress + offset.
  virtual bool
  isGAPlusOffset(SDNode *N, const GlobalValue* &GA, int64_t &Offset) const;

  /// This method will be invoked for all target nodes and for any
  /// target-independent nodes that the target has registered with invoke it
  /// for.
  ///
  /// The semantics are as follows:
  /// Return Value:
  ///   SDValue.Val == 0   - No change was made
  ///   SDValue.Val == N   - N was replaced, is dead, and is already handled.
  ///   otherwise          - N should be replaced by the returned Operand.
  ///
  /// In addition, methods provided by DAGCombinerInfo may be used to perform
  /// more complex transformations.
  ///
  virtual SDValue PerformDAGCombine(SDNode *N, DAGCombinerInfo &DCI) const;

  /// Return true if it is profitable to move this shift by a constant amount
  /// through its operand, adjusting any immediate operands as necessary to
  /// preserve semantics. This transformation may not be desirable if it
  /// disrupts a particularly auspicious target-specific tree (e.g. bitfield
  /// extraction in AArch64). By default, it returns true.
  ///
  /// @param N the shift node
  /// @param Level the current DAGCombine legalization level.
  virtual bool isDesirableToCommuteWithShift(const SDNode *N,
                                             CombineLevel Level) const {
    return true;
  }

  /// GlobalISel - return true if it is profitable to move this shift by a
  /// constant amount through its operand, adjusting any immediate operands as
  /// necessary to preserve semantics. This transformation may not be desirable
  /// if it disrupts a particularly auspicious target-specific tree (e.g.
  /// bitfield extraction in AArch64). By default, it returns true.
  ///
  /// @param MI the shift instruction
  /// @param IsAfterLegal true if running after legalization.
  virtual bool isDesirableToCommuteWithShift(const MachineInstr &MI,
                                             bool IsAfterLegal) const {
    return true;
  }

  /// GlobalISel - return true if it's profitable to perform the combine:
  /// shl ([sza]ext x), y => zext (shl x, y)
  virtual bool isDesirableToPullExtFromShl(const MachineInstr &MI) const {
    return true;
  }

  // Return AndOrSETCCFoldKind::{AddAnd, ABS} if its desirable to try and
  // optimize LogicOp(SETCC0, SETCC1). An example (what is implemented as of
  // writing this) is:
  //    With C as a power of 2 and C != 0 and C != INT_MIN:
  //    AddAnd:
  //     (icmp eq A, C) | (icmp eq A, -C)
  //            -> (icmp eq and(add(A, C), ~(C + C)), 0)
  //     (icmp ne A, C) & (icmp ne A, -C)w
  //            -> (icmp ne and(add(A, C), ~(C + C)), 0)
  //    ABS:
  //     (icmp eq A, C) | (icmp eq A, -C)
  //            -> (icmp eq Abs(A), C)
  //     (icmp ne A, C) & (icmp ne A, -C)w
  //            -> (icmp ne Abs(A), C)
  //
  // @param LogicOp the logic op
  // @param SETCC0 the first of the SETCC nodes
  // @param SETCC0 the second of the SETCC nodes
  virtual AndOrSETCCFoldKind isDesirableToCombineLogicOpOfSETCC(
      const SDNode *LogicOp, const SDNode *SETCC0, const SDNode *SETCC1) const {
    return AndOrSETCCFoldKind::None;
  }

  /// Return true if it is profitable to combine an XOR of a logical shift
  /// to create a logical shift of NOT. This transformation may not be desirable
  /// if it disrupts a particularly auspicious target-specific tree (e.g.
  /// BIC on ARM/AArch64). By default, it returns true.
  virtual bool isDesirableToCommuteXorWithShift(const SDNode *N) const {
    return true;
  }

  /// Return true if the target has native support for the specified value type
  /// and it is 'desirable' to use the type for the given node type. e.g. On x86
  /// i16 is legal, but undesirable since i16 instruction encodings are longer
  /// and some i16 instructions are slow.
  virtual bool isTypeDesirableForOp(unsigned /*Opc*/, EVT VT) const {
    // By default, assume all legal types are desirable.
    return isTypeLegal(VT);
  }

  /// Return true if it is profitable for dag combiner to transform a floating
  /// point op of specified opcode to a equivalent op of an integer
  /// type. e.g. f32 load -> i32 load can be profitable on ARM.
  virtual bool isDesirableToTransformToIntegerOp(unsigned /*Opc*/,
                                                 EVT /*VT*/) const {
    return false;
  }

  /// This method query the target whether it is beneficial for dag combiner to
  /// promote the specified node. If true, it should return the desired
  /// promotion type by reference.
  virtual bool IsDesirableToPromoteOp(SDValue /*Op*/, EVT &/*PVT*/) const {
    return false;
  }

  /// Return true if the target supports swifterror attribute. It optimizes
  /// loads and stores to reading and writing a specific register.
  virtual bool supportSwiftError() const {
    return false;
  }

  /// Return true if the target supports that a subset of CSRs for the given
  /// machine function is handled explicitly via copies.
  virtual bool supportSplitCSR(MachineFunction *MF) const {
    return false;
  }

  /// Return true if the target supports kcfi operand bundles.
  virtual bool supportKCFIBundles() const { return false; }

  /// Return true if the target supports ptrauth operand bundles.
  virtual bool supportPtrAuthBundles() const { return false; }

  /// Perform necessary initialization to handle a subset of CSRs explicitly
  /// via copies. This function is called at the beginning of instruction
  /// selection.
  virtual void initializeSplitCSR(MachineBasicBlock *Entry) const {
    llvm_unreachable("Not Implemented");
  }

  /// Insert explicit copies in entry and exit blocks. We copy a subset of
  /// CSRs to virtual registers in the entry block, and copy them back to
  /// physical registers in the exit blocks. This function is called at the end
  /// of instruction selection.
  virtual void insertCopiesSplitCSR(
      MachineBasicBlock *Entry,
      const SmallVectorImpl<MachineBasicBlock *> &Exits) const {
    llvm_unreachable("Not Implemented");
  }

  /// Return the newly negated expression if the cost is not expensive and
  /// set the cost in \p Cost to indicate that if it is cheaper or neutral to
  /// do the negation.
  virtual SDValue getNegatedExpression(SDValue Op, SelectionDAG &DAG,
                                       bool LegalOps, bool OptForSize,
                                       NegatibleCost &Cost,
                                       unsigned Depth = 0) const;

  SDValue getCheaperOrNeutralNegatedExpression(
      SDValue Op, SelectionDAG &DAG, bool LegalOps, bool OptForSize,
      const NegatibleCost CostThreshold = NegatibleCost::Neutral,
      unsigned Depth = 0) const {
    NegatibleCost Cost = NegatibleCost::Expensive;
    SDValue Neg =
        getNegatedExpression(Op, DAG, LegalOps, OptForSize, Cost, Depth);
    if (!Neg)
      return SDValue();

    if (Cost <= CostThreshold)
      return Neg;

    // Remove the new created node to avoid the side effect to the DAG.
    if (Neg->use_empty())
      DAG.RemoveDeadNode(Neg.getNode());
    return SDValue();
  }

  /// This is the helper function to return the newly negated expression only
  /// when the cost is cheaper.
  SDValue getCheaperNegatedExpression(SDValue Op, SelectionDAG &DAG,
                                      bool LegalOps, bool OptForSize,
                                      unsigned Depth = 0) const {
    return getCheaperOrNeutralNegatedExpression(Op, DAG, LegalOps, OptForSize,
                                                NegatibleCost::Cheaper, Depth);
  }

  /// This is the helper function to return the newly negated expression if
  /// the cost is not expensive.
  SDValue getNegatedExpression(SDValue Op, SelectionDAG &DAG, bool LegalOps,
                               bool OptForSize, unsigned Depth = 0) const {
    NegatibleCost Cost = NegatibleCost::Expensive;
    return getNegatedExpression(Op, DAG, LegalOps, OptForSize, Cost, Depth);
  }

  //===--------------------------------------------------------------------===//
  // Lowering methods - These methods must be implemented by targets so that
  // the SelectionDAGBuilder code knows how to lower these.
  //

  /// Target-specific splitting of values into parts that fit a register
  /// storing a legal type
  virtual bool splitValueIntoRegisterParts(
      SelectionDAG & DAG, const SDLoc &DL, SDValue Val, SDValue *Parts,
      unsigned NumParts, MVT PartVT, std::optional<CallingConv::ID> CC) const {
    return false;
  }

  /// Allows the target to handle physreg-carried dependency
  /// in target-specific way. Used from the ScheduleDAGSDNodes to decide whether
  /// to add the edge to the dependency graph.
  /// Def - input: Selection DAG node defininfg physical register
  /// User - input: Selection DAG node using physical register
  /// Op - input: Number of User operand
  /// PhysReg - inout: set to the physical register if the edge is
  /// necessary, unchanged otherwise
  /// Cost - inout: physical register copy cost.
  /// Returns 'true' is the edge is necessary, 'false' otherwise
  virtual bool checkForPhysRegDependency(SDNode *Def, SDNode *User, unsigned Op,
                                         const TargetRegisterInfo *TRI,
                                         const TargetInstrInfo *TII,
                                         unsigned &PhysReg, int &Cost) const {
    return false;
  }

  /// Target-specific combining of register parts into its original value
  virtual SDValue
  joinRegisterPartsIntoValue(SelectionDAG &DAG, const SDLoc &DL,
                             const SDValue *Parts, unsigned NumParts,
                             MVT PartVT, EVT ValueVT,
                             std::optional<CallingConv::ID> CC) const {
    return SDValue();
  }

  /// This hook must be implemented to lower the incoming (formal) arguments,
  /// described by the Ins array, into the specified DAG. The implementation
  /// should fill in the InVals array with legal-type argument values, and
  /// return the resulting token chain value.
  virtual SDValue LowerFormalArguments(
      SDValue /*Chain*/, CallingConv::ID /*CallConv*/, bool /*isVarArg*/,
      const SmallVectorImpl<ISD::InputArg> & /*Ins*/, const SDLoc & /*dl*/,
      SelectionDAG & /*DAG*/, SmallVectorImpl<SDValue> & /*InVals*/) const {
    llvm_unreachable("Not Implemented");
  }

  /// This structure contains the information necessary for lowering
  /// pointer-authenticating indirect calls.  It is equivalent to the "ptrauth"
  /// operand bundle found on the call instruction, if any.
  struct PtrAuthInfo {
    uint64_t Key;
    SDValue Discriminator;
  };

  /// This structure contains all information that is necessary for lowering
  /// calls. It is passed to TLI::LowerCallTo when the SelectionDAG builder
  /// needs to lower a call, and targets will see this struct in their LowerCall
  /// implementation.
  struct CallLoweringInfo {
    SDValue Chain;
    Type *RetTy = nullptr;
    bool RetSExt           : 1;
    bool RetZExt           : 1;
    bool IsVarArg          : 1;
    bool IsInReg           : 1;
    bool DoesNotReturn     : 1;
    bool IsReturnValueUsed : 1;
    bool IsConvergent      : 1;
    bool IsPatchPoint      : 1;
    bool IsPreallocated : 1;
    bool NoMerge           : 1;

    // IsTailCall should be modified by implementations of
    // TargetLowering::LowerCall that perform tail call conversions.
    bool IsTailCall = false;

    // Is Call lowering done post SelectionDAG type legalization.
    bool IsPostTypeLegalization = false;

    unsigned NumFixedArgs = -1;
    CallingConv::ID CallConv = CallingConv::C;
    SDValue Callee;
    ArgListTy Args;
    SelectionDAG &DAG;
    SDLoc DL;
    const CallBase *CB = nullptr;
    SmallVector<ISD::OutputArg, 32> Outs;
    SmallVector<SDValue, 32> OutVals;
    SmallVector<ISD::InputArg, 32> Ins;
    SmallVector<SDValue, 4> InVals;
    const ConstantInt *CFIType = nullptr;
    SDValue ConvergenceControlToken;

    std::optional<PtrAuthInfo> PAI;

    CallLoweringInfo(SelectionDAG &DAG)
        : RetSExt(false), RetZExt(false), IsVarArg(false), IsInReg(false),
          DoesNotReturn(false), IsReturnValueUsed(true), IsConvergent(false),
          IsPatchPoint(false), IsPreallocated(false), NoMerge(false),
          DAG(DAG) {}

    CallLoweringInfo &setDebugLoc(const SDLoc &dl) {
      DL = dl;
      return *this;
    }

    CallLoweringInfo &setChain(SDValue InChain) {
      Chain = InChain;
      return *this;
    }

    // setCallee with target/module-specific attributes
    CallLoweringInfo &setLibCallee(CallingConv::ID CC, Type *ResultType,
                                   SDValue Target, ArgListTy &&ArgsList) {
      RetTy = ResultType;
      Callee = Target;
      CallConv = CC;
      NumFixedArgs = ArgsList.size();
      Args = std::move(ArgsList);

      DAG.getTargetLoweringInfo().markLibCallAttributes(
          &(DAG.getMachineFunction()), CC, Args);
      return *this;
    }

    CallLoweringInfo &setCallee(CallingConv::ID CC, Type *ResultType,
                                SDValue Target, ArgListTy &&ArgsList,
                                AttributeSet ResultAttrs = {}) {
      RetTy = ResultType;
      IsInReg = ResultAttrs.hasAttribute(Attribute::InReg);
      RetSExt = ResultAttrs.hasAttribute(Attribute::SExt);
      RetZExt = ResultAttrs.hasAttribute(Attribute::ZExt);
      NoMerge = ResultAttrs.hasAttribute(Attribute::NoMerge);

      Callee = Target;
      CallConv = CC;
      NumFixedArgs = ArgsList.size();
      Args = std::move(ArgsList);
      return *this;
    }

    CallLoweringInfo &setCallee(Type *ResultType, FunctionType *FTy,
                                SDValue Target, ArgListTy &&ArgsList,
                                const CallBase &Call) {
      RetTy = ResultType;

      IsInReg = Call.hasRetAttr(Attribute::InReg);
      DoesNotReturn =
          Call.doesNotReturn() ||
          (!isa<InvokeInst>(Call) && isa<UnreachableInst>(Call.getNextNode()));
      IsVarArg = FTy->isVarArg();
      IsReturnValueUsed = !Call.use_empty();
      RetSExt = Call.hasRetAttr(Attribute::SExt);
      RetZExt = Call.hasRetAttr(Attribute::ZExt);
      NoMerge = Call.hasFnAttr(Attribute::NoMerge);

      Callee = Target;

      CallConv = Call.getCallingConv();
      NumFixedArgs = FTy->getNumParams();
      Args = std::move(ArgsList);

      CB = &Call;

      return *this;
    }

    CallLoweringInfo &setInRegister(bool Value = true) {
      IsInReg = Value;
      return *this;
    }

    CallLoweringInfo &setNoReturn(bool Value = true) {
      DoesNotReturn = Value;
      return *this;
    }

    CallLoweringInfo &setVarArg(bool Value = true) {
      IsVarArg = Value;
      return *this;
    }

    CallLoweringInfo &setTailCall(bool Value = true) {
      IsTailCall = Value;
      return *this;
    }

    CallLoweringInfo &setDiscardResult(bool Value = true) {
      IsReturnValueUsed = !Value;
      return *this;
    }

    CallLoweringInfo &setConvergent(bool Value = true) {
      IsConvergent = Value;
      return *this;
    }

    CallLoweringInfo &setSExtResult(bool Value = true) {
      RetSExt = Value;
      return *this;
    }

    CallLoweringInfo &setZExtResult(bool Value = true) {
      RetZExt = Value;
      return *this;
    }

    CallLoweringInfo &setIsPatchPoint(bool Value = true) {
      IsPatchPoint = Value;
      return *this;
    }

    CallLoweringInfo &setIsPreallocated(bool Value = true) {
      IsPreallocated = Value;
      return *this;
    }

    CallLoweringInfo &setPtrAuth(PtrAuthInfo Value) {
      PAI = Value;
      return *this;
    }

    CallLoweringInfo &setIsPostTypeLegalization(bool Value=true) {
      IsPostTypeLegalization = Value;
      return *this;
    }

    CallLoweringInfo &setCFIType(const ConstantInt *Type) {
      CFIType = Type;
      return *this;
    }

    CallLoweringInfo &setConvergenceControlToken(SDValue Token) {
      ConvergenceControlToken = Token;
      return *this;
    }

    ArgListTy &getArgs() {
      return Args;
    }
  };

  /// This structure is used to pass arguments to makeLibCall function.
  struct MakeLibCallOptions {
    // By passing type list before soften to makeLibCall, the target hook
    // shouldExtendTypeInLibCall can get the original type before soften.
    ArrayRef<EVT> OpsVTBeforeSoften;
    EVT RetVTBeforeSoften;
    bool IsSExt : 1;
    bool DoesNotReturn : 1;
    bool IsReturnValueUsed : 1;
    bool IsPostTypeLegalization : 1;
    bool IsSoften : 1;

    MakeLibCallOptions()
        : IsSExt(false), DoesNotReturn(false), IsReturnValueUsed(true),
          IsPostTypeLegalization(false), IsSoften(false) {}

    MakeLibCallOptions &setSExt(bool Value = true) {
      IsSExt = Value;
      return *this;
    }

    MakeLibCallOptions &setNoReturn(bool Value = true) {
      DoesNotReturn = Value;
      return *this;
    }

    MakeLibCallOptions &setDiscardResult(bool Value = true) {
      IsReturnValueUsed = !Value;
      return *this;
    }

    MakeLibCallOptions &setIsPostTypeLegalization(bool Value = true) {
      IsPostTypeLegalization = Value;
      return *this;
    }

    MakeLibCallOptions &setTypeListBeforeSoften(ArrayRef<EVT> OpsVT, EVT RetVT,
                                                bool Value = true) {
      OpsVTBeforeSoften = OpsVT;
      RetVTBeforeSoften = RetVT;
      IsSoften = Value;
      return *this;
    }
  };

  /// This function lowers an abstract call to a function into an actual call.
  /// This returns a pair of operands.  The first element is the return value
  /// for the function (if RetTy is not VoidTy).  The second element is the
  /// outgoing token chain. It calls LowerCall to do the actual lowering.
  std::pair<SDValue, SDValue> LowerCallTo(CallLoweringInfo &CLI) const;

  /// This hook must be implemented to lower calls into the specified
  /// DAG. The outgoing arguments to the call are described by the Outs array,
  /// and the values to be returned by the call are described by the Ins
  /// array. The implementation should fill in the InVals array with legal-type
  /// return values from the call, and return the resulting token chain value.
  virtual SDValue
    LowerCall(CallLoweringInfo &/*CLI*/,
              SmallVectorImpl<SDValue> &/*InVals*/) const {
    llvm_unreachable("Not Implemented");
  }

  /// Target-specific cleanup for formal ByVal parameters.
  virtual void HandleByVal(CCState *, unsigned &, Align) const {}

  /// This hook should be implemented to check whether the return values
  /// described by the Outs array can fit into the return registers.  If false
  /// is returned, an sret-demotion is performed.
  virtual bool CanLowerReturn(CallingConv::ID /*CallConv*/,
                              MachineFunction &/*MF*/, bool /*isVarArg*/,
               const SmallVectorImpl<ISD::OutputArg> &/*Outs*/,
               LLVMContext &/*Context*/) const
  {
    // Return true by default to get preexisting behavior.
    return true;
  }

  /// This hook must be implemented to lower outgoing return values, described
  /// by the Outs array, into the specified DAG. The implementation should
  /// return the resulting token chain value.
  virtual SDValue LowerReturn(SDValue /*Chain*/, CallingConv::ID /*CallConv*/,
                              bool /*isVarArg*/,
                              const SmallVectorImpl<ISD::OutputArg> & /*Outs*/,
                              const SmallVectorImpl<SDValue> & /*OutVals*/,
                              const SDLoc & /*dl*/,
                              SelectionDAG & /*DAG*/) const {
    llvm_unreachable("Not Implemented");
  }

  /// Return true if result of the specified node is used by a return node
  /// only. It also compute and return the input chain for the tail call.
  ///
  /// This is used to determine whether it is possible to codegen a libcall as
  /// tail call at legalization time.
  virtual bool isUsedByReturnOnly(SDNode *, SDValue &/*Chain*/) const {
    return false;
  }

  /// Return true if the target may be able emit the call instruction as a tail
  /// call. This is used by optimization passes to determine if it's profitable
  /// to duplicate return instructions to enable tailcall optimization.
  virtual bool mayBeEmittedAsTailCall(const CallInst *) const {
    return false;
  }

  /// Return the register ID of the name passed in. Used by named register
  /// global variables extension. There is no target-independent behaviour
  /// so the default action is to bail.
  virtual Register getRegisterByName(const char* RegName, LLT Ty,
                                     const MachineFunction &MF) const {
    report_fatal_error("Named registers not implemented for this target");
  }

  /// Return the type that should be used to zero or sign extend a
  /// zeroext/signext integer return value.  FIXME: Some C calling conventions
  /// require the return type to be promoted, but this is not true all the time,
  /// e.g. i1/i8/i16 on x86/x86_64. It is also not necessary for non-C calling
  /// conventions. The frontend should handle this and include all of the
  /// necessary information.
  virtual EVT getTypeForExtReturn(LLVMContext &Context, EVT VT,
                                       ISD::NodeType /*ExtendKind*/) const {
    EVT MinVT = getRegisterType(MVT::i32);
    return VT.bitsLT(MinVT) ? MinVT : VT;
  }

  /// For some targets, an LLVM struct type must be broken down into multiple
  /// simple types, but the calling convention specifies that the entire struct
  /// must be passed in a block of consecutive registers.
  virtual bool
  functionArgumentNeedsConsecutiveRegisters(Type *Ty, CallingConv::ID CallConv,
                                            bool isVarArg,
                                            const DataLayout &DL) const {
    return false;
  }

  /// For most targets, an LLVM type must be broken down into multiple
  /// smaller types. Usually the halves are ordered according to the endianness
  /// but for some platform that would break. So this method will default to
  /// matching the endianness but can be overridden.
  virtual bool
  shouldSplitFunctionArgumentsAsLittleEndian(const DataLayout &DL) const {
    return DL.isLittleEndian();
  }

  /// Returns a 0 terminated array of registers that can be safely used as
  /// scratch registers.
  virtual const MCPhysReg *getScratchRegisters(CallingConv::ID CC) const {
    return nullptr;
  }

  /// Returns a 0 terminated array of rounding control registers that can be
  /// attached into strict FP call.
  virtual ArrayRef<MCPhysReg> getRoundingControlRegisters() const {
    return ArrayRef<MCPhysReg>();
  }

  /// This callback is used to prepare for a volatile or atomic load.
  /// It takes a chain node as input and returns the chain for the load itself.
  ///
  /// Having a callback like this is necessary for targets like SystemZ,
  /// which allows a CPU to reuse the result of a previous load indefinitely,
  /// even if a cache-coherent store is performed by another CPU.  The default
  /// implementation does nothing.
  virtual SDValue prepareVolatileOrAtomicLoad(SDValue Chain, const SDLoc &DL,
                                              SelectionDAG &DAG) const {
    return Chain;
  }

  /// This callback is invoked by the type legalizer to legalize nodes with an
  /// illegal operand type but legal result types.  It replaces the
  /// LowerOperation callback in the type Legalizer.  The reason we can not do
  /// away with LowerOperation entirely is that LegalizeDAG isn't yet ready to
  /// use this callback.
  ///
  /// TODO: Consider merging with ReplaceNodeResults.
  ///
  /// The target places new result values for the node in Results (their number
  /// and types must exactly match those of the original return values of
  /// the node), or leaves Results empty, which indicates that the node is not
  /// to be custom lowered after all.
  /// The default implementation calls LowerOperation.
  virtual void LowerOperationWrapper(SDNode *N,
                                     SmallVectorImpl<SDValue> &Results,
                                     SelectionDAG &DAG) const;

  /// This callback is invoked for operations that are unsupported by the
  /// target, which are registered to use 'custom' lowering, and whose defined
  /// values are all legal.  If the target has no operations that require custom
  /// lowering, it need not implement this.  The default implementation of this
  /// aborts.
  virtual SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const;

  /// This callback is invoked when a node result type is illegal for the
  /// target, and the operation was registered to use 'custom' lowering for that
  /// result type.  The target places new result values for the node in Results
  /// (their number and types must exactly match those of the original return
  /// values of the node), or leaves Results empty, which indicates that the
  /// node is not to be custom lowered after all.
  ///
  /// If the target has no operations that require custom lowering, it need not
  /// implement this.  The default implementation aborts.
  virtual void ReplaceNodeResults(SDNode * /*N*/,
                                  SmallVectorImpl<SDValue> &/*Results*/,
                                  SelectionDAG &/*DAG*/) const {
    llvm_unreachable("ReplaceNodeResults not implemented for this target!");
  }

  /// This method returns the name of a target specific DAG node.
  virtual const char *getTargetNodeName(unsigned Opcode) const;

  /// This method returns a target specific FastISel object, or null if the
  /// target does not support "fast" ISel.
  virtual FastISel *createFastISel(FunctionLoweringInfo &,
                                   const TargetLibraryInfo *) const {
    return nullptr;
  }

  bool verifyReturnAddressArgumentIsConstant(SDValue Op,
                                             SelectionDAG &DAG) const;

#ifndef NDEBUG
  /// Check the given SDNode.  Aborts if it is invalid.
  virtual void verifyTargetSDNode(const SDNode *N) const {};
#endif

  //===--------------------------------------------------------------------===//
  // Inline Asm Support hooks
  //

  /// This hook allows the target to expand an inline asm call to be explicit
  /// llvm code if it wants to.  This is useful for turning simple inline asms
  /// into LLVM intrinsics, which gives the compiler more information about the
  /// behavior of the code.
  virtual bool ExpandInlineAsm(CallInst *) const {
    return false;
  }

  enum ConstraintType {
    C_Register,            // Constraint represents specific register(s).
    C_RegisterClass,       // Constraint represents any of register(s) in class.
    C_Memory,              // Memory constraint.
    C_Address,             // Address constraint.
    C_Immediate,           // Requires an immediate.
    C_Other,               // Something else.
    C_Unknown              // Unsupported constraint.
  };

  enum ConstraintWeight {
    // Generic weights.
    CW_Invalid  = -1,     // No match.
    CW_Okay     = 0,      // Acceptable.
    CW_Good     = 1,      // Good weight.
    CW_Better   = 2,      // Better weight.
    CW_Best     = 3,      // Best weight.

    // Well-known weights.
    CW_SpecificReg  = CW_Okay,    // Specific register operands.
    CW_Register     = CW_Good,    // Register operands.
    CW_Memory       = CW_Better,  // Memory operands.
    CW_Constant     = CW_Best,    // Constant operand.
    CW_Default      = CW_Okay     // Default or don't know type.
  };

  /// This contains information for each constraint that we are lowering.
  struct AsmOperandInfo : public InlineAsm::ConstraintInfo {
    /// This contains the actual string for the code, like "m".  TargetLowering
    /// picks the 'best' code from ConstraintInfo::Codes that most closely
    /// matches the operand.
    std::string ConstraintCode;

    /// Information about the constraint code, e.g. Register, RegisterClass,
    /// Memory, Other, Unknown.
    TargetLowering::ConstraintType ConstraintType = TargetLowering::C_Unknown;

    /// If this is the result output operand or a clobber, this is null,
    /// otherwise it is the incoming operand to the CallInst.  This gets
    /// modified as the asm is processed.
    Value *CallOperandVal = nullptr;

    /// The ValueType for the operand value.
    MVT ConstraintVT = MVT::Other;

    /// Copy constructor for copying from a ConstraintInfo.
    AsmOperandInfo(InlineAsm::ConstraintInfo Info)
        : InlineAsm::ConstraintInfo(std::move(Info)) {}

    /// Return true of this is an input operand that is a matching constraint
    /// like "4".
    bool isMatchingInputConstraint() const;

    /// If this is an input matching constraint, this method returns the output
    /// operand it matches.
    unsigned getMatchedOperand() const;
  };

  using AsmOperandInfoVector = std::vector<AsmOperandInfo>;

  /// Split up the constraint string from the inline assembly value into the
  /// specific constraints and their prefixes, and also tie in the associated
  /// operand values.  If this returns an empty vector, and if the constraint
  /// string itself isn't empty, there was an error parsing.
  virtual AsmOperandInfoVector ParseConstraints(const DataLayout &DL,
                                                const TargetRegisterInfo *TRI,
                                                const CallBase &Call) const;

  /// Examine constraint type and operand type and determine a weight value.
  /// The operand object must already have been set up with the operand type.
  virtual ConstraintWeight getMultipleConstraintMatchWeight(
      AsmOperandInfo &info, int maIndex) const;

  /// Examine constraint string and operand type and determine a weight value.
  /// The operand object must already have been set up with the operand type.
  virtual ConstraintWeight getSingleConstraintMatchWeight(
      AsmOperandInfo &info, const char *constraint) const;

  /// Determines the constraint code and constraint type to use for the specific
  /// AsmOperandInfo, setting OpInfo.ConstraintCode and OpInfo.ConstraintType.
  /// If the actual operand being passed in is available, it can be passed in as
  /// Op, otherwise an empty SDValue can be passed.
  virtual void ComputeConstraintToUse(AsmOperandInfo &OpInfo,
                                      SDValue Op,
                                      SelectionDAG *DAG = nullptr) const;

  /// Given a constraint, return the type of constraint it is for this target.
  virtual ConstraintType getConstraintType(StringRef Constraint) const;

  using ConstraintPair = std::pair<StringRef, TargetLowering::ConstraintType>;
  using ConstraintGroup = SmallVector<ConstraintPair>;
  /// Given an OpInfo with list of constraints codes as strings, return a
  /// sorted Vector of pairs of constraint codes and their types in priority of
  /// what we'd prefer to lower them as. This may contain immediates that
  /// cannot be lowered, but it is meant to be a machine agnostic order of
  /// preferences.
  ConstraintGroup getConstraintPreferences(AsmOperandInfo &OpInfo) const;

  /// Given a physical register constraint (e.g.  {edx}), return the register
  /// number and the register class for the register.
  ///
  /// Given a register class constraint, like 'r', if this corresponds directly
  /// to an LLVM register class, return a register of 0 and the register class
  /// pointer.
  ///
  /// This should only be used for C_Register constraints.  On error, this
  /// returns a register number of 0 and a null register class pointer.
  virtual std::pair<unsigned, const TargetRegisterClass *>
  getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                               StringRef Constraint, MVT VT) const;

  virtual InlineAsm::ConstraintCode
  getInlineAsmMemConstraint(StringRef ConstraintCode) const {
    if (ConstraintCode == "m")
      return InlineAsm::ConstraintCode::m;
    if (ConstraintCode == "o")
      return InlineAsm::ConstraintCode::o;
    if (ConstraintCode == "X")
      return InlineAsm::ConstraintCode::X;
    if (ConstraintCode == "p")
      return InlineAsm::ConstraintCode::p;
    return InlineAsm::ConstraintCode::Unknown;
  }

  /// Try to replace an X constraint, which matches anything, with another that
  /// has more specific requirements based on the type of the corresponding
  /// operand.  This returns null if there is no replacement to make.
  virtual const char *LowerXConstraint(EVT ConstraintVT) const;

  /// Lower the specified operand into the Ops vector.  If it is invalid, don't
  /// add anything to Ops.
  virtual void LowerAsmOperandForConstraint(SDValue Op, StringRef Constraint,
                                            std::vector<SDValue> &Ops,
                                            SelectionDAG &DAG) const;

  // Lower custom output constraints. If invalid, return SDValue().
  virtual SDValue LowerAsmOutputForConstraint(SDValue &Chain, SDValue &Glue,
                                              const SDLoc &DL,
                                              const AsmOperandInfo &OpInfo,
                                              SelectionDAG &DAG) const;

  // Targets may override this function to collect operands from the CallInst
  // and for example, lower them into the SelectionDAG operands.
  virtual void CollectTargetIntrinsicOperands(const CallInst &I,
                                              SmallVectorImpl<SDValue> &Ops,
                                              SelectionDAG &DAG) const;

  //===--------------------------------------------------------------------===//
  // Div utility functions
  //

  SDValue BuildSDIV(SDNode *N, SelectionDAG &DAG, bool IsAfterLegalization,
                    SmallVectorImpl<SDNode *> &Created) const;
  SDValue BuildUDIV(SDNode *N, SelectionDAG &DAG, bool IsAfterLegalization,
                    SmallVectorImpl<SDNode *> &Created) const;
  // Build sdiv by power-of-2 with conditional move instructions
  SDValue buildSDIVPow2WithCMov(SDNode *N, const APInt &Divisor,
                                SelectionDAG &DAG,
                                SmallVectorImpl<SDNode *> &Created) const;

  /// Targets may override this function to provide custom SDIV lowering for
  /// power-of-2 denominators.  If the target returns an empty SDValue, LLVM
  /// assumes SDIV is expensive and replaces it with a series of other integer
  /// operations.
  virtual SDValue BuildSDIVPow2(SDNode *N, const APInt &Divisor,
                                SelectionDAG &DAG,
                                SmallVectorImpl<SDNode *> &Created) const;

  /// Targets may override this function to provide custom SREM lowering for
  /// power-of-2 denominators.  If the target returns an empty SDValue, LLVM
  /// assumes SREM is expensive and replaces it with a series of other integer
  /// operations.
  virtual SDValue BuildSREMPow2(SDNode *N, const APInt &Divisor,
                                SelectionDAG &DAG,
                                SmallVectorImpl<SDNode *> &Created) const;

  /// Indicate whether this target prefers to combine FDIVs with the same
  /// divisor. If the transform should never be done, return zero. If the
  /// transform should be done, return the minimum number of divisor uses
  /// that must exist.
  virtual unsigned combineRepeatedFPDivisors() const {
    return 0;
  }

  /// Hooks for building estimates in place of slower divisions and square
  /// roots.

  /// Return either a square root or its reciprocal estimate value for the input
  /// operand.
  /// \p Enabled is a ReciprocalEstimate enum with value either 'Unspecified' or
  /// 'Enabled' as set by a potential default override attribute.
  /// If \p RefinementSteps is 'Unspecified', the number of Newton-Raphson
  /// refinement iterations required to generate a sufficient (though not
  /// necessarily IEEE-754 compliant) estimate is returned in that parameter.
  /// The boolean UseOneConstNR output is used to select a Newton-Raphson
  /// algorithm implementation that uses either one or two constants.
  /// The boolean Reciprocal is used to select whether the estimate is for the
  /// square root of the input operand or the reciprocal of its square root.
  /// A target may choose to implement its own refinement within this function.
  /// If that's true, then return '0' as the number of RefinementSteps to avoid
  /// any further refinement of the estimate.
  /// An empty SDValue return means no estimate sequence can be created.
  virtual SDValue getSqrtEstimate(SDValue Operand, SelectionDAG &DAG,
                                  int Enabled, int &RefinementSteps,
                                  bool &UseOneConstNR, bool Reciprocal) const {
    return SDValue();
  }

  /// Try to convert the fminnum/fmaxnum to a compare/select sequence. This is
  /// required for correctness since InstCombine might have canonicalized a
  /// fcmp+select sequence to a FMINNUM/FMAXNUM intrinsic.  If we were to fall
  /// through to the default expansion/soften to libcall, we might introduce a
  /// link-time dependency on libm into a file that originally did not have one.
  SDValue createSelectForFMINNUM_FMAXNUM(SDNode *Node, SelectionDAG &DAG) const;

  /// Return a reciprocal estimate value for the input operand.
  /// \p Enabled is a ReciprocalEstimate enum with value either 'Unspecified' or
  /// 'Enabled' as set by a potential default override attribute.
  /// If \p RefinementSteps is 'Unspecified', the number of Newton-Raphson
  /// refinement iterations required to generate a sufficient (though not
  /// necessarily IEEE-754 compliant) estimate is returned in that parameter.
  /// A target may choose to implement its own refinement within this function.
  /// If that's true, then return '0' as the number of RefinementSteps to avoid
  /// any further refinement of the estimate.
  /// An empty SDValue return means no estimate sequence can be created.
  virtual SDValue getRecipEstimate(SDValue Operand, SelectionDAG &DAG,
                                   int Enabled, int &RefinementSteps) const {
    return SDValue();
  }

  /// Return a target-dependent comparison result if the input operand is
  /// suitable for use with a square root estimate calculation. For example, the
  /// comparison may check if the operand is NAN, INF, zero, normal, etc. The
  /// result should be used as the condition operand for a select or branch.
  virtual SDValue getSqrtInputTest(SDValue Operand, SelectionDAG &DAG,
                                   const DenormalMode &Mode) const;

  /// Return a target-dependent result if the input operand is not suitable for
  /// use with a square root estimate calculation.
  virtual SDValue getSqrtResultForDenormInput(SDValue Operand,
                                              SelectionDAG &DAG) const {
    return DAG.getConstantFP(0.0, SDLoc(Operand), Operand.getValueType());
  }

  //===--------------------------------------------------------------------===//
  // Legalization utility functions
  //

  /// Expand a MUL or [US]MUL_LOHI of n-bit values into two or four nodes,
  /// respectively, each computing an n/2-bit part of the result.
  /// \param Result A vector that will be filled with the parts of the result
  ///        in little-endian order.
  /// \param LL Low bits of the LHS of the MUL.  You can use this parameter
  ///        if you want to control how low bits are extracted from the LHS.
  /// \param LH High bits of the LHS of the MUL.  See LL for meaning.
  /// \param RL Low bits of the RHS of the MUL.  See LL for meaning
  /// \param RH High bits of the RHS of the MUL.  See LL for meaning.
  /// \returns true if the node has been expanded, false if it has not
  bool expandMUL_LOHI(unsigned Opcode, EVT VT, const SDLoc &dl, SDValue LHS,
                      SDValue RHS, SmallVectorImpl<SDValue> &Result, EVT HiLoVT,
                      SelectionDAG &DAG, MulExpansionKind Kind,
                      SDValue LL = SDValue(), SDValue LH = SDValue(),
                      SDValue RL = SDValue(), SDValue RH = SDValue()) const;

  /// Expand a MUL into two nodes.  One that computes the high bits of
  /// the result and one that computes the low bits.
  /// \param HiLoVT The value type to use for the Lo and Hi nodes.
  /// \param LL Low bits of the LHS of the MUL.  You can use this parameter
  ///        if you want to control how low bits are extracted from the LHS.
  /// \param LH High bits of the LHS of the MUL.  See LL for meaning.
  /// \param RL Low bits of the RHS of the MUL.  See LL for meaning
  /// \param RH High bits of the RHS of the MUL.  See LL for meaning.
  /// \returns true if the node has been expanded. false if it has not
  bool expandMUL(SDNode *N, SDValue &Lo, SDValue &Hi, EVT HiLoVT,
                 SelectionDAG &DAG, MulExpansionKind Kind,
                 SDValue LL = SDValue(), SDValue LH = SDValue(),
                 SDValue RL = SDValue(), SDValue RH = SDValue()) const;

  /// Attempt to expand an n-bit div/rem/divrem by constant using a n/2-bit
  /// urem by constant and other arithmetic ops. The n/2-bit urem by constant
  /// will be expanded by DAGCombiner. This is not possible for all constant
  /// divisors.
  /// \param N Node to expand
  /// \param Result A vector that will be filled with the lo and high parts of
  ///        the results. For *DIVREM, this will be the quotient parts followed
  ///        by the remainder parts.
  /// \param HiLoVT The value type to use for the Lo and Hi parts. Should be
  ///        half of VT.
  /// \param LL Low bits of the LHS of the operation. You can use this
  ///        parameter if you want to control how low bits are extracted from
  ///        the LHS.
  /// \param LH High bits of the LHS of the operation. See LL for meaning.
  /// \returns true if the node has been expanded, false if it has not.
  bool expandDIVREMByConstant(SDNode *N, SmallVectorImpl<SDValue> &Result,
                              EVT HiLoVT, SelectionDAG &DAG,
                              SDValue LL = SDValue(),
                              SDValue LH = SDValue()) const;

  /// Expand funnel shift.
  /// \param N Node to expand
  /// \returns The expansion if successful, SDValue() otherwise
  SDValue expandFunnelShift(SDNode *N, SelectionDAG &DAG) const;

  /// Expand rotations.
  /// \param N Node to expand
  /// \param AllowVectorOps expand vector rotate, this should only be performed
  ///        if the legalization is happening outside of LegalizeVectorOps
  /// \returns The expansion if successful, SDValue() otherwise
  SDValue expandROT(SDNode *N, bool AllowVectorOps, SelectionDAG &DAG) const;

  /// Expand shift-by-parts.
  /// \param N Node to expand
  /// \param Lo lower-output-part after conversion
  /// \param Hi upper-output-part after conversion
  void expandShiftParts(SDNode *N, SDValue &Lo, SDValue &Hi,
                        SelectionDAG &DAG) const;

  /// Expand float(f32) to SINT(i64) conversion
  /// \param N Node to expand
  /// \param Result output after conversion
  /// \returns True, if the expansion was successful, false otherwise
  bool expandFP_TO_SINT(SDNode *N, SDValue &Result, SelectionDAG &DAG) const;

  /// Expand float to UINT conversion
  /// \param N Node to expand
  /// \param Result output after conversion
  /// \param Chain output chain after conversion
  /// \returns True, if the expansion was successful, false otherwise
  bool expandFP_TO_UINT(SDNode *N, SDValue &Result, SDValue &Chain,
                        SelectionDAG &DAG) const;

  /// Expand UINT(i64) to double(f64) conversion
  /// \param N Node to expand
  /// \param Result output after conversion
  /// \param Chain output chain after conversion
  /// \returns True, if the expansion was successful, false otherwise
  bool expandUINT_TO_FP(SDNode *N, SDValue &Result, SDValue &Chain,
                        SelectionDAG &DAG) const;

  /// Expand fminnum/fmaxnum into fminnum_ieee/fmaxnum_ieee with quieted inputs.
  SDValue expandFMINNUM_FMAXNUM(SDNode *N, SelectionDAG &DAG) const;

  /// Expand fminimum/fmaximum into multiple comparison with selects.
  SDValue expandFMINIMUM_FMAXIMUM(SDNode *N, SelectionDAG &DAG) const;

  /// Expand FP_TO_[US]INT_SAT into FP_TO_[US]INT and selects or min/max.
  /// \param N Node to expand
  /// \returns The expansion result
  SDValue expandFP_TO_INT_SAT(SDNode *N, SelectionDAG &DAG) const;

  /// Truncate Op to ResultVT. If the result is exact, leave it alone. If it is
  /// not exact, force the result to be odd.
  /// \param ResultVT The type of result.
  /// \param Op The value to round.
  /// \returns The expansion result
  SDValue expandRoundInexactToOdd(EVT ResultVT, SDValue Op, const SDLoc &DL,
                                  SelectionDAG &DAG) const;

  /// Expand round(fp) to fp conversion
  /// \param N Node to expand
  /// \returns The expansion result
  SDValue expandFP_ROUND(SDNode *Node, SelectionDAG &DAG) const;

  /// Expand check for floating point class.
  /// \param ResultVT The type of intrinsic call result.
  /// \param Op The tested value.
  /// \param Test The test to perform.
  /// \param Flags The optimization flags.
  /// \returns The expansion result or SDValue() if it fails.
  SDValue expandIS_FPCLASS(EVT ResultVT, SDValue Op, FPClassTest Test,
                           SDNodeFlags Flags, const SDLoc &DL,
                           SelectionDAG &DAG) const;

  /// Expand CTPOP nodes. Expands vector/scalar CTPOP nodes,
  /// vector nodes can only succeed if all operations are legal/custom.
  /// \param N Node to expand
  /// \returns The expansion result or SDValue() if it fails.
  SDValue expandCTPOP(SDNode *N, SelectionDAG &DAG) const;

  /// Expand VP_CTPOP nodes.
  /// \returns The expansion result or SDValue() if it fails.
  SDValue expandVPCTPOP(SDNode *N, SelectionDAG &DAG) const;

  /// Expand CTLZ/CTLZ_ZERO_UNDEF nodes. Expands vector/scalar CTLZ nodes,
  /// vector nodes can only succeed if all operations are legal/custom.
  /// \param N Node to expand
  /// \returns The expansion result or SDValue() if it fails.
  SDValue expandCTLZ(SDNode *N, SelectionDAG &DAG) const;

  /// Expand VP_CTLZ/VP_CTLZ_ZERO_UNDEF nodes.
  /// \param N Node to expand
  /// \returns The expansion result or SDValue() if it fails.
  SDValue expandVPCTLZ(SDNode *N, SelectionDAG &DAG) const;

  /// Expand CTTZ via Table Lookup.
  /// \param N Node to expand
  /// \returns The expansion result or SDValue() if it fails.
  SDValue CTTZTableLookup(SDNode *N, SelectionDAG &DAG, const SDLoc &DL, EVT VT,
                          SDValue Op, unsigned NumBitsPerElt) const;

  /// Expand CTTZ/CTTZ_ZERO_UNDEF nodes. Expands vector/scalar CTTZ nodes,
  /// vector nodes can only succeed if all operations are legal/custom.
  /// \param N Node to expand
  /// \returns The expansion result or SDValue() if it fails.
  SDValue expandCTTZ(SDNode *N, SelectionDAG &DAG) const;

  /// Expand VP_CTTZ/VP_CTTZ_ZERO_UNDEF nodes.
  /// \param N Node to expand
  /// \returns The expansion result or SDValue() if it fails.
  SDValue expandVPCTTZ(SDNode *N, SelectionDAG &DAG) const;

  /// Expand VP_CTTZ_ELTS/VP_CTTZ_ELTS_ZERO_UNDEF nodes.
  /// \param N Node to expand
  /// \returns The expansion result or SDValue() if it fails.
  SDValue expandVPCTTZElements(SDNode *N, SelectionDAG &DAG) const;

  /// Expand ABS nodes. Expands vector/scalar ABS nodes,
  /// vector nodes can only succeed if all operations are legal/custom.
  /// (ABS x) -> (XOR (ADD x, (SRA x, type_size)), (SRA x, type_size))
  /// \param N Node to expand
  /// \param IsNegative indicate negated abs
  /// \returns The expansion result or SDValue() if it fails.
  SDValue expandABS(SDNode *N, SelectionDAG &DAG,
                    bool IsNegative = false) const;

  /// Expand ABDS/ABDU nodes. Expands vector/scalar ABDS/ABDU nodes.
  /// \param N Node to expand
  /// \returns The expansion result or SDValue() if it fails.
  SDValue expandABD(SDNode *N, SelectionDAG &DAG) const;

  /// Expand vector/scalar AVGCEILS/AVGCEILU/AVGFLOORS/AVGFLOORU nodes.
  /// \param N Node to expand
  /// \returns The expansion result or SDValue() if it fails.
  SDValue expandAVG(SDNode *N, SelectionDAG &DAG) const;

  /// Expand BSWAP nodes. Expands scalar/vector BSWAP nodes with i16/i32/i64
  /// scalar types. Returns SDValue() if expand fails.
  /// \param N Node to expand
  /// \returns The expansion result or SDValue() if it fails.
  SDValue expandBSWAP(SDNode *N, SelectionDAG &DAG) const;

  /// Expand VP_BSWAP nodes. Expands VP_BSWAP nodes with
  /// i16/i32/i64 scalar types. Returns SDValue() if expand fails. \param N Node
  /// to expand \returns The expansion result or SDValue() if it fails.
  SDValue expandVPBSWAP(SDNode *N, SelectionDAG &DAG) const;

  /// Expand BITREVERSE nodes. Expands scalar/vector BITREVERSE nodes.
  /// Returns SDValue() if expand fails.
  /// \param N Node to expand
  /// \returns The expansion result or SDValue() if it fails.
  SDValue expandBITREVERSE(SDNode *N, SelectionDAG &DAG) const;

  /// Expand VP_BITREVERSE nodes. Expands VP_BITREVERSE nodes with
  /// i8/i16/i32/i64 scalar types. \param N Node to expand \returns The
  /// expansion result or SDValue() if it fails.
  SDValue expandVPBITREVERSE(SDNode *N, SelectionDAG &DAG) const;

  /// Turn load of vector type into a load of the individual elements.
  /// \param LD load to expand
  /// \returns BUILD_VECTOR and TokenFactor nodes.
  std::pair<SDValue, SDValue> scalarizeVectorLoad(LoadSDNode *LD,
                                                  SelectionDAG &DAG) const;

  // Turn a store of a vector type into stores of the individual elements.
  /// \param ST Store with a vector value type
  /// \returns TokenFactor of the individual store chains.
  SDValue scalarizeVectorStore(StoreSDNode *ST, SelectionDAG &DAG) const;

  /// Expands an unaligned load to 2 half-size loads for an integer, and
  /// possibly more for vectors.
  std::pair<SDValue, SDValue> expandUnalignedLoad(LoadSDNode *LD,
                                                  SelectionDAG &DAG) const;

  /// Expands an unaligned store to 2 half-size stores for integer values, and
  /// possibly more for vectors.
  SDValue expandUnalignedStore(StoreSDNode *ST, SelectionDAG &DAG) const;

  /// Increments memory address \p Addr according to the type of the value
  /// \p DataVT that should be stored. If the data is stored in compressed
  /// form, the memory address should be incremented according to the number of
  /// the stored elements. This number is equal to the number of '1's bits
  /// in the \p Mask.
  /// \p DataVT is a vector type. \p Mask is a vector value.
  /// \p DataVT and \p Mask have the same number of vector elements.
  SDValue IncrementMemoryAddress(SDValue Addr, SDValue Mask, const SDLoc &DL,
                                 EVT DataVT, SelectionDAG &DAG,
                                 bool IsCompressedMemory) const;

  /// Get a pointer to vector element \p Idx located in memory for a vector of
  /// type \p VecVT starting at a base address of \p VecPtr. If \p Idx is out of
  /// bounds the returned pointer is unspecified, but will be within the vector
  /// bounds.
  SDValue getVectorElementPointer(SelectionDAG &DAG, SDValue VecPtr, EVT VecVT,
                                  SDValue Index) const;

  /// Get a pointer to a sub-vector of type \p SubVecVT at index \p Idx located
  /// in memory for a vector of type \p VecVT starting at a base address of
  /// \p VecPtr. If \p Idx plus the size of \p SubVecVT is out of bounds the
  /// returned pointer is unspecified, but the value returned will be such that
  /// the entire subvector would be within the vector bounds.
  SDValue getVectorSubVecPointer(SelectionDAG &DAG, SDValue VecPtr, EVT VecVT,
                                 EVT SubVecVT, SDValue Index) const;

  /// Method for building the DAG expansion of ISD::[US][MIN|MAX]. This
  /// method accepts integers as its arguments.
  SDValue expandIntMINMAX(SDNode *Node, SelectionDAG &DAG) const;

  /// Method for building the DAG expansion of ISD::[US][ADD|SUB]SAT. This
  /// method accepts integers as its arguments.
  SDValue expandAddSubSat(SDNode *Node, SelectionDAG &DAG) const;

  /// Method for building the DAG expansion of ISD::[US]CMP. This
  /// method accepts integers as its arguments
  SDValue expandCMP(SDNode *Node, SelectionDAG &DAG) const;

  /// Method for building the DAG expansion of ISD::[US]SHLSAT. This
  /// method accepts integers as its arguments.
  SDValue expandShlSat(SDNode *Node, SelectionDAG &DAG) const;

  /// Method for building the DAG expansion of ISD::[U|S]MULFIX[SAT]. This
  /// method accepts integers as its arguments.
  SDValue expandFixedPointMul(SDNode *Node, SelectionDAG &DAG) const;

  /// Method for building the DAG expansion of ISD::[US]DIVFIX[SAT]. This
  /// method accepts integers as its arguments.
  /// Note: This method may fail if the division could not be performed
  /// within the type. Clients must retry with a wider type if this happens.
  SDValue expandFixedPointDiv(unsigned Opcode, const SDLoc &dl,
                              SDValue LHS, SDValue RHS,
                              unsigned Scale, SelectionDAG &DAG) const;

  /// Method for building the DAG expansion of ISD::U(ADD|SUB)O. Expansion
  /// always suceeds and populates the Result and Overflow arguments.
  void expandUADDSUBO(SDNode *Node, SDValue &Result, SDValue &Overflow,
                      SelectionDAG &DAG) const;

  /// Method for building the DAG expansion of ISD::S(ADD|SUB)O. Expansion
  /// always suceeds and populates the Result and Overflow arguments.
  void expandSADDSUBO(SDNode *Node, SDValue &Result, SDValue &Overflow,
                      SelectionDAG &DAG) const;

  /// Method for building the DAG expansion of ISD::[US]MULO. Returns whether
  /// expansion was successful and populates the Result and Overflow arguments.
  bool expandMULO(SDNode *Node, SDValue &Result, SDValue &Overflow,
                  SelectionDAG &DAG) const;

  /// forceExpandWideMUL - Unconditionally expand a MUL into either a libcall or
  /// brute force via a wide multiplication. The expansion works by
  /// attempting to do a multiplication on a wider type twice the size of the
  /// original operands. LL and LH represent the lower and upper halves of the
  /// first operand. RL and RH represent the lower and upper halves of the
  /// second operand. The upper and lower halves of the result are stored in Lo
  /// and Hi.
  void forceExpandWideMUL(SelectionDAG &DAG, const SDLoc &dl, bool Signed,
                          EVT WideVT, const SDValue LL, const SDValue LH,
                          const SDValue RL, const SDValue RH, SDValue &Lo,
                          SDValue &Hi) const;

  /// Same as above, but creates the upper halves of each operand by
  /// sign/zero-extending the operands.
  void forceExpandWideMUL(SelectionDAG &DAG, const SDLoc &dl, bool Signed,
                          const SDValue LHS, const SDValue RHS, SDValue &Lo,
                          SDValue &Hi) const;

  /// Expand a VECREDUCE_* into an explicit calculation. If Count is specified,
  /// only the first Count elements of the vector are used.
  SDValue expandVecReduce(SDNode *Node, SelectionDAG &DAG) const;

  /// Expand a VECREDUCE_SEQ_* into an explicit ordered calculation.
  SDValue expandVecReduceSeq(SDNode *Node, SelectionDAG &DAG) const;

  /// Expand an SREM or UREM using SDIV/UDIV or SDIVREM/UDIVREM, if legal.
  /// Returns true if the expansion was successful.
  bool expandREM(SDNode *Node, SDValue &Result, SelectionDAG &DAG) const;

  /// Method for building the DAG expansion of ISD::VECTOR_SPLICE. This
  /// method accepts vectors as its arguments.
  SDValue expandVectorSplice(SDNode *Node, SelectionDAG &DAG) const;

  /// Expand a vector VECTOR_COMPRESS into a sequence of extract element, store
  /// temporarily, advance store position, before re-loading the final vector.
  SDValue expandVECTOR_COMPRESS(SDNode *Node, SelectionDAG &DAG) const;

  /// Legalize a SETCC or VP_SETCC with given LHS and RHS and condition code CC
  /// on the current target. A VP_SETCC will additionally be given a Mask
  /// and/or EVL not equal to SDValue().
  ///
  /// If the SETCC has been legalized using AND / OR, then the legalized node
  /// will be stored in LHS. RHS and CC will be set to SDValue(). NeedInvert
  /// will be set to false. This will also hold if the VP_SETCC has been
  /// legalized using VP_AND / VP_OR.
  ///
  /// If the SETCC / VP_SETCC has been legalized by using
  /// getSetCCSwappedOperands(), then the values of LHS and RHS will be
  /// swapped, CC will be set to the new condition, and NeedInvert will be set
  /// to false.
  ///
  /// If the SETCC / VP_SETCC has been legalized using the inverse condcode,
  /// then LHS and RHS will be unchanged, CC will set to the inverted condcode,
  /// and NeedInvert will be set to true. The caller must invert the result of
  /// the SETCC with SelectionDAG::getLogicalNOT() or take equivalent action to
  /// swap the effect of a true/false result.
  ///
  /// \returns true if the SETCC / VP_SETCC has been legalized, false if it
  /// hasn't.
  bool LegalizeSetCCCondCode(SelectionDAG &DAG, EVT VT, SDValue &LHS,
                             SDValue &RHS, SDValue &CC, SDValue Mask,
                             SDValue EVL, bool &NeedInvert, const SDLoc &dl,
                             SDValue &Chain, bool IsSignaling = false) const;

  //===--------------------------------------------------------------------===//
  // Instruction Emitting Hooks
  //

  /// This method should be implemented by targets that mark instructions with
  /// the 'usesCustomInserter' flag.  These instructions are special in various
  /// ways, which require special support to insert.  The specified MachineInstr
  /// is created but not inserted into any basic blocks, and this method is
  /// called to expand it into a sequence of instructions, potentially also
  /// creating new basic blocks and control flow.
  /// As long as the returned basic block is different (i.e., we created a new
  /// one), the custom inserter is free to modify the rest of \p MBB.
  virtual MachineBasicBlock *
  EmitInstrWithCustomInserter(MachineInstr &MI, MachineBasicBlock *MBB) const;

  /// This method should be implemented by targets that mark instructions with
  /// the 'hasPostISelHook' flag. These instructions must be adjusted after
  /// instruction selection by target hooks.  e.g. To fill in optional defs for
  /// ARM 's' setting instructions.
  virtual void AdjustInstrPostInstrSelection(MachineInstr &MI,
                                             SDNode *Node) const;

  /// If this function returns true, SelectionDAGBuilder emits a
  /// LOAD_STACK_GUARD node when it is lowering Intrinsic::stackprotector.
  virtual bool useLoadStackGuardNode() const {
    return false;
  }

  virtual SDValue emitStackGuardXorFP(SelectionDAG &DAG, SDValue Val,
                                      const SDLoc &DL) const {
    llvm_unreachable("not implemented for this target");
  }

  /// Lower TLS global address SDNode for target independent emulated TLS model.
  virtual SDValue LowerToTLSEmulatedModel(const GlobalAddressSDNode *GA,
                                          SelectionDAG &DAG) const;

  /// Expands target specific indirect branch for the case of JumpTable
  /// expansion.
  virtual SDValue expandIndirectJTBranch(const SDLoc &dl, SDValue Value,
                                         SDValue Addr, int JTI,
                                         SelectionDAG &DAG) const;

  // seteq(x, 0) -> truncate(srl(ctlz(zext(x)), log2(#bits)))
  // If we're comparing for equality to zero and isCtlzFast is true, expose the
  // fact that this can be implemented as a ctlz/srl pair, so that the dag
  // combiner can fold the new nodes.
  SDValue lowerCmpEqZeroToCtlzSrl(SDValue Op, SelectionDAG &DAG) const;

  // Return true if `X & Y eq/ne 0` is preferable to `X & Y ne/eq Y`
  virtual bool isXAndYEqZeroPreferableToXAndYEqY(ISD::CondCode, EVT) const {
    return true;
  }

private:
  SDValue foldSetCCWithAnd(EVT VT, SDValue N0, SDValue N1, ISD::CondCode Cond,
                           const SDLoc &DL, DAGCombinerInfo &DCI) const;
  SDValue foldSetCCWithBinOp(EVT VT, SDValue N0, SDValue N1, ISD::CondCode Cond,
                             const SDLoc &DL, DAGCombinerInfo &DCI) const;

  SDValue optimizeSetCCOfSignedTruncationCheck(EVT SCCVT, SDValue N0,
                                               SDValue N1, ISD::CondCode Cond,
                                               DAGCombinerInfo &DCI,
                                               const SDLoc &DL) const;

  // (X & (C l>>/<< Y)) ==/!= 0  -->  ((X <</l>> Y) & C) ==/!= 0
  SDValue optimizeSetCCByHoistingAndByConstFromLogicalShift(
      EVT SCCVT, SDValue N0, SDValue N1C, ISD::CondCode Cond,
      DAGCombinerInfo &DCI, const SDLoc &DL) const;

  SDValue prepareUREMEqFold(EVT SETCCVT, SDValue REMNode,
                            SDValue CompTargetNode, ISD::CondCode Cond,
                            DAGCombinerInfo &DCI, const SDLoc &DL,
                            SmallVectorImpl<SDNode *> &Created) const;
  SDValue buildUREMEqFold(EVT SETCCVT, SDValue REMNode, SDValue CompTargetNode,
                          ISD::CondCode Cond, DAGCombinerInfo &DCI,
                          const SDLoc &DL) const;

  SDValue prepareSREMEqFold(EVT SETCCVT, SDValue REMNode,
                            SDValue CompTargetNode, ISD::CondCode Cond,
                            DAGCombinerInfo &DCI, const SDLoc &DL,
                            SmallVectorImpl<SDNode *> &Created) const;
  SDValue buildSREMEqFold(EVT SETCCVT, SDValue REMNode, SDValue CompTargetNode,
                          ISD::CondCode Cond, DAGCombinerInfo &DCI,
                          const SDLoc &DL) const;
};

/// Given an LLVM IR type and return type attributes, compute the return value
/// EVTs and flags, and optionally also the offsets, if the return value is
/// being lowered to memory.
void GetReturnInfo(CallingConv::ID CC, Type *ReturnType, AttributeList attr,
                   SmallVectorImpl<ISD::OutputArg> &Outs,
                   const TargetLowering &TLI, const DataLayout &DL);

} // end namespace llvm

#endif // LLVM_CODEGEN_TARGETLOWERING_H

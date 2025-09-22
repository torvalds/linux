//===- llvm/CodeGen/GlobalISel/GIMatchTableExecutor.h -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file This file declares the GIMatchTableExecutor API, the opcodes supported
/// by the match table, and some associated data structures used by the
/// executor's implementation (see `GIMatchTableExecutorImpl.h`).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_GIMATCHTABLEEXECUTOR_H
#define LLVM_CODEGEN_GLOBALISEL_GIMATCHTABLEEXECUTOR_H

#include "llvm/ADT/Bitset.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGenTypes/LowLevelType.h"
#include "llvm/IR/Function.h"
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <vector>

namespace llvm {

class BlockFrequencyInfo;
class CodeGenCoverage;
class MachineBasicBlock;
class ProfileSummaryInfo;
class APInt;
class APFloat;
class GISelKnownBits;
class MachineInstr;
class MachineIRBuilder;
class MachineInstrBuilder;
class MachineFunction;
class MachineOperand;
class MachineRegisterInfo;
class RegisterBankInfo;
class TargetInstrInfo;
class TargetRegisterInfo;

enum {
  GICXXPred_Invalid = 0,
  GICXXCustomAction_Invalid = 0,
};

/// The MatchTable is encoded as an array of bytes.
/// Thus, opcodes are expected to be <255.
///
/// Operands can be variable-sized, their size is always after their name
/// in the docs, e.g. "Foo(4)" means that "Foo" takes 4 entries in the table,
/// so 4 bytes. "Foo()"
///
/// As a general rule of thumb:
///   - Instruction & Operand IDs are ULEB128
///   - LLT IDs are 1 byte
///   - Predicates and target opcodes, register and register class IDs are 2
///     bytes.
///   - Indexes into the table are 4 bytes.
///   - Inline constants are 8 bytes
///
/// Design notes:
///   - Inst/Op IDs have to be LEB128 because some targets generate
///     extremely long patterns which need more than 255 temporaries.
///     We could just use 2 bytes everytime, but then some targets like
///     X86/AMDGPU that have no need for it will pay the price all the time.
enum {
  /// Begin a try-block to attempt a match and jump to OnFail if it is
  /// unsuccessful.
  /// - OnFail(4) - The MatchTable entry at which to resume if the match fails.
  ///
  /// FIXME: This ought to take an argument indicating the number of try-blocks
  ///        to exit on failure. It's usually one but the last match attempt of
  ///        a block will need more. The (implemented) alternative is to tack a
  ///        GIM_Reject on the end of each try-block which is simpler but
  ///        requires an extra opcode and iteration in the interpreter on each
  ///        failed match.
  GIM_Try,

  /// Switch over the opcode on the specified instruction
  /// - InsnID(ULEB128) - Instruction ID
  /// - LowerBound(2) - numerically minimum opcode supported
  /// - UpperBound(2) - numerically maximum + 1 opcode supported
  /// - Default(4) - failure jump target
  /// - JumpTable(4)... - (UpperBound - LowerBound) (at least 2) jump targets
  GIM_SwitchOpcode,

  /// Switch over the LLT on the specified instruction operand
  /// - InsnID(ULEB128) - Instruction ID
  /// - OpIdx(ULEB128) - Operand index
  /// - LowerBound(2) - numerically minimum Type ID supported
  /// - UpperBound(2) - numerically maximum + 1 Type ID supported
  /// - Default(4) - failure jump target
  /// - JumpTable(4)... - (UpperBound - LowerBound) (at least 2) jump targets
  GIM_SwitchType,

  /// Record the specified instruction.
  /// The IgnoreCopies variant ignores COPY instructions.
  /// - NewInsnID(ULEB128) - Instruction ID to define
  /// - InsnID(ULEB128) - Instruction ID
  /// - OpIdx(ULEB128) - Operand index
  GIM_RecordInsn,
  GIM_RecordInsnIgnoreCopies,

  /// Check the feature bits
  ///   Feature(2) - Expected features
  GIM_CheckFeatures,

  /// Check the opcode on the specified instruction
  /// - InsnID(ULEB128) - Instruction ID
  /// - Opc(2) - Expected opcode
  GIM_CheckOpcode,

  /// Check the opcode on the specified instruction, checking 2 acceptable
  /// alternatives.
  /// - InsnID(ULEB128) - Instruction ID
  /// - Opc(2) - Expected opcode
  /// - Opc(2) - Alternative expected opcode
  GIM_CheckOpcodeIsEither,

  /// Check the instruction has the right number of operands
  /// - InsnID(ULEB128) - Instruction ID
  /// - Ops(ULEB128) - Expected number of operands
  GIM_CheckNumOperands,

  /// Check an immediate predicate on the specified instruction
  /// - InsnID(ULEB128) - Instruction ID
  /// - Pred(2) - The predicate to test
  GIM_CheckI64ImmPredicate,
  /// Check an immediate predicate on the specified instruction via an APInt.
  /// - InsnID(ULEB128) - Instruction ID
  /// - Pred(2) - The predicate to test
  GIM_CheckAPIntImmPredicate,
  /// Check a floating point immediate predicate on the specified instruction.
  /// - InsnID(ULEB128) - Instruction ID
  /// - Pred(2) - The predicate to test
  GIM_CheckAPFloatImmPredicate,
  /// Check an immediate predicate on the specified instruction
  /// - InsnID(ULEB128) - Instruction ID
  /// - OpIdx(ULEB128) - Operand index
  /// - Pred(2) - The predicate to test
  GIM_CheckImmOperandPredicate,

  /// Check a memory operation has the specified atomic ordering.
  /// - InsnID(ULEB128) - Instruction ID
  /// - Ordering(ULEB128) - The AtomicOrdering value
  GIM_CheckAtomicOrdering,
  GIM_CheckAtomicOrderingOrStrongerThan,
  GIM_CheckAtomicOrderingWeakerThan,

  /// Check the size of the memory access for the given machine memory operand.
  /// - InsnID(ULEB128) - Instruction ID
  /// - MMOIdx(ULEB128) - MMO index
  /// - Size(4) - The size in bytes of the memory access
  GIM_CheckMemorySizeEqualTo,

  /// Check the address space of the memory access for the given machine memory
  /// operand.
  /// - InsnID(ULEB128) - Instruction ID
  /// - MMOIdx(ULEB128) - MMO index
  /// - NumAddrSpace(1) - Number of valid address spaces
  /// - AddrSpaceN(ULEB128) - An allowed space of the memory access
  /// - AddrSpaceN+1 ...
  GIM_CheckMemoryAddressSpace,

  /// Check the minimum alignment of the memory access for the given machine
  /// memory operand.
  /// - InsnID(ULEB128) - Instruction ID
  /// - MMOIdx(ULEB128) - MMO index
  /// - MinAlign(1) - Minimum acceptable alignment
  GIM_CheckMemoryAlignment,

  /// Check the size of the memory access for the given machine memory operand
  /// against the size of an operand.
  /// - InsnID(ULEB128) - Instruction ID
  /// - MMOIdx(ULEB128) - MMO index
  /// - OpIdx(ULEB128) - The operand index to compare the MMO against
  GIM_CheckMemorySizeEqualToLLT,
  GIM_CheckMemorySizeLessThanLLT,
  GIM_CheckMemorySizeGreaterThanLLT,

  /// Check if this is a vector that can be treated as a vector splat
  /// constant. This is valid for both G_BUILD_VECTOR as well as
  /// G_BUILD_VECTOR_TRUNC. For AllOnes refers to individual bits, so a -1
  /// element.
  /// - InsnID(ULEB128) - Instruction ID
  GIM_CheckIsBuildVectorAllOnes,
  GIM_CheckIsBuildVectorAllZeros,

  /// Check a trivial predicate which takes no arguments.
  /// This can be used by executors to implement custom flags that don't fit in
  /// target features.
  /// - Pred(2) - Predicate ID to check.
  GIM_CheckSimplePredicate,

  /// Check a generic C++ instruction predicate
  /// - InsnID(ULEB128) - Instruction ID
  /// - PredicateID(2) - The ID of the predicate function to call
  GIM_CheckCxxInsnPredicate,

  /// Check if there's no use of the first result.
  /// - InsnID(ULEB128) - Instruction ID
  GIM_CheckHasNoUse,

  /// Check if there's one use of the first result.
  /// - InsnID(ULEB128) - Instruction ID
  GIM_CheckHasOneUse,

  /// Check the type for the specified operand
  /// - InsnID(ULEB128) - Instruction ID
  /// - OpIdx(ULEB128) - Operand index
  /// - Ty(1) - Expected type
  GIM_CheckType,
  /// GIM_CheckType but InsnID is omitted and defaults to zero.
  GIM_RootCheckType,

  /// Check the type of a pointer to any address space.
  /// - InsnID(ULEB128) - Instruction ID
  /// - OpIdx(ULEB128) - Operand index
  /// - SizeInBits(ULEB128) - The size of the pointer value in bits.
  GIM_CheckPointerToAny,

  /// Check the register bank for the specified operand
  /// - InsnID(ULEB128) - Instruction ID
  /// - OpIdx(ULEB128) - Operand index
  /// - RC(2) - Expected register bank (specified as a register class)
  GIM_CheckRegBankForClass,
  /// GIM_CheckRegBankForClass but InsnID is omitted and defaults to zero.
  GIM_RootCheckRegBankForClass,

  /// Check the operand matches a complex predicate
  /// - InsnID(ULEB128) - Instruction ID
  /// - OpIdx(ULEB128) - Operand index
  /// - RendererID(2) - The renderer to hold the result
  /// - Pred(2) - Complex predicate ID
  GIM_CheckComplexPattern,

  /// Check the operand is a specific integer
  /// - InsnID(ULEB128) - Instruction ID
  /// - OpIdx(ULEB128) - Operand index
  /// - Val(8) Expected integer
  GIM_CheckConstantInt,

  /// Check the operand is a specific 8-bit signed integer
  /// - InsnID(ULEB128) - Instruction ID
  /// - OpIdx(ULEB128) - Operand index
  /// - Val(1) Expected integer
  GIM_CheckConstantInt8,

  /// Check the operand is a specific literal integer (i.e. MO.isImm() or
  /// MO.isCImm() is true).
  /// - InsnID(ULEB128) - Instruction ID
  /// - OpIdx(ULEB128) - Operand index
  /// - Val(8) - Expected integer
  GIM_CheckLiteralInt,

  /// Check the operand is a specific intrinsic ID
  /// - InsnID(ULEB128) - Instruction ID
  /// - OpIdx(ULEB128) - Operand index
  /// - IID(2) - Expected Intrinsic ID
  GIM_CheckIntrinsicID,

  /// Check the operand is a specific predicate
  /// - InsnID(ULEB128) - Instruction ID
  /// - OpIdx(ULEB128) - Operand index
  /// - Pred(2) - Expected predicate
  GIM_CheckCmpPredicate,

  /// Check the specified operand is an MBB
  /// - InsnID(ULEB128) - Instruction ID
  /// - OpIdx(ULEB128) - Operand index
  GIM_CheckIsMBB,

  /// Check the specified operand is an Imm
  /// - InsnID(ULEB128) - Instruction ID
  /// - OpIdx(ULEB128) - Operand index
  GIM_CheckIsImm,

  /// Checks if the matched instructions numbered [1, 1+N) can
  /// be folded into the root (inst 0).
  /// - Num(1)
  GIM_CheckIsSafeToFold,

  /// Check the specified operands are identical.
  /// The IgnoreCopies variant looks through COPY instructions before
  /// comparing the operands.
  /// - InsnID(ULEB128) - Instruction ID
  /// - OpIdx(ULEB128) - Operand index
  /// - OtherInsnID(ULEB128) - Other instruction ID
  /// - OtherOpIdx(ULEB128) - Other operand index
  GIM_CheckIsSameOperand,
  GIM_CheckIsSameOperandIgnoreCopies,

  /// Check we can replace all uses of a register with another.
  /// - OldInsnID(ULEB128)
  /// - OldOpIdx(ULEB128)
  /// - NewInsnID(ULEB128)
  /// - NewOpIdx(ULEB128)
  GIM_CheckCanReplaceReg,

  /// Check that a matched instruction has, or doesn't have a MIFlag.
  ///
  /// - InsnID(ULEB128) - Instruction to check.
  /// - Flags(4) - (can be one or more flags OR'd together)
  GIM_MIFlags,
  GIM_MIFlagsNot,

  /// Predicates with 'let PredicateCodeUsesOperands = 1' need to examine some
  /// named operands that will be recorded in RecordedOperands. Names of these
  /// operands are referenced in predicate argument list. Emitter determines
  /// StoreIdx(corresponds to the order in which names appear in argument list).
  /// - InsnID(ULEB128) - Instruction ID
  /// - OpIdx(ULEB128) - Operand index
  /// - StoreIdx(ULEB128) - Store location in RecordedOperands.
  GIM_RecordNamedOperand,

  /// Records an operand's register type into the set of temporary types.
  /// - InsnID(ULEB128) - Instruction ID
  /// - OpIdx(ULEB128) - Operand index
  /// - TempTypeIdx(1) - Temp Type Index, always negative.
  GIM_RecordRegType,

  /// Fail the current try-block, or completely fail to match if there is no
  /// current try-block.
  GIM_Reject,

  //=== Renderers ===

  /// Mutate an instruction
  /// - NewInsnID(ULEB128) - Instruction ID to define
  /// - OldInsnID(ULEB128) - Instruction ID to mutate
  /// - NewOpcode(2) - The new opcode to use
  GIR_MutateOpcode,

  /// Build a new instruction
  /// - InsnID(ULEB128) - Instruction ID to define
  /// - Opcode(2) - The new opcode to use
  GIR_BuildMI,
  /// GIR_BuildMI but InsnID is omitted and defaults to zero.
  GIR_BuildRootMI,

  /// Builds a constant and stores its result in a TempReg.
  /// - TempRegID(ULEB128) - Temp Register to define.
  /// - Imm(8) - The immediate to add
  GIR_BuildConstant,

  /// Copy an operand to the specified instruction
  /// - NewInsnID(ULEB128) - Instruction ID to modify
  /// - OldInsnID(ULEB128) - Instruction ID to copy from
  /// - OpIdx(ULEB128) - The operand to copy
  GIR_Copy,
  /// GIR_Copy but with both New/OldInsnIDs omitted and defaulting to zero.
  GIR_RootToRootCopy,

  /// Copy an operand to the specified instruction or add a zero register if the
  /// operand is a zero immediate.
  /// - NewInsnID(ULEB128) - Instruction ID to modify
  /// - OldInsnID(ULEB128) - Instruction ID to copy from
  /// - OpIdx(ULEB128) - The operand to copy
  /// - ZeroReg(2) - The zero register to use
  GIR_CopyOrAddZeroReg,
  /// Copy an operand to the specified instruction
  /// - NewInsnID(ULEB128) - Instruction ID to modify
  /// - OldInsnID(ULEB128) - Instruction ID to copy from
  /// - OpIdx(ULEB128) - The operand to copy
  /// - SubRegIdx(2) - The subregister to copy
  GIR_CopySubReg,

  /// Add an implicit register def to the specified instruction
  /// - InsnID(ULEB128) - Instruction ID to modify
  /// - RegNum(2) - The register to add
  /// - Flags(2) - Register Flags
  GIR_AddImplicitDef,
  /// Add an implicit register use to the specified instruction
  /// - InsnID(ULEB128) - Instruction ID to modify
  /// - RegNum(2) - The register to add
  GIR_AddImplicitUse,
  /// Add an register to the specified instruction
  /// - InsnID(ULEB128) - Instruction ID to modify
  /// - RegNum(2) - The register to add
  /// - Flags(2) - Register Flags
  GIR_AddRegister,

  /// Adds an intrinsic ID to the specified instruction.
  /// - InsnID(ULEB128) - Instruction ID to modify
  /// - IID(2) - Intrinsic ID
  GIR_AddIntrinsicID,

  /// Marks the implicit def of a register as dead.
  /// - InsnID(ULEB128) - Instruction ID to modify
  /// - OpIdx(ULEB128) - The implicit def operand index
  ///
  /// OpIdx starts at 0 for the first implicit def.
  GIR_SetImplicitDefDead,

  /// Set or unset a MIFlag on an instruction.
  ///
  /// - InsnID(ULEB128)  - Instruction to modify.
  /// - Flags(4) - (can be one or more flags OR'd together)
  GIR_SetMIFlags,
  GIR_UnsetMIFlags,

  /// Copy the MIFlags of a matched instruction into an
  /// output instruction. The flags are OR'd together.
  ///
  /// - InsnID(ULEB128)     - Instruction to modify.
  /// - OldInsnID(ULEB128)  - Matched instruction to copy flags from.
  GIR_CopyMIFlags,

  /// Add a temporary register to the specified instruction
  /// - InsnID(ULEB128) - Instruction ID to modify
  /// - TempRegID(ULEB128) - The temporary register ID to add
  /// - TempRegFlags(2) - The register flags to set
  GIR_AddTempRegister,

  /// Add a temporary register to the specified instruction without
  /// setting any flags.
  /// - InsnID(ULEB128) - Instruction ID to modify
  /// - TempRegID(ULEB128) - The temporary register ID to add
  GIR_AddSimpleTempRegister,

  /// Add a temporary register to the specified instruction
  /// - InsnID(ULEB128) - Instruction ID to modify
  /// - TempRegID(ULEB128) - The temporary register ID to add
  /// - TempRegFlags(2) - The register flags to set
  /// - SubRegIndex(2) - The subregister index to set
  GIR_AddTempSubRegister,

  /// Add an immediate to the specified instruction
  /// - InsnID(ULEB128) - Instruction ID to modify
  /// - Imm(8) - The immediate to add
  GIR_AddImm,

  /// Add signed 8 bit immediate to the specified instruction
  /// - InsnID(ULEB128) - Instruction ID to modify
  /// - Imm(1) - The immediate to add
  GIR_AddImm8,

  /// Add an CImm to the specified instruction
  /// - InsnID(ULEB128) - Instruction ID to modify
  /// - Ty(1) - Type of the constant immediate.
  /// - Imm(8) - The immediate to add
  GIR_AddCImm,

  /// Render complex operands to the specified instruction
  /// - InsnID(ULEB128) - Instruction ID to modify
  /// - RendererID(2) - The renderer to call
  GIR_ComplexRenderer,
  /// Render sub-operands of complex operands to the specified instruction
  /// - InsnID(ULEB128) - Instruction ID to modify
  /// - RendererID(2) - The renderer to call
  /// - RenderOpID(ULEB128) - The suboperand to render.
  GIR_ComplexSubOperandRenderer,
  /// Render subregisters of suboperands of complex operands to the
  /// specified instruction
  /// - InsnID(ULEB128) - Instruction ID to modify
  /// - RendererID(2) - The renderer to call
  /// - RenderOpID(ULEB128) - The suboperand to render
  /// - SubRegIdx(2) - The subregister to extract
  GIR_ComplexSubOperandSubRegRenderer,

  /// Render operands to the specified instruction using a custom function
  /// - InsnID(ULEB128) - Instruction ID to modify
  /// - OldInsnID(ULEB128) - Instruction ID to get the matched operand from
  /// - RendererFnID(2) - Custom renderer function to call
  GIR_CustomRenderer,

  /// Calls a C++ function that concludes the current match.
  /// The C++ function is free to return false and reject the match, or
  /// return true and mutate the instruction(s) (or do nothing, even).
  /// - FnID(2) - The function to call.
  GIR_DoneWithCustomAction,

  /// Render operands to the specified instruction using a custom function,
  /// reading from a specific operand.
  /// - InsnID(ULEB128) - Instruction ID to modify
  /// - OldInsnID(ULEB128) - Instruction ID to get the matched operand from
  /// - OpIdx(ULEB128) - Operand index in OldInsnID the render function should
  /// read
  /// from..
  /// - RendererFnID(2) - Custom renderer function to call
  GIR_CustomOperandRenderer,

  /// Render a G_CONSTANT operator as a sign-extended immediate.
  /// - NewInsnID(ULEB128) - Instruction ID to modify
  /// - OldInsnID(ULEB128) - Instruction ID to copy from
  /// The operand index is implicitly 1.
  GIR_CopyConstantAsSImm,

  /// Render a G_FCONSTANT operator as a sign-extended immediate.
  /// - NewInsnID(ULEB128) - Instruction ID to modify
  /// - OldInsnID(ULEB128) - Instruction ID to copy from
  /// The operand index is implicitly 1.
  GIR_CopyFConstantAsFPImm,

  /// Constrain an instruction operand to a register class.
  /// - InsnID(ULEB128) - Instruction ID to modify
  /// - OpIdx(ULEB128) - Operand index
  /// - RCEnum(2) - Register class enumeration value
  GIR_ConstrainOperandRC,

  /// Constrain an instructions operands according to the instruction
  /// description.
  /// - InsnID(ULEB128) - Instruction ID to modify
  GIR_ConstrainSelectedInstOperands,
  /// GIR_ConstrainSelectedInstOperands but InsnID is omitted and defaults to
  /// zero.
  GIR_RootConstrainSelectedInstOperands,

  /// Merge all memory operands into instruction.
  /// - InsnID(ULEB128) - Instruction ID to modify
  /// - NumInsnID(1) - Number of instruction IDs following this argument
  /// - MergeInsnID(ULEB128)... - One or more Instruction ID to merge into the
  /// result.
  GIR_MergeMemOperands,

  /// Erase from parent.
  /// - InsnID(ULEB128) - Instruction ID to erase
  GIR_EraseFromParent,

  /// Combines both a GIR_EraseFromParent 0 + GIR_Done
  GIR_EraseRootFromParent_Done,

  /// Create a new temporary register that's not constrained.
  /// - TempRegID(ULEB128) - The temporary register ID to initialize.
  /// - Ty(1) - Expected type
  GIR_MakeTempReg,

  /// Replaces all references to a register from an instruction
  /// with another register from another instruction.
  /// - OldInsnID(ULEB128)
  /// - OldOpIdx(ULEB128)
  /// - NewInsnID(ULEB128)
  /// - NewOpIdx(ULEB128)
  GIR_ReplaceReg,

  /// Replaces all references to a register with a temporary register.
  /// - OldInsnID(ULEB128)
  /// - OldOpIdx(ULEB128)
  /// - TempRegIdx(ULEB128)
  GIR_ReplaceRegWithTempReg,

  /// A successful emission
  GIR_Done,

  /// Increment the rule coverage counter.
  /// - RuleID(4) - The ID of the rule that was covered.
  GIR_Coverage,

  /// Keeping track of the number of the GI opcodes. Must be the last entry.
  GIU_NumOpcodes,
};

/// Provides the logic to execute GlobalISel match tables, which are used by the
/// instruction selector and instruction combiners as their engine to match and
/// apply MIR patterns.
class GIMatchTableExecutor {
public:
  virtual ~GIMatchTableExecutor() = default;

  CodeGenCoverage *CoverageInfo = nullptr;
  GISelKnownBits *KB = nullptr;
  MachineFunction *MF = nullptr;
  ProfileSummaryInfo *PSI = nullptr;
  BlockFrequencyInfo *BFI = nullptr;
  // For some predicates, we need to track the current MBB.
  MachineBasicBlock *CurMBB = nullptr;

  virtual void setupGeneratedPerFunctionState(MachineFunction &MF) = 0;

  /// Setup per-MF executor state.
  virtual void setupMF(MachineFunction &mf, GISelKnownBits *kb,
                       CodeGenCoverage *covinfo = nullptr,
                       ProfileSummaryInfo *psi = nullptr,
                       BlockFrequencyInfo *bfi = nullptr) {
    CoverageInfo = covinfo;
    KB = kb;
    MF = &mf;
    PSI = psi;
    BFI = bfi;
    CurMBB = nullptr;
    setupGeneratedPerFunctionState(mf);
  }

protected:
  using ComplexRendererFns =
      std::optional<SmallVector<std::function<void(MachineInstrBuilder &)>, 4>>;
  using RecordedMIVector = SmallVector<MachineInstr *, 4>;
  using NewMIVector = SmallVector<MachineInstrBuilder, 4>;

  struct MatcherState {
    std::vector<ComplexRendererFns::value_type> Renderers;
    RecordedMIVector MIs;
    DenseMap<unsigned, unsigned> TempRegisters;
    /// Named operands that predicate with 'let PredicateCodeUsesOperands = 1'
    /// referenced in its argument list. Operands are inserted at index set by
    /// emitter, it corresponds to the order in which names appear in argument
    /// list. Currently such predicates don't have more then 3 arguments.
    std::array<const MachineOperand *, 3> RecordedOperands;

    /// Types extracted from an instruction's operand.
    /// Whenever a type index is negative, we look here instead.
    SmallVector<LLT, 4> RecordedTypes;

    MatcherState(unsigned MaxRenderers);
  };

  bool shouldOptForSize(const MachineFunction *MF) const {
    const auto &F = MF->getFunction();
    return F.hasOptSize() || F.hasMinSize() ||
           (PSI && BFI && CurMBB && llvm::shouldOptForSize(*CurMBB, PSI, BFI));
  }

public:
  template <class PredicateBitset, class ComplexMatcherMemFn,
            class CustomRendererFn>
  struct ExecInfoTy {
    ExecInfoTy(const LLT *TypeObjects, size_t NumTypeObjects,
               const PredicateBitset *FeatureBitsets,
               const ComplexMatcherMemFn *ComplexPredicates,
               const CustomRendererFn *CustomRenderers)
        : TypeObjects(TypeObjects), FeatureBitsets(FeatureBitsets),
          ComplexPredicates(ComplexPredicates),
          CustomRenderers(CustomRenderers) {

      for (size_t I = 0; I < NumTypeObjects; ++I)
        TypeIDMap[TypeObjects[I]] = I;
    }
    const LLT *TypeObjects;
    const PredicateBitset *FeatureBitsets;
    const ComplexMatcherMemFn *ComplexPredicates;
    const CustomRendererFn *CustomRenderers;

    SmallDenseMap<LLT, unsigned, 64> TypeIDMap;
  };

protected:
  GIMatchTableExecutor();

  /// Execute a given matcher table and return true if the match was successful
  /// and false otherwise.
  template <class TgtExecutor, class PredicateBitset, class ComplexMatcherMemFn,
            class CustomRendererFn>
  bool executeMatchTable(TgtExecutor &Exec, MatcherState &State,
                         const ExecInfoTy<PredicateBitset, ComplexMatcherMemFn,
                                          CustomRendererFn> &ExecInfo,
                         MachineIRBuilder &Builder, const uint8_t *MatchTable,
                         const TargetInstrInfo &TII, MachineRegisterInfo &MRI,
                         const TargetRegisterInfo &TRI,
                         const RegisterBankInfo &RBI,
                         const PredicateBitset &AvailableFeatures,
                         CodeGenCoverage *CoverageInfo) const;

  virtual const uint8_t *getMatchTable() const {
    llvm_unreachable("Should have been overridden by tablegen if used");
  }

  virtual bool testImmPredicate_I64(unsigned, int64_t) const {
    llvm_unreachable(
        "Subclasses must override this with a tablegen-erated function");
  }
  virtual bool testImmPredicate_APInt(unsigned, const APInt &) const {
    llvm_unreachable(
        "Subclasses must override this with a tablegen-erated function");
  }
  virtual bool testImmPredicate_APFloat(unsigned, const APFloat &) const {
    llvm_unreachable(
        "Subclasses must override this with a tablegen-erated function");
  }
  virtual bool testMIPredicate_MI(unsigned, const MachineInstr &,
                                  const MatcherState &State) const {
    llvm_unreachable(
        "Subclasses must override this with a tablegen-erated function");
  }

  virtual bool testSimplePredicate(unsigned) const {
    llvm_unreachable("Subclass does not implement testSimplePredicate!");
  }

  virtual bool runCustomAction(unsigned, const MatcherState &State,
                               NewMIVector &OutMIs) const {
    llvm_unreachable("Subclass does not implement runCustomAction!");
  }

  bool isOperandImmEqual(const MachineOperand &MO, int64_t Value,
                         const MachineRegisterInfo &MRI,
                         bool Splat = false) const;

  /// Return true if the specified operand is a G_PTR_ADD with a G_CONSTANT on
  /// the right-hand side. GlobalISel's separation of pointer and integer types
  /// means that we don't need to worry about G_OR with equivalent semantics.
  bool isBaseWithConstantOffset(const MachineOperand &Root,
                                const MachineRegisterInfo &MRI) const;

  /// Return true if MI can obviously be folded into IntoMI.
  /// MI and IntoMI do not need to be in the same basic blocks, but MI must
  /// preceed IntoMI.
  bool isObviouslySafeToFold(MachineInstr &MI, MachineInstr &IntoMI) const;

  template <typename Ty> static Ty readBytesAs(const uint8_t *MatchTable) {
    Ty Ret;
    memcpy(&Ret, MatchTable, sizeof(Ret));
    return Ret;
  }

public:
  // Faster ULEB128 decoder tailored for the Match Table Executor.
  //
  // - Arguments are fixed to avoid mid-function checks.
  // - Unchecked execution, assumes no error.
  // - Fast common case handling (1 byte values).
  LLVM_ATTRIBUTE_ALWAYS_INLINE static uint64_t
  fastDecodeULEB128(const uint8_t *LLVM_ATTRIBUTE_RESTRICT MatchTable,
                    uint64_t &CurrentIdx) {
    uint64_t Value = MatchTable[CurrentIdx++];
    if (LLVM_UNLIKELY(Value >= 128)) {
      Value &= 0x7f;
      unsigned Shift = 7;
      do {
        uint64_t Slice = MatchTable[CurrentIdx] & 0x7f;
        Value += Slice << Shift;
        Shift += 7;
      } while (MatchTable[CurrentIdx++] >= 128);
    }
    return Value;
  }
};

} // end namespace llvm

#endif // LLVM_CODEGEN_GLOBALISEL_GIMATCHTABLEEXECUTOR_H

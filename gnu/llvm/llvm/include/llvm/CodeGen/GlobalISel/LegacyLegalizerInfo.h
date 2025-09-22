//===- llvm/CodeGen/GlobalISel/LegacyLegalizerInfo.h ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// Interface for Targets to specify which operations they can successfully
/// select and how the others should be expanded most efficiently.
/// This implementation has been deprecated for a long time but it still in use
/// in a few places.
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_LEGACYLEGALIZERINFO_H
#define LLVM_CODEGEN_GLOBALISEL_LEGACYLEGALIZERINFO_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGenTypes/LowLevelType.h"
#include <unordered_map>
#include <vector>

namespace llvm {
struct LegalityQuery;

namespace LegacyLegalizeActions {
enum LegacyLegalizeAction : std::uint8_t {
  /// The operation is expected to be selectable directly by the target, and
  /// no transformation is necessary.
  Legal,

  /// The operation should be synthesized from multiple instructions acting on
  /// a narrower scalar base-type. For example a 64-bit add might be
  /// implemented in terms of 32-bit add-with-carry.
  NarrowScalar,

  /// The operation should be implemented in terms of a wider scalar
  /// base-type. For example a <2 x s8> add could be implemented as a <2
  /// x s32> add (ignoring the high bits).
  WidenScalar,

  /// The (vector) operation should be implemented by splitting it into
  /// sub-vectors where the operation is legal. For example a <8 x s64> add
  /// might be implemented as 4 separate <2 x s64> adds.
  FewerElements,

  /// The (vector) operation should be implemented by widening the input
  /// vector and ignoring the lanes added by doing so. For example <2 x i8> is
  /// rarely legal, but you might perform an <8 x i8> and then only look at
  /// the first two results.
  MoreElements,

  /// Perform the operation on a different, but equivalently sized type.
  Bitcast,

  /// The operation itself must be expressed in terms of simpler actions on
  /// this target. E.g. a SREM replaced by an SDIV and subtraction.
  Lower,

  /// The operation should be implemented as a call to some kind of runtime
  /// support library. For example this usually happens on machines that don't
  /// support floating-point operations natively.
  Libcall,

  /// The target wants to do something special with this combination of
  /// operand and type. A callback will be issued when it is needed.
  Custom,

  /// This operation is completely unsupported on the target. A programming
  /// error has occurred.
  Unsupported,

  /// Sentinel value for when no action was found in the specified table.
  NotFound,
};
} // end namespace LegacyLegalizeActions
raw_ostream &operator<<(raw_ostream &OS,
                        LegacyLegalizeActions::LegacyLegalizeAction Action);

/// Legalization is decided based on an instruction's opcode, which type slot
/// we're considering, and what the existing type is. These aspects are gathered
/// together for convenience in the InstrAspect class.
struct InstrAspect {
  unsigned Opcode;
  unsigned Idx = 0;
  LLT Type;

  InstrAspect(unsigned Opcode, LLT Type) : Opcode(Opcode), Type(Type) {}
  InstrAspect(unsigned Opcode, unsigned Idx, LLT Type)
      : Opcode(Opcode), Idx(Idx), Type(Type) {}

  bool operator==(const InstrAspect &RHS) const {
    return Opcode == RHS.Opcode && Idx == RHS.Idx && Type == RHS.Type;
  }
};

/// The result of a query. It either indicates a final answer of Legal or
/// Unsupported or describes an action that must be taken to make an operation
/// more legal.
struct LegacyLegalizeActionStep {
  /// The action to take or the final answer.
  LegacyLegalizeActions::LegacyLegalizeAction Action;
  /// If describing an action, the type index to change. Otherwise zero.
  unsigned TypeIdx;
  /// If describing an action, the new type for TypeIdx. Otherwise LLT{}.
  LLT NewType;

  LegacyLegalizeActionStep(LegacyLegalizeActions::LegacyLegalizeAction Action,
                           unsigned TypeIdx, const LLT NewType)
      : Action(Action), TypeIdx(TypeIdx), NewType(NewType) {}

  bool operator==(const LegacyLegalizeActionStep &RHS) const {
    return std::tie(Action, TypeIdx, NewType) ==
        std::tie(RHS.Action, RHS.TypeIdx, RHS.NewType);
  }
};


class LegacyLegalizerInfo {
public:
  using SizeAndAction =
      std::pair<uint16_t, LegacyLegalizeActions::LegacyLegalizeAction>;
  using SizeAndActionsVec = std::vector<SizeAndAction>;
  using SizeChangeStrategy =
      std::function<SizeAndActionsVec(const SizeAndActionsVec &v)>;

  LegacyLegalizerInfo();

  static bool needsLegalizingToDifferentSize(
      const LegacyLegalizeActions::LegacyLegalizeAction Action) {
    using namespace LegacyLegalizeActions;
    switch (Action) {
    case NarrowScalar:
    case WidenScalar:
    case FewerElements:
    case MoreElements:
    case Unsupported:
      return true;
    default:
      return false;
    }
  }

  /// Compute any ancillary tables needed to quickly decide how an operation
  /// should be handled. This must be called after all "set*Action"methods but
  /// before any query is made or incorrect results may be returned.
  void computeTables();

  /// More friendly way to set an action for common types that have an LLT
  /// representation.
  /// The LegacyLegalizeAction must be one for which
  /// NeedsLegalizingToDifferentSize returns false.
  void setAction(const InstrAspect &Aspect,
                 LegacyLegalizeActions::LegacyLegalizeAction Action) {
    assert(!needsLegalizingToDifferentSize(Action));
    TablesInitialized = false;
    const unsigned OpcodeIdx = Aspect.Opcode - FirstOp;
    if (SpecifiedActions[OpcodeIdx].size() <= Aspect.Idx)
      SpecifiedActions[OpcodeIdx].resize(Aspect.Idx + 1);
    SpecifiedActions[OpcodeIdx][Aspect.Idx][Aspect.Type] = Action;
  }

  /// The setAction calls record the non-size-changing legalization actions
  /// to take on specificly-sized types. The SizeChangeStrategy defines what
  /// to do when the size of the type needs to be changed to reach a legally
  /// sized type (i.e., one that was defined through a setAction call).
  /// e.g.
  /// setAction ({G_ADD, 0, LLT::scalar(32)}, Legal);
  /// setLegalizeScalarToDifferentSizeStrategy(
  ///   G_ADD, 0, widenToLargerTypesAndNarrowToLargest);
  /// will end up defining getAction({G_ADD, 0, T}) to return the following
  /// actions for different scalar types T:
  ///  LLT::scalar(1)..LLT::scalar(31): {WidenScalar, 0, LLT::scalar(32)}
  ///  LLT::scalar(32):                 {Legal, 0, LLT::scalar(32)}
  ///  LLT::scalar(33)..:               {NarrowScalar, 0, LLT::scalar(32)}
  ///
  /// If no SizeChangeAction gets defined, through this function,
  /// the default is unsupportedForDifferentSizes.
  void setLegalizeScalarToDifferentSizeStrategy(const unsigned Opcode,
                                                const unsigned TypeIdx,
                                                SizeChangeStrategy S) {
    const unsigned OpcodeIdx = Opcode - FirstOp;
    if (ScalarSizeChangeStrategies[OpcodeIdx].size() <= TypeIdx)
      ScalarSizeChangeStrategies[OpcodeIdx].resize(TypeIdx + 1);
    ScalarSizeChangeStrategies[OpcodeIdx][TypeIdx] = S;
  }

  /// See also setLegalizeScalarToDifferentSizeStrategy.
  /// This function allows to set the SizeChangeStrategy for vector elements.
  void setLegalizeVectorElementToDifferentSizeStrategy(const unsigned Opcode,
                                                       const unsigned TypeIdx,
                                                       SizeChangeStrategy S) {
    const unsigned OpcodeIdx = Opcode - FirstOp;
    if (VectorElementSizeChangeStrategies[OpcodeIdx].size() <= TypeIdx)
      VectorElementSizeChangeStrategies[OpcodeIdx].resize(TypeIdx + 1);
    VectorElementSizeChangeStrategies[OpcodeIdx][TypeIdx] = S;
  }

  /// A SizeChangeStrategy for the common case where legalization for a
  /// particular operation consists of only supporting a specific set of type
  /// sizes. E.g.
  ///   setAction ({G_DIV, 0, LLT::scalar(32)}, Legal);
  ///   setAction ({G_DIV, 0, LLT::scalar(64)}, Legal);
  ///   setLegalizeScalarToDifferentSizeStrategy(
  ///     G_DIV, 0, unsupportedForDifferentSizes);
  /// will result in getAction({G_DIV, 0, T}) to return Legal for s32 and s64,
  /// and Unsupported for all other scalar types T.
  static SizeAndActionsVec
  unsupportedForDifferentSizes(const SizeAndActionsVec &v) {
    using namespace LegacyLegalizeActions;
    return increaseToLargerTypesAndDecreaseToLargest(v, Unsupported,
                                                     Unsupported);
  }

  /// A SizeChangeStrategy for the common case where legalization for a
  /// particular operation consists of widening the type to a large legal type,
  /// unless there is no such type and then instead it should be narrowed to the
  /// largest legal type.
  static SizeAndActionsVec
  widenToLargerTypesAndNarrowToLargest(const SizeAndActionsVec &v) {
    using namespace LegacyLegalizeActions;
    assert(v.size() > 0 &&
           "At least one size that can be legalized towards is needed"
           " for this SizeChangeStrategy");
    return increaseToLargerTypesAndDecreaseToLargest(v, WidenScalar,
                                                     NarrowScalar);
  }

  static SizeAndActionsVec
  widenToLargerTypesUnsupportedOtherwise(const SizeAndActionsVec &v) {
    using namespace LegacyLegalizeActions;
    return increaseToLargerTypesAndDecreaseToLargest(v, WidenScalar,
                                                     Unsupported);
  }

  static SizeAndActionsVec
  narrowToSmallerAndUnsupportedIfTooSmall(const SizeAndActionsVec &v) {
    using namespace LegacyLegalizeActions;
    return decreaseToSmallerTypesAndIncreaseToSmallest(v, NarrowScalar,
                                                       Unsupported);
  }

  /// A SizeChangeStrategy for the common case where legalization for a
  /// particular vector operation consists of having more elements in the
  /// vector, to a type that is legal. Unless there is no such type and then
  /// instead it should be legalized towards the widest vector that's still
  /// legal. E.g.
  ///   setAction({G_ADD, LLT::vector(8, 8)}, Legal);
  ///   setAction({G_ADD, LLT::vector(16, 8)}, Legal);
  ///   setAction({G_ADD, LLT::vector(2, 32)}, Legal);
  ///   setAction({G_ADD, LLT::vector(4, 32)}, Legal);
  ///   setLegalizeVectorElementToDifferentSizeStrategy(
  ///     G_ADD, 0, moreToWiderTypesAndLessToWidest);
  /// will result in the following getAction results:
  ///   * getAction({G_ADD, LLT::vector(8,8)}) returns
  ///       (Legal, vector(8,8)).
  ///   * getAction({G_ADD, LLT::vector(9,8)}) returns
  ///       (MoreElements, vector(16,8)).
  ///   * getAction({G_ADD, LLT::vector(8,32)}) returns
  ///       (FewerElements, vector(4,32)).
  static SizeAndActionsVec
  moreToWiderTypesAndLessToWidest(const SizeAndActionsVec &v) {
    using namespace LegacyLegalizeActions;
    return increaseToLargerTypesAndDecreaseToLargest(v, MoreElements,
                                                     FewerElements);
  }

  /// Helper function to implement many typical SizeChangeStrategy functions.
  static SizeAndActionsVec increaseToLargerTypesAndDecreaseToLargest(
      const SizeAndActionsVec &v,
      LegacyLegalizeActions::LegacyLegalizeAction IncreaseAction,
      LegacyLegalizeActions::LegacyLegalizeAction DecreaseAction);
  /// Helper function to implement many typical SizeChangeStrategy functions.
  static SizeAndActionsVec decreaseToSmallerTypesAndIncreaseToSmallest(
      const SizeAndActionsVec &v,
      LegacyLegalizeActions::LegacyLegalizeAction DecreaseAction,
      LegacyLegalizeActions::LegacyLegalizeAction IncreaseAction);

  LegacyLegalizeActionStep getAction(const LegalityQuery &Query) const;

  unsigned getOpcodeIdxForOpcode(unsigned Opcode) const;

private:
  /// Determine what action should be taken to legalize the given generic
  /// instruction opcode, type-index and type. Requires computeTables to have
  /// been called.
  ///
  /// \returns a pair consisting of the kind of legalization that should be
  /// performed and the destination type.
  std::pair<LegacyLegalizeActions::LegacyLegalizeAction, LLT>
  getAspectAction(const InstrAspect &Aspect) const;

  /// The SizeAndActionsVec is a representation mapping between all natural
  /// numbers and an Action. The natural number represents the bit size of
  /// the InstrAspect. For example, for a target with native support for 32-bit
  /// and 64-bit additions, you'd express that as:
  /// setScalarAction(G_ADD, 0,
  ///           {{1, WidenScalar},  // bit sizes [ 1, 31[
  ///            {32, Legal},       // bit sizes [32, 33[
  ///            {33, WidenScalar}, // bit sizes [33, 64[
  ///            {64, Legal},       // bit sizes [64, 65[
  ///            {65, NarrowScalar} // bit sizes [65, +inf[
  ///           });
  /// It may be that only 64-bit pointers are supported on your target:
  /// setPointerAction(G_PTR_ADD, 0, LLT:pointer(1),
  ///           {{1, Unsupported},  // bit sizes [ 1, 63[
  ///            {64, Legal},       // bit sizes [64, 65[
  ///            {65, Unsupported}, // bit sizes [65, +inf[
  ///           });
  void setScalarAction(const unsigned Opcode, const unsigned TypeIndex,
                       const SizeAndActionsVec &SizeAndActions) {
    const unsigned OpcodeIdx = Opcode - FirstOp;
    SmallVector<SizeAndActionsVec, 1> &Actions = ScalarActions[OpcodeIdx];
    setActions(TypeIndex, Actions, SizeAndActions);
  }
  void setPointerAction(const unsigned Opcode, const unsigned TypeIndex,
                        const unsigned AddressSpace,
                        const SizeAndActionsVec &SizeAndActions) {
    const unsigned OpcodeIdx = Opcode - FirstOp;
    if (AddrSpace2PointerActions[OpcodeIdx].find(AddressSpace) ==
        AddrSpace2PointerActions[OpcodeIdx].end())
      AddrSpace2PointerActions[OpcodeIdx][AddressSpace] = {{}};
    SmallVector<SizeAndActionsVec, 1> &Actions =
        AddrSpace2PointerActions[OpcodeIdx].find(AddressSpace)->second;
    setActions(TypeIndex, Actions, SizeAndActions);
  }

  /// If an operation on a given vector type (say <M x iN>) isn't explicitly
  /// specified, we proceed in 2 stages. First we legalize the underlying scalar
  /// (so that there's at least one legal vector with that scalar), then we
  /// adjust the number of elements in the vector so that it is legal. The
  /// desired action in the first step is controlled by this function.
  void setScalarInVectorAction(const unsigned Opcode, const unsigned TypeIndex,
                               const SizeAndActionsVec &SizeAndActions) {
    unsigned OpcodeIdx = Opcode - FirstOp;
    SmallVector<SizeAndActionsVec, 1> &Actions =
        ScalarInVectorActions[OpcodeIdx];
    setActions(TypeIndex, Actions, SizeAndActions);
  }

  /// See also setScalarInVectorAction.
  /// This function let's you specify the number of elements in a vector that
  /// are legal for a legal element size.
  void setVectorNumElementAction(const unsigned Opcode,
                                 const unsigned TypeIndex,
                                 const unsigned ElementSize,
                                 const SizeAndActionsVec &SizeAndActions) {
    const unsigned OpcodeIdx = Opcode - FirstOp;
    if (NumElements2Actions[OpcodeIdx].find(ElementSize) ==
        NumElements2Actions[OpcodeIdx].end())
      NumElements2Actions[OpcodeIdx][ElementSize] = {{}};
    SmallVector<SizeAndActionsVec, 1> &Actions =
        NumElements2Actions[OpcodeIdx].find(ElementSize)->second;
    setActions(TypeIndex, Actions, SizeAndActions);
  }

  /// A partial SizeAndActionsVec potentially doesn't cover all bit sizes,
  /// i.e. it's OK if it doesn't start from size 1.
  static void checkPartialSizeAndActionsVector(const SizeAndActionsVec& v) {
    using namespace LegacyLegalizeActions;
#ifndef NDEBUG
    // The sizes should be in increasing order
    int prev_size = -1;
    for(auto SizeAndAction: v) {
      assert(SizeAndAction.first > prev_size);
      prev_size = SizeAndAction.first;
    }
    // - for every Widen action, there should be a larger bitsize that
    //   can be legalized towards (e.g. Legal, Lower, Libcall or Custom
    //   action).
    // - for every Narrow action, there should be a smaller bitsize that
    //   can be legalized towards.
    int SmallestNarrowIdx = -1;
    int LargestWidenIdx = -1;
    int SmallestLegalizableToSameSizeIdx = -1;
    int LargestLegalizableToSameSizeIdx = -1;
    for(size_t i=0; i<v.size(); ++i) {
      switch (v[i].second) {
        case FewerElements:
        case NarrowScalar:
          if (SmallestNarrowIdx == -1)
            SmallestNarrowIdx = i;
          break;
        case WidenScalar:
        case MoreElements:
          LargestWidenIdx = i;
          break;
        case Unsupported:
          break;
        default:
          if (SmallestLegalizableToSameSizeIdx == -1)
            SmallestLegalizableToSameSizeIdx = i;
          LargestLegalizableToSameSizeIdx = i;
      }
    }
    if (SmallestNarrowIdx != -1) {
      assert(SmallestLegalizableToSameSizeIdx != -1);
      assert(SmallestNarrowIdx > SmallestLegalizableToSameSizeIdx);
    }
    if (LargestWidenIdx != -1)
      assert(LargestWidenIdx < LargestLegalizableToSameSizeIdx);
#endif
  }

  /// A full SizeAndActionsVec must cover all bit sizes, i.e. must start with
  /// from size 1.
  static void checkFullSizeAndActionsVector(const SizeAndActionsVec& v) {
#ifndef NDEBUG
    // Data structure invariant: The first bit size must be size 1.
    assert(v.size() >= 1);
    assert(v[0].first == 1);
    checkPartialSizeAndActionsVector(v);
#endif
  }

  /// Sets actions for all bit sizes on a particular generic opcode, type
  /// index and scalar or pointer type.
  void setActions(unsigned TypeIndex,
                  SmallVector<SizeAndActionsVec, 1> &Actions,
                  const SizeAndActionsVec &SizeAndActions) {
    checkFullSizeAndActionsVector(SizeAndActions);
    if (Actions.size() <= TypeIndex)
      Actions.resize(TypeIndex + 1);
    Actions[TypeIndex] = SizeAndActions;
  }

  static SizeAndAction findAction(const SizeAndActionsVec &Vec,
                                  const uint32_t Size);

  /// Returns the next action needed to get the scalar or pointer type closer
  /// to being legal
  /// E.g. findLegalAction({G_REM, 13}) should return
  /// (WidenScalar, 32). After that, findLegalAction({G_REM, 32}) will
  /// probably be called, which should return (Lower, 32).
  /// This is assuming the setScalarAction on G_REM was something like:
  /// setScalarAction(G_REM, 0,
  ///           {{1, WidenScalar},  // bit sizes [ 1, 31[
  ///            {32, Lower},       // bit sizes [32, 33[
  ///            {33, NarrowScalar} // bit sizes [65, +inf[
  ///           });
  std::pair<LegacyLegalizeActions::LegacyLegalizeAction, LLT>
  findScalarLegalAction(const InstrAspect &Aspect) const;

  /// Returns the next action needed towards legalizing the vector type.
  std::pair<LegacyLegalizeActions::LegacyLegalizeAction, LLT>
  findVectorLegalAction(const InstrAspect &Aspect) const;

  static const int FirstOp = TargetOpcode::PRE_ISEL_GENERIC_OPCODE_START;
  static const int LastOp = TargetOpcode::PRE_ISEL_GENERIC_OPCODE_END;

  // Data structures used temporarily during construction of legality data:
  using TypeMap = DenseMap<LLT, LegacyLegalizeActions::LegacyLegalizeAction>;
  SmallVector<TypeMap, 1> SpecifiedActions[LastOp - FirstOp + 1];
  SmallVector<SizeChangeStrategy, 1>
      ScalarSizeChangeStrategies[LastOp - FirstOp + 1];
  SmallVector<SizeChangeStrategy, 1>
      VectorElementSizeChangeStrategies[LastOp - FirstOp + 1];
  bool TablesInitialized = false;

  // Data structures used by getAction:
  SmallVector<SizeAndActionsVec, 1> ScalarActions[LastOp - FirstOp + 1];
  SmallVector<SizeAndActionsVec, 1> ScalarInVectorActions[LastOp - FirstOp + 1];
  std::unordered_map<uint16_t, SmallVector<SizeAndActionsVec, 1>>
      AddrSpace2PointerActions[LastOp - FirstOp + 1];
  std::unordered_map<uint16_t, SmallVector<SizeAndActionsVec, 1>>
      NumElements2Actions[LastOp - FirstOp + 1];
};

} // end namespace llvm

#endif // LLVM_CODEGEN_GLOBALISEL_LEGACYLEGALIZERINFO_H

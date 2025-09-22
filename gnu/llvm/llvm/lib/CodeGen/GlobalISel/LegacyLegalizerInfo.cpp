//===- lib/CodeGen/GlobalISel/LegacyLegalizerInfo.cpp - Legalizer ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implement an interface to specify and query how an illegal operation on a
// given type should be expanded.
//
// Issues to be resolved:
//   + Make it fast.
//   + Support weird types like i3, <7 x i3>, ...
//   + Operations with more than one type (ICMP, CMPXCHG, intrinsics, ...)
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/GlobalISel/LegacyLegalizerInfo.h"
#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"
#include <map>

using namespace llvm;
using namespace LegacyLegalizeActions;

#define DEBUG_TYPE "legalizer-info"

raw_ostream &llvm::operator<<(raw_ostream &OS, LegacyLegalizeAction Action) {
  switch (Action) {
  case Legal:
    OS << "Legal";
    break;
  case NarrowScalar:
    OS << "NarrowScalar";
    break;
  case WidenScalar:
    OS << "WidenScalar";
    break;
  case FewerElements:
    OS << "FewerElements";
    break;
  case MoreElements:
    OS << "MoreElements";
    break;
  case Bitcast:
    OS << "Bitcast";
    break;
  case Lower:
    OS << "Lower";
    break;
  case Libcall:
    OS << "Libcall";
    break;
  case Custom:
    OS << "Custom";
    break;
  case Unsupported:
    OS << "Unsupported";
    break;
  case NotFound:
    OS << "NotFound";
    break;
  }
  return OS;
}

LegacyLegalizerInfo::LegacyLegalizerInfo() {
  // Set defaults.
  // FIXME: these two (G_ANYEXT and G_TRUNC?) can be legalized to the
  // fundamental load/store Jakob proposed. Once loads & stores are supported.
  setScalarAction(TargetOpcode::G_ANYEXT, 1, {{1, Legal}});
  setScalarAction(TargetOpcode::G_ZEXT, 1, {{1, Legal}});
  setScalarAction(TargetOpcode::G_SEXT, 1, {{1, Legal}});
  setScalarAction(TargetOpcode::G_TRUNC, 0, {{1, Legal}});
  setScalarAction(TargetOpcode::G_TRUNC, 1, {{1, Legal}});

  setScalarAction(TargetOpcode::G_INTRINSIC, 0, {{1, Legal}});
  setScalarAction(TargetOpcode::G_INTRINSIC_W_SIDE_EFFECTS, 0, {{1, Legal}});
  setScalarAction(TargetOpcode::G_INTRINSIC_CONVERGENT, 0, {{1, Legal}});
  setScalarAction(TargetOpcode::G_INTRINSIC_CONVERGENT_W_SIDE_EFFECTS, 0,
                  {{1, Legal}});

  setLegalizeScalarToDifferentSizeStrategy(
      TargetOpcode::G_IMPLICIT_DEF, 0, narrowToSmallerAndUnsupportedIfTooSmall);
  setLegalizeScalarToDifferentSizeStrategy(
      TargetOpcode::G_ADD, 0, widenToLargerTypesAndNarrowToLargest);
  setLegalizeScalarToDifferentSizeStrategy(
      TargetOpcode::G_OR, 0, widenToLargerTypesAndNarrowToLargest);
  setLegalizeScalarToDifferentSizeStrategy(
      TargetOpcode::G_LOAD, 0, narrowToSmallerAndUnsupportedIfTooSmall);
  setLegalizeScalarToDifferentSizeStrategy(
      TargetOpcode::G_STORE, 0, narrowToSmallerAndUnsupportedIfTooSmall);

  setLegalizeScalarToDifferentSizeStrategy(
      TargetOpcode::G_BRCOND, 0, widenToLargerTypesUnsupportedOtherwise);
  setLegalizeScalarToDifferentSizeStrategy(
      TargetOpcode::G_INSERT, 0, narrowToSmallerAndUnsupportedIfTooSmall);
  setLegalizeScalarToDifferentSizeStrategy(
      TargetOpcode::G_EXTRACT, 0, narrowToSmallerAndUnsupportedIfTooSmall);
  setLegalizeScalarToDifferentSizeStrategy(
      TargetOpcode::G_EXTRACT, 1, narrowToSmallerAndUnsupportedIfTooSmall);
  setScalarAction(TargetOpcode::G_FNEG, 0, {{1, Lower}});
}

void LegacyLegalizerInfo::computeTables() {
  assert(TablesInitialized == false);

  for (unsigned OpcodeIdx = 0; OpcodeIdx <= LastOp - FirstOp; ++OpcodeIdx) {
    const unsigned Opcode = FirstOp + OpcodeIdx;
    for (unsigned TypeIdx = 0; TypeIdx != SpecifiedActions[OpcodeIdx].size();
         ++TypeIdx) {
      // 0. Collect information specified through the setAction API, i.e.
      // for specific bit sizes.
      // For scalar types:
      SizeAndActionsVec ScalarSpecifiedActions;
      // For pointer types:
      std::map<uint16_t, SizeAndActionsVec> AddressSpace2SpecifiedActions;
      // For vector types:
      std::map<uint16_t, SizeAndActionsVec> ElemSize2SpecifiedActions;
      for (auto LLT2Action : SpecifiedActions[OpcodeIdx][TypeIdx]) {
        const LLT Type = LLT2Action.first;
        const LegacyLegalizeAction Action = LLT2Action.second;

        auto SizeAction = std::make_pair(Type.getSizeInBits(), Action);
        if (Type.isPointer())
          AddressSpace2SpecifiedActions[Type.getAddressSpace()].push_back(
              SizeAction);
        else if (Type.isVector())
          ElemSize2SpecifiedActions[Type.getElementType().getSizeInBits()]
              .push_back(SizeAction);
        else
          ScalarSpecifiedActions.push_back(SizeAction);
      }

      // 1. Handle scalar types
      {
        // Decide how to handle bit sizes for which no explicit specification
        // was given.
        SizeChangeStrategy S = &unsupportedForDifferentSizes;
        if (TypeIdx < ScalarSizeChangeStrategies[OpcodeIdx].size() &&
            ScalarSizeChangeStrategies[OpcodeIdx][TypeIdx] != nullptr)
          S = ScalarSizeChangeStrategies[OpcodeIdx][TypeIdx];
        llvm::sort(ScalarSpecifiedActions);
        checkPartialSizeAndActionsVector(ScalarSpecifiedActions);
        setScalarAction(Opcode, TypeIdx, S(ScalarSpecifiedActions));
      }

      // 2. Handle pointer types
      for (auto PointerSpecifiedActions : AddressSpace2SpecifiedActions) {
        llvm::sort(PointerSpecifiedActions.second);
        checkPartialSizeAndActionsVector(PointerSpecifiedActions.second);
        // For pointer types, we assume that there isn't a meaningfull way
        // to change the number of bits used in the pointer.
        setPointerAction(
            Opcode, TypeIdx, PointerSpecifiedActions.first,
            unsupportedForDifferentSizes(PointerSpecifiedActions.second));
      }

      // 3. Handle vector types
      SizeAndActionsVec ElementSizesSeen;
      for (auto VectorSpecifiedActions : ElemSize2SpecifiedActions) {
        llvm::sort(VectorSpecifiedActions.second);
        const uint16_t ElementSize = VectorSpecifiedActions.first;
        ElementSizesSeen.push_back({ElementSize, Legal});
        checkPartialSizeAndActionsVector(VectorSpecifiedActions.second);
        // For vector types, we assume that the best way to adapt the number
        // of elements is to the next larger number of elements type for which
        // the vector type is legal, unless there is no such type. In that case,
        // legalize towards a vector type with a smaller number of elements.
        SizeAndActionsVec NumElementsActions;
        for (SizeAndAction BitsizeAndAction : VectorSpecifiedActions.second) {
          assert(BitsizeAndAction.first % ElementSize == 0);
          const uint16_t NumElements = BitsizeAndAction.first / ElementSize;
          NumElementsActions.push_back({NumElements, BitsizeAndAction.second});
        }
        setVectorNumElementAction(
            Opcode, TypeIdx, ElementSize,
            moreToWiderTypesAndLessToWidest(NumElementsActions));
      }
      llvm::sort(ElementSizesSeen);
      SizeChangeStrategy VectorElementSizeChangeStrategy =
          &unsupportedForDifferentSizes;
      if (TypeIdx < VectorElementSizeChangeStrategies[OpcodeIdx].size() &&
          VectorElementSizeChangeStrategies[OpcodeIdx][TypeIdx] != nullptr)
        VectorElementSizeChangeStrategy =
            VectorElementSizeChangeStrategies[OpcodeIdx][TypeIdx];
      setScalarInVectorAction(
          Opcode, TypeIdx, VectorElementSizeChangeStrategy(ElementSizesSeen));
    }
  }

  TablesInitialized = true;
}

// FIXME: inefficient implementation for now. Without ComputeValueVTs we're
// probably going to need specialized lookup structures for various types before
// we have any hope of doing well with something like <13 x i3>. Even the common
// cases should do better than what we have now.
std::pair<LegacyLegalizeAction, LLT>
LegacyLegalizerInfo::getAspectAction(const InstrAspect &Aspect) const {
  assert(TablesInitialized && "backend forgot to call computeTables");
  // These *have* to be implemented for now, they're the fundamental basis of
  // how everything else is transformed.
  if (Aspect.Type.isScalar() || Aspect.Type.isPointer())
    return findScalarLegalAction(Aspect);
  assert(Aspect.Type.isVector());
  return findVectorLegalAction(Aspect);
}

LegacyLegalizerInfo::SizeAndActionsVec
LegacyLegalizerInfo::increaseToLargerTypesAndDecreaseToLargest(
    const SizeAndActionsVec &v, LegacyLegalizeAction IncreaseAction,
    LegacyLegalizeAction DecreaseAction) {
  SizeAndActionsVec result;
  unsigned LargestSizeSoFar = 0;
  if (v.size() >= 1 && v[0].first != 1)
    result.push_back({1, IncreaseAction});
  for (size_t i = 0; i < v.size(); ++i) {
    result.push_back(v[i]);
    LargestSizeSoFar = v[i].first;
    if (i + 1 < v.size() && v[i + 1].first != v[i].first + 1) {
      result.push_back({LargestSizeSoFar + 1, IncreaseAction});
      LargestSizeSoFar = v[i].first + 1;
    }
  }
  result.push_back({LargestSizeSoFar + 1, DecreaseAction});
  return result;
}

LegacyLegalizerInfo::SizeAndActionsVec
LegacyLegalizerInfo::decreaseToSmallerTypesAndIncreaseToSmallest(
    const SizeAndActionsVec &v, LegacyLegalizeAction DecreaseAction,
    LegacyLegalizeAction IncreaseAction) {
  SizeAndActionsVec result;
  if (v.size() == 0 || v[0].first != 1)
    result.push_back({1, IncreaseAction});
  for (size_t i = 0; i < v.size(); ++i) {
    result.push_back(v[i]);
    if (i + 1 == v.size() || v[i + 1].first != v[i].first + 1) {
      result.push_back({v[i].first + 1, DecreaseAction});
    }
  }
  return result;
}

LegacyLegalizerInfo::SizeAndAction
LegacyLegalizerInfo::findAction(const SizeAndActionsVec &Vec, const uint32_t Size) {
  assert(Size >= 1);
  // Find the last element in Vec that has a bitsize equal to or smaller than
  // the requested bit size.
  // That is the element just before the first element that is bigger than Size.
  auto It = partition_point(
      Vec, [=](const SizeAndAction &A) { return A.first <= Size; });
  assert(It != Vec.begin() && "Does Vec not start with size 1?");
  int VecIdx = It - Vec.begin() - 1;

  LegacyLegalizeAction Action = Vec[VecIdx].second;
  switch (Action) {
  case Legal:
  case Bitcast:
  case Lower:
  case Libcall:
  case Custom:
    return {Size, Action};
  case FewerElements:
    // FIXME: is this special case still needed and correct?
    // Special case for scalarization:
    if (Vec == SizeAndActionsVec({{1, FewerElements}}))
      return {1, FewerElements};
    [[fallthrough]];
  case NarrowScalar: {
    // The following needs to be a loop, as for now, we do allow needing to
    // go over "Unsupported" bit sizes before finding a legalizable bit size.
    // e.g. (s8, WidenScalar), (s9, Unsupported), (s32, Legal). if Size==8,
    // we need to iterate over s9, and then to s32 to return (s32, Legal).
    // If we want to get rid of the below loop, we should have stronger asserts
    // when building the SizeAndActionsVecs, probably not allowing
    // "Unsupported" unless at the ends of the vector.
    for (int i = VecIdx - 1; i >= 0; --i)
      if (!needsLegalizingToDifferentSize(Vec[i].second) &&
          Vec[i].second != Unsupported)
        return {Vec[i].first, Action};
    llvm_unreachable("");
  }
  case WidenScalar:
  case MoreElements: {
    // See above, the following needs to be a loop, at least for now.
    for (std::size_t i = VecIdx + 1; i < Vec.size(); ++i)
      if (!needsLegalizingToDifferentSize(Vec[i].second) &&
          Vec[i].second != Unsupported)
        return {Vec[i].first, Action};
    llvm_unreachable("");
  }
  case Unsupported:
    return {Size, Unsupported};
  case NotFound:
    llvm_unreachable("NotFound");
  }
  llvm_unreachable("Action has an unknown enum value");
}

std::pair<LegacyLegalizeAction, LLT>
LegacyLegalizerInfo::findScalarLegalAction(const InstrAspect &Aspect) const {
  assert(Aspect.Type.isScalar() || Aspect.Type.isPointer());
  if (Aspect.Opcode < FirstOp || Aspect.Opcode > LastOp)
    return {NotFound, LLT()};
  const unsigned OpcodeIdx = getOpcodeIdxForOpcode(Aspect.Opcode);
  if (Aspect.Type.isPointer() &&
      AddrSpace2PointerActions[OpcodeIdx].find(Aspect.Type.getAddressSpace()) ==
          AddrSpace2PointerActions[OpcodeIdx].end()) {
    return {NotFound, LLT()};
  }
  const SmallVector<SizeAndActionsVec, 1> &Actions =
      Aspect.Type.isPointer()
          ? AddrSpace2PointerActions[OpcodeIdx]
                .find(Aspect.Type.getAddressSpace())
                ->second
          : ScalarActions[OpcodeIdx];
  if (Aspect.Idx >= Actions.size())
    return {NotFound, LLT()};
  const SizeAndActionsVec &Vec = Actions[Aspect.Idx];
  // FIXME: speed up this search, e.g. by using a results cache for repeated
  // queries?
  auto SizeAndAction = findAction(Vec, Aspect.Type.getSizeInBits());
  return {SizeAndAction.second,
          Aspect.Type.isScalar() ? LLT::scalar(SizeAndAction.first)
                                 : LLT::pointer(Aspect.Type.getAddressSpace(),
                                                SizeAndAction.first)};
}

std::pair<LegacyLegalizeAction, LLT>
LegacyLegalizerInfo::findVectorLegalAction(const InstrAspect &Aspect) const {
  assert(Aspect.Type.isVector());
  // First legalize the vector element size, then legalize the number of
  // lanes in the vector.
  if (Aspect.Opcode < FirstOp || Aspect.Opcode > LastOp)
    return {NotFound, Aspect.Type};
  const unsigned OpcodeIdx = getOpcodeIdxForOpcode(Aspect.Opcode);
  const unsigned TypeIdx = Aspect.Idx;
  if (TypeIdx >= ScalarInVectorActions[OpcodeIdx].size())
    return {NotFound, Aspect.Type};
  const SizeAndActionsVec &ElemSizeVec =
      ScalarInVectorActions[OpcodeIdx][TypeIdx];

  LLT IntermediateType;
  auto ElementSizeAndAction =
      findAction(ElemSizeVec, Aspect.Type.getScalarSizeInBits());
  IntermediateType = LLT::fixed_vector(Aspect.Type.getNumElements(),
                                       ElementSizeAndAction.first);
  if (ElementSizeAndAction.second != Legal)
    return {ElementSizeAndAction.second, IntermediateType};

  auto i = NumElements2Actions[OpcodeIdx].find(
      IntermediateType.getScalarSizeInBits());
  if (i == NumElements2Actions[OpcodeIdx].end()) {
    return {NotFound, IntermediateType};
  }
  const SizeAndActionsVec &NumElementsVec = (*i).second[TypeIdx];
  auto NumElementsAndAction =
      findAction(NumElementsVec, IntermediateType.getNumElements());
  return {NumElementsAndAction.second,
          LLT::fixed_vector(NumElementsAndAction.first,
                            IntermediateType.getScalarSizeInBits())};
}

unsigned LegacyLegalizerInfo::getOpcodeIdxForOpcode(unsigned Opcode) const {
  assert(Opcode >= FirstOp && Opcode <= LastOp && "Unsupported opcode");
  return Opcode - FirstOp;
}


LegacyLegalizeActionStep
LegacyLegalizerInfo::getAction(const LegalityQuery &Query) const {
  for (unsigned i = 0; i < Query.Types.size(); ++i) {
    auto Action = getAspectAction({Query.Opcode, i, Query.Types[i]});
    if (Action.first != Legal) {
      LLVM_DEBUG(dbgs() << ".. (legacy) Type " << i << " Action="
                        << Action.first << ", " << Action.second << "\n");
      return {Action.first, i, Action.second};
    } else
      LLVM_DEBUG(dbgs() << ".. (legacy) Type " << i << " Legal\n");
  }
  LLVM_DEBUG(dbgs() << ".. (legacy) Legal\n");
  return {Legal, 0, LLT{}};
}


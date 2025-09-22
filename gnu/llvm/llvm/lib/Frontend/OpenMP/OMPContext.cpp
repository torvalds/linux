//===- OMPContext.cpp ------ Collection of helpers for OpenMP contexts ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file implements helper functions and classes to deal with OpenMP
/// contexts as used by `[begin/end] declare variant` and `metadirective`.
///
//===----------------------------------------------------------------------===//

#include "llvm/Frontend/OpenMP/OMPContext.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"

#define DEBUG_TYPE "openmp-ir-builder"

using namespace llvm;
using namespace omp;

OMPContext::OMPContext(bool IsDeviceCompilation, Triple TargetTriple) {
  // Add the appropriate device kind trait based on the triple and the
  // IsDeviceCompilation flag.
  ActiveTraits.set(unsigned(IsDeviceCompilation
                                ? TraitProperty::device_kind_nohost
                                : TraitProperty::device_kind_host));
  switch (TargetTriple.getArch()) {
  case Triple::arm:
  case Triple::armeb:
  case Triple::aarch64:
  case Triple::aarch64_be:
  case Triple::aarch64_32:
  case Triple::mips:
  case Triple::mipsel:
  case Triple::mips64:
  case Triple::mips64el:
  case Triple::ppc:
  case Triple::ppcle:
  case Triple::ppc64:
  case Triple::ppc64le:
  case Triple::systemz:
  case Triple::x86:
  case Triple::x86_64:
    ActiveTraits.set(unsigned(TraitProperty::device_kind_cpu));
    break;
  case Triple::amdgcn:
  case Triple::nvptx:
  case Triple::nvptx64:
    ActiveTraits.set(unsigned(TraitProperty::device_kind_gpu));
    break;
  default:
    break;
  }

  // Add the appropriate device architecture trait based on the triple.
#define OMP_TRAIT_PROPERTY(Enum, TraitSetEnum, TraitSelectorEnum, Str)         \
  if (TraitSelector::TraitSelectorEnum == TraitSelector::device_arch) {        \
    if (TargetTriple.getArch() == TargetTriple.getArchTypeForLLVMName(Str))    \
      ActiveTraits.set(unsigned(TraitProperty::Enum));                         \
    if (StringRef(Str) == "x86_64" &&                                          \
        TargetTriple.getArch() == Triple::x86_64)                              \
      ActiveTraits.set(unsigned(TraitProperty::Enum));                         \
  }
#include "llvm/Frontend/OpenMP/OMPKinds.def"

  // TODO: What exactly do we want to see as device ISA trait?
  //       The discussion on the list did not seem to have come to an agreed
  //       upon solution.

  // LLVM is the "OpenMP vendor" but we could also interpret vendor as the
  // target vendor.
  ActiveTraits.set(unsigned(TraitProperty::implementation_vendor_llvm));

  // The user condition true is accepted but not false.
  ActiveTraits.set(unsigned(TraitProperty::user_condition_true));

  // This is for sure some device.
  ActiveTraits.set(unsigned(TraitProperty::device_kind_any));

  LLVM_DEBUG({
    dbgs() << "[" << DEBUG_TYPE
           << "] New OpenMP context with the following properties:\n";
    for (unsigned Bit : ActiveTraits.set_bits()) {
      TraitProperty Property = TraitProperty(Bit);
      dbgs() << "\t " << getOpenMPContextTraitPropertyFullName(Property)
             << "\n";
    }
  });
}

/// Return true if \p C0 is a subset of \p C1. Note that both arrays are
/// expected to be sorted.
template <typename T> static bool isSubset(ArrayRef<T> C0, ArrayRef<T> C1) {
#ifdef EXPENSIVE_CHECKS
  assert(llvm::is_sorted(C0) && llvm::is_sorted(C1) &&
         "Expected sorted arrays!");
#endif
  if (C0.size() > C1.size())
    return false;
  auto It0 = C0.begin(), End0 = C0.end();
  auto It1 = C1.begin(), End1 = C1.end();
  while (It0 != End0) {
    if (It1 == End1)
      return false;
    if (*It0 == *It1) {
      ++It0;
      ++It1;
      continue;
    }
    ++It0;
  }
  return true;
}

/// Return true if \p C0 is a strict subset of \p C1. Note that both arrays are
/// expected to be sorted.
template <typename T>
static bool isStrictSubset(ArrayRef<T> C0, ArrayRef<T> C1) {
  if (C0.size() >= C1.size())
    return false;
  return isSubset<T>(C0, C1);
}

static bool isStrictSubset(const VariantMatchInfo &VMI0,
                           const VariantMatchInfo &VMI1) {
  // If all required traits are a strict subset and the ordered vectors storing
  // the construct traits, we say it is a strict subset. Note that the latter
  // relation is not required to be strict.
  if (VMI0.RequiredTraits.count() >= VMI1.RequiredTraits.count())
    return false;
  for (unsigned Bit : VMI0.RequiredTraits.set_bits())
    if (!VMI1.RequiredTraits.test(Bit))
      return false;
  if (!isSubset<TraitProperty>(VMI0.ConstructTraits, VMI1.ConstructTraits))
    return false;
  return true;
}

static int isVariantApplicableInContextHelper(
    const VariantMatchInfo &VMI, const OMPContext &Ctx,
    SmallVectorImpl<unsigned> *ConstructMatches, bool DeviceSetOnly) {

  // The match kind determines if we need to match all traits, any of the
  // traits, or none of the traits for it to be an applicable context.
  enum MatchKind { MK_ALL, MK_ANY, MK_NONE };

  MatchKind MK = MK_ALL;
  // Determine the match kind the user wants, "all" is the default and provided
  // to the user only for completeness.
  if (VMI.RequiredTraits.test(
          unsigned(TraitProperty::implementation_extension_match_any)))
    MK = MK_ANY;
  if (VMI.RequiredTraits.test(
          unsigned(TraitProperty::implementation_extension_match_none)))
    MK = MK_NONE;

  // Helper to deal with a single property that was (not) found in the OpenMP
  // context based on the match kind selected by the user via
  // `implementation={extensions(match_[all,any,none])}'
  auto HandleTrait = [MK](TraitProperty Property,
                          bool WasFound) -> std::optional<bool> /* Result */ {
    // For kind "any" a single match is enough but we ignore non-matched
    // properties.
    if (MK == MK_ANY) {
      if (WasFound)
        return true;
      return std::nullopt;
    }

    // In "all" or "none" mode we accept a matching or non-matching property
    // respectively and move on. We are not done yet!
    if ((WasFound && MK == MK_ALL) || (!WasFound && MK == MK_NONE))
      return std::nullopt;

    // We missed a property, provide some debug output and indicate failure.
    LLVM_DEBUG({
      if (MK == MK_ALL)
        dbgs() << "[" << DEBUG_TYPE << "] Property "
               << getOpenMPContextTraitPropertyName(Property, "")
               << " was not in the OpenMP context but match kind is all.\n";
      if (MK == MK_NONE)
        dbgs() << "[" << DEBUG_TYPE << "] Property "
               << getOpenMPContextTraitPropertyName(Property, "")
               << " was in the OpenMP context but match kind is none.\n";
    });
    return false;
  };

  for (unsigned Bit : VMI.RequiredTraits.set_bits()) {
    TraitProperty Property = TraitProperty(Bit);
    if (DeviceSetOnly &&
        getOpenMPContextTraitSetForProperty(Property) != TraitSet::device)
      continue;

    // So far all extensions are handled elsewhere, we skip them here as they
    // are not part of the OpenMP context.
    if (getOpenMPContextTraitSelectorForProperty(Property) ==
        TraitSelector::implementation_extension)
      continue;

    bool IsActiveTrait = Ctx.ActiveTraits.test(unsigned(Property));

    // We overwrite the isa trait as it is actually up to the OMPContext hook to
    // check the raw string(s).
    if (Property == TraitProperty::device_isa___ANY)
      IsActiveTrait = llvm::all_of(VMI.ISATraits, [&](StringRef RawString) {
        return Ctx.matchesISATrait(RawString);
      });

    if (std::optional<bool> Result = HandleTrait(Property, IsActiveTrait))
      return *Result;
  }

  if (!DeviceSetOnly) {
    // We could use isSubset here but we also want to record the match
    // locations.
    unsigned ConstructIdx = 0, NoConstructTraits = Ctx.ConstructTraits.size();
    for (TraitProperty Property : VMI.ConstructTraits) {
      assert(getOpenMPContextTraitSetForProperty(Property) ==
                 TraitSet::construct &&
             "Variant context is ill-formed!");

      // Verify the nesting.
      bool FoundInOrder = false;
      while (!FoundInOrder && ConstructIdx != NoConstructTraits)
        FoundInOrder = (Ctx.ConstructTraits[ConstructIdx++] == Property);
      if (ConstructMatches)
        ConstructMatches->push_back(ConstructIdx - 1);

      if (std::optional<bool> Result = HandleTrait(Property, FoundInOrder))
        return *Result;

      if (!FoundInOrder) {
        LLVM_DEBUG(dbgs() << "[" << DEBUG_TYPE << "] Construct property "
                          << getOpenMPContextTraitPropertyName(Property, "")
                          << " was not nested properly.\n");
        return false;
      }

      // TODO: Verify SIMD
    }

    assert(isSubset<TraitProperty>(VMI.ConstructTraits, Ctx.ConstructTraits) &&
           "Broken invariant!");
  }

  if (MK == MK_ANY) {
    LLVM_DEBUG(dbgs() << "[" << DEBUG_TYPE
                      << "] None of the properties was in the OpenMP context "
                         "but match kind is any.\n");
    return false;
  }

  return true;
}

bool llvm::omp::isVariantApplicableInContext(const VariantMatchInfo &VMI,
                                             const OMPContext &Ctx,
                                             bool DeviceSetOnly) {
  return isVariantApplicableInContextHelper(
      VMI, Ctx, /* ConstructMatches */ nullptr, DeviceSetOnly);
}

static APInt getVariantMatchScore(const VariantMatchInfo &VMI,
                                  const OMPContext &Ctx,
                                  SmallVectorImpl<unsigned> &ConstructMatches) {
  APInt Score(64, 1);

  unsigned NoConstructTraits = VMI.ConstructTraits.size();
  for (unsigned Bit : VMI.RequiredTraits.set_bits()) {
    TraitProperty Property = TraitProperty(Bit);
    // If there is a user score attached, use it.
    if (VMI.ScoreMap.count(Property)) {
      const APInt &UserScore = VMI.ScoreMap.lookup(Property);
      assert(UserScore.uge(0) && "Expect non-negative user scores!");
      Score += UserScore.getZExtValue();
      continue;
    }

    switch (getOpenMPContextTraitSetForProperty(Property)) {
    case TraitSet::construct:
      // We handle the construct traits later via the VMI.ConstructTraits
      // container.
      continue;
    case TraitSet::implementation:
      // No effect on the score (implementation defined).
      continue;
    case TraitSet::user:
      // No effect on the score.
      continue;
    case TraitSet::device:
      // Handled separately below.
      break;
    case TraitSet::invalid:
      llvm_unreachable("Unknown trait set is not to be used!");
    }

    // device={kind(any)} is "as if" no kind selector was specified.
    if (Property == TraitProperty::device_kind_any)
      continue;

    switch (getOpenMPContextTraitSelectorForProperty(Property)) {
    case TraitSelector::device_kind:
      Score += (1ULL << (NoConstructTraits + 0));
      continue;
    case TraitSelector::device_arch:
      Score += (1ULL << (NoConstructTraits + 1));
      continue;
    case TraitSelector::device_isa:
      Score += (1ULL << (NoConstructTraits + 2));
      continue;
    default:
      continue;
    }
  }

  unsigned ConstructIdx = 0;
  assert(NoConstructTraits == ConstructMatches.size() &&
         "Mismatch in the construct traits!");
  for (TraitProperty Property : VMI.ConstructTraits) {
    assert(getOpenMPContextTraitSetForProperty(Property) ==
               TraitSet::construct &&
           "Ill-formed variant match info!");
    (void)Property;
    // ConstructMatches is the position p - 1 and we need 2^(p-1).
    Score += (1ULL << ConstructMatches[ConstructIdx++]);
  }

  LLVM_DEBUG(dbgs() << "[" << DEBUG_TYPE << "] Variant has a score of " << Score
                    << "\n");
  return Score;
}

int llvm::omp::getBestVariantMatchForContext(
    const SmallVectorImpl<VariantMatchInfo> &VMIs, const OMPContext &Ctx) {

  APInt BestScore(64, 0);
  int BestVMIIdx = -1;
  const VariantMatchInfo *BestVMI = nullptr;

  for (unsigned u = 0, e = VMIs.size(); u < e; ++u) {
    const VariantMatchInfo &VMI = VMIs[u];

    SmallVector<unsigned, 8> ConstructMatches;
    // If the variant is not applicable its not the best.
    if (!isVariantApplicableInContextHelper(VMI, Ctx, &ConstructMatches,
                                            /* DeviceSetOnly */ false))
      continue;
    // Check if its clearly not the best.
    APInt Score = getVariantMatchScore(VMI, Ctx, ConstructMatches);
    if (Score.ult(BestScore))
      continue;
    // Equal score need subset checks.
    if (Score.eq(BestScore)) {
      // Strict subset are never best.
      if (isStrictSubset(VMI, *BestVMI))
        continue;
      // Same score and the current best is no strict subset so we keep it.
      if (!isStrictSubset(*BestVMI, VMI))
        continue;
    }
    // New best found.
    BestVMI = &VMI;
    BestVMIIdx = u;
    BestScore = Score;
  }

  return BestVMIIdx;
}

TraitSet llvm::omp::getOpenMPContextTraitSetKind(StringRef S) {
  return StringSwitch<TraitSet>(S)
#define OMP_TRAIT_SET(Enum, Str) .Case(Str, TraitSet::Enum)
#include "llvm/Frontend/OpenMP/OMPKinds.def"
      .Default(TraitSet::invalid);
}

TraitSet
llvm::omp::getOpenMPContextTraitSetForSelector(TraitSelector Selector) {
  switch (Selector) {
#define OMP_TRAIT_SELECTOR(Enum, TraitSetEnum, Str, ReqProp)                   \
  case TraitSelector::Enum:                                                    \
    return TraitSet::TraitSetEnum;
#include "llvm/Frontend/OpenMP/OMPKinds.def"
  }
  llvm_unreachable("Unknown trait selector!");
}
TraitSet
llvm::omp::getOpenMPContextTraitSetForProperty(TraitProperty Property) {
  switch (Property) {
#define OMP_TRAIT_PROPERTY(Enum, TraitSetEnum, TraitSelectorEnum, Str)         \
  case TraitProperty::Enum:                                                    \
    return TraitSet::TraitSetEnum;
#include "llvm/Frontend/OpenMP/OMPKinds.def"
  }
  llvm_unreachable("Unknown trait set!");
}
StringRef llvm::omp::getOpenMPContextTraitSetName(TraitSet Kind) {
  switch (Kind) {
#define OMP_TRAIT_SET(Enum, Str)                                               \
  case TraitSet::Enum:                                                         \
    return Str;
#include "llvm/Frontend/OpenMP/OMPKinds.def"
  }
  llvm_unreachable("Unknown trait set!");
}

TraitSelector llvm::omp::getOpenMPContextTraitSelectorKind(StringRef S) {
  return StringSwitch<TraitSelector>(S)
#define OMP_TRAIT_SELECTOR(Enum, TraitSetEnum, Str, ReqProp)                   \
  .Case(Str, TraitSelector::Enum)
#include "llvm/Frontend/OpenMP/OMPKinds.def"
      .Default(TraitSelector::invalid);
}
TraitSelector
llvm::omp::getOpenMPContextTraitSelectorForProperty(TraitProperty Property) {
  switch (Property) {
#define OMP_TRAIT_PROPERTY(Enum, TraitSetEnum, TraitSelectorEnum, Str)         \
  case TraitProperty::Enum:                                                    \
    return TraitSelector::TraitSelectorEnum;
#include "llvm/Frontend/OpenMP/OMPKinds.def"
  }
  llvm_unreachable("Unknown trait set!");
}
StringRef llvm::omp::getOpenMPContextTraitSelectorName(TraitSelector Kind) {
  switch (Kind) {
#define OMP_TRAIT_SELECTOR(Enum, TraitSetEnum, Str, ReqProp)                   \
  case TraitSelector::Enum:                                                    \
    return Str;
#include "llvm/Frontend/OpenMP/OMPKinds.def"
  }
  llvm_unreachable("Unknown trait selector!");
}

TraitProperty llvm::omp::getOpenMPContextTraitPropertyKind(
    TraitSet Set, TraitSelector Selector, StringRef S) {
  // Special handling for `device={isa(...)}` as we accept anything here. It is
  // up to the target to decide if the feature is available.
  if (Set == TraitSet::device && Selector == TraitSelector::device_isa)
    return TraitProperty::device_isa___ANY;
#define OMP_TRAIT_PROPERTY(Enum, TraitSetEnum, TraitSelectorEnum, Str)         \
  if (Set == TraitSet::TraitSetEnum && Str == S)                               \
    return TraitProperty::Enum;
#include "llvm/Frontend/OpenMP/OMPKinds.def"
  return TraitProperty::invalid;
}
TraitProperty
llvm::omp::getOpenMPContextTraitPropertyForSelector(TraitSelector Selector) {
  return StringSwitch<TraitProperty>(
             getOpenMPContextTraitSelectorName(Selector))
#define OMP_TRAIT_PROPERTY(Enum, TraitSetEnum, TraitSelectorEnum, Str)         \
  .Case(Str, Selector == TraitSelector::TraitSelectorEnum                      \
                 ? TraitProperty::Enum                                         \
                 : TraitProperty::invalid)
#include "llvm/Frontend/OpenMP/OMPKinds.def"
      .Default(TraitProperty::invalid);
}
StringRef llvm::omp::getOpenMPContextTraitPropertyName(TraitProperty Kind,
                                                       StringRef RawString) {
  if (Kind == TraitProperty::device_isa___ANY)
    return RawString;
  switch (Kind) {
#define OMP_TRAIT_PROPERTY(Enum, TraitSetEnum, TraitSelectorEnum, Str)         \
  case TraitProperty::Enum:                                                    \
    return Str;
#include "llvm/Frontend/OpenMP/OMPKinds.def"
  }
  llvm_unreachable("Unknown trait property!");
}
StringRef llvm::omp::getOpenMPContextTraitPropertyFullName(TraitProperty Kind) {
  switch (Kind) {
#define OMP_TRAIT_PROPERTY(Enum, TraitSetEnum, TraitSelectorEnum, Str)         \
  case TraitProperty::Enum:                                                    \
    return "(" #TraitSetEnum "," #TraitSelectorEnum "," Str ")";
#include "llvm/Frontend/OpenMP/OMPKinds.def"
  }
  llvm_unreachable("Unknown trait property!");
}

bool llvm::omp::isValidTraitSelectorForTraitSet(TraitSelector Selector,
                                                TraitSet Set,
                                                bool &AllowsTraitScore,
                                                bool &RequiresProperty) {
  AllowsTraitScore = Set != TraitSet::construct && Set != TraitSet::device;
  switch (Selector) {
#define OMP_TRAIT_SELECTOR(Enum, TraitSetEnum, Str, ReqProp)                   \
  case TraitSelector::Enum:                                                    \
    RequiresProperty = ReqProp;                                                \
    return Set == TraitSet::TraitSetEnum;
#include "llvm/Frontend/OpenMP/OMPKinds.def"
  }
  llvm_unreachable("Unknown trait selector!");
}

bool llvm::omp::isValidTraitPropertyForTraitSetAndSelector(
    TraitProperty Property, TraitSelector Selector, TraitSet Set) {
  switch (Property) {
#define OMP_TRAIT_PROPERTY(Enum, TraitSetEnum, TraitSelectorEnum, Str)         \
  case TraitProperty::Enum:                                                    \
    return Set == TraitSet::TraitSetEnum &&                                    \
           Selector == TraitSelector::TraitSelectorEnum;
#include "llvm/Frontend/OpenMP/OMPKinds.def"
  }
  llvm_unreachable("Unknown trait property!");
}

std::string llvm::omp::listOpenMPContextTraitSets() {
  std::string S;
#define OMP_TRAIT_SET(Enum, Str)                                               \
  if (StringRef(Str) != "invalid")                                             \
    S.append("'").append(Str).append("'").append(" ");
#include "llvm/Frontend/OpenMP/OMPKinds.def"
  S.pop_back();
  return S;
}

std::string llvm::omp::listOpenMPContextTraitSelectors(TraitSet Set) {
  std::string S;
#define OMP_TRAIT_SELECTOR(Enum, TraitSetEnum, Str, ReqProp)                   \
  if (TraitSet::TraitSetEnum == Set && StringRef(Str) != "Invalid")            \
    S.append("'").append(Str).append("'").append(" ");
#include "llvm/Frontend/OpenMP/OMPKinds.def"
  S.pop_back();
  return S;
}

std::string
llvm::omp::listOpenMPContextTraitProperties(TraitSet Set,
                                            TraitSelector Selector) {
  std::string S;
#define OMP_TRAIT_PROPERTY(Enum, TraitSetEnum, TraitSelectorEnum, Str)         \
  if (TraitSet::TraitSetEnum == Set &&                                         \
      TraitSelector::TraitSelectorEnum == Selector &&                          \
      StringRef(Str) != "invalid")                                             \
    S.append("'").append(Str).append("'").append(" ");
#include "llvm/Frontend/OpenMP/OMPKinds.def"
  if (S.empty())
    return "<none>";
  S.pop_back();
  return S;
}

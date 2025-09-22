//===- SampleProf.h - Sampling profiling format support ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains common definitions used in the reading and writing of
// sample profile data.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_SAMPLEPROF_H
#define LLVM_PROFILEDATA_SAMPLEPROF_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/ProfileData/FunctionId.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/ProfileData/HashKeyMap.h"
#include <algorithm>
#include <cstdint>
#include <list>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace llvm {

class DILocation;
class raw_ostream;

const std::error_category &sampleprof_category();

enum class sampleprof_error {
  success = 0,
  bad_magic,
  unsupported_version,
  too_large,
  truncated,
  malformed,
  unrecognized_format,
  unsupported_writing_format,
  truncated_name_table,
  not_implemented,
  counter_overflow,
  ostream_seek_unsupported,
  uncompress_failed,
  zlib_unavailable,
  hash_mismatch
};

inline std::error_code make_error_code(sampleprof_error E) {
  return std::error_code(static_cast<int>(E), sampleprof_category());
}

inline sampleprof_error mergeSampleProfErrors(sampleprof_error &Accumulator,
                                              sampleprof_error Result) {
  // Prefer first error encountered as later errors may be secondary effects of
  // the initial problem.
  if (Accumulator == sampleprof_error::success &&
      Result != sampleprof_error::success)
    Accumulator = Result;
  return Accumulator;
}

} // end namespace llvm

namespace std {

template <>
struct is_error_code_enum<llvm::sampleprof_error> : std::true_type {};

} // end namespace std

namespace llvm {
namespace sampleprof {

enum SampleProfileFormat {
  SPF_None = 0,
  SPF_Text = 0x1,
  SPF_Compact_Binary = 0x2, // Deprecated
  SPF_GCC = 0x3,
  SPF_Ext_Binary = 0x4,
  SPF_Binary = 0xff
};

enum SampleProfileLayout {
  SPL_None = 0,
  SPL_Nest = 0x1,
  SPL_Flat = 0x2,
};

static inline uint64_t SPMagic(SampleProfileFormat Format = SPF_Binary) {
  return uint64_t('S') << (64 - 8) | uint64_t('P') << (64 - 16) |
         uint64_t('R') << (64 - 24) | uint64_t('O') << (64 - 32) |
         uint64_t('F') << (64 - 40) | uint64_t('4') << (64 - 48) |
         uint64_t('2') << (64 - 56) | uint64_t(Format);
}

static inline uint64_t SPVersion() { return 103; }

// Section Type used by SampleProfileExtBinaryBaseReader and
// SampleProfileExtBinaryBaseWriter. Never change the existing
// value of enum. Only append new ones.
enum SecType {
  SecInValid = 0,
  SecProfSummary = 1,
  SecNameTable = 2,
  SecProfileSymbolList = 3,
  SecFuncOffsetTable = 4,
  SecFuncMetadata = 5,
  SecCSNameTable = 6,
  // marker for the first type of profile.
  SecFuncProfileFirst = 32,
  SecLBRProfile = SecFuncProfileFirst
};

static inline std::string getSecName(SecType Type) {
  switch (static_cast<int>(Type)) { // Avoid -Wcovered-switch-default
  case SecInValid:
    return "InvalidSection";
  case SecProfSummary:
    return "ProfileSummarySection";
  case SecNameTable:
    return "NameTableSection";
  case SecProfileSymbolList:
    return "ProfileSymbolListSection";
  case SecFuncOffsetTable:
    return "FuncOffsetTableSection";
  case SecFuncMetadata:
    return "FunctionMetadata";
  case SecCSNameTable:
    return "CSNameTableSection";
  case SecLBRProfile:
    return "LBRProfileSection";
  default:
    return "UnknownSection";
  }
}

// Entry type of section header table used by SampleProfileExtBinaryBaseReader
// and SampleProfileExtBinaryBaseWriter.
struct SecHdrTableEntry {
  SecType Type;
  uint64_t Flags;
  uint64_t Offset;
  uint64_t Size;
  // The index indicating the location of the current entry in
  // SectionHdrLayout table.
  uint64_t LayoutIndex;
};

// Flags common for all sections are defined here. In SecHdrTableEntry::Flags,
// common flags will be saved in the lower 32bits and section specific flags
// will be saved in the higher 32 bits.
enum class SecCommonFlags : uint32_t {
  SecFlagInValid = 0,
  SecFlagCompress = (1 << 0),
  // Indicate the section contains only profile without context.
  SecFlagFlat = (1 << 1)
};

// Section specific flags are defined here.
// !!!Note: Everytime a new enum class is created here, please add
// a new check in verifySecFlag.
enum class SecNameTableFlags : uint32_t {
  SecFlagInValid = 0,
  SecFlagMD5Name = (1 << 0),
  // Store MD5 in fixed length instead of ULEB128 so NameTable can be
  // accessed like an array.
  SecFlagFixedLengthMD5 = (1 << 1),
  // Profile contains ".__uniq." suffix name. Compiler shouldn't strip
  // the suffix when doing profile matching when seeing the flag.
  SecFlagUniqSuffix = (1 << 2)
};
enum class SecProfSummaryFlags : uint32_t {
  SecFlagInValid = 0,
  /// SecFlagPartial means the profile is for common/shared code.
  /// The common profile is usually merged from profiles collected
  /// from running other targets.
  SecFlagPartial = (1 << 0),
  /// SecFlagContext means this is context-sensitive flat profile for
  /// CSSPGO
  SecFlagFullContext = (1 << 1),
  /// SecFlagFSDiscriminator means this profile uses flow-sensitive
  /// discriminators.
  SecFlagFSDiscriminator = (1 << 2),
  /// SecFlagIsPreInlined means this profile contains ShouldBeInlined
  /// contexts thus this is CS preinliner computed.
  SecFlagIsPreInlined = (1 << 4),
};

enum class SecFuncMetadataFlags : uint32_t {
  SecFlagInvalid = 0,
  SecFlagIsProbeBased = (1 << 0),
  SecFlagHasAttribute = (1 << 1),
};

enum class SecFuncOffsetFlags : uint32_t {
  SecFlagInvalid = 0,
  // Store function offsets in an order of contexts. The order ensures that
  // callee contexts of a given context laid out next to it.
  SecFlagOrdered = (1 << 0),
};

// Verify section specific flag is used for the correct section.
template <class SecFlagType>
static inline void verifySecFlag(SecType Type, SecFlagType Flag) {
  // No verification is needed for common flags.
  if (std::is_same<SecCommonFlags, SecFlagType>())
    return;

  // Verification starts here for section specific flag.
  bool IsFlagLegal = false;
  switch (Type) {
  case SecNameTable:
    IsFlagLegal = std::is_same<SecNameTableFlags, SecFlagType>();
    break;
  case SecProfSummary:
    IsFlagLegal = std::is_same<SecProfSummaryFlags, SecFlagType>();
    break;
  case SecFuncMetadata:
    IsFlagLegal = std::is_same<SecFuncMetadataFlags, SecFlagType>();
    break;
  default:
  case SecFuncOffsetTable:
    IsFlagLegal = std::is_same<SecFuncOffsetFlags, SecFlagType>();
    break;
  }
  if (!IsFlagLegal)
    llvm_unreachable("Misuse of a flag in an incompatible section");
}

template <class SecFlagType>
static inline void addSecFlag(SecHdrTableEntry &Entry, SecFlagType Flag) {
  verifySecFlag(Entry.Type, Flag);
  auto FVal = static_cast<uint64_t>(Flag);
  bool IsCommon = std::is_same<SecCommonFlags, SecFlagType>();
  Entry.Flags |= IsCommon ? FVal : (FVal << 32);
}

template <class SecFlagType>
static inline void removeSecFlag(SecHdrTableEntry &Entry, SecFlagType Flag) {
  verifySecFlag(Entry.Type, Flag);
  auto FVal = static_cast<uint64_t>(Flag);
  bool IsCommon = std::is_same<SecCommonFlags, SecFlagType>();
  Entry.Flags &= ~(IsCommon ? FVal : (FVal << 32));
}

template <class SecFlagType>
static inline bool hasSecFlag(const SecHdrTableEntry &Entry, SecFlagType Flag) {
  verifySecFlag(Entry.Type, Flag);
  auto FVal = static_cast<uint64_t>(Flag);
  bool IsCommon = std::is_same<SecCommonFlags, SecFlagType>();
  return Entry.Flags & (IsCommon ? FVal : (FVal << 32));
}

/// Represents the relative location of an instruction.
///
/// Instruction locations are specified by the line offset from the
/// beginning of the function (marked by the line where the function
/// header is) and the discriminator value within that line.
///
/// The discriminator value is useful to distinguish instructions
/// that are on the same line but belong to different basic blocks
/// (e.g., the two post-increment instructions in "if (p) x++; else y++;").
struct LineLocation {
  LineLocation(uint32_t L, uint32_t D) : LineOffset(L), Discriminator(D) {}

  void print(raw_ostream &OS) const;
  void dump() const;

  bool operator<(const LineLocation &O) const {
    return LineOffset < O.LineOffset ||
           (LineOffset == O.LineOffset && Discriminator < O.Discriminator);
  }

  bool operator==(const LineLocation &O) const {
    return LineOffset == O.LineOffset && Discriminator == O.Discriminator;
  }

  bool operator!=(const LineLocation &O) const {
    return LineOffset != O.LineOffset || Discriminator != O.Discriminator;
  }

  uint64_t getHashCode() const {
    return ((uint64_t) Discriminator << 32) | LineOffset;
  }

  uint32_t LineOffset;
  uint32_t Discriminator;
};

struct LineLocationHash {
  uint64_t operator()(const LineLocation &Loc) const {
    return Loc.getHashCode();
  }
};

raw_ostream &operator<<(raw_ostream &OS, const LineLocation &Loc);

/// Representation of a single sample record.
///
/// A sample record is represented by a positive integer value, which
/// indicates how frequently was the associated line location executed.
///
/// Additionally, if the associated location contains a function call,
/// the record will hold a list of all the possible called targets. For
/// direct calls, this will be the exact function being invoked. For
/// indirect calls (function pointers, virtual table dispatch), this
/// will be a list of one or more functions.
class SampleRecord {
public:
  using CallTarget = std::pair<FunctionId, uint64_t>;
  struct CallTargetComparator {
    bool operator()(const CallTarget &LHS, const CallTarget &RHS) const {
      if (LHS.second != RHS.second)
        return LHS.second > RHS.second;

      return LHS.first < RHS.first;
    }
  };

  using SortedCallTargetSet = std::set<CallTarget, CallTargetComparator>;
  using CallTargetMap = std::unordered_map<FunctionId, uint64_t>;
  SampleRecord() = default;

  /// Increment the number of samples for this record by \p S.
  /// Optionally scale sample count \p S by \p Weight.
  ///
  /// Sample counts accumulate using saturating arithmetic, to avoid wrapping
  /// around unsigned integers.
  sampleprof_error addSamples(uint64_t S, uint64_t Weight = 1) {
    bool Overflowed;
    NumSamples = SaturatingMultiplyAdd(S, Weight, NumSamples, &Overflowed);
    return Overflowed ? sampleprof_error::counter_overflow
                      : sampleprof_error::success;
  }

  /// Decrease the number of samples for this record by \p S. Return the amout
  /// of samples actually decreased.
  uint64_t removeSamples(uint64_t S) {
    if (S > NumSamples)
      S = NumSamples;
    NumSamples -= S;
    return S;
  }

  /// Add called function \p F with samples \p S.
  /// Optionally scale sample count \p S by \p Weight.
  ///
  /// Sample counts accumulate using saturating arithmetic, to avoid wrapping
  /// around unsigned integers.
  sampleprof_error addCalledTarget(FunctionId F, uint64_t S,
                                   uint64_t Weight = 1) {
    uint64_t &TargetSamples = CallTargets[F];
    bool Overflowed;
    TargetSamples =
        SaturatingMultiplyAdd(S, Weight, TargetSamples, &Overflowed);
    return Overflowed ? sampleprof_error::counter_overflow
                      : sampleprof_error::success;
  }

  /// Remove called function from the call target map. Return the target sample
  /// count of the called function.
  uint64_t removeCalledTarget(FunctionId F) {
    uint64_t Count = 0;
    auto I = CallTargets.find(F);
    if (I != CallTargets.end()) {
      Count = I->second;
      CallTargets.erase(I);
    }
    return Count;
  }

  /// Return true if this sample record contains function calls.
  bool hasCalls() const { return !CallTargets.empty(); }

  uint64_t getSamples() const { return NumSamples; }
  const CallTargetMap &getCallTargets() const { return CallTargets; }
  const SortedCallTargetSet getSortedCallTargets() const {
    return sortCallTargets(CallTargets);
  }

  uint64_t getCallTargetSum() const {
    uint64_t Sum = 0;
    for (const auto &I : CallTargets)
      Sum += I.second;
    return Sum;
  }

  /// Sort call targets in descending order of call frequency.
  static const SortedCallTargetSet
  sortCallTargets(const CallTargetMap &Targets) {
    SortedCallTargetSet SortedTargets;
    for (const auto &[Target, Frequency] : Targets) {
      SortedTargets.emplace(Target, Frequency);
    }
    return SortedTargets;
  }

  /// Prorate call targets by a distribution factor.
  static const CallTargetMap adjustCallTargets(const CallTargetMap &Targets,
                                               float DistributionFactor) {
    CallTargetMap AdjustedTargets;
    for (const auto &[Target, Frequency] : Targets) {
      AdjustedTargets[Target] = Frequency * DistributionFactor;
    }
    return AdjustedTargets;
  }

  /// Merge the samples in \p Other into this record.
  /// Optionally scale sample counts by \p Weight.
  sampleprof_error merge(const SampleRecord &Other, uint64_t Weight = 1);
  void print(raw_ostream &OS, unsigned Indent) const;
  void dump() const;

  bool operator==(const SampleRecord &Other) const {
    return NumSamples == Other.NumSamples && CallTargets == Other.CallTargets;
  }

  bool operator!=(const SampleRecord &Other) const {
    return !(*this == Other);
  }

private:
  uint64_t NumSamples = 0;
  CallTargetMap CallTargets;
};

raw_ostream &operator<<(raw_ostream &OS, const SampleRecord &Sample);

// State of context associated with FunctionSamples
enum ContextStateMask {
  UnknownContext = 0x0,   // Profile without context
  RawContext = 0x1,       // Full context profile from input profile
  SyntheticContext = 0x2, // Synthetic context created for context promotion
  InlinedContext = 0x4,   // Profile for context that is inlined into caller
  MergedContext = 0x8     // Profile for context merged into base profile
};

// Attribute of context associated with FunctionSamples
enum ContextAttributeMask {
  ContextNone = 0x0,
  ContextWasInlined = 0x1,      // Leaf of context was inlined in previous build
  ContextShouldBeInlined = 0x2, // Leaf of context should be inlined
  ContextDuplicatedIntoBase =
      0x4, // Leaf of context is duplicated into the base profile
};

// Represents a context frame with profile function and line location
struct SampleContextFrame {
  FunctionId Func;
  LineLocation Location;

  SampleContextFrame() : Location(0, 0) {}

  SampleContextFrame(FunctionId Func, LineLocation Location)
      : Func(Func), Location(Location) {}

  bool operator==(const SampleContextFrame &That) const {
    return Location == That.Location && Func == That.Func;
  }

  bool operator!=(const SampleContextFrame &That) const {
    return !(*this == That);
  }

  std::string toString(bool OutputLineLocation) const {
    std::ostringstream OContextStr;
    OContextStr << Func.str();
    if (OutputLineLocation) {
      OContextStr << ":" << Location.LineOffset;
      if (Location.Discriminator)
        OContextStr << "." << Location.Discriminator;
    }
    return OContextStr.str();
  }

  uint64_t getHashCode() const {
    uint64_t NameHash = Func.getHashCode();
    uint64_t LocId = Location.getHashCode();
    return NameHash + (LocId << 5) + LocId;
  }
};

static inline hash_code hash_value(const SampleContextFrame &arg) {
  return arg.getHashCode();
}

using SampleContextFrameVector = SmallVector<SampleContextFrame, 1>;
using SampleContextFrames = ArrayRef<SampleContextFrame>;

struct SampleContextFrameHash {
  uint64_t operator()(const SampleContextFrameVector &S) const {
    return hash_combine_range(S.begin(), S.end());
  }
};

// Sample context for FunctionSamples. It consists of the calling context,
// the function name and context state. Internally sample context is represented
// using ArrayRef, which is also the input for constructing a `SampleContext`.
// It can accept and represent both full context string as well as context-less
// function name.
// For a CS profile, a full context vector can look like:
//    `main:3 _Z5funcAi:1 _Z8funcLeafi`
// For a base CS profile without calling context, the context vector should only
// contain the leaf frame name.
// For a non-CS profile, the context vector should be empty.
class SampleContext {
public:
  SampleContext() : State(UnknownContext), Attributes(ContextNone) {}

  SampleContext(StringRef Name)
      : Func(Name), State(UnknownContext), Attributes(ContextNone) {
        assert(!Name.empty() && "Name is empty");
      }

  SampleContext(FunctionId Func)
      : Func(Func), State(UnknownContext), Attributes(ContextNone) {}

  SampleContext(SampleContextFrames Context,
                ContextStateMask CState = RawContext)
      : Attributes(ContextNone) {
    assert(!Context.empty() && "Context is empty");
    setContext(Context, CState);
  }

  // Give a context string, decode and populate internal states like
  // Function name, Calling context and context state. Example of input
  // `ContextStr`: `[main:3 @ _Z5funcAi:1 @ _Z8funcLeafi]`
  SampleContext(StringRef ContextStr,
                std::list<SampleContextFrameVector> &CSNameTable,
                ContextStateMask CState = RawContext)
      : Attributes(ContextNone) {
    assert(!ContextStr.empty());
    // Note that `[]` wrapped input indicates a full context string, otherwise
    // it's treated as context-less function name only.
    bool HasContext = ContextStr.starts_with("[");
    if (!HasContext) {
      State = UnknownContext;
      Func = FunctionId(ContextStr);
    } else {
      CSNameTable.emplace_back();
      SampleContextFrameVector &Context = CSNameTable.back();
      createCtxVectorFromStr(ContextStr, Context);
      setContext(Context, CState);
    }
  }

  /// Create a context vector from a given context string and save it in
  /// `Context`.
  static void createCtxVectorFromStr(StringRef ContextStr,
                                     SampleContextFrameVector &Context) {
    // Remove encapsulating '[' and ']' if any
    ContextStr = ContextStr.substr(1, ContextStr.size() - 2);
    StringRef ContextRemain = ContextStr;
    StringRef ChildContext;
    FunctionId Callee;
    while (!ContextRemain.empty()) {
      auto ContextSplit = ContextRemain.split(" @ ");
      ChildContext = ContextSplit.first;
      ContextRemain = ContextSplit.second;
      LineLocation CallSiteLoc(0, 0);
      decodeContextString(ChildContext, Callee, CallSiteLoc);
      Context.emplace_back(Callee, CallSiteLoc);
    }
  }

  // Decode context string for a frame to get function name and location.
  // `ContextStr` is in the form of `FuncName:StartLine.Discriminator`.
  static void decodeContextString(StringRef ContextStr,
                                  FunctionId &Func,
                                  LineLocation &LineLoc) {
    // Get function name
    auto EntrySplit = ContextStr.split(':');
    Func = FunctionId(EntrySplit.first);

    LineLoc = {0, 0};
    if (!EntrySplit.second.empty()) {
      // Get line offset, use signed int for getAsInteger so string will
      // be parsed as signed.
      int LineOffset = 0;
      auto LocSplit = EntrySplit.second.split('.');
      LocSplit.first.getAsInteger(10, LineOffset);
      LineLoc.LineOffset = LineOffset;

      // Get discriminator
      if (!LocSplit.second.empty())
        LocSplit.second.getAsInteger(10, LineLoc.Discriminator);
    }
  }

  operator SampleContextFrames() const { return FullContext; }
  bool hasAttribute(ContextAttributeMask A) { return Attributes & (uint32_t)A; }
  void setAttribute(ContextAttributeMask A) { Attributes |= (uint32_t)A; }
  uint32_t getAllAttributes() { return Attributes; }
  void setAllAttributes(uint32_t A) { Attributes = A; }
  bool hasState(ContextStateMask S) { return State & (uint32_t)S; }
  void setState(ContextStateMask S) { State |= (uint32_t)S; }
  void clearState(ContextStateMask S) { State &= (uint32_t)~S; }
  bool hasContext() const { return State != UnknownContext; }
  bool isBaseContext() const { return FullContext.size() == 1; }
  FunctionId getFunction() const { return Func; }
  SampleContextFrames getContextFrames() const { return FullContext; }

  static std::string getContextString(SampleContextFrames Context,
                                      bool IncludeLeafLineLocation = false) {
    std::ostringstream OContextStr;
    for (uint32_t I = 0; I < Context.size(); I++) {
      if (OContextStr.str().size()) {
        OContextStr << " @ ";
      }
      OContextStr << Context[I].toString(I != Context.size() - 1 ||
                                         IncludeLeafLineLocation);
    }
    return OContextStr.str();
  }

  std::string toString() const {
    if (!hasContext())
      return Func.str();
    return getContextString(FullContext, false);
  }

  uint64_t getHashCode() const {
    if (hasContext())
      return hash_value(getContextFrames());
    return getFunction().getHashCode();
  }

  /// Set the name of the function and clear the current context.
  void setFunction(FunctionId NewFunctionID) {
    Func = NewFunctionID;
    FullContext = SampleContextFrames();
    State = UnknownContext;
  }

  void setContext(SampleContextFrames Context,
                  ContextStateMask CState = RawContext) {
    assert(CState != UnknownContext);
    FullContext = Context;
    Func = Context.back().Func;
    State = CState;
  }

  bool operator==(const SampleContext &That) const {
    return State == That.State && Func == That.Func &&
           FullContext == That.FullContext;
  }

  bool operator!=(const SampleContext &That) const { return !(*this == That); }

  bool operator<(const SampleContext &That) const {
    if (State != That.State)
      return State < That.State;

    if (!hasContext()) {
      return Func < That.Func;
    }

    uint64_t I = 0;
    while (I < std::min(FullContext.size(), That.FullContext.size())) {
      auto &Context1 = FullContext[I];
      auto &Context2 = That.FullContext[I];
      auto V = Context1.Func.compare(Context2.Func);
      if (V)
        return V < 0;
      if (Context1.Location != Context2.Location)
        return Context1.Location < Context2.Location;
      I++;
    }

    return FullContext.size() < That.FullContext.size();
  }

  struct Hash {
    uint64_t operator()(const SampleContext &Context) const {
      return Context.getHashCode();
    }
  };

  bool isPrefixOf(const SampleContext &That) const {
    auto ThisContext = FullContext;
    auto ThatContext = That.FullContext;
    if (ThatContext.size() < ThisContext.size())
      return false;
    ThatContext = ThatContext.take_front(ThisContext.size());
    // Compare Leaf frame first
    if (ThisContext.back().Func != ThatContext.back().Func)
      return false;
    // Compare leading context
    return ThisContext.drop_back() == ThatContext.drop_back();
  }

private:
  // The function associated with this context. If CS profile, this is the leaf
  // function.
  FunctionId Func;
  // Full context including calling context and leaf function name
  SampleContextFrames FullContext;
  // State of the associated sample profile
  uint32_t State;
  // Attribute of the associated sample profile
  uint32_t Attributes;
};

static inline hash_code hash_value(const SampleContext &Context) {
  return Context.getHashCode();
}

inline raw_ostream &operator<<(raw_ostream &OS, const SampleContext &Context) {
  return OS << Context.toString();
}

class FunctionSamples;
class SampleProfileReaderItaniumRemapper;

using BodySampleMap = std::map<LineLocation, SampleRecord>;
// NOTE: Using a StringMap here makes parsed profiles consume around 17% more
// memory, which is *very* significant for large profiles.
using FunctionSamplesMap = std::map<FunctionId, FunctionSamples>;
using CallsiteSampleMap = std::map<LineLocation, FunctionSamplesMap>;
using LocToLocMap =
    std::unordered_map<LineLocation, LineLocation, LineLocationHash>;

/// Representation of the samples collected for a function.
///
/// This data structure contains all the collected samples for the body
/// of a function. Each sample corresponds to a LineLocation instance
/// within the body of the function.
class FunctionSamples {
public:
  FunctionSamples() = default;

  void print(raw_ostream &OS = dbgs(), unsigned Indent = 0) const;
  void dump() const;

  sampleprof_error addTotalSamples(uint64_t Num, uint64_t Weight = 1) {
    bool Overflowed;
    TotalSamples =
        SaturatingMultiplyAdd(Num, Weight, TotalSamples, &Overflowed);
    return Overflowed ? sampleprof_error::counter_overflow
                      : sampleprof_error::success;
  }

  void removeTotalSamples(uint64_t Num) {
    if (TotalSamples < Num)
      TotalSamples = 0;
    else
      TotalSamples -= Num;
  }

  void setTotalSamples(uint64_t Num) { TotalSamples = Num; }

  void setHeadSamples(uint64_t Num) { TotalHeadSamples = Num; }

  sampleprof_error addHeadSamples(uint64_t Num, uint64_t Weight = 1) {
    bool Overflowed;
    TotalHeadSamples =
        SaturatingMultiplyAdd(Num, Weight, TotalHeadSamples, &Overflowed);
    return Overflowed ? sampleprof_error::counter_overflow
                      : sampleprof_error::success;
  }

  sampleprof_error addBodySamples(uint32_t LineOffset, uint32_t Discriminator,
                                  uint64_t Num, uint64_t Weight = 1) {
    return BodySamples[LineLocation(LineOffset, Discriminator)].addSamples(
        Num, Weight);
  }

  sampleprof_error addCalledTargetSamples(uint32_t LineOffset,
                                          uint32_t Discriminator,
                                          FunctionId Func,
                                          uint64_t Num,
                                          uint64_t Weight = 1) {
    return BodySamples[LineLocation(LineOffset, Discriminator)].addCalledTarget(
        Func, Num, Weight);
  }

  sampleprof_error addSampleRecord(LineLocation Location,
                                   const SampleRecord &SampleRecord,
                                   uint64_t Weight = 1) {
    return BodySamples[Location].merge(SampleRecord, Weight);
  }

  // Remove a call target and decrease the body sample correspondingly. Return
  // the number of body samples actually decreased.
  uint64_t removeCalledTargetAndBodySample(uint32_t LineOffset,
                                           uint32_t Discriminator,
                                           FunctionId Func) {
    uint64_t Count = 0;
    auto I = BodySamples.find(LineLocation(LineOffset, Discriminator));
    if (I != BodySamples.end()) {
      Count = I->second.removeCalledTarget(Func);
      Count = I->second.removeSamples(Count);
      if (!I->second.getSamples())
        BodySamples.erase(I);
    }
    return Count;
  }

  // Remove all call site samples for inlinees. This is needed when flattening
  // a nested profile.
  void removeAllCallsiteSamples() {
    CallsiteSamples.clear();
  }

  // Accumulate all call target samples to update the body samples.
  void updateCallsiteSamples() {
    for (auto &I : BodySamples) {
      uint64_t TargetSamples = I.second.getCallTargetSum();
      // It's possible that the body sample count can be greater than the call
      // target sum. E.g, if some call targets are external targets, they won't
      // be considered valid call targets, but the body sample count which is
      // from lbr ranges can actually include them.
      if (TargetSamples > I.second.getSamples())
        I.second.addSamples(TargetSamples - I.second.getSamples());
    }
  }

  // Accumulate all body samples to set total samples.
  void updateTotalSamples() {
    setTotalSamples(0);
    for (const auto &I : BodySamples)
      addTotalSamples(I.second.getSamples());

    for (auto &I : CallsiteSamples) {
      for (auto &CS : I.second) {
        CS.second.updateTotalSamples();
        addTotalSamples(CS.second.getTotalSamples());
      }
    }
  }

  // Set current context and all callee contexts to be synthetic.
  void setContextSynthetic() {
    Context.setState(SyntheticContext);
    for (auto &I : CallsiteSamples) {
      for (auto &CS : I.second) {
        CS.second.setContextSynthetic();
      }
    }
  }

  // Query the stale profile matching results and remap the location.
  const LineLocation &mapIRLocToProfileLoc(const LineLocation &IRLoc) const {
    // There is no remapping if the profile is not stale or the matching gives
    // the same location.
    if (!IRToProfileLocationMap)
      return IRLoc;
    const auto &ProfileLoc = IRToProfileLocationMap->find(IRLoc);
    if (ProfileLoc != IRToProfileLocationMap->end())
      return ProfileLoc->second;
    return IRLoc;
  }

  /// Return the number of samples collected at the given location.
  /// Each location is specified by \p LineOffset and \p Discriminator.
  /// If the location is not found in profile, return error.
  ErrorOr<uint64_t> findSamplesAt(uint32_t LineOffset,
                                  uint32_t Discriminator) const {
    const auto &Ret = BodySamples.find(
        mapIRLocToProfileLoc(LineLocation(LineOffset, Discriminator)));
    if (Ret == BodySamples.end())
      return std::error_code();
    return Ret->second.getSamples();
  }

  /// Returns the call target map collected at a given location.
  /// Each location is specified by \p LineOffset and \p Discriminator.
  /// If the location is not found in profile, return error.
  ErrorOr<const SampleRecord::CallTargetMap &>
  findCallTargetMapAt(uint32_t LineOffset, uint32_t Discriminator) const {
    const auto &Ret = BodySamples.find(
        mapIRLocToProfileLoc(LineLocation(LineOffset, Discriminator)));
    if (Ret == BodySamples.end())
      return std::error_code();
    return Ret->second.getCallTargets();
  }

  /// Returns the call target map collected at a given location specified by \p
  /// CallSite. If the location is not found in profile, return error.
  ErrorOr<const SampleRecord::CallTargetMap &>
  findCallTargetMapAt(const LineLocation &CallSite) const {
    const auto &Ret = BodySamples.find(mapIRLocToProfileLoc(CallSite));
    if (Ret == BodySamples.end())
      return std::error_code();
    return Ret->second.getCallTargets();
  }

  /// Return the function samples at the given callsite location.
  FunctionSamplesMap &functionSamplesAt(const LineLocation &Loc) {
    return CallsiteSamples[mapIRLocToProfileLoc(Loc)];
  }

  /// Returns the FunctionSamplesMap at the given \p Loc.
  const FunctionSamplesMap *
  findFunctionSamplesMapAt(const LineLocation &Loc) const {
    auto Iter = CallsiteSamples.find(mapIRLocToProfileLoc(Loc));
    if (Iter == CallsiteSamples.end())
      return nullptr;
    return &Iter->second;
  }

  /// Returns a pointer to FunctionSamples at the given callsite location
  /// \p Loc with callee \p CalleeName. If no callsite can be found, relax
  /// the restriction to return the FunctionSamples at callsite location
  /// \p Loc with the maximum total sample count. If \p Remapper or \p
  /// FuncNameToProfNameMap is not nullptr, use them to find FunctionSamples
  /// with equivalent name as \p CalleeName.
  const FunctionSamples *findFunctionSamplesAt(
      const LineLocation &Loc, StringRef CalleeName,
      SampleProfileReaderItaniumRemapper *Remapper,
      const HashKeyMap<std::unordered_map, FunctionId, FunctionId>
          *FuncNameToProfNameMap = nullptr) const;

  bool empty() const { return TotalSamples == 0; }

  /// Return the total number of samples collected inside the function.
  uint64_t getTotalSamples() const { return TotalSamples; }

  /// For top-level functions, return the total number of branch samples that
  /// have the function as the branch target (or 0 otherwise). This is the raw
  /// data fetched from the profile. This should be equivalent to the sample of
  /// the first instruction of the symbol. But as we directly get this info for
  /// raw profile without referring to potentially inaccurate debug info, this
  /// gives more accurate profile data and is preferred for standalone symbols.
  uint64_t getHeadSamples() const { return TotalHeadSamples; }

  /// Return an estimate of the sample count of the function entry basic block.
  /// The function can be either a standalone symbol or an inlined function.
  /// For Context-Sensitive profiles, this will prefer returning the head
  /// samples (i.e. getHeadSamples()), if non-zero. Otherwise it estimates from
  /// the function body's samples or callsite samples.
  uint64_t getHeadSamplesEstimate() const {
    if (FunctionSamples::ProfileIsCS && getHeadSamples()) {
      // For CS profile, if we already have more accurate head samples
      // counted by branch sample from caller, use them as entry samples.
      return getHeadSamples();
    }
    uint64_t Count = 0;
    // Use either BodySamples or CallsiteSamples which ever has the smaller
    // lineno.
    if (!BodySamples.empty() &&
        (CallsiteSamples.empty() ||
         BodySamples.begin()->first < CallsiteSamples.begin()->first))
      Count = BodySamples.begin()->second.getSamples();
    else if (!CallsiteSamples.empty()) {
      // An indirect callsite may be promoted to several inlined direct calls.
      // We need to get the sum of them.
      for (const auto &FuncSamples : CallsiteSamples.begin()->second)
        Count += FuncSamples.second.getHeadSamplesEstimate();
    }
    // Return at least 1 if total sample is not 0.
    return Count ? Count : TotalSamples > 0;
  }

  /// Return all the samples collected in the body of the function.
  const BodySampleMap &getBodySamples() const { return BodySamples; }

  /// Return all the callsite samples collected in the body of the function.
  const CallsiteSampleMap &getCallsiteSamples() const {
    return CallsiteSamples;
  }

  /// Return the maximum of sample counts in a function body. When SkipCallSite
  /// is false, which is the default, the return count includes samples in the
  /// inlined functions. When SkipCallSite is true, the return count only
  /// considers the body samples.
  uint64_t getMaxCountInside(bool SkipCallSite = false) const {
    uint64_t MaxCount = 0;
    for (const auto &L : getBodySamples())
      MaxCount = std::max(MaxCount, L.second.getSamples());
    if (SkipCallSite)
      return MaxCount;
    for (const auto &C : getCallsiteSamples())
      for (const FunctionSamplesMap::value_type &F : C.second)
        MaxCount = std::max(MaxCount, F.second.getMaxCountInside());
    return MaxCount;
  }

  /// Merge the samples in \p Other into this one.
  /// Optionally scale samples by \p Weight.
  sampleprof_error merge(const FunctionSamples &Other, uint64_t Weight = 1) {
    sampleprof_error Result = sampleprof_error::success;
    if (!GUIDToFuncNameMap)
      GUIDToFuncNameMap = Other.GUIDToFuncNameMap;
    if (Context.getFunction().empty())
      Context = Other.getContext();
    if (FunctionHash == 0) {
      // Set the function hash code for the target profile.
      FunctionHash = Other.getFunctionHash();
    } else if (FunctionHash != Other.getFunctionHash()) {
      // The two profiles coming with different valid hash codes indicates
      // either:
      // 1. They are same-named static functions from different compilation
      // units (without using -unique-internal-linkage-names), or
      // 2. They are really the same function but from different compilations.
      // Let's bail out in either case for now, which means one profile is
      // dropped.
      return sampleprof_error::hash_mismatch;
    }

    mergeSampleProfErrors(Result,
                          addTotalSamples(Other.getTotalSamples(), Weight));
    mergeSampleProfErrors(Result,
                          addHeadSamples(Other.getHeadSamples(), Weight));
    for (const auto &I : Other.getBodySamples()) {
      const LineLocation &Loc = I.first;
      const SampleRecord &Rec = I.second;
      mergeSampleProfErrors(Result, BodySamples[Loc].merge(Rec, Weight));
    }
    for (const auto &I : Other.getCallsiteSamples()) {
      const LineLocation &Loc = I.first;
      FunctionSamplesMap &FSMap = functionSamplesAt(Loc);
      for (const auto &Rec : I.second)
        mergeSampleProfErrors(Result,
                              FSMap[Rec.first].merge(Rec.second, Weight));
    }
    return Result;
  }

  /// Recursively traverses all children, if the total sample count of the
  /// corresponding function is no less than \p Threshold, add its corresponding
  /// GUID to \p S. Also traverse the BodySamples to add hot CallTarget's GUID
  /// to \p S.
  void findInlinedFunctions(DenseSet<GlobalValue::GUID> &S,
                            const HashKeyMap<std::unordered_map, FunctionId,
                                             Function *>  &SymbolMap,
                            uint64_t Threshold) const {
    if (TotalSamples <= Threshold)
      return;
    auto IsDeclaration = [](const Function *F) {
      return !F || F->isDeclaration();
    };
    if (IsDeclaration(SymbolMap.lookup(getFunction()))) {
      // Add to the import list only when it's defined out of module.
      S.insert(getGUID());
    }
    // Import hot CallTargets, which may not be available in IR because full
    // profile annotation cannot be done until backend compilation in ThinLTO.
    for (const auto &BS : BodySamples)
      for (const auto &TS : BS.second.getCallTargets())
        if (TS.second > Threshold) {
          const Function *Callee = SymbolMap.lookup(TS.first);
          if (IsDeclaration(Callee))
            S.insert(TS.first.getHashCode());
        }
    for (const auto &CS : CallsiteSamples)
      for (const auto &NameFS : CS.second)
        NameFS.second.findInlinedFunctions(S, SymbolMap, Threshold);
  }

  /// Set the name of the function.
  void setFunction(FunctionId NewFunctionID) {
    Context.setFunction(NewFunctionID);
  }

  /// Return the function name.
  FunctionId getFunction() const { return Context.getFunction(); }

  /// Return the original function name.
  StringRef getFuncName() const { return getFuncName(getFunction()); }

  void setFunctionHash(uint64_t Hash) { FunctionHash = Hash; }

  uint64_t getFunctionHash() const { return FunctionHash; }

  void setIRToProfileLocationMap(const LocToLocMap *LTLM) {
    assert(IRToProfileLocationMap == nullptr && "this should be set only once");
    IRToProfileLocationMap = LTLM;
  }

  /// Return the canonical name for a function, taking into account
  /// suffix elision policy attributes.
  static StringRef getCanonicalFnName(const Function &F) {
    const char *AttrName = "sample-profile-suffix-elision-policy";
    auto Attr = F.getFnAttribute(AttrName).getValueAsString();
    return getCanonicalFnName(F.getName(), Attr);
  }

  /// Name suffixes which canonicalization should handle to avoid
  /// profile mismatch.
  static constexpr const char *LLVMSuffix = ".llvm.";
  static constexpr const char *PartSuffix = ".part.";
  static constexpr const char *UniqSuffix = ".__uniq.";

  static StringRef getCanonicalFnName(StringRef FnName,
                                      StringRef Attr = "selected") {
    // Note the sequence of the suffixes in the knownSuffixes array matters.
    // If suffix "A" is appended after the suffix "B", "A" should be in front
    // of "B" in knownSuffixes.
    const char *KnownSuffixes[] = {LLVMSuffix, PartSuffix, UniqSuffix};
    if (Attr == "" || Attr == "all")
      return FnName.split('.').first;
    if (Attr == "selected") {
      StringRef Cand(FnName);
      for (const auto &Suf : KnownSuffixes) {
        StringRef Suffix(Suf);
        // If the profile contains ".__uniq." suffix, don't strip the
        // suffix for names in the IR.
        if (Suffix == UniqSuffix && FunctionSamples::HasUniqSuffix)
          continue;
        auto It = Cand.rfind(Suffix);
        if (It == StringRef::npos)
          continue;
        auto Dit = Cand.rfind('.');
        if (Dit == It + Suffix.size() - 1)
          Cand = Cand.substr(0, It);
      }
      return Cand;
    }
    if (Attr == "none")
      return FnName;
    assert(false && "internal error: unknown suffix elision policy");
    return FnName;
  }

  /// Translate \p Func into its original name.
  /// When profile doesn't use MD5, \p Func needs no translation.
  /// When profile uses MD5, \p Func in current FunctionSamples
  /// is actually GUID of the original function name. getFuncName will
  /// translate \p Func in current FunctionSamples into its original name
  /// by looking up in the function map GUIDToFuncNameMap.
  /// If the original name doesn't exist in the map, return empty StringRef.
  StringRef getFuncName(FunctionId Func) const {
    if (!UseMD5)
      return Func.stringRef();

    assert(GUIDToFuncNameMap && "GUIDToFuncNameMap needs to be populated first");
    return GUIDToFuncNameMap->lookup(Func.getHashCode());
  }

  /// Returns the line offset to the start line of the subprogram.
  /// We assume that a single function will not exceed 65535 LOC.
  static unsigned getOffset(const DILocation *DIL);

  /// Returns a unique call site identifier for a given debug location of a call
  /// instruction. This is wrapper of two scenarios, the probe-based profile and
  /// regular profile, to hide implementation details from the sample loader and
  /// the context tracker.
  static LineLocation getCallSiteIdentifier(const DILocation *DIL,
                                            bool ProfileIsFS = false);

  /// Returns a unique hash code for a combination of a callsite location and
  /// the callee function name.
  /// Guarantee MD5 and non-MD5 representation of the same function results in
  /// the same hash.
  static uint64_t getCallSiteHash(FunctionId Callee,
                                  const LineLocation &Callsite) {
    return SampleContextFrame(Callee, Callsite).getHashCode();
  }

  /// Get the FunctionSamples of the inline instance where DIL originates
  /// from.
  ///
  /// The FunctionSamples of the instruction (Machine or IR) associated to
  /// \p DIL is the inlined instance in which that instruction is coming from.
  /// We traverse the inline stack of that instruction, and match it with the
  /// tree nodes in the profile.
  ///
  /// \returns the FunctionSamples pointer to the inlined instance.
  /// If \p Remapper or \p FuncNameToProfNameMap is not nullptr, it will be used
  /// to find matching FunctionSamples with not exactly the same but equivalent
  /// name.
  const FunctionSamples *findFunctionSamples(
      const DILocation *DIL,
      SampleProfileReaderItaniumRemapper *Remapper = nullptr,
      const HashKeyMap<std::unordered_map, FunctionId, FunctionId>
          *FuncNameToProfNameMap = nullptr) const;

  static bool ProfileIsProbeBased;

  static bool ProfileIsCS;

  static bool ProfileIsPreInlined;

  SampleContext &getContext() const { return Context; }

  void setContext(const SampleContext &FContext) { Context = FContext; }

  /// Whether the profile uses MD5 to represent string.
  static bool UseMD5;

  /// Whether the profile contains any ".__uniq." suffix in a name.
  static bool HasUniqSuffix;

  /// If this profile uses flow sensitive discriminators.
  static bool ProfileIsFS;

  /// GUIDToFuncNameMap saves the mapping from GUID to the symbol name, for
  /// all the function symbols defined or declared in current module.
  DenseMap<uint64_t, StringRef> *GUIDToFuncNameMap = nullptr;

  /// Return the GUID of the context's name. If the context is already using
  /// MD5, don't hash it again.
  uint64_t getGUID() const {
    return getFunction().getHashCode();
  }

  // Find all the names in the current FunctionSamples including names in
  // all the inline instances and names of call targets.
  void findAllNames(DenseSet<FunctionId> &NameSet) const;

  bool operator==(const FunctionSamples &Other) const {
    return (GUIDToFuncNameMap == Other.GUIDToFuncNameMap ||
            (GUIDToFuncNameMap && Other.GUIDToFuncNameMap &&
             *GUIDToFuncNameMap == *Other.GUIDToFuncNameMap)) &&
           FunctionHash == Other.FunctionHash && Context == Other.Context &&
           TotalSamples == Other.TotalSamples &&
           TotalHeadSamples == Other.TotalHeadSamples &&
           BodySamples == Other.BodySamples &&
           CallsiteSamples == Other.CallsiteSamples;
  }

  bool operator!=(const FunctionSamples &Other) const {
    return !(*this == Other);
  }

private:
  /// CFG hash value for the function.
  uint64_t FunctionHash = 0;

  /// Calling context for function profile
  mutable SampleContext Context;

  /// Total number of samples collected inside this function.
  ///
  /// Samples are cumulative, they include all the samples collected
  /// inside this function and all its inlined callees.
  uint64_t TotalSamples = 0;

  /// Total number of samples collected at the head of the function.
  /// This is an approximation of the number of calls made to this function
  /// at runtime.
  uint64_t TotalHeadSamples = 0;

  /// Map instruction locations to collected samples.
  ///
  /// Each entry in this map contains the number of samples
  /// collected at the corresponding line offset. All line locations
  /// are an offset from the start of the function.
  BodySampleMap BodySamples;

  /// Map call sites to collected samples for the called function.
  ///
  /// Each entry in this map corresponds to all the samples
  /// collected for the inlined function call at the given
  /// location. For example, given:
  ///
  ///     void foo() {
  ///  1    bar();
  ///  ...
  ///  8    baz();
  ///     }
  ///
  /// If the bar() and baz() calls were inlined inside foo(), this
  /// map will contain two entries.  One for all the samples collected
  /// in the call to bar() at line offset 1, the other for all the samples
  /// collected in the call to baz() at line offset 8.
  CallsiteSampleMap CallsiteSamples;

  /// IR to profile location map generated by stale profile matching.
  ///
  /// Each entry is a mapping from the location on current build to the matched
  /// location in the "stale" profile. For example:
  ///   Profiled source code:
  ///      void foo() {
  ///   1    bar();
  ///      }
  ///
  ///   Current source code:
  ///      void foo() {
  ///   1    // Code change
  ///   2    bar();
  ///      }
  /// Supposing the stale profile matching algorithm generated the mapping [2 ->
  /// 1], the profile query using the location of bar on the IR which is 2 will
  /// be remapped to 1 and find the location of bar in the profile.
  const LocToLocMap *IRToProfileLocationMap = nullptr;
};

/// Get the proper representation of a string according to whether the
/// current Format uses MD5 to represent the string.
static inline FunctionId getRepInFormat(StringRef Name) {
  if (Name.empty() || !FunctionSamples::UseMD5)
    return FunctionId(Name);
  return FunctionId(Function::getGUID(Name));
}

raw_ostream &operator<<(raw_ostream &OS, const FunctionSamples &FS);

/// This class provides operator overloads to the map container using MD5 as the
/// key type, so that existing code can still work in most cases using
/// SampleContext as key.
/// Note: when populating container, make sure to assign the SampleContext to
/// the mapped value immediately because the key no longer holds it.
class SampleProfileMap
    : public HashKeyMap<std::unordered_map, SampleContext, FunctionSamples> {
public:
  // Convenience method because this is being used in many places. Set the
  // FunctionSamples' context if its newly inserted.
  mapped_type &create(const SampleContext &Ctx) {
    auto Ret = try_emplace(Ctx, FunctionSamples());
    if (Ret.second)
      Ret.first->second.setContext(Ctx);
    return Ret.first->second;
  }

  iterator find(const SampleContext &Ctx) {
    return HashKeyMap<std::unordered_map, SampleContext, FunctionSamples>::find(
        Ctx);
  }

  const_iterator find(const SampleContext &Ctx) const {
    return HashKeyMap<std::unordered_map, SampleContext, FunctionSamples>::find(
        Ctx);
  }

  size_t erase(const SampleContext &Ctx) {
    return HashKeyMap<std::unordered_map, SampleContext, FunctionSamples>::
        erase(Ctx);
  }

  size_t erase(const key_type &Key) { return base_type::erase(Key); }

  iterator erase(iterator It) { return base_type::erase(It); }
};

using NameFunctionSamples = std::pair<hash_code, const FunctionSamples *>;

void sortFuncProfiles(const SampleProfileMap &ProfileMap,
                      std::vector<NameFunctionSamples> &SortedProfiles);

/// Sort a LocationT->SampleT map by LocationT.
///
/// It produces a sorted list of <LocationT, SampleT> records by ascending
/// order of LocationT.
template <class LocationT, class SampleT> class SampleSorter {
public:
  using SamplesWithLoc = std::pair<const LocationT, SampleT>;
  using SamplesWithLocList = SmallVector<const SamplesWithLoc *, 20>;

  SampleSorter(const std::map<LocationT, SampleT> &Samples) {
    for (const auto &I : Samples)
      V.push_back(&I);
    llvm::stable_sort(V, [](const SamplesWithLoc *A, const SamplesWithLoc *B) {
      return A->first < B->first;
    });
  }

  const SamplesWithLocList &get() const { return V; }

private:
  SamplesWithLocList V;
};

/// SampleContextTrimmer impelements helper functions to trim, merge cold
/// context profiles. It also supports context profile canonicalization to make
/// sure ProfileMap's key is consistent with FunctionSample's name/context.
class SampleContextTrimmer {
public:
  SampleContextTrimmer(SampleProfileMap &Profiles) : ProfileMap(Profiles){};
  // Trim and merge cold context profile when requested. TrimBaseProfileOnly
  // should only be effective when TrimColdContext is true. On top of
  // TrimColdContext, TrimBaseProfileOnly can be used to specify to trim all
  // cold profiles or only cold base profiles. Trimming base profiles only is
  // mainly to honor the preinliner decsion. Note that when MergeColdContext is
  // true, preinliner decsion is not honored anyway so TrimBaseProfileOnly will
  // be ignored.
  void trimAndMergeColdContextProfiles(uint64_t ColdCountThreshold,
                                       bool TrimColdContext,
                                       bool MergeColdContext,
                                       uint32_t ColdContextFrameLength,
                                       bool TrimBaseProfileOnly);

private:
  SampleProfileMap &ProfileMap;
};

/// Helper class for profile conversion.
///
/// It supports full context-sensitive profile to nested profile conversion,
/// nested profile to flatten profile conversion, etc.
class ProfileConverter {
public:
  ProfileConverter(SampleProfileMap &Profiles);
  // Convert a full context-sensitive flat sample profile into a nested sample
  // profile.
  void convertCSProfiles();
  struct FrameNode {
    FrameNode(FunctionId FName = FunctionId(),
              FunctionSamples *FSamples = nullptr,
              LineLocation CallLoc = {0, 0})
        : FuncName(FName), FuncSamples(FSamples), CallSiteLoc(CallLoc){};

    // Map line+discriminator location to child frame
    std::map<uint64_t, FrameNode> AllChildFrames;
    // Function name for current frame
    FunctionId FuncName;
    // Function Samples for current frame
    FunctionSamples *FuncSamples;
    // Callsite location in parent context
    LineLocation CallSiteLoc;

    FrameNode *getOrCreateChildFrame(const LineLocation &CallSite,
                                     FunctionId CalleeName);
  };

  static void flattenProfile(SampleProfileMap &ProfileMap,
                             bool ProfileIsCS = false) {
    SampleProfileMap TmpProfiles;
    flattenProfile(ProfileMap, TmpProfiles, ProfileIsCS);
    ProfileMap = std::move(TmpProfiles);
  }

  static void flattenProfile(const SampleProfileMap &InputProfiles,
                             SampleProfileMap &OutputProfiles,
                             bool ProfileIsCS = false) {
    if (ProfileIsCS) {
      for (const auto &I : InputProfiles) {
        // Retain the profile name and clear the full context for each function
        // profile.
        FunctionSamples &FS = OutputProfiles.create(I.second.getFunction());
        FS.merge(I.second);
      }
    } else {
      for (const auto &I : InputProfiles)
        flattenNestedProfile(OutputProfiles, I.second);
    }
  }

private:
  static void flattenNestedProfile(SampleProfileMap &OutputProfiles,
                                   const FunctionSamples &FS) {
    // To retain the context, checksum, attributes of the original profile, make
    // a copy of it if no profile is found.
    SampleContext &Context = FS.getContext();
    auto Ret = OutputProfiles.try_emplace(Context, FS);
    FunctionSamples &Profile = Ret.first->second;
    if (Ret.second) {
      // Clear nested inlinees' samples for the flattened copy. These inlinees
      // will have their own top-level entries after flattening.
      Profile.removeAllCallsiteSamples();
      // We recompute TotalSamples later, so here set to zero.
      Profile.setTotalSamples(0);
    } else {
      for (const auto &[LineLocation, SampleRecord] : FS.getBodySamples()) {
        Profile.addSampleRecord(LineLocation, SampleRecord);
      }
    }

    assert(Profile.getCallsiteSamples().empty() &&
           "There should be no inlinees' profiles after flattening.");

    // TotalSamples might not be equal to the sum of all samples from
    // BodySamples and CallsiteSamples. So here we use "TotalSamples =
    // Original_TotalSamples - All_of_Callsite_TotalSamples +
    // All_of_Callsite_HeadSamples" to compute the new TotalSamples.
    uint64_t TotalSamples = FS.getTotalSamples();

    for (const auto &I : FS.getCallsiteSamples()) {
      for (const auto &Callee : I.second) {
        const auto &CalleeProfile = Callee.second;
        // Add body sample.
        Profile.addBodySamples(I.first.LineOffset, I.first.Discriminator,
                               CalleeProfile.getHeadSamplesEstimate());
        // Add callsite sample.
        Profile.addCalledTargetSamples(
            I.first.LineOffset, I.first.Discriminator,
            CalleeProfile.getFunction(),
            CalleeProfile.getHeadSamplesEstimate());
        // Update total samples.
        TotalSamples = TotalSamples >= CalleeProfile.getTotalSamples()
                           ? TotalSamples - CalleeProfile.getTotalSamples()
                           : 0;
        TotalSamples += CalleeProfile.getHeadSamplesEstimate();
        // Recursively convert callee profile.
        flattenNestedProfile(OutputProfiles, CalleeProfile);
      }
    }
    Profile.addTotalSamples(TotalSamples);

    Profile.setHeadSamples(Profile.getHeadSamplesEstimate());
  }

  // Nest all children profiles into the profile of Node.
  void convertCSProfiles(FrameNode &Node);
  FrameNode *getOrCreateContextPath(const SampleContext &Context);

  SampleProfileMap &ProfileMap;
  FrameNode RootFrame;
};

/// ProfileSymbolList records the list of function symbols shown up
/// in the binary used to generate the profile. It is useful to
/// to discriminate a function being so cold as not to shown up
/// in the profile and a function newly added.
class ProfileSymbolList {
public:
  /// copy indicates whether we need to copy the underlying memory
  /// for the input Name.
  void add(StringRef Name, bool Copy = false) {
    if (!Copy) {
      Syms.insert(Name);
      return;
    }
    Syms.insert(Name.copy(Allocator));
  }

  bool contains(StringRef Name) { return Syms.count(Name); }

  void merge(const ProfileSymbolList &List) {
    for (auto Sym : List.Syms)
      add(Sym, true);
  }

  unsigned size() { return Syms.size(); }

  void setToCompress(bool TC) { ToCompress = TC; }
  bool toCompress() { return ToCompress; }

  std::error_code read(const uint8_t *Data, uint64_t ListSize);
  std::error_code write(raw_ostream &OS);
  void dump(raw_ostream &OS = dbgs()) const;

private:
  // Determine whether or not to compress the symbol list when
  // writing it into profile. The variable is unused when the symbol
  // list is read from an existing profile.
  bool ToCompress = false;
  DenseSet<StringRef> Syms;
  BumpPtrAllocator Allocator;
};

} // end namespace sampleprof

using namespace sampleprof;
// Provide DenseMapInfo for SampleContext.
template <> struct DenseMapInfo<SampleContext> {
  static inline SampleContext getEmptyKey() { return SampleContext(); }

  static inline SampleContext getTombstoneKey() {
    return SampleContext(FunctionId(~1ULL));
  }

  static unsigned getHashValue(const SampleContext &Val) {
    return Val.getHashCode();
  }

  static bool isEqual(const SampleContext &LHS, const SampleContext &RHS) {
    return LHS == RHS;
  }
};

// Prepend "__uniq" before the hash for tools like profilers to understand
// that this symbol is of internal linkage type.  The "__uniq" is the
// pre-determined prefix that is used to tell tools that this symbol was
// created with -funique-internal-linkage-symbols and the tools can strip or
// keep the prefix as needed.
inline std::string getUniqueInternalLinkagePostfix(const StringRef &FName) {
  llvm::MD5 Md5;
  Md5.update(FName);
  llvm::MD5::MD5Result R;
  Md5.final(R);
  SmallString<32> Str;
  llvm::MD5::stringifyResult(R, Str);
  // Convert MD5hash to Decimal. Demangler suffixes can either contain
  // numbers or characters but not both.
  llvm::APInt IntHash(128, Str.str(), 16);
  return toString(IntHash, /* Radix = */ 10, /* Signed = */ false)
      .insert(0, FunctionSamples::UniqSuffix);
}

} // end namespace llvm

#endif // LLVM_PROFILEDATA_SAMPLEPROF_H

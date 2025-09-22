//===- llvm/ModuleSummaryIndex.h - Module Summary Index ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// @file
/// ModuleSummaryIndex.h This file contains the declarations the classes that
///  hold the module index and summary for function importing.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_MODULESUMMARYINDEX_H
#define LLVM_IR_MODULESUMMARYINDEX_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/ScaledNumber.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace llvm {

template <class GraphType> struct GraphTraits;

namespace yaml {

template <typename T> struct MappingTraits;

} // end namespace yaml

/// Class to accumulate and hold information about a callee.
struct CalleeInfo {
  enum class HotnessType : uint8_t {
    Unknown = 0,
    Cold = 1,
    None = 2,
    Hot = 3,
    Critical = 4
  };

  // The size of the bit-field might need to be adjusted if more values are
  // added to HotnessType enum.
  uint32_t Hotness : 3;

  // True if at least one of the calls to the callee is a tail call.
  bool HasTailCall : 1;

  /// The value stored in RelBlockFreq has to be interpreted as the digits of
  /// a scaled number with a scale of \p -ScaleShift.
  static constexpr unsigned RelBlockFreqBits = 28;
  uint32_t RelBlockFreq : RelBlockFreqBits;
  static constexpr int32_t ScaleShift = 8;
  static constexpr uint64_t MaxRelBlockFreq = (1 << RelBlockFreqBits) - 1;

  CalleeInfo()
      : Hotness(static_cast<uint32_t>(HotnessType::Unknown)),
        HasTailCall(false), RelBlockFreq(0) {}
  explicit CalleeInfo(HotnessType Hotness, bool HasTC, uint64_t RelBF)
      : Hotness(static_cast<uint32_t>(Hotness)), HasTailCall(HasTC),
        RelBlockFreq(RelBF) {}

  void updateHotness(const HotnessType OtherHotness) {
    Hotness = std::max(Hotness, static_cast<uint32_t>(OtherHotness));
  }

  bool hasTailCall() const { return HasTailCall; }

  void setHasTailCall(const bool HasTC) { HasTailCall = HasTC; }

  HotnessType getHotness() const { return HotnessType(Hotness); }

  /// Update \p RelBlockFreq from \p BlockFreq and \p EntryFreq
  ///
  /// BlockFreq is divided by EntryFreq and added to RelBlockFreq. To represent
  /// fractional values, the result is represented as a fixed point number with
  /// scale of -ScaleShift.
  void updateRelBlockFreq(uint64_t BlockFreq, uint64_t EntryFreq) {
    if (EntryFreq == 0)
      return;
    using Scaled64 = ScaledNumber<uint64_t>;
    Scaled64 Temp(BlockFreq, ScaleShift);
    Temp /= Scaled64::get(EntryFreq);

    uint64_t Sum =
        SaturatingAdd<uint64_t>(Temp.toInt<uint64_t>(), RelBlockFreq);
    Sum = std::min(Sum, uint64_t(MaxRelBlockFreq));
    RelBlockFreq = static_cast<uint32_t>(Sum);
  }
};

inline const char *getHotnessName(CalleeInfo::HotnessType HT) {
  switch (HT) {
  case CalleeInfo::HotnessType::Unknown:
    return "unknown";
  case CalleeInfo::HotnessType::Cold:
    return "cold";
  case CalleeInfo::HotnessType::None:
    return "none";
  case CalleeInfo::HotnessType::Hot:
    return "hot";
  case CalleeInfo::HotnessType::Critical:
    return "critical";
  }
  llvm_unreachable("invalid hotness");
}

class GlobalValueSummary;

using GlobalValueSummaryList = std::vector<std::unique_ptr<GlobalValueSummary>>;

struct alignas(8) GlobalValueSummaryInfo {
  union NameOrGV {
    NameOrGV(bool HaveGVs) {
      if (HaveGVs)
        GV = nullptr;
      else
        Name = "";
    }

    /// The GlobalValue corresponding to this summary. This is only used in
    /// per-module summaries and when the IR is available. E.g. when module
    /// analysis is being run, or when parsing both the IR and the summary
    /// from assembly.
    const GlobalValue *GV;

    /// Summary string representation. This StringRef points to BC module
    /// string table and is valid until module data is stored in memory.
    /// This is guaranteed to happen until runThinLTOBackend function is
    /// called, so it is safe to use this field during thin link. This field
    /// is only valid if summary index was loaded from BC file.
    StringRef Name;
  } U;

  inline GlobalValueSummaryInfo(bool HaveGVs);

  /// List of global value summary structures for a particular value held
  /// in the GlobalValueMap. Requires a vector in the case of multiple
  /// COMDAT values of the same name.
  GlobalValueSummaryList SummaryList;
};

/// Map from global value GUID to corresponding summary structures. Use a
/// std::map rather than a DenseMap so that pointers to the map's value_type
/// (which are used by ValueInfo) are not invalidated by insertion. Also it will
/// likely incur less overhead, as the value type is not very small and the size
/// of the map is unknown, resulting in inefficiencies due to repeated
/// insertions and resizing.
using GlobalValueSummaryMapTy =
    std::map<GlobalValue::GUID, GlobalValueSummaryInfo>;

/// Struct that holds a reference to a particular GUID in a global value
/// summary.
struct ValueInfo {
  enum Flags { HaveGV = 1, ReadOnly = 2, WriteOnly = 4 };
  PointerIntPair<const GlobalValueSummaryMapTy::value_type *, 3, int>
      RefAndFlags;

  ValueInfo() = default;
  ValueInfo(bool HaveGVs, const GlobalValueSummaryMapTy::value_type *R) {
    RefAndFlags.setPointer(R);
    RefAndFlags.setInt(HaveGVs);
  }

  explicit operator bool() const { return getRef(); }

  GlobalValue::GUID getGUID() const { return getRef()->first; }
  const GlobalValue *getValue() const {
    assert(haveGVs());
    return getRef()->second.U.GV;
  }

  ArrayRef<std::unique_ptr<GlobalValueSummary>> getSummaryList() const {
    return getRef()->second.SummaryList;
  }

  StringRef name() const {
    return haveGVs() ? getRef()->second.U.GV->getName()
                     : getRef()->second.U.Name;
  }

  bool haveGVs() const { return RefAndFlags.getInt() & HaveGV; }
  bool isReadOnly() const {
    assert(isValidAccessSpecifier());
    return RefAndFlags.getInt() & ReadOnly;
  }
  bool isWriteOnly() const {
    assert(isValidAccessSpecifier());
    return RefAndFlags.getInt() & WriteOnly;
  }
  unsigned getAccessSpecifier() const {
    assert(isValidAccessSpecifier());
    return RefAndFlags.getInt() & (ReadOnly | WriteOnly);
  }
  bool isValidAccessSpecifier() const {
    unsigned BadAccessMask = ReadOnly | WriteOnly;
    return (RefAndFlags.getInt() & BadAccessMask) != BadAccessMask;
  }
  void setReadOnly() {
    // We expect ro/wo attribute to set only once during
    // ValueInfo lifetime.
    assert(getAccessSpecifier() == 0);
    RefAndFlags.setInt(RefAndFlags.getInt() | ReadOnly);
  }
  void setWriteOnly() {
    assert(getAccessSpecifier() == 0);
    RefAndFlags.setInt(RefAndFlags.getInt() | WriteOnly);
  }

  const GlobalValueSummaryMapTy::value_type *getRef() const {
    return RefAndFlags.getPointer();
  }

  /// Returns the most constraining visibility among summaries. The
  /// visibilities, ordered from least to most constraining, are: default,
  /// protected and hidden.
  GlobalValue::VisibilityTypes getELFVisibility() const;

  /// Checks if all summaries are DSO local (have the flag set). When DSOLocal
  /// propagation has been done, set the parameter to enable fast check.
  bool isDSOLocal(bool WithDSOLocalPropagation = false) const;

  /// Checks if all copies are eligible for auto-hiding (have flag set).
  bool canAutoHide() const;
};

inline raw_ostream &operator<<(raw_ostream &OS, const ValueInfo &VI) {
  OS << VI.getGUID();
  if (!VI.name().empty())
    OS << " (" << VI.name() << ")";
  return OS;
}

inline bool operator==(const ValueInfo &A, const ValueInfo &B) {
  assert(A.getRef() && B.getRef() &&
         "Need ValueInfo with non-null Ref for comparison");
  return A.getRef() == B.getRef();
}

inline bool operator!=(const ValueInfo &A, const ValueInfo &B) {
  assert(A.getRef() && B.getRef() &&
         "Need ValueInfo with non-null Ref for comparison");
  return A.getRef() != B.getRef();
}

inline bool operator<(const ValueInfo &A, const ValueInfo &B) {
  assert(A.getRef() && B.getRef() &&
         "Need ValueInfo with non-null Ref to compare GUIDs");
  return A.getGUID() < B.getGUID();
}

template <> struct DenseMapInfo<ValueInfo> {
  static inline ValueInfo getEmptyKey() {
    return ValueInfo(false, (GlobalValueSummaryMapTy::value_type *)-8);
  }

  static inline ValueInfo getTombstoneKey() {
    return ValueInfo(false, (GlobalValueSummaryMapTy::value_type *)-16);
  }

  static inline bool isSpecialKey(ValueInfo V) {
    return V == getTombstoneKey() || V == getEmptyKey();
  }

  static bool isEqual(ValueInfo L, ValueInfo R) {
    // We are not supposed to mix ValueInfo(s) with different HaveGVs flag
    // in a same container.
    assert(isSpecialKey(L) || isSpecialKey(R) || (L.haveGVs() == R.haveGVs()));
    return L.getRef() == R.getRef();
  }
  static unsigned getHashValue(ValueInfo I) { return (uintptr_t)I.getRef(); }
};

/// Summary of memprof callsite metadata.
struct CallsiteInfo {
  // Actual callee function.
  ValueInfo Callee;

  // Used to record whole program analysis cloning decisions.
  // The ThinLTO backend will need to create as many clones as there are entries
  // in the vector (it is expected and should be confirmed that all such
  // summaries in the same FunctionSummary have the same number of entries).
  // Each index records version info for the corresponding clone of this
  // function. The value is the callee clone it calls (becomes the appended
  // suffix id). Index 0 is the original version, and a value of 0 calls the
  // original callee.
  SmallVector<unsigned> Clones{0};

  // Represents stack ids in this context, recorded as indices into the
  // StackIds vector in the summary index, which in turn holds the full 64-bit
  // stack ids. This reduces memory as there are in practice far fewer unique
  // stack ids than stack id references.
  SmallVector<unsigned> StackIdIndices;

  CallsiteInfo(ValueInfo Callee, SmallVector<unsigned> StackIdIndices)
      : Callee(Callee), StackIdIndices(std::move(StackIdIndices)) {}
  CallsiteInfo(ValueInfo Callee, SmallVector<unsigned> Clones,
               SmallVector<unsigned> StackIdIndices)
      : Callee(Callee), Clones(std::move(Clones)),
        StackIdIndices(std::move(StackIdIndices)) {}
};

inline raw_ostream &operator<<(raw_ostream &OS, const CallsiteInfo &SNI) {
  OS << "Callee: " << SNI.Callee;
  bool First = true;
  OS << " Clones: ";
  for (auto V : SNI.Clones) {
    if (!First)
      OS << ", ";
    First = false;
    OS << V;
  }
  First = true;
  OS << " StackIds: ";
  for (auto Id : SNI.StackIdIndices) {
    if (!First)
      OS << ", ";
    First = false;
    OS << Id;
  }
  return OS;
}

// Allocation type assigned to an allocation reached by a given context.
// More can be added, now this is cold, notcold and hot.
// Values should be powers of two so that they can be ORed, in particular to
// track allocations that have different behavior with different calling
// contexts.
enum class AllocationType : uint8_t {
  None = 0,
  NotCold = 1,
  Cold = 2,
  Hot = 4,
  All = 7 // This should always be set to the OR of all values.
};

/// Summary of a single MIB in a memprof metadata on allocations.
struct MIBInfo {
  // The allocation type for this profiled context.
  AllocationType AllocType;

  // Represents stack ids in this context, recorded as indices into the
  // StackIds vector in the summary index, which in turn holds the full 64-bit
  // stack ids. This reduces memory as there are in practice far fewer unique
  // stack ids than stack id references.
  SmallVector<unsigned> StackIdIndices;

  MIBInfo(AllocationType AllocType, SmallVector<unsigned> StackIdIndices)
      : AllocType(AllocType), StackIdIndices(std::move(StackIdIndices)) {}
};

inline raw_ostream &operator<<(raw_ostream &OS, const MIBInfo &MIB) {
  OS << "AllocType " << (unsigned)MIB.AllocType;
  bool First = true;
  OS << " StackIds: ";
  for (auto Id : MIB.StackIdIndices) {
    if (!First)
      OS << ", ";
    First = false;
    OS << Id;
  }
  return OS;
}

/// Summary of memprof metadata on allocations.
struct AllocInfo {
  // Used to record whole program analysis cloning decisions.
  // The ThinLTO backend will need to create as many clones as there are entries
  // in the vector (it is expected and should be confirmed that all such
  // summaries in the same FunctionSummary have the same number of entries).
  // Each index records version info for the corresponding clone of this
  // function. The value is the allocation type of the corresponding allocation.
  // Index 0 is the original version. Before cloning, index 0 may have more than
  // one allocation type.
  SmallVector<uint8_t> Versions;

  // Vector of MIBs in this memprof metadata.
  std::vector<MIBInfo> MIBs;

  // If requested, keep track of total profiled sizes for each MIB. This will be
  // a vector of the same length and order as the MIBs vector, if non-empty.
  std::vector<uint64_t> TotalSizes;

  AllocInfo(std::vector<MIBInfo> MIBs) : MIBs(std::move(MIBs)) {
    Versions.push_back(0);
  }
  AllocInfo(SmallVector<uint8_t> Versions, std::vector<MIBInfo> MIBs)
      : Versions(std::move(Versions)), MIBs(std::move(MIBs)) {}
};

inline raw_ostream &operator<<(raw_ostream &OS, const AllocInfo &AE) {
  bool First = true;
  OS << "Versions: ";
  for (auto V : AE.Versions) {
    if (!First)
      OS << ", ";
    First = false;
    OS << (unsigned)V;
  }
  OS << " MIB:\n";
  for (auto &M : AE.MIBs) {
    OS << "\t\t" << M << "\n";
  }
  if (!AE.TotalSizes.empty()) {
    OS << " TotalSizes per MIB:\n\t\t";
    First = true;
    for (uint64_t TS : AE.TotalSizes) {
      if (!First)
        OS << ", ";
      First = false;
      OS << TS << "\n";
    }
  }
  return OS;
}

/// Function and variable summary information to aid decisions and
/// implementation of importing.
class GlobalValueSummary {
public:
  /// Sububclass discriminator (for dyn_cast<> et al.)
  enum SummaryKind : unsigned { AliasKind, FunctionKind, GlobalVarKind };

  enum ImportKind : unsigned {
    // The global value definition corresponding to the summary should be
    // imported from source module
    Definition = 0,

    // When its definition doesn't exist in the destination module and not
    // imported (e.g., function is too large to be inlined), the global value
    // declaration corresponding to the summary should be imported, or the
    // attributes from summary should be annotated on the function declaration.
    Declaration = 1,
  };

  /// Group flags (Linkage, NotEligibleToImport, etc.) as a bitfield.
  struct GVFlags {
    /// The linkage type of the associated global value.
    ///
    /// One use is to flag values that have local linkage types and need to
    /// have module identifier appended before placing into the combined
    /// index, to disambiguate from other values with the same name.
    /// In the future this will be used to update and optimize linkage
    /// types based on global summary-based analysis.
    unsigned Linkage : 4;

    /// Indicates the visibility.
    unsigned Visibility : 2;

    /// Indicate if the global value cannot be imported (e.g. it cannot
    /// be renamed or references something that can't be renamed).
    unsigned NotEligibleToImport : 1;

    /// In per-module summary, indicate that the global value must be considered
    /// a live root for index-based liveness analysis. Used for special LLVM
    /// values such as llvm.global_ctors that the linker does not know about.
    ///
    /// In combined summary, indicate that the global value is live.
    unsigned Live : 1;

    /// Indicates that the linker resolved the symbol to a definition from
    /// within the same linkage unit.
    unsigned DSOLocal : 1;

    /// In the per-module summary, indicates that the global value is
    /// linkonce_odr and global unnamed addr (so eligible for auto-hiding
    /// via hidden visibility). In the combined summary, indicates that the
    /// prevailing linkonce_odr copy can be auto-hidden via hidden visibility
    /// when it is upgraded to weak_odr in the backend. This is legal when
    /// all copies are eligible for auto-hiding (i.e. all copies were
    /// linkonce_odr global unnamed addr. If any copy is not (e.g. it was
    /// originally weak_odr, we cannot auto-hide the prevailing copy as it
    /// means the symbol was externally visible.
    unsigned CanAutoHide : 1;

    /// This field is written by the ThinLTO indexing step to postlink combined
    /// summary. The value is interpreted as 'ImportKind' enum defined above.
    unsigned ImportType : 1;

    /// Convenience Constructors
    explicit GVFlags(GlobalValue::LinkageTypes Linkage,
                     GlobalValue::VisibilityTypes Visibility,
                     bool NotEligibleToImport, bool Live, bool IsLocal,
                     bool CanAutoHide, ImportKind ImportType)
        : Linkage(Linkage), Visibility(Visibility),
          NotEligibleToImport(NotEligibleToImport), Live(Live),
          DSOLocal(IsLocal), CanAutoHide(CanAutoHide),
          ImportType(static_cast<unsigned>(ImportType)) {}
  };

private:
  /// Kind of summary for use in dyn_cast<> et al.
  SummaryKind Kind;

  GVFlags Flags;

  /// This is the hash of the name of the symbol in the original file. It is
  /// identical to the GUID for global symbols, but differs for local since the
  /// GUID includes the module level id in the hash.
  GlobalValue::GUID OriginalName = 0;

  /// Path of module IR containing value's definition, used to locate
  /// module during importing.
  ///
  /// This is only used during parsing of the combined index, or when
  /// parsing the per-module index for creation of the combined summary index,
  /// not during writing of the per-module index which doesn't contain a
  /// module path string table.
  StringRef ModulePath;

  /// List of values referenced by this global value's definition
  /// (either by the initializer of a global variable, or referenced
  /// from within a function). This does not include functions called, which
  /// are listed in the derived FunctionSummary object.
  std::vector<ValueInfo> RefEdgeList;

protected:
  GlobalValueSummary(SummaryKind K, GVFlags Flags, std::vector<ValueInfo> Refs)
      : Kind(K), Flags(Flags), RefEdgeList(std::move(Refs)) {
    assert((K != AliasKind || Refs.empty()) &&
           "Expect no references for AliasSummary");
  }

public:
  virtual ~GlobalValueSummary() = default;

  /// Returns the hash of the original name, it is identical to the GUID for
  /// externally visible symbols, but not for local ones.
  GlobalValue::GUID getOriginalName() const { return OriginalName; }

  /// Initialize the original name hash in this summary.
  void setOriginalName(GlobalValue::GUID Name) { OriginalName = Name; }

  /// Which kind of summary subclass this is.
  SummaryKind getSummaryKind() const { return Kind; }

  /// Set the path to the module containing this function, for use in
  /// the combined index.
  void setModulePath(StringRef ModPath) { ModulePath = ModPath; }

  /// Get the path to the module containing this function.
  StringRef modulePath() const { return ModulePath; }

  /// Get the flags for this GlobalValue (see \p struct GVFlags).
  GVFlags flags() const { return Flags; }

  /// Return linkage type recorded for this global value.
  GlobalValue::LinkageTypes linkage() const {
    return static_cast<GlobalValue::LinkageTypes>(Flags.Linkage);
  }

  /// Sets the linkage to the value determined by global summary-based
  /// optimization. Will be applied in the ThinLTO backends.
  void setLinkage(GlobalValue::LinkageTypes Linkage) {
    Flags.Linkage = Linkage;
  }

  /// Return true if this global value can't be imported.
  bool notEligibleToImport() const { return Flags.NotEligibleToImport; }

  bool isLive() const { return Flags.Live; }

  void setLive(bool Live) { Flags.Live = Live; }

  void setDSOLocal(bool Local) { Flags.DSOLocal = Local; }

  bool isDSOLocal() const { return Flags.DSOLocal; }

  void setCanAutoHide(bool CanAutoHide) { Flags.CanAutoHide = CanAutoHide; }

  bool canAutoHide() const { return Flags.CanAutoHide; }

  bool shouldImportAsDecl() const {
    return Flags.ImportType == GlobalValueSummary::ImportKind::Declaration;
  }

  void setImportKind(ImportKind IK) { Flags.ImportType = IK; }

  GlobalValueSummary::ImportKind importType() const {
    return static_cast<ImportKind>(Flags.ImportType);
  }

  GlobalValue::VisibilityTypes getVisibility() const {
    return (GlobalValue::VisibilityTypes)Flags.Visibility;
  }
  void setVisibility(GlobalValue::VisibilityTypes Vis) {
    Flags.Visibility = (unsigned)Vis;
  }

  /// Flag that this global value cannot be imported.
  void setNotEligibleToImport() { Flags.NotEligibleToImport = true; }

  /// Return the list of values referenced by this global value definition.
  ArrayRef<ValueInfo> refs() const { return RefEdgeList; }

  /// If this is an alias summary, returns the summary of the aliased object (a
  /// global variable or function), otherwise returns itself.
  GlobalValueSummary *getBaseObject();
  const GlobalValueSummary *getBaseObject() const;

  friend class ModuleSummaryIndex;
};

GlobalValueSummaryInfo::GlobalValueSummaryInfo(bool HaveGVs) : U(HaveGVs) {}

/// Alias summary information.
class AliasSummary : public GlobalValueSummary {
  ValueInfo AliaseeValueInfo;

  /// This is the Aliasee in the same module as alias (could get from VI, trades
  /// memory for time). Note that this pointer may be null (and the value info
  /// empty) when we have a distributed index where the alias is being imported
  /// (as a copy of the aliasee), but the aliasee is not.
  GlobalValueSummary *AliaseeSummary;

public:
  AliasSummary(GVFlags Flags)
      : GlobalValueSummary(AliasKind, Flags, ArrayRef<ValueInfo>{}),
        AliaseeSummary(nullptr) {}

  /// Check if this is an alias summary.
  static bool classof(const GlobalValueSummary *GVS) {
    return GVS->getSummaryKind() == AliasKind;
  }

  void setAliasee(ValueInfo &AliaseeVI, GlobalValueSummary *Aliasee) {
    AliaseeValueInfo = AliaseeVI;
    AliaseeSummary = Aliasee;
  }

  bool hasAliasee() const {
    assert(!!AliaseeSummary == (AliaseeValueInfo &&
                                !AliaseeValueInfo.getSummaryList().empty()) &&
           "Expect to have both aliasee summary and summary list or neither");
    return !!AliaseeSummary;
  }

  const GlobalValueSummary &getAliasee() const {
    assert(AliaseeSummary && "Unexpected missing aliasee summary");
    return *AliaseeSummary;
  }

  GlobalValueSummary &getAliasee() {
    return const_cast<GlobalValueSummary &>(
                         static_cast<const AliasSummary *>(this)->getAliasee());
  }
  ValueInfo getAliaseeVI() const {
    assert(AliaseeValueInfo && "Unexpected missing aliasee");
    return AliaseeValueInfo;
  }
  GlobalValue::GUID getAliaseeGUID() const {
    assert(AliaseeValueInfo && "Unexpected missing aliasee");
    return AliaseeValueInfo.getGUID();
  }
};

const inline GlobalValueSummary *GlobalValueSummary::getBaseObject() const {
  if (auto *AS = dyn_cast<AliasSummary>(this))
    return &AS->getAliasee();
  return this;
}

inline GlobalValueSummary *GlobalValueSummary::getBaseObject() {
  if (auto *AS = dyn_cast<AliasSummary>(this))
    return &AS->getAliasee();
  return this;
}

/// Function summary information to aid decisions and implementation of
/// importing.
class FunctionSummary : public GlobalValueSummary {
public:
  /// <CalleeValueInfo, CalleeInfo> call edge pair.
  using EdgeTy = std::pair<ValueInfo, CalleeInfo>;

  /// Types for -force-summary-edges-cold debugging option.
  enum ForceSummaryHotnessType : unsigned {
    FSHT_None,
    FSHT_AllNonCritical,
    FSHT_All
  };

  /// An "identifier" for a virtual function. This contains the type identifier
  /// represented as a GUID and the offset from the address point to the virtual
  /// function pointer, where "address point" is as defined in the Itanium ABI:
  /// https://itanium-cxx-abi.github.io/cxx-abi/abi.html#vtable-general
  struct VFuncId {
    GlobalValue::GUID GUID;
    uint64_t Offset;
  };

  /// A specification for a virtual function call with all constant integer
  /// arguments. This is used to perform virtual constant propagation on the
  /// summary.
  struct ConstVCall {
    VFuncId VFunc;
    std::vector<uint64_t> Args;
  };

  /// All type identifier related information. Because these fields are
  /// relatively uncommon we only allocate space for them if necessary.
  struct TypeIdInfo {
    /// List of type identifiers used by this function in llvm.type.test
    /// intrinsics referenced by something other than an llvm.assume intrinsic,
    /// represented as GUIDs.
    std::vector<GlobalValue::GUID> TypeTests;

    /// List of virtual calls made by this function using (respectively)
    /// llvm.assume(llvm.type.test) or llvm.type.checked.load intrinsics that do
    /// not have all constant integer arguments.
    std::vector<VFuncId> TypeTestAssumeVCalls, TypeCheckedLoadVCalls;

    /// List of virtual calls made by this function using (respectively)
    /// llvm.assume(llvm.type.test) or llvm.type.checked.load intrinsics with
    /// all constant integer arguments.
    std::vector<ConstVCall> TypeTestAssumeConstVCalls,
        TypeCheckedLoadConstVCalls;
  };

  /// Flags specific to function summaries.
  struct FFlags {
    // Function attribute flags. Used to track if a function accesses memory,
    // recurses or aliases.
    unsigned ReadNone : 1;
    unsigned ReadOnly : 1;
    unsigned NoRecurse : 1;
    unsigned ReturnDoesNotAlias : 1;

    // Indicate if the global value cannot be inlined.
    unsigned NoInline : 1;
    // Indicate if function should be always inlined.
    unsigned AlwaysInline : 1;
    // Indicate if function never raises an exception. Can be modified during
    // thinlink function attribute propagation
    unsigned NoUnwind : 1;
    // Indicate if function contains instructions that mayThrow
    unsigned MayThrow : 1;

    // If there are calls to unknown targets (e.g. indirect)
    unsigned HasUnknownCall : 1;

    // Indicate if a function must be an unreachable function.
    //
    // This bit is sufficient but not necessary;
    // if this bit is on, the function must be regarded as unreachable;
    // if this bit is off, the function might be reachable or unreachable.
    unsigned MustBeUnreachable : 1;

    FFlags &operator&=(const FFlags &RHS) {
      this->ReadNone &= RHS.ReadNone;
      this->ReadOnly &= RHS.ReadOnly;
      this->NoRecurse &= RHS.NoRecurse;
      this->ReturnDoesNotAlias &= RHS.ReturnDoesNotAlias;
      this->NoInline &= RHS.NoInline;
      this->AlwaysInline &= RHS.AlwaysInline;
      this->NoUnwind &= RHS.NoUnwind;
      this->MayThrow &= RHS.MayThrow;
      this->HasUnknownCall &= RHS.HasUnknownCall;
      this->MustBeUnreachable &= RHS.MustBeUnreachable;
      return *this;
    }

    bool anyFlagSet() {
      return this->ReadNone | this->ReadOnly | this->NoRecurse |
             this->ReturnDoesNotAlias | this->NoInline | this->AlwaysInline |
             this->NoUnwind | this->MayThrow | this->HasUnknownCall |
             this->MustBeUnreachable;
    }

    operator std::string() {
      std::string Output;
      raw_string_ostream OS(Output);
      OS << "funcFlags: (";
      OS << "readNone: " << this->ReadNone;
      OS << ", readOnly: " << this->ReadOnly;
      OS << ", noRecurse: " << this->NoRecurse;
      OS << ", returnDoesNotAlias: " << this->ReturnDoesNotAlias;
      OS << ", noInline: " << this->NoInline;
      OS << ", alwaysInline: " << this->AlwaysInline;
      OS << ", noUnwind: " << this->NoUnwind;
      OS << ", mayThrow: " << this->MayThrow;
      OS << ", hasUnknownCall: " << this->HasUnknownCall;
      OS << ", mustBeUnreachable: " << this->MustBeUnreachable;
      OS << ")";
      return Output;
    }
  };

  /// Describes the uses of a parameter by the function.
  struct ParamAccess {
    static constexpr uint32_t RangeWidth = 64;

    /// Describes the use of a value in a call instruction, specifying the
    /// call's target, the value's parameter number, and the possible range of
    /// offsets from the beginning of the value that are passed.
    struct Call {
      uint64_t ParamNo = 0;
      ValueInfo Callee;
      ConstantRange Offsets{/*BitWidth=*/RangeWidth, /*isFullSet=*/true};

      Call() = default;
      Call(uint64_t ParamNo, ValueInfo Callee, const ConstantRange &Offsets)
          : ParamNo(ParamNo), Callee(Callee), Offsets(Offsets) {}
    };

    uint64_t ParamNo = 0;
    /// The range contains byte offsets from the parameter pointer which
    /// accessed by the function. In the per-module summary, it only includes
    /// accesses made by the function instructions. In the combined summary, it
    /// also includes accesses by nested function calls.
    ConstantRange Use{/*BitWidth=*/RangeWidth, /*isFullSet=*/true};
    /// In the per-module summary, it summarizes the byte offset applied to each
    /// pointer parameter before passing to each corresponding callee.
    /// In the combined summary, it's empty and information is propagated by
    /// inter-procedural analysis and applied to the Use field.
    std::vector<Call> Calls;

    ParamAccess() = default;
    ParamAccess(uint64_t ParamNo, const ConstantRange &Use)
        : ParamNo(ParamNo), Use(Use) {}
  };

  /// Create an empty FunctionSummary (with specified call edges).
  /// Used to represent external nodes and the dummy root node.
  static FunctionSummary
  makeDummyFunctionSummary(std::vector<FunctionSummary::EdgeTy> Edges) {
    return FunctionSummary(
        FunctionSummary::GVFlags(
            GlobalValue::LinkageTypes::AvailableExternallyLinkage,
            GlobalValue::DefaultVisibility,
            /*NotEligibleToImport=*/true, /*Live=*/true, /*IsLocal=*/false,
            /*CanAutoHide=*/false, GlobalValueSummary::ImportKind::Definition),
        /*NumInsts=*/0, FunctionSummary::FFlags{}, /*EntryCount=*/0,
        std::vector<ValueInfo>(), std::move(Edges),
        std::vector<GlobalValue::GUID>(),
        std::vector<FunctionSummary::VFuncId>(),
        std::vector<FunctionSummary::VFuncId>(),
        std::vector<FunctionSummary::ConstVCall>(),
        std::vector<FunctionSummary::ConstVCall>(),
        std::vector<FunctionSummary::ParamAccess>(),
        std::vector<CallsiteInfo>(), std::vector<AllocInfo>());
  }

  /// A dummy node to reference external functions that aren't in the index
  static FunctionSummary ExternalNode;

private:
  /// Number of instructions (ignoring debug instructions, e.g.) computed
  /// during the initial compile step when the summary index is first built.
  unsigned InstCount;

  /// Function summary specific flags.
  FFlags FunFlags;

  /// The synthesized entry count of the function.
  /// This is only populated during ThinLink phase and remains unused while
  /// generating per-module summaries.
  uint64_t EntryCount = 0;

  /// List of <CalleeValueInfo, CalleeInfo> call edge pairs from this function.
  std::vector<EdgeTy> CallGraphEdgeList;

  std::unique_ptr<TypeIdInfo> TIdInfo;

  /// Uses for every parameter to this function.
  using ParamAccessesTy = std::vector<ParamAccess>;
  std::unique_ptr<ParamAccessesTy> ParamAccesses;

  /// Optional list of memprof callsite metadata summaries. The correspondence
  /// between the callsite summary and the callsites in the function is implied
  /// by the order in the vector (and can be validated by comparing the stack
  /// ids in the CallsiteInfo to those in the instruction callsite metadata).
  /// As a memory savings optimization, we only create these for the prevailing
  /// copy of a symbol when creating the combined index during LTO.
  using CallsitesTy = std::vector<CallsiteInfo>;
  std::unique_ptr<CallsitesTy> Callsites;

  /// Optional list of allocation memprof metadata summaries. The correspondence
  /// between the alloc memprof summary and the allocation callsites in the
  /// function is implied by the order in the vector (and can be validated by
  /// comparing the stack ids in the AllocInfo to those in the instruction
  /// memprof metadata).
  /// As a memory savings optimization, we only create these for the prevailing
  /// copy of a symbol when creating the combined index during LTO.
  using AllocsTy = std::vector<AllocInfo>;
  std::unique_ptr<AllocsTy> Allocs;

public:
  FunctionSummary(GVFlags Flags, unsigned NumInsts, FFlags FunFlags,
                  uint64_t EntryCount, std::vector<ValueInfo> Refs,
                  std::vector<EdgeTy> CGEdges,
                  std::vector<GlobalValue::GUID> TypeTests,
                  std::vector<VFuncId> TypeTestAssumeVCalls,
                  std::vector<VFuncId> TypeCheckedLoadVCalls,
                  std::vector<ConstVCall> TypeTestAssumeConstVCalls,
                  std::vector<ConstVCall> TypeCheckedLoadConstVCalls,
                  std::vector<ParamAccess> Params, CallsitesTy CallsiteList,
                  AllocsTy AllocList)
      : GlobalValueSummary(FunctionKind, Flags, std::move(Refs)),
        InstCount(NumInsts), FunFlags(FunFlags), EntryCount(EntryCount),
        CallGraphEdgeList(std::move(CGEdges)) {
    if (!TypeTests.empty() || !TypeTestAssumeVCalls.empty() ||
        !TypeCheckedLoadVCalls.empty() || !TypeTestAssumeConstVCalls.empty() ||
        !TypeCheckedLoadConstVCalls.empty())
      TIdInfo = std::make_unique<TypeIdInfo>(
          TypeIdInfo{std::move(TypeTests), std::move(TypeTestAssumeVCalls),
                     std::move(TypeCheckedLoadVCalls),
                     std::move(TypeTestAssumeConstVCalls),
                     std::move(TypeCheckedLoadConstVCalls)});
    if (!Params.empty())
      ParamAccesses = std::make_unique<ParamAccessesTy>(std::move(Params));
    if (!CallsiteList.empty())
      Callsites = std::make_unique<CallsitesTy>(std::move(CallsiteList));
    if (!AllocList.empty())
      Allocs = std::make_unique<AllocsTy>(std::move(AllocList));
  }
  // Gets the number of readonly and writeonly refs in RefEdgeList
  std::pair<unsigned, unsigned> specialRefCounts() const;

  /// Check if this is a function summary.
  static bool classof(const GlobalValueSummary *GVS) {
    return GVS->getSummaryKind() == FunctionKind;
  }

  /// Get function summary flags.
  FFlags fflags() const { return FunFlags; }

  void setNoRecurse() { FunFlags.NoRecurse = true; }

  void setNoUnwind() { FunFlags.NoUnwind = true; }

  /// Get the instruction count recorded for this function.
  unsigned instCount() const { return InstCount; }

  /// Get the synthetic entry count for this function.
  uint64_t entryCount() const { return EntryCount; }

  /// Set the synthetic entry count for this function.
  void setEntryCount(uint64_t EC) { EntryCount = EC; }

  /// Return the list of <CalleeValueInfo, CalleeInfo> pairs.
  ArrayRef<EdgeTy> calls() const { return CallGraphEdgeList; }

  std::vector<EdgeTy> &mutableCalls() { return CallGraphEdgeList; }

  void addCall(EdgeTy E) { CallGraphEdgeList.push_back(E); }

  /// Returns the list of type identifiers used by this function in
  /// llvm.type.test intrinsics other than by an llvm.assume intrinsic,
  /// represented as GUIDs.
  ArrayRef<GlobalValue::GUID> type_tests() const {
    if (TIdInfo)
      return TIdInfo->TypeTests;
    return {};
  }

  /// Returns the list of virtual calls made by this function using
  /// llvm.assume(llvm.type.test) intrinsics that do not have all constant
  /// integer arguments.
  ArrayRef<VFuncId> type_test_assume_vcalls() const {
    if (TIdInfo)
      return TIdInfo->TypeTestAssumeVCalls;
    return {};
  }

  /// Returns the list of virtual calls made by this function using
  /// llvm.type.checked.load intrinsics that do not have all constant integer
  /// arguments.
  ArrayRef<VFuncId> type_checked_load_vcalls() const {
    if (TIdInfo)
      return TIdInfo->TypeCheckedLoadVCalls;
    return {};
  }

  /// Returns the list of virtual calls made by this function using
  /// llvm.assume(llvm.type.test) intrinsics with all constant integer
  /// arguments.
  ArrayRef<ConstVCall> type_test_assume_const_vcalls() const {
    if (TIdInfo)
      return TIdInfo->TypeTestAssumeConstVCalls;
    return {};
  }

  /// Returns the list of virtual calls made by this function using
  /// llvm.type.checked.load intrinsics with all constant integer arguments.
  ArrayRef<ConstVCall> type_checked_load_const_vcalls() const {
    if (TIdInfo)
      return TIdInfo->TypeCheckedLoadConstVCalls;
    return {};
  }

  /// Returns the list of known uses of pointer parameters.
  ArrayRef<ParamAccess> paramAccesses() const {
    if (ParamAccesses)
      return *ParamAccesses;
    return {};
  }

  /// Sets the list of known uses of pointer parameters.
  void setParamAccesses(std::vector<ParamAccess> NewParams) {
    if (NewParams.empty())
      ParamAccesses.reset();
    else if (ParamAccesses)
      *ParamAccesses = std::move(NewParams);
    else
      ParamAccesses = std::make_unique<ParamAccessesTy>(std::move(NewParams));
  }

  /// Add a type test to the summary. This is used by WholeProgramDevirt if we
  /// were unable to devirtualize a checked call.
  void addTypeTest(GlobalValue::GUID Guid) {
    if (!TIdInfo)
      TIdInfo = std::make_unique<TypeIdInfo>();
    TIdInfo->TypeTests.push_back(Guid);
  }

  const TypeIdInfo *getTypeIdInfo() const { return TIdInfo.get(); };

  ArrayRef<CallsiteInfo> callsites() const {
    if (Callsites)
      return *Callsites;
    return {};
  }

  CallsitesTy &mutableCallsites() {
    assert(Callsites);
    return *Callsites;
  }

  void addCallsite(CallsiteInfo &Callsite) {
    if (!Callsites)
      Callsites = std::make_unique<CallsitesTy>();
    Callsites->push_back(Callsite);
  }

  ArrayRef<AllocInfo> allocs() const {
    if (Allocs)
      return *Allocs;
    return {};
  }

  AllocsTy &mutableAllocs() {
    assert(Allocs);
    return *Allocs;
  }

  friend struct GraphTraits<ValueInfo>;
};

template <> struct DenseMapInfo<FunctionSummary::VFuncId> {
  static FunctionSummary::VFuncId getEmptyKey() { return {0, uint64_t(-1)}; }

  static FunctionSummary::VFuncId getTombstoneKey() {
    return {0, uint64_t(-2)};
  }

  static bool isEqual(FunctionSummary::VFuncId L, FunctionSummary::VFuncId R) {
    return L.GUID == R.GUID && L.Offset == R.Offset;
  }

  static unsigned getHashValue(FunctionSummary::VFuncId I) { return I.GUID; }
};

template <> struct DenseMapInfo<FunctionSummary::ConstVCall> {
  static FunctionSummary::ConstVCall getEmptyKey() {
    return {{0, uint64_t(-1)}, {}};
  }

  static FunctionSummary::ConstVCall getTombstoneKey() {
    return {{0, uint64_t(-2)}, {}};
  }

  static bool isEqual(FunctionSummary::ConstVCall L,
                      FunctionSummary::ConstVCall R) {
    return DenseMapInfo<FunctionSummary::VFuncId>::isEqual(L.VFunc, R.VFunc) &&
           L.Args == R.Args;
  }

  static unsigned getHashValue(FunctionSummary::ConstVCall I) {
    return I.VFunc.GUID;
  }
};

/// The ValueInfo and offset for a function within a vtable definition
/// initializer array.
struct VirtFuncOffset {
  VirtFuncOffset(ValueInfo VI, uint64_t Offset)
      : FuncVI(VI), VTableOffset(Offset) {}

  ValueInfo FuncVI;
  uint64_t VTableOffset;
};
/// List of functions referenced by a particular vtable definition.
using VTableFuncList = std::vector<VirtFuncOffset>;

/// Global variable summary information to aid decisions and
/// implementation of importing.
///
/// Global variable summary has two extra flag, telling if it is
/// readonly or writeonly. Both readonly and writeonly variables
/// can be optimized in the backed: readonly variables can be
/// const-folded, while writeonly vars can be completely eliminated
/// together with corresponding stores. We let both things happen
/// by means of internalizing such variables after ThinLTO import.
class GlobalVarSummary : public GlobalValueSummary {
private:
  /// For vtable definitions this holds the list of functions and
  /// their corresponding offsets within the initializer array.
  std::unique_ptr<VTableFuncList> VTableFuncs;

public:
  struct GVarFlags {
    GVarFlags(bool ReadOnly, bool WriteOnly, bool Constant,
              GlobalObject::VCallVisibility Vis)
        : MaybeReadOnly(ReadOnly), MaybeWriteOnly(WriteOnly),
          Constant(Constant), VCallVisibility(Vis) {}

    // If true indicates that this global variable might be accessed
    // purely by non-volatile load instructions. This in turn means
    // it can be internalized in source and destination modules during
    // thin LTO import because it neither modified nor its address
    // is taken.
    unsigned MaybeReadOnly : 1;
    // If true indicates that variable is possibly only written to, so
    // its value isn't loaded and its address isn't taken anywhere.
    // False, when 'Constant' attribute is set.
    unsigned MaybeWriteOnly : 1;
    // Indicates that value is a compile-time constant. Global variable
    // can be 'Constant' while not being 'ReadOnly' on several occasions:
    // - it is volatile, (e.g mapped device address)
    // - its address is taken, meaning that unlike 'ReadOnly' vars we can't
    //   internalize it.
    // Constant variables are always imported thus giving compiler an
    // opportunity to make some extra optimizations. Readonly constants
    // are also internalized.
    unsigned Constant : 1;
    // Set from metadata on vtable definitions during the module summary
    // analysis.
    unsigned VCallVisibility : 2;
  } VarFlags;

  GlobalVarSummary(GVFlags Flags, GVarFlags VarFlags,
                   std::vector<ValueInfo> Refs)
      : GlobalValueSummary(GlobalVarKind, Flags, std::move(Refs)),
        VarFlags(VarFlags) {}

  /// Check if this is a global variable summary.
  static bool classof(const GlobalValueSummary *GVS) {
    return GVS->getSummaryKind() == GlobalVarKind;
  }

  GVarFlags varflags() const { return VarFlags; }
  void setReadOnly(bool RO) { VarFlags.MaybeReadOnly = RO; }
  void setWriteOnly(bool WO) { VarFlags.MaybeWriteOnly = WO; }
  bool maybeReadOnly() const { return VarFlags.MaybeReadOnly; }
  bool maybeWriteOnly() const { return VarFlags.MaybeWriteOnly; }
  bool isConstant() const { return VarFlags.Constant; }
  void setVCallVisibility(GlobalObject::VCallVisibility Vis) {
    VarFlags.VCallVisibility = Vis;
  }
  GlobalObject::VCallVisibility getVCallVisibility() const {
    return (GlobalObject::VCallVisibility)VarFlags.VCallVisibility;
  }

  void setVTableFuncs(VTableFuncList Funcs) {
    assert(!VTableFuncs);
    VTableFuncs = std::make_unique<VTableFuncList>(std::move(Funcs));
  }

  ArrayRef<VirtFuncOffset> vTableFuncs() const {
    if (VTableFuncs)
      return *VTableFuncs;
    return {};
  }
};

struct TypeTestResolution {
  /// Specifies which kind of type check we should emit for this byte array.
  /// See http://clang.llvm.org/docs/ControlFlowIntegrityDesign.html for full
  /// details on each kind of check; the enumerators are described with
  /// reference to that document.
  enum Kind {
    Unsat,     ///< Unsatisfiable type (i.e. no global has this type metadata)
    ByteArray, ///< Test a byte array (first example)
    Inline,    ///< Inlined bit vector ("Short Inline Bit Vectors")
    Single,    ///< Single element (last example in "Short Inline Bit Vectors")
    AllOnes,   ///< All-ones bit vector ("Eliminating Bit Vector Checks for
               ///  All-Ones Bit Vectors")
    Unknown,   ///< Unknown (analysis not performed, don't lower)
  } TheKind = Unknown;

  /// Range of size-1 expressed as a bit width. For example, if the size is in
  /// range [1,256], this number will be 8. This helps generate the most compact
  /// instruction sequences.
  unsigned SizeM1BitWidth = 0;

  // The following fields are only used if the target does not support the use
  // of absolute symbols to store constants. Their meanings are the same as the
  // corresponding fields in LowerTypeTestsModule::TypeIdLowering in
  // LowerTypeTests.cpp.

  uint64_t AlignLog2 = 0;
  uint64_t SizeM1 = 0;
  uint8_t BitMask = 0;
  uint64_t InlineBits = 0;
};

struct WholeProgramDevirtResolution {
  enum Kind {
    Indir,        ///< Just do a regular virtual call
    SingleImpl,   ///< Single implementation devirtualization
    BranchFunnel, ///< When retpoline mitigation is enabled, use a branch funnel
                  ///< that is defined in the merged module. Otherwise same as
                  ///< Indir.
  } TheKind = Indir;

  std::string SingleImplName;

  struct ByArg {
    enum Kind {
      Indir,            ///< Just do a regular virtual call
      UniformRetVal,    ///< Uniform return value optimization
      UniqueRetVal,     ///< Unique return value optimization
      VirtualConstProp, ///< Virtual constant propagation
    } TheKind = Indir;

    /// Additional information for the resolution:
    /// - UniformRetVal: the uniform return value.
    /// - UniqueRetVal: the return value associated with the unique vtable (0 or
    ///   1).
    uint64_t Info = 0;

    // The following fields are only used if the target does not support the use
    // of absolute symbols to store constants.

    uint32_t Byte = 0;
    uint32_t Bit = 0;
  };

  /// Resolutions for calls with all constant integer arguments (excluding the
  /// first argument, "this"), where the key is the argument vector.
  std::map<std::vector<uint64_t>, ByArg> ResByArg;
};

struct TypeIdSummary {
  TypeTestResolution TTRes;

  /// Mapping from byte offset to whole-program devirt resolution for that
  /// (typeid, byte offset) pair.
  std::map<uint64_t, WholeProgramDevirtResolution> WPDRes;
};

/// 160 bits SHA1
using ModuleHash = std::array<uint32_t, 5>;

/// Type used for iterating through the global value summary map.
using const_gvsummary_iterator = GlobalValueSummaryMapTy::const_iterator;
using gvsummary_iterator = GlobalValueSummaryMapTy::iterator;

/// String table to hold/own module path strings, as well as a hash
/// of the module. The StringMap makes a copy of and owns inserted strings.
using ModulePathStringTableTy = StringMap<ModuleHash>;

/// Map of global value GUID to its summary, used to identify values defined in
/// a particular module, and provide efficient access to their summary.
using GVSummaryMapTy = DenseMap<GlobalValue::GUID, GlobalValueSummary *>;

/// A set of global value summary pointers.
using GVSummaryPtrSet = std::unordered_set<GlobalValueSummary *>;

/// Map of a type GUID to type id string and summary (multimap used
/// in case of GUID conflicts).
using TypeIdSummaryMapTy =
    std::multimap<GlobalValue::GUID, std::pair<std::string, TypeIdSummary>>;

/// The following data structures summarize type metadata information.
/// For type metadata overview see https://llvm.org/docs/TypeMetadata.html.
/// Each type metadata includes both the type identifier and the offset of
/// the address point of the type (the address held by objects of that type
/// which may not be the beginning of the virtual table). Vtable definitions
/// are decorated with type metadata for the types they are compatible with.
///
/// Holds information about vtable definitions decorated with type metadata:
/// the vtable definition value and its address point offset in a type
/// identifier metadata it is decorated (compatible) with.
struct TypeIdOffsetVtableInfo {
  TypeIdOffsetVtableInfo(uint64_t Offset, ValueInfo VI)
      : AddressPointOffset(Offset), VTableVI(VI) {}

  uint64_t AddressPointOffset;
  ValueInfo VTableVI;
};
/// List of vtable definitions decorated by a particular type identifier,
/// and their corresponding offsets in that type identifier's metadata.
/// Note that each type identifier may be compatible with multiple vtables, due
/// to inheritance, which is why this is a vector.
using TypeIdCompatibleVtableInfo = std::vector<TypeIdOffsetVtableInfo>;

/// Class to hold module path string table and global value map,
/// and encapsulate methods for operating on them.
class ModuleSummaryIndex {
private:
  /// Map from value name to list of summary instances for values of that
  /// name (may be duplicates in the COMDAT case, e.g.).
  GlobalValueSummaryMapTy GlobalValueMap;

  /// Holds strings for combined index, mapping to the corresponding module ID.
  ModulePathStringTableTy ModulePathStringTable;

  /// Mapping from type identifier GUIDs to type identifier and its summary
  /// information. Produced by thin link.
  TypeIdSummaryMapTy TypeIdMap;

  /// Mapping from type identifier to information about vtables decorated
  /// with that type identifier's metadata. Produced by per module summary
  /// analysis and consumed by thin link. For more information, see description
  /// above where TypeIdCompatibleVtableInfo is defined.
  std::map<std::string, TypeIdCompatibleVtableInfo, std::less<>>
      TypeIdCompatibleVtableMap;

  /// Mapping from original ID to GUID. If original ID can map to multiple
  /// GUIDs, it will be mapped to 0.
  std::map<GlobalValue::GUID, GlobalValue::GUID> OidGuidMap;

  /// Indicates that summary-based GlobalValue GC has run, and values with
  /// GVFlags::Live==false are really dead. Otherwise, all values must be
  /// considered live.
  bool WithGlobalValueDeadStripping = false;

  /// Indicates that summary-based attribute propagation has run and
  /// GVarFlags::MaybeReadonly / GVarFlags::MaybeWriteonly are really
  /// read/write only.
  bool WithAttributePropagation = false;

  /// Indicates that summary-based DSOLocal propagation has run and the flag in
  /// every summary of a GV is synchronized.
  bool WithDSOLocalPropagation = false;

  /// Indicates that we have whole program visibility.
  bool WithWholeProgramVisibility = false;

  /// Indicates that summary-based synthetic entry count propagation has run
  bool HasSyntheticEntryCounts = false;

  /// Indicates that we linked with allocator supporting hot/cold new operators.
  bool WithSupportsHotColdNew = false;

  /// Indicates that distributed backend should skip compilation of the
  /// module. Flag is suppose to be set by distributed ThinLTO indexing
  /// when it detected that the module is not needed during the final
  /// linking. As result distributed backend should just output a minimal
  /// valid object file.
  bool SkipModuleByDistributedBackend = false;

  /// If true then we're performing analysis of IR module, or parsing along with
  /// the IR from assembly. The value of 'false' means we're reading summary
  /// from BC or YAML source. Affects the type of value stored in NameOrGV
  /// union.
  bool HaveGVs;

  // True if the index was created for a module compiled with -fsplit-lto-unit.
  bool EnableSplitLTOUnit;

  // True if the index was created for a module compiled with -funified-lto
  bool UnifiedLTO;

  // True if some of the modules were compiled with -fsplit-lto-unit and
  // some were not. Set when the combined index is created during the thin link.
  bool PartiallySplitLTOUnits = false;

  /// True if some of the FunctionSummary contains a ParamAccess.
  bool HasParamAccess = false;

  std::set<std::string> CfiFunctionDefs;
  std::set<std::string> CfiFunctionDecls;

  // Used in cases where we want to record the name of a global, but
  // don't have the string owned elsewhere (e.g. the Strtab on a module).
  BumpPtrAllocator Alloc;
  StringSaver Saver;

  // The total number of basic blocks in the module in the per-module summary or
  // the total number of basic blocks in the LTO unit in the combined index.
  // FIXME: Putting this in the distributed ThinLTO index files breaks LTO
  // backend caching on any BB change to any linked file. It is currently not
  // used except in the case of a SamplePGO partial profile, and should be
  // reevaluated/redesigned to allow more effective incremental builds in that
  // case.
  uint64_t BlockCount;

  // List of unique stack ids (hashes). We use a 4B index of the id in the
  // stack id lists on the alloc and callsite summaries for memory savings,
  // since the number of unique ids is in practice much smaller than the
  // number of stack id references in the summaries.
  std::vector<uint64_t> StackIds;

  // Temporary map while building StackIds list. Clear when index is completely
  // built via releaseTemporaryMemory.
  DenseMap<uint64_t, unsigned> StackIdToIndex;

  // YAML I/O support.
  friend yaml::MappingTraits<ModuleSummaryIndex>;

  GlobalValueSummaryMapTy::value_type *
  getOrInsertValuePtr(GlobalValue::GUID GUID) {
    return &*GlobalValueMap.emplace(GUID, GlobalValueSummaryInfo(HaveGVs))
                 .first;
  }

public:
  // See HaveGVs variable comment.
  ModuleSummaryIndex(bool HaveGVs, bool EnableSplitLTOUnit = false,
                     bool UnifiedLTO = false)
      : HaveGVs(HaveGVs), EnableSplitLTOUnit(EnableSplitLTOUnit),
        UnifiedLTO(UnifiedLTO), Saver(Alloc), BlockCount(0) {}

  // Current version for the module summary in bitcode files.
  // The BitcodeSummaryVersion should be bumped whenever we introduce changes
  // in the way some record are interpreted, like flags for instance.
  // Note that incrementing this may require changes in both BitcodeReader.cpp
  // and BitcodeWriter.cpp.
  static constexpr uint64_t BitcodeSummaryVersion = 10;

  // Regular LTO module name for ASM writer
  static constexpr const char *getRegularLTOModuleName() {
    return "[Regular LTO]";
  }

  bool haveGVs() const { return HaveGVs; }

  uint64_t getFlags() const;
  void setFlags(uint64_t Flags);

  uint64_t getBlockCount() const { return BlockCount; }
  void addBlockCount(uint64_t C) { BlockCount += C; }
  void setBlockCount(uint64_t C) { BlockCount = C; }

  gvsummary_iterator begin() { return GlobalValueMap.begin(); }
  const_gvsummary_iterator begin() const { return GlobalValueMap.begin(); }
  gvsummary_iterator end() { return GlobalValueMap.end(); }
  const_gvsummary_iterator end() const { return GlobalValueMap.end(); }
  size_t size() const { return GlobalValueMap.size(); }

  const std::vector<uint64_t> &stackIds() const { return StackIds; }

  unsigned addOrGetStackIdIndex(uint64_t StackId) {
    auto Inserted = StackIdToIndex.insert({StackId, StackIds.size()});
    if (Inserted.second)
      StackIds.push_back(StackId);
    return Inserted.first->second;
  }

  uint64_t getStackIdAtIndex(unsigned Index) const {
    assert(StackIds.size() > Index);
    return StackIds[Index];
  }

  // Facility to release memory from data structures only needed during index
  // construction (including while building combined index). Currently this only
  // releases the temporary map used while constructing a correspondence between
  // stack ids and their index in the StackIds vector. Mostly impactful when
  // building a large combined index.
  void releaseTemporaryMemory() {
    assert(StackIdToIndex.size() == StackIds.size());
    StackIdToIndex.clear();
    StackIds.shrink_to_fit();
  }

  /// Convenience function for doing a DFS on a ValueInfo. Marks the function in
  /// the FunctionHasParent map.
  static void discoverNodes(ValueInfo V,
                            std::map<ValueInfo, bool> &FunctionHasParent) {
    if (!V.getSummaryList().size())
      return; // skip external functions that don't have summaries

    // Mark discovered if we haven't yet
    auto S = FunctionHasParent.emplace(V, false);

    // Stop if we've already discovered this node
    if (!S.second)
      return;

    FunctionSummary *F =
        dyn_cast<FunctionSummary>(V.getSummaryList().front().get());
    assert(F != nullptr && "Expected FunctionSummary node");

    for (const auto &C : F->calls()) {
      // Insert node if necessary
      auto S = FunctionHasParent.emplace(C.first, true);

      // Skip nodes that we're sure have parents
      if (!S.second && S.first->second)
        continue;

      if (S.second)
        discoverNodes(C.first, FunctionHasParent);
      else
        S.first->second = true;
    }
  }

  // Calculate the callgraph root
  FunctionSummary calculateCallGraphRoot() {
    // Functions that have a parent will be marked in FunctionHasParent pair.
    // Once we've marked all functions, the functions in the map that are false
    // have no parent (so they're the roots)
    std::map<ValueInfo, bool> FunctionHasParent;

    for (auto &S : *this) {
      // Skip external functions
      if (!S.second.SummaryList.size() ||
          !isa<FunctionSummary>(S.second.SummaryList.front().get()))
        continue;
      discoverNodes(ValueInfo(HaveGVs, &S), FunctionHasParent);
    }

    std::vector<FunctionSummary::EdgeTy> Edges;
    // create edges to all roots in the Index
    for (auto &P : FunctionHasParent) {
      if (P.second)
        continue; // skip over non-root nodes
      Edges.push_back(std::make_pair(P.first, CalleeInfo{}));
    }
    if (Edges.empty()) {
      // Failed to find root - return an empty node
      return FunctionSummary::makeDummyFunctionSummary({});
    }
    auto CallGraphRoot = FunctionSummary::makeDummyFunctionSummary(Edges);
    return CallGraphRoot;
  }

  bool withGlobalValueDeadStripping() const {
    return WithGlobalValueDeadStripping;
  }
  void setWithGlobalValueDeadStripping() {
    WithGlobalValueDeadStripping = true;
  }

  bool withAttributePropagation() const { return WithAttributePropagation; }
  void setWithAttributePropagation() {
    WithAttributePropagation = true;
  }

  bool withDSOLocalPropagation() const { return WithDSOLocalPropagation; }
  void setWithDSOLocalPropagation() { WithDSOLocalPropagation = true; }

  bool withWholeProgramVisibility() const { return WithWholeProgramVisibility; }
  void setWithWholeProgramVisibility() { WithWholeProgramVisibility = true; }

  bool isReadOnly(const GlobalVarSummary *GVS) const {
    return WithAttributePropagation && GVS->maybeReadOnly();
  }
  bool isWriteOnly(const GlobalVarSummary *GVS) const {
    return WithAttributePropagation && GVS->maybeWriteOnly();
  }

  bool hasSyntheticEntryCounts() const { return HasSyntheticEntryCounts; }
  void setHasSyntheticEntryCounts() { HasSyntheticEntryCounts = true; }

  bool withSupportsHotColdNew() const { return WithSupportsHotColdNew; }
  void setWithSupportsHotColdNew() { WithSupportsHotColdNew = true; }

  bool skipModuleByDistributedBackend() const {
    return SkipModuleByDistributedBackend;
  }
  void setSkipModuleByDistributedBackend() {
    SkipModuleByDistributedBackend = true;
  }

  bool enableSplitLTOUnit() const { return EnableSplitLTOUnit; }
  void setEnableSplitLTOUnit() { EnableSplitLTOUnit = true; }

  bool hasUnifiedLTO() const { return UnifiedLTO; }
  void setUnifiedLTO() { UnifiedLTO = true; }

  bool partiallySplitLTOUnits() const { return PartiallySplitLTOUnits; }
  void setPartiallySplitLTOUnits() { PartiallySplitLTOUnits = true; }

  bool hasParamAccess() const { return HasParamAccess; }

  bool isGlobalValueLive(const GlobalValueSummary *GVS) const {
    return !WithGlobalValueDeadStripping || GVS->isLive();
  }
  bool isGUIDLive(GlobalValue::GUID GUID) const;

  /// Return a ValueInfo for the index value_type (convenient when iterating
  /// index).
  ValueInfo getValueInfo(const GlobalValueSummaryMapTy::value_type &R) const {
    return ValueInfo(HaveGVs, &R);
  }

  /// Return a ValueInfo for GUID if it exists, otherwise return ValueInfo().
  ValueInfo getValueInfo(GlobalValue::GUID GUID) const {
    auto I = GlobalValueMap.find(GUID);
    return ValueInfo(HaveGVs, I == GlobalValueMap.end() ? nullptr : &*I);
  }

  /// Return a ValueInfo for \p GUID.
  ValueInfo getOrInsertValueInfo(GlobalValue::GUID GUID) {
    return ValueInfo(HaveGVs, getOrInsertValuePtr(GUID));
  }

  // Save a string in the Index. Use before passing Name to
  // getOrInsertValueInfo when the string isn't owned elsewhere (e.g. on the
  // module's Strtab).
  StringRef saveString(StringRef String) { return Saver.save(String); }

  /// Return a ValueInfo for \p GUID setting value \p Name.
  ValueInfo getOrInsertValueInfo(GlobalValue::GUID GUID, StringRef Name) {
    assert(!HaveGVs);
    auto VP = getOrInsertValuePtr(GUID);
    VP->second.U.Name = Name;
    return ValueInfo(HaveGVs, VP);
  }

  /// Return a ValueInfo for \p GV and mark it as belonging to GV.
  ValueInfo getOrInsertValueInfo(const GlobalValue *GV) {
    assert(HaveGVs);
    auto VP = getOrInsertValuePtr(GV->getGUID());
    VP->second.U.GV = GV;
    return ValueInfo(HaveGVs, VP);
  }

  /// Return the GUID for \p OriginalId in the OidGuidMap.
  GlobalValue::GUID getGUIDFromOriginalID(GlobalValue::GUID OriginalID) const {
    const auto I = OidGuidMap.find(OriginalID);
    return I == OidGuidMap.end() ? 0 : I->second;
  }

  std::set<std::string> &cfiFunctionDefs() { return CfiFunctionDefs; }
  const std::set<std::string> &cfiFunctionDefs() const { return CfiFunctionDefs; }

  std::set<std::string> &cfiFunctionDecls() { return CfiFunctionDecls; }
  const std::set<std::string> &cfiFunctionDecls() const { return CfiFunctionDecls; }

  /// Add a global value summary for a value.
  void addGlobalValueSummary(const GlobalValue &GV,
                             std::unique_ptr<GlobalValueSummary> Summary) {
    addGlobalValueSummary(getOrInsertValueInfo(&GV), std::move(Summary));
  }

  /// Add a global value summary for a value of the given name.
  void addGlobalValueSummary(StringRef ValueName,
                             std::unique_ptr<GlobalValueSummary> Summary) {
    addGlobalValueSummary(getOrInsertValueInfo(GlobalValue::getGUID(ValueName)),
                          std::move(Summary));
  }

  /// Add a global value summary for the given ValueInfo.
  void addGlobalValueSummary(ValueInfo VI,
                             std::unique_ptr<GlobalValueSummary> Summary) {
    if (const FunctionSummary *FS = dyn_cast<FunctionSummary>(Summary.get()))
      HasParamAccess |= !FS->paramAccesses().empty();
    addOriginalName(VI.getGUID(), Summary->getOriginalName());
    // Here we have a notionally const VI, but the value it points to is owned
    // by the non-const *this.
    const_cast<GlobalValueSummaryMapTy::value_type *>(VI.getRef())
        ->second.SummaryList.push_back(std::move(Summary));
  }

  /// Add an original name for the value of the given GUID.
  void addOriginalName(GlobalValue::GUID ValueGUID,
                       GlobalValue::GUID OrigGUID) {
    if (OrigGUID == 0 || ValueGUID == OrigGUID)
      return;
    if (OidGuidMap.count(OrigGUID) && OidGuidMap[OrigGUID] != ValueGUID)
      OidGuidMap[OrigGUID] = 0;
    else
      OidGuidMap[OrigGUID] = ValueGUID;
  }

  /// Find the summary for ValueInfo \p VI in module \p ModuleId, or nullptr if
  /// not found.
  GlobalValueSummary *findSummaryInModule(ValueInfo VI, StringRef ModuleId) const {
    auto SummaryList = VI.getSummaryList();
    auto Summary =
        llvm::find_if(SummaryList,
                      [&](const std::unique_ptr<GlobalValueSummary> &Summary) {
                        return Summary->modulePath() == ModuleId;
                      });
    if (Summary == SummaryList.end())
      return nullptr;
    return Summary->get();
  }

  /// Find the summary for global \p GUID in module \p ModuleId, or nullptr if
  /// not found.
  GlobalValueSummary *findSummaryInModule(GlobalValue::GUID ValueGUID,
                                          StringRef ModuleId) const {
    auto CalleeInfo = getValueInfo(ValueGUID);
    if (!CalleeInfo)
      return nullptr; // This function does not have a summary
    return findSummaryInModule(CalleeInfo, ModuleId);
  }

  /// Returns the first GlobalValueSummary for \p GV, asserting that there
  /// is only one if \p PerModuleIndex.
  GlobalValueSummary *getGlobalValueSummary(const GlobalValue &GV,
                                            bool PerModuleIndex = true) const {
    assert(GV.hasName() && "Can't get GlobalValueSummary for GV with no name");
    return getGlobalValueSummary(GV.getGUID(), PerModuleIndex);
  }

  /// Returns the first GlobalValueSummary for \p ValueGUID, asserting that
  /// there
  /// is only one if \p PerModuleIndex.
  GlobalValueSummary *getGlobalValueSummary(GlobalValue::GUID ValueGUID,
                                            bool PerModuleIndex = true) const;

  /// Table of modules, containing module hash and id.
  const StringMap<ModuleHash> &modulePaths() const {
    return ModulePathStringTable;
  }

  /// Table of modules, containing hash and id.
  StringMap<ModuleHash> &modulePaths() { return ModulePathStringTable; }

  /// Get the module SHA1 hash recorded for the given module path.
  const ModuleHash &getModuleHash(const StringRef ModPath) const {
    auto It = ModulePathStringTable.find(ModPath);
    assert(It != ModulePathStringTable.end() && "Module not registered");
    return It->second;
  }

  /// Convenience method for creating a promoted global name
  /// for the given value name of a local, and its original module's ID.
  static std::string getGlobalNameForLocal(StringRef Name, ModuleHash ModHash) {
    std::string Suffix = utostr((uint64_t(ModHash[0]) << 32) |
                                ModHash[1]); // Take the first 64 bits
    return getGlobalNameForLocal(Name, Suffix);
  }

  static std::string getGlobalNameForLocal(StringRef Name, StringRef Suffix) {
    SmallString<256> NewName(Name);
    NewName += ".llvm.";
    NewName += Suffix;
    return std::string(NewName);
  }

  /// Helper to obtain the unpromoted name for a global value (or the original
  /// name if not promoted). Split off the rightmost ".llvm.${hash}" suffix,
  /// because it is possible in certain clients (not clang at the moment) for
  /// two rounds of ThinLTO optimization and therefore promotion to occur.
  static StringRef getOriginalNameBeforePromote(StringRef Name) {
    std::pair<StringRef, StringRef> Pair = Name.rsplit(".llvm.");
    return Pair.first;
  }

  typedef ModulePathStringTableTy::value_type ModuleInfo;

  /// Add a new module with the given \p Hash, mapped to the given \p
  /// ModID, and return a reference to the module.
  ModuleInfo *addModule(StringRef ModPath, ModuleHash Hash = ModuleHash{{0}}) {
    return &*ModulePathStringTable.insert({ModPath, Hash}).first;
  }

  /// Return module entry for module with the given \p ModPath.
  ModuleInfo *getModule(StringRef ModPath) {
    auto It = ModulePathStringTable.find(ModPath);
    assert(It != ModulePathStringTable.end() && "Module not registered");
    return &*It;
  }

  /// Return module entry for module with the given \p ModPath.
  const ModuleInfo *getModule(StringRef ModPath) const {
    auto It = ModulePathStringTable.find(ModPath);
    assert(It != ModulePathStringTable.end() && "Module not registered");
    return &*It;
  }

  /// Check if the given Module has any functions available for exporting
  /// in the index. We consider any module present in the ModulePathStringTable
  /// to have exported functions.
  bool hasExportedFunctions(const Module &M) const {
    return ModulePathStringTable.count(M.getModuleIdentifier());
  }

  const TypeIdSummaryMapTy &typeIds() const { return TypeIdMap; }

  /// Return an existing or new TypeIdSummary entry for \p TypeId.
  /// This accessor can mutate the map and therefore should not be used in
  /// the ThinLTO backends.
  TypeIdSummary &getOrInsertTypeIdSummary(StringRef TypeId) {
    auto TidIter = TypeIdMap.equal_range(GlobalValue::getGUID(TypeId));
    for (auto It = TidIter.first; It != TidIter.second; ++It)
      if (It->second.first == TypeId)
        return It->second.second;
    auto It = TypeIdMap.insert(
        {GlobalValue::getGUID(TypeId), {std::string(TypeId), TypeIdSummary()}});
    return It->second.second;
  }

  /// This returns either a pointer to the type id summary (if present in the
  /// summary map) or null (if not present). This may be used when importing.
  const TypeIdSummary *getTypeIdSummary(StringRef TypeId) const {
    auto TidIter = TypeIdMap.equal_range(GlobalValue::getGUID(TypeId));
    for (auto It = TidIter.first; It != TidIter.second; ++It)
      if (It->second.first == TypeId)
        return &It->second.second;
    return nullptr;
  }

  TypeIdSummary *getTypeIdSummary(StringRef TypeId) {
    return const_cast<TypeIdSummary *>(
        static_cast<const ModuleSummaryIndex *>(this)->getTypeIdSummary(
            TypeId));
  }

  const auto &typeIdCompatibleVtableMap() const {
    return TypeIdCompatibleVtableMap;
  }

  /// Return an existing or new TypeIdCompatibleVtableMap entry for \p TypeId.
  /// This accessor can mutate the map and therefore should not be used in
  /// the ThinLTO backends.
  TypeIdCompatibleVtableInfo &
  getOrInsertTypeIdCompatibleVtableSummary(StringRef TypeId) {
    return TypeIdCompatibleVtableMap[std::string(TypeId)];
  }

  /// For the given \p TypeId, this returns the TypeIdCompatibleVtableMap
  /// entry if present in the summary map. This may be used when importing.
  std::optional<TypeIdCompatibleVtableInfo>
  getTypeIdCompatibleVtableSummary(StringRef TypeId) const {
    auto I = TypeIdCompatibleVtableMap.find(TypeId);
    if (I == TypeIdCompatibleVtableMap.end())
      return std::nullopt;
    return I->second;
  }

  /// Collect for the given module the list of functions it defines
  /// (GUID -> Summary).
  void collectDefinedFunctionsForModule(StringRef ModulePath,
                                        GVSummaryMapTy &GVSummaryMap) const;

  /// Collect for each module the list of Summaries it defines (GUID ->
  /// Summary).
  template <class Map>
  void
  collectDefinedGVSummariesPerModule(Map &ModuleToDefinedGVSummaries) const {
    for (const auto &GlobalList : *this) {
      auto GUID = GlobalList.first;
      for (const auto &Summary : GlobalList.second.SummaryList) {
        ModuleToDefinedGVSummaries[Summary->modulePath()][GUID] = Summary.get();
      }
    }
  }

  /// Print to an output stream.
  void print(raw_ostream &OS, bool IsForDebug = false) const;

  /// Dump to stderr (for debugging).
  void dump() const;

  /// Export summary to dot file for GraphViz.
  void
  exportToDot(raw_ostream &OS,
              const DenseSet<GlobalValue::GUID> &GUIDPreservedSymbols) const;

  /// Print out strongly connected components for debugging.
  void dumpSCCs(raw_ostream &OS);

  /// Do the access attribute and DSOLocal propagation in combined index.
  void propagateAttributes(const DenseSet<GlobalValue::GUID> &PreservedSymbols);

  /// Checks if we can import global variable from another module.
  bool canImportGlobalVar(const GlobalValueSummary *S, bool AnalyzeRefs) const;
};

/// GraphTraits definition to build SCC for the index
template <> struct GraphTraits<ValueInfo> {
  typedef ValueInfo NodeRef;
  using EdgeRef = FunctionSummary::EdgeTy &;

  static NodeRef valueInfoFromEdge(FunctionSummary::EdgeTy &P) {
    return P.first;
  }
  using ChildIteratorType =
      mapped_iterator<std::vector<FunctionSummary::EdgeTy>::iterator,
                      decltype(&valueInfoFromEdge)>;

  using ChildEdgeIteratorType = std::vector<FunctionSummary::EdgeTy>::iterator;

  static NodeRef getEntryNode(ValueInfo V) { return V; }

  static ChildIteratorType child_begin(NodeRef N) {
    if (!N.getSummaryList().size()) // handle external function
      return ChildIteratorType(
          FunctionSummary::ExternalNode.CallGraphEdgeList.begin(),
          &valueInfoFromEdge);
    FunctionSummary *F =
        cast<FunctionSummary>(N.getSummaryList().front()->getBaseObject());
    return ChildIteratorType(F->CallGraphEdgeList.begin(), &valueInfoFromEdge);
  }

  static ChildIteratorType child_end(NodeRef N) {
    if (!N.getSummaryList().size()) // handle external function
      return ChildIteratorType(
          FunctionSummary::ExternalNode.CallGraphEdgeList.end(),
          &valueInfoFromEdge);
    FunctionSummary *F =
        cast<FunctionSummary>(N.getSummaryList().front()->getBaseObject());
    return ChildIteratorType(F->CallGraphEdgeList.end(), &valueInfoFromEdge);
  }

  static ChildEdgeIteratorType child_edge_begin(NodeRef N) {
    if (!N.getSummaryList().size()) // handle external function
      return FunctionSummary::ExternalNode.CallGraphEdgeList.begin();

    FunctionSummary *F =
        cast<FunctionSummary>(N.getSummaryList().front()->getBaseObject());
    return F->CallGraphEdgeList.begin();
  }

  static ChildEdgeIteratorType child_edge_end(NodeRef N) {
    if (!N.getSummaryList().size()) // handle external function
      return FunctionSummary::ExternalNode.CallGraphEdgeList.end();

    FunctionSummary *F =
        cast<FunctionSummary>(N.getSummaryList().front()->getBaseObject());
    return F->CallGraphEdgeList.end();
  }

  static NodeRef edge_dest(EdgeRef E) { return E.first; }
};

template <>
struct GraphTraits<ModuleSummaryIndex *> : public GraphTraits<ValueInfo> {
  static NodeRef getEntryNode(ModuleSummaryIndex *I) {
    std::unique_ptr<GlobalValueSummary> Root =
        std::make_unique<FunctionSummary>(I->calculateCallGraphRoot());
    GlobalValueSummaryInfo G(I->haveGVs());
    G.SummaryList.push_back(std::move(Root));
    static auto P =
        GlobalValueSummaryMapTy::value_type(GlobalValue::GUID(0), std::move(G));
    return ValueInfo(I->haveGVs(), &P);
  }
};
} // end namespace llvm

#endif // LLVM_IR_MODULESUMMARYINDEX_H

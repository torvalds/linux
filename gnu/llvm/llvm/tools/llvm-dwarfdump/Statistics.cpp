//===-- Statistics.cpp - Debug Info quality metrics -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm-dwarfdump.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugLoc.h"
#include "llvm/DebugInfo/DWARF/DWARFExpression.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/JSON.h"

#define DEBUG_TYPE "dwarfdump"
using namespace llvm;
using namespace llvm::dwarfdump;
using namespace llvm::object;

namespace {
/// This represents the number of categories of debug location coverage being
/// calculated. The first category is the number of variables with 0% location
/// coverage, but the last category is the number of variables with 100%
/// location coverage.
constexpr int NumOfCoverageCategories = 12;

/// This is used for zero location coverage bucket.
constexpr unsigned ZeroCoverageBucket = 0;

/// The UINT64_MAX is used as an indication of the overflow.
constexpr uint64_t OverflowValue = std::numeric_limits<uint64_t>::max();

/// This represents variables DIE offsets.
using AbstractOriginVarsTy = llvm::SmallVector<uint64_t>;
/// This maps function DIE offset to its variables.
using AbstractOriginVarsTyMap = llvm::DenseMap<uint64_t, AbstractOriginVarsTy>;
/// This represents function DIE offsets containing an abstract_origin.
using FunctionsWithAbstractOriginTy = llvm::SmallVector<uint64_t>;

/// This represents a data type for the stats and it helps us to
/// detect an overflow.
/// NOTE: This can be implemented as a template if there is an another type
/// needing this.
struct SaturatingUINT64 {
  /// Number that represents the stats.
  uint64_t Value;

  SaturatingUINT64(uint64_t Value_) : Value(Value_) {}

  void operator++(int) { return *this += 1; }
  void operator+=(uint64_t Value_) {
    if (Value != OverflowValue) {
      if (Value < OverflowValue - Value_)
        Value += Value_;
      else
        Value = OverflowValue;
    }
  }
};

/// Utility struct to store the full location of a DIE - its CU and offset.
struct DIELocation {
  DWARFUnit *DwUnit;
  uint64_t DIEOffset;
  DIELocation(DWARFUnit *_DwUnit, uint64_t _DIEOffset)
      : DwUnit(_DwUnit), DIEOffset(_DIEOffset) {}
};
/// This represents DWARF locations of CrossCU referencing DIEs.
using CrossCUReferencingDIELocationTy = llvm::SmallVector<DIELocation>;

/// This maps function DIE offset to its DWARF CU.
using FunctionDIECUTyMap = llvm::DenseMap<uint64_t, DWARFUnit *>;

/// Holds statistics for one function (or other entity that has a PC range and
/// contains variables, such as a compile unit).
struct PerFunctionStats {
  /// Number of inlined instances of this function.
  uint64_t NumFnInlined = 0;
  /// Number of out-of-line instances of this function.
  uint64_t NumFnOutOfLine = 0;
  /// Number of inlined instances that have abstract origins.
  uint64_t NumAbstractOrigins = 0;
  /// Number of variables and parameters with location across all inlined
  /// instances.
  uint64_t TotalVarWithLoc = 0;
  /// Number of constants with location across all inlined instances.
  uint64_t ConstantMembers = 0;
  /// Number of arificial variables, parameters or members across all instances.
  uint64_t NumArtificial = 0;
  /// List of all Variables and parameters in this function.
  StringSet<> VarsInFunction;
  /// Compile units also cover a PC range, but have this flag set to false.
  bool IsFunction = false;
  /// Function has source location information.
  bool HasSourceLocation = false;
  /// Number of function parameters.
  uint64_t NumParams = 0;
  /// Number of function parameters with source location.
  uint64_t NumParamSourceLocations = 0;
  /// Number of function parameters with type.
  uint64_t NumParamTypes = 0;
  /// Number of function parameters with a DW_AT_location.
  uint64_t NumParamLocations = 0;
  /// Number of local variables.
  uint64_t NumLocalVars = 0;
  /// Number of local variables with source location.
  uint64_t NumLocalVarSourceLocations = 0;
  /// Number of local variables with type.
  uint64_t NumLocalVarTypes = 0;
  /// Number of local variables with DW_AT_location.
  uint64_t NumLocalVarLocations = 0;
};

/// Holds accumulated global statistics about DIEs.
struct GlobalStats {
  /// Total number of PC range bytes covered by DW_AT_locations.
  SaturatingUINT64 TotalBytesCovered = 0;
  /// Total number of parent DIE PC range bytes covered by DW_AT_Locations.
  SaturatingUINT64 ScopeBytesCovered = 0;
  /// Total number of PC range bytes in each variable's enclosing scope.
  SaturatingUINT64 ScopeBytes = 0;
  /// Total number of PC range bytes covered by DW_AT_locations with
  /// the debug entry values (DW_OP_entry_value).
  SaturatingUINT64 ScopeEntryValueBytesCovered = 0;
  /// Total number of PC range bytes covered by DW_AT_locations of
  /// formal parameters.
  SaturatingUINT64 ParamScopeBytesCovered = 0;
  /// Total number of PC range bytes in each parameter's enclosing scope.
  SaturatingUINT64 ParamScopeBytes = 0;
  /// Total number of PC range bytes covered by DW_AT_locations with
  /// the debug entry values (DW_OP_entry_value) (only for parameters).
  SaturatingUINT64 ParamScopeEntryValueBytesCovered = 0;
  /// Total number of PC range bytes covered by DW_AT_locations (only for local
  /// variables).
  SaturatingUINT64 LocalVarScopeBytesCovered = 0;
  /// Total number of PC range bytes in each local variable's enclosing scope.
  SaturatingUINT64 LocalVarScopeBytes = 0;
  /// Total number of PC range bytes covered by DW_AT_locations with
  /// the debug entry values (DW_OP_entry_value) (only for local variables).
  SaturatingUINT64 LocalVarScopeEntryValueBytesCovered = 0;
  /// Total number of call site entries (DW_AT_call_file & DW_AT_call_line).
  SaturatingUINT64 CallSiteEntries = 0;
  /// Total number of call site DIEs (DW_TAG_call_site).
  SaturatingUINT64 CallSiteDIEs = 0;
  /// Total number of call site parameter DIEs (DW_TAG_call_site_parameter).
  SaturatingUINT64 CallSiteParamDIEs = 0;
  /// Total byte size of concrete functions. This byte size includes
  /// inline functions contained in the concrete functions.
  SaturatingUINT64 FunctionSize = 0;
  /// Total byte size of inlined functions. This is the total number of bytes
  /// for the top inline functions within concrete functions. This can help
  /// tune the inline settings when compiling to match user expectations.
  SaturatingUINT64 InlineFunctionSize = 0;
};

/// Holds accumulated debug location statistics about local variables and
/// formal parameters.
struct LocationStats {
  /// Map the scope coverage decile to the number of variables in the decile.
  /// The first element of the array (at the index zero) represents the number
  /// of variables with the no debug location at all, but the last element
  /// in the vector represents the number of fully covered variables within
  /// its scope.
  std::vector<SaturatingUINT64> VarParamLocStats{
      std::vector<SaturatingUINT64>(NumOfCoverageCategories, 0)};
  /// Map non debug entry values coverage.
  std::vector<SaturatingUINT64> VarParamNonEntryValLocStats{
      std::vector<SaturatingUINT64>(NumOfCoverageCategories, 0)};
  /// The debug location statistics for formal parameters.
  std::vector<SaturatingUINT64> ParamLocStats{
      std::vector<SaturatingUINT64>(NumOfCoverageCategories, 0)};
  /// Map non debug entry values coverage for formal parameters.
  std::vector<SaturatingUINT64> ParamNonEntryValLocStats{
      std::vector<SaturatingUINT64>(NumOfCoverageCategories, 0)};
  /// The debug location statistics for local variables.
  std::vector<SaturatingUINT64> LocalVarLocStats{
      std::vector<SaturatingUINT64>(NumOfCoverageCategories, 0)};
  /// Map non debug entry values coverage for local variables.
  std::vector<SaturatingUINT64> LocalVarNonEntryValLocStats{
      std::vector<SaturatingUINT64>(NumOfCoverageCategories, 0)};
  /// Total number of local variables and function parameters processed.
  SaturatingUINT64 NumVarParam = 0;
  /// Total number of formal parameters processed.
  SaturatingUINT64 NumParam = 0;
  /// Total number of local variables processed.
  SaturatingUINT64 NumVar = 0;
};

/// Holds accumulated debug line statistics across all CUs.
struct LineStats {
  SaturatingUINT64 NumBytes = 0;
  SaturatingUINT64 NumLineZeroBytes = 0;
  SaturatingUINT64 NumEntries = 0;
  SaturatingUINT64 NumIsStmtEntries = 0;
  SaturatingUINT64 NumUniqueEntries = 0;
  SaturatingUINT64 NumUniqueNonZeroEntries = 0;
};
} // namespace

/// Collect debug location statistics for one DIE.
static void collectLocStats(uint64_t ScopeBytesCovered, uint64_t BytesInScope,
                            std::vector<SaturatingUINT64> &VarParamLocStats,
                            std::vector<SaturatingUINT64> &ParamLocStats,
                            std::vector<SaturatingUINT64> &LocalVarLocStats,
                            bool IsParam, bool IsLocalVar) {
  auto getCoverageBucket = [ScopeBytesCovered, BytesInScope]() -> unsigned {
    // No debug location at all for the variable.
    if (ScopeBytesCovered == 0)
      return 0;
    // Fully covered variable within its scope.
    if (ScopeBytesCovered >= BytesInScope)
      return NumOfCoverageCategories - 1;
    // Get covered range (e.g. 20%-29%).
    unsigned LocBucket = 100 * (double)ScopeBytesCovered / BytesInScope;
    LocBucket /= 10;
    return LocBucket + 1;
  };

  unsigned CoverageBucket = getCoverageBucket();

  VarParamLocStats[CoverageBucket].Value++;
  if (IsParam)
    ParamLocStats[CoverageBucket].Value++;
  else if (IsLocalVar)
    LocalVarLocStats[CoverageBucket].Value++;
}

/// Construct an identifier for a given DIE from its Prefix, Name, DeclFileName
/// and DeclLine. The identifier aims to be unique for any unique entities,
/// but keeping the same among different instances of the same entity.
static std::string constructDieID(DWARFDie Die,
                                  StringRef Prefix = StringRef()) {
  std::string IDStr;
  llvm::raw_string_ostream ID(IDStr);
  ID << Prefix
     << Die.getName(DINameKind::LinkageName);

  // Prefix + Name is enough for local variables and parameters.
  if (!Prefix.empty() && Prefix != "g")
    return ID.str();

  auto DeclFile = Die.findRecursively(dwarf::DW_AT_decl_file);
  std::string File;
  if (DeclFile) {
    DWARFUnit *U = Die.getDwarfUnit();
    if (const auto *LT = U->getContext().getLineTableForUnit(U))
      if (LT->getFileNameByIndex(
              dwarf::toUnsigned(DeclFile, 0), U->getCompilationDir(),
              DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath, File))
        File = std::string(sys::path::filename(File));
  }
  ID << ":" << (File.empty() ? "/" : File);
  ID << ":"
     << dwarf::toUnsigned(Die.findRecursively(dwarf::DW_AT_decl_line), 0);
  return ID.str();
}

/// Return the number of bytes in the overlap of ranges A and B.
static uint64_t calculateOverlap(DWARFAddressRange A, DWARFAddressRange B) {
  uint64_t Lower = std::max(A.LowPC, B.LowPC);
  uint64_t Upper = std::min(A.HighPC, B.HighPC);
  if (Lower >= Upper)
    return 0;
  return Upper - Lower;
}

/// Collect debug info quality metrics for one DIE.
static void collectStatsForDie(DWARFDie Die, const std::string &FnPrefix,
                               const std::string &VarPrefix,
                               uint64_t BytesInScope, uint32_t InlineDepth,
                               StringMap<PerFunctionStats> &FnStatMap,
                               GlobalStats &GlobalStats,
                               LocationStats &LocStats,
                               AbstractOriginVarsTy *AbstractOriginVariables) {
  const dwarf::Tag Tag = Die.getTag();
  // Skip CU node.
  if (Tag == dwarf::DW_TAG_compile_unit)
    return;

  bool HasLoc = false;
  bool HasSrcLoc = false;
  bool HasType = false;
  uint64_t TotalBytesCovered = 0;
  uint64_t ScopeBytesCovered = 0;
  uint64_t BytesEntryValuesCovered = 0;
  auto &FnStats = FnStatMap[FnPrefix];
  bool IsParam = Tag == dwarf::DW_TAG_formal_parameter;
  bool IsLocalVar = Tag == dwarf::DW_TAG_variable;
  bool IsConstantMember = Tag == dwarf::DW_TAG_member &&
                          Die.find(dwarf::DW_AT_const_value);

  // For zero covered inlined variables the locstats will be
  // calculated later.
  bool DeferLocStats = false;

  if (Tag == dwarf::DW_TAG_call_site || Tag == dwarf::DW_TAG_GNU_call_site) {
    GlobalStats.CallSiteDIEs++;
    return;
  }

  if (Tag == dwarf::DW_TAG_call_site_parameter ||
      Tag == dwarf::DW_TAG_GNU_call_site_parameter) {
    GlobalStats.CallSiteParamDIEs++;
    return;
  }

  if (!IsParam && !IsLocalVar && !IsConstantMember) {
    // Not a variable or constant member.
    return;
  }

  // Ignore declarations of global variables.
  if (IsLocalVar && Die.find(dwarf::DW_AT_declaration))
    return;

  if (Die.findRecursively(dwarf::DW_AT_decl_file) &&
      Die.findRecursively(dwarf::DW_AT_decl_line))
    HasSrcLoc = true;

  if (Die.findRecursively(dwarf::DW_AT_type))
    HasType = true;

  if (Die.find(dwarf::DW_AT_abstract_origin)) {
    if (Die.find(dwarf::DW_AT_location) || Die.find(dwarf::DW_AT_const_value)) {
      if (AbstractOriginVariables) {
        auto Offset = Die.find(dwarf::DW_AT_abstract_origin);
        // Do not track this variable any more, since it has location
        // coverage.
        llvm::erase(*AbstractOriginVariables, (*Offset).getRawUValue());
      }
    } else {
      // The locstats will be handled at the end of
      // the collectStatsRecursive().
      DeferLocStats = true;
    }
  }

  auto IsEntryValue = [&](ArrayRef<uint8_t> D) -> bool {
    DWARFUnit *U = Die.getDwarfUnit();
    DataExtractor Data(toStringRef(D),
                       Die.getDwarfUnit()->getContext().isLittleEndian(), 0);
    DWARFExpression Expression(Data, U->getAddressByteSize(),
                               U->getFormParams().Format);
    // Consider the expression containing the DW_OP_entry_value as
    // an entry value.
    return llvm::any_of(Expression, [](const DWARFExpression::Operation &Op) {
      return Op.getCode() == dwarf::DW_OP_entry_value ||
             Op.getCode() == dwarf::DW_OP_GNU_entry_value;
    });
  };

  if (Die.find(dwarf::DW_AT_const_value)) {
    // This catches constant members *and* variables.
    HasLoc = true;
    ScopeBytesCovered = BytesInScope;
    TotalBytesCovered = BytesInScope;
  } else {
    // Handle variables and function arguments.
    Expected<std::vector<DWARFLocationExpression>> Loc =
        Die.getLocations(dwarf::DW_AT_location);
    if (!Loc) {
      consumeError(Loc.takeError());
    } else {
      HasLoc = true;
      // Get PC coverage.
      auto Default = find_if(
          *Loc, [](const DWARFLocationExpression &L) { return !L.Range; });
      if (Default != Loc->end()) {
        // Assume the entire range is covered by a single location.
        ScopeBytesCovered = BytesInScope;
        TotalBytesCovered = BytesInScope;
      } else {
        // Caller checks this Expected result already, it cannot fail.
        auto ScopeRanges = cantFail(Die.getParent().getAddressRanges());
        for (auto Entry : *Loc) {
          TotalBytesCovered += Entry.Range->HighPC - Entry.Range->LowPC;
          uint64_t ScopeBytesCoveredByEntry = 0;
          // Calculate how many bytes of the parent scope this entry covers.
          // FIXME: In section 2.6.2 of the DWARFv5 spec it says that "The
          // address ranges defined by the bounded location descriptions of a
          // location list may overlap". So in theory a variable can have
          // multiple simultaneous locations, which would make this calculation
          // misleading because we will count the overlapped areas
          // twice. However, clang does not currently emit DWARF like this.
          for (DWARFAddressRange R : ScopeRanges) {
            ScopeBytesCoveredByEntry += calculateOverlap(*Entry.Range, R);
          }
          ScopeBytesCovered += ScopeBytesCoveredByEntry;
          if (IsEntryValue(Entry.Expr))
            BytesEntryValuesCovered += ScopeBytesCoveredByEntry;
        }
      }
    }
  }

  // Calculate the debug location statistics.
  if (BytesInScope && !DeferLocStats) {
    LocStats.NumVarParam.Value++;
    if (IsParam)
      LocStats.NumParam.Value++;
    else if (IsLocalVar)
      LocStats.NumVar.Value++;

    collectLocStats(ScopeBytesCovered, BytesInScope, LocStats.VarParamLocStats,
                    LocStats.ParamLocStats, LocStats.LocalVarLocStats, IsParam,
                    IsLocalVar);
    // Non debug entry values coverage statistics.
    collectLocStats(ScopeBytesCovered - BytesEntryValuesCovered, BytesInScope,
                    LocStats.VarParamNonEntryValLocStats,
                    LocStats.ParamNonEntryValLocStats,
                    LocStats.LocalVarNonEntryValLocStats, IsParam, IsLocalVar);
  }

  // Collect PC range coverage data.
  if (DWARFDie D =
          Die.getAttributeValueAsReferencedDie(dwarf::DW_AT_abstract_origin))
    Die = D;

  std::string VarID = constructDieID(Die, VarPrefix);
  FnStats.VarsInFunction.insert(VarID);

  GlobalStats.TotalBytesCovered += TotalBytesCovered;
  if (BytesInScope) {
    GlobalStats.ScopeBytesCovered += ScopeBytesCovered;
    GlobalStats.ScopeBytes += BytesInScope;
    GlobalStats.ScopeEntryValueBytesCovered += BytesEntryValuesCovered;
    if (IsParam) {
      GlobalStats.ParamScopeBytesCovered += ScopeBytesCovered;
      GlobalStats.ParamScopeBytes += BytesInScope;
      GlobalStats.ParamScopeEntryValueBytesCovered += BytesEntryValuesCovered;
    } else if (IsLocalVar) {
      GlobalStats.LocalVarScopeBytesCovered += ScopeBytesCovered;
      GlobalStats.LocalVarScopeBytes += BytesInScope;
      GlobalStats.LocalVarScopeEntryValueBytesCovered +=
          BytesEntryValuesCovered;
    }
    assert(GlobalStats.ScopeBytesCovered.Value <= GlobalStats.ScopeBytes.Value);
  }

  if (IsConstantMember) {
    FnStats.ConstantMembers++;
    return;
  }

  FnStats.TotalVarWithLoc += (unsigned)HasLoc;

  if (Die.find(dwarf::DW_AT_artificial)) {
    FnStats.NumArtificial++;
    return;
  }

  if (IsParam) {
    FnStats.NumParams++;
    if (HasType)
      FnStats.NumParamTypes++;
    if (HasSrcLoc)
      FnStats.NumParamSourceLocations++;
    if (HasLoc)
      FnStats.NumParamLocations++;
  } else if (IsLocalVar) {
    FnStats.NumLocalVars++;
    if (HasType)
      FnStats.NumLocalVarTypes++;
    if (HasSrcLoc)
      FnStats.NumLocalVarSourceLocations++;
    if (HasLoc)
      FnStats.NumLocalVarLocations++;
  }
}

/// Recursively collect variables from subprogram with DW_AT_inline attribute.
static void collectAbstractOriginFnInfo(
    DWARFDie Die, uint64_t SPOffset,
    AbstractOriginVarsTyMap &GlobalAbstractOriginFnInfo,
    AbstractOriginVarsTyMap &LocalAbstractOriginFnInfo) {
  DWARFDie Child = Die.getFirstChild();
  while (Child) {
    const dwarf::Tag ChildTag = Child.getTag();
    if (ChildTag == dwarf::DW_TAG_formal_parameter ||
        ChildTag == dwarf::DW_TAG_variable) {
      GlobalAbstractOriginFnInfo[SPOffset].push_back(Child.getOffset());
      LocalAbstractOriginFnInfo[SPOffset].push_back(Child.getOffset());
    } else if (ChildTag == dwarf::DW_TAG_lexical_block)
      collectAbstractOriginFnInfo(Child, SPOffset, GlobalAbstractOriginFnInfo,
                                  LocalAbstractOriginFnInfo);
    Child = Child.getSibling();
  }
}

/// Recursively collect debug info quality metrics.
static void collectStatsRecursive(
    DWARFDie Die, std::string FnPrefix, std::string VarPrefix,
    uint64_t BytesInScope, uint32_t InlineDepth,
    StringMap<PerFunctionStats> &FnStatMap, GlobalStats &GlobalStats,
    LocationStats &LocStats, FunctionDIECUTyMap &AbstractOriginFnCUs,
    AbstractOriginVarsTyMap &GlobalAbstractOriginFnInfo,
    AbstractOriginVarsTyMap &LocalAbstractOriginFnInfo,
    FunctionsWithAbstractOriginTy &FnsWithAbstractOriginToBeProcessed,
    AbstractOriginVarsTy *AbstractOriginVarsPtr = nullptr) {
  // Skip NULL nodes.
  if (Die.isNULL())
    return;

  const dwarf::Tag Tag = Die.getTag();
  // Skip function types.
  if (Tag == dwarf::DW_TAG_subroutine_type)
    return;

  // Handle any kind of lexical scope.
  const bool HasAbstractOrigin =
      Die.find(dwarf::DW_AT_abstract_origin) != std::nullopt;
  const bool IsFunction = Tag == dwarf::DW_TAG_subprogram;
  const bool IsBlock = Tag == dwarf::DW_TAG_lexical_block;
  const bool IsInlinedFunction = Tag == dwarf::DW_TAG_inlined_subroutine;
  // We want to know how many variables (with abstract_origin) don't have
  // location info.
  const bool IsCandidateForZeroLocCovTracking =
      (IsInlinedFunction || (IsFunction && HasAbstractOrigin));

  AbstractOriginVarsTy AbstractOriginVars;

  // Get the vars of the inlined fn, so the locstats
  // reports the missing vars (with coverage 0%).
  if (IsCandidateForZeroLocCovTracking) {
    auto OffsetFn = Die.find(dwarf::DW_AT_abstract_origin);
    if (OffsetFn) {
      uint64_t OffsetOfInlineFnCopy = (*OffsetFn).getRawUValue();
      if (LocalAbstractOriginFnInfo.count(OffsetOfInlineFnCopy)) {
        AbstractOriginVars = LocalAbstractOriginFnInfo[OffsetOfInlineFnCopy];
        AbstractOriginVarsPtr = &AbstractOriginVars;
      } else {
        // This means that the DW_AT_inline fn copy is out of order
        // or that the abstract_origin references another CU,
        // so this abstract origin instance will be processed later.
        FnsWithAbstractOriginToBeProcessed.push_back(Die.getOffset());
        AbstractOriginVarsPtr = nullptr;
      }
    }
  }

  if (IsFunction || IsInlinedFunction || IsBlock) {
    // Reset VarPrefix when entering a new function.
    if (IsFunction || IsInlinedFunction)
      VarPrefix = "v";

    // Ignore forward declarations.
    if (Die.find(dwarf::DW_AT_declaration))
      return;

    // Check for call sites.
    if (Die.find(dwarf::DW_AT_call_file) && Die.find(dwarf::DW_AT_call_line))
      GlobalStats.CallSiteEntries++;

    // PC Ranges.
    auto RangesOrError = Die.getAddressRanges();
    if (!RangesOrError) {
      llvm::consumeError(RangesOrError.takeError());
      return;
    }

    auto Ranges = RangesOrError.get();
    uint64_t BytesInThisScope = 0;
    for (auto Range : Ranges)
      BytesInThisScope += Range.HighPC - Range.LowPC;

    // Count the function.
    if (!IsBlock) {
      // Skip over abstract origins, but collect variables
      // from it so it can be used for location statistics
      // for inlined instancies.
      if (Die.find(dwarf::DW_AT_inline)) {
        uint64_t SPOffset = Die.getOffset();
        AbstractOriginFnCUs[SPOffset] = Die.getDwarfUnit();
        collectAbstractOriginFnInfo(Die, SPOffset, GlobalAbstractOriginFnInfo,
                                    LocalAbstractOriginFnInfo);
        return;
      }

      std::string FnID = constructDieID(Die);
      // We've seen an instance of this function.
      auto &FnStats = FnStatMap[FnID];
      FnStats.IsFunction = true;
      if (IsInlinedFunction) {
        FnStats.NumFnInlined++;
        if (Die.findRecursively(dwarf::DW_AT_abstract_origin))
          FnStats.NumAbstractOrigins++;
      } else {
        FnStats.NumFnOutOfLine++;
      }
      if (Die.findRecursively(dwarf::DW_AT_decl_file) &&
          Die.findRecursively(dwarf::DW_AT_decl_line))
        FnStats.HasSourceLocation = true;
      // Update function prefix.
      FnPrefix = FnID;
    }

    if (BytesInThisScope) {
      BytesInScope = BytesInThisScope;
      if (IsFunction)
        GlobalStats.FunctionSize += BytesInThisScope;
      else if (IsInlinedFunction && InlineDepth == 0)
        GlobalStats.InlineFunctionSize += BytesInThisScope;
    }
  } else {
    // Not a scope, visit the Die itself. It could be a variable.
    collectStatsForDie(Die, FnPrefix, VarPrefix, BytesInScope, InlineDepth,
                       FnStatMap, GlobalStats, LocStats, AbstractOriginVarsPtr);
  }

  // Set InlineDepth correctly for child recursion
  if (IsFunction)
    InlineDepth = 0;
  else if (IsInlinedFunction)
    ++InlineDepth;

  // Traverse children.
  unsigned LexicalBlockIndex = 0;
  unsigned FormalParameterIndex = 0;
  DWARFDie Child = Die.getFirstChild();
  while (Child) {
    std::string ChildVarPrefix = VarPrefix;
    if (Child.getTag() == dwarf::DW_TAG_lexical_block)
      ChildVarPrefix += toHex(LexicalBlockIndex++) + '.';
    if (Child.getTag() == dwarf::DW_TAG_formal_parameter)
      ChildVarPrefix += 'p' + toHex(FormalParameterIndex++) + '.';

    collectStatsRecursive(
        Child, FnPrefix, ChildVarPrefix, BytesInScope, InlineDepth, FnStatMap,
        GlobalStats, LocStats, AbstractOriginFnCUs, GlobalAbstractOriginFnInfo,
        LocalAbstractOriginFnInfo, FnsWithAbstractOriginToBeProcessed,
        AbstractOriginVarsPtr);
    Child = Child.getSibling();
  }

  if (!IsCandidateForZeroLocCovTracking)
    return;

  // After we have processed all vars of the inlined function (or function with
  // an abstract_origin), we want to know how many variables have no location.
  for (auto Offset : AbstractOriginVars) {
    LocStats.NumVarParam++;
    LocStats.VarParamLocStats[ZeroCoverageBucket]++;
    auto FnDie = Die.getDwarfUnit()->getDIEForOffset(Offset);
    if (!FnDie)
      continue;
    auto Tag = FnDie.getTag();
    if (Tag == dwarf::DW_TAG_formal_parameter) {
      LocStats.NumParam++;
      LocStats.ParamLocStats[ZeroCoverageBucket]++;
    } else if (Tag == dwarf::DW_TAG_variable) {
      LocStats.NumVar++;
      LocStats.LocalVarLocStats[ZeroCoverageBucket]++;
    }
  }
}

/// Print human-readable output.
/// \{
static void printDatum(json::OStream &J, const char *Key, json::Value Value) {
  if (Value == OverflowValue)
    J.attribute(Key, "overflowed");
  else
    J.attribute(Key, Value);

  LLVM_DEBUG(llvm::dbgs() << Key << ": " << Value << '\n');
}

static void printLocationStats(json::OStream &J, const char *Key,
                               std::vector<SaturatingUINT64> &LocationStats) {
  if (LocationStats[0].Value == OverflowValue)
    J.attribute((Twine(Key) +
                 " with (0%,10%) of parent scope covered by DW_AT_location")
                    .str(),
                "overflowed");
  else
    J.attribute(
        (Twine(Key) + " with 0% of parent scope covered by DW_AT_location")
            .str(),
        LocationStats[0].Value);
  LLVM_DEBUG(
      llvm::dbgs() << Key
                   << " with 0% of parent scope covered by DW_AT_location: \\"
                   << LocationStats[0].Value << '\n');

  if (LocationStats[1].Value == OverflowValue)
    J.attribute((Twine(Key) +
                 " with (0%,10%) of parent scope covered by DW_AT_location")
                    .str(),
                "overflowed");
  else
    J.attribute((Twine(Key) +
                 " with (0%,10%) of parent scope covered by DW_AT_location")
                    .str(),
                LocationStats[1].Value);
  LLVM_DEBUG(llvm::dbgs()
             << Key
             << " with (0%,10%) of parent scope covered by DW_AT_location: "
             << LocationStats[1].Value << '\n');

  for (unsigned i = 2; i < NumOfCoverageCategories - 1; ++i) {
    if (LocationStats[i].Value == OverflowValue)
      J.attribute((Twine(Key) + " with [" + Twine((i - 1) * 10) + "%," +
                   Twine(i * 10) +
                   "%) of parent scope covered by DW_AT_location")
                      .str(),
                  "overflowed");
    else
      J.attribute((Twine(Key) + " with [" + Twine((i - 1) * 10) + "%," +
                   Twine(i * 10) +
                   "%) of parent scope covered by DW_AT_location")
                      .str(),
                  LocationStats[i].Value);
    LLVM_DEBUG(llvm::dbgs()
               << Key << " with [" << (i - 1) * 10 << "%," << i * 10
               << "%) of parent scope covered by DW_AT_location: "
               << LocationStats[i].Value);
  }
  if (LocationStats[NumOfCoverageCategories - 1].Value == OverflowValue)
    J.attribute(
        (Twine(Key) + " with 100% of parent scope covered by DW_AT_location")
            .str(),
        "overflowed");
  else
    J.attribute(
        (Twine(Key) + " with 100% of parent scope covered by DW_AT_location")
            .str(),
        LocationStats[NumOfCoverageCategories - 1].Value);
  LLVM_DEBUG(
      llvm::dbgs() << Key
                   << " with 100% of parent scope covered by DW_AT_location: "
                   << LocationStats[NumOfCoverageCategories - 1].Value);
}

static void printSectionSizes(json::OStream &J, const SectionSizes &Sizes) {
  for (const auto &It : Sizes.DebugSectionSizes)
    J.attribute((Twine("#bytes in ") + It.first).str(), int64_t(It.second));
}

/// Stop tracking variables that contain abstract_origin with a location.
/// This is used for out-of-order DW_AT_inline subprograms only.
static void updateVarsWithAbstractOriginLocCovInfo(
    DWARFDie FnDieWithAbstractOrigin,
    AbstractOriginVarsTy &AbstractOriginVars) {
  DWARFDie Child = FnDieWithAbstractOrigin.getFirstChild();
  while (Child) {
    const dwarf::Tag ChildTag = Child.getTag();
    if ((ChildTag == dwarf::DW_TAG_formal_parameter ||
         ChildTag == dwarf::DW_TAG_variable) &&
        (Child.find(dwarf::DW_AT_location) ||
         Child.find(dwarf::DW_AT_const_value))) {
      auto OffsetVar = Child.find(dwarf::DW_AT_abstract_origin);
      if (OffsetVar)
        llvm::erase(AbstractOriginVars, (*OffsetVar).getRawUValue());
    } else if (ChildTag == dwarf::DW_TAG_lexical_block)
      updateVarsWithAbstractOriginLocCovInfo(Child, AbstractOriginVars);
    Child = Child.getSibling();
  }
}

/// Collect zero location coverage for inlined variables which refer to
/// a DW_AT_inline copy of subprogram that is out of order in the DWARF.
/// Also cover the variables of a concrete function (represented with
/// the DW_TAG_subprogram) with an abstract_origin attribute.
static void collectZeroLocCovForVarsWithAbstractOrigin(
    DWARFUnit *DwUnit, GlobalStats &GlobalStats, LocationStats &LocStats,
    AbstractOriginVarsTyMap &LocalAbstractOriginFnInfo,
    FunctionsWithAbstractOriginTy &FnsWithAbstractOriginToBeProcessed) {
  // The next variable is used to filter out functions that have been processed,
  // leaving FnsWithAbstractOriginToBeProcessed with just CrossCU references.
  FunctionsWithAbstractOriginTy ProcessedFns;
  for (auto FnOffset : FnsWithAbstractOriginToBeProcessed) {
    DWARFDie FnDieWithAbstractOrigin = DwUnit->getDIEForOffset(FnOffset);
    auto FnCopy = FnDieWithAbstractOrigin.find(dwarf::DW_AT_abstract_origin);
    AbstractOriginVarsTy AbstractOriginVars;
    if (!FnCopy)
      continue;
    uint64_t FnCopyRawUValue = (*FnCopy).getRawUValue();
    // If there is no entry within LocalAbstractOriginFnInfo for the given
    // FnCopyRawUValue, function isn't out-of-order in DWARF. Rather, we have
    // CrossCU referencing.
    if (!LocalAbstractOriginFnInfo.count(FnCopyRawUValue))
      continue;
    AbstractOriginVars = LocalAbstractOriginFnInfo[FnCopyRawUValue];
    updateVarsWithAbstractOriginLocCovInfo(FnDieWithAbstractOrigin,
                                           AbstractOriginVars);

    for (auto Offset : AbstractOriginVars) {
      LocStats.NumVarParam++;
      LocStats.VarParamLocStats[ZeroCoverageBucket]++;
      auto Tag = DwUnit->getDIEForOffset(Offset).getTag();
      if (Tag == dwarf::DW_TAG_formal_parameter) {
        LocStats.NumParam++;
        LocStats.ParamLocStats[ZeroCoverageBucket]++;
      } else if (Tag == dwarf::DW_TAG_variable) {
        LocStats.NumVar++;
        LocStats.LocalVarLocStats[ZeroCoverageBucket]++;
      }
    }
    ProcessedFns.push_back(FnOffset);
  }
  for (auto ProcessedFn : ProcessedFns)
    llvm::erase(FnsWithAbstractOriginToBeProcessed, ProcessedFn);
}

/// Collect zero location coverage for inlined variables which refer to
/// a DW_AT_inline copy of subprogram that is in a different CU.
static void collectZeroLocCovForVarsWithCrossCUReferencingAbstractOrigin(
    LocationStats &LocStats, FunctionDIECUTyMap AbstractOriginFnCUs,
    AbstractOriginVarsTyMap &GlobalAbstractOriginFnInfo,
    CrossCUReferencingDIELocationTy &CrossCUReferencesToBeResolved) {
  for (const auto &CrossCUReferenceToBeResolved :
       CrossCUReferencesToBeResolved) {
    DWARFUnit *DwUnit = CrossCUReferenceToBeResolved.DwUnit;
    DWARFDie FnDIEWithCrossCUReferencing =
        DwUnit->getDIEForOffset(CrossCUReferenceToBeResolved.DIEOffset);
    auto FnCopy =
        FnDIEWithCrossCUReferencing.find(dwarf::DW_AT_abstract_origin);
    if (!FnCopy)
      continue;
    uint64_t FnCopyRawUValue = (*FnCopy).getRawUValue();
    AbstractOriginVarsTy AbstractOriginVars =
        GlobalAbstractOriginFnInfo[FnCopyRawUValue];
    updateVarsWithAbstractOriginLocCovInfo(FnDIEWithCrossCUReferencing,
                                           AbstractOriginVars);
    for (auto Offset : AbstractOriginVars) {
      LocStats.NumVarParam++;
      LocStats.VarParamLocStats[ZeroCoverageBucket]++;
      auto Tag = (AbstractOriginFnCUs[FnCopyRawUValue])
                     ->getDIEForOffset(Offset)
                     .getTag();
      if (Tag == dwarf::DW_TAG_formal_parameter) {
        LocStats.NumParam++;
        LocStats.ParamLocStats[ZeroCoverageBucket]++;
      } else if (Tag == dwarf::DW_TAG_variable) {
        LocStats.NumVar++;
        LocStats.LocalVarLocStats[ZeroCoverageBucket]++;
      }
    }
  }
}

/// \}

/// Collect debug info quality metrics for an entire DIContext.
///
/// Do the impossible and reduce the quality of the debug info down to a few
/// numbers. The idea is to condense the data into numbers that can be tracked
/// over time to identify trends in newer compiler versions and gauge the effect
/// of particular optimizations. The raw numbers themselves are not particularly
/// useful, only the delta between compiling the same program with different
/// compilers is.
bool dwarfdump::collectStatsForObjectFile(ObjectFile &Obj, DWARFContext &DICtx,
                                          const Twine &Filename,
                                          raw_ostream &OS) {
  StringRef FormatName = Obj.getFileFormatName();
  GlobalStats GlobalStats;
  LocationStats LocStats;
  LineStats LnStats;
  StringMap<PerFunctionStats> Statistics;
  // This variable holds variable information for functions with
  // abstract_origin globally, across all CUs.
  AbstractOriginVarsTyMap GlobalAbstractOriginFnInfo;
  // This variable holds information about the CU of a function with
  // abstract_origin.
  FunctionDIECUTyMap AbstractOriginFnCUs;
  CrossCUReferencingDIELocationTy CrossCUReferencesToBeResolved;
  // Tuple representing a single source code position in the line table. Fields
  // are respectively: Line, Col, File, where 'File' is an index into the Files
  // vector below.
  using LineTuple = std::tuple<uint32_t, uint16_t, uint16_t>;
  SmallVector<std::string> Files;
  DenseSet<LineTuple> UniqueLines;
  DenseSet<LineTuple> UniqueNonZeroLines;

  for (const auto &CU : static_cast<DWARFContext *>(&DICtx)->compile_units()) {
    if (DWARFDie CUDie = CU->getNonSkeletonUnitDIE(false)) {
      // This variable holds variable information for functions with
      // abstract_origin, but just for the current CU.
      AbstractOriginVarsTyMap LocalAbstractOriginFnInfo;
      FunctionsWithAbstractOriginTy FnsWithAbstractOriginToBeProcessed;

      collectStatsRecursive(
          CUDie, "/", "g", 0, 0, Statistics, GlobalStats, LocStats,
          AbstractOriginFnCUs, GlobalAbstractOriginFnInfo,
          LocalAbstractOriginFnInfo, FnsWithAbstractOriginToBeProcessed);

      // collectZeroLocCovForVarsWithAbstractOrigin will filter out all
      // out-of-order DWARF functions that have been processed within it,
      // leaving FnsWithAbstractOriginToBeProcessed with only CrossCU
      // references.
      collectZeroLocCovForVarsWithAbstractOrigin(
          CUDie.getDwarfUnit(), GlobalStats, LocStats,
          LocalAbstractOriginFnInfo, FnsWithAbstractOriginToBeProcessed);

      // Collect all CrossCU references into CrossCUReferencesToBeResolved.
      for (auto CrossCUReferencingDIEOffset :
           FnsWithAbstractOriginToBeProcessed)
        CrossCUReferencesToBeResolved.push_back(
            DIELocation(CUDie.getDwarfUnit(), CrossCUReferencingDIEOffset));
    }
    const auto *LineTable = DICtx.getLineTableForUnit(CU.get());
    std::optional<uint64_t> LastFileIdxOpt;
    if (LineTable)
      LastFileIdxOpt = LineTable->getLastValidFileIndex();
    if (LastFileIdxOpt) {
      // Each CU has its own file index; in order to track unique line entries
      // across CUs, we therefore need to map each CU file index to a global
      // file index, which we store here.
      DenseMap<uint64_t, uint16_t> CUFileMapping;
      for (uint64_t FileIdx = 0; FileIdx <= *LastFileIdxOpt; ++FileIdx) {
        std::string File;
        if (LineTable->getFileNameByIndex(
                FileIdx, CU->getCompilationDir(),
                DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath,
                File)) {
          auto ExistingFile = llvm::find(Files, File);
          if (ExistingFile != Files.end()) {
            CUFileMapping[FileIdx] = std::distance(Files.begin(), ExistingFile);
          } else {
            CUFileMapping[FileIdx] = Files.size();
            Files.push_back(File);
          }
        }
      }
      for (const auto &Seq : LineTable->Sequences) {
        LnStats.NumBytes += Seq.HighPC - Seq.LowPC;
        // Ignore the `end_sequence` entry, since it's not interesting for us.
        LnStats.NumEntries += Seq.LastRowIndex - Seq.FirstRowIndex - 1;
        for (size_t RowIdx = Seq.FirstRowIndex; RowIdx < Seq.LastRowIndex - 1;
             ++RowIdx) {
          auto Entry = LineTable->Rows[RowIdx];
          if (Entry.IsStmt)
            LnStats.NumIsStmtEntries += 1;
          assert(CUFileMapping.contains(Entry.File) &&
                 "Should have been collected earlier!");
          uint16_t MappedFile = CUFileMapping[Entry.File];
          UniqueLines.insert({Entry.Line, Entry.Column, MappedFile});
          if (Entry.Line != 0) {
            UniqueNonZeroLines.insert({Entry.Line, Entry.Column, MappedFile});
          } else {
            auto EntryStartAddress = Entry.Address.Address;
            auto EntryEndAddress = LineTable->Rows[RowIdx + 1].Address.Address;
            LnStats.NumLineZeroBytes += EntryEndAddress - EntryStartAddress;
          }
        }
      }
    }
  }

  LnStats.NumUniqueEntries = UniqueLines.size();
  LnStats.NumUniqueNonZeroEntries = UniqueNonZeroLines.size();

  /// Resolve CrossCU references.
  collectZeroLocCovForVarsWithCrossCUReferencingAbstractOrigin(
      LocStats, AbstractOriginFnCUs, GlobalAbstractOriginFnInfo,
      CrossCUReferencesToBeResolved);

  /// Collect the sizes of debug sections.
  SectionSizes Sizes;
  calculateSectionSizes(Obj, Sizes, Filename);

  /// The version number should be increased every time the algorithm is changed
  /// (including bug fixes). New metrics may be added without increasing the
  /// version.
  unsigned Version = 9;
  SaturatingUINT64 VarParamTotal = 0;
  SaturatingUINT64 VarParamUnique = 0;
  SaturatingUINT64 VarParamWithLoc = 0;
  SaturatingUINT64 NumFunctions = 0;
  SaturatingUINT64 NumInlinedFunctions = 0;
  SaturatingUINT64 NumFuncsWithSrcLoc = 0;
  SaturatingUINT64 NumAbstractOrigins = 0;
  SaturatingUINT64 ParamTotal = 0;
  SaturatingUINT64 ParamWithType = 0;
  SaturatingUINT64 ParamWithLoc = 0;
  SaturatingUINT64 ParamWithSrcLoc = 0;
  SaturatingUINT64 LocalVarTotal = 0;
  SaturatingUINT64 LocalVarWithType = 0;
  SaturatingUINT64 LocalVarWithSrcLoc = 0;
  SaturatingUINT64 LocalVarWithLoc = 0;
  for (auto &Entry : Statistics) {
    PerFunctionStats &Stats = Entry.getValue();
    uint64_t TotalVars = Stats.VarsInFunction.size() *
                         (Stats.NumFnInlined + Stats.NumFnOutOfLine);
    // Count variables in global scope.
    if (!Stats.IsFunction)
      TotalVars =
          Stats.NumLocalVars + Stats.ConstantMembers + Stats.NumArtificial;
    uint64_t Constants = Stats.ConstantMembers;
    VarParamWithLoc += Stats.TotalVarWithLoc + Constants;
    VarParamTotal += TotalVars;
    VarParamUnique += Stats.VarsInFunction.size();
    LLVM_DEBUG(for (auto &V
                    : Stats.VarsInFunction) llvm::dbgs()
               << Entry.getKey() << ": " << V.getKey() << "\n");
    NumFunctions += Stats.IsFunction;
    NumFuncsWithSrcLoc += Stats.HasSourceLocation;
    NumInlinedFunctions += Stats.IsFunction * Stats.NumFnInlined;
    NumAbstractOrigins += Stats.IsFunction * Stats.NumAbstractOrigins;
    ParamTotal += Stats.NumParams;
    ParamWithType += Stats.NumParamTypes;
    ParamWithLoc += Stats.NumParamLocations;
    ParamWithSrcLoc += Stats.NumParamSourceLocations;
    LocalVarTotal += Stats.NumLocalVars;
    LocalVarWithType += Stats.NumLocalVarTypes;
    LocalVarWithLoc += Stats.NumLocalVarLocations;
    LocalVarWithSrcLoc += Stats.NumLocalVarSourceLocations;
  }

  // Print summary.
  OS.SetBufferSize(1024);
  json::OStream J(OS, 2);
  J.objectBegin();
  J.attribute("version", Version);
  LLVM_DEBUG(llvm::dbgs() << "Variable location quality metrics\n";
             llvm::dbgs() << "---------------------------------\n");

  printDatum(J, "file", Filename.str());
  printDatum(J, "format", FormatName);

  printDatum(J, "#functions", NumFunctions.Value);
  printDatum(J, "#functions with location", NumFuncsWithSrcLoc.Value);
  printDatum(J, "#inlined functions", NumInlinedFunctions.Value);
  printDatum(J, "#inlined functions with abstract origins",
             NumAbstractOrigins.Value);

  // This includes local variables and formal parameters.
  printDatum(J, "#unique source variables", VarParamUnique.Value);
  printDatum(J, "#source variables", VarParamTotal.Value);
  printDatum(J, "#source variables with location", VarParamWithLoc.Value);

  printDatum(J, "#call site entries", GlobalStats.CallSiteEntries.Value);
  printDatum(J, "#call site DIEs", GlobalStats.CallSiteDIEs.Value);
  printDatum(J, "#call site parameter DIEs",
             GlobalStats.CallSiteParamDIEs.Value);

  printDatum(J, "sum_all_variables(#bytes in parent scope)",
             GlobalStats.ScopeBytes.Value);
  printDatum(J,
             "sum_all_variables(#bytes in any scope covered by DW_AT_location)",
             GlobalStats.TotalBytesCovered.Value);
  printDatum(J,
             "sum_all_variables(#bytes in parent scope covered by "
             "DW_AT_location)",
             GlobalStats.ScopeBytesCovered.Value);
  printDatum(J,
             "sum_all_variables(#bytes in parent scope covered by "
             "DW_OP_entry_value)",
             GlobalStats.ScopeEntryValueBytesCovered.Value);

  printDatum(J, "sum_all_params(#bytes in parent scope)",
             GlobalStats.ParamScopeBytes.Value);
  printDatum(J,
             "sum_all_params(#bytes in parent scope covered by DW_AT_location)",
             GlobalStats.ParamScopeBytesCovered.Value);
  printDatum(J,
             "sum_all_params(#bytes in parent scope covered by "
             "DW_OP_entry_value)",
             GlobalStats.ParamScopeEntryValueBytesCovered.Value);

  printDatum(J, "sum_all_local_vars(#bytes in parent scope)",
             GlobalStats.LocalVarScopeBytes.Value);
  printDatum(J,
             "sum_all_local_vars(#bytes in parent scope covered by "
             "DW_AT_location)",
             GlobalStats.LocalVarScopeBytesCovered.Value);
  printDatum(J,
             "sum_all_local_vars(#bytes in parent scope covered by "
             "DW_OP_entry_value)",
             GlobalStats.LocalVarScopeEntryValueBytesCovered.Value);

  printDatum(J, "#bytes within functions", GlobalStats.FunctionSize.Value);
  printDatum(J, "#bytes within inlined functions",
             GlobalStats.InlineFunctionSize.Value);

  // Print the summary for formal parameters.
  printDatum(J, "#params", ParamTotal.Value);
  printDatum(J, "#params with source location", ParamWithSrcLoc.Value);
  printDatum(J, "#params with type", ParamWithType.Value);
  printDatum(J, "#params with binary location", ParamWithLoc.Value);

  // Print the summary for local variables.
  printDatum(J, "#local vars", LocalVarTotal.Value);
  printDatum(J, "#local vars with source location", LocalVarWithSrcLoc.Value);
  printDatum(J, "#local vars with type", LocalVarWithType.Value);
  printDatum(J, "#local vars with binary location", LocalVarWithLoc.Value);

  // Print the debug section sizes.
  printSectionSizes(J, Sizes);

  // Print the location statistics for variables (includes local variables
  // and formal parameters).
  printDatum(J, "#variables processed by location statistics",
             LocStats.NumVarParam.Value);
  printLocationStats(J, "#variables", LocStats.VarParamLocStats);
  printLocationStats(J, "#variables - entry values",
                     LocStats.VarParamNonEntryValLocStats);

  // Print the location statistics for formal parameters.
  printDatum(J, "#params processed by location statistics",
             LocStats.NumParam.Value);
  printLocationStats(J, "#params", LocStats.ParamLocStats);
  printLocationStats(J, "#params - entry values",
                     LocStats.ParamNonEntryValLocStats);

  // Print the location statistics for local variables.
  printDatum(J, "#local vars processed by location statistics",
             LocStats.NumVar.Value);
  printLocationStats(J, "#local vars", LocStats.LocalVarLocStats);
  printLocationStats(J, "#local vars - entry values",
                     LocStats.LocalVarNonEntryValLocStats);

  // Print line statistics for the object file.
  printDatum(J, "#bytes with line information", LnStats.NumBytes.Value);
  printDatum(J, "#bytes with line-0 locations", LnStats.NumLineZeroBytes.Value);
  printDatum(J, "#line entries", LnStats.NumEntries.Value);
  printDatum(J, "#line entries (is_stmt)", LnStats.NumIsStmtEntries.Value);
  printDatum(J, "#line entries (unique)", LnStats.NumUniqueEntries.Value);
  printDatum(J, "#line entries (unique non-0)",
             LnStats.NumUniqueNonZeroEntries.Value);

  J.objectEnd();
  OS << '\n';
  LLVM_DEBUG(
      llvm::dbgs() << "Total Availability: "
                   << (VarParamTotal.Value
                           ? (int)std::round((VarParamWithLoc.Value * 100.0) /
                                             VarParamTotal.Value)
                           : 0)
                   << "%\n";
      llvm::dbgs() << "PC Ranges covered: "
                   << (GlobalStats.ScopeBytes.Value
                           ? (int)std::round(
                                 (GlobalStats.ScopeBytesCovered.Value * 100.0) /
                                 GlobalStats.ScopeBytes.Value)
                           : 0)
                   << "%\n");
  return true;
}

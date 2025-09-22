//===--- DiagnosticIDs.cpp - Diagnostic IDs Handling ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements the Diagnostic IDs-related interfaces.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/AllDiagnostics.h"
#include "clang/Basic/DiagnosticCategories.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/ErrorHandling.h"
#include <map>
#include <optional>
using namespace clang;

//===----------------------------------------------------------------------===//
// Builtin Diagnostic information
//===----------------------------------------------------------------------===//

namespace {

struct StaticDiagInfoRec;

// Store the descriptions in a separate table to avoid pointers that need to
// be relocated, and also decrease the amount of data needed on 64-bit
// platforms. See "How To Write Shared Libraries" by Ulrich Drepper.
struct StaticDiagInfoDescriptionStringTable {
#define DIAG(ENUM, CLASS, DEFAULT_SEVERITY, DESC, GROUP, SFINAE, NOWERROR,     \
             SHOWINSYSHEADER, SHOWINSYSMACRO, DEFERRABLE, CATEGORY)            \
  char ENUM##_desc[sizeof(DESC)];
  // clang-format off
#include "clang/Basic/DiagnosticCommonKinds.inc"
#include "clang/Basic/DiagnosticDriverKinds.inc"
#include "clang/Basic/DiagnosticFrontendKinds.inc"
#include "clang/Basic/DiagnosticSerializationKinds.inc"
#include "clang/Basic/DiagnosticLexKinds.inc"
#include "clang/Basic/DiagnosticParseKinds.inc"
#include "clang/Basic/DiagnosticASTKinds.inc"
#include "clang/Basic/DiagnosticCommentKinds.inc"
#include "clang/Basic/DiagnosticCrossTUKinds.inc"
#include "clang/Basic/DiagnosticSemaKinds.inc"
#include "clang/Basic/DiagnosticAnalysisKinds.inc"
#include "clang/Basic/DiagnosticRefactoringKinds.inc"
#include "clang/Basic/DiagnosticInstallAPIKinds.inc"
  // clang-format on
#undef DIAG
};

const StaticDiagInfoDescriptionStringTable StaticDiagInfoDescriptions = {
#define DIAG(ENUM, CLASS, DEFAULT_SEVERITY, DESC, GROUP, SFINAE, NOWERROR,     \
             SHOWINSYSHEADER, SHOWINSYSMACRO, DEFERRABLE, CATEGORY)            \
  DESC,
// clang-format off
#include "clang/Basic/DiagnosticCommonKinds.inc"
#include "clang/Basic/DiagnosticDriverKinds.inc"
#include "clang/Basic/DiagnosticFrontendKinds.inc"
#include "clang/Basic/DiagnosticSerializationKinds.inc"
#include "clang/Basic/DiagnosticLexKinds.inc"
#include "clang/Basic/DiagnosticParseKinds.inc"
#include "clang/Basic/DiagnosticASTKinds.inc"
#include "clang/Basic/DiagnosticCommentKinds.inc"
#include "clang/Basic/DiagnosticCrossTUKinds.inc"
#include "clang/Basic/DiagnosticSemaKinds.inc"
#include "clang/Basic/DiagnosticAnalysisKinds.inc"
#include "clang/Basic/DiagnosticRefactoringKinds.inc"
#include "clang/Basic/DiagnosticInstallAPIKinds.inc"
// clang-format on
#undef DIAG
};

extern const StaticDiagInfoRec StaticDiagInfo[];

// Stored separately from StaticDiagInfoRec to pack better.  Otherwise,
// StaticDiagInfoRec would have extra padding on 64-bit platforms.
const uint32_t StaticDiagInfoDescriptionOffsets[] = {
#define DIAG(ENUM, CLASS, DEFAULT_SEVERITY, DESC, GROUP, SFINAE, NOWERROR,     \
             SHOWINSYSHEADER, SHOWINSYSMACRO, DEFERRABLE, CATEGORY)            \
  offsetof(StaticDiagInfoDescriptionStringTable, ENUM##_desc),
// clang-format off
#include "clang/Basic/DiagnosticCommonKinds.inc"
#include "clang/Basic/DiagnosticDriverKinds.inc"
#include "clang/Basic/DiagnosticFrontendKinds.inc"
#include "clang/Basic/DiagnosticSerializationKinds.inc"
#include "clang/Basic/DiagnosticLexKinds.inc"
#include "clang/Basic/DiagnosticParseKinds.inc"
#include "clang/Basic/DiagnosticASTKinds.inc"
#include "clang/Basic/DiagnosticCommentKinds.inc"
#include "clang/Basic/DiagnosticCrossTUKinds.inc"
#include "clang/Basic/DiagnosticSemaKinds.inc"
#include "clang/Basic/DiagnosticAnalysisKinds.inc"
#include "clang/Basic/DiagnosticRefactoringKinds.inc"
#include "clang/Basic/DiagnosticInstallAPIKinds.inc"
// clang-format on
#undef DIAG
};

// Diagnostic classes.
enum DiagnosticClass {
  CLASS_NOTE       = 0x01,
  CLASS_REMARK     = 0x02,
  CLASS_WARNING    = 0x03,
  CLASS_EXTENSION  = 0x04,
  CLASS_ERROR      = 0x05
};

struct StaticDiagInfoRec {
  uint16_t DiagID;
  LLVM_PREFERRED_TYPE(diag::Severity)
  uint8_t DefaultSeverity : 3;
  LLVM_PREFERRED_TYPE(DiagnosticClass)
  uint8_t Class : 3;
  LLVM_PREFERRED_TYPE(DiagnosticIDs::SFINAEResponse)
  uint8_t SFINAE : 2;
  uint8_t Category : 6;
  LLVM_PREFERRED_TYPE(bool)
  uint8_t WarnNoWerror : 1;
  LLVM_PREFERRED_TYPE(bool)
  uint8_t WarnShowInSystemHeader : 1;
  LLVM_PREFERRED_TYPE(bool)
  uint8_t WarnShowInSystemMacro : 1;

  uint16_t OptionGroupIndex : 15;
  LLVM_PREFERRED_TYPE(bool)
  uint16_t Deferrable : 1;

  uint16_t DescriptionLen;

  unsigned getOptionGroupIndex() const {
    return OptionGroupIndex;
  }

  StringRef getDescription() const {
    size_t MyIndex = this - &StaticDiagInfo[0];
    uint32_t StringOffset = StaticDiagInfoDescriptionOffsets[MyIndex];
    const char* Table = reinterpret_cast<const char*>(&StaticDiagInfoDescriptions);
    return StringRef(&Table[StringOffset], DescriptionLen);
  }

  diag::Flavor getFlavor() const {
    return Class == CLASS_REMARK ? diag::Flavor::Remark
                                 : diag::Flavor::WarningOrError;
  }

  bool operator<(const StaticDiagInfoRec &RHS) const {
    return DiagID < RHS.DiagID;
  }
};

#define STRINGIFY_NAME(NAME) #NAME
#define VALIDATE_DIAG_SIZE(NAME)                                               \
  static_assert(                                                               \
      static_cast<unsigned>(diag::NUM_BUILTIN_##NAME##_DIAGNOSTICS) <          \
          static_cast<unsigned>(diag::DIAG_START_##NAME) +                     \
              static_cast<unsigned>(diag::DIAG_SIZE_##NAME),                   \
      STRINGIFY_NAME(                                                          \
          DIAG_SIZE_##NAME) " is insufficient to contain all "                 \
                            "diagnostics, it may need to be made larger in "   \
                            "DiagnosticIDs.h.");
VALIDATE_DIAG_SIZE(COMMON)
VALIDATE_DIAG_SIZE(DRIVER)
VALIDATE_DIAG_SIZE(FRONTEND)
VALIDATE_DIAG_SIZE(SERIALIZATION)
VALIDATE_DIAG_SIZE(LEX)
VALIDATE_DIAG_SIZE(PARSE)
VALIDATE_DIAG_SIZE(AST)
VALIDATE_DIAG_SIZE(COMMENT)
VALIDATE_DIAG_SIZE(CROSSTU)
VALIDATE_DIAG_SIZE(SEMA)
VALIDATE_DIAG_SIZE(ANALYSIS)
VALIDATE_DIAG_SIZE(REFACTORING)
VALIDATE_DIAG_SIZE(INSTALLAPI)
#undef VALIDATE_DIAG_SIZE
#undef STRINGIFY_NAME

const StaticDiagInfoRec StaticDiagInfo[] = {
// clang-format off
#define DIAG(ENUM, CLASS, DEFAULT_SEVERITY, DESC, GROUP, SFINAE, NOWERROR,     \
             SHOWINSYSHEADER, SHOWINSYSMACRO, DEFERRABLE, CATEGORY)            \
  {                                                                            \
      diag::ENUM,                                                              \
      DEFAULT_SEVERITY,                                                        \
      CLASS,                                                                   \
      DiagnosticIDs::SFINAE,                                                   \
      CATEGORY,                                                                \
      NOWERROR,                                                                \
      SHOWINSYSHEADER,                                                         \
      SHOWINSYSMACRO,                                                          \
      GROUP,                                                                   \
	    DEFERRABLE,                                                              \
      STR_SIZE(DESC, uint16_t)},
#include "clang/Basic/DiagnosticCommonKinds.inc"
#include "clang/Basic/DiagnosticDriverKinds.inc"
#include "clang/Basic/DiagnosticFrontendKinds.inc"
#include "clang/Basic/DiagnosticSerializationKinds.inc"
#include "clang/Basic/DiagnosticLexKinds.inc"
#include "clang/Basic/DiagnosticParseKinds.inc"
#include "clang/Basic/DiagnosticASTKinds.inc"
#include "clang/Basic/DiagnosticCommentKinds.inc"
#include "clang/Basic/DiagnosticCrossTUKinds.inc"
#include "clang/Basic/DiagnosticSemaKinds.inc"
#include "clang/Basic/DiagnosticAnalysisKinds.inc"
#include "clang/Basic/DiagnosticRefactoringKinds.inc"
#include "clang/Basic/DiagnosticInstallAPIKinds.inc"
// clang-format on
#undef DIAG
};

} // namespace

static const unsigned StaticDiagInfoSize = std::size(StaticDiagInfo);

/// GetDiagInfo - Return the StaticDiagInfoRec entry for the specified DiagID,
/// or null if the ID is invalid.
static const StaticDiagInfoRec *GetDiagInfo(unsigned DiagID) {
  // Out of bounds diag. Can't be in the table.
  using namespace diag;
  if (DiagID >= DIAG_UPPER_LIMIT || DiagID <= DIAG_START_COMMON)
    return nullptr;

  // Compute the index of the requested diagnostic in the static table.
  // 1. Add the number of diagnostics in each category preceding the
  //    diagnostic and of the category the diagnostic is in. This gives us
  //    the offset of the category in the table.
  // 2. Subtract the number of IDs in each category from our ID. This gives us
  //    the offset of the diagnostic in the category.
  // This is cheaper than a binary search on the table as it doesn't touch
  // memory at all.
  unsigned Offset = 0;
  unsigned ID = DiagID - DIAG_START_COMMON - 1;
#define CATEGORY(NAME, PREV) \
  if (DiagID > DIAG_START_##NAME) { \
    Offset += NUM_BUILTIN_##PREV##_DIAGNOSTICS - DIAG_START_##PREV - 1; \
    ID -= DIAG_START_##NAME - DIAG_START_##PREV; \
  }
CATEGORY(DRIVER, COMMON)
CATEGORY(FRONTEND, DRIVER)
CATEGORY(SERIALIZATION, FRONTEND)
CATEGORY(LEX, SERIALIZATION)
CATEGORY(PARSE, LEX)
CATEGORY(AST, PARSE)
CATEGORY(COMMENT, AST)
CATEGORY(CROSSTU, COMMENT)
CATEGORY(SEMA, CROSSTU)
CATEGORY(ANALYSIS, SEMA)
CATEGORY(REFACTORING, ANALYSIS)
CATEGORY(INSTALLAPI, REFACTORING)
#undef CATEGORY

  // Avoid out of bounds reads.
  if (ID + Offset >= StaticDiagInfoSize)
    return nullptr;

  assert(ID < StaticDiagInfoSize && Offset < StaticDiagInfoSize);

  const StaticDiagInfoRec *Found = &StaticDiagInfo[ID + Offset];
  // If the diag id doesn't match we found a different diag, abort. This can
  // happen when this function is called with an ID that points into a hole in
  // the diagID space.
  if (Found->DiagID != DiagID)
    return nullptr;
  return Found;
}

DiagnosticMapping DiagnosticIDs::getDefaultMapping(unsigned DiagID) {
  DiagnosticMapping Info = DiagnosticMapping::Make(
      diag::Severity::Fatal, /*IsUser=*/false, /*IsPragma=*/false);

  if (const StaticDiagInfoRec *StaticInfo = GetDiagInfo(DiagID)) {
    Info.setSeverity((diag::Severity)StaticInfo->DefaultSeverity);

    if (StaticInfo->WarnNoWerror) {
      assert(Info.getSeverity() == diag::Severity::Warning &&
             "Unexpected mapping with no-Werror bit!");
      Info.setNoWarningAsError(true);
    }
  }

  return Info;
}

/// getCategoryNumberForDiag - Return the category number that a specified
/// DiagID belongs to, or 0 if no category.
unsigned DiagnosticIDs::getCategoryNumberForDiag(unsigned DiagID) {
  if (const StaticDiagInfoRec *Info = GetDiagInfo(DiagID))
    return Info->Category;
  return 0;
}

namespace {
  // The diagnostic category names.
  struct StaticDiagCategoryRec {
    const char *NameStr;
    uint8_t NameLen;

    StringRef getName() const {
      return StringRef(NameStr, NameLen);
    }
  };
}

static const StaticDiagCategoryRec CategoryNameTable[] = {
#define GET_CATEGORY_TABLE
#define CATEGORY(X, ENUM) { X, STR_SIZE(X, uint8_t) },
#include "clang/Basic/DiagnosticGroups.inc"
#undef GET_CATEGORY_TABLE
  { nullptr, 0 }
};

/// getNumberOfCategories - Return the number of categories
unsigned DiagnosticIDs::getNumberOfCategories() {
  return std::size(CategoryNameTable) - 1;
}

/// getCategoryNameFromID - Given a category ID, return the name of the
/// category, an empty string if CategoryID is zero, or null if CategoryID is
/// invalid.
StringRef DiagnosticIDs::getCategoryNameFromID(unsigned CategoryID) {
  if (CategoryID >= getNumberOfCategories())
   return StringRef();
  return CategoryNameTable[CategoryID].getName();
}



DiagnosticIDs::SFINAEResponse
DiagnosticIDs::getDiagnosticSFINAEResponse(unsigned DiagID) {
  if (const StaticDiagInfoRec *Info = GetDiagInfo(DiagID))
    return static_cast<DiagnosticIDs::SFINAEResponse>(Info->SFINAE);
  return SFINAE_Report;
}

bool DiagnosticIDs::isDeferrable(unsigned DiagID) {
  if (const StaticDiagInfoRec *Info = GetDiagInfo(DiagID))
    return Info->Deferrable;
  return false;
}

/// getBuiltinDiagClass - Return the class field of the diagnostic.
///
static unsigned getBuiltinDiagClass(unsigned DiagID) {
  if (const StaticDiagInfoRec *Info = GetDiagInfo(DiagID))
    return Info->Class;
  return ~0U;
}

//===----------------------------------------------------------------------===//
// Custom Diagnostic information
//===----------------------------------------------------------------------===//

namespace clang {
  namespace diag {
    class CustomDiagInfo {
      typedef std::pair<DiagnosticIDs::Level, std::string> DiagDesc;
      std::vector<DiagDesc> DiagInfo;
      std::map<DiagDesc, unsigned> DiagIDs;
    public:

      /// getDescription - Return the description of the specified custom
      /// diagnostic.
      StringRef getDescription(unsigned DiagID) const {
        assert(DiagID - DIAG_UPPER_LIMIT < DiagInfo.size() &&
               "Invalid diagnostic ID");
        return DiagInfo[DiagID-DIAG_UPPER_LIMIT].second;
      }

      /// getLevel - Return the level of the specified custom diagnostic.
      DiagnosticIDs::Level getLevel(unsigned DiagID) const {
        assert(DiagID - DIAG_UPPER_LIMIT < DiagInfo.size() &&
               "Invalid diagnostic ID");
        return DiagInfo[DiagID-DIAG_UPPER_LIMIT].first;
      }

      unsigned getOrCreateDiagID(DiagnosticIDs::Level L, StringRef Message,
                                 DiagnosticIDs &Diags) {
        DiagDesc D(L, std::string(Message));
        // Check to see if it already exists.
        std::map<DiagDesc, unsigned>::iterator I = DiagIDs.lower_bound(D);
        if (I != DiagIDs.end() && I->first == D)
          return I->second;

        // If not, assign a new ID.
        unsigned ID = DiagInfo.size()+DIAG_UPPER_LIMIT;
        DiagIDs.insert(std::make_pair(D, ID));
        DiagInfo.push_back(D);
        return ID;
      }
    };

  } // end diag namespace
} // end clang namespace


//===----------------------------------------------------------------------===//
// Common Diagnostic implementation
//===----------------------------------------------------------------------===//

DiagnosticIDs::DiagnosticIDs() {}

DiagnosticIDs::~DiagnosticIDs() {}

/// getCustomDiagID - Return an ID for a diagnostic with the specified message
/// and level.  If this is the first request for this diagnostic, it is
/// registered and created, otherwise the existing ID is returned.
///
/// \param FormatString A fixed diagnostic format string that will be hashed and
/// mapped to a unique DiagID.
unsigned DiagnosticIDs::getCustomDiagID(Level L, StringRef FormatString) {
  if (!CustomDiagInfo)
    CustomDiagInfo.reset(new diag::CustomDiagInfo());
  return CustomDiagInfo->getOrCreateDiagID(L, FormatString, *this);
}


/// isBuiltinWarningOrExtension - Return true if the unmapped diagnostic
/// level of the specified diagnostic ID is a Warning or Extension.
/// This only works on builtin diagnostics, not custom ones, and is not legal to
/// call on NOTEs.
bool DiagnosticIDs::isBuiltinWarningOrExtension(unsigned DiagID) {
  return DiagID < diag::DIAG_UPPER_LIMIT &&
         getBuiltinDiagClass(DiagID) != CLASS_ERROR;
}

/// Determine whether the given built-in diagnostic ID is a
/// Note.
bool DiagnosticIDs::isBuiltinNote(unsigned DiagID) {
  return DiagID < diag::DIAG_UPPER_LIMIT &&
    getBuiltinDiagClass(DiagID) == CLASS_NOTE;
}

/// isBuiltinExtensionDiag - Determine whether the given built-in diagnostic
/// ID is for an extension of some sort.  This also returns EnabledByDefault,
/// which is set to indicate whether the diagnostic is ignored by default (in
/// which case -pedantic enables it) or treated as a warning/error by default.
///
bool DiagnosticIDs::isBuiltinExtensionDiag(unsigned DiagID,
                                        bool &EnabledByDefault) {
  if (DiagID >= diag::DIAG_UPPER_LIMIT ||
      getBuiltinDiagClass(DiagID) != CLASS_EXTENSION)
    return false;

  EnabledByDefault =
      getDefaultMapping(DiagID).getSeverity() != diag::Severity::Ignored;
  return true;
}

bool DiagnosticIDs::isDefaultMappingAsError(unsigned DiagID) {
  if (DiagID >= diag::DIAG_UPPER_LIMIT)
    return false;

  return getDefaultMapping(DiagID).getSeverity() >= diag::Severity::Error;
}

/// getDescription - Given a diagnostic ID, return a description of the
/// issue.
StringRef DiagnosticIDs::getDescription(unsigned DiagID) const {
  if (const StaticDiagInfoRec *Info = GetDiagInfo(DiagID))
    return Info->getDescription();
  assert(CustomDiagInfo && "Invalid CustomDiagInfo");
  return CustomDiagInfo->getDescription(DiagID);
}

static DiagnosticIDs::Level toLevel(diag::Severity SV) {
  switch (SV) {
  case diag::Severity::Ignored:
    return DiagnosticIDs::Ignored;
  case diag::Severity::Remark:
    return DiagnosticIDs::Remark;
  case diag::Severity::Warning:
    return DiagnosticIDs::Warning;
  case diag::Severity::Error:
    return DiagnosticIDs::Error;
  case diag::Severity::Fatal:
    return DiagnosticIDs::Fatal;
  }
  llvm_unreachable("unexpected severity");
}

/// getDiagnosticLevel - Based on the way the client configured the
/// DiagnosticsEngine object, classify the specified diagnostic ID into a Level,
/// by consumable the DiagnosticClient.
DiagnosticIDs::Level
DiagnosticIDs::getDiagnosticLevel(unsigned DiagID, SourceLocation Loc,
                                  const DiagnosticsEngine &Diag) const {
  // Handle custom diagnostics, which cannot be mapped.
  if (DiagID >= diag::DIAG_UPPER_LIMIT) {
    assert(CustomDiagInfo && "Invalid CustomDiagInfo");
    return CustomDiagInfo->getLevel(DiagID);
  }

  unsigned DiagClass = getBuiltinDiagClass(DiagID);
  if (DiagClass == CLASS_NOTE) return DiagnosticIDs::Note;
  return toLevel(getDiagnosticSeverity(DiagID, Loc, Diag));
}

/// Based on the way the client configured the Diagnostic
/// object, classify the specified diagnostic ID into a Level, consumable by
/// the DiagnosticClient.
///
/// \param Loc The source location we are interested in finding out the
/// diagnostic state. Can be null in order to query the latest state.
diag::Severity
DiagnosticIDs::getDiagnosticSeverity(unsigned DiagID, SourceLocation Loc,
                                     const DiagnosticsEngine &Diag) const {
  assert(getBuiltinDiagClass(DiagID) != CLASS_NOTE);

  // Specific non-error diagnostics may be mapped to various levels from ignored
  // to error.  Errors can only be mapped to fatal.
  diag::Severity Result = diag::Severity::Fatal;

  // Get the mapping information, or compute it lazily.
  DiagnosticsEngine::DiagState *State = Diag.GetDiagStateForLoc(Loc);
  DiagnosticMapping &Mapping = State->getOrAddMapping((diag::kind)DiagID);

  // TODO: Can a null severity really get here?
  if (Mapping.getSeverity() != diag::Severity())
    Result = Mapping.getSeverity();

  // Upgrade ignored diagnostics if -Weverything is enabled.
  if (State->EnableAllWarnings && Result == diag::Severity::Ignored &&
      !Mapping.isUser() && getBuiltinDiagClass(DiagID) != CLASS_REMARK)
    Result = diag::Severity::Warning;

  // Ignore -pedantic diagnostics inside __extension__ blocks.
  // (The diagnostics controlled by -pedantic are the extension diagnostics
  // that are not enabled by default.)
  bool EnabledByDefault = false;
  bool IsExtensionDiag = isBuiltinExtensionDiag(DiagID, EnabledByDefault);
  if (Diag.AllExtensionsSilenced && IsExtensionDiag && !EnabledByDefault)
    return diag::Severity::Ignored;

  // For extension diagnostics that haven't been explicitly mapped, check if we
  // should upgrade the diagnostic.
  if (IsExtensionDiag && !Mapping.isUser())
    Result = std::max(Result, State->ExtBehavior);

  // At this point, ignored errors can no longer be upgraded.
  if (Result == diag::Severity::Ignored)
    return Result;

  // Honor -w: this disables all messages which are not Error/Fatal by
  // default (disregarding attempts to upgrade severity from Warning to Error),
  // as well as disabling all messages which are currently mapped to Warning
  // (whether by default or downgraded from Error via e.g. -Wno-error or #pragma
  // diagnostic.)
  if (State->IgnoreAllWarnings) {
    if (Result == diag::Severity::Warning ||
        (Result >= diag::Severity::Error &&
         !isDefaultMappingAsError((diag::kind)DiagID)))
      return diag::Severity::Ignored;
  }

  // If -Werror is enabled, map warnings to errors unless explicitly disabled.
  if (Result == diag::Severity::Warning) {
    if (State->WarningsAsErrors && !Mapping.hasNoWarningAsError())
      Result = diag::Severity::Error;
  }

  // If -Wfatal-errors is enabled, map errors to fatal unless explicitly
  // disabled.
  if (Result == diag::Severity::Error) {
    if (State->ErrorsAsFatal && !Mapping.hasNoErrorAsFatal())
      Result = diag::Severity::Fatal;
  }

  // If explicitly requested, map fatal errors to errors.
  if (Result == diag::Severity::Fatal &&
      Diag.CurDiagID != diag::fatal_too_many_errors && Diag.FatalsAsError)
    Result = diag::Severity::Error;

  // Custom diagnostics always are emitted in system headers.
  bool ShowInSystemHeader =
      !GetDiagInfo(DiagID) || GetDiagInfo(DiagID)->WarnShowInSystemHeader;

  // If we are in a system header, we ignore it. We look at the diagnostic class
  // because we also want to ignore extensions and warnings in -Werror and
  // -pedantic-errors modes, which *map* warnings/extensions to errors.
  if (State->SuppressSystemWarnings && !ShowInSystemHeader && Loc.isValid() &&
      Diag.getSourceManager().isInSystemHeader(
          Diag.getSourceManager().getExpansionLoc(Loc)))
    return diag::Severity::Ignored;

  // We also ignore warnings due to system macros
  bool ShowInSystemMacro =
      !GetDiagInfo(DiagID) || GetDiagInfo(DiagID)->WarnShowInSystemMacro;
  if (State->SuppressSystemWarnings && !ShowInSystemMacro && Loc.isValid() &&
      Diag.getSourceManager().isInSystemMacro(Loc))
    return diag::Severity::Ignored;

  return Result;
}

#define GET_DIAG_ARRAYS
#include "clang/Basic/DiagnosticGroups.inc"
#undef GET_DIAG_ARRAYS

namespace {
  struct WarningOption {
    uint16_t NameOffset;
    uint16_t Members;
    uint16_t SubGroups;
    StringRef Documentation;

    // String is stored with a pascal-style length byte.
    StringRef getName() const {
      return StringRef(DiagGroupNames + NameOffset + 1,
                       DiagGroupNames[NameOffset]);
    }
  };
}

// Second the table of options, sorted by name for fast binary lookup.
static const WarningOption OptionTable[] = {
#define DIAG_ENTRY(GroupName, FlagNameOffset, Members, SubGroups, Docs)        \
  {FlagNameOffset, Members, SubGroups, Docs},
#include "clang/Basic/DiagnosticGroups.inc"
#undef DIAG_ENTRY
};

/// Given a diagnostic group ID, return its documentation.
StringRef DiagnosticIDs::getWarningOptionDocumentation(diag::Group Group) {
  return OptionTable[static_cast<int>(Group)].Documentation;
}

StringRef DiagnosticIDs::getWarningOptionForGroup(diag::Group Group) {
  return OptionTable[static_cast<int>(Group)].getName();
}

std::optional<diag::Group>
DiagnosticIDs::getGroupForWarningOption(StringRef Name) {
  const auto *Found = llvm::partition_point(
      OptionTable, [=](const WarningOption &O) { return O.getName() < Name; });
  if (Found == std::end(OptionTable) || Found->getName() != Name)
    return std::nullopt;
  return static_cast<diag::Group>(Found - OptionTable);
}

std::optional<diag::Group> DiagnosticIDs::getGroupForDiag(unsigned DiagID) {
  if (const StaticDiagInfoRec *Info = GetDiagInfo(DiagID))
    return static_cast<diag::Group>(Info->getOptionGroupIndex());
  return std::nullopt;
}

/// getWarningOptionForDiag - Return the lowest-level warning option that
/// enables the specified diagnostic.  If there is no -Wfoo flag that controls
/// the diagnostic, this returns null.
StringRef DiagnosticIDs::getWarningOptionForDiag(unsigned DiagID) {
  if (auto G = getGroupForDiag(DiagID))
    return getWarningOptionForGroup(*G);
  return StringRef();
}

std::vector<std::string> DiagnosticIDs::getDiagnosticFlags() {
  std::vector<std::string> Res{"-W", "-Wno-"};
  for (size_t I = 1; DiagGroupNames[I] != '\0';) {
    std::string Diag(DiagGroupNames + I + 1, DiagGroupNames[I]);
    I += DiagGroupNames[I] + 1;
    Res.push_back("-W" + Diag);
    Res.push_back("-Wno-" + Diag);
  }

  return Res;
}

/// Return \c true if any diagnostics were found in this group, even if they
/// were filtered out due to having the wrong flavor.
static bool getDiagnosticsInGroup(diag::Flavor Flavor,
                                  const WarningOption *Group,
                                  SmallVectorImpl<diag::kind> &Diags) {
  // An empty group is considered to be a warning group: we have empty groups
  // for GCC compatibility, and GCC does not have remarks.
  if (!Group->Members && !Group->SubGroups)
    return Flavor == diag::Flavor::Remark;

  bool NotFound = true;

  // Add the members of the option diagnostic set.
  const int16_t *Member = DiagArrays + Group->Members;
  for (; *Member != -1; ++Member) {
    if (GetDiagInfo(*Member)->getFlavor() == Flavor) {
      NotFound = false;
      Diags.push_back(*Member);
    }
  }

  // Add the members of the subgroups.
  const int16_t *SubGroups = DiagSubGroups + Group->SubGroups;
  for (; *SubGroups != (int16_t)-1; ++SubGroups)
    NotFound &= getDiagnosticsInGroup(Flavor, &OptionTable[(short)*SubGroups],
                                      Diags);

  return NotFound;
}

bool
DiagnosticIDs::getDiagnosticsInGroup(diag::Flavor Flavor, StringRef Group,
                                     SmallVectorImpl<diag::kind> &Diags) const {
  if (std::optional<diag::Group> G = getGroupForWarningOption(Group))
    return ::getDiagnosticsInGroup(
        Flavor, &OptionTable[static_cast<unsigned>(*G)], Diags);
  return true;
}

void DiagnosticIDs::getAllDiagnostics(diag::Flavor Flavor,
                                      std::vector<diag::kind> &Diags) {
  for (unsigned i = 0; i != StaticDiagInfoSize; ++i)
    if (StaticDiagInfo[i].getFlavor() == Flavor)
      Diags.push_back(StaticDiagInfo[i].DiagID);
}

StringRef DiagnosticIDs::getNearestOption(diag::Flavor Flavor,
                                          StringRef Group) {
  StringRef Best;
  unsigned BestDistance = Group.size() + 1; // Maximum threshold.
  for (const WarningOption &O : OptionTable) {
    // Don't suggest ignored warning flags.
    if (!O.Members && !O.SubGroups)
      continue;

    unsigned Distance = O.getName().edit_distance(Group, true, BestDistance);
    if (Distance > BestDistance)
      continue;

    // Don't suggest groups that are not of this kind.
    llvm::SmallVector<diag::kind, 8> Diags;
    if (::getDiagnosticsInGroup(Flavor, &O, Diags) || Diags.empty())
      continue;

    if (Distance == BestDistance) {
      // Two matches with the same distance, don't prefer one over the other.
      Best = "";
    } else if (Distance < BestDistance) {
      // This is a better match.
      Best = O.getName();
      BestDistance = Distance;
    }
  }

  return Best;
}

/// ProcessDiag - This is the method used to report a diagnostic that is
/// finally fully formed.
bool DiagnosticIDs::ProcessDiag(DiagnosticsEngine &Diag) const {
  Diagnostic Info(&Diag);

  assert(Diag.getClient() && "DiagnosticClient not set!");

  // Figure out the diagnostic level of this message.
  unsigned DiagID = Info.getID();
  DiagnosticIDs::Level DiagLevel
    = getDiagnosticLevel(DiagID, Info.getLocation(), Diag);

  // Update counts for DiagnosticErrorTrap even if a fatal error occurred
  // or diagnostics are suppressed.
  if (DiagLevel >= DiagnosticIDs::Error) {
    ++Diag.TrapNumErrorsOccurred;
    if (isUnrecoverable(DiagID))
      ++Diag.TrapNumUnrecoverableErrorsOccurred;
  }

  if (Diag.SuppressAllDiagnostics)
    return false;

  if (DiagLevel != DiagnosticIDs::Note) {
    // Record that a fatal error occurred only when we see a second
    // non-note diagnostic. This allows notes to be attached to the
    // fatal error, but suppresses any diagnostics that follow those
    // notes.
    if (Diag.LastDiagLevel == DiagnosticIDs::Fatal)
      Diag.FatalErrorOccurred = true;

    Diag.LastDiagLevel = DiagLevel;
  }

  // If a fatal error has already been emitted, silence all subsequent
  // diagnostics.
  if (Diag.FatalErrorOccurred) {
    if (DiagLevel >= DiagnosticIDs::Error &&
        Diag.Client->IncludeInDiagnosticCounts()) {
      ++Diag.NumErrors;
    }

    return false;
  }

  // If the client doesn't care about this message, don't issue it.  If this is
  // a note and the last real diagnostic was ignored, ignore it too.
  if (DiagLevel == DiagnosticIDs::Ignored ||
      (DiagLevel == DiagnosticIDs::Note &&
       Diag.LastDiagLevel == DiagnosticIDs::Ignored))
    return false;

  if (DiagLevel >= DiagnosticIDs::Error) {
    if (isUnrecoverable(DiagID))
      Diag.UnrecoverableErrorOccurred = true;

    // Warnings which have been upgraded to errors do not prevent compilation.
    if (isDefaultMappingAsError(DiagID))
      Diag.UncompilableErrorOccurred = true;

    Diag.ErrorOccurred = true;
    if (Diag.Client->IncludeInDiagnosticCounts()) {
      ++Diag.NumErrors;
    }

    // If we've emitted a lot of errors, emit a fatal error instead of it to
    // stop a flood of bogus errors.
    if (Diag.ErrorLimit && Diag.NumErrors > Diag.ErrorLimit &&
        DiagLevel == DiagnosticIDs::Error) {
      Diag.SetDelayedDiagnostic(diag::fatal_too_many_errors);
      return false;
    }
  }

  // Make sure we set FatalErrorOccurred to ensure that the notes from the
  // diagnostic that caused `fatal_too_many_errors` won't be emitted.
  if (Diag.CurDiagID == diag::fatal_too_many_errors)
    Diag.FatalErrorOccurred = true;
  // Finally, report it.
  EmitDiag(Diag, DiagLevel);
  return true;
}

void DiagnosticIDs::EmitDiag(DiagnosticsEngine &Diag, Level DiagLevel) const {
  Diagnostic Info(&Diag);
  assert(DiagLevel != DiagnosticIDs::Ignored && "Cannot emit ignored diagnostics!");

  Diag.Client->HandleDiagnostic((DiagnosticsEngine::Level)DiagLevel, Info);
  if (Diag.Client->IncludeInDiagnosticCounts()) {
    if (DiagLevel == DiagnosticIDs::Warning)
      ++Diag.NumWarnings;
  }

  Diag.CurDiagID = ~0U;
}

bool DiagnosticIDs::isUnrecoverable(unsigned DiagID) const {
  if (DiagID >= diag::DIAG_UPPER_LIMIT) {
    assert(CustomDiagInfo && "Invalid CustomDiagInfo");
    // Custom diagnostics.
    return CustomDiagInfo->getLevel(DiagID) >= DiagnosticIDs::Error;
  }

  // Only errors may be unrecoverable.
  if (getBuiltinDiagClass(DiagID) < CLASS_ERROR)
    return false;

  if (DiagID == diag::err_unavailable ||
      DiagID == diag::err_unavailable_message)
    return false;

  // Currently we consider all ARC errors as recoverable.
  if (isARCDiagnostic(DiagID))
    return false;

  if (isCodegenABICheckDiagnostic(DiagID))
    return false;

  return true;
}

bool DiagnosticIDs::isARCDiagnostic(unsigned DiagID) {
  unsigned cat = getCategoryNumberForDiag(DiagID);
  return DiagnosticIDs::getCategoryNameFromID(cat).starts_with("ARC ");
}

bool DiagnosticIDs::isCodegenABICheckDiagnostic(unsigned DiagID) {
  unsigned cat = getCategoryNumberForDiag(DiagID);
  return DiagnosticIDs::getCategoryNameFromID(cat) == "Codegen ABI Check";
}

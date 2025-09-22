//===- DiagnosticNames.cpp - Defines a table of all builtin diagnostics ----==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DiagnosticNames.h"
#include "clang/Basic/AllDiagnostics.h"
#include "llvm/ADT/STLExtras.h"

using namespace clang;
using namespace diagtool;

static const DiagnosticRecord BuiltinDiagnosticsByName[] = {
#define DIAG_NAME_INDEX(ENUM) { #ENUM, diag::ENUM, STR_SIZE(#ENUM, uint8_t) },
#include "clang/Basic/DiagnosticIndexName.inc"
#undef DIAG_NAME_INDEX
};

llvm::ArrayRef<DiagnosticRecord> diagtool::getBuiltinDiagnosticsByName() {
  return llvm::ArrayRef(BuiltinDiagnosticsByName);
}


// FIXME: Is it worth having two tables, especially when this one can get
// out of sync easily?
static const DiagnosticRecord BuiltinDiagnosticsByID[] = {
#define DIAG(ENUM, CLASS, DEFAULT_MAPPING, DESC, GROUP, SFINAE, NOWERROR,      \
             SHOWINSYSHEADER, SHOWINSYSMACRO, DEFER, CATEGORY)                 \
  {#ENUM, diag::ENUM, STR_SIZE(#ENUM, uint8_t)},
#include "clang/Basic/DiagnosticCommonKinds.inc"
#include "clang/Basic/DiagnosticCrossTUKinds.inc"
#include "clang/Basic/DiagnosticDriverKinds.inc"
#include "clang/Basic/DiagnosticFrontendKinds.inc"
#include "clang/Basic/DiagnosticSerializationKinds.inc"
#include "clang/Basic/DiagnosticLexKinds.inc"
#include "clang/Basic/DiagnosticParseKinds.inc"
#include "clang/Basic/DiagnosticASTKinds.inc"
#include "clang/Basic/DiagnosticCommentKinds.inc"
#include "clang/Basic/DiagnosticSemaKinds.inc"
#include "clang/Basic/DiagnosticAnalysisKinds.inc"
#include "clang/Basic/DiagnosticRefactoringKinds.inc"
#include "clang/Basic/DiagnosticInstallAPIKinds.inc"
#undef DIAG
};

static bool orderByID(const DiagnosticRecord &Left,
                      const DiagnosticRecord &Right) {
  return Left.DiagID < Right.DiagID;
}

const DiagnosticRecord &diagtool::getDiagnosticForID(short DiagID) {
  DiagnosticRecord Key = {nullptr, DiagID, 0};

  const DiagnosticRecord *Result =
      llvm::lower_bound(BuiltinDiagnosticsByID, Key, orderByID);
  assert(Result && "diagnostic not found; table may be out of date");
  return *Result;
}


#define GET_DIAG_ARRAYS
#include "clang/Basic/DiagnosticGroups.inc"
#undef GET_DIAG_ARRAYS

// Second the table of options, sorted by name for fast binary lookup.
static const GroupRecord OptionTable[] = {
#define DIAG_ENTRY(GroupName, FlagNameOffset, Members, SubGroups, Docs)        \
  {FlagNameOffset, Members, SubGroups},
#include "clang/Basic/DiagnosticGroups.inc"
#undef DIAG_ENTRY
};

llvm::StringRef GroupRecord::getName() const {
  return StringRef(DiagGroupNames + NameOffset + 1, DiagGroupNames[NameOffset]);
}

GroupRecord::subgroup_iterator GroupRecord::subgroup_begin() const {
  return DiagSubGroups + SubGroups;
}

GroupRecord::subgroup_iterator GroupRecord::subgroup_end() const {
  return nullptr;
}

llvm::iterator_range<diagtool::GroupRecord::subgroup_iterator>
GroupRecord::subgroups() const {
  return llvm::make_range(subgroup_begin(), subgroup_end());
}

GroupRecord::diagnostics_iterator GroupRecord::diagnostics_begin() const {
  return DiagArrays + Members;
}

GroupRecord::diagnostics_iterator GroupRecord::diagnostics_end() const {
  return nullptr;
}

llvm::iterator_range<diagtool::GroupRecord::diagnostics_iterator>
GroupRecord::diagnostics() const {
  return llvm::make_range(diagnostics_begin(), diagnostics_end());
}

llvm::ArrayRef<GroupRecord> diagtool::getDiagnosticGroups() {
  return llvm::ArrayRef(OptionTable);
}

//===- DirectiveEmitter.cpp - Directive Language Emitter ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// DirectiveEmitter uses the descriptions of directives and clauses to construct
// common code declarations to be used in Frontends.
//
//===----------------------------------------------------------------------===//

#include "llvm/TableGen/DirectiveEmitter.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"

#include <numeric>
#include <vector>

using namespace llvm;

namespace {
// Simple RAII helper for defining ifdef-undef-endif scopes.
class IfDefScope {
public:
  IfDefScope(StringRef Name, raw_ostream &OS) : Name(Name), OS(OS) {
    OS << "#ifdef " << Name << "\n"
       << "#undef " << Name << "\n";
  }

  ~IfDefScope() { OS << "\n#endif // " << Name << "\n\n"; }

private:
  StringRef Name;
  raw_ostream &OS;
};
} // namespace

// Generate enum class. Entries are emitted in the order in which they appear
// in the `Records` vector.
static void GenerateEnumClass(const std::vector<Record *> &Records,
                              raw_ostream &OS, StringRef Enum, StringRef Prefix,
                              const DirectiveLanguage &DirLang,
                              bool ExportEnums) {
  OS << "\n";
  OS << "enum class " << Enum << " {\n";
  for (const auto &R : Records) {
    BaseRecord Rec{R};
    OS << "  " << Prefix << Rec.getFormattedName() << ",\n";
  }
  OS << "};\n";
  OS << "\n";
  OS << "static constexpr std::size_t " << Enum
     << "_enumSize = " << Records.size() << ";\n";

  // Make the enum values available in the defined namespace. This allows us to
  // write something like Enum_X if we have a `using namespace <CppNamespace>`.
  // At the same time we do not loose the strong type guarantees of the enum
  // class, that is we cannot pass an unsigned as Directive without an explicit
  // cast.
  if (ExportEnums) {
    OS << "\n";
    for (const auto &R : Records) {
      BaseRecord Rec{R};
      OS << "constexpr auto " << Prefix << Rec.getFormattedName() << " = "
         << "llvm::" << DirLang.getCppNamespace() << "::" << Enum
         << "::" << Prefix << Rec.getFormattedName() << ";\n";
    }
  }
}

// Generate enums for values that clauses can take.
// Also generate function declarations for get<Enum>Name(StringRef Str).
static void GenerateEnumClauseVal(const std::vector<Record *> &Records,
                                  raw_ostream &OS,
                                  const DirectiveLanguage &DirLang,
                                  std::string &EnumHelperFuncs) {
  for (const auto &R : Records) {
    Clause C{R};
    const auto &ClauseVals = C.getClauseVals();
    if (ClauseVals.size() <= 0)
      continue;

    const auto &EnumName = C.getEnumName();
    if (EnumName.size() == 0) {
      PrintError("enumClauseValue field not set in Clause" +
                 C.getFormattedName() + ".");
      return;
    }

    OS << "\n";
    OS << "enum class " << EnumName << " {\n";
    for (const auto &CV : ClauseVals) {
      ClauseVal CVal{CV};
      OS << "  " << CV->getName() << "=" << CVal.getValue() << ",\n";
    }
    OS << "};\n";

    if (DirLang.hasMakeEnumAvailableInNamespace()) {
      OS << "\n";
      for (const auto &CV : ClauseVals) {
        OS << "constexpr auto " << CV->getName() << " = "
           << "llvm::" << DirLang.getCppNamespace() << "::" << EnumName
           << "::" << CV->getName() << ";\n";
      }
      EnumHelperFuncs += (llvm::Twine(EnumName) + llvm::Twine(" get") +
                          llvm::Twine(EnumName) + llvm::Twine("(StringRef);\n"))
                             .str();

      EnumHelperFuncs +=
          (llvm::Twine("llvm::StringRef get") + llvm::Twine(DirLang.getName()) +
           llvm::Twine(EnumName) + llvm::Twine("Name(") +
           llvm::Twine(EnumName) + llvm::Twine(");\n"))
              .str();
    }
  }
}

static bool HasDuplicateClauses(const std::vector<Record *> &Clauses,
                                const Directive &Directive,
                                llvm::StringSet<> &CrtClauses) {
  bool HasError = false;
  for (const auto &C : Clauses) {
    VersionedClause VerClause{C};
    const auto insRes = CrtClauses.insert(VerClause.getClause().getName());
    if (!insRes.second) {
      PrintError("Clause " + VerClause.getClause().getRecordName() +
                 " already defined on directive " + Directive.getRecordName());
      HasError = true;
    }
  }
  return HasError;
}

// Check for duplicate clauses in lists. Clauses cannot appear twice in the
// three allowed list. Also, since required implies allowed, clauses cannot
// appear in both the allowedClauses and requiredClauses lists.
static bool
HasDuplicateClausesInDirectives(const std::vector<Record *> &Directives) {
  bool HasDuplicate = false;
  for (const auto &D : Directives) {
    Directive Dir{D};
    llvm::StringSet<> Clauses;
    // Check for duplicates in the three allowed lists.
    if (HasDuplicateClauses(Dir.getAllowedClauses(), Dir, Clauses) ||
        HasDuplicateClauses(Dir.getAllowedOnceClauses(), Dir, Clauses) ||
        HasDuplicateClauses(Dir.getAllowedExclusiveClauses(), Dir, Clauses)) {
      HasDuplicate = true;
    }
    // Check for duplicate between allowedClauses and required
    Clauses.clear();
    if (HasDuplicateClauses(Dir.getAllowedClauses(), Dir, Clauses) ||
        HasDuplicateClauses(Dir.getRequiredClauses(), Dir, Clauses)) {
      HasDuplicate = true;
    }
    if (HasDuplicate)
      PrintFatalError("One or more clauses are defined multiple times on"
                      " directive " +
                      Dir.getRecordName());
  }

  return HasDuplicate;
}

// Check consitency of records. Return true if an error has been detected.
// Return false if the records are valid.
bool DirectiveLanguage::HasValidityErrors() const {
  if (getDirectiveLanguages().size() != 1) {
    PrintFatalError("A single definition of DirectiveLanguage is needed.");
    return true;
  }

  return HasDuplicateClausesInDirectives(getDirectives());
}

// Count the maximum number of leaf constituents per construct.
static size_t GetMaxLeafCount(const DirectiveLanguage &DirLang) {
  size_t MaxCount = 0;
  for (Record *R : DirLang.getDirectives()) {
    size_t Count = Directive{R}.getLeafConstructs().size();
    MaxCount = std::max(MaxCount, Count);
  }
  return MaxCount;
}

// Generate the declaration section for the enumeration in the directive
// language
static void EmitDirectivesDecl(RecordKeeper &Records, raw_ostream &OS) {
  const auto DirLang = DirectiveLanguage{Records};
  if (DirLang.HasValidityErrors())
    return;

  OS << "#ifndef LLVM_" << DirLang.getName() << "_INC\n";
  OS << "#define LLVM_" << DirLang.getName() << "_INC\n";
  OS << "\n#include \"llvm/ADT/ArrayRef.h\"\n";

  if (DirLang.hasEnableBitmaskEnumInNamespace())
    OS << "#include \"llvm/ADT/BitmaskEnum.h\"\n";

  OS << "#include <cstddef>\n"; // for size_t
  OS << "\n";
  OS << "namespace llvm {\n";
  OS << "class StringRef;\n";

  // Open namespaces defined in the directive language
  llvm::SmallVector<StringRef, 2> Namespaces;
  llvm::SplitString(DirLang.getCppNamespace(), Namespaces, "::");
  for (auto Ns : Namespaces)
    OS << "namespace " << Ns << " {\n";

  if (DirLang.hasEnableBitmaskEnumInNamespace())
    OS << "\nLLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();\n";

  // Emit Directive associations
  std::vector<Record *> associations;
  llvm::copy_if(
      DirLang.getAssociations(), std::back_inserter(associations),
      // Skip the "special" value
      [](const Record *Def) { return Def->getName() != "AS_FromLeaves"; });
  GenerateEnumClass(associations, OS, "Association",
                    /*Prefix=*/"", DirLang, /*ExportEnums=*/false);

  GenerateEnumClass(DirLang.getCategories(), OS, "Category", /*Prefix=*/"",
                    DirLang, /*ExportEnums=*/false);

  // Emit Directive enumeration
  GenerateEnumClass(DirLang.getDirectives(), OS, "Directive",
                    DirLang.getDirectivePrefix(), DirLang,
                    DirLang.hasMakeEnumAvailableInNamespace());

  // Emit Clause enumeration
  GenerateEnumClass(DirLang.getClauses(), OS, "Clause",
                    DirLang.getClausePrefix(), DirLang,
                    DirLang.hasMakeEnumAvailableInNamespace());

  // Emit ClauseVal enumeration
  std::string EnumHelperFuncs;
  GenerateEnumClauseVal(DirLang.getClauses(), OS, DirLang, EnumHelperFuncs);

  // Generic function signatures
  OS << "\n";
  OS << "// Enumeration helper functions\n";
  OS << "Directive get" << DirLang.getName()
     << "DirectiveKind(llvm::StringRef Str);\n";
  OS << "\n";
  OS << "llvm::StringRef get" << DirLang.getName()
     << "DirectiveName(Directive D);\n";
  OS << "\n";
  OS << "Clause get" << DirLang.getName()
     << "ClauseKind(llvm::StringRef Str);\n";
  OS << "\n";
  OS << "llvm::StringRef get" << DirLang.getName() << "ClauseName(Clause C);\n";
  OS << "\n";
  OS << "/// Return true if \\p C is a valid clause for \\p D in version \\p "
     << "Version.\n";
  OS << "bool isAllowedClauseForDirective(Directive D, "
     << "Clause C, unsigned Version);\n";
  OS << "\n";
  OS << "constexpr std::size_t getMaxLeafCount() { return "
     << GetMaxLeafCount(DirLang) << "; }\n";
  OS << "Association getDirectiveAssociation(Directive D);\n";
  OS << "Category getDirectiveCategory(Directive D);\n";
  if (EnumHelperFuncs.length() > 0) {
    OS << EnumHelperFuncs;
    OS << "\n";
  }

  // Closing namespaces
  for (auto Ns : llvm::reverse(Namespaces))
    OS << "} // namespace " << Ns << "\n";

  OS << "} // namespace llvm\n";

  OS << "#endif // LLVM_" << DirLang.getName() << "_INC\n";
}

// Generate function implementation for get<Enum>Name(StringRef Str)
static void GenerateGetName(const std::vector<Record *> &Records,
                            raw_ostream &OS, StringRef Enum,
                            const DirectiveLanguage &DirLang,
                            StringRef Prefix) {
  OS << "\n";
  OS << "llvm::StringRef llvm::" << DirLang.getCppNamespace() << "::get"
     << DirLang.getName() << Enum << "Name(" << Enum << " Kind) {\n";
  OS << "  switch (Kind) {\n";
  for (const auto &R : Records) {
    BaseRecord Rec{R};
    OS << "    case " << Prefix << Rec.getFormattedName() << ":\n";
    OS << "      return \"";
    if (Rec.getAlternativeName().empty())
      OS << Rec.getName();
    else
      OS << Rec.getAlternativeName();
    OS << "\";\n";
  }
  OS << "  }\n"; // switch
  OS << "  llvm_unreachable(\"Invalid " << DirLang.getName() << " " << Enum
     << " kind\");\n";
  OS << "}\n";
}

// Generate function implementation for get<Enum>Kind(StringRef Str)
static void GenerateGetKind(const std::vector<Record *> &Records,
                            raw_ostream &OS, StringRef Enum,
                            const DirectiveLanguage &DirLang, StringRef Prefix,
                            bool ImplicitAsUnknown) {

  auto DefaultIt = llvm::find_if(
      Records, [](Record *R) { return R->getValueAsBit("isDefault") == true; });

  if (DefaultIt == Records.end()) {
    PrintError("At least one " + Enum + " must be defined as default.");
    return;
  }

  BaseRecord DefaultRec{(*DefaultIt)};

  OS << "\n";
  OS << Enum << " llvm::" << DirLang.getCppNamespace() << "::get"
     << DirLang.getName() << Enum << "Kind(llvm::StringRef Str) {\n";
  OS << "  return llvm::StringSwitch<" << Enum << ">(Str)\n";

  for (const auto &R : Records) {
    BaseRecord Rec{R};
    if (ImplicitAsUnknown && R->getValueAsBit("isImplicit")) {
      OS << "    .Case(\"" << Rec.getName() << "\"," << Prefix
         << DefaultRec.getFormattedName() << ")\n";
    } else {
      OS << "    .Case(\"" << Rec.getName() << "\"," << Prefix
         << Rec.getFormattedName() << ")\n";
    }
  }
  OS << "    .Default(" << Prefix << DefaultRec.getFormattedName() << ");\n";
  OS << "}\n";
}

// Generate function implementation for get<ClauseVal>Kind(StringRef Str)
static void GenerateGetKindClauseVal(const DirectiveLanguage &DirLang,
                                     raw_ostream &OS) {
  for (const auto &R : DirLang.getClauses()) {
    Clause C{R};
    const auto &ClauseVals = C.getClauseVals();
    if (ClauseVals.size() <= 0)
      continue;

    auto DefaultIt = llvm::find_if(ClauseVals, [](Record *CV) {
      return CV->getValueAsBit("isDefault") == true;
    });

    if (DefaultIt == ClauseVals.end()) {
      PrintError("At least one val in Clause " + C.getFormattedName() +
                 " must be defined as default.");
      return;
    }
    const auto DefaultName = (*DefaultIt)->getName();

    const auto &EnumName = C.getEnumName();
    if (EnumName.size() == 0) {
      PrintError("enumClauseValue field not set in Clause" +
                 C.getFormattedName() + ".");
      return;
    }

    OS << "\n";
    OS << EnumName << " llvm::" << DirLang.getCppNamespace() << "::get"
       << EnumName << "(llvm::StringRef Str) {\n";
    OS << "  return llvm::StringSwitch<" << EnumName << ">(Str)\n";
    for (const auto &CV : ClauseVals) {
      ClauseVal CVal{CV};
      OS << "    .Case(\"" << CVal.getFormattedName() << "\"," << CV->getName()
         << ")\n";
    }
    OS << "    .Default(" << DefaultName << ");\n";
    OS << "}\n";

    OS << "\n";
    OS << "llvm::StringRef llvm::" << DirLang.getCppNamespace() << "::get"
       << DirLang.getName() << EnumName
       << "Name(llvm::" << DirLang.getCppNamespace() << "::" << EnumName
       << " x) {\n";
    OS << "  switch (x) {\n";
    for (const auto &CV : ClauseVals) {
      ClauseVal CVal{CV};
      OS << "    case " << CV->getName() << ":\n";
      OS << "      return \"" << CVal.getFormattedName() << "\";\n";
    }
    OS << "  }\n"; // switch
    OS << "  llvm_unreachable(\"Invalid " << DirLang.getName() << " "
       << EnumName << " kind\");\n";
    OS << "}\n";
  }
}

static void
GenerateCaseForVersionedClauses(const std::vector<Record *> &Clauses,
                                raw_ostream &OS, StringRef DirectiveName,
                                const DirectiveLanguage &DirLang,
                                llvm::StringSet<> &Cases) {
  for (const auto &C : Clauses) {
    VersionedClause VerClause{C};

    const auto ClauseFormattedName = VerClause.getClause().getFormattedName();

    if (Cases.insert(ClauseFormattedName).second) {
      OS << "        case " << DirLang.getClausePrefix() << ClauseFormattedName
         << ":\n";
      OS << "          return " << VerClause.getMinVersion()
         << " <= Version && " << VerClause.getMaxVersion() << " >= Version;\n";
    }
  }
}

static std::string GetDirectiveName(const DirectiveLanguage &DirLang,
                                    const Record *Rec) {
  Directive Dir{Rec};
  return (llvm::Twine("llvm::") + DirLang.getCppNamespace() +
          "::" + DirLang.getDirectivePrefix() + Dir.getFormattedName())
      .str();
}

static std::string GetDirectiveType(const DirectiveLanguage &DirLang) {
  return (llvm::Twine("llvm::") + DirLang.getCppNamespace() + "::Directive")
      .str();
}

// Generate the isAllowedClauseForDirective function implementation.
static void GenerateIsAllowedClause(const DirectiveLanguage &DirLang,
                                    raw_ostream &OS) {
  OS << "\n";
  OS << "bool llvm::" << DirLang.getCppNamespace()
     << "::isAllowedClauseForDirective("
     << "Directive D, Clause C, unsigned Version) {\n";
  OS << "  assert(unsigned(D) <= llvm::" << DirLang.getCppNamespace()
     << "::Directive_enumSize);\n";
  OS << "  assert(unsigned(C) <= llvm::" << DirLang.getCppNamespace()
     << "::Clause_enumSize);\n";

  OS << "  switch (D) {\n";

  for (const auto &D : DirLang.getDirectives()) {
    Directive Dir{D};

    OS << "    case " << DirLang.getDirectivePrefix() << Dir.getFormattedName()
       << ":\n";
    if (Dir.getAllowedClauses().size() == 0 &&
        Dir.getAllowedOnceClauses().size() == 0 &&
        Dir.getAllowedExclusiveClauses().size() == 0 &&
        Dir.getRequiredClauses().size() == 0) {
      OS << "      return false;\n";
    } else {
      OS << "      switch (C) {\n";

      llvm::StringSet<> Cases;

      GenerateCaseForVersionedClauses(Dir.getAllowedClauses(), OS,
                                      Dir.getName(), DirLang, Cases);

      GenerateCaseForVersionedClauses(Dir.getAllowedOnceClauses(), OS,
                                      Dir.getName(), DirLang, Cases);

      GenerateCaseForVersionedClauses(Dir.getAllowedExclusiveClauses(), OS,
                                      Dir.getName(), DirLang, Cases);

      GenerateCaseForVersionedClauses(Dir.getRequiredClauses(), OS,
                                      Dir.getName(), DirLang, Cases);

      OS << "        default:\n";
      OS << "          return false;\n";
      OS << "      }\n"; // End of clauses switch
    }
    OS << "      break;\n";
  }

  OS << "  }\n"; // End of directives switch
  OS << "  llvm_unreachable(\"Invalid " << DirLang.getName()
     << " Directive kind\");\n";
  OS << "}\n"; // End of function isAllowedClauseForDirective
}

static void EmitLeafTable(const DirectiveLanguage &DirLang, raw_ostream &OS,
                          StringRef TableName) {
  // The leaf constructs are emitted in a form of a 2D table, where each
  // row corresponds to a directive (and there is a row for each directive).
  //
  // Each row consists of
  // - the id of the directive itself,
  // - number of leaf constructs that will follow (0 for leafs),
  // - ids of the leaf constructs (none if the directive is itself a leaf).
  // The total number of these entries is at most MaxLeafCount+2. If this
  // number is less than that, it is padded to occupy exactly MaxLeafCount+2
  // entries in memory.
  //
  // The rows are stored in the table in the lexicographical order. This
  // is intended to enable binary search when mapping a sequence of leafs
  // back to the compound directive.
  // The consequence of that is that in order to find a row corresponding
  // to the given directive, we'd need to scan the first element of each
  // row. To avoid this, an auxiliary ordering table is created, such that
  //   row for Dir_A = table[auxiliary[Dir_A]].

  std::vector<Record *> Directives = DirLang.getDirectives();
  DenseMap<Record *, int> DirId; // Record * -> llvm::omp::Directive

  for (auto [Idx, Rec] : llvm::enumerate(Directives))
    DirId.insert(std::make_pair(Rec, Idx));

  using LeafList = std::vector<int>;
  int MaxLeafCount = GetMaxLeafCount(DirLang);

  // The initial leaf table, rows order is same as directive order.
  std::vector<LeafList> LeafTable(Directives.size());
  for (auto [Idx, Rec] : llvm::enumerate(Directives)) {
    Directive Dir{Rec};
    std::vector<Record *> Leaves = Dir.getLeafConstructs();

    auto &List = LeafTable[Idx];
    List.resize(MaxLeafCount + 2);
    List[0] = Idx;           // The id of the directive itself.
    List[1] = Leaves.size(); // The number of leaves to follow.

    for (int I = 0; I != MaxLeafCount; ++I)
      List[I + 2] =
          static_cast<size_t>(I) < Leaves.size() ? DirId.at(Leaves[I]) : -1;
  }

  // Some Fortran directives are delimited, i.e. they have the form of
  // "directive"---"end directive". If "directive" is a compound construct,
  // then the set of leaf constituents will be nonempty and the same for
  // both directives. Given this set of leafs, looking up the corresponding
  // compound directive should return "directive", and not "end directive".
  // To avoid this problem, gather all "end directives" at the end of the
  // leaf table, and only do the search on the initial segment of the table
  // that excludes the "end directives".
  // It's safe to find all directives whose names begin with "end ". The
  // problem only exists for compound directives, like "end do simd".
  // All existing directives with names starting with "end " are either
  // "end directives" for an existing "directive", or leaf directives
  // (such as "end declare target").
  DenseSet<int> EndDirectives;
  for (auto [Rec, Id] : DirId) {
    if (Directive{Rec}.getName().starts_with_insensitive("end "))
      EndDirectives.insert(Id);
  }

  // Avoid sorting the vector<vector> array, instead sort an index array.
  // It will also be useful later to create the auxiliary indexing array.
  std::vector<int> Ordering(Directives.size());
  std::iota(Ordering.begin(), Ordering.end(), 0);

  llvm::sort(Ordering, [&](int A, int B) {
    auto &LeavesA = LeafTable[A];
    auto &LeavesB = LeafTable[B];
    int DirA = LeavesA[0], DirB = LeavesB[0];
    // First of all, end directives compare greater than non-end directives.
    int IsEndA = EndDirectives.count(DirA), IsEndB = EndDirectives.count(DirB);
    if (IsEndA != IsEndB)
      return IsEndA < IsEndB;
    if (LeavesA[1] == 0 && LeavesB[1] == 0)
      return DirA < DirB;
    return std::lexicographical_compare(&LeavesA[2], &LeavesA[2] + LeavesA[1],
                                        &LeavesB[2], &LeavesB[2] + LeavesB[1]);
  });

  // Emit the table

  // The directives are emitted into a scoped enum, for which the underlying
  // type is `int` (by default). The code above uses `int` to store directive
  // ids, so make sure that we catch it when something changes in the
  // underlying type.
  std::string DirectiveType = GetDirectiveType(DirLang);
  OS << "\nstatic_assert(sizeof(" << DirectiveType << ") == sizeof(int));\n";

  OS << "[[maybe_unused]] static const " << DirectiveType << ' ' << TableName
     << "[][" << MaxLeafCount + 2 << "] = {\n";
  for (size_t I = 0, E = Directives.size(); I != E; ++I) {
    auto &Leaves = LeafTable[Ordering[I]];
    OS << "    {" << GetDirectiveName(DirLang, Directives[Leaves[0]]);
    OS << ", static_cast<" << DirectiveType << ">(" << Leaves[1] << "),";
    for (size_t I = 2, E = Leaves.size(); I != E; ++I) {
      int Idx = Leaves[I];
      if (Idx >= 0)
        OS << ' ' << GetDirectiveName(DirLang, Directives[Leaves[I]]) << ',';
      else
        OS << " static_cast<" << DirectiveType << ">(-1),";
    }
    OS << "},\n";
  }
  OS << "};\n\n";

  // Emit a marker where the first "end directive" is.
  auto FirstE = llvm::find_if(Ordering, [&](int RowIdx) {
    return EndDirectives.count(LeafTable[RowIdx][0]);
  });
  OS << "[[maybe_unused]] static auto " << TableName
     << "EndDirective = " << TableName << " + "
     << std::distance(Ordering.begin(), FirstE) << ";\n\n";

  // Emit the auxiliary index table: it's the inverse of the `Ordering`
  // table above.
  OS << "[[maybe_unused]] static const int " << TableName << "Ordering[] = {\n";
  OS << "   ";
  std::vector<int> Reverse(Ordering.size());
  for (int I = 0, E = Ordering.size(); I != E; ++I)
    Reverse[Ordering[I]] = I;
  for (int Idx : Reverse)
    OS << ' ' << Idx << ',';
  OS << "\n};\n";
}

static void GenerateGetDirectiveAssociation(const DirectiveLanguage &DirLang,
                                            raw_ostream &OS) {
  enum struct Association {
    None = 0, // None should be the smallest value.
    Block,    // The values of the rest don't matter.
    Declaration,
    Delimited,
    Loop,
    Separating,
    FromLeaves,
    Invalid,
  };

  std::vector<Record *> associations = DirLang.getAssociations();

  auto getAssocValue = [](StringRef name) -> Association {
    return StringSwitch<Association>(name)
        .Case("AS_Block", Association::Block)
        .Case("AS_Declaration", Association::Declaration)
        .Case("AS_Delimited", Association::Delimited)
        .Case("AS_Loop", Association::Loop)
        .Case("AS_None", Association::None)
        .Case("AS_Separating", Association::Separating)
        .Case("AS_FromLeaves", Association::FromLeaves)
        .Default(Association::Invalid);
  };

  auto getAssocName = [&](Association A) -> StringRef {
    if (A != Association::Invalid && A != Association::FromLeaves) {
      auto F = llvm::find_if(associations, [&](const Record *R) {
        return getAssocValue(R->getName()) == A;
      });
      if (F != associations.end())
        return (*F)->getValueAsString("name"); // enum name
    }
    llvm_unreachable("Unexpected association value");
  };

  auto errorPrefixFor = [&](Directive D) -> std::string {
    return (Twine("Directive '") + D.getName() + "' in namespace '" +
            DirLang.getCppNamespace() + "' ")
        .str();
  };

  auto reduce = [&](Association A, Association B) -> Association {
    if (A > B)
      std::swap(A, B);

    // Calculate the result using the following rules:
    //   x + x = x
    //   AS_None + x = x
    //   AS_Block + AS_Loop = AS_Loop
    if (A == Association::None || A == B)
      return B;
    if (A == Association::Block && B == Association::Loop)
      return B;
    if (A == Association::Loop && B == Association::Block)
      return A;
    return Association::Invalid;
  };

  llvm::DenseMap<const Record *, Association> AsMap;

  auto compAssocImpl = [&](const Record *R, auto &&Self) -> Association {
    if (auto F = AsMap.find(R); F != AsMap.end())
      return F->second;

    Directive D{R};
    Association AS = getAssocValue(D.getAssociation()->getName());
    if (AS == Association::Invalid) {
      PrintFatalError(errorPrefixFor(D) +
                      "has an unrecognized value for association: '" +
                      D.getAssociation()->getName() + "'");
    }
    if (AS != Association::FromLeaves) {
      AsMap.insert(std::make_pair(R, AS));
      return AS;
    }
    // Compute the association from leaf constructs.
    std::vector<Record *> leaves = D.getLeafConstructs();
    if (leaves.empty()) {
      llvm::errs() << D.getName() << '\n';
      PrintFatalError(errorPrefixFor(D) +
                      "requests association to be computed from leaves, "
                      "but it has no leaves");
    }

    Association Result = Self(leaves[0], Self);
    for (int I = 1, E = leaves.size(); I < E; ++I) {
      Association A = Self(leaves[I], Self);
      Association R = reduce(Result, A);
      if (R == Association::Invalid) {
        PrintFatalError(errorPrefixFor(D) +
                        "has leaves with incompatible association values: " +
                        getAssocName(A) + " and " + getAssocName(R));
      }
      Result = R;
    }

    assert(Result != Association::Invalid);
    assert(Result != Association::FromLeaves);
    AsMap.insert(std::make_pair(R, Result));
    return Result;
  };

  for (Record *R : DirLang.getDirectives())
    compAssocImpl(R, compAssocImpl); // Updates AsMap.

  OS << '\n';

  auto getQualifiedName = [&](StringRef Formatted) -> std::string {
    return (llvm::Twine("llvm::") + DirLang.getCppNamespace() +
            "::Directive::" + DirLang.getDirectivePrefix() + Formatted)
        .str();
  };

  std::string DirectiveTypeName =
      std::string("llvm::") + DirLang.getCppNamespace().str() + "::Directive";
  std::string AssociationTypeName =
      std::string("llvm::") + DirLang.getCppNamespace().str() + "::Association";

  OS << AssociationTypeName << " llvm::" << DirLang.getCppNamespace()
     << "::getDirectiveAssociation(" << DirectiveTypeName << " Dir) {\n";
  OS << "  switch (Dir) {\n";
  for (Record *R : DirLang.getDirectives()) {
    if (auto F = AsMap.find(R); F != AsMap.end()) {
      Directive Dir{R};
      OS << "  case " << getQualifiedName(Dir.getFormattedName()) << ":\n";
      OS << "    return " << AssociationTypeName
         << "::" << getAssocName(F->second) << ";\n";
    }
  }
  OS << "  } // switch (Dir)\n";
  OS << "  llvm_unreachable(\"Unexpected directive\");\n";
  OS << "}\n";
}

static void GenerateGetDirectiveCategory(const DirectiveLanguage &DirLang,
                                         raw_ostream &OS) {
  std::string LangNamespace = "llvm::" + DirLang.getCppNamespace().str();
  std::string CategoryTypeName = LangNamespace + "::Category";
  std::string CategoryNamespace = CategoryTypeName + "::";

  OS << '\n';
  OS << CategoryTypeName << ' ' << LangNamespace << "::getDirectiveCategory("
     << GetDirectiveType(DirLang) << " Dir) {\n";
  OS << "  switch (Dir) {\n";

  for (Record *R : DirLang.getDirectives()) {
    Directive D{R};
    OS << "  case " << GetDirectiveName(DirLang, R) << ":\n";
    OS << "    return " << CategoryNamespace
       << D.getCategory()->getValueAsString("name") << ";\n";
  }
  OS << "  } // switch (Dir)\n";
  OS << "  llvm_unreachable(\"Unexpected directive\");\n";
  OS << "}\n";
}

// Generate a simple enum set with the give clauses.
static void GenerateClauseSet(const std::vector<Record *> &Clauses,
                              raw_ostream &OS, StringRef ClauseSetPrefix,
                              Directive &Dir,
                              const DirectiveLanguage &DirLang) {

  OS << "\n";
  OS << "  static " << DirLang.getClauseEnumSetClass() << " " << ClauseSetPrefix
     << DirLang.getDirectivePrefix() << Dir.getFormattedName() << " {\n";

  for (const auto &C : Clauses) {
    VersionedClause VerClause{C};
    OS << "    llvm::" << DirLang.getCppNamespace()
       << "::Clause::" << DirLang.getClausePrefix()
       << VerClause.getClause().getFormattedName() << ",\n";
  }
  OS << "  };\n";
}

// Generate an enum set for the 4 kinds of clauses linked to a directive.
static void GenerateDirectiveClauseSets(const DirectiveLanguage &DirLang,
                                        raw_ostream &OS) {

  IfDefScope Scope("GEN_FLANG_DIRECTIVE_CLAUSE_SETS", OS);

  OS << "\n";
  OS << "namespace llvm {\n";

  // Open namespaces defined in the directive language.
  llvm::SmallVector<StringRef, 2> Namespaces;
  llvm::SplitString(DirLang.getCppNamespace(), Namespaces, "::");
  for (auto Ns : Namespaces)
    OS << "namespace " << Ns << " {\n";

  for (const auto &D : DirLang.getDirectives()) {
    Directive Dir{D};

    OS << "\n";
    OS << "  // Sets for " << Dir.getName() << "\n";

    GenerateClauseSet(Dir.getAllowedClauses(), OS, "allowedClauses_", Dir,
                      DirLang);
    GenerateClauseSet(Dir.getAllowedOnceClauses(), OS, "allowedOnceClauses_",
                      Dir, DirLang);
    GenerateClauseSet(Dir.getAllowedExclusiveClauses(), OS,
                      "allowedExclusiveClauses_", Dir, DirLang);
    GenerateClauseSet(Dir.getRequiredClauses(), OS, "requiredClauses_", Dir,
                      DirLang);
  }

  // Closing namespaces
  for (auto Ns : llvm::reverse(Namespaces))
    OS << "} // namespace " << Ns << "\n";

  OS << "} // namespace llvm\n";
}

// Generate a map of directive (key) with DirectiveClauses struct as values.
// The struct holds the 4 sets of enumeration for the 4 kinds of clauses
// allowances (allowed, allowed once, allowed exclusive and required).
static void GenerateDirectiveClauseMap(const DirectiveLanguage &DirLang,
                                       raw_ostream &OS) {

  IfDefScope Scope("GEN_FLANG_DIRECTIVE_CLAUSE_MAP", OS);

  OS << "\n";
  OS << "{\n";

  for (const auto &D : DirLang.getDirectives()) {
    Directive Dir{D};
    OS << "  {llvm::" << DirLang.getCppNamespace()
       << "::Directive::" << DirLang.getDirectivePrefix()
       << Dir.getFormattedName() << ",\n";
    OS << "    {\n";
    OS << "      llvm::" << DirLang.getCppNamespace() << "::allowedClauses_"
       << DirLang.getDirectivePrefix() << Dir.getFormattedName() << ",\n";
    OS << "      llvm::" << DirLang.getCppNamespace() << "::allowedOnceClauses_"
       << DirLang.getDirectivePrefix() << Dir.getFormattedName() << ",\n";
    OS << "      llvm::" << DirLang.getCppNamespace()
       << "::allowedExclusiveClauses_" << DirLang.getDirectivePrefix()
       << Dir.getFormattedName() << ",\n";
    OS << "      llvm::" << DirLang.getCppNamespace() << "::requiredClauses_"
       << DirLang.getDirectivePrefix() << Dir.getFormattedName() << ",\n";
    OS << "    }\n";
    OS << "  },\n";
  }

  OS << "}\n";
}

// Generate classes entry for Flang clauses in the Flang parse-tree
// If the clause as a non-generic class, no entry is generated.
// If the clause does not hold a value, an EMPTY_CLASS is used.
// If the clause class is generic then a WRAPPER_CLASS is used. When the value
// is optional, the value class is wrapped into a std::optional.
static void GenerateFlangClauseParserClass(const DirectiveLanguage &DirLang,
                                           raw_ostream &OS) {

  IfDefScope Scope("GEN_FLANG_CLAUSE_PARSER_CLASSES", OS);

  OS << "\n";

  for (const auto &C : DirLang.getClauses()) {
    Clause Clause{C};
    if (!Clause.getFlangClass().empty()) {
      OS << "WRAPPER_CLASS(" << Clause.getFormattedParserClassName() << ", ";
      if (Clause.isValueOptional() && Clause.isValueList()) {
        OS << "std::optional<std::list<" << Clause.getFlangClass() << ">>";
      } else if (Clause.isValueOptional()) {
        OS << "std::optional<" << Clause.getFlangClass() << ">";
      } else if (Clause.isValueList()) {
        OS << "std::list<" << Clause.getFlangClass() << ">";
      } else {
        OS << Clause.getFlangClass();
      }
    } else {
      OS << "EMPTY_CLASS(" << Clause.getFormattedParserClassName();
    }
    OS << ");\n";
  }
}

// Generate a list of the different clause classes for Flang.
static void GenerateFlangClauseParserClassList(const DirectiveLanguage &DirLang,
                                               raw_ostream &OS) {

  IfDefScope Scope("GEN_FLANG_CLAUSE_PARSER_CLASSES_LIST", OS);

  OS << "\n";
  llvm::interleaveComma(DirLang.getClauses(), OS, [&](Record *C) {
    Clause Clause{C};
    OS << Clause.getFormattedParserClassName() << "\n";
  });
}

// Generate dump node list for the clauses holding a generic class name.
static void GenerateFlangClauseDump(const DirectiveLanguage &DirLang,
                                    raw_ostream &OS) {

  IfDefScope Scope("GEN_FLANG_DUMP_PARSE_TREE_CLAUSES", OS);

  OS << "\n";
  for (const auto &C : DirLang.getClauses()) {
    Clause Clause{C};
    OS << "NODE(" << DirLang.getFlangClauseBaseClass() << ", "
       << Clause.getFormattedParserClassName() << ")\n";
  }
}

// Generate Unparse functions for clauses classes in the Flang parse-tree
// If the clause is a non-generic class, no entry is generated.
static void GenerateFlangClauseUnparse(const DirectiveLanguage &DirLang,
                                       raw_ostream &OS) {

  IfDefScope Scope("GEN_FLANG_CLAUSE_UNPARSE", OS);

  OS << "\n";

  for (const auto &C : DirLang.getClauses()) {
    Clause Clause{C};
    if (!Clause.getFlangClass().empty()) {
      if (Clause.isValueOptional() && Clause.getDefaultValue().empty()) {
        OS << "void Unparse(const " << DirLang.getFlangClauseBaseClass()
           << "::" << Clause.getFormattedParserClassName() << " &x) {\n";
        OS << "  Word(\"" << Clause.getName().upper() << "\");\n";

        OS << "  Walk(\"(\", x.v, \")\");\n";
        OS << "}\n";
      } else if (Clause.isValueOptional()) {
        OS << "void Unparse(const " << DirLang.getFlangClauseBaseClass()
           << "::" << Clause.getFormattedParserClassName() << " &x) {\n";
        OS << "  Word(\"" << Clause.getName().upper() << "\");\n";
        OS << "  Put(\"(\");\n";
        OS << "  if (x.v.has_value())\n";
        if (Clause.isValueList())
          OS << "    Walk(x.v, \",\");\n";
        else
          OS << "    Walk(x.v);\n";
        OS << "  else\n";
        OS << "    Put(\"" << Clause.getDefaultValue() << "\");\n";
        OS << "  Put(\")\");\n";
        OS << "}\n";
      } else {
        OS << "void Unparse(const " << DirLang.getFlangClauseBaseClass()
           << "::" << Clause.getFormattedParserClassName() << " &x) {\n";
        OS << "  Word(\"" << Clause.getName().upper() << "\");\n";
        OS << "  Put(\"(\");\n";
        if (Clause.isValueList())
          OS << "  Walk(x.v, \",\");\n";
        else
          OS << "  Walk(x.v);\n";
        OS << "  Put(\")\");\n";
        OS << "}\n";
      }
    } else {
      OS << "void Before(const " << DirLang.getFlangClauseBaseClass()
         << "::" << Clause.getFormattedParserClassName() << " &) { Word(\""
         << Clause.getName().upper() << "\"); }\n";
    }
  }
}

// Generate check in the Enter functions for clauses classes.
static void GenerateFlangClauseCheckPrototypes(const DirectiveLanguage &DirLang,
                                               raw_ostream &OS) {

  IfDefScope Scope("GEN_FLANG_CLAUSE_CHECK_ENTER", OS);

  OS << "\n";
  for (const auto &C : DirLang.getClauses()) {
    Clause Clause{C};
    OS << "void Enter(const parser::" << DirLang.getFlangClauseBaseClass()
       << "::" << Clause.getFormattedParserClassName() << " &);\n";
  }
}

// Generate the mapping for clauses between the parser class and the
// corresponding clause Kind
static void GenerateFlangClauseParserKindMap(const DirectiveLanguage &DirLang,
                                             raw_ostream &OS) {

  IfDefScope Scope("GEN_FLANG_CLAUSE_PARSER_KIND_MAP", OS);

  OS << "\n";
  for (const auto &C : DirLang.getClauses()) {
    Clause Clause{C};
    OS << "if constexpr (std::is_same_v<A, parser::"
       << DirLang.getFlangClauseBaseClass()
       << "::" << Clause.getFormattedParserClassName();
    OS << ">)\n";
    OS << "  return llvm::" << DirLang.getCppNamespace()
       << "::Clause::" << DirLang.getClausePrefix() << Clause.getFormattedName()
       << ";\n";
  }

  OS << "llvm_unreachable(\"Invalid " << DirLang.getName()
     << " Parser clause\");\n";
}

static bool compareClauseName(Record *R1, Record *R2) {
  Clause C1{R1};
  Clause C2{R2};
  return (C1.getName() > C2.getName());
}

// Generate the parser for the clauses.
static void GenerateFlangClausesParser(const DirectiveLanguage &DirLang,
                                       raw_ostream &OS) {
  std::vector<Record *> Clauses = DirLang.getClauses();
  // Sort clauses in reverse alphabetical order so with clauses with same
  // beginning, the longer option is tried before.
  llvm::sort(Clauses, compareClauseName);
  IfDefScope Scope("GEN_FLANG_CLAUSES_PARSER", OS);
  OS << "\n";
  unsigned index = 0;
  unsigned lastClauseIndex = DirLang.getClauses().size() - 1;
  OS << "TYPE_PARSER(\n";
  for (const auto &C : Clauses) {
    Clause Clause{C};
    if (Clause.getAliases().empty()) {
      OS << "  \"" << Clause.getName() << "\"";
    } else {
      OS << "  ("
         << "\"" << Clause.getName() << "\"_tok";
      for (StringRef alias : Clause.getAliases()) {
        OS << " || \"" << alias << "\"_tok";
      }
      OS << ")";
    }

    OS << " >> construct<" << DirLang.getFlangClauseBaseClass()
       << ">(construct<" << DirLang.getFlangClauseBaseClass()
       << "::" << Clause.getFormattedParserClassName() << ">(";
    if (Clause.getFlangClass().empty()) {
      OS << "))";
      if (index != lastClauseIndex)
        OS << " ||";
      OS << "\n";
      ++index;
      continue;
    }

    if (Clause.isValueOptional())
      OS << "maybe(";
    OS << "parenthesized(";
    if (Clause.isValueList())
      OS << "nonemptyList(";

    if (!Clause.getPrefix().empty())
      OS << "\"" << Clause.getPrefix() << ":\" >> ";

    // The common Flang parser are used directly. Their name is identical to
    // the Flang class with first letter as lowercase. If the Flang class is
    // not a common class, we assume there is a specific Parser<>{} with the
    // Flang class name provided.
    llvm::SmallString<128> Scratch;
    StringRef Parser =
        llvm::StringSwitch<StringRef>(Clause.getFlangClass())
            .Case("Name", "name")
            .Case("ScalarIntConstantExpr", "scalarIntConstantExpr")
            .Case("ScalarIntExpr", "scalarIntExpr")
            .Case("ScalarExpr", "scalarExpr")
            .Case("ScalarLogicalExpr", "scalarLogicalExpr")
            .Default(("Parser<" + Clause.getFlangClass() + ">{}")
                         .toStringRef(Scratch));
    OS << Parser;
    if (!Clause.getPrefix().empty() && Clause.isPrefixOptional())
      OS << " || " << Parser;
    if (Clause.isValueList()) // close nonemptyList(.
      OS << ")";
    OS << ")"; // close parenthesized(.

    if (Clause.isValueOptional()) // close maybe(.
      OS << ")";
    OS << "))";
    if (index != lastClauseIndex)
      OS << " ||";
    OS << "\n";
    ++index;
  }
  OS << ")\n";
}

// Generate the implementation section for the enumeration in the directive
// language
static void EmitDirectivesFlangImpl(const DirectiveLanguage &DirLang,
                                    raw_ostream &OS) {

  GenerateDirectiveClauseSets(DirLang, OS);

  GenerateDirectiveClauseMap(DirLang, OS);

  GenerateFlangClauseParserClass(DirLang, OS);

  GenerateFlangClauseParserClassList(DirLang, OS);

  GenerateFlangClauseDump(DirLang, OS);

  GenerateFlangClauseUnparse(DirLang, OS);

  GenerateFlangClauseCheckPrototypes(DirLang, OS);

  GenerateFlangClauseParserKindMap(DirLang, OS);

  GenerateFlangClausesParser(DirLang, OS);
}

static void GenerateClauseClassMacro(const DirectiveLanguage &DirLang,
                                     raw_ostream &OS) {
  // Generate macros style information for legacy code in clang
  IfDefScope Scope("GEN_CLANG_CLAUSE_CLASS", OS);

  OS << "\n";

  OS << "#ifndef CLAUSE\n";
  OS << "#define CLAUSE(Enum, Str, Implicit)\n";
  OS << "#endif\n";
  OS << "#ifndef CLAUSE_CLASS\n";
  OS << "#define CLAUSE_CLASS(Enum, Str, Class)\n";
  OS << "#endif\n";
  OS << "#ifndef CLAUSE_NO_CLASS\n";
  OS << "#define CLAUSE_NO_CLASS(Enum, Str)\n";
  OS << "#endif\n";
  OS << "\n";
  OS << "#define __CLAUSE(Name, Class)                      \\\n";
  OS << "  CLAUSE(" << DirLang.getClausePrefix()
     << "##Name, #Name, /* Implicit */ false) \\\n";
  OS << "  CLAUSE_CLASS(" << DirLang.getClausePrefix()
     << "##Name, #Name, Class)\n";
  OS << "#define __CLAUSE_NO_CLASS(Name)                    \\\n";
  OS << "  CLAUSE(" << DirLang.getClausePrefix()
     << "##Name, #Name, /* Implicit */ false) \\\n";
  OS << "  CLAUSE_NO_CLASS(" << DirLang.getClausePrefix() << "##Name, #Name)\n";
  OS << "#define __IMPLICIT_CLAUSE_CLASS(Name, Str, Class)  \\\n";
  OS << "  CLAUSE(" << DirLang.getClausePrefix()
     << "##Name, Str, /* Implicit */ true)    \\\n";
  OS << "  CLAUSE_CLASS(" << DirLang.getClausePrefix()
     << "##Name, Str, Class)\n";
  OS << "#define __IMPLICIT_CLAUSE_NO_CLASS(Name, Str)      \\\n";
  OS << "  CLAUSE(" << DirLang.getClausePrefix()
     << "##Name, Str, /* Implicit */ true)    \\\n";
  OS << "  CLAUSE_NO_CLASS(" << DirLang.getClausePrefix() << "##Name, Str)\n";
  OS << "\n";

  for (const auto &R : DirLang.getClauses()) {
    Clause C{R};
    if (C.getClangClass().empty()) { // NO_CLASS
      if (C.isImplicit()) {
        OS << "__IMPLICIT_CLAUSE_NO_CLASS(" << C.getFormattedName() << ", \""
           << C.getFormattedName() << "\")\n";
      } else {
        OS << "__CLAUSE_NO_CLASS(" << C.getFormattedName() << ")\n";
      }
    } else { // CLASS
      if (C.isImplicit()) {
        OS << "__IMPLICIT_CLAUSE_CLASS(" << C.getFormattedName() << ", \""
           << C.getFormattedName() << "\", " << C.getClangClass() << ")\n";
      } else {
        OS << "__CLAUSE(" << C.getFormattedName() << ", " << C.getClangClass()
           << ")\n";
      }
    }
  }

  OS << "\n";
  OS << "#undef __IMPLICIT_CLAUSE_NO_CLASS\n";
  OS << "#undef __IMPLICIT_CLAUSE_CLASS\n";
  OS << "#undef __CLAUSE_NO_CLASS\n";
  OS << "#undef __CLAUSE\n";
  OS << "#undef CLAUSE_NO_CLASS\n";
  OS << "#undef CLAUSE_CLASS\n";
  OS << "#undef CLAUSE\n";
}

// Generate the implemenation for the enumeration in the directive
// language. This code can be included in library.
void EmitDirectivesBasicImpl(const DirectiveLanguage &DirLang,
                             raw_ostream &OS) {
  IfDefScope Scope("GEN_DIRECTIVES_IMPL", OS);

  OS << "\n#include \"llvm/Support/ErrorHandling.h\"\n";

  // getDirectiveKind(StringRef Str)
  GenerateGetKind(DirLang.getDirectives(), OS, "Directive", DirLang,
                  DirLang.getDirectivePrefix(), /*ImplicitAsUnknown=*/false);

  // getDirectiveName(Directive Kind)
  GenerateGetName(DirLang.getDirectives(), OS, "Directive", DirLang,
                  DirLang.getDirectivePrefix());

  // getClauseKind(StringRef Str)
  GenerateGetKind(DirLang.getClauses(), OS, "Clause", DirLang,
                  DirLang.getClausePrefix(),
                  /*ImplicitAsUnknown=*/true);

  // getClauseName(Clause Kind)
  GenerateGetName(DirLang.getClauses(), OS, "Clause", DirLang,
                  DirLang.getClausePrefix());

  // get<ClauseVal>Kind(StringRef Str)
  GenerateGetKindClauseVal(DirLang, OS);

  // isAllowedClauseForDirective(Directive D, Clause C, unsigned Version)
  GenerateIsAllowedClause(DirLang, OS);

  // getDirectiveAssociation(Directive D)
  GenerateGetDirectiveAssociation(DirLang, OS);

  // getDirectiveCategory(Directive D)
  GenerateGetDirectiveCategory(DirLang, OS);

  // Leaf table for getLeafConstructs, etc.
  EmitLeafTable(DirLang, OS, "LeafConstructTable");
}

// Generate the implemenation section for the enumeration in the directive
// language.
static void EmitDirectivesImpl(RecordKeeper &Records, raw_ostream &OS) {
  const auto DirLang = DirectiveLanguage{Records};
  if (DirLang.HasValidityErrors())
    return;

  EmitDirectivesFlangImpl(DirLang, OS);

  GenerateClauseClassMacro(DirLang, OS);

  EmitDirectivesBasicImpl(DirLang, OS);
}

static TableGen::Emitter::Opt
    X("gen-directive-decl", EmitDirectivesDecl,
      "Generate directive related declaration code (header file)");

static TableGen::Emitter::Opt
    Y("gen-directive-impl", EmitDirectivesImpl,
      "Generate directive related implementation code");

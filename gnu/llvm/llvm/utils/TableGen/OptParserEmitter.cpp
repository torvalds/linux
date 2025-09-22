//===- OptParserEmitter.cpp - Table Driven Command Line Parsing -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Common/OptEmitter.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <cstring>
#include <map>
#include <memory>

using namespace llvm;

static std::string getOptionName(const Record &R) {
  // Use the record name unless EnumName is defined.
  if (isa<UnsetInit>(R.getValueInit("EnumName")))
    return std::string(R.getName());

  return std::string(R.getValueAsString("EnumName"));
}

static raw_ostream &write_cstring(raw_ostream &OS, llvm::StringRef Str) {
  OS << '"';
  OS.write_escaped(Str);
  OS << '"';
  return OS;
}

static std::string getOptionPrefixedName(const Record &R) {
  std::vector<StringRef> Prefixes = R.getValueAsListOfStrings("Prefixes");
  StringRef Name = R.getValueAsString("Name");

  if (Prefixes.empty())
    return Name.str();

  return (Prefixes[0] + Twine(Name)).str();
}

class MarshallingInfo {
public:
  static constexpr const char *MacroName = "OPTION_WITH_MARSHALLING";
  const Record &R;
  bool ShouldAlwaysEmit = false;
  StringRef MacroPrefix;
  StringRef KeyPath;
  StringRef DefaultValue;
  StringRef NormalizedValuesScope;
  StringRef ImpliedCheck;
  StringRef ImpliedValue;
  StringRef ShouldParse;
  StringRef Normalizer;
  StringRef Denormalizer;
  StringRef ValueMerger;
  StringRef ValueExtractor;
  int TableIndex = -1;
  std::vector<StringRef> Values;
  std::vector<StringRef> NormalizedValues;
  std::string ValueTableName;

  static size_t NextTableIndex;

  static constexpr const char *ValueTablePreamble = R"(
struct SimpleEnumValue {
  const char *Name;
  unsigned Value;
};

struct SimpleEnumValueTable {
  const SimpleEnumValue *Table;
  unsigned Size;
};
)";

  static constexpr const char *ValueTablesDecl =
      "static const SimpleEnumValueTable SimpleEnumValueTables[] = ";

  MarshallingInfo(const Record &R) : R(R) {}

  std::string getMacroName() const {
    return (MacroPrefix + MarshallingInfo::MacroName).str();
  }

  void emit(raw_ostream &OS) const {
    OS << ShouldParse;
    OS << ", ";
    OS << ShouldAlwaysEmit;
    OS << ", ";
    OS << KeyPath;
    OS << ", ";
    emitScopedNormalizedValue(OS, DefaultValue);
    OS << ", ";
    OS << ImpliedCheck;
    OS << ", ";
    emitScopedNormalizedValue(OS, ImpliedValue);
    OS << ", ";
    OS << Normalizer;
    OS << ", ";
    OS << Denormalizer;
    OS << ", ";
    OS << ValueMerger;
    OS << ", ";
    OS << ValueExtractor;
    OS << ", ";
    OS << TableIndex;
  }

  std::optional<StringRef> emitValueTable(raw_ostream &OS) const {
    if (TableIndex == -1)
      return {};
    OS << "static const SimpleEnumValue " << ValueTableName << "[] = {\n";
    for (unsigned I = 0, E = Values.size(); I != E; ++I) {
      OS << "{";
      write_cstring(OS, Values[I]);
      OS << ",";
      OS << "static_cast<unsigned>(";
      emitScopedNormalizedValue(OS, NormalizedValues[I]);
      OS << ")},";
    }
    OS << "};\n";
    return StringRef(ValueTableName);
  }

private:
  void emitScopedNormalizedValue(raw_ostream &OS,
                                 StringRef NormalizedValue) const {
    if (!NormalizedValuesScope.empty())
      OS << NormalizedValuesScope << "::";
    OS << NormalizedValue;
  }
};

size_t MarshallingInfo::NextTableIndex = 0;

static MarshallingInfo createMarshallingInfo(const Record &R) {
  assert(!isa<UnsetInit>(R.getValueInit("KeyPath")) &&
         !isa<UnsetInit>(R.getValueInit("DefaultValue")) &&
         !isa<UnsetInit>(R.getValueInit("ValueMerger")) &&
         "MarshallingInfo must have a provide a keypath, default value and a "
         "value merger");

  MarshallingInfo Ret(R);

  Ret.ShouldAlwaysEmit = R.getValueAsBit("ShouldAlwaysEmit");
  Ret.MacroPrefix = R.getValueAsString("MacroPrefix");
  Ret.KeyPath = R.getValueAsString("KeyPath");
  Ret.DefaultValue = R.getValueAsString("DefaultValue");
  Ret.NormalizedValuesScope = R.getValueAsString("NormalizedValuesScope");
  Ret.ImpliedCheck = R.getValueAsString("ImpliedCheck");
  Ret.ImpliedValue =
      R.getValueAsOptionalString("ImpliedValue").value_or(Ret.DefaultValue);

  Ret.ShouldParse = R.getValueAsString("ShouldParse");
  Ret.Normalizer = R.getValueAsString("Normalizer");
  Ret.Denormalizer = R.getValueAsString("Denormalizer");
  Ret.ValueMerger = R.getValueAsString("ValueMerger");
  Ret.ValueExtractor = R.getValueAsString("ValueExtractor");

  if (!isa<UnsetInit>(R.getValueInit("NormalizedValues"))) {
    assert(!isa<UnsetInit>(R.getValueInit("Values")) &&
           "Cannot provide normalized values for value-less options");
    Ret.TableIndex = MarshallingInfo::NextTableIndex++;
    Ret.NormalizedValues = R.getValueAsListOfStrings("NormalizedValues");
    Ret.Values.reserve(Ret.NormalizedValues.size());
    Ret.ValueTableName = getOptionName(R) + "ValueTable";

    StringRef ValuesStr = R.getValueAsString("Values");
    for (;;) {
      size_t Idx = ValuesStr.find(',');
      if (Idx == StringRef::npos)
        break;
      if (Idx > 0)
        Ret.Values.push_back(ValuesStr.slice(0, Idx));
      ValuesStr = ValuesStr.slice(Idx + 1, StringRef::npos);
    }
    if (!ValuesStr.empty())
      Ret.Values.push_back(ValuesStr);

    assert(Ret.Values.size() == Ret.NormalizedValues.size() &&
           "The number of normalized values doesn't match the number of "
           "values");
  }

  return Ret;
}

static void EmitHelpTextsForVariants(
    raw_ostream &OS, std::vector<std::pair<std::vector<std::string>, StringRef>>
                         HelpTextsForVariants) {
  // OptTable must be constexpr so it uses std::arrays with these capacities.
  const unsigned MaxVisibilityPerHelp = 2;
  const unsigned MaxVisibilityHelp = 1;

  assert(HelpTextsForVariants.size() <= MaxVisibilityHelp &&
         "Too many help text variants to store in "
         "OptTable::HelpTextsForVariants");

  // This function must initialise any unused elements of those arrays.
  for (auto [Visibilities, _] : HelpTextsForVariants)
    while (Visibilities.size() < MaxVisibilityPerHelp)
      Visibilities.push_back("0");

  while (HelpTextsForVariants.size() < MaxVisibilityHelp)
    HelpTextsForVariants.push_back(
        {std::vector<std::string>(MaxVisibilityPerHelp, "0"), ""});

  OS << ", (std::array<std::pair<std::array<unsigned, " << MaxVisibilityPerHelp
     << ">, const char*>, " << MaxVisibilityHelp << ">{{ ";

  auto VisibilityHelpEnd = HelpTextsForVariants.cend();
  for (auto VisibilityHelp = HelpTextsForVariants.cbegin();
       VisibilityHelp != VisibilityHelpEnd; ++VisibilityHelp) {
    auto [Visibilities, Help] = *VisibilityHelp;

    assert(Visibilities.size() <= MaxVisibilityPerHelp &&
           "Too many visibilities to store in an "
           "OptTable::HelpTextsForVariants entry");
    OS << "std::make_pair(std::array<unsigned, " << MaxVisibilityPerHelp
       << ">{{";

    auto VisibilityEnd = Visibilities.cend();
    for (auto Visibility = Visibilities.cbegin(); Visibility != VisibilityEnd;
         ++Visibility) {
      OS << *Visibility;
      if (std::next(Visibility) != VisibilityEnd)
        OS << ", ";
    }

    OS << "}}, ";

    if (Help.size())
      write_cstring(OS, Help);
    else
      OS << "nullptr";
    OS << ")";

    if (std::next(VisibilityHelp) != VisibilityHelpEnd)
      OS << ", ";
  }
  OS << " }})";
}

/// OptParserEmitter - This tablegen backend takes an input .td file
/// describing a list of options and emits a data structure for parsing and
/// working with those options when given an input command line.
static void EmitOptParser(RecordKeeper &Records, raw_ostream &OS) {
  // Get the option groups and options.
  const std::vector<Record *> &Groups =
      Records.getAllDerivedDefinitions("OptionGroup");
  std::vector<Record *> Opts = Records.getAllDerivedDefinitions("Option");

  emitSourceFileHeader("Option Parsing Definitions", OS);

  array_pod_sort(Opts.begin(), Opts.end(), CompareOptionRecords);
  // Generate prefix groups.
  typedef SmallVector<SmallString<2>, 2> PrefixKeyT;
  typedef std::map<PrefixKeyT, std::string> PrefixesT;
  PrefixesT Prefixes;
  Prefixes.insert(std::pair(PrefixKeyT(), "prefix_0"));
  unsigned CurPrefix = 0;
  for (const Record &R : llvm::make_pointee_range(Opts)) {
    std::vector<StringRef> RPrefixes = R.getValueAsListOfStrings("Prefixes");
    PrefixKeyT PrefixKey(RPrefixes.begin(), RPrefixes.end());
    unsigned NewPrefix = CurPrefix + 1;
    std::string Prefix = (Twine("prefix_") + Twine(NewPrefix)).str();
    if (Prefixes.insert(std::pair(PrefixKey, Prefix)).second)
      CurPrefix = NewPrefix;
  }

  DenseSet<StringRef> PrefixesUnionSet;
  for (const auto &Prefix : Prefixes)
    PrefixesUnionSet.insert(Prefix.first.begin(), Prefix.first.end());
  SmallVector<StringRef> PrefixesUnion(PrefixesUnionSet.begin(),
                                       PrefixesUnionSet.end());
  array_pod_sort(PrefixesUnion.begin(), PrefixesUnion.end());

  // Dump prefixes.
  OS << "/////////\n";
  OS << "// Prefixes\n\n";
  OS << "#ifdef PREFIX\n";
  OS << "#define COMMA ,\n";
  for (const auto &Prefix : Prefixes) {
    OS << "PREFIX(";

    // Prefix name.
    OS << Prefix.second;

    // Prefix values.
    OS << ", {";
    for (const auto &PrefixKey : Prefix.first)
      OS << "llvm::StringLiteral(\"" << PrefixKey << "\") COMMA ";
    // Append an empty element to avoid ending up with an empty array.
    OS << "llvm::StringLiteral(\"\")})\n";
  }
  OS << "#undef COMMA\n";
  OS << "#endif // PREFIX\n\n";

  // Dump prefix unions.
  OS << "/////////\n";
  OS << "// Prefix Union\n\n";
  OS << "#ifdef PREFIX_UNION\n";
  OS << "#define COMMA ,\n";
  OS << "PREFIX_UNION({\n";
  for (const auto &Prefix : PrefixesUnion) {
    OS << "llvm::StringLiteral(\"" << Prefix << "\") COMMA ";
  }
  OS << "llvm::StringLiteral(\"\")})\n";
  OS << "#undef COMMA\n";
  OS << "#endif // PREFIX_UNION\n\n";

  // Dump groups.
  OS << "/////////\n";
  OS << "// ValuesCode\n\n";
  OS << "#ifdef OPTTABLE_VALUES_CODE\n";
  for (const Record &R : llvm::make_pointee_range(Opts)) {
    // The option values, if any;
    if (!isa<UnsetInit>(R.getValueInit("ValuesCode"))) {
      assert(isa<UnsetInit>(R.getValueInit("Values")) &&
             "Cannot choose between Values and ValuesCode");
      OS << "#define VALUES_CODE " << getOptionName(R) << "_Values\n";
      OS << R.getValueAsString("ValuesCode") << "\n";
      OS << "#undef VALUES_CODE\n";
    }
  }
  OS << "#endif\n";

  OS << "/////////\n";
  OS << "// Groups\n\n";
  OS << "#ifdef OPTION\n";
  for (const Record &R : llvm::make_pointee_range(Groups)) {
    // Start a single option entry.
    OS << "OPTION(";

    // The option prefix;
    OS << "llvm::ArrayRef<llvm::StringLiteral>()";

    // The option string.
    OS << ", \"" << R.getValueAsString("Name") << '"';

    // The option identifier name.
    OS << ", " << getOptionName(R);

    // The option kind.
    OS << ", Group";

    // The containing option group (if any).
    OS << ", ";
    if (const DefInit *DI = dyn_cast<DefInit>(R.getValueInit("Group")))
      OS << getOptionName(*DI->getDef());
    else
      OS << "INVALID";

    // The other option arguments (unused for groups).
    OS << ", INVALID, nullptr, 0, 0, 0";

    // The option help text.
    if (!isa<UnsetInit>(R.getValueInit("HelpText"))) {
      OS << ",\n";
      OS << "       ";
      write_cstring(OS, R.getValueAsString("HelpText"));
    } else
      OS << ", nullptr";

    // Not using Visibility specific text for group help.
    EmitHelpTextsForVariants(OS, {});

    // The option meta-variable name (unused).
    OS << ", nullptr";

    // The option Values (unused for groups).
    OS << ", nullptr)\n";
  }
  OS << "\n";

  OS << "//////////\n";
  OS << "// Options\n\n";

  auto WriteOptRecordFields = [&](raw_ostream &OS, const Record &R) {
    // The option prefix;
    std::vector<StringRef> RPrefixes = R.getValueAsListOfStrings("Prefixes");
    OS << Prefixes[PrefixKeyT(RPrefixes.begin(), RPrefixes.end())] << ", ";

    // The option prefixed name.
    write_cstring(OS, getOptionPrefixedName(R));

    // The option identifier name.
    OS << ", " << getOptionName(R);

    // The option kind.
    OS << ", " << R.getValueAsDef("Kind")->getValueAsString("Name");

    // The containing option group (if any).
    OS << ", ";
    const ListInit *GroupFlags = nullptr;
    const ListInit *GroupVis = nullptr;
    if (const DefInit *DI = dyn_cast<DefInit>(R.getValueInit("Group"))) {
      GroupFlags = DI->getDef()->getValueAsListInit("Flags");
      GroupVis = DI->getDef()->getValueAsListInit("Visibility");
      OS << getOptionName(*DI->getDef());
    } else
      OS << "INVALID";

    // The option alias (if any).
    OS << ", ";
    if (const DefInit *DI = dyn_cast<DefInit>(R.getValueInit("Alias")))
      OS << getOptionName(*DI->getDef());
    else
      OS << "INVALID";

    // The option alias arguments (if any).
    // Emitted as a \0 separated list in a string, e.g. ["foo", "bar"]
    // would become "foo\0bar\0". Note that the compiler adds an implicit
    // terminating \0 at the end.
    OS << ", ";
    std::vector<StringRef> AliasArgs = R.getValueAsListOfStrings("AliasArgs");
    if (AliasArgs.size() == 0) {
      OS << "nullptr";
    } else {
      OS << "\"";
      for (StringRef AliasArg : AliasArgs)
        OS << AliasArg << "\\0";
      OS << "\"";
    }

    // "Flags" for the option, such as HelpHidden and Render*
    OS << ", ";
    int NumFlags = 0;
    const ListInit *LI = R.getValueAsListInit("Flags");
    for (Init *I : *LI)
      OS << (NumFlags++ ? " | " : "") << cast<DefInit>(I)->getDef()->getName();
    if (GroupFlags) {
      for (Init *I : *GroupFlags)
        OS << (NumFlags++ ? " | " : "")
           << cast<DefInit>(I)->getDef()->getName();
    }
    if (NumFlags == 0)
      OS << '0';

    // Option visibility, for sharing options between drivers.
    OS << ", ";
    int NumVisFlags = 0;
    LI = R.getValueAsListInit("Visibility");
    for (Init *I : *LI)
      OS << (NumVisFlags++ ? " | " : "")
         << cast<DefInit>(I)->getDef()->getName();
    if (GroupVis) {
      for (Init *I : *GroupVis)
        OS << (NumVisFlags++ ? " | " : "")
           << cast<DefInit>(I)->getDef()->getName();
    }
    if (NumVisFlags == 0)
      OS << '0';

    // The option parameter field.
    OS << ", " << R.getValueAsInt("NumArgs");

    // The option help text.
    if (!isa<UnsetInit>(R.getValueInit("HelpText"))) {
      OS << ",\n";
      OS << "       ";
      write_cstring(OS, R.getValueAsString("HelpText"));
    } else
      OS << ", nullptr";

    std::vector<std::pair<std::vector<std::string>, StringRef>>
        HelpTextsForVariants;
    for (Record *VisibilityHelp :
         R.getValueAsListOfDefs("HelpTextsForVariants")) {
      ArrayRef<Init *> Visibilities =
          VisibilityHelp->getValueAsListInit("Visibilities")->getValues();

      std::vector<std::string> VisibilityNames;
      for (Init *Visibility : Visibilities)
        VisibilityNames.push_back(Visibility->getAsUnquotedString());

      HelpTextsForVariants.push_back(std::make_pair(
          VisibilityNames, VisibilityHelp->getValueAsString("Text")));
    }
    EmitHelpTextsForVariants(OS, HelpTextsForVariants);

    // The option meta-variable name.
    OS << ", ";
    if (!isa<UnsetInit>(R.getValueInit("MetaVarName")))
      write_cstring(OS, R.getValueAsString("MetaVarName"));
    else
      OS << "nullptr";

    // The option Values. Used for shell autocompletion.
    OS << ", ";
    if (!isa<UnsetInit>(R.getValueInit("Values")))
      write_cstring(OS, R.getValueAsString("Values"));
    else if (!isa<UnsetInit>(R.getValueInit("ValuesCode"))) {
      OS << getOptionName(R) << "_Values";
    } else
      OS << "nullptr";
  };

  auto IsMarshallingOption = [](const Record &R) {
    return !isa<UnsetInit>(R.getValueInit("KeyPath")) &&
           !R.getValueAsString("KeyPath").empty();
  };

  std::vector<const Record *> OptsWithMarshalling;
  for (const Record &R : llvm::make_pointee_range(Opts)) {
    // Start a single option entry.
    OS << "OPTION(";
    WriteOptRecordFields(OS, R);
    OS << ")\n";
    if (IsMarshallingOption(R))
      OptsWithMarshalling.push_back(&R);
  }
  OS << "#endif // OPTION\n";

  auto CmpMarshallingOpts = [](const Record *const *A, const Record *const *B) {
    unsigned AID = (*A)->getID();
    unsigned BID = (*B)->getID();

    if (AID < BID)
      return -1;
    if (AID > BID)
      return 1;
    return 0;
  };
  // The RecordKeeper stores records (options) in lexicographical order, and we
  // have reordered the options again when generating prefix groups. We need to
  // restore the original definition order of options with marshalling to honor
  // the topology of the dependency graph implied by `DefaultAnyOf`.
  array_pod_sort(OptsWithMarshalling.begin(), OptsWithMarshalling.end(),
                 CmpMarshallingOpts);

  std::vector<MarshallingInfo> MarshallingInfos;
  MarshallingInfos.reserve(OptsWithMarshalling.size());
  for (const auto *R : OptsWithMarshalling)
    MarshallingInfos.push_back(createMarshallingInfo(*R));

  for (const auto &MI : MarshallingInfos) {
    OS << "#ifdef " << MI.getMacroName() << "\n";
    OS << MI.getMacroName() << "(";
    WriteOptRecordFields(OS, MI.R);
    OS << ", ";
    MI.emit(OS);
    OS << ")\n";
    OS << "#endif // " << MI.getMacroName() << "\n";
  }

  OS << "\n";
  OS << "#ifdef SIMPLE_ENUM_VALUE_TABLE";
  OS << "\n";
  OS << MarshallingInfo::ValueTablePreamble;
  std::vector<StringRef> ValueTableNames;
  for (const auto &MI : MarshallingInfos)
    if (auto MaybeValueTableName = MI.emitValueTable(OS))
      ValueTableNames.push_back(*MaybeValueTableName);

  OS << MarshallingInfo::ValueTablesDecl << "{";
  for (auto ValueTableName : ValueTableNames)
    OS << "{" << ValueTableName << ", std::size(" << ValueTableName << ")},\n";
  OS << "};\n";
  OS << "static const unsigned SimpleEnumValueTablesSize = "
        "std::size(SimpleEnumValueTables);\n";

  OS << "#endif // SIMPLE_ENUM_VALUE_TABLE\n";
  OS << "\n";

  OS << "\n";
}

static TableGen::Emitter::Opt X("gen-opt-parser-defs", EmitOptParser,
                                "Generate option definitions");

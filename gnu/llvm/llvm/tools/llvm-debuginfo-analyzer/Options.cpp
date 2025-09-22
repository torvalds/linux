//===-- options.cpp - Command line options for llvm-debuginfo-analyzer----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This handles the command line options for llvm-debuginfo-analyzer.
//
//===----------------------------------------------------------------------===//

#include "Options.h"
#include "llvm/DebugInfo/LogicalView/Core/LVOptions.h"
#include "llvm/DebugInfo/LogicalView/Core/LVSort.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;
using namespace llvm::logicalview;
using namespace llvm::logicalview::cmdline;

/// @}
/// Command line options.
/// @{

OffsetParser::OffsetParser(cl::Option &O) : parser<unsigned long long>(O) {}
OffsetParser::~OffsetParser() = default;

bool OffsetParser::parse(cl::Option &O, StringRef ArgName, StringRef Arg,
                         unsigned long long &Val) {
  char *End;
  std::string Argument(Arg);
  Val = strtoull(Argument.c_str(), &End, 0);
  if (*End)
    // Print an error message if unrecognized character.
    return O.error("'" + Arg + "' unrecognized character.");
  return false;
}

LVOptions cmdline::ReaderOptions;

//===----------------------------------------------------------------------===//
// Specific options
//===----------------------------------------------------------------------===//
cl::list<std::string>
    cmdline::InputFilenames(cl::desc("<input object files or .dSYM bundles>"),
                            cl::Positional, cl::ZeroOrMore);

//===----------------------------------------------------------------------===//
// '--attribute' options
//===----------------------------------------------------------------------===//
cl::OptionCategory
    cmdline::AttributeCategory("Attribute Options",
                               "These control extra attributes that are "
                               "added when the element is printed.");

// --attribute=<value>[,<value>,...]
cl::list<LVAttributeKind> cmdline::AttributeOptions(
    "attribute", cl::cat(AttributeCategory), cl::desc("Element attributes."),
    cl::Hidden, cl::CommaSeparated,
    values(clEnumValN(LVAttributeKind::All, "all", "Include all attributes."),
           clEnumValN(LVAttributeKind::Argument, "argument",
                      "Template parameters replaced by its arguments."),
           clEnumValN(LVAttributeKind::Base, "base",
                      "Base types (int, bool, etc.)."),
           clEnumValN(LVAttributeKind::Coverage, "coverage",
                      "Symbol location coverage."),
           clEnumValN(LVAttributeKind::Directories, "directories",
                      "Directories referenced in the debug information."),
           clEnumValN(LVAttributeKind::Discarded, "discarded",
                      "Discarded elements by the linker."),
           clEnumValN(LVAttributeKind::Discriminator, "discriminator",
                      "Discriminators for inlined function instances."),
           clEnumValN(LVAttributeKind::Encoded, "encoded",
                      "Template arguments encoded in the template name."),
           clEnumValN(LVAttributeKind::Extended, "extended",
                      "Advanced attributes alias."),
           clEnumValN(LVAttributeKind::Filename, "filename",
                      "Filename where the element is defined."),
           clEnumValN(LVAttributeKind::Files, "files",
                      "Files referenced in the debug information."),
           clEnumValN(LVAttributeKind::Format, "format",
                      "Object file format name."),
           clEnumValN(LVAttributeKind::Gaps, "gaps",
                      "Missing debug location (gaps)."),
           clEnumValN(LVAttributeKind::Generated, "generated",
                      "Compiler generated elements."),
           clEnumValN(LVAttributeKind::Global, "global",
                      "Element referenced across Compile Units."),
           clEnumValN(LVAttributeKind::Inserted, "inserted",
                      "Generated inlined abstract references."),
           clEnumValN(LVAttributeKind::Level, "level",
                      "Lexical scope level (File=0, Compile Unit=1)."),
           clEnumValN(LVAttributeKind::Linkage, "linkage", "Linkage name."),
           clEnumValN(LVAttributeKind::Local, "local",
                      "Element referenced only in the Compile Unit."),
           clEnumValN(LVAttributeKind::Location, "location",
                      "Element debug location."),
           clEnumValN(LVAttributeKind::Offset, "offset",
                      "Debug information offset."),
           clEnumValN(LVAttributeKind::Pathname, "pathname",
                      "Pathname where the element is defined."),
           clEnumValN(LVAttributeKind::Producer, "producer",
                      "Toolchain identification name."),
           clEnumValN(LVAttributeKind::Publics, "publics",
                      "Function names that are public."),
           clEnumValN(LVAttributeKind::Qualified, "qualified",
                      "The element type include parents in its name."),
           clEnumValN(LVAttributeKind::Qualifier, "qualifier",
                      "Line qualifiers (Newstatement, BasicBlock, etc.)."),
           clEnumValN(LVAttributeKind::Range, "range",
                      "Debug location ranges."),
           clEnumValN(LVAttributeKind::Reference, "reference",
                      "Element declaration and definition references."),
           clEnumValN(LVAttributeKind::Register, "register",
                      "Processor register names."),
           clEnumValN(LVAttributeKind::Standard, "standard",
                      "Basic attributes alias."),
           clEnumValN(LVAttributeKind::Subrange, "subrange",
                      "Subrange encoding information for arrays."),
           clEnumValN(LVAttributeKind::System, "system",
                      "Display PDB's MS system elements."),
           clEnumValN(LVAttributeKind::Typename, "typename",
                      "Include Parameters in templates."),
           clEnumValN(LVAttributeKind::Underlying, "underlying",
                      "Underlying type for type definitions."),
           clEnumValN(LVAttributeKind::Zero, "zero", "Zero line numbers.")));

//===----------------------------------------------------------------------===//
// '--compare' options
//===----------------------------------------------------------------------===//
cl::OptionCategory
    cmdline::CompareCategory("Compare Options",
                             "These control the view comparison.");

// --compare-context
static cl::opt<bool, true>
    CompareContext("compare-context", cl::cat(CompareCategory),
                   cl::desc("Add the view as compare context."), cl::Hidden,
                   cl::ZeroOrMore, cl::location(ReaderOptions.Compare.Context),
                   cl::init(false));

// --compare=<value>[,<value>,...]
cl::list<LVCompareKind> cmdline::CompareElements(
    "compare", cl::cat(CompareCategory), cl::desc("Elements to compare."),
    cl::Hidden, cl::CommaSeparated,
    values(clEnumValN(LVCompareKind::All, "all", "Compare all elements."),
           clEnumValN(LVCompareKind::Lines, "lines", "Lines."),
           clEnumValN(LVCompareKind::Scopes, "scopes", "Scopes."),
           clEnumValN(LVCompareKind::Symbols, "symbols", "Symbols."),
           clEnumValN(LVCompareKind::Types, "types", "Types.")));

//===----------------------------------------------------------------------===//
// '--output' options
//===----------------------------------------------------------------------===//
cl::OptionCategory
    cmdline::OutputCategory("Output Options",
                            "These control the output generated.");

// --output-file=<filename>
cl::opt<std::string>
    cmdline::OutputFilename("output-file", cl::cat(OutputCategory),
                            cl::desc("Redirect output to the specified file."),
                            cl::Hidden, cl::value_desc("filename"),
                            cl::init("-"));

// --output-folder=<path>
static cl::opt<std::string, true>
    OutputFolder("output-folder", cl::cat(OutputCategory),
                 cl::desc("Folder name for view splitting."),
                 cl::value_desc("pathname"), cl::Hidden, cl::ZeroOrMore,
                 cl::location(ReaderOptions.Output.Folder));

// --output-level=<level>
static cl::opt<unsigned, true>
    OutputLevel("output-level", cl::cat(OutputCategory),
                cl::desc("Only print to a depth of N elements."),
                cl::value_desc("N"), cl::Hidden, cl::ZeroOrMore,
                cl::location(ReaderOptions.Output.Level), cl::init(-1U));

// --ouput=<value>[,<value>,...]
cl::list<LVOutputKind> cmdline::OutputOptions(
    "output", cl::cat(OutputCategory), cl::desc("Outputs for view."),
    cl::Hidden, cl::CommaSeparated,
    values(clEnumValN(LVOutputKind::All, "all", "All outputs."),
           clEnumValN(LVOutputKind::Split, "split",
                      "Split the output by Compile Units."),
           clEnumValN(LVOutputKind::Text, "text",
                      "Use a free form text output."),
           clEnumValN(LVOutputKind::Json, "json",
                      "Use JSON as the output format.")));

// --output-sort
static cl::opt<LVSortMode, true> OutputSort(
    "output-sort", cl::cat(OutputCategory),
    cl::desc("Primary key when ordering logical view (default: line)."),
    cl::Hidden, cl::ZeroOrMore,
    values(clEnumValN(LVSortMode::Kind, "kind", "Sort by element kind."),
           clEnumValN(LVSortMode::Line, "line", "Sort by element line number."),
           clEnumValN(LVSortMode::Name, "name", "Sort by element name."),
           clEnumValN(LVSortMode::Offset, "offset", "Sort by element offset.")),
    cl::location(ReaderOptions.Output.SortMode), cl::init(LVSortMode::Line));

//===----------------------------------------------------------------------===//
// '--print' options
//===----------------------------------------------------------------------===//
cl::OptionCategory
    cmdline::PrintCategory("Print Options",
                           "These control which elements are printed.");

// --print=<value>[,<value>,...]
cl::list<LVPrintKind> cmdline::PrintOptions(
    "print", cl::cat(PrintCategory), cl::desc("Element to print."),
    cl::CommaSeparated,
    values(clEnumValN(LVPrintKind::All, "all", "All elements."),
           clEnumValN(LVPrintKind::Elements, "elements",
                      "Instructions, lines, scopes, symbols and types."),
           clEnumValN(LVPrintKind::Instructions, "instructions",
                      "Assembler instructions."),
           clEnumValN(LVPrintKind::Lines, "lines",
                      "Lines referenced in the debug information."),
           clEnumValN(LVPrintKind::Scopes, "scopes",
                      "A lexical block (Function, Class, etc.)."),
           clEnumValN(LVPrintKind::Sizes, "sizes",
                      "Scope contributions to the debug information."),
           clEnumValN(LVPrintKind::Summary, "summary",
                      "Summary of elements missing/added/matched/printed."),
           clEnumValN(LVPrintKind::Symbols, "symbols",
                      "Symbols (Variable, Members, etc.)."),
           clEnumValN(LVPrintKind::Types, "types",
                      "Types (Pointer, Reference, etc.)."),
           clEnumValN(LVPrintKind::Warnings, "warnings",
                      "Warnings detected.")));

//===----------------------------------------------------------------------===//
// '--report' options
//===----------------------------------------------------------------------===//
cl::OptionCategory
    cmdline::ReportCategory("Report Options",
                            "These control how the elements are printed.");

// --report=<value>[,<value>,...]
cl::list<LVReportKind> cmdline::ReportOptions(
    "report", cl::cat(ReportCategory),
    cl::desc("Reports layout used for print, compare and select."), cl::Hidden,
    cl::CommaSeparated,
    values(clEnumValN(LVReportKind::All, "all", "Generate all reports."),
           clEnumValN(LVReportKind::Children, "children",
                      "Selected elements are displayed in a tree view "
                      "(Include children)"),
           clEnumValN(LVReportKind::List, "list",
                      "Selected elements are displayed in a tabular format."),
           clEnumValN(LVReportKind::Parents, "parents",
                      "Selected elements are displayed in a tree view. "
                      "(Include parents)"),
           clEnumValN(LVReportKind::View, "view",
                      "Selected elements are displayed in a tree view "
                      "(Include parents and children.")));

//===----------------------------------------------------------------------===//
// '--select' options
//===----------------------------------------------------------------------===//
cl::OptionCategory
    cmdline::SelectCategory("Select Options",
                            "These control which elements are selected.");

// --select-nocase
static cl::opt<bool, true>
    SelectIgnoreCase("select-nocase", cl::cat(SelectCategory),
                     cl::desc("Ignore case distinctions when searching."),
                     cl::Hidden, cl::ZeroOrMore,
                     cl::location(ReaderOptions.Select.IgnoreCase),
                     cl::init(false));

// --select-regex
static cl::opt<bool, true> SelectUseRegex(
    "select-regex", cl::cat(SelectCategory),
    cl::desc("Treat any <pattern> strings as regular expressions when "
             "selecting instead of just as an exact string match."),
    cl::Hidden, cl::ZeroOrMore, cl::location(ReaderOptions.Select.UseRegex),
    cl::init(false));

// --select=<pattern>
cl::list<std::string> cmdline::SelectPatterns(
    "select", cl::cat(SelectCategory),
    cl::desc("Search elements matching the given pattern."), cl::Hidden,
    cl::value_desc("pattern"), cl::CommaSeparated);

// --select-offsets=<value>[,<value>,...]
OffsetOptionList cmdline::SelectOffsets("select-offsets",
                                        cl::cat(SelectCategory),
                                        cl::desc("Offset element to print."),
                                        cl::Hidden, cl::value_desc("offset"),
                                        cl::CommaSeparated, cl::ZeroOrMore);

// --select-elements=<value>[,<value>,...]
cl::list<LVElementKind> cmdline::SelectElements(
    "select-elements", cl::cat(SelectCategory),
    cl::desc("Conditions to use when printing elements."), cl::Hidden,
    cl::CommaSeparated,
    values(clEnumValN(LVElementKind::Discarded, "Discarded",
                      "Discarded elements by the linker."),
           clEnumValN(LVElementKind::Global, "Global",
                      "Element referenced across Compile Units."),
           clEnumValN(LVElementKind::Optimized, "Optimized",
                      "Generated inlined abstract references.")));

// --select-lines=<value>[,<value>,...]
cl::list<LVLineKind> cmdline::SelectLines(
    "select-lines", cl::cat(SelectCategory),
    cl::desc("Line kind to use when printing lines."), cl::Hidden,
    cl::CommaSeparated,
    values(
        clEnumValN(LVLineKind::IsAlwaysStepInto, "AlwaysStepInto",
                   "Always Step Into."),
        clEnumValN(LVLineKind::IsBasicBlock, "BasicBlock", "Basic block."),
        clEnumValN(LVLineKind::IsDiscriminator, "Discriminator",
                   "Discriminator."),
        clEnumValN(LVLineKind::IsEndSequence, "EndSequence", "End sequence."),
        clEnumValN(LVLineKind::IsEpilogueBegin, "EpilogueBegin.",
                   "Epilogue begin."),
        clEnumValN(LVLineKind::IsLineDebug, "LineDebug", "Debug line."),
        clEnumValN(LVLineKind::IsLineAssembler, "LineAssembler",
                   "Assembler line."),
        clEnumValN(LVLineKind::IsNeverStepInto, "NeverStepInto",
                   "Never Step Into."),
        clEnumValN(LVLineKind::IsNewStatement, "NewStatement",
                   "New statement."),
        clEnumValN(LVLineKind::IsPrologueEnd, "PrologueEnd", "Prologue end.")));

// --select-scopes=<value>[,<value>,...]
cl::list<LVScopeKind> cmdline::SelectScopes(
    "select-scopes", cl::cat(SelectCategory),
    cl::desc("Scope kind to use when printing scopes."), cl::Hidden,
    cl::CommaSeparated,
    values(
        clEnumValN(LVScopeKind::IsAggregate, "Aggregate",
                   "Class, Structure or Union."),
        clEnumValN(LVScopeKind::IsArray, "Array", "Array."),
        clEnumValN(LVScopeKind::IsBlock, "Block", "Lexical block."),
        clEnumValN(LVScopeKind::IsCallSite, "CallSite", "Call site block."),
        clEnumValN(LVScopeKind::IsCatchBlock, "CatchBlock",
                   "Exception catch block."),
        clEnumValN(LVScopeKind::IsClass, "Class", "Class."),
        clEnumValN(LVScopeKind::IsCompileUnit, "CompileUnit", "Compile unit."),
        clEnumValN(LVScopeKind::IsEntryPoint, "EntryPoint",
                   "Function entry point."),
        clEnumValN(LVScopeKind::IsEnumeration, "Enumeration", "Enumeration."),
        clEnumValN(LVScopeKind::IsFunction, "Function", "Function."),
        clEnumValN(LVScopeKind::IsFunctionType, "FunctionType",
                   "Function type."),
        clEnumValN(LVScopeKind::IsInlinedFunction, "InlinedFunction",
                   "Inlined function."),
        clEnumValN(LVScopeKind::IsLabel, "Label", "Label."),
        clEnumValN(LVScopeKind::IsLexicalBlock, "LexicalBlock",
                   "Lexical block."),
        clEnumValN(LVScopeKind::IsNamespace, "Namespace", "Namespace."),
        clEnumValN(LVScopeKind::IsRoot, "Root", "Root."),
        clEnumValN(LVScopeKind::IsStructure, "Structure", "Structure."),
        clEnumValN(LVScopeKind::IsSubprogram, "Subprogram", "Subprogram."),
        clEnumValN(LVScopeKind::IsTemplate, "Template", "Template."),
        clEnumValN(LVScopeKind::IsTemplateAlias, "TemplateAlias",
                   "Template alias."),
        clEnumValN(LVScopeKind::IsTemplatePack, "TemplatePack",
                   "Template pack."),
        clEnumValN(LVScopeKind::IsTryBlock, "TryBlock", "Exception try block."),
        clEnumValN(LVScopeKind::IsUnion, "Union", "Union.")));

// --select-symbols=<value>[,<value>,...]
cl::list<LVSymbolKind> cmdline::SelectSymbols(
    "select-symbols", cl::cat(SelectCategory),
    cl::desc("Symbol kind to use when printing symbols."), cl::Hidden,
    cl::CommaSeparated,
    values(clEnumValN(LVSymbolKind::IsCallSiteParameter, "CallSiteParameter",
                      "Call site parameter."),
           clEnumValN(LVSymbolKind::IsConstant, "Constant", "Constant."),
           clEnumValN(LVSymbolKind::IsInheritance, "Inheritance",
                      "Inheritance."),
           clEnumValN(LVSymbolKind::IsMember, "Member", "Member."),
           clEnumValN(LVSymbolKind::IsParameter, "Parameter", "Parameter."),
           clEnumValN(LVSymbolKind::IsUnspecified, "Unspecified",
                      "Unspecified parameter."),
           clEnumValN(LVSymbolKind::IsVariable, "Variable", "Variable.")));

// --select-types=<value>[,<value>,...]
cl::list<LVTypeKind> cmdline::SelectTypes(
    "select-types", cl::cat(SelectCategory),
    cl::desc("Type kind to use when printing types."), cl::Hidden,
    cl::CommaSeparated,
    values(
        clEnumValN(LVTypeKind::IsBase, "Base", "Base Type (int, bool, etc.)."),
        clEnumValN(LVTypeKind::IsConst, "Const", "Constant specifier."),
        clEnumValN(LVTypeKind::IsEnumerator, "Enumerator", "Enumerator."),
        clEnumValN(LVTypeKind::IsImport, "Import", "Import."),
        clEnumValN(LVTypeKind::IsImportDeclaration, "ImportDeclaration",
                   "Import declaration."),
        clEnumValN(LVTypeKind::IsImportModule, "ImportModule",
                   "Import module."),
        clEnumValN(LVTypeKind::IsPointer, "Pointer", "Pointer."),
        clEnumValN(LVTypeKind::IsPointerMember, "PointerMember",
                   "Pointer to member."),
        clEnumValN(LVTypeKind::IsReference, "Reference", "Reference type."),
        clEnumValN(LVTypeKind::IsRestrict, "Restrict", "Restrict specifier."),
        clEnumValN(LVTypeKind::IsRvalueReference, "RvalueReference",
                   "Rvalue reference."),
        clEnumValN(LVTypeKind::IsSubrange, "Subrange", "Array subrange."),
        clEnumValN(LVTypeKind::IsTemplateParam, "TemplateParam",
                   "Template Parameter."),
        clEnumValN(LVTypeKind::IsTemplateTemplateParam, "TemplateTemplateParam",
                   "Template template parameter."),
        clEnumValN(LVTypeKind::IsTemplateTypeParam, "TemplateTypeParam",
                   "Template type parameter."),
        clEnumValN(LVTypeKind::IsTemplateValueParam, "TemplateValueParam",
                   "Template value parameter."),
        clEnumValN(LVTypeKind::IsTypedef, "Typedef", "Type definition."),
        clEnumValN(LVTypeKind::IsUnspecified, "Unspecified",
                   "Unspecified type."),
        clEnumValN(LVTypeKind::IsVolatile, "Volatile", "Volatile specifier.")));

//===----------------------------------------------------------------------===//
// '--warning' options
//===----------------------------------------------------------------------===//
cl::OptionCategory
    cmdline::WarningCategory("Warning Options",
                             "These control the generated warnings.");

// --warning=<value>[,<value>,...]
cl::list<LVWarningKind> cmdline::WarningOptions(
    "warning", cl::cat(WarningCategory), cl::desc("Warnings to generate."),
    cl::Hidden, cl::CommaSeparated,
    values(
        clEnumValN(LVWarningKind::All, "all", "All warnings."),
        clEnumValN(LVWarningKind::Coverages, "coverages",
                   "Invalid symbol coverages values."),
        clEnumValN(LVWarningKind::Lines, "lines", "Debug lines that are zero."),
        clEnumValN(LVWarningKind::Locations, "locations",
                   "Invalid symbol locations."),
        clEnumValN(LVWarningKind::Ranges, "ranges", "Invalid code ranges.")));

//===----------------------------------------------------------------------===//
// '--internal' options
//===----------------------------------------------------------------------===//
cl::OptionCategory
    cmdline::InternalCategory("Internal Options",
                              "Internal traces and extra debugging code.");

// --internal=<value>[,<value>,...]
cl::list<LVInternalKind> cmdline::InternalOptions(
    "internal", cl::cat(InternalCategory), cl::desc("Traces to enable."),
    cl::Hidden, cl::CommaSeparated,
    values(
        clEnumValN(LVInternalKind::All, "all", "Enable all traces."),
        clEnumValN(LVInternalKind::Cmdline, "cmdline", "Print command line."),
        clEnumValN(LVInternalKind::ID, "id", "Print unique element ID"),
        clEnumValN(LVInternalKind::Integrity, "integrity",
                   "Check elements integrity."),
        clEnumValN(LVInternalKind::None, "none", "Ignore element line number."),
        clEnumValN(LVInternalKind::Tag, "tag", "Debug information tags.")));

/// @}

// Copy local options into a globally accessible data structure.
void llvm::logicalview::cmdline::propagateOptions() {
  // Traverse list of options and update the given set (Using case and Regex).
  auto UpdatePattern = [&](auto &List, auto &Set, bool IgnoreCase,
                           bool UseRegex) {
    if (!List.empty())
      for (std::string &Pattern : List)
        Set.insert((IgnoreCase && !UseRegex) ? StringRef(Pattern).lower()
                                             : Pattern);
  };

  // Handle --select.
  UpdatePattern(SelectPatterns, ReaderOptions.Select.Generic,
                ReaderOptions.Select.IgnoreCase, ReaderOptions.Select.UseRegex);

  // Traverse list of options and update the given set.
  auto UpdateSet = [&](auto &List, auto &Set) {
    std::copy(List.begin(), List.end(), std::inserter(Set, Set.begin()));
  };

  // Handle options sets.
  UpdateSet(AttributeOptions, ReaderOptions.Attribute.Kinds);
  UpdateSet(PrintOptions, ReaderOptions.Print.Kinds);
  UpdateSet(OutputOptions, ReaderOptions.Output.Kinds);
  UpdateSet(ReportOptions, ReaderOptions.Report.Kinds);
  UpdateSet(WarningOptions, ReaderOptions.Warning.Kinds);
  UpdateSet(InternalOptions, ReaderOptions.Internal.Kinds);

  UpdateSet(SelectElements, ReaderOptions.Select.Elements);
  UpdateSet(SelectLines, ReaderOptions.Select.Lines);
  UpdateSet(SelectScopes, ReaderOptions.Select.Scopes);
  UpdateSet(SelectSymbols, ReaderOptions.Select.Symbols);
  UpdateSet(SelectTypes, ReaderOptions.Select.Types);
  UpdateSet(SelectOffsets, ReaderOptions.Select.Offsets);
  UpdateSet(CompareElements, ReaderOptions.Compare.Elements);

  // Resolve any options dependencies (ie. --print=all should set other
  // print options, etc.).
  ReaderOptions.resolveDependencies();
}

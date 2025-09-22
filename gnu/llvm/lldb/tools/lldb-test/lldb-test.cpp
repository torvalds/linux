//===- lldb-test.cpp ------------------------------------------ *- C++ --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "FormatUtil.h"
#include "SystemInitializerTest.h"

#include "Plugins/SymbolFile/DWARF/SymbolFileDWARF.h"
#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/Section.h"
#include "lldb/Expression/IRMemoryMap.h"
#include "lldb/Initialization/SystemLifetimeManager.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/LineTable.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/Symtab.h"
#include "lldb/Symbol/TypeList.h"
#include "lldb/Symbol/TypeMap.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/State.h"
#include "lldb/Utility/StreamString.h"

#include "llvm/ADT/IntervalMap.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/WithColor.h"

#include <cstdio>
#include <optional>
#include <thread>

using namespace lldb;
using namespace lldb_private;
using namespace llvm;

namespace opts {
static cl::SubCommand BreakpointSubcommand("breakpoints",
                                           "Test breakpoint resolution");
cl::SubCommand ObjectFileSubcommand("object-file",
                                    "Display LLDB object file information");
cl::SubCommand SymbolsSubcommand("symbols", "Dump symbols for an object file");
cl::SubCommand SymTabSubcommand("symtab",
                                "Test symbol table functionality");
cl::SubCommand IRMemoryMapSubcommand("ir-memory-map", "Test IRMemoryMap");
cl::SubCommand AssertSubcommand("assert", "Test assert handling");

cl::opt<std::string> Log("log", cl::desc("Path to a log file"), cl::init(""),
                         cl::sub(BreakpointSubcommand),
                         cl::sub(ObjectFileSubcommand),
                         cl::sub(SymbolsSubcommand),
                         cl::sub(SymTabSubcommand),
                         cl::sub(IRMemoryMapSubcommand));

/// Create a target using the file pointed to by \p Filename, or abort.
TargetSP createTarget(Debugger &Dbg, const std::string &Filename);

/// Read \p Filename into a null-terminated buffer, or abort.
std::unique_ptr<MemoryBuffer> openFile(const std::string &Filename);

namespace breakpoint {
static cl::opt<std::string> Target(cl::Positional, cl::desc("<target>"),
                                   cl::Required, cl::sub(BreakpointSubcommand));
static cl::opt<std::string> CommandFile(cl::Positional,
                                        cl::desc("<command-file>"),
                                        cl::init("-"),
                                        cl::sub(BreakpointSubcommand));
static cl::opt<bool> Persistent(
    "persistent",
    cl::desc("Don't automatically remove all breakpoints before each command"),
    cl::sub(BreakpointSubcommand));

static llvm::StringRef plural(uintmax_t value) { return value == 1 ? "" : "s"; }
static void dumpState(const BreakpointList &List, LinePrinter &P);
static std::string substitute(StringRef Cmd);
static int evaluateBreakpoints(Debugger &Dbg);
} // namespace breakpoint

namespace object {
cl::opt<bool> SectionContents("contents",
                              cl::desc("Dump each section's contents"),
                              cl::sub(ObjectFileSubcommand));
cl::opt<bool> SectionDependentModules("dep-modules",
                                      cl::desc("Dump each dependent module"),
                                      cl::sub(ObjectFileSubcommand));
cl::list<std::string> InputFilenames(cl::Positional, cl::desc("<input files>"),
                                     cl::OneOrMore,
                                     cl::sub(ObjectFileSubcommand));
} // namespace object

namespace symtab {

/// The same enum as Mangled::NamePreference but with a default
/// 'None' case. This is needed to disambiguate wheter "ManglingPreference" was
/// explicitly set or not.
enum class ManglingPreference {
  None,
  Mangled,
  Demangled,
  MangledWithoutArguments,
};

static cl::opt<std::string> FindSymbolsByRegex(
    "find-symbols-by-regex",
    cl::desc(
        "Dump symbols found in the symbol table matching the specified regex."),
    cl::sub(SymTabSubcommand));

static cl::opt<ManglingPreference> ManglingPreference(
    "mangling-preference",
    cl::desc("Preference on mangling scheme the regex should match against and "
             "dumped."),
    cl::values(
        clEnumValN(ManglingPreference::Mangled, "mangled", "Prefer mangled"),
        clEnumValN(ManglingPreference::Demangled, "demangled",
                   "Prefer demangled"),
        clEnumValN(ManglingPreference::MangledWithoutArguments,
                   "demangled-without-args", "Prefer mangled without args")),
    cl::sub(SymTabSubcommand));

static cl::opt<std::string> InputFile(cl::Positional, cl::desc("<input file>"),
                                      cl::Required, cl::sub(SymTabSubcommand));

/// Validate that the options passed make sense.
static std::optional<llvm::Error> validate();

/// Transforms the selected mangling preference into a Mangled::NamePreference
static Mangled::NamePreference getNamePreference();

static int handleSymtabCommand(Debugger &Dbg);
} // namespace symtab

namespace symbols {
static cl::opt<std::string> InputFile(cl::Positional, cl::desc("<input file>"),
                                      cl::Required, cl::sub(SymbolsSubcommand));

static cl::opt<std::string>
    SymbolPath("symbol-file",
               cl::desc("The file from which to fetch symbol information."),
               cl::value_desc("file"), cl::sub(SymbolsSubcommand));

enum class FindType {
  None,
  Function,
  Block,
  Namespace,
  Type,
  Variable,
};
static cl::opt<FindType> Find(
    "find", cl::desc("Choose search type:"),
    cl::values(
        clEnumValN(FindType::None, "none", "No search, just dump the module."),
        clEnumValN(FindType::Function, "function", "Find functions."),
        clEnumValN(FindType::Block, "block", "Find blocks."),
        clEnumValN(FindType::Namespace, "namespace", "Find namespaces."),
        clEnumValN(FindType::Type, "type", "Find types."),
        clEnumValN(FindType::Variable, "variable", "Find global variables.")),
    cl::sub(SymbolsSubcommand));

static cl::opt<std::string> Name("name", cl::desc("Name to find."),
                                 cl::sub(SymbolsSubcommand));
static cl::opt<bool>
    Regex("regex",
          cl::desc("Search using regular expressions (available for variables "
                   "and functions only)."),
          cl::sub(SymbolsSubcommand));
static cl::opt<std::string>
    Context("context",
            cl::desc("Restrict search to the context of the given variable."),
            cl::value_desc("variable"), cl::sub(SymbolsSubcommand));

static cl::opt<std::string> CompilerContext(
    "compiler-context",
    cl::desc("Specify a compiler context as \"kind:name,...\"."),
    cl::value_desc("context"), cl::sub(SymbolsSubcommand));

static cl::opt<std::string>
    Language("language", cl::desc("Specify a language type, like C99."),
             cl::value_desc("language"), cl::sub(SymbolsSubcommand));

static cl::list<FunctionNameType> FunctionNameFlags(
    "function-flags", cl::desc("Function search flags:"),
    cl::values(clEnumValN(eFunctionNameTypeAuto, "auto",
                          "Automatically deduce flags based on name."),
               clEnumValN(eFunctionNameTypeFull, "full", "Full function name."),
               clEnumValN(eFunctionNameTypeBase, "base", "Base name."),
               clEnumValN(eFunctionNameTypeMethod, "method", "Method name."),
               clEnumValN(eFunctionNameTypeSelector, "selector",
                          "Selector name.")),
    cl::sub(SymbolsSubcommand));
static FunctionNameType getFunctionNameFlags() {
  FunctionNameType Result = FunctionNameType(0);
  for (FunctionNameType Flag : FunctionNameFlags)
    Result = FunctionNameType(Result | Flag);
  return Result;
}

static cl::opt<bool> DumpAST("dump-ast",
                             cl::desc("Dump AST restored from symbols."),
                             cl::sub(SymbolsSubcommand));
static cl::opt<bool> DumpClangAST(
    "dump-clang-ast",
    cl::desc("Dump clang AST restored from symbols. When used on its own this "
             "will dump the entire AST of all loaded symbols. When combined "
             "with -find, it changes the presentation of the search results "
             "from pretty-printing the types to an AST dump."),
    cl::sub(SymbolsSubcommand));

static cl::opt<bool> Verify("verify", cl::desc("Verify symbol information."),
                            cl::sub(SymbolsSubcommand));

static cl::opt<std::string> File("file",
                                 cl::desc("File (compile unit) to search."),
                                 cl::sub(SymbolsSubcommand));
static cl::opt<int> Line("line", cl::desc("Line to search."),
                         cl::sub(SymbolsSubcommand));

static Expected<CompilerDeclContext> getDeclContext(SymbolFile &Symfile);

static Error findFunctions(lldb_private::Module &Module);
static Error findBlocks(lldb_private::Module &Module);
static Error findNamespaces(lldb_private::Module &Module);
static Error findTypes(lldb_private::Module &Module);
static Error findVariables(lldb_private::Module &Module);
static Error dumpModule(lldb_private::Module &Module);
static Error dumpAST(lldb_private::Module &Module);
static Error dumpEntireClangAST(lldb_private::Module &Module);
static Error verify(lldb_private::Module &Module);

static Expected<Error (*)(lldb_private::Module &)> getAction();
static int dumpSymbols(Debugger &Dbg);
} // namespace symbols

namespace irmemorymap {
static cl::opt<std::string> Target(cl::Positional, cl::desc("<target>"),
                                   cl::Required,
                                   cl::sub(IRMemoryMapSubcommand));
static cl::opt<std::string> CommandFile(cl::Positional,
                                        cl::desc("<command-file>"),
                                        cl::init("-"),
                                        cl::sub(IRMemoryMapSubcommand));
static cl::opt<bool> UseHostOnlyAllocationPolicy(
    "host-only", cl::desc("Use the host-only allocation policy"),
    cl::init(false), cl::sub(IRMemoryMapSubcommand));

using AllocationT = std::pair<addr_t, addr_t>;
using AddrIntervalMap =
    IntervalMap<addr_t, unsigned, 8, IntervalMapHalfOpenInfo<addr_t>>;

struct IRMemoryMapTestState {
  TargetSP Target;
  IRMemoryMap Map;

  AddrIntervalMap::Allocator IntervalMapAllocator;
  AddrIntervalMap Allocations;

  StringMap<addr_t> Label2AddrMap;

  IRMemoryMapTestState(TargetSP Target)
      : Target(Target), Map(Target), Allocations(IntervalMapAllocator) {}
};

bool evalMalloc(StringRef Line, IRMemoryMapTestState &State);
bool evalFree(StringRef Line, IRMemoryMapTestState &State);
int evaluateMemoryMapCommands(Debugger &Dbg);
} // namespace irmemorymap

namespace assert {
int lldb_assert(Debugger &Dbg);
} // namespace assert
} // namespace opts

llvm::SmallVector<CompilerContext, 4> parseCompilerContext() {
  llvm::SmallVector<CompilerContext, 4> result;
  if (opts::symbols::CompilerContext.empty())
    return result;

  StringRef str{opts::symbols::CompilerContext};
  SmallVector<StringRef, 8> entries_str;
  str.split(entries_str, ',', /*maxSplit*/-1, /*keepEmpty=*/false);
  for (auto entry_str : entries_str) {
    StringRef key, value;
    std::tie(key, value) = entry_str.split(':');
    auto kind =
        StringSwitch<CompilerContextKind>(key)
            .Case("TranslationUnit", CompilerContextKind::TranslationUnit)
            .Case("Module", CompilerContextKind::Module)
            .Case("Namespace", CompilerContextKind::Namespace)
            .Case("ClassOrStruct", CompilerContextKind::ClassOrStruct)
            .Case("Union", CompilerContextKind::Union)
            .Case("Function", CompilerContextKind::Function)
            .Case("Variable", CompilerContextKind::Variable)
            .Case("Enum", CompilerContextKind::Enum)
            .Case("Typedef", CompilerContextKind::Typedef)
            .Case("AnyModule", CompilerContextKind::AnyModule)
            .Case("AnyType", CompilerContextKind::AnyType)
            .Default(CompilerContextKind::Invalid);
    if (value.empty()) {
      WithColor::error() << "compiler context entry has no \"name\"\n";
      exit(1);
    }
    result.push_back({kind, ConstString{value}});
  }
  outs() << "Search context: {";
  lldb_private::StreamString s;
  llvm::interleaveComma(result, s, [&](auto &ctx) { ctx.Dump(s); });
  outs() << s.GetString().str() << "}\n";

  return result;
}

template <typename... Args>
static Error make_string_error(const char *Format, Args &&... args) {
  return llvm::make_error<llvm::StringError>(
      llvm::formatv(Format, std::forward<Args>(args)...).str(),
      llvm::inconvertibleErrorCode());
}

TargetSP opts::createTarget(Debugger &Dbg, const std::string &Filename) {
  TargetSP Target;
  Status ST = Dbg.GetTargetList().CreateTarget(
      Dbg, Filename, /*triple*/ "", eLoadDependentsNo,
      /*platform_options*/ nullptr, Target);
  if (ST.Fail()) {
    errs() << formatv("Failed to create target '{0}: {1}\n", Filename, ST);
    exit(1);
  }
  return Target;
}

std::unique_ptr<MemoryBuffer> opts::openFile(const std::string &Filename) {
  auto MB = MemoryBuffer::getFileOrSTDIN(Filename);
  if (!MB) {
    errs() << formatv("Could not open file '{0}: {1}\n", Filename,
                      MB.getError().message());
    exit(1);
  }
  return std::move(*MB);
}

void opts::breakpoint::dumpState(const BreakpointList &List, LinePrinter &P) {
  P.formatLine("{0} breakpoint{1}", List.GetSize(), plural(List.GetSize()));
  if (List.GetSize() > 0)
    P.formatLine("At least one breakpoint.");
  for (size_t i = 0, e = List.GetSize(); i < e; ++i) {
    BreakpointSP BP = List.GetBreakpointAtIndex(i);
    P.formatLine("Breakpoint ID {0}:", BP->GetID());
    AutoIndent Indent(P, 2);
    P.formatLine("{0} location{1}.", BP->GetNumLocations(),
                 plural(BP->GetNumLocations()));
    if (BP->GetNumLocations() > 0)
      P.formatLine("At least one location.");
    P.formatLine("{0} resolved location{1}.", BP->GetNumResolvedLocations(),
                 plural(BP->GetNumResolvedLocations()));
    if (BP->GetNumResolvedLocations() > 0)
      P.formatLine("At least one resolved location.");
    for (size_t l = 0, le = BP->GetNumLocations(); l < le; ++l) {
      BreakpointLocationSP Loc = BP->GetLocationAtIndex(l);
      P.formatLine("Location ID {0}:", Loc->GetID());
      AutoIndent Indent(P, 2);
      P.formatLine("Enabled: {0}", Loc->IsEnabled());
      P.formatLine("Resolved: {0}", Loc->IsResolved());
      SymbolContext sc;
      Loc->GetAddress().CalculateSymbolContext(&sc);
      lldb_private::StreamString S;
      sc.DumpStopContext(&S, BP->GetTarget().GetProcessSP().get(),
                         Loc->GetAddress(), false, true, false, true, true);
      P.formatLine("Address: {0}", S.GetString());
    }
  }
  P.NewLine();
}

std::string opts::breakpoint::substitute(StringRef Cmd) {
  std::string Result;
  raw_string_ostream OS(Result);
  while (!Cmd.empty()) {
    switch (Cmd[0]) {
    case '%':
      if (Cmd.consume_front("%p") && (Cmd.empty() || !isalnum(Cmd[0]))) {
        OS << sys::path::parent_path(breakpoint::CommandFile);
        break;
      }
      [[fallthrough]];
    default:
      size_t pos = Cmd.find('%');
      OS << Cmd.substr(0, pos);
      Cmd = Cmd.substr(pos);
      break;
    }
  }
  return std::move(OS.str());
}

int opts::breakpoint::evaluateBreakpoints(Debugger &Dbg) {
  TargetSP Target = opts::createTarget(Dbg, breakpoint::Target);
  std::unique_ptr<MemoryBuffer> MB = opts::openFile(breakpoint::CommandFile);

  LinePrinter P(4, outs());
  StringRef Rest = MB->getBuffer();
  int HadErrors = 0;
  while (!Rest.empty()) {
    StringRef Line;
    std::tie(Line, Rest) = Rest.split('\n');
    Line = Line.ltrim().rtrim();
    if (Line.empty() || Line[0] == '#')
      continue;

    if (!Persistent)
      Target->RemoveAllBreakpoints(/*internal_also*/ true);

    std::string Command = substitute(Line);
    P.formatLine("Command: {0}", Command);
    CommandReturnObject Result(/*colors*/ false);
    if (!Dbg.GetCommandInterpreter().HandleCommand(
            Command.c_str(), /*add_to_history*/ eLazyBoolNo, Result)) {
      P.formatLine("Failed: {0}", Result.GetErrorData());
      HadErrors = 1;
      continue;
    }

    dumpState(Target->GetBreakpointList(/*internal*/ false), P);
  }
  return HadErrors;
}

Expected<CompilerDeclContext>
opts::symbols::getDeclContext(SymbolFile &Symfile) {
  if (Context.empty())
    return CompilerDeclContext();
  VariableList List;
  Symfile.FindGlobalVariables(ConstString(Context), CompilerDeclContext(),
                              UINT32_MAX, List);
  if (List.Empty())
    return make_string_error("Context search didn't find a match.");
  if (List.GetSize() > 1)
    return make_string_error("Context search found multiple matches.");
  return List.GetVariableAtIndex(0)->GetDeclContext();
}

static lldb::DescriptionLevel GetDescriptionLevel() {
  return opts::symbols::DumpClangAST ? eDescriptionLevelVerbose : eDescriptionLevelFull;
}

Error opts::symbols::findFunctions(lldb_private::Module &Module) {
  SymbolFile &Symfile = *Module.GetSymbolFile();
  SymbolContextList List;
  auto compiler_context = parseCompilerContext();
  if (!File.empty()) {
    assert(Line != 0);

    FileSpec src_file(File);
    size_t cu_count = Module.GetNumCompileUnits();
    for (size_t i = 0; i < cu_count; i++) {
      lldb::CompUnitSP cu_sp = Module.GetCompileUnitAtIndex(i);
      if (!cu_sp)
        continue;

      LineEntry le;
      cu_sp->FindLineEntry(0, Line, &src_file, false, &le);
      if (!le.IsValid())
        continue;
      const bool include_inlined_functions = false;
      auto addr =
          le.GetSameLineContiguousAddressRange(include_inlined_functions)
              .GetBaseAddress();
      if (!addr.IsValid())
        continue;

      SymbolContext sc;
      uint32_t resolved =
          addr.CalculateSymbolContext(&sc, eSymbolContextFunction);
      if (resolved & eSymbolContextFunction)
        List.Append(sc);
    }
  } else if (Regex) {
    RegularExpression RE(Name);
    assert(RE.IsValid());
    List.Clear();
    Symfile.FindFunctions(RE, true, List);
  } else if (!compiler_context.empty()) {
    List.Clear();
    Module.FindFunctions(compiler_context, getFunctionNameFlags(), {}, List);
  } else {
    Expected<CompilerDeclContext> ContextOr = getDeclContext(Symfile);
    if (!ContextOr)
      return ContextOr.takeError();
    const CompilerDeclContext &ContextPtr =
        ContextOr->IsValid() ? *ContextOr : CompilerDeclContext();

    List.Clear();
    lldb_private::Module::LookupInfo lookup_info(
        ConstString(Name), getFunctionNameFlags(), eLanguageTypeUnknown);
    Symfile.FindFunctions(lookup_info, ContextPtr, true, List);
  }
  outs() << formatv("Found {0} functions:\n", List.GetSize());
  StreamString Stream;
  List.Dump(&Stream, nullptr);
  outs() << Stream.GetData() << "\n";
  return Error::success();
}

Error opts::symbols::findBlocks(lldb_private::Module &Module) {
  assert(!Regex);
  assert(!File.empty());
  assert(Line != 0);

  SymbolContextList List;

  FileSpec src_file(File);
  size_t cu_count = Module.GetNumCompileUnits();
  for (size_t i = 0; i < cu_count; i++) {
    lldb::CompUnitSP cu_sp = Module.GetCompileUnitAtIndex(i);
    if (!cu_sp)
      continue;

    LineEntry le;
    cu_sp->FindLineEntry(0, Line, &src_file, false, &le);
    if (!le.IsValid())
      continue;
    const bool include_inlined_functions = false;
    auto addr = le.GetSameLineContiguousAddressRange(include_inlined_functions)
                    .GetBaseAddress();
    if (!addr.IsValid())
      continue;

    SymbolContext sc;
    uint32_t resolved = addr.CalculateSymbolContext(&sc, eSymbolContextBlock);
    if (resolved & eSymbolContextBlock)
      List.Append(sc);
  }

  outs() << formatv("Found {0} blocks:\n", List.GetSize());
  StreamString Stream;
  List.Dump(&Stream, nullptr);
  outs() << Stream.GetData() << "\n";
  return Error::success();
}

Error opts::symbols::findNamespaces(lldb_private::Module &Module) {
  SymbolFile &Symfile = *Module.GetSymbolFile();
  Expected<CompilerDeclContext> ContextOr = getDeclContext(Symfile);
  if (!ContextOr)
    return ContextOr.takeError();
  const CompilerDeclContext &ContextPtr =
      ContextOr->IsValid() ? *ContextOr : CompilerDeclContext();

  CompilerDeclContext Result =
      Symfile.FindNamespace(ConstString(Name), ContextPtr);
  if (Result)
    outs() << "Found namespace: "
           << Result.GetScopeQualifiedName().GetStringRef() << "\n";
  else
    outs() << "Namespace not found.\n";
  return Error::success();
}

Error opts::symbols::findTypes(lldb_private::Module &Module) {
  SymbolFile &Symfile = *Module.GetSymbolFile();
  Expected<CompilerDeclContext> ContextOr = getDeclContext(Symfile);
  if (!ContextOr)
    return ContextOr.takeError();

  TypeResults results;
  if (!Name.empty()) {
    if (ContextOr->IsValid()) {
      TypeQuery query(*ContextOr, ConstString(Name),
                      TypeQueryOptions::e_module_search);
      if (!Language.empty())
        query.AddLanguage(Language::GetLanguageTypeFromString(Language));
      Symfile.FindTypes(query, results);
    } else {
      TypeQuery query(Name);
      if (!Language.empty())
        query.AddLanguage(Language::GetLanguageTypeFromString(Language));
      Symfile.FindTypes(query, results);
    }
  } else {
    TypeQuery query(parseCompilerContext(), TypeQueryOptions::e_module_search);
    if (!Language.empty())
      query.AddLanguage(Language::GetLanguageTypeFromString(Language));
    Symfile.FindTypes(query, results);
  }
  outs() << formatv("Found {0} types:\n", results.GetTypeMap().GetSize());
  StreamString Stream;
  // Resolve types to force-materialize typedef types.
  for (const auto &type_sp : results.GetTypeMap().Types())
    type_sp->GetFullCompilerType();
  results.GetTypeMap().Dump(&Stream, false, GetDescriptionLevel());
  outs() << Stream.GetData() << "\n";
  return Error::success();
}

Error opts::symbols::findVariables(lldb_private::Module &Module) {
  SymbolFile &Symfile = *Module.GetSymbolFile();
  VariableList List;
  if (Regex) {
    RegularExpression RE(Name);
    assert(RE.IsValid());
    Symfile.FindGlobalVariables(RE, UINT32_MAX, List);
  } else if (!File.empty()) {
    CompUnitSP CU;
    for (size_t Ind = 0; !CU && Ind < Module.GetNumCompileUnits(); ++Ind) {
      CompUnitSP Candidate = Module.GetCompileUnitAtIndex(Ind);
      if (!Candidate ||
          Candidate->GetPrimaryFile().GetFilename().GetStringRef() != File)
        continue;
      if (CU)
        return make_string_error("Multiple compile units for file `{0}` found.",
                                 File);
      CU = std::move(Candidate);
    }

    if (!CU)
      return make_string_error("Compile unit `{0}` not found.", File);

    List.AddVariables(CU->GetVariableList(true).get());
  } else {
    Expected<CompilerDeclContext> ContextOr = getDeclContext(Symfile);
    if (!ContextOr)
      return ContextOr.takeError();
    const CompilerDeclContext &ContextPtr =
        ContextOr->IsValid() ? *ContextOr : CompilerDeclContext();

    Symfile.FindGlobalVariables(ConstString(Name), ContextPtr, UINT32_MAX, List);
  }
  outs() << formatv("Found {0} variables:\n", List.GetSize());
  StreamString Stream;
  List.Dump(&Stream, false);
  outs() << Stream.GetData() << "\n";
  return Error::success();
}

Error opts::symbols::dumpModule(lldb_private::Module &Module) {
  StreamString Stream;
  Module.ParseAllDebugSymbols();
  Module.Dump(&Stream);
  outs() << Stream.GetData() << "\n";
  return Error::success();
}

Error opts::symbols::dumpAST(lldb_private::Module &Module) {
  Module.ParseAllDebugSymbols();

  SymbolFile *symfile = Module.GetSymbolFile();
  if (!symfile)
    return make_string_error("Module has no symbol file.");

  auto type_system_or_err =
      symfile->GetTypeSystemForLanguage(eLanguageTypeC_plus_plus);
  if (!type_system_or_err)
    return make_string_error("Can't retrieve TypeSystemClang");

  auto ts = *type_system_or_err;
  auto *clang_ast_ctx = llvm::dyn_cast_or_null<TypeSystemClang>(ts.get());
  if (!clang_ast_ctx)
    return make_string_error("Retrieved TypeSystem was not a TypeSystemClang");

  clang::ASTContext &ast_ctx = clang_ast_ctx->getASTContext();

  clang::TranslationUnitDecl *tu = ast_ctx.getTranslationUnitDecl();
  if (!tu)
    return make_string_error("Can't retrieve translation unit declaration.");

  tu->print(outs());

  return Error::success();
}

Error opts::symbols::dumpEntireClangAST(lldb_private::Module &Module) {
  Module.ParseAllDebugSymbols();

  SymbolFile *symfile = Module.GetSymbolFile();
  if (!symfile)
    return make_string_error("Module has no symbol file.");

  auto type_system_or_err =
      symfile->GetTypeSystemForLanguage(eLanguageTypeObjC_plus_plus);
  if (!type_system_or_err)
    return make_string_error("Can't retrieve TypeSystemClang");
  auto ts = *type_system_or_err;
  auto *clang_ast_ctx = llvm::dyn_cast_or_null<TypeSystemClang>(ts.get());
  if (!clang_ast_ctx)
    return make_string_error("Retrieved TypeSystem was not a TypeSystemClang");

  StreamString Stream;
  clang_ast_ctx->DumpFromSymbolFile(Stream, Name);
  outs() << Stream.GetData() << "\n";

  return Error::success();
}

Error opts::symbols::verify(lldb_private::Module &Module) {
  SymbolFile *symfile = Module.GetSymbolFile();
  if (!symfile)
    return make_string_error("Module has no symbol file.");

  uint32_t comp_units_count = symfile->GetNumCompileUnits();

  outs() << "Found " << comp_units_count << " compile units.\n";

  for (uint32_t i = 0; i < comp_units_count; i++) {
    lldb::CompUnitSP comp_unit = symfile->GetCompileUnitAtIndex(i);
    if (!comp_unit)
      return make_string_error("Cannot parse compile unit {0}.", i);

    outs() << "Processing '"
           << comp_unit->GetPrimaryFile().GetFilename().AsCString()
           << "' compile unit.\n";

    LineTable *lt = comp_unit->GetLineTable();
    if (!lt)
      return make_string_error("Can't get a line table of a compile unit.");

    uint32_t count = lt->GetSize();

    outs() << "The line table contains " << count << " entries.\n";

    if (count == 0)
      continue;

    LineEntry le;
    if (!lt->GetLineEntryAtIndex(0, le))
      return make_string_error("Can't get a line entry of a compile unit.");

    for (uint32_t i = 1; i < count; i++) {
      lldb::addr_t curr_end =
          le.range.GetBaseAddress().GetFileAddress() + le.range.GetByteSize();

      if (!lt->GetLineEntryAtIndex(i, le))
        return make_string_error("Can't get a line entry of a compile unit");

      if (curr_end > le.range.GetBaseAddress().GetFileAddress())
        return make_string_error(
            "Line table of a compile unit is inconsistent.");
    }
  }

  outs() << "The symbol information is verified.\n";

  return Error::success();
}

Expected<Error (*)(lldb_private::Module &)> opts::symbols::getAction() {
  if (Verify && DumpAST)
    return make_string_error(
        "Cannot both verify symbol information and dump AST.");

  if (Verify) {
    if (Find != FindType::None)
      return make_string_error(
          "Cannot both search and verify symbol information.");
    if (Regex || !Context.empty() || !Name.empty() || !File.empty() ||
        Line != 0)
      return make_string_error(
          "-regex, -context, -name, -file and -line options are not "
          "applicable for symbol verification.");
    return verify;
  }

  if (DumpAST) {
    if (Find != FindType::None)
      return make_string_error("Cannot both search and dump AST.");
    if (Regex || !Context.empty() || !Name.empty() || !File.empty() ||
        Line != 0)
      return make_string_error(
          "-regex, -context, -name, -file and -line options are not "
          "applicable for dumping AST.");
    return dumpAST;
  }

  if (DumpClangAST) {
    if (Find == FindType::None) {
      if (Regex || !Context.empty() || !File.empty() || Line != 0)
        return make_string_error(
            "-regex, -context, -name, -file and -line options are not "
            "applicable for dumping the entire clang AST. Either combine with "
            "-find, or use -dump-clang-ast as a standalone option.");
      return dumpEntireClangAST;
    }
    if (Find != FindType::Type)
      return make_string_error("This combination of -dump-clang-ast and -find "
                               "<kind> is not yet implemented.");
  }

  if (Regex && !Context.empty())
    return make_string_error(
        "Cannot search using both regular expressions and context.");

  if (Regex && !RegularExpression(Name).IsValid())
    return make_string_error("`{0}` is not a valid regular expression.", Name);

  if (Regex + !Context.empty() + !File.empty() >= 2)
    return make_string_error(
        "Only one of -regex, -context and -file may be used simultaneously.");
  if (Regex && Name.empty())
    return make_string_error("-regex used without a -name");

  switch (Find) {
  case FindType::None:
    if (!Context.empty() || !Name.empty() || !File.empty() || Line != 0)
      return make_string_error(
          "Specify search type (-find) to use search options.");
    return dumpModule;

  case FindType::Function:
    if (!File.empty() + (Line != 0) == 1)
      return make_string_error("Both file name and line number must be "
                               "specified when searching a function "
                               "by file position.");
    if (Regex + (getFunctionNameFlags() != 0) + !File.empty() >= 2)
      return make_string_error("Only one of regular expression, function-flags "
                               "and file position may be used simultaneously "
                               "when searching a function.");
    return findFunctions;

  case FindType::Block:
    if (File.empty() || Line == 0)
      return make_string_error("Both file name and line number must be "
                               "specified when searching a block.");
    if (Regex || getFunctionNameFlags() != 0)
      return make_string_error("Cannot use regular expression or "
                               "function-flags for searching a block.");
    return findBlocks;

  case FindType::Namespace:
    if (Regex || !File.empty() || Line != 0)
      return make_string_error("Cannot search for namespaces using regular "
                               "expressions, file names or line numbers.");
    return findNamespaces;

  case FindType::Type:
    if (Regex || !File.empty() || Line != 0)
      return make_string_error("Cannot search for types using regular "
                               "expressions, file names or line numbers.");
    if (!Name.empty() && !CompilerContext.empty())
      return make_string_error("Name is ignored if compiler context present.");

    return findTypes;

  case FindType::Variable:
    if (Line != 0)
      return make_string_error("Cannot search for variables "
                               "using line numbers.");
    return findVariables;
  }

  llvm_unreachable("Unsupported symbol action.");
}

std::optional<llvm::Error> opts::symtab::validate() {
  if (ManglingPreference != ManglingPreference::None &&
      FindSymbolsByRegex.empty())
    return make_string_error("Mangling preference set but no regex specified.");

  return {};
}

static Mangled::NamePreference opts::symtab::getNamePreference() {
  switch (ManglingPreference) {
  case ManglingPreference::None:
  case ManglingPreference::Mangled:
    return Mangled::ePreferMangled;
  case ManglingPreference::Demangled:
    return Mangled::ePreferDemangled;
  case ManglingPreference::MangledWithoutArguments:
    return Mangled::ePreferDemangledWithoutArguments;
  }
  llvm_unreachable("Fully covered switch above!");
}

int opts::symtab::handleSymtabCommand(Debugger &Dbg) {
  if (auto error = validate()) {
    logAllUnhandledErrors(std::move(*error), WithColor::error(), "");
    return 1;
  }

  if (!FindSymbolsByRegex.empty()) {
    ModuleSpec Spec{FileSpec(InputFile)};

    auto ModulePtr = std::make_shared<lldb_private::Module>(Spec);
    auto *Symtab = ModulePtr->GetSymtab();
    auto NamePreference = getNamePreference();
    std::vector<uint32_t> Indexes;

    Symtab->FindAllSymbolsMatchingRexExAndType(
        RegularExpression(FindSymbolsByRegex), lldb::eSymbolTypeAny,
        Symtab::eDebugAny, Symtab::eVisibilityAny, Indexes, NamePreference);
    for (auto i : Indexes) {
      auto *symbol = Symtab->SymbolAtIndex(i);
      if (symbol) {
        StreamString stream;
        symbol->Dump(&stream, nullptr, i, NamePreference);
        outs() << stream.GetString();
      }
    }
  }

  return 0;
}

int opts::symbols::dumpSymbols(Debugger &Dbg) {
  auto ActionOr = getAction();
  if (!ActionOr) {
    logAllUnhandledErrors(ActionOr.takeError(), WithColor::error(), "");
    return 1;
  }
  auto Action = *ActionOr;

  outs() << "Module: " << InputFile << "\n";
  ModuleSpec Spec{FileSpec(InputFile)};
  StringRef Symbols = SymbolPath.empty() ? InputFile : SymbolPath;
  Spec.GetSymbolFileSpec().SetFile(Symbols, FileSpec::Style::native);

  auto ModulePtr = std::make_shared<lldb_private::Module>(Spec);
  SymbolFile *Symfile = ModulePtr->GetSymbolFile();
  if (!Symfile) {
    WithColor::error() << "Module has no symbol vendor.\n";
    return 1;
  }

  if (Error E = Action(*ModulePtr)) {
    WithColor::error() << toString(std::move(E)) << "\n";
    return 1;
  }

  return 0;
}

static void dumpSectionList(LinePrinter &Printer, const SectionList &List, bool is_subsection) {
  size_t Count = List.GetNumSections(0);
  if (Count == 0) {
    Printer.formatLine("There are no {0}sections", is_subsection ? "sub" : "");
    return;
  }
  Printer.formatLine("Showing {0} {1}sections", Count,
                     is_subsection ? "sub" : "");
  for (size_t I = 0; I < Count; ++I) {
    auto S = List.GetSectionAtIndex(I);
    assert(S);
    AutoIndent Indent(Printer, 2);
    Printer.formatLine("Index: {0}", I);
    Printer.formatLine("ID: {0:x}", S->GetID());
    Printer.formatLine("Name: {0}", S->GetName().GetStringRef());
    Printer.formatLine("Type: {0}", S->GetTypeAsCString());
    Printer.formatLine("Permissions: {0}", GetPermissionsAsCString(S->GetPermissions()));
    Printer.formatLine("Thread specific: {0:y}", S->IsThreadSpecific());
    Printer.formatLine("VM address: {0:x}", S->GetFileAddress());
    Printer.formatLine("VM size: {0}", S->GetByteSize());
    Printer.formatLine("File size: {0}", S->GetFileSize());

    if (opts::object::SectionContents) {
      lldb_private::DataExtractor Data;
      S->GetSectionData(Data);
      ArrayRef<uint8_t> Bytes(Data.GetDataStart(), Data.GetDataEnd());
      Printer.formatBinary("Data: ", Bytes, 0);
    }

    if (S->GetType() == eSectionTypeContainer)
      dumpSectionList(Printer, S->GetChildren(), true);
    Printer.NewLine();
  }
}

static int dumpObjectFiles(Debugger &Dbg) {
  LinePrinter Printer(4, llvm::outs());

  int HadErrors = 0;
  for (const auto &File : opts::object::InputFilenames) {
    ModuleSpec Spec{FileSpec(File)};

    auto ModulePtr = std::make_shared<lldb_private::Module>(Spec);

    ObjectFile *ObjectPtr = ModulePtr->GetObjectFile();
    if (!ObjectPtr) {
      WithColor::error() << File << " not recognised as an object file\n";
      HadErrors = 1;
      continue;
    }

    // Fetch symbol vendor before we get the section list to give the symbol
    // vendor a chance to populate it.
    ModulePtr->GetSymbolFile();
    SectionList *Sections = ModulePtr->GetSectionList();
    if (!Sections) {
      llvm::errs() << "Could not load sections for module " << File << "\n";
      HadErrors = 1;
      continue;
    }

    Printer.formatLine("Plugin name: {0}", ObjectPtr->GetPluginName());
    Printer.formatLine("Architecture: {0}",
                       ModulePtr->GetArchitecture().GetTriple().getTriple());
    Printer.formatLine("UUID: {0}", ModulePtr->GetUUID().GetAsString());
    Printer.formatLine("Executable: {0}", ObjectPtr->IsExecutable());
    Printer.formatLine("Stripped: {0}", ObjectPtr->IsStripped());
    Printer.formatLine("Type: {0}", ObjectPtr->GetType());
    Printer.formatLine("Strata: {0}", ObjectPtr->GetStrata());
    Printer.formatLine("Base VM address: {0:x}",
                       ObjectPtr->GetBaseAddress().GetFileAddress());

    dumpSectionList(Printer, *Sections, /*is_subsection*/ false);

    if (opts::object::SectionDependentModules) {
      // A non-empty section list ensures a valid object file.
      auto Obj = ModulePtr->GetObjectFile();
      FileSpecList Files;
      auto Count = Obj->GetDependentModules(Files);
      Printer.formatLine("Showing {0} dependent module(s)", Count);
      for (size_t I = 0; I < Files.GetSize(); ++I) {
        AutoIndent Indent(Printer, 2);
        Printer.formatLine("Name: {0}",
                           Files.GetFileSpecAtIndex(I).GetPath());
      }
      Printer.NewLine();
    }
  }
  return HadErrors;
}

bool opts::irmemorymap::evalMalloc(StringRef Line,
                                   IRMemoryMapTestState &State) {
  // ::= <label> = malloc <size> <alignment>
  StringRef Label;
  std::tie(Label, Line) = Line.split('=');
  if (Line.empty())
    return false;
  Label = Label.trim();
  Line = Line.trim();
  size_t Size;
  uint8_t Alignment;
  int Matches = sscanf(Line.data(), "malloc %zu %hhu", &Size, &Alignment);
  if (Matches != 2)
    return false;

  outs() << formatv("Command: {0} = malloc(size={1}, alignment={2})\n", Label,
                    Size, Alignment);
  if (!isPowerOf2_32(Alignment)) {
    outs() << "Malloc error: alignment is not a power of 2\n";
    exit(1);
  }

  IRMemoryMap::AllocationPolicy AP =
      UseHostOnlyAllocationPolicy ? IRMemoryMap::eAllocationPolicyHostOnly
                                  : IRMemoryMap::eAllocationPolicyProcessOnly;

  // Issue the malloc in the target process with "-rw" permissions.
  const uint32_t Permissions = 0x3;
  const bool ZeroMemory = false;
  Status ST;
  addr_t Addr =
      State.Map.Malloc(Size, Alignment, Permissions, AP, ZeroMemory, ST);
  if (ST.Fail()) {
    outs() << formatv("Malloc error: {0}\n", ST);
    return true;
  }

  // Print the result of the allocation before checking its validity.
  outs() << formatv("Malloc: address = {0:x}\n", Addr);

  // Check that the allocation is aligned.
  if (!Addr || Addr % Alignment != 0) {
    outs() << "Malloc error: zero or unaligned allocation detected\n";
    exit(1);
  }

  // In case of Size == 0, we still expect the returned address to be unique and
  // non-overlapping.
  addr_t EndOfRegion = Addr + std::max<size_t>(Size, 1);
  if (State.Allocations.overlaps(Addr, EndOfRegion)) {
    auto I = State.Allocations.find(Addr);
    outs() << "Malloc error: overlapping allocation detected"
           << formatv(", previous allocation at [{0:x}, {1:x})\n", I.start(),
                      I.stop());
    exit(1);
  }

  // Insert the new allocation into the interval map. Use unique allocation
  // IDs to inhibit interval coalescing.
  static unsigned AllocationID = 0;
  State.Allocations.insert(Addr, EndOfRegion, AllocationID++);

  // Store the label -> address mapping.
  State.Label2AddrMap[Label] = Addr;

  return true;
}

bool opts::irmemorymap::evalFree(StringRef Line, IRMemoryMapTestState &State) {
  // ::= free <label>
  if (!Line.consume_front("free"))
    return false;
  StringRef Label = Line.trim();

  outs() << formatv("Command: free({0})\n", Label);
  auto LabelIt = State.Label2AddrMap.find(Label);
  if (LabelIt == State.Label2AddrMap.end()) {
    outs() << "Free error: Invalid allocation label\n";
    exit(1);
  }

  Status ST;
  addr_t Addr = LabelIt->getValue();
  State.Map.Free(Addr, ST);
  if (ST.Fail()) {
    outs() << formatv("Free error: {0}\n", ST);
    exit(1);
  }

  // Erase the allocation from the live interval map.
  auto Interval = State.Allocations.find(Addr);
  if (Interval != State.Allocations.end()) {
    outs() << formatv("Free: [{0:x}, {1:x})\n", Interval.start(),
                      Interval.stop());
    Interval.erase();
  }

  return true;
}

int opts::irmemorymap::evaluateMemoryMapCommands(Debugger &Dbg) {
  // Set up a Target.
  TargetSP Target = opts::createTarget(Dbg, irmemorymap::Target);

  // Set up a Process. In order to allocate memory within a target, this
  // process must be alive and must support JIT'ing.
  CommandReturnObject Result(/*colors*/ false);
  Dbg.SetAsyncExecution(false);
  CommandInterpreter &CI = Dbg.GetCommandInterpreter();
  auto IssueCmd = [&](const char *Cmd) -> bool {
    return CI.HandleCommand(Cmd, eLazyBoolNo, Result);
  };
  if (!IssueCmd("b main") || !IssueCmd("run")) {
    outs() << formatv("Failed: {0}\n", Result.GetErrorData());
    exit(1);
  }

  ProcessSP Process = Target->GetProcessSP();
  if (!Process || !Process->IsAlive() || !Process->CanJIT()) {
    outs() << "Cannot use process to test IRMemoryMap\n";
    exit(1);
  }

  // Set up an IRMemoryMap and associated testing state.
  IRMemoryMapTestState State(Target);

  // Parse and apply commands from the command file.
  std::unique_ptr<MemoryBuffer> MB = opts::openFile(irmemorymap::CommandFile);
  StringRef Rest = MB->getBuffer();
  while (!Rest.empty()) {
    StringRef Line;
    std::tie(Line, Rest) = Rest.split('\n');
    Line = Line.ltrim().rtrim();

    if (Line.empty() || Line[0] == '#')
      continue;

    if (evalMalloc(Line, State))
      continue;

    if (evalFree(Line, State))
      continue;

    errs() << "Could not parse line: " << Line << "\n";
    exit(1);
  }
  return 0;
}

int opts::assert::lldb_assert(Debugger &Dbg) {
  lldbassert(false && "lldb-test assert");
  return 1;
}

int main(int argc, const char *argv[]) {
  StringRef ToolName = argv[0];
  sys::PrintStackTraceOnErrorSignal(ToolName);
  PrettyStackTraceProgram X(argc, argv);
  llvm_shutdown_obj Y;

  cl::ParseCommandLineOptions(argc, argv, "LLDB Testing Utility\n");

  SystemLifetimeManager DebuggerLifetime;
  if (auto e = DebuggerLifetime.Initialize(
          std::make_unique<SystemInitializerTest>(), nullptr)) {
    WithColor::error() << "initialization failed: " << toString(std::move(e))
                       << '\n';
    return 1;
  }

  auto TerminateDebugger =
      llvm::make_scope_exit([&] { DebuggerLifetime.Terminate(); });

  auto Dbg = lldb_private::Debugger::CreateInstance();
  ModuleList::GetGlobalModuleListProperties().SetEnableExternalLookup(false);
  CommandReturnObject Result(/*colors*/ false);
  Dbg->GetCommandInterpreter().HandleCommand(
      "settings set plugin.process.gdb-remote.packet-timeout 60",
      /*add_to_history*/ eLazyBoolNo, Result);
  Dbg->GetCommandInterpreter().HandleCommand(
      "settings set target.inherit-tcc true",
      /*add_to_history*/ eLazyBoolNo, Result);
  Dbg->GetCommandInterpreter().HandleCommand(
      "settings set target.detach-on-error false",
      /*add_to_history*/ eLazyBoolNo, Result);

  if (!opts::Log.empty())
    Dbg->EnableLog("lldb", {"all"}, opts::Log, 0, 0, eLogHandlerStream, errs());

  if (opts::BreakpointSubcommand)
    return opts::breakpoint::evaluateBreakpoints(*Dbg);
  if (opts::ObjectFileSubcommand)
    return dumpObjectFiles(*Dbg);
  if (opts::SymbolsSubcommand)
    return opts::symbols::dumpSymbols(*Dbg);
  if (opts::SymTabSubcommand)
    return opts::symtab::handleSymtabCommand(*Dbg);
  if (opts::IRMemoryMapSubcommand)
    return opts::irmemorymap::evaluateMemoryMapCommands(*Dbg);
  if (opts::AssertSubcommand)
    return opts::assert::lldb_assert(*Dbg);

  WithColor::error() << "No command specified.\n";
  return 1;
}

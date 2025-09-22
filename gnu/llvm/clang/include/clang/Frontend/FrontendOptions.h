//===- FrontendOptions.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_FRONTENDOPTIONS_H
#define LLVM_CLANG_FRONTEND_FRONTENDOPTIONS_H

#include "clang/AST/ASTDumperUtils.h"
#include "clang/Basic/LangStandard.h"
#include "clang/Frontend/CommandLineSourceLoc.h"
#include "clang/Sema/CodeCompleteOptions.h"
#include "clang/Serialization/ModuleFileExtension.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MemoryBuffer.h"
#include <cassert>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace llvm {

class MemoryBuffer;

} // namespace llvm

namespace clang {

namespace frontend {

enum ActionKind {
  /// Parse ASTs and list Decl nodes.
  ASTDeclList,

  /// Parse ASTs and dump them.
  ASTDump,

  /// Parse ASTs and print them.
  ASTPrint,

  /// Parse ASTs and view them in Graphviz.
  ASTView,

  /// Dump the compiler configuration.
  DumpCompilerOptions,

  /// Dump out raw tokens.
  DumpRawTokens,

  /// Dump out preprocessed tokens.
  DumpTokens,

  /// Emit a .s file.
  EmitAssembly,

  /// Emit a .bc file.
  EmitBC,

  /// Translate input source into HTML.
  EmitHTML,

  /// Emit a .cir file
  EmitCIR,

  /// Emit a .ll file.
  EmitLLVM,

  /// Generate LLVM IR, but do not emit anything.
  EmitLLVMOnly,

  /// Generate machine code, but don't emit anything.
  EmitCodeGenOnly,

  /// Emit a .o file.
  EmitObj,

  // Extract API information
  ExtractAPI,

  /// Parse and apply any fixits to the source.
  FixIt,

  /// Generate pre-compiled module from a module map.
  GenerateModule,

  /// Generate pre-compiled module from a standard C++ module interface unit.
  GenerateModuleInterface,

  /// Generate reduced module interface for a standard C++ module interface
  /// unit.
  GenerateReducedModuleInterface,

  /// Generate a C++20 header unit module from a header file.
  GenerateHeaderUnit,

  /// Generate pre-compiled header.
  GeneratePCH,

  /// Generate Interface Stub Files.
  GenerateInterfaceStubs,

  /// Only execute frontend initialization.
  InitOnly,

  /// Dump information about a module file.
  ModuleFileInfo,

  /// Load and verify that a PCH file is usable.
  VerifyPCH,

  /// Parse and perform semantic analysis.
  ParseSyntaxOnly,

  /// Run a plugin action, \see ActionName.
  PluginAction,

  /// Print the "preamble" of the input file
  PrintPreamble,

  /// -E mode.
  PrintPreprocessedInput,

  /// Expand macros but not \#includes.
  RewriteMacros,

  /// ObjC->C Rewriter.
  RewriteObjC,

  /// Rewriter playground
  RewriteTest,

  /// Run one or more source code analyses.
  RunAnalysis,

  /// Dump template instantiations
  TemplightDump,

  /// Run migrator.
  MigrateSource,

  /// Just lex, no output.
  RunPreprocessorOnly,

  /// Print the output of the dependency directives source minimizer.
  PrintDependencyDirectivesSourceMinimizerOutput
};

} // namespace frontend

/// The kind of a file that we've been handed as an input.
class InputKind {
public:
  /// The input file format.
  enum Format {
    Source,
    ModuleMap,
    Precompiled
  };

  // If we are building a header unit, what kind it is; this affects whether
  // we look for the file in the user or system include search paths before
  // flagging a missing input.
  enum HeaderUnitKind {
    HeaderUnit_None,
    HeaderUnit_User,
    HeaderUnit_System,
    HeaderUnit_Abs
  };

private:
  Language Lang;
  LLVM_PREFERRED_TYPE(Format)
  unsigned Fmt : 3;
  LLVM_PREFERRED_TYPE(bool)
  unsigned Preprocessed : 1;
  LLVM_PREFERRED_TYPE(HeaderUnitKind)
  unsigned HeaderUnit : 3;
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsHeader : 1;

public:
  constexpr InputKind(Language L = Language::Unknown, Format F = Source,
                      bool PP = false, HeaderUnitKind HU = HeaderUnit_None,
                      bool HD = false)
      : Lang(L), Fmt(F), Preprocessed(PP), HeaderUnit(HU), IsHeader(HD) {}

  Language getLanguage() const { return static_cast<Language>(Lang); }
  Format getFormat() const { return static_cast<Format>(Fmt); }
  HeaderUnitKind getHeaderUnitKind() const {
    return static_cast<HeaderUnitKind>(HeaderUnit);
  }
  bool isPreprocessed() const { return Preprocessed; }
  bool isHeader() const { return IsHeader; }
  bool isHeaderUnit() const { return HeaderUnit != HeaderUnit_None; }

  /// Is the input kind fully-unknown?
  bool isUnknown() const { return Lang == Language::Unknown && Fmt == Source; }

  /// Is the language of the input some dialect of Objective-C?
  bool isObjectiveC() const {
    return Lang == Language::ObjC || Lang == Language::ObjCXX;
  }

  InputKind getPreprocessed() const {
    return InputKind(getLanguage(), getFormat(), true, getHeaderUnitKind(),
                     isHeader());
  }

  InputKind getHeader() const {
    return InputKind(getLanguage(), getFormat(), isPreprocessed(),
                     getHeaderUnitKind(), true);
  }

  InputKind withHeaderUnit(HeaderUnitKind HU) const {
    return InputKind(getLanguage(), getFormat(), isPreprocessed(), HU,
                     isHeader());
  }

  InputKind withFormat(Format F) const {
    return InputKind(getLanguage(), F, isPreprocessed(), getHeaderUnitKind(),
                     isHeader());
  }
};

/// An input file for the front end.
class FrontendInputFile {
  /// The file name, or "-" to read from standard input.
  std::string File;

  /// The input, if it comes from a buffer rather than a file. This object
  /// does not own the buffer, and the caller is responsible for ensuring
  /// that it outlives any users.
  std::optional<llvm::MemoryBufferRef> Buffer;

  /// The kind of input, e.g., C source, AST file, LLVM IR.
  InputKind Kind;

  /// Whether we're dealing with a 'system' input (vs. a 'user' input).
  bool IsSystem = false;

public:
  FrontendInputFile() = default;
  FrontendInputFile(StringRef File, InputKind Kind, bool IsSystem = false)
      : File(File.str()), Kind(Kind), IsSystem(IsSystem) {}
  FrontendInputFile(llvm::MemoryBufferRef Buffer, InputKind Kind,
                    bool IsSystem = false)
      : Buffer(Buffer), Kind(Kind), IsSystem(IsSystem) {}

  InputKind getKind() const { return Kind; }
  bool isSystem() const { return IsSystem; }

  bool isEmpty() const { return File.empty() && Buffer == std::nullopt; }
  bool isFile() const { return !isBuffer(); }
  bool isBuffer() const { return Buffer != std::nullopt; }
  bool isPreprocessed() const { return Kind.isPreprocessed(); }
  bool isHeader() const { return Kind.isHeader(); }
  InputKind::HeaderUnitKind getHeaderUnitKind() const {
    return Kind.getHeaderUnitKind();
  }

  StringRef getFile() const {
    assert(isFile());
    return File;
  }

  llvm::MemoryBufferRef getBuffer() const {
    assert(isBuffer());
    return *Buffer;
  }
};

/// FrontendOptions - Options for controlling the behavior of the frontend.
class FrontendOptions {
public:
  /// Disable memory freeing on exit.
  LLVM_PREFERRED_TYPE(bool)
  unsigned DisableFree : 1;

  /// When generating PCH files, instruct the AST writer to create relocatable
  /// PCH files.
  LLVM_PREFERRED_TYPE(bool)
  unsigned RelocatablePCH : 1;

  /// Show the -help text.
  LLVM_PREFERRED_TYPE(bool)
  unsigned ShowHelp : 1;

  /// Show frontend performance metrics and statistics.
  LLVM_PREFERRED_TYPE(bool)
  unsigned ShowStats : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned AppendStats : 1;

  /// print the supported cpus for the current target
  LLVM_PREFERRED_TYPE(bool)
  unsigned PrintSupportedCPUs : 1;

  /// Print the supported extensions for the current target.
  LLVM_PREFERRED_TYPE(bool)
  unsigned PrintSupportedExtensions : 1;

  /// Print the extensions enabled for the current target.
  LLVM_PREFERRED_TYPE(bool)
  unsigned PrintEnabledExtensions : 1;

  /// Show the -version text.
  LLVM_PREFERRED_TYPE(bool)
  unsigned ShowVersion : 1;

  /// Apply fixes even if there are unfixable errors.
  LLVM_PREFERRED_TYPE(bool)
  unsigned FixWhatYouCan : 1;

  /// Apply fixes only for warnings.
  LLVM_PREFERRED_TYPE(bool)
  unsigned FixOnlyWarnings : 1;

  /// Apply fixes and recompile.
  LLVM_PREFERRED_TYPE(bool)
  unsigned FixAndRecompile : 1;

  /// Apply fixes to temporary files.
  LLVM_PREFERRED_TYPE(bool)
  unsigned FixToTemporaries : 1;

  /// Emit ARC errors even if the migrator can fix them.
  LLVM_PREFERRED_TYPE(bool)
  unsigned ARCMTMigrateEmitARCErrors : 1;

  /// Skip over function bodies to speed up parsing in cases you do not need
  /// them (e.g. with code completion).
  LLVM_PREFERRED_TYPE(bool)
  unsigned SkipFunctionBodies : 1;

  /// Whether we can use the global module index if available.
  LLVM_PREFERRED_TYPE(bool)
  unsigned UseGlobalModuleIndex : 1;

  /// Whether we can generate the global module index if needed.
  LLVM_PREFERRED_TYPE(bool)
  unsigned GenerateGlobalModuleIndex : 1;

  /// Whether we include declaration dumps in AST dumps.
  LLVM_PREFERRED_TYPE(bool)
  unsigned ASTDumpDecls : 1;

  /// Whether we deserialize all decls when forming AST dumps.
  LLVM_PREFERRED_TYPE(bool)
  unsigned ASTDumpAll : 1;

  /// Whether we include lookup table dumps in AST dumps.
  LLVM_PREFERRED_TYPE(bool)
  unsigned ASTDumpLookups : 1;

  /// Whether we include declaration type dumps in AST dumps.
  LLVM_PREFERRED_TYPE(bool)
  unsigned ASTDumpDeclTypes : 1;

  /// Whether we are performing an implicit module build.
  LLVM_PREFERRED_TYPE(bool)
  unsigned BuildingImplicitModule : 1;

  /// Whether to use a filesystem lock when building implicit modules.
  LLVM_PREFERRED_TYPE(bool)
  unsigned BuildingImplicitModuleUsesLock : 1;

  /// Whether we should embed all used files into the PCM file.
  LLVM_PREFERRED_TYPE(bool)
  unsigned ModulesEmbedAllFiles : 1;

  /// Whether timestamps should be written to the produced PCH file.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IncludeTimestamps : 1;

  /// Should a temporary file be used during compilation.
  LLVM_PREFERRED_TYPE(bool)
  unsigned UseTemporary : 1;

  /// When using -emit-module, treat the modulemap as a system module.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsSystemModule : 1;

  /// Output (and read) PCM files regardless of compiler errors.
  LLVM_PREFERRED_TYPE(bool)
  unsigned AllowPCMWithCompilerErrors : 1;

  /// Whether to share the FileManager when building modules.
  LLVM_PREFERRED_TYPE(bool)
  unsigned ModulesShareFileManager : 1;

  /// Whether to emit symbol graph files as a side effect of compilation.
  LLVM_PREFERRED_TYPE(bool)
  unsigned EmitSymbolGraph : 1;

  /// Whether to emit additional symbol graphs for extended modules.
  LLVM_PREFERRED_TYPE(bool)
  unsigned EmitExtensionSymbolGraphs : 1;

  /// Whether to emit symbol labels for testing in generated symbol graphs
  LLVM_PREFERRED_TYPE(bool)
  unsigned EmitSymbolGraphSymbolLabelsForTesting : 1;

  /// Whether to emit symbol labels for testing in generated symbol graphs
  LLVM_PREFERRED_TYPE(bool)
  unsigned EmitPrettySymbolGraphs : 1;

  /// Whether to generate reduced BMI for C++20 named modules.
  LLVM_PREFERRED_TYPE(bool)
  unsigned GenReducedBMI : 1;

  /// Use Clang IR pipeline to emit code
  LLVM_PREFERRED_TYPE(bool)
  unsigned UseClangIRPipeline : 1;

  CodeCompleteOptions CodeCompleteOpts;

  /// Specifies the output format of the AST.
  ASTDumpOutputFormat ASTDumpFormat = ADOF_Default;

  enum {
    ARCMT_None,
    ARCMT_Check,
    ARCMT_Modify,
    ARCMT_Migrate
  } ARCMTAction = ARCMT_None;

  enum {
    ObjCMT_None = 0,

    /// Enable migration to modern ObjC literals.
    ObjCMT_Literals = 0x1,

    /// Enable migration to modern ObjC subscripting.
    ObjCMT_Subscripting = 0x2,

    /// Enable migration to modern ObjC readonly property.
    ObjCMT_ReadonlyProperty = 0x4,

    /// Enable migration to modern ObjC readwrite property.
    ObjCMT_ReadwriteProperty = 0x8,

    /// Enable migration to modern ObjC property.
    ObjCMT_Property = (ObjCMT_ReadonlyProperty | ObjCMT_ReadwriteProperty),

    /// Enable annotation of ObjCMethods of all kinds.
    ObjCMT_Annotation = 0x10,

    /// Enable migration of ObjC methods to 'instancetype'.
    ObjCMT_Instancetype = 0x20,

    /// Enable migration to NS_ENUM/NS_OPTIONS macros.
    ObjCMT_NsMacros = 0x40,

    /// Enable migration to add conforming protocols.
    ObjCMT_ProtocolConformance = 0x80,

    /// prefer 'atomic' property over 'nonatomic'.
    ObjCMT_AtomicProperty = 0x100,

    /// annotate property with NS_RETURNS_INNER_POINTER
    ObjCMT_ReturnsInnerPointerProperty = 0x200,

    /// use NS_NONATOMIC_IOSONLY for property 'atomic' attribute
    ObjCMT_NsAtomicIOSOnlyProperty = 0x400,

    /// Enable inferring NS_DESIGNATED_INITIALIZER for ObjC methods.
    ObjCMT_DesignatedInitializer = 0x800,

    /// Enable converting setter/getter expressions to property-dot syntx.
    ObjCMT_PropertyDotSyntax = 0x1000,

    ObjCMT_MigrateDecls = (ObjCMT_ReadonlyProperty | ObjCMT_ReadwriteProperty |
                           ObjCMT_Annotation | ObjCMT_Instancetype |
                           ObjCMT_NsMacros | ObjCMT_ProtocolConformance |
                           ObjCMT_NsAtomicIOSOnlyProperty |
                           ObjCMT_DesignatedInitializer),
    ObjCMT_MigrateAll = (ObjCMT_Literals | ObjCMT_Subscripting |
                         ObjCMT_MigrateDecls | ObjCMT_PropertyDotSyntax)
  };
  unsigned ObjCMTAction = ObjCMT_None;
  std::string ObjCMTAllowListPath;

  std::string MTMigrateDir;
  std::string ARCMTMigrateReportOut;

  /// The input kind, either specified via -x argument or deduced from the input
  /// file name.
  InputKind DashX;

  /// The input files and their types.
  SmallVector<FrontendInputFile, 0> Inputs;

  /// When the input is a module map, the original module map file from which
  /// that map was inferred, if any (for umbrella modules).
  std::string OriginalModuleMap;

  /// The output file, if any.
  std::string OutputFile;

  /// If given, the new suffix for fix-it rewritten files.
  std::string FixItSuffix;

  /// If given, filter dumped AST Decl nodes by this substring.
  std::string ASTDumpFilter;

  /// If given, enable code completion at the provided location.
  ParsedSourceLocation CodeCompletionAt;

  /// The frontend action to perform.
  frontend::ActionKind ProgramAction = frontend::ParseSyntaxOnly;

  /// The name of the action to run when using a plugin action.
  std::string ActionName;

  // Currently this is only used as part of the `-extract-api` action.
  /// The name of the product the input files belong too.
  std::string ProductName;

  // Currently this is only used as part of the `-extract-api` action.
  // A comma separated list of files providing a list of APIs to
  // ignore when extracting documentation.
  std::vector<std::string> ExtractAPIIgnoresFileList;

  // Location of output directory where symbol graph information would
  // be dumped. This overrides regular -o output file specification
  std::string SymbolGraphOutputDir;

  /// Args to pass to the plugins
  std::map<std::string, std::vector<std::string>> PluginArgs;

  /// The list of plugin actions to run in addition to the normal action.
  std::vector<std::string> AddPluginActions;

  /// The list of plugins to load.
  std::vector<std::string> Plugins;

  /// The list of module file extensions.
  std::vector<std::shared_ptr<ModuleFileExtension>> ModuleFileExtensions;

  /// The list of module map files to load before processing the input.
  std::vector<std::string> ModuleMapFiles;

  /// The list of additional prebuilt module files to load before
  /// processing the input.
  std::vector<std::string> ModuleFiles;

  /// The list of files to embed into the compiled module file.
  std::vector<std::string> ModulesEmbedFiles;

  /// The list of AST files to merge.
  std::vector<std::string> ASTMergeFiles;

  /// A list of arguments to forward to LLVM's option processing; this
  /// should only be used for debugging and experimental features.
  std::vector<std::string> LLVMArgs;

  /// File name of the file that will provide record layouts
  /// (in the format produced by -fdump-record-layouts).
  std::string OverrideRecordLayoutsFile;

  /// Auxiliary triple for CUDA/HIP compilation.
  std::string AuxTriple;

  /// Auxiliary target CPU for CUDA/HIP compilation.
  std::optional<std::string> AuxTargetCPU;

  /// Auxiliary target features for CUDA/HIP compilation.
  std::optional<std::vector<std::string>> AuxTargetFeatures;

  /// Filename to write statistics to.
  std::string StatsFile;

  /// Minimum time granularity (in microseconds) traced by time profiler.
  unsigned TimeTraceGranularity;

  /// Make time trace capture verbose event details (e.g. source filenames).
  /// This can increase the size of the output by 2-3 times.
  LLVM_PREFERRED_TYPE(bool)
  unsigned TimeTraceVerbose : 1;

  /// Path which stores the output files for -ftime-trace
  std::string TimeTracePath;

  /// Output Path for module output file.
  std::string ModuleOutputPath;

public:
  FrontendOptions()
      : DisableFree(false), RelocatablePCH(false), ShowHelp(false),
        ShowStats(false), AppendStats(false), ShowVersion(false),
        FixWhatYouCan(false), FixOnlyWarnings(false), FixAndRecompile(false),
        FixToTemporaries(false), ARCMTMigrateEmitARCErrors(false),
        SkipFunctionBodies(false), UseGlobalModuleIndex(true),
        GenerateGlobalModuleIndex(true), ASTDumpDecls(false),
        ASTDumpLookups(false), BuildingImplicitModule(false),
        BuildingImplicitModuleUsesLock(true), ModulesEmbedAllFiles(false),
        IncludeTimestamps(true), UseTemporary(true),
        AllowPCMWithCompilerErrors(false), ModulesShareFileManager(true),
        EmitSymbolGraph(false), EmitExtensionSymbolGraphs(false),
        EmitSymbolGraphSymbolLabelsForTesting(false),
        EmitPrettySymbolGraphs(false), GenReducedBMI(false),
        UseClangIRPipeline(false), TimeTraceGranularity(500),
        TimeTraceVerbose(false) {}

  /// getInputKindForExtension - Return the appropriate input kind for a file
  /// extension. For example, "c" would return Language::C.
  ///
  /// \return The input kind for the extension, or Language::Unknown if the
  /// extension is not recognized.
  static InputKind getInputKindForExtension(StringRef Extension);
};

} // namespace clang

#endif // LLVM_CLANG_FRONTEND_FRONTENDOPTIONS_H

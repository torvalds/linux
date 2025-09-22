//===- PreprocessorOptions.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LEX_PREPROCESSOROPTIONS_H_
#define LLVM_CLANG_LEX_PREPROCESSOROPTIONS_H_

#include "clang/Basic/BitmaskEnum.h"
#include "clang/Basic/FileEntry.h"
#include "clang/Basic/LLVM.h"
#include "clang/Lex/DependencyDirectivesScanner.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

class MemoryBuffer;

} // namespace llvm

namespace clang {

/// Enumerate the kinds of standard library that
enum ObjCXXARCStandardLibraryKind {
  ARCXX_nolib,

  /// libc++
  ARCXX_libcxx,

  /// libstdc++
  ARCXX_libstdcxx
};

/// Whether to disable the normal validation performed on precompiled
/// headers and module files when they are loaded.
enum class DisableValidationForModuleKind {
  /// Perform validation, don't disable it.
  None = 0,

  /// Disable validation for a precompiled header and the modules it depends on.
  PCH = 0x1,

  /// Disable validation for module files.
  Module = 0x2,

  /// Disable validation for all kinds.
  All = PCH | Module,

  LLVM_MARK_AS_BITMASK_ENUM(Module)
};

/// PreprocessorOptions - This class is used for passing the various options
/// used in preprocessor initialization to InitializePreprocessor().
class PreprocessorOptions {
public:
  std::vector<std::pair<std::string, bool/*isUndef*/>> Macros;
  std::vector<std::string> Includes;
  std::vector<std::string> MacroIncludes;

  /// Perform extra checks when loading PCM files for mutable file systems.
  bool ModulesCheckRelocated = true;

  /// Initialize the preprocessor with the compiler and target specific
  /// predefines.
  bool UsePredefines = true;

  /// Indicates whether to predefine target OS macros.
  bool DefineTargetOSMacros = false;

  /// Whether we should maintain a detailed record of all macro
  /// definitions and expansions.
  bool DetailedRecord = false;

  /// When true, we are creating or using a PCH where a #pragma hdrstop is
  /// expected to indicate the beginning or end of the PCH.
  bool PCHWithHdrStop = false;

  /// When true, we are creating a PCH or creating the PCH object while
  /// expecting a #pragma hdrstop to separate the two.  Allow for a
  /// missing #pragma hdrstop, which generates a PCH for the whole file,
  /// and creates an empty PCH object.
  bool PCHWithHdrStopCreate = false;

  /// If non-empty, the filename used in an #include directive in the primary
  /// source file (or command-line preinclude) that is used to implement
  /// MSVC-style precompiled headers. When creating a PCH, after the #include
  /// of this header, the PCH generation stops. When using a PCH, tokens are
  /// skipped until after an #include of this header is seen.
  std::string PCHThroughHeader;

  /// The implicit PCH included at the start of the translation unit, or empty.
  std::string ImplicitPCHInclude;

  /// Headers that will be converted to chained PCHs in memory.
  std::vector<std::string> ChainedIncludes;

  /// Whether to disable most of the normal validation performed on
  /// precompiled headers and module files.
  DisableValidationForModuleKind DisablePCHOrModuleValidation =
      DisableValidationForModuleKind::None;

  /// When true, a PCH with compiler errors will not be rejected.
  bool AllowPCHWithCompilerErrors = false;

  /// When true, a PCH with modules cache path different to the current
  /// compilation will not be rejected.
  bool AllowPCHWithDifferentModulesCachePath = false;

  /// Dump declarations that are deserialized from PCH, for testing.
  bool DumpDeserializedPCHDecls = false;

  /// This is a set of names for decls that we do not want to be
  /// deserialized, and we emit an error if they are; for testing purposes.
  std::set<std::string> DeserializedPCHDeclsToErrorOn;

  /// If non-zero, the implicit PCH include is actually a precompiled
  /// preamble that covers this number of bytes in the main source file.
  ///
  /// The boolean indicates whether the preamble ends at the start of a new
  /// line.
  std::pair<unsigned, bool> PrecompiledPreambleBytes;

  /// True indicates that a preamble is being generated.
  ///
  /// When the lexer is done, one of the things that need to be preserved is the
  /// conditional #if stack, so the ASTWriter/ASTReader can save/restore it when
  /// processing the rest of the file. Similarly, we track an unterminated
  /// #pragma assume_nonnull.
  bool GeneratePreamble = false;

  /// Whether to write comment locations into the PCH when building it.
  /// Reading the comments from the PCH can be a performance hit even if the
  /// clients don't use them.
  bool WriteCommentListToPCH = true;

  /// When enabled, preprocessor is in a mode for parsing a single file only.
  ///
  /// Disables #includes of other files and if there are unresolved identifiers
  /// in preprocessor directive conditions it causes all blocks to be parsed so
  /// that the client can get the maximum amount of information from the parser.
  bool SingleFileParseMode = false;

  /// When enabled, the preprocessor will construct editor placeholder tokens.
  bool LexEditorPlaceholders = true;

  /// True if the SourceManager should report the original file name for
  /// contents of files that were remapped to other files. Defaults to true.
  bool RemappedFilesKeepOriginalName = true;

  /// The set of file remappings, which take existing files on
  /// the system (the first part of each pair) and gives them the
  /// contents of other files on the system (the second part of each
  /// pair).
  std::vector<std::pair<std::string, std::string>> RemappedFiles;

  /// The set of file-to-buffer remappings, which take existing files
  /// on the system (the first part of each pair) and gives them the contents
  /// of the specified memory buffer (the second part of each pair).
  std::vector<std::pair<std::string, llvm::MemoryBuffer *>> RemappedFileBuffers;

  /// User specified embed entries.
  std::vector<std::string> EmbedEntries;

  /// Whether the compiler instance should retain (i.e., not free)
  /// the buffers associated with remapped files.
  ///
  /// This flag defaults to false; it can be set true only through direct
  /// manipulation of the compiler invocation object, in cases where the
  /// compiler invocation and its buffers will be reused.
  bool RetainRemappedFileBuffers = false;

  /// When enabled, excluded conditional blocks retain in the main file.
  bool RetainExcludedConditionalBlocks = false;

  /// The Objective-C++ ARC standard library that we should support,
  /// by providing appropriate definitions to retrofit the standard library
  /// with support for lifetime-qualified pointers.
  ObjCXXARCStandardLibraryKind ObjCXXARCStandardLibrary = ARCXX_nolib;

  /// Function for getting the dependency preprocessor directives of a file.
  ///
  /// These are directives derived from a special form of lexing where the
  /// source input is scanned for the preprocessor directives that might have an
  /// effect on the dependencies for a compilation unit.
  ///
  /// Enables a client to cache the directives for a file and provide them
  /// across multiple compiler invocations.
  /// FIXME: Allow returning an error.
  std::function<std::optional<ArrayRef<dependency_directives_scan::Directive>>(
      FileEntryRef)>
      DependencyDirectivesForFile;

  /// Set up preprocessor for RunAnalysis action.
  bool SetUpStaticAnalyzer = false;

  /// Prevents intended crashes when using #pragma clang __debug. For testing.
  bool DisablePragmaDebugCrash = false;

  /// If set, the UNIX timestamp specified by SOURCE_DATE_EPOCH.
  std::optional<uint64_t> SourceDateEpoch;

public:
  PreprocessorOptions() : PrecompiledPreambleBytes(0, false) {}

  void addMacroDef(StringRef Name) {
    Macros.emplace_back(std::string(Name), false);
  }
  void addMacroUndef(StringRef Name) {
    Macros.emplace_back(std::string(Name), true);
  }

  void addRemappedFile(StringRef From, StringRef To) {
    RemappedFiles.emplace_back(std::string(From), std::string(To));
  }

  void addRemappedFile(StringRef From, llvm::MemoryBuffer *To) {
    RemappedFileBuffers.emplace_back(std::string(From), To);
  }

  void clearRemappedFiles() {
    RemappedFiles.clear();
    RemappedFileBuffers.clear();
  }

  /// Reset any options that are not considered when building a
  /// module.
  void resetNonModularOptions() {
    Includes.clear();
    MacroIncludes.clear();
    ChainedIncludes.clear();
    DumpDeserializedPCHDecls = false;
    ImplicitPCHInclude.clear();
    SingleFileParseMode = false;
    LexEditorPlaceholders = true;
    RetainRemappedFileBuffers = true;
    PrecompiledPreambleBytes.first = 0;
    PrecompiledPreambleBytes.second = false;
    RetainExcludedConditionalBlocks = false;
  }
};

} // namespace clang

#endif // LLVM_CLANG_LEX_PREPROCESSOROPTIONS_H_

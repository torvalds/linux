//===- ASTReader.h - AST File Reader ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ASTReader class, which reads AST files.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SERIALIZATION_ASTREADER_H
#define LLVM_CLANG_SERIALIZATION_ASTREADER_H

#include "clang/AST/Type.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/OpenCLOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Version.h"
#include "clang/Lex/ExternalPreprocessorSource.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/PreprocessingRecord.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Sema/ExternalSemaSource.h"
#include "clang/Sema/IdentifierResolver.h"
#include "clang/Sema/Sema.h"
#include "clang/Serialization/ASTBitCodes.h"
#include "clang/Serialization/ContinuousRangeMap.h"
#include "clang/Serialization/ModuleFile.h"
#include "clang/Serialization/ModuleFileExtension.h"
#include "clang/Serialization/ModuleManager.h"
#include "clang/Serialization/SourceLocationEncoding.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/PagedVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Bitstream/BitstreamReader.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/VersionTuple.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <deque>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace clang {

class ASTConsumer;
class ASTContext;
class ASTDeserializationListener;
class ASTReader;
class ASTRecordReader;
class CXXTemporary;
class Decl;
class DeclarationName;
class DeclaratorDecl;
class DeclContext;
class EnumDecl;
class Expr;
class FieldDecl;
class FileEntry;
class FileManager;
class FileSystemOptions;
class FunctionDecl;
class GlobalModuleIndex;
struct HeaderFileInfo;
class HeaderSearchOptions;
class LangOptions;
class MacroInfo;
class InMemoryModuleCache;
class NamedDecl;
class NamespaceDecl;
class ObjCCategoryDecl;
class ObjCInterfaceDecl;
class PCHContainerReader;
class Preprocessor;
class PreprocessorOptions;
class Sema;
class SourceManager;
class Stmt;
class SwitchCase;
class TargetOptions;
class Token;
class TypedefNameDecl;
class ValueDecl;
class VarDecl;

/// Abstract interface for callback invocations by the ASTReader.
///
/// While reading an AST file, the ASTReader will call the methods of the
/// listener to pass on specific information. Some of the listener methods can
/// return true to indicate to the ASTReader that the information (and
/// consequently the AST file) is invalid.
class ASTReaderListener {
public:
  virtual ~ASTReaderListener();

  /// Receives the full Clang version information.
  ///
  /// \returns true to indicate that the version is invalid. Subclasses should
  /// generally defer to this implementation.
  virtual bool ReadFullVersionInformation(StringRef FullVersion) {
    return FullVersion != getClangFullRepositoryVersion();
  }

  virtual void ReadModuleName(StringRef ModuleName) {}
  virtual void ReadModuleMapFile(StringRef ModuleMapPath) {}

  /// Receives the language options.
  ///
  /// \returns true to indicate the options are invalid or false otherwise.
  virtual bool ReadLanguageOptions(const LangOptions &LangOpts,
                                   bool Complain,
                                   bool AllowCompatibleDifferences) {
    return false;
  }

  /// Receives the target options.
  ///
  /// \returns true to indicate the target options are invalid, or false
  /// otherwise.
  virtual bool ReadTargetOptions(const TargetOptions &TargetOpts, bool Complain,
                                 bool AllowCompatibleDifferences) {
    return false;
  }

  /// Receives the diagnostic options.
  ///
  /// \returns true to indicate the diagnostic options are invalid, or false
  /// otherwise.
  virtual bool
  ReadDiagnosticOptions(IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts,
                        bool Complain) {
    return false;
  }

  /// Receives the file system options.
  ///
  /// \returns true to indicate the file system options are invalid, or false
  /// otherwise.
  virtual bool ReadFileSystemOptions(const FileSystemOptions &FSOpts,
                                     bool Complain) {
    return false;
  }

  /// Receives the header search options.
  ///
  /// \param HSOpts The read header search options. The following fields are
  ///               missing and are reported in ReadHeaderSearchPaths():
  ///               UserEntries, SystemHeaderPrefixes, VFSOverlayFiles.
  ///
  /// \returns true to indicate the header search options are invalid, or false
  /// otherwise.
  virtual bool ReadHeaderSearchOptions(const HeaderSearchOptions &HSOpts,
                                       StringRef SpecificModuleCachePath,
                                       bool Complain) {
    return false;
  }

  /// Receives the header search paths.
  ///
  /// \param HSOpts The read header search paths. Only the following fields are
  ///               initialized: UserEntries, SystemHeaderPrefixes,
  ///               VFSOverlayFiles. The rest is reported in
  ///               ReadHeaderSearchOptions().
  ///
  /// \returns true to indicate the header search paths are invalid, or false
  /// otherwise.
  virtual bool ReadHeaderSearchPaths(const HeaderSearchOptions &HSOpts,
                                     bool Complain) {
    return false;
  }

  /// Receives the preprocessor options.
  ///
  /// \param SuggestedPredefines Can be filled in with the set of predefines
  /// that are suggested by the preprocessor options. Typically only used when
  /// loading a precompiled header.
  ///
  /// \returns true to indicate the preprocessor options are invalid, or false
  /// otherwise.
  virtual bool ReadPreprocessorOptions(const PreprocessorOptions &PPOpts,
                                       bool ReadMacros, bool Complain,
                                       std::string &SuggestedPredefines) {
    return false;
  }

  /// Receives __COUNTER__ value.
  virtual void ReadCounter(const serialization::ModuleFile &M,
                           unsigned Value) {}

  /// This is called for each AST file loaded.
  virtual void visitModuleFile(StringRef Filename,
                               serialization::ModuleKind Kind) {}

  /// Returns true if this \c ASTReaderListener wants to receive the
  /// input files of the AST file via \c visitInputFile, false otherwise.
  virtual bool needsInputFileVisitation() { return false; }

  /// Returns true if this \c ASTReaderListener wants to receive the
  /// system input files of the AST file via \c visitInputFile, false otherwise.
  virtual bool needsSystemInputFileVisitation() { return false; }

  /// if \c needsInputFileVisitation returns true, this is called for
  /// each non-system input file of the AST File. If
  /// \c needsSystemInputFileVisitation is true, then it is called for all
  /// system input files as well.
  ///
  /// \returns true to continue receiving the next input file, false to stop.
  virtual bool visitInputFile(StringRef Filename, bool isSystem,
                              bool isOverridden, bool isExplicitModule) {
    return true;
  }

  /// Returns true if this \c ASTReaderListener wants to receive the
  /// imports of the AST file via \c visitImport, false otherwise.
  virtual bool needsImportVisitation() const { return false; }

  /// If needsImportVisitation returns \c true, this is called for each
  /// AST file imported by this AST file.
  virtual void visitImport(StringRef ModuleName, StringRef Filename) {}

  /// Indicates that a particular module file extension has been read.
  virtual void readModuleFileExtension(
                 const ModuleFileExtensionMetadata &Metadata) {}
};

/// Simple wrapper class for chaining listeners.
class ChainedASTReaderListener : public ASTReaderListener {
  std::unique_ptr<ASTReaderListener> First;
  std::unique_ptr<ASTReaderListener> Second;

public:
  /// Takes ownership of \p First and \p Second.
  ChainedASTReaderListener(std::unique_ptr<ASTReaderListener> First,
                           std::unique_ptr<ASTReaderListener> Second)
      : First(std::move(First)), Second(std::move(Second)) {}

  std::unique_ptr<ASTReaderListener> takeFirst() { return std::move(First); }
  std::unique_ptr<ASTReaderListener> takeSecond() { return std::move(Second); }

  bool ReadFullVersionInformation(StringRef FullVersion) override;
  void ReadModuleName(StringRef ModuleName) override;
  void ReadModuleMapFile(StringRef ModuleMapPath) override;
  bool ReadLanguageOptions(const LangOptions &LangOpts, bool Complain,
                           bool AllowCompatibleDifferences) override;
  bool ReadTargetOptions(const TargetOptions &TargetOpts, bool Complain,
                         bool AllowCompatibleDifferences) override;
  bool ReadDiagnosticOptions(IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts,
                             bool Complain) override;
  bool ReadFileSystemOptions(const FileSystemOptions &FSOpts,
                             bool Complain) override;

  bool ReadHeaderSearchOptions(const HeaderSearchOptions &HSOpts,
                               StringRef SpecificModuleCachePath,
                               bool Complain) override;
  bool ReadPreprocessorOptions(const PreprocessorOptions &PPOpts,
                               bool ReadMacros, bool Complain,
                               std::string &SuggestedPredefines) override;

  void ReadCounter(const serialization::ModuleFile &M, unsigned Value) override;
  bool needsInputFileVisitation() override;
  bool needsSystemInputFileVisitation() override;
  void visitModuleFile(StringRef Filename,
                       serialization::ModuleKind Kind) override;
  bool visitInputFile(StringRef Filename, bool isSystem,
                      bool isOverridden, bool isExplicitModule) override;
  void readModuleFileExtension(
         const ModuleFileExtensionMetadata &Metadata) override;
};

/// ASTReaderListener implementation to validate the information of
/// the PCH file against an initialized Preprocessor.
class PCHValidator : public ASTReaderListener {
  Preprocessor &PP;
  ASTReader &Reader;

public:
  PCHValidator(Preprocessor &PP, ASTReader &Reader)
      : PP(PP), Reader(Reader) {}

  bool ReadLanguageOptions(const LangOptions &LangOpts, bool Complain,
                           bool AllowCompatibleDifferences) override;
  bool ReadTargetOptions(const TargetOptions &TargetOpts, bool Complain,
                         bool AllowCompatibleDifferences) override;
  bool ReadDiagnosticOptions(IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts,
                             bool Complain) override;
  bool ReadPreprocessorOptions(const PreprocessorOptions &PPOpts,
                               bool ReadMacros, bool Complain,
                               std::string &SuggestedPredefines) override;
  bool ReadHeaderSearchOptions(const HeaderSearchOptions &HSOpts,
                               StringRef SpecificModuleCachePath,
                               bool Complain) override;
  void ReadCounter(const serialization::ModuleFile &M, unsigned Value) override;
};

/// ASTReaderListenter implementation to set SuggestedPredefines of
/// ASTReader which is required to use a pch file. This is the replacement
/// of PCHValidator or SimplePCHValidator when using a pch file without
/// validating it.
class SimpleASTReaderListener : public ASTReaderListener {
  Preprocessor &PP;

public:
  SimpleASTReaderListener(Preprocessor &PP) : PP(PP) {}

  bool ReadPreprocessorOptions(const PreprocessorOptions &PPOpts,
                               bool ReadMacros, bool Complain,
                               std::string &SuggestedPredefines) override;
};

namespace serialization {

class ReadMethodPoolVisitor;

namespace reader {

class ASTIdentifierLookupTrait;

/// The on-disk hash table(s) used for DeclContext name lookup.
struct DeclContextLookupTable;

} // namespace reader

} // namespace serialization

/// Reads an AST files chain containing the contents of a translation
/// unit.
///
/// The ASTReader class reads bitstreams (produced by the ASTWriter
/// class) containing the serialized representation of a given
/// abstract syntax tree and its supporting data structures. An
/// instance of the ASTReader can be attached to an ASTContext object,
/// which will provide access to the contents of the AST files.
///
/// The AST reader provides lazy de-serialization of declarations, as
/// required when traversing the AST. Only those AST nodes that are
/// actually required will be de-serialized.
class ASTReader
  : public ExternalPreprocessorSource,
    public ExternalPreprocessingRecordSource,
    public ExternalHeaderFileInfoSource,
    public ExternalSemaSource,
    public IdentifierInfoLookup,
    public ExternalSLocEntrySource
{
public:
  /// Types of AST files.
  friend class ASTDeclReader;
  friend class ASTIdentifierIterator;
  friend class ASTRecordReader;
  friend class ASTUnit; // ASTUnit needs to remap source locations.
  friend class ASTWriter;
  friend class PCHValidator;
  friend class serialization::reader::ASTIdentifierLookupTrait;
  friend class serialization::ReadMethodPoolVisitor;
  friend class TypeLocReader;
  friend class LocalDeclID;

  using RecordData = SmallVector<uint64_t, 64>;
  using RecordDataImpl = SmallVectorImpl<uint64_t>;

  /// The result of reading the control block of an AST file, which
  /// can fail for various reasons.
  enum ASTReadResult {
    /// The control block was read successfully. Aside from failures,
    /// the AST file is safe to read into the current context.
    Success,

    /// The AST file itself appears corrupted.
    Failure,

    /// The AST file was missing.
    Missing,

    /// The AST file is out-of-date relative to its input files,
    /// and needs to be regenerated.
    OutOfDate,

    /// The AST file was written by a different version of Clang.
    VersionMismatch,

    /// The AST file was written with a different language/target
    /// configuration.
    ConfigurationMismatch,

    /// The AST file has errors.
    HadErrors
  };

  using ModuleFile = serialization::ModuleFile;
  using ModuleKind = serialization::ModuleKind;
  using ModuleManager = serialization::ModuleManager;
  using ModuleIterator = ModuleManager::ModuleIterator;
  using ModuleConstIterator = ModuleManager::ModuleConstIterator;
  using ModuleReverseIterator = ModuleManager::ModuleReverseIterator;

private:
  using LocSeq = SourceLocationSequence;

  /// The receiver of some callbacks invoked by ASTReader.
  std::unique_ptr<ASTReaderListener> Listener;

  /// The receiver of deserialization events.
  ASTDeserializationListener *DeserializationListener = nullptr;

  bool OwnsDeserializationListener = false;

  SourceManager &SourceMgr;
  FileManager &FileMgr;
  const PCHContainerReader &PCHContainerRdr;
  DiagnosticsEngine &Diags;
  // Sema has duplicate logic, but SemaObj can sometimes be null so ASTReader
  // has its own version.
  bool WarnedStackExhausted = false;

  /// The semantic analysis object that will be processing the
  /// AST files and the translation unit that uses it.
  Sema *SemaObj = nullptr;

  /// The preprocessor that will be loading the source file.
  Preprocessor &PP;

  /// The AST context into which we'll read the AST files.
  ASTContext *ContextObj = nullptr;

  /// The AST consumer.
  ASTConsumer *Consumer = nullptr;

  /// The module manager which manages modules and their dependencies
  ModuleManager ModuleMgr;

  /// A dummy identifier resolver used to merge TU-scope declarations in
  /// C, for the cases where we don't have a Sema object to provide a real
  /// identifier resolver.
  IdentifierResolver DummyIdResolver;

  /// A mapping from extension block names to module file extensions.
  llvm::StringMap<std::shared_ptr<ModuleFileExtension>> ModuleFileExtensions;

  /// A timer used to track the time spent deserializing.
  std::unique_ptr<llvm::Timer> ReadTimer;

  /// The location where the module file will be considered as
  /// imported from. For non-module AST types it should be invalid.
  SourceLocation CurrentImportLoc;

  /// The module kind that is currently deserializing.
  std::optional<ModuleKind> CurrentDeserializingModuleKind;

  /// The global module index, if loaded.
  std::unique_ptr<GlobalModuleIndex> GlobalIndex;

  /// A map of global bit offsets to the module that stores entities
  /// at those bit offsets.
  ContinuousRangeMap<uint64_t, ModuleFile*, 4> GlobalBitOffsetsMap;

  /// A map of negated SLocEntryIDs to the modules containing them.
  ContinuousRangeMap<unsigned, ModuleFile*, 64> GlobalSLocEntryMap;

  using GlobalSLocOffsetMapType =
      ContinuousRangeMap<unsigned, ModuleFile *, 64>;

  /// A map of reversed (SourceManager::MaxLoadedOffset - SLocOffset)
  /// SourceLocation offsets to the modules containing them.
  GlobalSLocOffsetMapType GlobalSLocOffsetMap;

  /// Types that have already been loaded from the chain.
  ///
  /// When the pointer at index I is non-NULL, the type with
  /// ID = (I + 1) << FastQual::Width has already been loaded
  llvm::PagedVector<QualType> TypesLoaded;

  /// Declarations that have already been loaded from the chain.
  ///
  /// When the pointer at index I is non-NULL, the declaration with ID
  /// = I + 1 has already been loaded.
  llvm::PagedVector<Decl *> DeclsLoaded;

  using FileOffset = std::pair<ModuleFile *, uint64_t>;
  using FileOffsetsTy = SmallVector<FileOffset, 2>;
  using DeclUpdateOffsetsMap = llvm::DenseMap<GlobalDeclID, FileOffsetsTy>;

  /// Declarations that have modifications residing in a later file
  /// in the chain.
  DeclUpdateOffsetsMap DeclUpdateOffsets;

  using DelayedNamespaceOffsetMapTy =
      llvm::DenseMap<GlobalDeclID, std::pair</*LexicalOffset*/ uint64_t,
                                             /*VisibleOffset*/ uint64_t>>;

  /// Mapping from global declaration IDs to the lexical and visible block
  /// offset for delayed namespace in reduced BMI.
  ///
  /// We can't use the existing DeclUpdate mechanism since the DeclUpdate
  /// may only be applied in an outer most read. However, we need to know
  /// whether or not a DeclContext has external storage during the recursive
  /// reading. So we need to apply the offset immediately after we read the
  /// namespace as if it is not delayed.
  DelayedNamespaceOffsetMapTy DelayedNamespaceOffsetMap;

  struct PendingUpdateRecord {
    Decl *D;
    GlobalDeclID ID;

    // Whether the declaration was just deserialized.
    bool JustLoaded;

    PendingUpdateRecord(GlobalDeclID ID, Decl *D, bool JustLoaded)
        : D(D), ID(ID), JustLoaded(JustLoaded) {}
  };

  /// Declaration updates for already-loaded declarations that we need
  /// to apply once we finish processing an import.
  llvm::SmallVector<PendingUpdateRecord, 16> PendingUpdateRecords;

  enum class PendingFakeDefinitionKind { NotFake, Fake, FakeLoaded };

  /// The DefinitionData pointers that we faked up for class definitions
  /// that we needed but hadn't loaded yet.
  llvm::DenseMap<void *, PendingFakeDefinitionKind> PendingFakeDefinitionData;

  /// Exception specification updates that have been loaded but not yet
  /// propagated across the relevant redeclaration chain. The map key is the
  /// canonical declaration (used only for deduplication) and the value is a
  /// declaration that has an exception specification.
  llvm::SmallMapVector<Decl *, FunctionDecl *, 4> PendingExceptionSpecUpdates;

  /// Deduced return type updates that have been loaded but not yet propagated
  /// across the relevant redeclaration chain. The map key is the canonical
  /// declaration and the value is the deduced return type.
  llvm::SmallMapVector<FunctionDecl *, QualType, 4> PendingDeducedTypeUpdates;

  /// Functions has undededuced return type and we wish we can find the deduced
  /// return type by iterating the redecls in other modules.
  llvm::SmallVector<FunctionDecl *, 4> PendingUndeducedFunctionDecls;

  /// Declarations that have been imported and have typedef names for
  /// linkage purposes.
  llvm::DenseMap<std::pair<DeclContext *, IdentifierInfo *>, NamedDecl *>
      ImportedTypedefNamesForLinkage;

  /// Mergeable declaration contexts that have anonymous declarations
  /// within them, and those anonymous declarations.
  llvm::DenseMap<Decl*, llvm::SmallVector<NamedDecl*, 2>>
    AnonymousDeclarationsForMerging;

  /// Map from numbering information for lambdas to the corresponding lambdas.
  llvm::DenseMap<std::pair<const Decl *, unsigned>, NamedDecl *>
      LambdaDeclarationsForMerging;

  /// Key used to identify LifetimeExtendedTemporaryDecl for merging,
  /// containing the lifetime-extending declaration and the mangling number.
  using LETemporaryKey = std::pair<Decl *, unsigned>;

  /// Map of already deserialiazed temporaries.
  llvm::DenseMap<LETemporaryKey, LifetimeExtendedTemporaryDecl *>
      LETemporaryForMerging;

  struct FileDeclsInfo {
    ModuleFile *Mod = nullptr;
    ArrayRef<serialization::unaligned_decl_id_t> Decls;

    FileDeclsInfo() = default;
    FileDeclsInfo(ModuleFile *Mod,
                  ArrayRef<serialization::unaligned_decl_id_t> Decls)
        : Mod(Mod), Decls(Decls) {}
  };

  /// Map from a FileID to the file-level declarations that it contains.
  llvm::DenseMap<FileID, FileDeclsInfo> FileDeclIDs;

  /// An array of lexical contents of a declaration context, as a sequence of
  /// Decl::Kind, DeclID pairs.
  using LexicalContents = ArrayRef<serialization::unaligned_decl_id_t>;

  /// Map from a DeclContext to its lexical contents.
  llvm::DenseMap<const DeclContext*, std::pair<ModuleFile*, LexicalContents>>
      LexicalDecls;

  /// Map from the TU to its lexical contents from each module file.
  std::vector<std::pair<ModuleFile*, LexicalContents>> TULexicalDecls;

  /// Map from a DeclContext to its lookup tables.
  llvm::DenseMap<const DeclContext *,
                 serialization::reader::DeclContextLookupTable> Lookups;

  // Updates for visible decls can occur for other contexts than just the
  // TU, and when we read those update records, the actual context may not
  // be available yet, so have this pending map using the ID as a key. It
  // will be realized when the context is actually loaded.
  struct PendingVisibleUpdate {
    ModuleFile *Mod;
    const unsigned char *Data;
  };
  using DeclContextVisibleUpdates = SmallVector<PendingVisibleUpdate, 1>;

  /// Updates to the visible declarations of declaration contexts that
  /// haven't been loaded yet.
  llvm::DenseMap<GlobalDeclID, DeclContextVisibleUpdates> PendingVisibleUpdates;

  /// The set of C++ or Objective-C classes that have forward
  /// declarations that have not yet been linked to their definitions.
  llvm::SmallPtrSet<Decl *, 4> PendingDefinitions;

  using PendingBodiesMap =
      llvm::MapVector<Decl *, uint64_t,
                      llvm::SmallDenseMap<Decl *, unsigned, 4>,
                      SmallVector<std::pair<Decl *, uint64_t>, 4>>;

  /// Functions or methods that have bodies that will be attached.
  PendingBodiesMap PendingBodies;

  /// Definitions for which we have added merged definitions but not yet
  /// performed deduplication.
  llvm::SetVector<NamedDecl *> PendingMergedDefinitionsToDeduplicate;

  /// Read the record that describes the lexical contents of a DC.
  bool ReadLexicalDeclContextStorage(ModuleFile &M,
                                     llvm::BitstreamCursor &Cursor,
                                     uint64_t Offset, DeclContext *DC);

  /// Read the record that describes the visible contents of a DC.
  bool ReadVisibleDeclContextStorage(ModuleFile &M,
                                     llvm::BitstreamCursor &Cursor,
                                     uint64_t Offset, GlobalDeclID ID);

  /// A vector containing identifiers that have already been
  /// loaded.
  ///
  /// If the pointer at index I is non-NULL, then it refers to the
  /// IdentifierInfo for the identifier with ID=I+1 that has already
  /// been loaded.
  std::vector<IdentifierInfo *> IdentifiersLoaded;

  /// A vector containing macros that have already been
  /// loaded.
  ///
  /// If the pointer at index I is non-NULL, then it refers to the
  /// MacroInfo for the identifier with ID=I+1 that has already
  /// been loaded.
  std::vector<MacroInfo *> MacrosLoaded;

  using LoadedMacroInfo =
      std::pair<IdentifierInfo *, serialization::SubmoduleID>;

  /// A set of #undef directives that we have loaded; used to
  /// deduplicate the same #undef information coming from multiple module
  /// files.
  llvm::DenseSet<LoadedMacroInfo> LoadedUndefs;

  using GlobalMacroMapType =
      ContinuousRangeMap<serialization::MacroID, ModuleFile *, 4>;

  /// Mapping from global macro IDs to the module in which the
  /// macro resides along with the offset that should be added to the
  /// global macro ID to produce a local ID.
  GlobalMacroMapType GlobalMacroMap;

  /// A vector containing submodules that have already been loaded.
  ///
  /// This vector is indexed by the Submodule ID (-1). NULL submodule entries
  /// indicate that the particular submodule ID has not yet been loaded.
  SmallVector<Module *, 2> SubmodulesLoaded;

  using GlobalSubmoduleMapType =
      ContinuousRangeMap<serialization::SubmoduleID, ModuleFile *, 4>;

  /// Mapping from global submodule IDs to the module file in which the
  /// submodule resides along with the offset that should be added to the
  /// global submodule ID to produce a local ID.
  GlobalSubmoduleMapType GlobalSubmoduleMap;

  /// A set of hidden declarations.
  using HiddenNames = SmallVector<Decl *, 2>;
  using HiddenNamesMapType = llvm::DenseMap<Module *, HiddenNames>;

  /// A mapping from each of the hidden submodules to the deserialized
  /// declarations in that submodule that could be made visible.
  HiddenNamesMapType HiddenNamesMap;

  /// A module import, export, or conflict that hasn't yet been resolved.
  struct UnresolvedModuleRef {
    /// The file in which this module resides.
    ModuleFile *File;

    /// The module that is importing or exporting.
    Module *Mod;

    /// The kind of module reference.
    enum { Import, Export, Conflict, Affecting } Kind;

    /// The local ID of the module that is being exported.
    unsigned ID;

    /// Whether this is a wildcard export.
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsWildcard : 1;

    /// String data.
    StringRef String;
  };

  /// The set of module imports and exports that still need to be
  /// resolved.
  SmallVector<UnresolvedModuleRef, 2> UnresolvedModuleRefs;

  /// A vector containing selectors that have already been loaded.
  ///
  /// This vector is indexed by the Selector ID (-1). NULL selector
  /// entries indicate that the particular selector ID has not yet
  /// been loaded.
  SmallVector<Selector, 16> SelectorsLoaded;

  using GlobalSelectorMapType =
      ContinuousRangeMap<serialization::SelectorID, ModuleFile *, 4>;

  /// Mapping from global selector IDs to the module in which the
  /// global selector ID to produce a local ID.
  GlobalSelectorMapType GlobalSelectorMap;

  /// The generation number of the last time we loaded data from the
  /// global method pool for this selector.
  llvm::DenseMap<Selector, unsigned> SelectorGeneration;

  /// Whether a selector is out of date. We mark a selector as out of date
  /// if we load another module after the method pool entry was pulled in.
  llvm::DenseMap<Selector, bool> SelectorOutOfDate;

  struct PendingMacroInfo {
    ModuleFile *M;
    /// Offset relative to ModuleFile::MacroOffsetsBase.
    uint32_t MacroDirectivesOffset;

    PendingMacroInfo(ModuleFile *M, uint32_t MacroDirectivesOffset)
        : M(M), MacroDirectivesOffset(MacroDirectivesOffset) {}
  };

  using PendingMacroIDsMap =
      llvm::MapVector<IdentifierInfo *, SmallVector<PendingMacroInfo, 2>>;

  /// Mapping from identifiers that have a macro history to the global
  /// IDs have not yet been deserialized to the global IDs of those macros.
  PendingMacroIDsMap PendingMacroIDs;

  using GlobalPreprocessedEntityMapType =
      ContinuousRangeMap<unsigned, ModuleFile *, 4>;

  /// Mapping from global preprocessing entity IDs to the module in
  /// which the preprocessed entity resides along with the offset that should be
  /// added to the global preprocessing entity ID to produce a local ID.
  GlobalPreprocessedEntityMapType GlobalPreprocessedEntityMap;

  using GlobalSkippedRangeMapType =
      ContinuousRangeMap<unsigned, ModuleFile *, 4>;

  /// Mapping from global skipped range base IDs to the module in which
  /// the skipped ranges reside.
  GlobalSkippedRangeMapType GlobalSkippedRangeMap;

  /// \name CodeGen-relevant special data
  /// Fields containing data that is relevant to CodeGen.
  //@{

  /// The IDs of all declarations that fulfill the criteria of
  /// "interesting" decls.
  ///
  /// This contains the data loaded from all EAGERLY_DESERIALIZED_DECLS blocks
  /// in the chain. The referenced declarations are deserialized and passed to
  /// the consumer eagerly.
  SmallVector<GlobalDeclID, 16> EagerlyDeserializedDecls;

  /// The IDs of all vtables to emit. The referenced declarations are passed
  /// to the consumers' HandleVTable eagerly after passing
  /// EagerlyDeserializedDecls.
  SmallVector<GlobalDeclID, 16> VTablesToEmit;

  /// The IDs of all tentative definitions stored in the chain.
  ///
  /// Sema keeps track of all tentative definitions in a TU because it has to
  /// complete them and pass them on to CodeGen. Thus, tentative definitions in
  /// the PCH chain must be eagerly deserialized.
  SmallVector<GlobalDeclID, 16> TentativeDefinitions;

  /// The IDs of all CXXRecordDecls stored in the chain whose VTables are
  /// used.
  ///
  /// CodeGen has to emit VTables for these records, so they have to be eagerly
  /// deserialized.
  struct VTableUse {
    GlobalDeclID ID;
    SourceLocation::UIntTy RawLoc;
    bool Used;
  };
  SmallVector<VTableUse> VTableUses;

  /// A snapshot of the pending instantiations in the chain.
  ///
  /// This record tracks the instantiations that Sema has to perform at the
  /// end of the TU. It consists of a pair of values for every pending
  /// instantiation where the first value is the ID of the decl and the second
  /// is the instantiation location.
  struct PendingInstantiation {
    GlobalDeclID ID;
    SourceLocation::UIntTy RawLoc;
  };
  SmallVector<PendingInstantiation, 64> PendingInstantiations;

  //@}

  /// \name DiagnosticsEngine-relevant special data
  /// Fields containing data that is used for generating diagnostics
  //@{

  /// A snapshot of Sema's unused file-scoped variable tracking, for
  /// generating warnings.
  SmallVector<GlobalDeclID, 16> UnusedFileScopedDecls;

  /// A list of all the delegating constructors we've seen, to diagnose
  /// cycles.
  SmallVector<GlobalDeclID, 4> DelegatingCtorDecls;

  /// Method selectors used in a @selector expression. Used for
  /// implementation of -Wselector.
  SmallVector<serialization::SelectorID, 64> ReferencedSelectorsData;

  /// A snapshot of Sema's weak undeclared identifier tracking, for
  /// generating warnings.
  SmallVector<serialization::IdentifierID, 64> WeakUndeclaredIdentifiers;

  /// The IDs of type aliases for ext_vectors that exist in the chain.
  ///
  /// Used by Sema for finding sugared names for ext_vectors in diagnostics.
  SmallVector<GlobalDeclID, 4> ExtVectorDecls;

  //@}

  /// \name Sema-relevant special data
  /// Fields containing data that is used for semantic analysis
  //@{

  /// The IDs of all potentially unused typedef names in the chain.
  ///
  /// Sema tracks these to emit warnings.
  SmallVector<GlobalDeclID, 16> UnusedLocalTypedefNameCandidates;

  /// Our current depth in #pragma cuda force_host_device begin/end
  /// macros.
  unsigned ForceHostDeviceDepth = 0;

  /// The IDs of the declarations Sema stores directly.
  ///
  /// Sema tracks a few important decls, such as namespace std, directly.
  SmallVector<GlobalDeclID, 4> SemaDeclRefs;

  /// The IDs of the types ASTContext stores directly.
  ///
  /// The AST context tracks a few important types, such as va_list, directly.
  SmallVector<serialization::TypeID, 16> SpecialTypes;

  /// The IDs of CUDA-specific declarations ASTContext stores directly.
  ///
  /// The AST context tracks a few important decls, currently cudaConfigureCall,
  /// directly.
  SmallVector<GlobalDeclID, 2> CUDASpecialDeclRefs;

  /// The floating point pragma option settings.
  SmallVector<uint64_t, 1> FPPragmaOptions;

  /// The pragma clang optimize location (if the pragma state is "off").
  SourceLocation OptimizeOffPragmaLocation;

  /// The PragmaMSStructKind pragma ms_struct state if set, or -1.
  int PragmaMSStructState = -1;

  /// The PragmaMSPointersToMembersKind pragma pointers_to_members state.
  int PragmaMSPointersToMembersState = -1;
  SourceLocation PointersToMembersPragmaLocation;

  /// The pragma float_control state.
  std::optional<FPOptionsOverride> FpPragmaCurrentValue;
  SourceLocation FpPragmaCurrentLocation;
  struct FpPragmaStackEntry {
    FPOptionsOverride Value;
    SourceLocation Location;
    SourceLocation PushLocation;
    StringRef SlotLabel;
  };
  llvm::SmallVector<FpPragmaStackEntry, 2> FpPragmaStack;
  llvm::SmallVector<std::string, 2> FpPragmaStrings;

  /// The pragma align/pack state.
  std::optional<Sema::AlignPackInfo> PragmaAlignPackCurrentValue;
  SourceLocation PragmaAlignPackCurrentLocation;
  struct PragmaAlignPackStackEntry {
    Sema::AlignPackInfo Value;
    SourceLocation Location;
    SourceLocation PushLocation;
    StringRef SlotLabel;
  };
  llvm::SmallVector<PragmaAlignPackStackEntry, 2> PragmaAlignPackStack;
  llvm::SmallVector<std::string, 2> PragmaAlignPackStrings;

  /// The OpenCL extension settings.
  OpenCLOptions OpenCLExtensions;

  /// Extensions required by an OpenCL type.
  llvm::DenseMap<const Type *, std::set<std::string>> OpenCLTypeExtMap;

  /// Extensions required by an OpenCL declaration.
  llvm::DenseMap<const Decl *, std::set<std::string>> OpenCLDeclExtMap;

  /// A list of the namespaces we've seen.
  SmallVector<GlobalDeclID, 4> KnownNamespaces;

  /// A list of undefined decls with internal linkage followed by the
  /// SourceLocation of a matching ODR-use.
  struct UndefinedButUsedDecl {
    GlobalDeclID ID;
    SourceLocation::UIntTy RawLoc;
  };
  SmallVector<UndefinedButUsedDecl, 8> UndefinedButUsed;

  /// Delete expressions to analyze at the end of translation unit.
  SmallVector<uint64_t, 8> DelayedDeleteExprs;

  // A list of late parsed template function data with their module files.
  SmallVector<std::pair<ModuleFile *, SmallVector<uint64_t, 1>>, 4>
      LateParsedTemplates;

  /// The IDs of all decls to be checked for deferred diags.
  ///
  /// Sema tracks these to emit deferred diags.
  llvm::SmallSetVector<GlobalDeclID, 4> DeclsToCheckForDeferredDiags;

private:
  struct ImportedSubmodule {
    serialization::SubmoduleID ID;
    SourceLocation ImportLoc;

    ImportedSubmodule(serialization::SubmoduleID ID, SourceLocation ImportLoc)
        : ID(ID), ImportLoc(ImportLoc) {}
  };

  /// A list of modules that were imported by precompiled headers or
  /// any other non-module AST file and have not yet been made visible. If a
  /// module is made visible in the ASTReader, it will be transfered to
  /// \c PendingImportedModulesSema.
  SmallVector<ImportedSubmodule, 2> PendingImportedModules;

  /// A list of modules that were imported by precompiled headers or
  /// any other non-module AST file and have not yet been made visible for Sema.
  SmallVector<ImportedSubmodule, 2> PendingImportedModulesSema;
  //@}

  /// The system include root to be used when loading the
  /// precompiled header.
  std::string isysroot;

  /// Whether to disable the normal validation performed on precompiled
  /// headers and module files when they are loaded.
  DisableValidationForModuleKind DisableValidationKind;

  /// Whether to accept an AST file with compiler errors.
  bool AllowASTWithCompilerErrors;

  /// Whether to accept an AST file that has a different configuration
  /// from the current compiler instance.
  bool AllowConfigurationMismatch;

  /// Whether validate system input files.
  bool ValidateSystemInputs;

  /// Whether validate headers and module maps using hash based on contents.
  bool ValidateASTInputFilesContent;

  /// Whether we are allowed to use the global module index.
  bool UseGlobalIndex;

  /// Whether we have tried loading the global module index yet.
  bool TriedLoadingGlobalIndex = false;

  ///Whether we are currently processing update records.
  bool ProcessingUpdateRecords = false;

  using SwitchCaseMapTy = llvm::DenseMap<unsigned, SwitchCase *>;

  /// Mapping from switch-case IDs in the chain to switch-case statements
  ///
  /// Statements usually don't have IDs, but switch cases need them, so that the
  /// switch statement can refer to them.
  SwitchCaseMapTy SwitchCaseStmts;

  SwitchCaseMapTy *CurrSwitchCaseStmts;

  /// The number of source location entries de-serialized from
  /// the PCH file.
  unsigned NumSLocEntriesRead = 0;

  /// The number of source location entries in the chain.
  unsigned TotalNumSLocEntries = 0;

  /// The number of statements (and expressions) de-serialized
  /// from the chain.
  unsigned NumStatementsRead = 0;

  /// The total number of statements (and expressions) stored
  /// in the chain.
  unsigned TotalNumStatements = 0;

  /// The number of macros de-serialized from the chain.
  unsigned NumMacrosRead = 0;

  /// The total number of macros stored in the chain.
  unsigned TotalNumMacros = 0;

  /// The number of lookups into identifier tables.
  unsigned NumIdentifierLookups = 0;

  /// The number of lookups into identifier tables that succeed.
  unsigned NumIdentifierLookupHits = 0;

  /// The number of selectors that have been read.
  unsigned NumSelectorsRead = 0;

  /// The number of method pool entries that have been read.
  unsigned NumMethodPoolEntriesRead = 0;

  /// The number of times we have looked up a selector in the method
  /// pool.
  unsigned NumMethodPoolLookups = 0;

  /// The number of times we have looked up a selector in the method
  /// pool and found something.
  unsigned NumMethodPoolHits = 0;

  /// The number of times we have looked up a selector in the method
  /// pool within a specific module.
  unsigned NumMethodPoolTableLookups = 0;

  /// The number of times we have looked up a selector in the method
  /// pool within a specific module and found something.
  unsigned NumMethodPoolTableHits = 0;

  /// The total number of method pool entries in the selector table.
  unsigned TotalNumMethodPoolEntries = 0;

  /// Number of lexical decl contexts read/total.
  unsigned NumLexicalDeclContextsRead = 0, TotalLexicalDeclContexts = 0;

  /// Number of visible decl contexts read/total.
  unsigned NumVisibleDeclContextsRead = 0, TotalVisibleDeclContexts = 0;

  /// Total size of modules, in bits, currently loaded
  uint64_t TotalModulesSizeInBits = 0;

  /// Number of Decl/types that are currently deserializing.
  unsigned NumCurrentElementsDeserializing = 0;

  /// Set true while we are in the process of passing deserialized
  /// "interesting" decls to consumer inside FinishedDeserializing().
  /// This is used as a guard to avoid recursively repeating the process of
  /// passing decls to consumer.
  bool PassingDeclsToConsumer = false;

  /// The set of identifiers that were read while the AST reader was
  /// (recursively) loading declarations.
  ///
  /// The declarations on the identifier chain for these identifiers will be
  /// loaded once the recursive loading has completed.
  llvm::MapVector<IdentifierInfo *, SmallVector<GlobalDeclID, 4>>
      PendingIdentifierInfos;

  /// The set of lookup results that we have faked in order to support
  /// merging of partially deserialized decls but that we have not yet removed.
  llvm::SmallMapVector<const IdentifierInfo *, SmallVector<NamedDecl *, 2>, 16>
      PendingFakeLookupResults;

  /// The generation number of each identifier, which keeps track of
  /// the last time we loaded information about this identifier.
  llvm::DenseMap<const IdentifierInfo *, unsigned> IdentifierGeneration;

  /// Contains declarations and definitions that could be
  /// "interesting" to the ASTConsumer, when we get that AST consumer.
  ///
  /// "Interesting" declarations are those that have data that may
  /// need to be emitted, such as inline function definitions or
  /// Objective-C protocols.
  std::deque<Decl *> PotentiallyInterestingDecls;

  /// The list of deduced function types that we have not yet read, because
  /// they might contain a deduced return type that refers to a local type
  /// declared within the function.
  SmallVector<std::pair<FunctionDecl *, serialization::TypeID>, 16>
      PendingDeducedFunctionTypes;

  /// The list of deduced variable types that we have not yet read, because
  /// they might contain a deduced type that refers to a local type declared
  /// within the variable.
  SmallVector<std::pair<VarDecl *, serialization::TypeID>, 16>
      PendingDeducedVarTypes;

  /// The list of redeclaration chains that still need to be
  /// reconstructed, and the local offset to the corresponding list
  /// of redeclarations.
  SmallVector<std::pair<Decl *, uint64_t>, 16> PendingDeclChains;

  /// The list of canonical declarations whose redeclaration chains
  /// need to be marked as incomplete once we're done deserializing things.
  SmallVector<Decl *, 16> PendingIncompleteDeclChains;

  /// The Decl IDs for the Sema/Lexical DeclContext of a Decl that has
  /// been loaded but its DeclContext was not set yet.
  struct PendingDeclContextInfo {
    Decl *D;
    GlobalDeclID SemaDC;
    GlobalDeclID LexicalDC;
  };

  /// The set of Decls that have been loaded but their DeclContexts are
  /// not set yet.
  ///
  /// The DeclContexts for these Decls will be set once recursive loading has
  /// been completed.
  std::deque<PendingDeclContextInfo> PendingDeclContextInfos;

  template <typename DeclTy>
  using DuplicateObjCDecls = std::pair<DeclTy *, DeclTy *>;

  /// When resolving duplicate ivars from Objective-C extensions we don't error
  /// out immediately but check if can merge identical extensions. Not checking
  /// extensions for equality immediately because ivar deserialization isn't
  /// over yet at that point.
  llvm::SmallMapVector<DuplicateObjCDecls<ObjCCategoryDecl>,
                       llvm::SmallVector<DuplicateObjCDecls<ObjCIvarDecl>, 4>,
                       2>
      PendingObjCExtensionIvarRedeclarations;

  /// Members that have been added to classes, for which the class has not yet
  /// been notified. CXXRecordDecl::addedMember will be called for each of
  /// these once recursive deserialization is complete.
  SmallVector<std::pair<CXXRecordDecl*, Decl*>, 4> PendingAddedClassMembers;

  /// The set of NamedDecls that have been loaded, but are members of a
  /// context that has been merged into another context where the corresponding
  /// declaration is either missing or has not yet been loaded.
  ///
  /// We will check whether the corresponding declaration is in fact missing
  /// once recursing loading has been completed.
  llvm::SmallVector<NamedDecl *, 16> PendingOdrMergeChecks;

  using DataPointers =
      std::pair<CXXRecordDecl *, struct CXXRecordDecl::DefinitionData *>;
  using ObjCInterfaceDataPointers =
      std::pair<ObjCInterfaceDecl *,
                struct ObjCInterfaceDecl::DefinitionData *>;
  using ObjCProtocolDataPointers =
      std::pair<ObjCProtocolDecl *, struct ObjCProtocolDecl::DefinitionData *>;

  /// Record definitions in which we found an ODR violation.
  llvm::SmallDenseMap<CXXRecordDecl *, llvm::SmallVector<DataPointers, 2>, 2>
      PendingOdrMergeFailures;

  /// C/ObjC record definitions in which we found an ODR violation.
  llvm::SmallDenseMap<RecordDecl *, llvm::SmallVector<RecordDecl *, 2>, 2>
      PendingRecordOdrMergeFailures;

  /// Function definitions in which we found an ODR violation.
  llvm::SmallDenseMap<FunctionDecl *, llvm::SmallVector<FunctionDecl *, 2>, 2>
      PendingFunctionOdrMergeFailures;

  /// Enum definitions in which we found an ODR violation.
  llvm::SmallDenseMap<EnumDecl *, llvm::SmallVector<EnumDecl *, 2>, 2>
      PendingEnumOdrMergeFailures;

  /// ObjCInterfaceDecl in which we found an ODR violation.
  llvm::SmallDenseMap<ObjCInterfaceDecl *,
                      llvm::SmallVector<ObjCInterfaceDataPointers, 2>, 2>
      PendingObjCInterfaceOdrMergeFailures;

  /// ObjCProtocolDecl in which we found an ODR violation.
  llvm::SmallDenseMap<ObjCProtocolDecl *,
                      llvm::SmallVector<ObjCProtocolDataPointers, 2>, 2>
      PendingObjCProtocolOdrMergeFailures;

  /// DeclContexts in which we have diagnosed an ODR violation.
  llvm::SmallPtrSet<DeclContext*, 2> DiagnosedOdrMergeFailures;

  /// The set of Objective-C categories that have been deserialized
  /// since the last time the declaration chains were linked.
  llvm::SmallPtrSet<ObjCCategoryDecl *, 16> CategoriesDeserialized;

  /// The set of Objective-C class definitions that have already been
  /// loaded, for which we will need to check for categories whenever a new
  /// module is loaded.
  SmallVector<ObjCInterfaceDecl *, 16> ObjCClassesLoaded;

  using KeyDeclsMap = llvm::DenseMap<Decl *, SmallVector<GlobalDeclID, 2>>;

  /// A mapping from canonical declarations to the set of global
  /// declaration IDs for key declaration that have been merged with that
  /// canonical declaration. A key declaration is a formerly-canonical
  /// declaration whose module did not import any other key declaration for that
  /// entity. These are the IDs that we use as keys when finding redecl chains.
  KeyDeclsMap KeyDecls;

  /// A mapping from DeclContexts to the semantic DeclContext that we
  /// are treating as the definition of the entity. This is used, for instance,
  /// when merging implicit instantiations of class templates across modules.
  llvm::DenseMap<DeclContext *, DeclContext *> MergedDeclContexts;

  /// A mapping from canonical declarations of enums to their canonical
  /// definitions. Only populated when using modules in C++.
  llvm::DenseMap<EnumDecl *, EnumDecl *> EnumDefinitions;

  /// A mapping from canonical declarations of records to their canonical
  /// definitions. Doesn't cover CXXRecordDecl.
  llvm::DenseMap<RecordDecl *, RecordDecl *> RecordDefinitions;

  /// When reading a Stmt tree, Stmt operands are placed in this stack.
  SmallVector<Stmt *, 16> StmtStack;

  /// What kind of records we are reading.
  enum ReadingKind {
    Read_None, Read_Decl, Read_Type, Read_Stmt
  };

  /// What kind of records we are reading.
  ReadingKind ReadingKind = Read_None;

  /// RAII object to change the reading kind.
  class ReadingKindTracker {
    ASTReader &Reader;
    enum ReadingKind PrevKind;

  public:
    ReadingKindTracker(enum ReadingKind newKind, ASTReader &reader)
        : Reader(reader), PrevKind(Reader.ReadingKind) {
      Reader.ReadingKind = newKind;
    }

    ReadingKindTracker(const ReadingKindTracker &) = delete;
    ReadingKindTracker &operator=(const ReadingKindTracker &) = delete;
    ~ReadingKindTracker() { Reader.ReadingKind = PrevKind; }
  };

  /// RAII object to mark the start of processing updates.
  class ProcessingUpdatesRAIIObj {
    ASTReader &Reader;
    bool PrevState;

  public:
    ProcessingUpdatesRAIIObj(ASTReader &reader)
        : Reader(reader), PrevState(Reader.ProcessingUpdateRecords) {
      Reader.ProcessingUpdateRecords = true;
    }

    ProcessingUpdatesRAIIObj(const ProcessingUpdatesRAIIObj &) = delete;
    ProcessingUpdatesRAIIObj &
    operator=(const ProcessingUpdatesRAIIObj &) = delete;
    ~ProcessingUpdatesRAIIObj() { Reader.ProcessingUpdateRecords = PrevState; }
  };

  /// Suggested contents of the predefines buffer, after this
  /// PCH file has been processed.
  ///
  /// In most cases, this string will be empty, because the predefines
  /// buffer computed to build the PCH file will be identical to the
  /// predefines buffer computed from the command line. However, when
  /// there are differences that the PCH reader can work around, this
  /// predefines buffer may contain additional definitions.
  std::string SuggestedPredefines;

  llvm::DenseMap<const Decl *, bool> DefinitionSource;

  bool shouldDisableValidationForFile(const serialization::ModuleFile &M) const;

  /// Reads a statement from the specified cursor.
  Stmt *ReadStmtFromStream(ModuleFile &F);

  /// Retrieve the stored information about an input file.
  serialization::InputFileInfo getInputFileInfo(ModuleFile &F, unsigned ID);

  /// Retrieve the file entry and 'overridden' bit for an input
  /// file in the given module file.
  serialization::InputFile getInputFile(ModuleFile &F, unsigned ID,
                                        bool Complain = true);

public:
  void ResolveImportedPath(ModuleFile &M, std::string &Filename);
  static void ResolveImportedPath(std::string &Filename, StringRef Prefix);

  /// Returns the first key declaration for the given declaration. This
  /// is one that is formerly-canonical (or still canonical) and whose module
  /// did not import any other key declaration of the entity.
  Decl *getKeyDeclaration(Decl *D) {
    D = D->getCanonicalDecl();
    if (D->isFromASTFile())
      return D;

    auto I = KeyDecls.find(D);
    if (I == KeyDecls.end() || I->second.empty())
      return D;
    return GetExistingDecl(I->second[0]);
  }
  const Decl *getKeyDeclaration(const Decl *D) {
    return getKeyDeclaration(const_cast<Decl*>(D));
  }

  /// Run a callback on each imported key declaration of \p D.
  template <typename Fn>
  void forEachImportedKeyDecl(const Decl *D, Fn Visit) {
    D = D->getCanonicalDecl();
    if (D->isFromASTFile())
      Visit(D);

    auto It = KeyDecls.find(const_cast<Decl*>(D));
    if (It != KeyDecls.end())
      for (auto ID : It->second)
        Visit(GetExistingDecl(ID));
  }

  /// Get the loaded lookup tables for \p Primary, if any.
  const serialization::reader::DeclContextLookupTable *
  getLoadedLookupTables(DeclContext *Primary) const;

private:
  struct ImportedModule {
    ModuleFile *Mod;
    ModuleFile *ImportedBy;
    SourceLocation ImportLoc;

    ImportedModule(ModuleFile *Mod,
                   ModuleFile *ImportedBy,
                   SourceLocation ImportLoc)
        : Mod(Mod), ImportedBy(ImportedBy), ImportLoc(ImportLoc) {}
  };

  ASTReadResult ReadASTCore(StringRef FileName, ModuleKind Type,
                            SourceLocation ImportLoc, ModuleFile *ImportedBy,
                            SmallVectorImpl<ImportedModule> &Loaded,
                            off_t ExpectedSize, time_t ExpectedModTime,
                            ASTFileSignature ExpectedSignature,
                            unsigned ClientLoadCapabilities);
  ASTReadResult ReadControlBlock(ModuleFile &F,
                                 SmallVectorImpl<ImportedModule> &Loaded,
                                 const ModuleFile *ImportedBy,
                                 unsigned ClientLoadCapabilities);
  static ASTReadResult ReadOptionsBlock(
      llvm::BitstreamCursor &Stream, unsigned ClientLoadCapabilities,
      bool AllowCompatibleConfigurationMismatch, ASTReaderListener &Listener,
      std::string &SuggestedPredefines);

  /// Read the unhashed control block.
  ///
  /// This has no effect on \c F.Stream, instead creating a fresh cursor from
  /// \c F.Data and reading ahead.
  ASTReadResult readUnhashedControlBlock(ModuleFile &F, bool WasImportedBy,
                                         unsigned ClientLoadCapabilities);

  static ASTReadResult
  readUnhashedControlBlockImpl(ModuleFile *F, llvm::StringRef StreamData,
                               unsigned ClientLoadCapabilities,
                               bool AllowCompatibleConfigurationMismatch,
                               ASTReaderListener *Listener,
                               bool ValidateDiagnosticOptions);

  llvm::Error ReadASTBlock(ModuleFile &F, unsigned ClientLoadCapabilities);
  llvm::Error ReadExtensionBlock(ModuleFile &F);
  void ReadModuleOffsetMap(ModuleFile &F) const;
  void ParseLineTable(ModuleFile &F, const RecordData &Record);
  llvm::Error ReadSourceManagerBlock(ModuleFile &F);
  SourceLocation getImportLocation(ModuleFile *F);
  ASTReadResult ReadModuleMapFileBlock(RecordData &Record, ModuleFile &F,
                                       const ModuleFile *ImportedBy,
                                       unsigned ClientLoadCapabilities);
  llvm::Error ReadSubmoduleBlock(ModuleFile &F,
                                 unsigned ClientLoadCapabilities);
  static bool ParseLanguageOptions(const RecordData &Record, bool Complain,
                                   ASTReaderListener &Listener,
                                   bool AllowCompatibleDifferences);
  static bool ParseTargetOptions(const RecordData &Record, bool Complain,
                                 ASTReaderListener &Listener,
                                 bool AllowCompatibleDifferences);
  static bool ParseDiagnosticOptions(const RecordData &Record, bool Complain,
                                     ASTReaderListener &Listener);
  static bool ParseFileSystemOptions(const RecordData &Record, bool Complain,
                                     ASTReaderListener &Listener);
  static bool ParseHeaderSearchOptions(const RecordData &Record, bool Complain,
                                       ASTReaderListener &Listener);
  static bool ParseHeaderSearchPaths(const RecordData &Record, bool Complain,
                                     ASTReaderListener &Listener);
  static bool ParsePreprocessorOptions(const RecordData &Record, bool Complain,
                                       ASTReaderListener &Listener,
                                       std::string &SuggestedPredefines);

  struct RecordLocation {
    ModuleFile *F;
    uint64_t Offset;

    RecordLocation(ModuleFile *M, uint64_t O) : F(M), Offset(O) {}
  };

  QualType readTypeRecord(serialization::TypeID ID);
  RecordLocation TypeCursorForIndex(serialization::TypeID ID);
  void LoadedDecl(unsigned Index, Decl *D);
  Decl *ReadDeclRecord(GlobalDeclID ID);
  void markIncompleteDeclChain(Decl *D);

  /// Returns the most recent declaration of a declaration (which must be
  /// of a redeclarable kind) that is either local or has already been loaded
  /// merged into its redecl chain.
  Decl *getMostRecentExistingDecl(Decl *D);

  RecordLocation DeclCursorForID(GlobalDeclID ID, SourceLocation &Location);
  void loadDeclUpdateRecords(PendingUpdateRecord &Record);
  void loadPendingDeclChain(Decl *D, uint64_t LocalOffset);
  void loadObjCCategories(GlobalDeclID ID, ObjCInterfaceDecl *D,
                          unsigned PreviousGeneration = 0);

  RecordLocation getLocalBitOffset(uint64_t GlobalOffset);
  uint64_t getGlobalBitOffset(ModuleFile &M, uint64_t LocalOffset);

  /// Returns the first preprocessed entity ID that begins or ends after
  /// \arg Loc.
  serialization::PreprocessedEntityID
  findPreprocessedEntity(SourceLocation Loc, bool EndsAfter) const;

  /// Find the next module that contains entities and return the ID
  /// of the first entry.
  ///
  /// \param SLocMapI points at a chunk of a module that contains no
  /// preprocessed entities or the entities it contains are not the
  /// ones we are looking for.
  serialization::PreprocessedEntityID
    findNextPreprocessedEntity(
                        GlobalSLocOffsetMapType::const_iterator SLocMapI) const;

  /// Returns (ModuleFile, Local index) pair for \p GlobalIndex of a
  /// preprocessed entity.
  std::pair<ModuleFile *, unsigned>
    getModulePreprocessedEntity(unsigned GlobalIndex);

  /// Returns (begin, end) pair for the preprocessed entities of a
  /// particular module.
  llvm::iterator_range<PreprocessingRecord::iterator>
  getModulePreprocessedEntities(ModuleFile &Mod) const;

  bool canRecoverFromOutOfDate(StringRef ModuleFileName,
                               unsigned ClientLoadCapabilities);

public:
  class ModuleDeclIterator
      : public llvm::iterator_adaptor_base<
            ModuleDeclIterator, const serialization::unaligned_decl_id_t *,
            std::random_access_iterator_tag, const Decl *, ptrdiff_t,
            const Decl *, const Decl *> {
    ASTReader *Reader = nullptr;
    ModuleFile *Mod = nullptr;

  public:
    ModuleDeclIterator() : iterator_adaptor_base(nullptr) {}

    ModuleDeclIterator(ASTReader *Reader, ModuleFile *Mod,
                       const serialization::unaligned_decl_id_t *Pos)
        : iterator_adaptor_base(Pos), Reader(Reader), Mod(Mod) {}

    value_type operator*() const {
      LocalDeclID ID = LocalDeclID::get(*Reader, *Mod, *I);
      return Reader->GetDecl(Reader->getGlobalDeclID(*Mod, ID));
    }

    value_type operator->() const { return **this; }

    bool operator==(const ModuleDeclIterator &RHS) const {
      assert(Reader == RHS.Reader && Mod == RHS.Mod);
      return I == RHS.I;
    }
  };

  llvm::iterator_range<ModuleDeclIterator>
  getModuleFileLevelDecls(ModuleFile &Mod);

private:
  bool isConsumerInterestedIn(Decl *D);
  void PassInterestingDeclsToConsumer();
  void PassInterestingDeclToConsumer(Decl *D);
  void PassVTableToConsumer(CXXRecordDecl *RD);

  void finishPendingActions();
  void diagnoseOdrViolations();

  void pushExternalDeclIntoScope(NamedDecl *D, DeclarationName Name);

  void addPendingDeclContextInfo(Decl *D, GlobalDeclID SemaDC,
                                 GlobalDeclID LexicalDC) {
    assert(D);
    PendingDeclContextInfo Info = { D, SemaDC, LexicalDC };
    PendingDeclContextInfos.push_back(Info);
  }

  /// Produce an error diagnostic and return true.
  ///
  /// This routine should only be used for fatal errors that have to
  /// do with non-routine failures (e.g., corrupted AST file).
  void Error(StringRef Msg) const;
  void Error(unsigned DiagID, StringRef Arg1 = StringRef(),
             StringRef Arg2 = StringRef(), StringRef Arg3 = StringRef()) const;
  void Error(llvm::Error &&Err) const;

  /// Translate a \param GlobalDeclID to the index of DeclsLoaded array.
  unsigned translateGlobalDeclIDToIndex(GlobalDeclID ID) const;

  /// Translate an \param IdentifierID ID to the index of IdentifiersLoaded
  /// array and the corresponding module file.
  std::pair<ModuleFile *, unsigned>
  translateIdentifierIDToIndex(serialization::IdentifierID ID) const;

  /// Translate an \param TypeID ID to the index of TypesLoaded
  /// array and the corresponding module file.
  std::pair<ModuleFile *, unsigned>
  translateTypeIDToIndex(serialization::TypeID ID) const;

public:
  /// Load the AST file and validate its contents against the given
  /// Preprocessor.
  ///
  /// \param PP the preprocessor associated with the context in which this
  /// precompiled header will be loaded.
  ///
  /// \param Context the AST context that this precompiled header will be
  /// loaded into, if any.
  ///
  /// \param PCHContainerRdr the PCHContainerOperations to use for loading and
  /// creating modules.
  ///
  /// \param Extensions the list of module file extensions that can be loaded
  /// from the AST files.
  ///
  /// \param isysroot If non-NULL, the system include path specified by the
  /// user. This is only used with relocatable PCH files. If non-NULL,
  /// a relocatable PCH file will use the default path "/".
  ///
  /// \param DisableValidationKind If set, the AST reader will suppress most
  /// of its regular consistency checking, allowing the use of precompiled
  /// headers and module files that cannot be determined to be compatible.
  ///
  /// \param AllowASTWithCompilerErrors If true, the AST reader will accept an
  /// AST file the was created out of an AST with compiler errors,
  /// otherwise it will reject it.
  ///
  /// \param AllowConfigurationMismatch If true, the AST reader will not check
  /// for configuration differences between the AST file and the invocation.
  ///
  /// \param ValidateSystemInputs If true, the AST reader will validate
  /// system input files in addition to user input files. This is only
  /// meaningful if \p DisableValidation is false.
  ///
  /// \param UseGlobalIndex If true, the AST reader will try to load and use
  /// the global module index.
  ///
  /// \param ReadTimer If non-null, a timer used to track the time spent
  /// deserializing.
  ASTReader(Preprocessor &PP, InMemoryModuleCache &ModuleCache,
            ASTContext *Context, const PCHContainerReader &PCHContainerRdr,
            ArrayRef<std::shared_ptr<ModuleFileExtension>> Extensions,
            StringRef isysroot = "",
            DisableValidationForModuleKind DisableValidationKind =
                DisableValidationForModuleKind::None,
            bool AllowASTWithCompilerErrors = false,
            bool AllowConfigurationMismatch = false,
            bool ValidateSystemInputs = false,
            bool ValidateASTInputFilesContent = false,
            bool UseGlobalIndex = true,
            std::unique_ptr<llvm::Timer> ReadTimer = {});
  ASTReader(const ASTReader &) = delete;
  ASTReader &operator=(const ASTReader &) = delete;
  ~ASTReader() override;

  SourceManager &getSourceManager() const { return SourceMgr; }
  FileManager &getFileManager() const { return FileMgr; }
  DiagnosticsEngine &getDiags() const { return Diags; }

  /// Flags that indicate what kind of AST loading failures the client
  /// of the AST reader can directly handle.
  ///
  /// When a client states that it can handle a particular kind of failure,
  /// the AST reader will not emit errors when producing that kind of failure.
  enum LoadFailureCapabilities {
    /// The client can't handle any AST loading failures.
    ARR_None = 0,

    /// The client can handle an AST file that cannot load because it
    /// is missing.
    ARR_Missing = 0x1,

    /// The client can handle an AST file that cannot load because it
    /// is out-of-date relative to its input files.
    ARR_OutOfDate = 0x2,

    /// The client can handle an AST file that cannot load because it
    /// was built with a different version of Clang.
    ARR_VersionMismatch = 0x4,

    /// The client can handle an AST file that cannot load because it's
    /// compiled configuration doesn't match that of the context it was
    /// loaded into.
    ARR_ConfigurationMismatch = 0x8,

    /// If a module file is marked with errors treat it as out-of-date so the
    /// caller can rebuild it.
    ARR_TreatModuleWithErrorsAsOutOfDate = 0x10
  };

  /// Load the AST file designated by the given file name.
  ///
  /// \param FileName The name of the AST file to load.
  ///
  /// \param Type The kind of AST being loaded, e.g., PCH, module, main file,
  /// or preamble.
  ///
  /// \param ImportLoc the location where the module file will be considered as
  /// imported from. For non-module AST types it should be invalid.
  ///
  /// \param ClientLoadCapabilities The set of client load-failure
  /// capabilities, represented as a bitset of the enumerators of
  /// LoadFailureCapabilities.
  ///
  /// \param LoadedModuleFile The optional out-parameter refers to the new
  /// loaded modules. In case the module specified by FileName is already
  /// loaded, the module file pointer referred by NewLoadedModuleFile wouldn't
  /// change. Otherwise if the AST file get loaded successfully,
  /// NewLoadedModuleFile would refer to the address of the new loaded top level
  /// module. The state of NewLoadedModuleFile is unspecified if the AST file
  /// isn't loaded successfully.
  ASTReadResult ReadAST(StringRef FileName, ModuleKind Type,
                        SourceLocation ImportLoc,
                        unsigned ClientLoadCapabilities,
                        ModuleFile **NewLoadedModuleFile = nullptr);

  /// Make the entities in the given module and any of its (non-explicit)
  /// submodules visible to name lookup.
  ///
  /// \param Mod The module whose names should be made visible.
  ///
  /// \param NameVisibility The level of visibility to give the names in the
  /// module.  Visibility can only be increased over time.
  ///
  /// \param ImportLoc The location at which the import occurs.
  void makeModuleVisible(Module *Mod,
                         Module::NameVisibilityKind NameVisibility,
                         SourceLocation ImportLoc);

  /// Make the names within this set of hidden names visible.
  void makeNamesVisible(const HiddenNames &Names, Module *Owner);

  /// Note that MergedDef is a redefinition of the canonical definition
  /// Def, so Def should be visible whenever MergedDef is.
  void mergeDefinitionVisibility(NamedDecl *Def, NamedDecl *MergedDef);

  /// Take the AST callbacks listener.
  std::unique_ptr<ASTReaderListener> takeListener() {
    return std::move(Listener);
  }

  /// Set the AST callbacks listener.
  void setListener(std::unique_ptr<ASTReaderListener> Listener) {
    this->Listener = std::move(Listener);
  }

  /// Add an AST callback listener.
  ///
  /// Takes ownership of \p L.
  void addListener(std::unique_ptr<ASTReaderListener> L) {
    if (Listener)
      L = std::make_unique<ChainedASTReaderListener>(std::move(L),
                                                      std::move(Listener));
    Listener = std::move(L);
  }

  /// RAII object to temporarily add an AST callback listener.
  class ListenerScope {
    ASTReader &Reader;
    bool Chained = false;

  public:
    ListenerScope(ASTReader &Reader, std::unique_ptr<ASTReaderListener> L)
        : Reader(Reader) {
      auto Old = Reader.takeListener();
      if (Old) {
        Chained = true;
        L = std::make_unique<ChainedASTReaderListener>(std::move(L),
                                                        std::move(Old));
      }
      Reader.setListener(std::move(L));
    }

    ~ListenerScope() {
      auto New = Reader.takeListener();
      if (Chained)
        Reader.setListener(static_cast<ChainedASTReaderListener *>(New.get())
                               ->takeSecond());
    }
  };

  /// Set the AST deserialization listener.
  void setDeserializationListener(ASTDeserializationListener *Listener,
                                  bool TakeOwnership = false);

  /// Get the AST deserialization listener.
  ASTDeserializationListener *getDeserializationListener() {
    return DeserializationListener;
  }

  /// Determine whether this AST reader has a global index.
  bool hasGlobalIndex() const { return (bool)GlobalIndex; }

  /// Return global module index.
  GlobalModuleIndex *getGlobalIndex() { return GlobalIndex.get(); }

  /// Reset reader for a reload try.
  void resetForReload() { TriedLoadingGlobalIndex = false; }

  /// Attempts to load the global index.
  ///
  /// \returns true if loading the global index has failed for any reason.
  bool loadGlobalIndex();

  /// Determine whether we tried to load the global index, but failed,
  /// e.g., because it is out-of-date or does not exist.
  bool isGlobalIndexUnavailable() const;

  /// Initializes the ASTContext
  void InitializeContext();

  /// Update the state of Sema after loading some additional modules.
  void UpdateSema();

  /// Add in-memory (virtual file) buffer.
  void addInMemoryBuffer(StringRef &FileName,
                         std::unique_ptr<llvm::MemoryBuffer> Buffer) {
    ModuleMgr.addInMemoryBuffer(FileName, std::move(Buffer));
  }

  /// Finalizes the AST reader's state before writing an AST file to
  /// disk.
  ///
  /// This operation may undo temporary state in the AST that should not be
  /// emitted.
  void finalizeForWriting();

  /// Retrieve the module manager.
  ModuleManager &getModuleManager() { return ModuleMgr; }
  const ModuleManager &getModuleManager() const { return ModuleMgr; }

  /// Retrieve the preprocessor.
  Preprocessor &getPreprocessor() const { return PP; }

  /// Retrieve the name of the original source file name for the primary
  /// module file.
  StringRef getOriginalSourceFile() {
    return ModuleMgr.getPrimaryModule().OriginalSourceFileName;
  }

  /// Retrieve the name of the original source file name directly from
  /// the AST file, without actually loading the AST file.
  static std::string
  getOriginalSourceFile(const std::string &ASTFileName, FileManager &FileMgr,
                        const PCHContainerReader &PCHContainerRdr,
                        DiagnosticsEngine &Diags);

  /// Read the control block for the named AST file.
  ///
  /// \returns true if an error occurred, false otherwise.
  static bool readASTFileControlBlock(
      StringRef Filename, FileManager &FileMgr,
      const InMemoryModuleCache &ModuleCache,
      const PCHContainerReader &PCHContainerRdr, bool FindModuleFileExtensions,
      ASTReaderListener &Listener, bool ValidateDiagnosticOptions,
      unsigned ClientLoadCapabilities = ARR_ConfigurationMismatch |
                                        ARR_OutOfDate);

  /// Determine whether the given AST file is acceptable to load into a
  /// translation unit with the given language and target options.
  static bool isAcceptableASTFile(StringRef Filename, FileManager &FileMgr,
                                  const InMemoryModuleCache &ModuleCache,
                                  const PCHContainerReader &PCHContainerRdr,
                                  const LangOptions &LangOpts,
                                  const TargetOptions &TargetOpts,
                                  const PreprocessorOptions &PPOpts,
                                  StringRef ExistingModuleCachePath,
                                  bool RequireStrictOptionMatches = false);

  /// Returns the suggested contents of the predefines buffer,
  /// which contains a (typically-empty) subset of the predefines
  /// build prior to including the precompiled header.
  const std::string &getSuggestedPredefines() { return SuggestedPredefines; }

  /// Read a preallocated preprocessed entity from the external source.
  ///
  /// \returns null if an error occurred that prevented the preprocessed
  /// entity from being loaded.
  PreprocessedEntity *ReadPreprocessedEntity(unsigned Index) override;

  /// Returns a pair of [Begin, End) indices of preallocated
  /// preprocessed entities that \p Range encompasses.
  std::pair<unsigned, unsigned>
      findPreprocessedEntitiesInRange(SourceRange Range) override;

  /// Optionally returns true or false if the preallocated preprocessed
  /// entity with index \p Index came from file \p FID.
  std::optional<bool> isPreprocessedEntityInFileID(unsigned Index,
                                                   FileID FID) override;

  /// Read a preallocated skipped range from the external source.
  SourceRange ReadSkippedRange(unsigned Index) override;

  /// Read the header file information for the given file entry.
  HeaderFileInfo GetHeaderFileInfo(FileEntryRef FE) override;

  void ReadPragmaDiagnosticMappings(DiagnosticsEngine &Diag);

  /// Returns the number of source locations found in the chain.
  unsigned getTotalNumSLocs() const {
    return TotalNumSLocEntries;
  }

  /// Returns the number of identifiers found in the chain.
  unsigned getTotalNumIdentifiers() const {
    return static_cast<unsigned>(IdentifiersLoaded.size());
  }

  /// Returns the number of macros found in the chain.
  unsigned getTotalNumMacros() const {
    return static_cast<unsigned>(MacrosLoaded.size());
  }

  /// Returns the number of types found in the chain.
  unsigned getTotalNumTypes() const {
    return static_cast<unsigned>(TypesLoaded.size());
  }

  /// Returns the number of declarations found in the chain.
  unsigned getTotalNumDecls() const {
    return static_cast<unsigned>(DeclsLoaded.size());
  }

  /// Returns the number of submodules known.
  unsigned getTotalNumSubmodules() const {
    return static_cast<unsigned>(SubmodulesLoaded.size());
  }

  /// Returns the number of selectors found in the chain.
  unsigned getTotalNumSelectors() const {
    return static_cast<unsigned>(SelectorsLoaded.size());
  }

  /// Returns the number of preprocessed entities known to the AST
  /// reader.
  unsigned getTotalNumPreprocessedEntities() const {
    unsigned Result = 0;
    for (const auto &M : ModuleMgr)
      Result += M.NumPreprocessedEntities;
    return Result;
  }

  /// Resolve a type ID into a type, potentially building a new
  /// type.
  QualType GetType(serialization::TypeID ID);

  /// Resolve a local type ID within a given AST file into a type.
  QualType getLocalType(ModuleFile &F, serialization::LocalTypeID LocalID);

  /// Map a local type ID within a given AST file into a global type ID.
  serialization::TypeID
  getGlobalTypeID(ModuleFile &F, serialization::LocalTypeID LocalID) const;

  /// Read a type from the current position in the given record, which
  /// was read from the given AST file.
  QualType readType(ModuleFile &F, const RecordData &Record, unsigned &Idx) {
    if (Idx >= Record.size())
      return {};

    return getLocalType(F, Record[Idx++]);
  }

  /// Map from a local declaration ID within a given module to a
  /// global declaration ID.
  GlobalDeclID getGlobalDeclID(ModuleFile &F, LocalDeclID LocalID) const;

  /// Returns true if global DeclID \p ID originated from module \p M.
  bool isDeclIDFromModule(GlobalDeclID ID, ModuleFile &M) const;

  /// Retrieve the module file that owns the given declaration, or NULL
  /// if the declaration is not from a module file.
  ModuleFile *getOwningModuleFile(const Decl *D) const;
  ModuleFile *getOwningModuleFile(GlobalDeclID ID) const;

  /// Returns the source location for the decl \p ID.
  SourceLocation getSourceLocationForDeclID(GlobalDeclID ID);

  /// Resolve a declaration ID into a declaration, potentially
  /// building a new declaration.
  Decl *GetDecl(GlobalDeclID ID);
  Decl *GetExternalDecl(GlobalDeclID ID) override;

  /// Resolve a declaration ID into a declaration. Return 0 if it's not
  /// been loaded yet.
  Decl *GetExistingDecl(GlobalDeclID ID);

  /// Reads a declaration with the given local ID in the given module.
  Decl *GetLocalDecl(ModuleFile &F, LocalDeclID LocalID) {
    return GetDecl(getGlobalDeclID(F, LocalID));
  }

  /// Reads a declaration with the given local ID in the given module.
  ///
  /// \returns The requested declaration, casted to the given return type.
  template <typename T> T *GetLocalDeclAs(ModuleFile &F, LocalDeclID LocalID) {
    return cast_or_null<T>(GetLocalDecl(F, LocalID));
  }

  /// Map a global declaration ID into the declaration ID used to
  /// refer to this declaration within the given module fule.
  ///
  /// \returns the global ID of the given declaration as known in the given
  /// module file.
  LocalDeclID mapGlobalIDToModuleFileGlobalID(ModuleFile &M,
                                              GlobalDeclID GlobalID);

  /// Reads a declaration ID from the given position in a record in the
  /// given module.
  ///
  /// \returns The declaration ID read from the record, adjusted to a global ID.
  GlobalDeclID ReadDeclID(ModuleFile &F, const RecordDataImpl &Record,
                          unsigned &Idx);

  /// Reads a declaration from the given position in a record in the
  /// given module.
  Decl *ReadDecl(ModuleFile &F, const RecordDataImpl &R, unsigned &I) {
    return GetDecl(ReadDeclID(F, R, I));
  }

  /// Reads a declaration from the given position in a record in the
  /// given module.
  ///
  /// \returns The declaration read from this location, casted to the given
  /// result type.
  template <typename T>
  T *ReadDeclAs(ModuleFile &F, const RecordDataImpl &R, unsigned &I) {
    return cast_or_null<T>(GetDecl(ReadDeclID(F, R, I)));
  }

  /// If any redeclarations of \p D have been imported since it was
  /// last checked, this digs out those redeclarations and adds them to the
  /// redeclaration chain for \p D.
  void CompleteRedeclChain(const Decl *D) override;

  CXXBaseSpecifier *GetExternalCXXBaseSpecifiers(uint64_t Offset) override;

  /// Resolve the offset of a statement into a statement.
  ///
  /// This operation will read a new statement from the external
  /// source each time it is called, and is meant to be used via a
  /// LazyOffsetPtr (which is used by Decls for the body of functions, etc).
  Stmt *GetExternalDeclStmt(uint64_t Offset) override;

  /// ReadBlockAbbrevs - Enter a subblock of the specified BlockID with the
  /// specified cursor.  Read the abbreviations that are at the top of the block
  /// and then leave the cursor pointing into the block.
  static llvm::Error ReadBlockAbbrevs(llvm::BitstreamCursor &Cursor,
                                      unsigned BlockID,
                                      uint64_t *StartOfBlockOffset = nullptr);

  /// Finds all the visible declarations with a given name.
  /// The current implementation of this method just loads the entire
  /// lookup table as unmaterialized references.
  bool FindExternalVisibleDeclsByName(const DeclContext *DC,
                                      DeclarationName Name) override;

  /// Read all of the declarations lexically stored in a
  /// declaration context.
  ///
  /// \param DC The declaration context whose declarations will be
  /// read.
  ///
  /// \param IsKindWeWant A predicate indicating which declaration kinds
  /// we are interested in.
  ///
  /// \param Decls Vector that will contain the declarations loaded
  /// from the external source. The caller is responsible for merging
  /// these declarations with any declarations already stored in the
  /// declaration context.
  void
  FindExternalLexicalDecls(const DeclContext *DC,
                           llvm::function_ref<bool(Decl::Kind)> IsKindWeWant,
                           SmallVectorImpl<Decl *> &Decls) override;

  /// Get the decls that are contained in a file in the Offset/Length
  /// range. \p Length can be 0 to indicate a point at \p Offset instead of
  /// a range.
  void FindFileRegionDecls(FileID File, unsigned Offset, unsigned Length,
                           SmallVectorImpl<Decl *> &Decls) override;

  /// Notify ASTReader that we started deserialization of
  /// a decl or type so until FinishedDeserializing is called there may be
  /// decls that are initializing. Must be paired with FinishedDeserializing.
  void StartedDeserializing() override;

  /// Notify ASTReader that we finished the deserialization of
  /// a decl or type. Must be paired with StartedDeserializing.
  void FinishedDeserializing() override;

  /// Function that will be invoked when we begin parsing a new
  /// translation unit involving this external AST source.
  ///
  /// This function will provide all of the external definitions to
  /// the ASTConsumer.
  void StartTranslationUnit(ASTConsumer *Consumer) override;

  /// Print some statistics about AST usage.
  void PrintStats() override;

  /// Dump information about the AST reader to standard error.
  void dump();

  /// Return the amount of memory used by memory buffers, breaking down
  /// by heap-backed versus mmap'ed memory.
  void getMemoryBufferSizes(MemoryBufferSizes &sizes) const override;

  /// Initialize the semantic source with the Sema instance
  /// being used to perform semantic analysis on the abstract syntax
  /// tree.
  void InitializeSema(Sema &S) override;

  /// Inform the semantic consumer that Sema is no longer available.
  void ForgetSema() override { SemaObj = nullptr; }

  /// Retrieve the IdentifierInfo for the named identifier.
  ///
  /// This routine builds a new IdentifierInfo for the given identifier. If any
  /// declarations with this name are visible from translation unit scope, their
  /// declarations will be deserialized and introduced into the declaration
  /// chain of the identifier.
  IdentifierInfo *get(StringRef Name) override;

  /// Retrieve an iterator into the set of all identifiers
  /// in all loaded AST files.
  IdentifierIterator *getIdentifiers() override;

  /// Load the contents of the global method pool for a given
  /// selector.
  void ReadMethodPool(Selector Sel) override;

  /// Load the contents of the global method pool for a given
  /// selector if necessary.
  void updateOutOfDateSelector(Selector Sel) override;

  /// Load the set of namespaces that are known to the external source,
  /// which will be used during typo correction.
  void ReadKnownNamespaces(
                         SmallVectorImpl<NamespaceDecl *> &Namespaces) override;

  void ReadUndefinedButUsed(
      llvm::MapVector<NamedDecl *, SourceLocation> &Undefined) override;

  void ReadMismatchingDeleteExpressions(llvm::MapVector<
      FieldDecl *, llvm::SmallVector<std::pair<SourceLocation, bool>, 4>> &
                                            Exprs) override;

  void ReadTentativeDefinitions(
                            SmallVectorImpl<VarDecl *> &TentativeDefs) override;

  void ReadUnusedFileScopedDecls(
                       SmallVectorImpl<const DeclaratorDecl *> &Decls) override;

  void ReadDelegatingConstructors(
                         SmallVectorImpl<CXXConstructorDecl *> &Decls) override;

  void ReadExtVectorDecls(SmallVectorImpl<TypedefNameDecl *> &Decls) override;

  void ReadUnusedLocalTypedefNameCandidates(
      llvm::SmallSetVector<const TypedefNameDecl *, 4> &Decls) override;

  void ReadDeclsToCheckForDeferredDiags(
      llvm::SmallSetVector<Decl *, 4> &Decls) override;

  void ReadReferencedSelectors(
           SmallVectorImpl<std::pair<Selector, SourceLocation>> &Sels) override;

  void ReadWeakUndeclaredIdentifiers(
      SmallVectorImpl<std::pair<IdentifierInfo *, WeakInfo>> &WeakIDs) override;

  void ReadUsedVTables(SmallVectorImpl<ExternalVTableUse> &VTables) override;

  void ReadPendingInstantiations(
                  SmallVectorImpl<std::pair<ValueDecl *,
                                            SourceLocation>> &Pending) override;

  void ReadLateParsedTemplates(
      llvm::MapVector<const FunctionDecl *, std::unique_ptr<LateParsedTemplate>>
          &LPTMap) override;

  void AssignedLambdaNumbering(const CXXRecordDecl *Lambda) override;

  /// Load a selector from disk, registering its ID if it exists.
  void LoadSelector(Selector Sel);

  void SetIdentifierInfo(serialization::IdentifierID ID, IdentifierInfo *II);
  void SetGloballyVisibleDecls(IdentifierInfo *II,
                               const SmallVectorImpl<GlobalDeclID> &DeclIDs,
                               SmallVectorImpl<Decl *> *Decls = nullptr);

  /// Report a diagnostic.
  DiagnosticBuilder Diag(unsigned DiagID) const;

  /// Report a diagnostic.
  DiagnosticBuilder Diag(SourceLocation Loc, unsigned DiagID) const;

  void warnStackExhausted(SourceLocation Loc);

  IdentifierInfo *DecodeIdentifierInfo(serialization::IdentifierID ID);

  IdentifierInfo *readIdentifier(ModuleFile &M, const RecordData &Record,
                                 unsigned &Idx) {
    return DecodeIdentifierInfo(getGlobalIdentifierID(M, Record[Idx++]));
  }

  IdentifierInfo *GetIdentifier(serialization::IdentifierID ID) override {
    // Note that we are loading an identifier.
    Deserializing AnIdentifier(this);

    return DecodeIdentifierInfo(ID);
  }

  IdentifierInfo *getLocalIdentifier(ModuleFile &M, uint64_t LocalID);

  serialization::IdentifierID getGlobalIdentifierID(ModuleFile &M,
                                                    uint64_t LocalID);

  void resolvePendingMacro(IdentifierInfo *II, const PendingMacroInfo &PMInfo);

  /// Retrieve the macro with the given ID.
  MacroInfo *getMacro(serialization::MacroID ID);

  /// Retrieve the global macro ID corresponding to the given local
  /// ID within the given module file.
  serialization::MacroID getGlobalMacroID(ModuleFile &M, unsigned LocalID);

  /// Read the source location entry with index ID.
  bool ReadSLocEntry(int ID) override;
  /// Get the index ID for the loaded SourceLocation offset.
  int getSLocEntryID(SourceLocation::UIntTy SLocOffset) override;
  /// Try to read the offset of the SLocEntry at the given index in the given
  /// module file.
  llvm::Expected<SourceLocation::UIntTy> readSLocOffset(ModuleFile *F,
                                                        unsigned Index);

  /// Retrieve the module import location and module name for the
  /// given source manager entry ID.
  std::pair<SourceLocation, StringRef> getModuleImportLoc(int ID) override;

  /// Retrieve the global submodule ID given a module and its local ID
  /// number.
  serialization::SubmoduleID getGlobalSubmoduleID(ModuleFile &M,
                                                  unsigned LocalID) const;

  /// Retrieve the submodule that corresponds to a global submodule ID.
  ///
  Module *getSubmodule(serialization::SubmoduleID GlobalID);

  /// Retrieve the module that corresponds to the given module ID.
  ///
  /// Note: overrides method in ExternalASTSource
  Module *getModule(unsigned ID) override;

  /// Retrieve the module file with a given local ID within the specified
  /// ModuleFile.
  ModuleFile *getLocalModuleFile(ModuleFile &M, unsigned ID) const;

  /// Get an ID for the given module file.
  unsigned getModuleFileID(ModuleFile *M);

  /// Return a descriptor for the corresponding module.
  std::optional<ASTSourceDescriptor> getSourceDescriptor(unsigned ID) override;

  ExtKind hasExternalDefinitions(const Decl *D) override;

  /// Retrieve a selector from the given module with its local ID
  /// number.
  Selector getLocalSelector(ModuleFile &M, unsigned LocalID);

  Selector DecodeSelector(serialization::SelectorID Idx);

  Selector GetExternalSelector(serialization::SelectorID ID) override;
  uint32_t GetNumExternalSelectors() override;

  Selector ReadSelector(ModuleFile &M, const RecordData &Record, unsigned &Idx) {
    return getLocalSelector(M, Record[Idx++]);
  }

  /// Retrieve the global selector ID that corresponds to this
  /// the local selector ID in a given module.
  serialization::SelectorID getGlobalSelectorID(ModuleFile &M,
                                                unsigned LocalID) const;

  /// Read the contents of a CXXCtorInitializer array.
  CXXCtorInitializer **GetExternalCXXCtorInitializers(uint64_t Offset) override;

  /// Read a AlignPackInfo from raw form.
  Sema::AlignPackInfo ReadAlignPackInfo(uint32_t Raw) const {
    return Sema::AlignPackInfo::getFromRawEncoding(Raw);
  }

  using RawLocEncoding = SourceLocationEncoding::RawLocEncoding;

  /// Read a source location from raw form and return it in its
  /// originating module file's source location space.
  std::pair<SourceLocation, unsigned>
  ReadUntranslatedSourceLocation(RawLocEncoding Raw,
                                 LocSeq *Seq = nullptr) const {
    return SourceLocationEncoding::decode(Raw, Seq);
  }

  /// Read a source location from raw form.
  SourceLocation ReadSourceLocation(ModuleFile &MF, RawLocEncoding Raw,
                                    LocSeq *Seq = nullptr) const {
    if (!MF.ModuleOffsetMap.empty())
      ReadModuleOffsetMap(MF);

    auto [Loc, ModuleFileIndex] = ReadUntranslatedSourceLocation(Raw, Seq);
    ModuleFile *OwningModuleFile =
        ModuleFileIndex == 0 ? &MF : MF.TransitiveImports[ModuleFileIndex - 1];

    assert(!SourceMgr.isLoadedSourceLocation(Loc) &&
           "Run out source location space");

    return TranslateSourceLocation(*OwningModuleFile, Loc);
  }

  /// Translate a source location from another module file's source
  /// location space into ours.
  SourceLocation TranslateSourceLocation(ModuleFile &ModuleFile,
                                         SourceLocation Loc) const {
    if (Loc.isInvalid())
      return Loc;

    // FIXME: TranslateSourceLocation is not re-enterable. It is problematic
    // to call TranslateSourceLocation on a translated source location.
    // We either need a method to know whether or not a source location is
    // translated or refactor the code to make it clear that
    // TranslateSourceLocation won't be called with translated source location.

    return Loc.getLocWithOffset(ModuleFile.SLocEntryBaseOffset - 2);
  }

  /// Read a source location.
  SourceLocation ReadSourceLocation(ModuleFile &ModuleFile,
                                    const RecordDataImpl &Record, unsigned &Idx,
                                    LocSeq *Seq = nullptr) {
    return ReadSourceLocation(ModuleFile, Record[Idx++], Seq);
  }

  /// Read a FileID.
  FileID ReadFileID(ModuleFile &F, const RecordDataImpl &Record,
                    unsigned &Idx) const {
    return TranslateFileID(F, FileID::get(Record[Idx++]));
  }

  /// Translate a FileID from another module file's FileID space into ours.
  FileID TranslateFileID(ModuleFile &F, FileID FID) const {
    assert(FID.ID >= 0 && "Reading non-local FileID.");
    return FileID::get(F.SLocEntryBaseID + FID.ID - 1);
  }

  /// Read a source range.
  SourceRange ReadSourceRange(ModuleFile &F, const RecordData &Record,
                              unsigned &Idx, LocSeq *Seq = nullptr);

  static llvm::BitVector ReadBitVector(const RecordData &Record,
                                       const StringRef Blob);

  // Read a string
  static std::string ReadString(const RecordDataImpl &Record, unsigned &Idx);

  // Skip a string
  static void SkipString(const RecordData &Record, unsigned &Idx) {
    Idx += Record[Idx] + 1;
  }

  // Read a path
  std::string ReadPath(ModuleFile &F, const RecordData &Record, unsigned &Idx);

  // Read a path
  std::string ReadPath(StringRef BaseDirectory, const RecordData &Record,
                       unsigned &Idx);

  // Skip a path
  static void SkipPath(const RecordData &Record, unsigned &Idx) {
    SkipString(Record, Idx);
  }

  /// Read a version tuple.
  static VersionTuple ReadVersionTuple(const RecordData &Record, unsigned &Idx);

  CXXTemporary *ReadCXXTemporary(ModuleFile &F, const RecordData &Record,
                                 unsigned &Idx);

  /// Reads a statement.
  Stmt *ReadStmt(ModuleFile &F);

  /// Reads an expression.
  Expr *ReadExpr(ModuleFile &F);

  /// Reads a sub-statement operand during statement reading.
  Stmt *ReadSubStmt() {
    assert(ReadingKind == Read_Stmt &&
           "Should be called only during statement reading!");
    // Subexpressions are stored from last to first, so the next Stmt we need
    // is at the back of the stack.
    assert(!StmtStack.empty() && "Read too many sub-statements!");
    return StmtStack.pop_back_val();
  }

  /// Reads a sub-expression operand during statement reading.
  Expr *ReadSubExpr();

  /// Reads a token out of a record.
  Token ReadToken(ModuleFile &M, const RecordDataImpl &Record, unsigned &Idx);

  /// Reads the macro record located at the given offset.
  MacroInfo *ReadMacroRecord(ModuleFile &F, uint64_t Offset);

  /// Determine the global preprocessed entity ID that corresponds to
  /// the given local ID within the given module.
  serialization::PreprocessedEntityID
  getGlobalPreprocessedEntityID(ModuleFile &M, unsigned LocalID) const;

  /// Add a macro to deserialize its macro directive history.
  ///
  /// \param II The name of the macro.
  /// \param M The module file.
  /// \param MacroDirectivesOffset Offset of the serialized macro directive
  /// history.
  void addPendingMacro(IdentifierInfo *II, ModuleFile *M,
                       uint32_t MacroDirectivesOffset);

  /// Read the set of macros defined by this external macro source.
  void ReadDefinedMacros() override;

  /// Update an out-of-date identifier.
  void updateOutOfDateIdentifier(const IdentifierInfo &II) override;

  /// Note that this identifier is up-to-date.
  void markIdentifierUpToDate(const IdentifierInfo *II);

  /// Load all external visible decls in the given DeclContext.
  void completeVisibleDeclsMap(const DeclContext *DC) override;

  /// Retrieve the AST context that this AST reader supplements.
  ASTContext &getContext() {
    assert(ContextObj && "requested AST context when not loading AST");
    return *ContextObj;
  }

  // Contains the IDs for declarations that were requested before we have
  // access to a Sema object.
  SmallVector<GlobalDeclID, 16> PreloadedDeclIDs;

  /// Retrieve the semantic analysis object used to analyze the
  /// translation unit in which the precompiled header is being
  /// imported.
  Sema *getSema() { return SemaObj; }

  /// Get the identifier resolver used for name lookup / updates
  /// in the translation unit scope. We have one of these even if we don't
  /// have a Sema object.
  IdentifierResolver &getIdResolver();

  /// Retrieve the identifier table associated with the
  /// preprocessor.
  IdentifierTable &getIdentifierTable();

  /// Record that the given ID maps to the given switch-case
  /// statement.
  void RecordSwitchCaseID(SwitchCase *SC, unsigned ID);

  /// Retrieve the switch-case statement with the given ID.
  SwitchCase *getSwitchCaseWithID(unsigned ID);

  void ClearSwitchCaseIDs();

  /// Cursors for comments blocks.
  SmallVector<std::pair<llvm::BitstreamCursor,
                        serialization::ModuleFile *>, 8> CommentsCursors;

  /// Loads comments ranges.
  void ReadComments() override;

  /// Visit all the input file infos of the given module file.
  void visitInputFileInfos(
      serialization::ModuleFile &MF, bool IncludeSystem,
      llvm::function_ref<void(const serialization::InputFileInfo &IFI,
                              bool IsSystem)>
          Visitor);

  /// Visit all the input files of the given module file.
  void visitInputFiles(serialization::ModuleFile &MF,
                       bool IncludeSystem, bool Complain,
          llvm::function_ref<void(const serialization::InputFile &IF,
                                  bool isSystem)> Visitor);

  /// Visit all the top-level module maps loaded when building the given module
  /// file.
  void visitTopLevelModuleMaps(serialization::ModuleFile &MF,
                               llvm::function_ref<void(FileEntryRef)> Visitor);

  bool isProcessingUpdateRecords() { return ProcessingUpdateRecords; }
};

/// A simple helper class to unpack an integer to bits and consuming
/// the bits in order.
class BitsUnpacker {
  constexpr static uint32_t BitsIndexUpbound = 32;

public:
  BitsUnpacker(uint32_t V) { updateValue(V); }
  BitsUnpacker(const BitsUnpacker &) = delete;
  BitsUnpacker(BitsUnpacker &&) = delete;
  BitsUnpacker operator=(const BitsUnpacker &) = delete;
  BitsUnpacker operator=(BitsUnpacker &&) = delete;
  ~BitsUnpacker() = default;

  void updateValue(uint32_t V) {
    Value = V;
    CurrentBitsIndex = 0;
  }

  void advance(uint32_t BitsWidth) { CurrentBitsIndex += BitsWidth; }

  bool getNextBit() {
    assert(isValid());
    return Value & (1 << CurrentBitsIndex++);
  }

  uint32_t getNextBits(uint32_t Width) {
    assert(isValid());
    assert(Width < BitsIndexUpbound);
    uint32_t Ret = (Value >> CurrentBitsIndex) & ((1 << Width) - 1);
    CurrentBitsIndex += Width;
    return Ret;
  }

  bool canGetNextNBits(uint32_t Width) const {
    return CurrentBitsIndex + Width < BitsIndexUpbound;
  }

private:
  bool isValid() const { return CurrentBitsIndex < BitsIndexUpbound; }

  uint32_t Value;
  uint32_t CurrentBitsIndex = ~0;
};

inline bool shouldSkipCheckingODR(const Decl *D) {
  return D->getASTContext().getLangOpts().SkipODRCheckInGMF &&
         D->isFromExplicitGlobalModule();
}

} // namespace clang

#endif // LLVM_CLANG_SERIALIZATION_ASTREADER_H

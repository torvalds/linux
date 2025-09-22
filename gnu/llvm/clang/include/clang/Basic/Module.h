//===- Module.h - Describe a module -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Defines the clang::Module class, which describes a module in the
/// source code.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_MODULE_H
#define LLVM_CLANG_BASIC_MODULE_H

#include "clang/Basic/DirectoryEntry.h"
#include "clang/Basic/FileEntry.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <ctime>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace llvm {

class raw_ostream;

} // namespace llvm

namespace clang {

class FileManager;
class LangOptions;
class TargetInfo;

/// Describes the name of a module.
using ModuleId = SmallVector<std::pair<std::string, SourceLocation>, 2>;

/// The signature of a module, which is a hash of the AST content.
struct ASTFileSignature : std::array<uint8_t, 20> {
  using BaseT = std::array<uint8_t, 20>;

  static constexpr size_t size = std::tuple_size<BaseT>::value;

  ASTFileSignature(BaseT S = {{0}}) : BaseT(std::move(S)) {}

  explicit operator bool() const { return *this != BaseT({{0}}); }

  /// Returns the value truncated to the size of an uint64_t.
  uint64_t truncatedValue() const {
    uint64_t Value = 0;
    static_assert(sizeof(*this) >= sizeof(uint64_t), "No need to truncate.");
    for (unsigned I = 0; I < sizeof(uint64_t); ++I)
      Value |= static_cast<uint64_t>((*this)[I]) << (I * 8);
    return Value;
  }

  static ASTFileSignature create(std::array<uint8_t, 20> Bytes) {
    return ASTFileSignature(std::move(Bytes));
  }

  static ASTFileSignature createDISentinel() {
    ASTFileSignature Sentinel;
    Sentinel.fill(0xFF);
    return Sentinel;
  }

  static ASTFileSignature createDummy() {
    ASTFileSignature Dummy;
    Dummy.fill(0x00);
    return Dummy;
  }

  template <typename InputIt>
  static ASTFileSignature create(InputIt First, InputIt Last) {
    assert(std::distance(First, Last) == size &&
           "Wrong amount of bytes to create an ASTFileSignature");

    ASTFileSignature Signature;
    std::copy(First, Last, Signature.begin());
    return Signature;
  }
};

/// Describes a module or submodule.
///
/// Aligned to 8 bytes to allow for llvm::PointerIntPair<Module *, 3>.
class alignas(8) Module {
public:
  /// The name of this module.
  std::string Name;

  /// The location of the module definition.
  SourceLocation DefinitionLoc;

  // FIXME: Consider if reducing the size of this enum (having Partition and
  // Named modules only) then representing interface/implementation separately
  // is more efficient.
  enum ModuleKind {
    /// This is a module that was defined by a module map and built out
    /// of header files.
    ModuleMapModule,

    /// This is a C++20 header unit.
    ModuleHeaderUnit,

    /// This is a C++20 module interface unit.
    ModuleInterfaceUnit,

    /// This is a C++20 module implementation unit.
    ModuleImplementationUnit,

    /// This is a C++20 module partition interface.
    ModulePartitionInterface,

    /// This is a C++20 module partition implementation.
    ModulePartitionImplementation,

    /// This is the explicit Global Module Fragment of a modular TU.
    /// As per C++ [module.global.frag].
    ExplicitGlobalModuleFragment,

    /// This is the private module fragment within some C++ module.
    PrivateModuleFragment,

    /// This is an implicit fragment of the global module which contains
    /// only language linkage declarations (made in the purview of the
    /// named module).
    ImplicitGlobalModuleFragment,
  };

  /// The kind of this module.
  ModuleKind Kind = ModuleMapModule;

  /// The parent of this module. This will be NULL for the top-level
  /// module.
  Module *Parent;

  /// The build directory of this module. This is the directory in
  /// which the module is notionally built, and relative to which its headers
  /// are found.
  OptionalDirectoryEntryRef Directory;

  /// The presumed file name for the module map defining this module.
  /// Only non-empty when building from preprocessed source.
  std::string PresumedModuleMapFile;

  /// The umbrella header or directory.
  std::variant<std::monostate, FileEntryRef, DirectoryEntryRef> Umbrella;

  /// The module signature.
  ASTFileSignature Signature;

  /// The name of the umbrella entry, as written in the module map.
  std::string UmbrellaAsWritten;

  // The path to the umbrella entry relative to the root module's \c Directory.
  std::string UmbrellaRelativeToRootModuleDirectory;

  /// The module through which entities defined in this module will
  /// eventually be exposed, for use in "private" modules.
  std::string ExportAsModule;

  /// For the debug info, the path to this module's .apinotes file, if any.
  std::string APINotesFile;

  /// Does this Module is a named module of a standard named module?
  bool isNamedModule() const {
    switch (Kind) {
    case ModuleInterfaceUnit:
    case ModuleImplementationUnit:
    case ModulePartitionInterface:
    case ModulePartitionImplementation:
    case PrivateModuleFragment:
      return true;
    default:
      return false;
    }
  }

  /// Does this Module scope describe a fragment of the global module within
  /// some C++ module.
  bool isGlobalModule() const {
    return isExplicitGlobalModule() || isImplicitGlobalModule();
  }
  bool isExplicitGlobalModule() const {
    return Kind == ExplicitGlobalModuleFragment;
  }
  bool isImplicitGlobalModule() const {
    return Kind == ImplicitGlobalModuleFragment;
  }

  bool isPrivateModule() const { return Kind == PrivateModuleFragment; }

  bool isModuleMapModule() const { return Kind == ModuleMapModule; }

private:
  /// The submodules of this module, indexed by name.
  std::vector<Module *> SubModules;

  /// A mapping from the submodule name to the index into the
  /// \c SubModules vector at which that submodule resides.
  llvm::StringMap<unsigned> SubModuleIndex;

  /// The AST file if this is a top-level module which has a
  /// corresponding serialized AST file, or null otherwise.
  OptionalFileEntryRef ASTFile;

  /// The top-level headers associated with this module.
  llvm::SmallSetVector<FileEntryRef, 2> TopHeaders;

  /// top-level header filenames that aren't resolved to FileEntries yet.
  std::vector<std::string> TopHeaderNames;

  /// Cache of modules visible to lookup in this module.
  mutable llvm::DenseSet<const Module*> VisibleModulesCache;

  /// The ID used when referencing this module within a VisibleModuleSet.
  unsigned VisibilityID;

public:
  enum HeaderKind {
    HK_Normal,
    HK_Textual,
    HK_Private,
    HK_PrivateTextual,
    HK_Excluded
  };
  static const int NumHeaderKinds = HK_Excluded + 1;

  /// Information about a header directive as found in the module map
  /// file.
  struct Header {
    std::string NameAsWritten;
    std::string PathRelativeToRootModuleDirectory;
    FileEntryRef Entry;
  };

  /// Information about a directory name as found in the module map
  /// file.
  struct DirectoryName {
    std::string NameAsWritten;
    std::string PathRelativeToRootModuleDirectory;
    DirectoryEntryRef Entry;
  };

  /// The headers that are part of this module.
  SmallVector<Header, 2> Headers[5];

  /// Stored information about a header directive that was found in the
  /// module map file but has not been resolved to a file.
  struct UnresolvedHeaderDirective {
    HeaderKind Kind = HK_Normal;
    SourceLocation FileNameLoc;
    std::string FileName;
    bool IsUmbrella = false;
    bool HasBuiltinHeader = false;
    std::optional<off_t> Size;
    std::optional<time_t> ModTime;
  };

  /// Headers that are mentioned in the module map file but that we have not
  /// yet attempted to resolve to a file on the file system.
  SmallVector<UnresolvedHeaderDirective, 1> UnresolvedHeaders;

  /// Headers that are mentioned in the module map file but could not be
  /// found on the file system.
  SmallVector<UnresolvedHeaderDirective, 1> MissingHeaders;

  struct Requirement {
    std::string FeatureName;
    bool RequiredState;
  };

  /// The set of language features required to use this module.
  ///
  /// If any of these requirements are not available, the \c IsAvailable bit
  /// will be false to indicate that this (sub)module is not available.
  SmallVector<Requirement, 2> Requirements;

  /// A module with the same name that shadows this module.
  Module *ShadowingModule = nullptr;

  /// Whether this module has declared itself unimportable, either because
  /// it's missing a requirement from \p Requirements or because it's been
  /// shadowed by another module.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsUnimportable : 1;

  /// Whether we tried and failed to load a module file for this module.
  LLVM_PREFERRED_TYPE(bool)
  unsigned HasIncompatibleModuleFile : 1;

  /// Whether this module is available in the current translation unit.
  ///
  /// If the module is missing headers or does not meet all requirements then
  /// this bit will be 0.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsAvailable : 1;

  /// Whether this module was loaded from a module file.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsFromModuleFile : 1;

  /// Whether this is a framework module.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsFramework : 1;

  /// Whether this is an explicit submodule.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsExplicit : 1;

  /// Whether this is a "system" module (which assumes that all
  /// headers in it are system headers).
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsSystem : 1;

  /// Whether this is an 'extern "C"' module (which implicitly puts all
  /// headers in it within an 'extern "C"' block, and allows the module to be
  /// imported within such a block).
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsExternC : 1;

  /// Whether this is an inferred submodule (module * { ... }).
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsInferred : 1;

  /// Whether we should infer submodules for this module based on
  /// the headers.
  ///
  /// Submodules can only be inferred for modules with an umbrella header.
  LLVM_PREFERRED_TYPE(bool)
  unsigned InferSubmodules : 1;

  /// Whether, when inferring submodules, the inferred submodules
  /// should be explicit.
  LLVM_PREFERRED_TYPE(bool)
  unsigned InferExplicitSubmodules : 1;

  /// Whether, when inferring submodules, the inferr submodules should
  /// export all modules they import (e.g., the equivalent of "export *").
  LLVM_PREFERRED_TYPE(bool)
  unsigned InferExportWildcard : 1;

  /// Whether the set of configuration macros is exhaustive.
  ///
  /// When the set of configuration macros is exhaustive, meaning
  /// that no identifier not in this list should affect how the module is
  /// built.
  LLVM_PREFERRED_TYPE(bool)
  unsigned ConfigMacrosExhaustive : 1;

  /// Whether files in this module can only include non-modular headers
  /// and headers from used modules.
  LLVM_PREFERRED_TYPE(bool)
  unsigned NoUndeclaredIncludes : 1;

  /// Whether this module came from a "private" module map, found next
  /// to a regular (public) module map.
  LLVM_PREFERRED_TYPE(bool)
  unsigned ModuleMapIsPrivate : 1;

  /// Whether this C++20 named modules doesn't need an initializer.
  /// This is only meaningful for C++20 modules.
  LLVM_PREFERRED_TYPE(bool)
  unsigned NamedModuleHasInit : 1;

  /// Describes the visibility of the various names within a
  /// particular module.
  enum NameVisibilityKind {
    /// All of the names in this module are hidden.
    Hidden,
    /// All of the names in this module are visible.
    AllVisible
  };

  /// The visibility of names within this particular module.
  NameVisibilityKind NameVisibility;

  /// The location of the inferred submodule.
  SourceLocation InferredSubmoduleLoc;

  /// The set of modules imported by this module, and on which this
  /// module depends.
  llvm::SmallSetVector<Module *, 2> Imports;

  /// The set of top-level modules that affected the compilation of this module,
  /// but were not imported.
  llvm::SmallSetVector<Module *, 2> AffectingClangModules;

  /// Describes an exported module.
  ///
  /// The pointer is the module being re-exported, while the bit will be true
  /// to indicate that this is a wildcard export.
  using ExportDecl = llvm::PointerIntPair<Module *, 1, bool>;

  /// The set of export declarations.
  SmallVector<ExportDecl, 2> Exports;

  /// Describes an exported module that has not yet been resolved
  /// (perhaps because the module it refers to has not yet been loaded).
  struct UnresolvedExportDecl {
    /// The location of the 'export' keyword in the module map file.
    SourceLocation ExportLoc;

    /// The name of the module.
    ModuleId Id;

    /// Whether this export declaration ends in a wildcard, indicating
    /// that all of its submodules should be exported (rather than the named
    /// module itself).
    bool Wildcard;
  };

  /// The set of export declarations that have yet to be resolved.
  SmallVector<UnresolvedExportDecl, 2> UnresolvedExports;

  /// The directly used modules.
  SmallVector<Module *, 2> DirectUses;

  /// The set of use declarations that have yet to be resolved.
  SmallVector<ModuleId, 2> UnresolvedDirectUses;

  /// When \c NoUndeclaredIncludes is true, the set of modules this module tried
  /// to import but didn't because they are not direct uses.
  llvm::SmallSetVector<const Module *, 2> UndeclaredUses;

  /// A library or framework to link against when an entity from this
  /// module is used.
  struct LinkLibrary {
    LinkLibrary() = default;
    LinkLibrary(const std::string &Library, bool IsFramework)
        : Library(Library), IsFramework(IsFramework) {}

    /// The library to link against.
    ///
    /// This will typically be a library or framework name, but can also
    /// be an absolute path to the library or framework.
    std::string Library;

    /// Whether this is a framework rather than a library.
    bool IsFramework = false;
  };

  /// The set of libraries or frameworks to link against when
  /// an entity from this module is used.
  llvm::SmallVector<LinkLibrary, 2> LinkLibraries;

  /// Autolinking uses the framework name for linking purposes
  /// when this is false and the export_as name otherwise.
  bool UseExportAsModuleLinkName = false;

  /// The set of "configuration macros", which are macros that
  /// (intentionally) change how this module is built.
  std::vector<std::string> ConfigMacros;

  /// An unresolved conflict with another module.
  struct UnresolvedConflict {
    /// The (unresolved) module id.
    ModuleId Id;

    /// The message provided to the user when there is a conflict.
    std::string Message;
  };

  /// The list of conflicts for which the module-id has not yet been
  /// resolved.
  std::vector<UnresolvedConflict> UnresolvedConflicts;

  /// A conflict between two modules.
  struct Conflict {
    /// The module that this module conflicts with.
    Module *Other;

    /// The message provided to the user when there is a conflict.
    std::string Message;
  };

  /// The list of conflicts.
  std::vector<Conflict> Conflicts;

  /// Construct a new module or submodule.
  Module(StringRef Name, SourceLocation DefinitionLoc, Module *Parent,
         bool IsFramework, bool IsExplicit, unsigned VisibilityID);

  ~Module();

  /// Determine whether this module has been declared unimportable.
  bool isUnimportable() const { return IsUnimportable; }

  /// Determine whether this module has been declared unimportable.
  ///
  /// \param LangOpts The language options used for the current
  /// translation unit.
  ///
  /// \param Target The target options used for the current translation unit.
  ///
  /// \param Req If this module is unimportable because of a missing
  /// requirement, this parameter will be set to one of the requirements that
  /// is not met for use of this module.
  ///
  /// \param ShadowingModule If this module is unimportable because it is
  /// shadowed, this parameter will be set to the shadowing module.
  bool isUnimportable(const LangOptions &LangOpts, const TargetInfo &Target,
                      Requirement &Req, Module *&ShadowingModule) const;

  /// Determine whether this module can be built in this compilation.
  bool isForBuilding(const LangOptions &LangOpts) const;

  /// Determine whether this module is available for use within the
  /// current translation unit.
  bool isAvailable() const { return IsAvailable; }

  /// Determine whether this module is available for use within the
  /// current translation unit.
  ///
  /// \param LangOpts The language options used for the current
  /// translation unit.
  ///
  /// \param Target The target options used for the current translation unit.
  ///
  /// \param Req If this module is unavailable because of a missing requirement,
  /// this parameter will be set to one of the requirements that is not met for
  /// use of this module.
  ///
  /// \param MissingHeader If this module is unavailable because of a missing
  /// header, this parameter will be set to one of the missing headers.
  ///
  /// \param ShadowingModule If this module is unavailable because it is
  /// shadowed, this parameter will be set to the shadowing module.
  bool isAvailable(const LangOptions &LangOpts,
                   const TargetInfo &Target,
                   Requirement &Req,
                   UnresolvedHeaderDirective &MissingHeader,
                   Module *&ShadowingModule) const;

  /// Determine whether this module is a submodule.
  bool isSubModule() const { return Parent != nullptr; }

  /// Check if this module is a (possibly transitive) submodule of \p Other.
  ///
  /// The 'A is a submodule of B' relation is a partial order based on the
  /// the parent-child relationship between individual modules.
  ///
  /// Returns \c false if \p Other is \c nullptr.
  bool isSubModuleOf(const Module *Other) const;

  /// Determine whether this module is a part of a framework,
  /// either because it is a framework module or because it is a submodule
  /// of a framework module.
  bool isPartOfFramework() const {
    for (const Module *Mod = this; Mod; Mod = Mod->Parent)
      if (Mod->IsFramework)
        return true;

    return false;
  }

  /// Determine whether this module is a subframework of another
  /// framework.
  bool isSubFramework() const {
    return IsFramework && Parent && Parent->isPartOfFramework();
  }

  /// Set the parent of this module. This should only be used if the parent
  /// could not be set during module creation.
  void setParent(Module *M) {
    assert(!Parent);
    Parent = M;
    Parent->SubModuleIndex[Name] = Parent->SubModules.size();
    Parent->SubModules.push_back(this);
  }

  /// Is this module have similar semantics as headers.
  bool isHeaderLikeModule() const {
    return isModuleMapModule() || isHeaderUnit();
  }

  /// Is this a module partition.
  bool isModulePartition() const {
    return Kind == ModulePartitionInterface ||
           Kind == ModulePartitionImplementation;
  }

  /// Is this a module partition implementation unit.
  bool isModulePartitionImplementation() const {
    return Kind == ModulePartitionImplementation;
  }

  /// Is this a module implementation.
  bool isModuleImplementation() const {
    return Kind == ModuleImplementationUnit;
  }

  /// Is this module a header unit.
  bool isHeaderUnit() const { return Kind == ModuleHeaderUnit; }
  // Is this a C++20 module interface or a partition.
  bool isInterfaceOrPartition() const {
    return Kind == ModuleInterfaceUnit || isModulePartition();
  }

  /// Is this a C++20 named module unit.
  bool isNamedModuleUnit() const {
    return isInterfaceOrPartition() || isModuleImplementation();
  }

  bool isModuleInterfaceUnit() const {
    return Kind == ModuleInterfaceUnit || Kind == ModulePartitionInterface;
  }

  bool isNamedModuleInterfaceHasInit() const { return NamedModuleHasInit; }

  /// Get the primary module interface name from a partition.
  StringRef getPrimaryModuleInterfaceName() const {
    // Technically, global module fragment belongs to global module. And global
    // module has no name: [module.unit]p6:
    //   The global module has no name, no module interface unit, and is not
    //   introduced by any module-declaration.
    //
    // <global> is the default name showed in module map.
    if (isGlobalModule())
      return "<global>";

    if (isModulePartition()) {
      auto pos = Name.find(':');
      return StringRef(Name.data(), pos);
    }

    if (isPrivateModule())
      return getTopLevelModuleName();

    return Name;
  }

  /// Retrieve the full name of this module, including the path from
  /// its top-level module.
  /// \param AllowStringLiterals If \c true, components that might not be
  ///        lexically valid as identifiers will be emitted as string literals.
  std::string getFullModuleName(bool AllowStringLiterals = false) const;

  /// Whether the full name of this module is equal to joining
  /// \p nameParts with "."s.
  ///
  /// This is more efficient than getFullModuleName().
  bool fullModuleNameIs(ArrayRef<StringRef> nameParts) const;

  /// Retrieve the top-level module for this (sub)module, which may
  /// be this module.
  Module *getTopLevelModule() {
    return const_cast<Module *>(
             const_cast<const Module *>(this)->getTopLevelModule());
  }

  /// Retrieve the top-level module for this (sub)module, which may
  /// be this module.
  const Module *getTopLevelModule() const;

  /// Retrieve the name of the top-level module.
  StringRef getTopLevelModuleName() const {
    return getTopLevelModule()->Name;
  }

  /// The serialized AST file for this module, if one was created.
  OptionalFileEntryRef getASTFile() const {
    return getTopLevelModule()->ASTFile;
  }

  /// Set the serialized AST file for the top-level module of this module.
  void setASTFile(OptionalFileEntryRef File) {
    assert((!getASTFile() || getASTFile() == File) && "file path changed");
    getTopLevelModule()->ASTFile = File;
  }

  /// Retrieve the umbrella directory as written.
  std::optional<DirectoryName> getUmbrellaDirAsWritten() const {
    if (const auto *Dir = std::get_if<DirectoryEntryRef>(&Umbrella))
      return DirectoryName{UmbrellaAsWritten,
                           UmbrellaRelativeToRootModuleDirectory, *Dir};
    return std::nullopt;
  }

  /// Retrieve the umbrella header as written.
  std::optional<Header> getUmbrellaHeaderAsWritten() const {
    if (const auto *Hdr = std::get_if<FileEntryRef>(&Umbrella))
      return Header{UmbrellaAsWritten, UmbrellaRelativeToRootModuleDirectory,
                    *Hdr};
    return std::nullopt;
  }

  /// Get the effective umbrella directory for this module: either the one
  /// explicitly written in the module map file, or the parent of the umbrella
  /// header.
  OptionalDirectoryEntryRef getEffectiveUmbrellaDir() const;

  /// Add a top-level header associated with this module.
  void addTopHeader(FileEntryRef File);

  /// Add a top-level header filename associated with this module.
  void addTopHeaderFilename(StringRef Filename) {
    TopHeaderNames.push_back(std::string(Filename));
  }

  /// The top-level headers associated with this module.
  ArrayRef<FileEntryRef> getTopHeaders(FileManager &FileMgr);

  /// Determine whether this module has declared its intention to
  /// directly use another module.
  bool directlyUses(const Module *Requested);

  /// Add the given feature requirement to the list of features
  /// required by this module.
  ///
  /// \param Feature The feature that is required by this module (and
  /// its submodules).
  ///
  /// \param RequiredState The required state of this feature: \c true
  /// if it must be present, \c false if it must be absent.
  ///
  /// \param LangOpts The set of language options that will be used to
  /// evaluate the availability of this feature.
  ///
  /// \param Target The target options that will be used to evaluate the
  /// availability of this feature.
  void addRequirement(StringRef Feature, bool RequiredState,
                      const LangOptions &LangOpts,
                      const TargetInfo &Target);

  /// Mark this module and all of its submodules as unavailable.
  void markUnavailable(bool Unimportable);

  /// Find the submodule with the given name.
  ///
  /// \returns The submodule if found, or NULL otherwise.
  Module *findSubmodule(StringRef Name) const;
  Module *findOrInferSubmodule(StringRef Name);

  /// Get the Global Module Fragment (sub-module) for this module, it there is
  /// one.
  ///
  /// \returns The GMF sub-module if found, or NULL otherwise.
  Module *getGlobalModuleFragment() const;

  /// Get the Private Module Fragment (sub-module) for this module, it there is
  /// one.
  ///
  /// \returns The PMF sub-module if found, or NULL otherwise.
  Module *getPrivateModuleFragment() const;

  /// Determine whether the specified module would be visible to
  /// a lookup at the end of this module.
  ///
  /// FIXME: This may return incorrect results for (submodules of) the
  /// module currently being built, if it's queried before we see all
  /// of its imports.
  bool isModuleVisible(const Module *M) const {
    if (VisibleModulesCache.empty())
      buildVisibleModulesCache();
    return VisibleModulesCache.count(M);
  }

  unsigned getVisibilityID() const { return VisibilityID; }

  using submodule_iterator = std::vector<Module *>::iterator;
  using submodule_const_iterator = std::vector<Module *>::const_iterator;

  llvm::iterator_range<submodule_iterator> submodules() {
    return llvm::make_range(SubModules.begin(), SubModules.end());
  }
  llvm::iterator_range<submodule_const_iterator> submodules() const {
    return llvm::make_range(SubModules.begin(), SubModules.end());
  }

  /// Appends this module's list of exported modules to \p Exported.
  ///
  /// This provides a subset of immediately imported modules (the ones that are
  /// directly exported), not the complete set of exported modules.
  void getExportedModules(SmallVectorImpl<Module *> &Exported) const;

  static StringRef getModuleInputBufferName() {
    return "<module-includes>";
  }

  /// Print the module map for this module to the given stream.
  void print(raw_ostream &OS, unsigned Indent = 0, bool Dump = false) const;

  /// Dump the contents of this module to the given output stream.
  void dump() const;

private:
  void buildVisibleModulesCache() const;
};

/// A set of visible modules.
class VisibleModuleSet {
public:
  VisibleModuleSet() = default;
  VisibleModuleSet(VisibleModuleSet &&O)
      : ImportLocs(std::move(O.ImportLocs)), Generation(O.Generation ? 1 : 0) {
    O.ImportLocs.clear();
    ++O.Generation;
  }

  /// Move from another visible modules set. Guaranteed to leave the source
  /// empty and bump the generation on both.
  VisibleModuleSet &operator=(VisibleModuleSet &&O) {
    ImportLocs = std::move(O.ImportLocs);
    O.ImportLocs.clear();
    ++O.Generation;
    ++Generation;
    return *this;
  }

  /// Get the current visibility generation. Incremented each time the
  /// set of visible modules changes in any way.
  unsigned getGeneration() const { return Generation; }

  /// Determine whether a module is visible.
  bool isVisible(const Module *M) const {
    return getImportLoc(M).isValid();
  }

  /// Get the location at which the import of a module was triggered.
  SourceLocation getImportLoc(const Module *M) const {
    return M->getVisibilityID() < ImportLocs.size()
               ? ImportLocs[M->getVisibilityID()]
               : SourceLocation();
  }

  /// A callback to call when a module is made visible (directly or
  /// indirectly) by a call to \ref setVisible.
  using VisibleCallback = llvm::function_ref<void(Module *M)>;

  /// A callback to call when a module conflict is found. \p Path
  /// consists of a sequence of modules from the conflicting module to the one
  /// made visible, where each was exported by the next.
  using ConflictCallback =
      llvm::function_ref<void(ArrayRef<Module *> Path, Module *Conflict,
                         StringRef Message)>;

  /// Make a specific module visible.
  void setVisible(Module *M, SourceLocation Loc,
                  VisibleCallback Vis = [](Module *) {},
                  ConflictCallback Cb = [](ArrayRef<Module *>, Module *,
                                           StringRef) {});
private:
  /// Import locations for each visible module. Indexed by the module's
  /// VisibilityID.
  std::vector<SourceLocation> ImportLocs;

  /// Visibility generation, bumped every time the visibility state changes.
  unsigned Generation = 0;
};

} // namespace clang

#endif // LLVM_CLANG_BASIC_MODULE_H

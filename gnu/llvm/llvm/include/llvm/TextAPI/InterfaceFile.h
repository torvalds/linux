//===- llvm/TextAPI/InterfaceFile.h - TAPI Interface File -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A generic and abstract interface representation for linkable objects. This
// could be an MachO executable, bundle, dylib, or text-based stub file.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TEXTAPI_INTERFACEFILE_H
#define LLVM_TEXTAPI_INTERFACEFILE_H

#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Support/Allocator.h"
#include "llvm/TextAPI/ArchitectureSet.h"
#include "llvm/TextAPI/FileTypes.h"
#include "llvm/TextAPI/PackedVersion.h"
#include "llvm/TextAPI/Platform.h"
#include "llvm/TextAPI/RecordsSlice.h"
#include "llvm/TextAPI/Symbol.h"
#include "llvm/TextAPI/SymbolSet.h"
#include "llvm/TextAPI/Target.h"

namespace llvm {
namespace MachO {

/// Defines a list of Objective-C constraints.
enum class ObjCConstraintType : unsigned {
  /// No constraint.
  None = 0,

  /// Retain/Release.
  Retain_Release = 1,

  /// Retain/Release for Simulator.
  Retain_Release_For_Simulator = 2,

  /// Retain/Release or Garbage Collection.
  Retain_Release_Or_GC = 3,

  /// Garbage Collection.
  GC = 4,
};

/// Reference to an interface file.
class InterfaceFileRef {
public:
  InterfaceFileRef() = default;

  InterfaceFileRef(StringRef InstallName) : InstallName(InstallName) {}

  InterfaceFileRef(StringRef InstallName, const TargetList Targets)
      : InstallName(InstallName), Targets(std::move(Targets)) {}

  StringRef getInstallName() const { return InstallName; };

  void addTarget(const Target &Target);
  template <typename RangeT> void addTargets(RangeT &&Targets) {
    for (const auto &Target : Targets)
      addTarget(Target(Target));
  }

  bool hasTarget(Target &Targ) const {
    return llvm::is_contained(Targets, Targ);
  }

  using const_target_iterator = TargetList::const_iterator;
  using const_target_range = llvm::iterator_range<const_target_iterator>;
  const_target_range targets() const { return {Targets}; }

  ArchitectureSet getArchitectures() const {
    return mapToArchitectureSet(Targets);
  }

  PlatformSet getPlatforms() const { return mapToPlatformSet(Targets); }

  bool operator==(const InterfaceFileRef &O) const {
    return std::tie(InstallName, Targets) == std::tie(O.InstallName, O.Targets);
  }

  bool operator!=(const InterfaceFileRef &O) const {
    return std::tie(InstallName, Targets) != std::tie(O.InstallName, O.Targets);
  }

  bool operator<(const InterfaceFileRef &O) const {
    return std::tie(InstallName, Targets) < std::tie(O.InstallName, O.Targets);
  }

private:
  std::string InstallName;
  TargetList Targets;
};

} // end namespace MachO.

namespace MachO {

/// Defines the interface file.
class InterfaceFile {
public:
  InterfaceFile(std::unique_ptr<SymbolSet> &&InputSymbols)
      : SymbolsSet(std::move(InputSymbols)) {}

  InterfaceFile() : SymbolsSet(std::make_unique<SymbolSet>()){};
  /// Set the path from which this file was generated (if applicable).
  ///
  /// \param Path_ The path to the source file.
  void setPath(StringRef Path_) { Path = std::string(Path_); }

  /// Get the path from which this file was generated (if applicable).
  ///
  /// \return The path to the source file or empty.
  StringRef getPath() const { return Path; }

  /// Set the file type.
  ///
  /// This is used by the YAML writer to identify the specification it should
  /// use for writing the file.
  ///
  /// \param Kind The file type.
  void setFileType(FileType Kind) { FileKind = Kind; }

  /// Get the file type.
  ///
  /// \return The file type.
  FileType getFileType() const { return FileKind; }

  /// Get the architectures.
  ///
  /// \return The applicable architectures.
  ArchitectureSet getArchitectures() const {
    return mapToArchitectureSet(Targets);
  }

  /// Get the platforms.
  ///
  /// \return The applicable platforms.
  PlatformSet getPlatforms() const { return mapToPlatformSet(Targets); }

  /// Set and add target.
  ///
  /// \param Target the target to add into.
  void addTarget(const Target &Target);

  /// Determine if target triple slice exists in file.
  ///
  /// \param Targ the value to find.
  bool hasTarget(const Target &Targ) const {
    return llvm::is_contained(Targets, Targ);
  }

  /// Set and add targets.
  ///
  /// Add the subset of llvm::triples that is supported by Tapi
  ///
  /// \param Targets the collection of targets.
  template <typename RangeT> void addTargets(RangeT &&Targets) {
    for (const auto &Target_ : Targets)
      addTarget(Target(Target_));
  }

  using const_target_iterator = TargetList::const_iterator;
  using const_target_range = llvm::iterator_range<const_target_iterator>;
  const_target_range targets() const { return {Targets}; }

  using const_filtered_target_iterator =
      llvm::filter_iterator<const_target_iterator,
                            std::function<bool(const Target &)>>;
  using const_filtered_target_range =
      llvm::iterator_range<const_filtered_target_iterator>;
  const_filtered_target_range targets(ArchitectureSet Archs) const;

  /// Set the install name of the library.
  void setInstallName(StringRef InstallName_) {
    InstallName = std::string(InstallName_);
  }

  /// Get the install name of the library.
  StringRef getInstallName() const { return InstallName; }

  /// Set the current version of the library.
  void setCurrentVersion(PackedVersion Version) { CurrentVersion = Version; }

  /// Get the current version of the library.
  PackedVersion getCurrentVersion() const { return CurrentVersion; }

  /// Set the compatibility version of the library.
  void setCompatibilityVersion(PackedVersion Version) {
    CompatibilityVersion = Version;
  }

  /// Get the compatibility version of the library.
  PackedVersion getCompatibilityVersion() const { return CompatibilityVersion; }

  /// Set the Swift ABI version of the library.
  void setSwiftABIVersion(uint8_t Version) { SwiftABIVersion = Version; }

  /// Get the Swift ABI version of the library.
  uint8_t getSwiftABIVersion() const { return SwiftABIVersion; }

  /// Specify if the library uses two-level namespace (or flat namespace).
  void setTwoLevelNamespace(bool V = true) { IsTwoLevelNamespace = V; }

  /// Check if the library uses two-level namespace.
  bool isTwoLevelNamespace() const { return IsTwoLevelNamespace; }

  /// Specify if the library is an OS library but not shared cache eligible.
  void setOSLibNotForSharedCache(bool V = true) {
    IsOSLibNotForSharedCache = V;
  }

  /// Check if the library is an OS library that is not shared cache eligible.
  bool isOSLibNotForSharedCache() const { return IsOSLibNotForSharedCache; }

  /// Specify if the library is application extension safe (or not).
  void setApplicationExtensionSafe(bool V = true) { IsAppExtensionSafe = V; }

  /// Check if the library is application extension safe.
  bool isApplicationExtensionSafe() const { return IsAppExtensionSafe; }

  /// Check if the library has simulator support.
  bool hasSimulatorSupport() const { return HasSimSupport; }

  /// Specify if the library has simulator support.
  void setSimulatorSupport(bool V = true) { HasSimSupport = V; }

  /// Set the Objective-C constraint.
  void setObjCConstraint(ObjCConstraintType Constraint) {
    ObjcConstraint = Constraint;
  }

  /// Get the Objective-C constraint.
  ObjCConstraintType getObjCConstraint() const { return ObjcConstraint; }

  /// Set the parent umbrella frameworks.
  /// \param Target_ The target applicable to Parent
  /// \param Parent  The name of Parent
  void addParentUmbrella(const Target &Target_, StringRef Parent);

  /// Get the list of Parent Umbrella frameworks.
  ///
  /// \return Returns a list of target information and install name of parent
  /// umbrellas.
  const std::vector<std::pair<Target, std::string>> &umbrellas() const {
    return ParentUmbrellas;
  }

  /// Add an allowable client.
  ///
  /// Mach-O Dynamic libraries have the concept of allowable clients that are
  /// checked during static link time. The name of the application or library
  /// that is being generated needs to match one of the allowable clients or the
  /// linker refuses to link this library.
  ///
  /// \param InstallName The name of the client that is allowed to link this
  /// library.
  /// \param Target The target triple for which this applies.
  void addAllowableClient(StringRef InstallName, const Target &Target);

  /// Get the list of allowable clients.
  ///
  /// \return Returns a list of allowable clients.
  const std::vector<InterfaceFileRef> &allowableClients() const {
    return AllowableClients;
  }

  /// Add a re-exported library.
  ///
  /// \param InstallName The name of the library to re-export.
  /// \param Target The target triple for which this applies.
  void addReexportedLibrary(StringRef InstallName, const Target &Target);

  /// Get the list of re-exported libraries.
  ///
  /// \return Returns a list of re-exported libraries.
  const std::vector<InterfaceFileRef> &reexportedLibraries() const {
    return ReexportedLibraries;
  }

  /// Add a library for inlining to top level library.
  ///
  ///\param Document The library to inline with top level library.
  void addDocument(std::shared_ptr<InterfaceFile> &&Document);

  /// Returns the pointer to parent document if exists or nullptr otherwise.
  InterfaceFile *getParent() const { return Parent; }

  /// Get the list of inlined libraries.
  ///
  /// \return Returns a list of the inlined frameworks.
  const std::vector<std::shared_ptr<InterfaceFile>> &documents() const {
    return Documents;
  }

  /// Set the runpath search paths.
  /// \param RPath The name of runpath.
  /// \param InputTarget The target applicable to runpath search path.
  void addRPath(StringRef RPath, const Target &InputTarget);

  /// Get the list of runpath search paths.
  ///
  /// \return Returns a list of the rpaths per target.
  const std::vector<std::pair<Target, std::string>> &rpaths() const {
    return RPaths;
  }

  /// Get symbol if exists in file.
  ///
  /// \param Kind The kind of global symbol to record.
  /// \param Name The name of the symbol.
  /// \param ObjCIF The ObjCInterface symbol type, if applicable.
  std::optional<const Symbol *>
  getSymbol(EncodeKind Kind, StringRef Name,
            ObjCIFSymbolKind ObjCIF = ObjCIFSymbolKind::None) const {
    if (auto *Sym = SymbolsSet->findSymbol(Kind, Name, ObjCIF))
      return Sym;
    return std::nullopt;
  }

  /// Add a symbol to the symbols list or extend an existing one.
  template <typename RangeT, typename ElT = std::remove_reference_t<
                                 decltype(*std::begin(std::declval<RangeT>()))>>
  void addSymbol(EncodeKind Kind, StringRef Name, RangeT &&Targets,
                 SymbolFlags Flags = SymbolFlags::None) {
    SymbolsSet->addGlobal(Kind, Name, Flags, Targets);
  }

  /// Add Symbol with multiple targets.
  ///
  /// \param Kind The kind of global symbol to record.
  /// \param Name The name of the symbol.
  /// \param Targets The list of targets the symbol is defined in.
  /// \param Flags The properties the symbol holds.
  void addSymbol(EncodeKind Kind, StringRef Name, TargetList &&Targets,
                 SymbolFlags Flags = SymbolFlags::None) {
    SymbolsSet->addGlobal(Kind, Name, Flags, Targets);
  }

  /// Add Symbol with single target.
  ///
  /// \param Kind The kind of global symbol to record.
  /// \param Name The name of the symbol.
  /// \param Target The target the symbol is defined in.
  /// \param Flags The properties the symbol holds.
  void addSymbol(EncodeKind Kind, StringRef Name, Target &Target,
                 SymbolFlags Flags = SymbolFlags::None) {
    SymbolsSet->addGlobal(Kind, Name, Flags, Target);
  }

  /// Get size of symbol set.
  /// \return The number of symbols the file holds.
  size_t symbolsCount() const { return SymbolsSet->size(); }

  using const_symbol_range = SymbolSet::const_symbol_range;
  using const_filtered_symbol_range = SymbolSet::const_filtered_symbol_range;

  const_symbol_range symbols() const { return SymbolsSet->symbols(); };
  const_filtered_symbol_range exports() const { return SymbolsSet->exports(); };
  const_filtered_symbol_range reexports() const {
    return SymbolsSet->reexports();
  };
  const_filtered_symbol_range undefineds() const {
    return SymbolsSet->undefineds();
  };

  /// Extract architecture slice from Interface.
  ///
  /// \param Arch architecture to extract from.
  /// \return New InterfaceFile with extracted architecture slice.
  llvm::Expected<std::unique_ptr<InterfaceFile>>
  extract(Architecture Arch) const;

  /// Remove architecture slice from Interface.
  ///
  /// \param Arch architecture to remove.
  /// \return New Interface File with removed architecture slice.
  llvm::Expected<std::unique_ptr<InterfaceFile>>
  remove(Architecture Arch) const;

  /// Merge Interfaces for the same library. The following library attributes
  /// must match.
  /// * Install name, Current & Compatibility version,
  /// * Two-level namespace enablement, and App extension enablement.
  ///
  /// \param O The Interface to merge.
  /// \return New Interface File that was merged.
  llvm::Expected<std::unique_ptr<InterfaceFile>>
  merge(const InterfaceFile *O) const;

  /// Inline reexported library into Interface.
  ///
  /// \param Library Interface of reexported library.
  /// \param Overwrite Whether to overwrite preexisting inlined library.
  void inlineLibrary(std::shared_ptr<InterfaceFile> Library,
                     bool Overwrite = false);

  /// Set InterfaceFile properties from pre-gathered binary attributes,
  /// if they are not set already.
  ///
  /// \param BA Attributes typically represented in load commands.
  /// \param Targ MachO Target slice to add attributes to.
  void setFromBinaryAttrs(const RecordsSlice::BinaryAttrs &BA,
                          const Target &Targ);

  /// The equality is determined by attributes that impact linking
  /// compatibilities. Path, & FileKind are irrelevant since these by
  /// itself should not impact linking.
  /// This is an expensive operation.
  bool operator==(const InterfaceFile &O) const;

  bool operator!=(const InterfaceFile &O) const { return !(*this == O); }

private:
  llvm::BumpPtrAllocator Allocator;
  StringRef copyString(StringRef String) {
    if (String.empty())
      return {};

    void *Ptr = Allocator.Allocate(String.size(), 1);
    memcpy(Ptr, String.data(), String.size());
    return StringRef(reinterpret_cast<const char *>(Ptr), String.size());
  }

  TargetList Targets;
  std::string Path;
  FileType FileKind{FileType::Invalid};
  std::string InstallName;
  PackedVersion CurrentVersion;
  PackedVersion CompatibilityVersion;
  uint8_t SwiftABIVersion{0};
  bool IsTwoLevelNamespace{false};
  bool IsOSLibNotForSharedCache{false};
  bool IsAppExtensionSafe{false};
  bool HasSimSupport{false};
  ObjCConstraintType ObjcConstraint = ObjCConstraintType::None;
  std::vector<std::pair<Target, std::string>> ParentUmbrellas;
  std::vector<InterfaceFileRef> AllowableClients;
  std::vector<InterfaceFileRef> ReexportedLibraries;
  std::vector<std::shared_ptr<InterfaceFile>> Documents;
  std::vector<std::pair<Target, std::string>> RPaths;
  std::unique_ptr<SymbolSet> SymbolsSet;
  InterfaceFile *Parent = nullptr;
};

// Keep containers that hold InterfaceFileRefs in sorted order and uniqued.
template <typename C>
typename C::iterator addEntry(C &Container, StringRef InstallName) {
  auto I = partition_point(Container, [=](const InterfaceFileRef &O) {
    return O.getInstallName() < InstallName;
  });
  if (I != Container.end() && I->getInstallName() == InstallName)
    return I;

  return Container.emplace(I, InstallName);
}

} // end namespace MachO.
} // end namespace llvm.

#endif // LLVM_TEXTAPI_INTERFACEFILE_H

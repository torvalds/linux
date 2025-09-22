//===- InterfaceFile.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements the Interface File.
//
//===----------------------------------------------------------------------===//

#include "llvm/TextAPI/InterfaceFile.h"
#include "llvm/TextAPI/RecordsSlice.h"
#include "llvm/TextAPI/TextAPIError.h"
#include <iomanip>
#include <sstream>

using namespace llvm;
using namespace llvm::MachO;

void InterfaceFileRef::addTarget(const Target &Target) {
  addEntry(Targets, Target);
}

void InterfaceFile::addAllowableClient(StringRef InstallName,
                                       const Target &Target) {
  if (InstallName.empty())
    return;
  auto Client = addEntry(AllowableClients, InstallName);
  Client->addTarget(Target);
}

void InterfaceFile::addReexportedLibrary(StringRef InstallName,
                                         const Target &Target) {
  if (InstallName.empty())
    return;
  auto Lib = addEntry(ReexportedLibraries, InstallName);
  Lib->addTarget(Target);
}

void InterfaceFile::addParentUmbrella(const Target &Target_, StringRef Parent) {
  if (Parent.empty())
    return;
  auto Iter = lower_bound(ParentUmbrellas, Target_,
                          [](const std::pair<Target, std::string> &LHS,
                             Target RHS) { return LHS.first < RHS; });

  if ((Iter != ParentUmbrellas.end()) && !(Target_ < Iter->first)) {
    Iter->second = std::string(Parent);
    return;
  }

  ParentUmbrellas.emplace(Iter, Target_, std::string(Parent));
}

void InterfaceFile::addRPath(StringRef RPath, const Target &InputTarget) {
  if (RPath.empty())
    return;
  using RPathEntryT = const std::pair<Target, std::string>;
  RPathEntryT Entry(InputTarget, RPath);
  auto Iter =
      lower_bound(RPaths, Entry,
                  [](RPathEntryT &LHS, RPathEntryT &RHS) { return LHS < RHS; });

  if ((Iter != RPaths.end()) && (*Iter == Entry))
    return;

  RPaths.emplace(Iter, Entry);
}

void InterfaceFile::addTarget(const Target &Target) {
  addEntry(Targets, Target);
}

InterfaceFile::const_filtered_target_range
InterfaceFile::targets(ArchitectureSet Archs) const {
  std::function<bool(const Target &)> fn = [Archs](const Target &Target_) {
    return Archs.has(Target_.Arch);
  };
  return make_filter_range(Targets, fn);
}

void InterfaceFile::addDocument(std::shared_ptr<InterfaceFile> &&Document) {
  auto Pos = llvm::lower_bound(Documents, Document,
                               [](const std::shared_ptr<InterfaceFile> &LHS,
                                  const std::shared_ptr<InterfaceFile> &RHS) {
                                 return LHS->InstallName < RHS->InstallName;
                               });
  Document->Parent = this;
  Documents.insert(Pos, Document);
}

void InterfaceFile::inlineLibrary(std::shared_ptr<InterfaceFile> Library,
                                  bool Overwrite) {
  auto AddFwk = [&](std::shared_ptr<InterfaceFile> &&Reexport) {
    auto It = lower_bound(
        Documents, Reexport->getInstallName(),
        [](std::shared_ptr<InterfaceFile> &Lhs, const StringRef Rhs) {
          return Lhs->getInstallName() < Rhs;
        });

    if (Overwrite && It != Documents.end() &&
        Reexport->getInstallName() == (*It)->getInstallName()) {
      std::replace(Documents.begin(), Documents.end(), *It,
                   std::move(Reexport));
      return;
    }

    if ((It != Documents.end()) &&
        !(Reexport->getInstallName() < (*It)->getInstallName()))
      return;

    Documents.emplace(It, std::move(Reexport));
  };
  for (auto Doc : Library->documents())
    AddFwk(std::move(Doc));

  Library->Documents.clear();
  AddFwk(std::move(Library));
}

Expected<std::unique_ptr<InterfaceFile>>
InterfaceFile::merge(const InterfaceFile *O) const {
  // Verify files can be merged.
  if (getInstallName() != O->getInstallName()) {
    return make_error<StringError>("install names do not match",
                                   inconvertibleErrorCode());
  }

  if (getCurrentVersion() != O->getCurrentVersion()) {
    return make_error<StringError>("current versions do not match",
                                   inconvertibleErrorCode());
  }

  if (getCompatibilityVersion() != O->getCompatibilityVersion()) {
    return make_error<StringError>("compatibility versions do not match",
                                   inconvertibleErrorCode());
  }

  if ((getSwiftABIVersion() != 0) && (O->getSwiftABIVersion() != 0) &&
      (getSwiftABIVersion() != O->getSwiftABIVersion())) {
    return make_error<StringError>("swift ABI versions do not match",
                                   inconvertibleErrorCode());
  }

  if (isTwoLevelNamespace() != O->isTwoLevelNamespace()) {
    return make_error<StringError>("two level namespace flags do not match",
                                   inconvertibleErrorCode());
  }

  if (isApplicationExtensionSafe() != O->isApplicationExtensionSafe()) {
    return make_error<StringError>(
        "application extension safe flags do not match",
        inconvertibleErrorCode());
  }

  std::unique_ptr<InterfaceFile> IF(new InterfaceFile());
  IF->setFileType(std::max(getFileType(), O->getFileType()));
  IF->setPath(getPath());
  IF->setInstallName(getInstallName());
  IF->setCurrentVersion(getCurrentVersion());
  IF->setCompatibilityVersion(getCompatibilityVersion());

  if (getSwiftABIVersion() == 0)
    IF->setSwiftABIVersion(O->getSwiftABIVersion());
  else
    IF->setSwiftABIVersion(getSwiftABIVersion());

  IF->setTwoLevelNamespace(isTwoLevelNamespace());
  IF->setApplicationExtensionSafe(isApplicationExtensionSafe());

  for (const auto &It : umbrellas()) {
    if (!It.second.empty())
      IF->addParentUmbrella(It.first, It.second);
  }
  for (const auto &It : O->umbrellas()) {
    if (!It.second.empty())
      IF->addParentUmbrella(It.first, It.second);
  }
  IF->addTargets(targets());
  IF->addTargets(O->targets());

  for (const auto &Lib : allowableClients())
    for (const auto &Target : Lib.targets())
      IF->addAllowableClient(Lib.getInstallName(), Target);

  for (const auto &Lib : O->allowableClients())
    for (const auto &Target : Lib.targets())
      IF->addAllowableClient(Lib.getInstallName(), Target);

  for (const auto &Lib : reexportedLibraries())
    for (const auto &Target : Lib.targets())
      IF->addReexportedLibrary(Lib.getInstallName(), Target);

  for (const auto &Lib : O->reexportedLibraries())
    for (const auto &Target : Lib.targets())
      IF->addReexportedLibrary(Lib.getInstallName(), Target);

  for (const auto &[Target, Path] : rpaths())
    IF->addRPath(Path, Target);
  for (const auto &[Target, Path] : O->rpaths())
    IF->addRPath(Path, Target);

  for (const auto *Sym : symbols()) {
    IF->addSymbol(Sym->getKind(), Sym->getName(), Sym->targets(),
                  Sym->getFlags());
  }

  for (const auto *Sym : O->symbols()) {
    IF->addSymbol(Sym->getKind(), Sym->getName(), Sym->targets(),
                  Sym->getFlags());
  }

  return std::move(IF);
}

Expected<std::unique_ptr<InterfaceFile>>
InterfaceFile::remove(Architecture Arch) const {
  if (getArchitectures() == Arch)
    return make_error<StringError>("cannot remove last architecture slice '" +
                                       getArchitectureName(Arch) + "'",
                                   inconvertibleErrorCode());

  if (!getArchitectures().has(Arch)) {
    bool Found = false;
    for (auto &Doc : Documents) {
      if (Doc->getArchitectures().has(Arch)) {
        Found = true;
        break;
      }
    }

    if (!Found)
      return make_error<TextAPIError>(TextAPIErrorCode::NoSuchArchitecture);
  }

  std::unique_ptr<InterfaceFile> IF(new InterfaceFile());
  IF->setFileType(getFileType());
  IF->setPath(getPath());
  IF->addTargets(targets(ArchitectureSet::All().clear(Arch)));
  IF->setInstallName(getInstallName());
  IF->setCurrentVersion(getCurrentVersion());
  IF->setCompatibilityVersion(getCompatibilityVersion());
  IF->setSwiftABIVersion(getSwiftABIVersion());
  IF->setTwoLevelNamespace(isTwoLevelNamespace());
  IF->setApplicationExtensionSafe(isApplicationExtensionSafe());
  for (const auto &It : umbrellas())
    if (It.first.Arch != Arch)
      IF->addParentUmbrella(It.first, It.second);

  for (const auto &Lib : allowableClients()) {
    for (const auto &Target : Lib.targets())
      if (Target.Arch != Arch)
        IF->addAllowableClient(Lib.getInstallName(), Target);
  }

  for (const auto &Lib : reexportedLibraries()) {
    for (const auto &Target : Lib.targets())
      if (Target.Arch != Arch)
        IF->addReexportedLibrary(Lib.getInstallName(), Target);
  }

  for (const auto *Sym : symbols()) {
    auto Archs = Sym->getArchitectures();
    Archs.clear(Arch);
    if (Archs.empty())
      continue;

    IF->addSymbol(Sym->getKind(), Sym->getName(), Sym->targets(Archs),
                  Sym->getFlags());
  }

  for (auto &Doc : Documents) {
    // Skip the inlined document if the to be removed architecture is the
    // only one left.
    if (Doc->getArchitectures() == Arch)
      continue;

    // If the document doesn't contain the arch, then no work is to be done
    // and it can be copied over.
    if (!Doc->getArchitectures().has(Arch)) {
      auto NewDoc = Doc;
      IF->addDocument(std::move(NewDoc));
      continue;
    }

    auto Result = Doc->remove(Arch);
    if (!Result)
      return Result;

    IF->addDocument(std::move(Result.get()));
  }

  return std::move(IF);
}

Expected<std::unique_ptr<InterfaceFile>>
InterfaceFile::extract(Architecture Arch) const {
  if (!getArchitectures().has(Arch)) {
    return make_error<StringError>("file doesn't have architecture '" +
                                       getArchitectureName(Arch) + "'",
                                   inconvertibleErrorCode());
  }

  std::unique_ptr<InterfaceFile> IF(new InterfaceFile());
  IF->setFileType(getFileType());
  IF->setPath(getPath());
  IF->addTargets(targets(Arch));
  IF->setInstallName(getInstallName());
  IF->setCurrentVersion(getCurrentVersion());
  IF->setCompatibilityVersion(getCompatibilityVersion());
  IF->setSwiftABIVersion(getSwiftABIVersion());
  IF->setTwoLevelNamespace(isTwoLevelNamespace());
  IF->setApplicationExtensionSafe(isApplicationExtensionSafe());
  for (const auto &It : umbrellas())
    if (It.first.Arch == Arch)
      IF->addParentUmbrella(It.first, It.second);

  for (const auto &It : rpaths())
    if (It.first.Arch == Arch)
      IF->addRPath(It.second, It.first);

  for (const auto &Lib : allowableClients())
    for (const auto &Target : Lib.targets())
      if (Target.Arch == Arch)
        IF->addAllowableClient(Lib.getInstallName(), Target);

  for (const auto &Lib : reexportedLibraries())
    for (const auto &Target : Lib.targets())
      if (Target.Arch == Arch)
        IF->addReexportedLibrary(Lib.getInstallName(), Target);

  for (const auto *Sym : symbols()) {
    if (Sym->hasArchitecture(Arch))
      IF->addSymbol(Sym->getKind(), Sym->getName(), Sym->targets(Arch),
                    Sym->getFlags());
  }

  for (auto &Doc : Documents) {
    // Skip documents that don't have the requested architecture.
    if (!Doc->getArchitectures().has(Arch))
      continue;

    auto Result = Doc->extract(Arch);
    if (!Result)
      return Result;

    IF->addDocument(std::move(Result.get()));
  }

  return std::move(IF);
}

void InterfaceFile::setFromBinaryAttrs(const RecordsSlice::BinaryAttrs &BA,
                                       const Target &Targ) {
  if (getFileType() != BA.File)
    setFileType(BA.File);
  if (getInstallName().empty())
    setInstallName(BA.InstallName);
  if (BA.AppExtensionSafe && !isApplicationExtensionSafe())
    setApplicationExtensionSafe();
  if (BA.TwoLevelNamespace && !isTwoLevelNamespace())
    setTwoLevelNamespace();
  if (BA.OSLibNotForSharedCache && !isOSLibNotForSharedCache())
    setOSLibNotForSharedCache();
  if (getCurrentVersion().empty())
    setCurrentVersion(BA.CurrentVersion);
  if (getCompatibilityVersion().empty())
    setCompatibilityVersion(BA.CompatVersion);
  if (getSwiftABIVersion() == 0)
    setSwiftABIVersion(BA.SwiftABI);
  if (getPath().empty())
    setPath(BA.Path);
  if (!BA.ParentUmbrella.empty())
    addParentUmbrella(Targ, BA.ParentUmbrella);
  for (const auto &Client : BA.AllowableClients)
    addAllowableClient(Client, Targ);
  for (const auto &Lib : BA.RexportedLibraries)
    addReexportedLibrary(Lib, Targ);
}

static bool isYAMLTextStub(const FileType &Kind) {
  return (Kind >= FileType::TBD_V1) && (Kind < FileType::TBD_V5);
}

bool InterfaceFile::operator==(const InterfaceFile &O) const {
  if (Targets != O.Targets)
    return false;
  if (InstallName != O.InstallName)
    return false;
  if ((CurrentVersion != O.CurrentVersion) ||
      (CompatibilityVersion != O.CompatibilityVersion))
    return false;
  if (SwiftABIVersion != O.SwiftABIVersion)
    return false;
  if (IsTwoLevelNamespace != O.IsTwoLevelNamespace)
    return false;
  if (IsAppExtensionSafe != O.IsAppExtensionSafe)
    return false;
  if (IsOSLibNotForSharedCache != O.IsOSLibNotForSharedCache)
    return false;
  if (HasSimSupport != O.HasSimSupport)
    return false;
  if (ParentUmbrellas != O.ParentUmbrellas)
    return false;
  if (AllowableClients != O.AllowableClients)
    return false;
  if (ReexportedLibraries != O.ReexportedLibraries)
    return false;
  if (*SymbolsSet != *O.SymbolsSet)
    return false;
  // Don't compare run search paths for older filetypes that cannot express
  // them.
  if (!(isYAMLTextStub(FileKind)) && !(isYAMLTextStub(O.FileKind))) {
    if (RPaths != O.RPaths)
      return false;
    if (mapToPlatformVersionSet(Targets) != mapToPlatformVersionSet(O.Targets))
      return false;
  }

  if (!std::equal(Documents.begin(), Documents.end(), O.Documents.begin(),
                  O.Documents.end(),
                  [](const std::shared_ptr<InterfaceFile> LHS,
                     const std::shared_ptr<InterfaceFile> RHS) {
                    return *LHS == *RHS;
                  }))
    return false;
  return true;
}

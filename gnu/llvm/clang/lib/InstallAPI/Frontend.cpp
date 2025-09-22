//===- Frontend.cpp ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/InstallAPI/Frontend.h"
#include "clang/AST/Availability.h"
#include "clang/InstallAPI/FrontendRecords.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"

using namespace llvm;
using namespace llvm::MachO;

namespace clang::installapi {
std::pair<GlobalRecord *, FrontendAttrs *> FrontendRecordsSlice::addGlobal(
    StringRef Name, RecordLinkage Linkage, GlobalRecord::Kind GV,
    const clang::AvailabilityInfo Avail, const Decl *D, const HeaderType Access,
    SymbolFlags Flags, bool Inlined) {

  GlobalRecord *GR =
      llvm::MachO::RecordsSlice::addGlobal(Name, Linkage, GV, Flags, Inlined);
  auto Result = FrontendRecords.insert(
      {GR, FrontendAttrs{Avail, D, D->getLocation(), Access}});
  return {GR, &(Result.first->second)};
}

std::pair<ObjCInterfaceRecord *, FrontendAttrs *>
FrontendRecordsSlice::addObjCInterface(StringRef Name, RecordLinkage Linkage,
                                       const clang::AvailabilityInfo Avail,
                                       const Decl *D, HeaderType Access,
                                       bool IsEHType) {
  ObjCIFSymbolKind SymType =
      ObjCIFSymbolKind::Class | ObjCIFSymbolKind::MetaClass;
  if (IsEHType)
    SymType |= ObjCIFSymbolKind::EHType;

  ObjCInterfaceRecord *ObjCR =
      llvm::MachO::RecordsSlice::addObjCInterface(Name, Linkage, SymType);
  auto Result = FrontendRecords.insert(
      {ObjCR, FrontendAttrs{Avail, D, D->getLocation(), Access}});
  return {ObjCR, &(Result.first->second)};
}

std::pair<ObjCCategoryRecord *, FrontendAttrs *>
FrontendRecordsSlice::addObjCCategory(StringRef ClassToExtend,
                                      StringRef CategoryName,
                                      const clang::AvailabilityInfo Avail,
                                      const Decl *D, HeaderType Access) {
  ObjCCategoryRecord *ObjCR =
      llvm::MachO::RecordsSlice::addObjCCategory(ClassToExtend, CategoryName);
  auto Result = FrontendRecords.insert(
      {ObjCR, FrontendAttrs{Avail, D, D->getLocation(), Access}});
  return {ObjCR, &(Result.first->second)};
}

std::pair<ObjCIVarRecord *, FrontendAttrs *> FrontendRecordsSlice::addObjCIVar(
    ObjCContainerRecord *Container, StringRef IvarName, RecordLinkage Linkage,
    const clang::AvailabilityInfo Avail, const Decl *D, HeaderType Access,
    const clang::ObjCIvarDecl::AccessControl AC) {
  // If the decl otherwise would have been exported, check their access control.
  // Ivar's linkage is also determined by this.
  if ((Linkage == RecordLinkage::Exported) &&
      ((AC == ObjCIvarDecl::Private) || (AC == ObjCIvarDecl::Package)))
    Linkage = RecordLinkage::Internal;
  ObjCIVarRecord *ObjCR =
      llvm::MachO::RecordsSlice::addObjCIVar(Container, IvarName, Linkage);
  auto Result = FrontendRecords.insert(
      {ObjCR, FrontendAttrs{Avail, D, D->getLocation(), Access}});

  return {ObjCR, &(Result.first->second)};
}

std::optional<HeaderType>
InstallAPIContext::findAndRecordFile(const FileEntry *FE,
                                     const Preprocessor &PP) {
  if (!FE)
    return std::nullopt;

  // Check if header has been looked up already and whether it is something
  // installapi should use.
  auto It = KnownFiles.find(FE);
  if (It != KnownFiles.end()) {
    if (It->second != HeaderType::Unknown)
      return It->second;
    else
      return std::nullopt;
  }

  // If file was not found, search by how the header was
  // included. This is primarily to resolve headers found
  // in a different location than what passed directly as input.
  StringRef IncludeName = PP.getHeaderSearchInfo().getIncludeNameForHeader(FE);
  auto BackupIt = KnownIncludes.find(IncludeName.str());
  if (BackupIt != KnownIncludes.end()) {
    KnownFiles[FE] = BackupIt->second;
    return BackupIt->second;
  }

  // Record that the file was found to avoid future string searches for the
  // same file.
  KnownFiles.insert({FE, HeaderType::Unknown});
  return std::nullopt;
}

void InstallAPIContext::addKnownHeader(const HeaderFile &H) {
  auto FE = FM->getFile(H.getPath());
  if (!FE)
    return; // File does not exist.
  KnownFiles[*FE] = H.getType();

  if (!H.useIncludeName())
    return;

  KnownIncludes[H.getIncludeName()] = H.getType();
}

static StringRef getFileExtension(clang::Language Lang) {
  switch (Lang) {
  default:
    llvm_unreachable("Unexpected language option.");
  case clang::Language::C:
    return ".c";
  case clang::Language::CXX:
    return ".cpp";
  case clang::Language::ObjC:
    return ".m";
  case clang::Language::ObjCXX:
    return ".mm";
  }
}

std::unique_ptr<MemoryBuffer> createInputBuffer(InstallAPIContext &Ctx) {
  assert(Ctx.Type != HeaderType::Unknown &&
         "unexpected access level for parsing");
  SmallString<4096> Contents;
  raw_svector_ostream OS(Contents);
  for (const HeaderFile &H : Ctx.InputHeaders) {
    if (H.isExcluded())
      continue;
    if (H.getType() != Ctx.Type)
      continue;
    if (Ctx.LangMode == Language::C || Ctx.LangMode == Language::CXX)
      OS << "#include ";
    else
      OS << "#import ";
    if (H.useIncludeName())
      OS << "<" << H.getIncludeName() << ">\n";
    else
      OS << "\"" << H.getPath() << "\"\n";

    Ctx.addKnownHeader(H);
  }
  if (Contents.empty())
    return nullptr;

  SmallString<64> BufferName(
      {"installapi-includes-", Ctx.Slice->getTriple().str(), "-",
       getName(Ctx.Type), getFileExtension(Ctx.LangMode)});
  return llvm::MemoryBuffer::getMemBufferCopy(Contents, BufferName);
}

std::string findLibrary(StringRef InstallName, FileManager &FM,
                        ArrayRef<std::string> FrameworkSearchPaths,
                        ArrayRef<std::string> LibrarySearchPaths,
                        ArrayRef<std::string> SearchPaths) {
  auto getLibrary =
      [&](const StringRef FullPath) -> std::optional<std::string> {
    // Prefer TextAPI files when possible.
    SmallString<PATH_MAX> TextAPIFilePath = FullPath;
    replace_extension(TextAPIFilePath, ".tbd");

    if (FM.getOptionalFileRef(TextAPIFilePath))
      return std::string(TextAPIFilePath);

    if (FM.getOptionalFileRef(FullPath))
      return std::string(FullPath);

    return std::nullopt;
  };

  const StringRef Filename = sys::path::filename(InstallName);
  const bool IsFramework = sys::path::parent_path(InstallName)
                               .ends_with((Filename + ".framework").str());
  if (IsFramework) {
    for (const StringRef Path : FrameworkSearchPaths) {
      SmallString<PATH_MAX> FullPath(Path);
      sys::path::append(FullPath, Filename + StringRef(".framework"), Filename);
      if (auto LibOrNull = getLibrary(FullPath))
        return *LibOrNull;
    }
  } else {
    // Copy Apple's linker behavior: If this is a .dylib inside a framework, do
    // not search -L paths.
    bool IsEmbeddedDylib = (sys::path::extension(InstallName) == ".dylib") &&
                           InstallName.contains(".framework/");
    if (!IsEmbeddedDylib) {
      for (const StringRef Path : LibrarySearchPaths) {
        SmallString<PATH_MAX> FullPath(Path);
        sys::path::append(FullPath, Filename);
        if (auto LibOrNull = getLibrary(FullPath))
          return *LibOrNull;
      }
    }
  }

  for (const StringRef Path : SearchPaths) {
    SmallString<PATH_MAX> FullPath(Path);
    sys::path::append(FullPath, InstallName);
    if (auto LibOrNull = getLibrary(FullPath))
      return *LibOrNull;
  }

  return {};
}

} // namespace clang::installapi

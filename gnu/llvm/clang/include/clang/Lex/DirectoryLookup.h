//===--- DirectoryLookup.h - Info for searching for headers -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the DirectoryLookup interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LEX_DIRECTORYLOOKUP_H
#define LLVM_CLANG_LEX_DIRECTORYLOOKUP_H

#include "clang/Basic/LLVM.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/ModuleMap.h"

namespace clang {
class HeaderMap;
class HeaderSearch;
class Module;

/// DirectoryLookup - This class represents one entry in the search list that
/// specifies the search order for directories in \#include directives.  It
/// represents either a directory, a framework, or a headermap.
///
class DirectoryLookup {
public:
  enum LookupType_t {
    LT_NormalDir,
    LT_Framework,
    LT_HeaderMap
  };
private:
  union DLU { // This union is discriminated by isHeaderMap.
    /// Dir - This is the actual directory that we're referring to for a normal
    /// directory or a framework.
    DirectoryEntryRef Dir;

    /// Map - This is the HeaderMap if this is a headermap lookup.
    ///
    const HeaderMap *Map;

    DLU(DirectoryEntryRef Dir) : Dir(Dir) {}
    DLU(const HeaderMap *Map) : Map(Map) {}
  } u;

  /// DirCharacteristic - The type of directory this is: this is an instance of
  /// SrcMgr::CharacteristicKind.
  LLVM_PREFERRED_TYPE(SrcMgr::CharacteristicKind)
  unsigned DirCharacteristic : 3;

  /// LookupType - This indicates whether this DirectoryLookup object is a
  /// normal directory, a framework, or a headermap.
  LLVM_PREFERRED_TYPE(LookupType_t)
  unsigned LookupType : 2;

  /// Whether this is a header map used when building a framework.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsIndexHeaderMap : 1;

  /// Whether we've performed an exhaustive search for module maps
  /// within the subdirectories of this directory.
  LLVM_PREFERRED_TYPE(bool)
  unsigned SearchedAllModuleMaps : 1;

public:
  /// This ctor *does not take ownership* of 'Dir'.
  DirectoryLookup(DirectoryEntryRef Dir, SrcMgr::CharacteristicKind DT,
                  bool isFramework)
      : u(Dir), DirCharacteristic(DT),
        LookupType(isFramework ? LT_Framework : LT_NormalDir),
        IsIndexHeaderMap(false), SearchedAllModuleMaps(false) {}

  /// This ctor *does not take ownership* of 'Map'.
  DirectoryLookup(const HeaderMap *Map, SrcMgr::CharacteristicKind DT,
                  bool isIndexHeaderMap)
      : u(Map), DirCharacteristic(DT), LookupType(LT_HeaderMap),
        IsIndexHeaderMap(isIndexHeaderMap), SearchedAllModuleMaps(false) {}

  /// getLookupType - Return the kind of directory lookup that this is: either a
  /// normal directory, a framework path, or a HeaderMap.
  LookupType_t getLookupType() const { return (LookupType_t)LookupType; }

  /// getName - Return the directory or filename corresponding to this lookup
  /// object.
  StringRef getName() const;

  /// getDir - Return the directory that this entry refers to.
  ///
  const DirectoryEntry *getDir() const {
    return isNormalDir() ? &u.Dir.getDirEntry() : nullptr;
  }

  OptionalDirectoryEntryRef getDirRef() const {
    return isNormalDir() ? OptionalDirectoryEntryRef(u.Dir) : std::nullopt;
  }

  /// getFrameworkDir - Return the directory that this framework refers to.
  ///
  const DirectoryEntry *getFrameworkDir() const {
    return isFramework() ? &u.Dir.getDirEntry() : nullptr;
  }

  OptionalDirectoryEntryRef getFrameworkDirRef() const {
    return isFramework() ? OptionalDirectoryEntryRef(u.Dir) : std::nullopt;
  }

  /// getHeaderMap - Return the directory that this entry refers to.
  ///
  const HeaderMap *getHeaderMap() const {
    return isHeaderMap() ? u.Map : nullptr;
  }

  /// isNormalDir - Return true if this is a normal directory, not a header map.
  bool isNormalDir() const { return getLookupType() == LT_NormalDir; }

  /// isFramework - True if this is a framework directory.
  ///
  bool isFramework() const { return getLookupType() == LT_Framework; }

  /// isHeaderMap - Return true if this is a header map, not a normal directory.
  bool isHeaderMap() const { return getLookupType() == LT_HeaderMap; }

  /// Determine whether we have already searched this entire
  /// directory for module maps.
  bool haveSearchedAllModuleMaps() const { return SearchedAllModuleMaps; }

  /// Specify whether we have already searched all of the subdirectories
  /// for module maps.
  void setSearchedAllModuleMaps(bool SAMM) {
    SearchedAllModuleMaps = SAMM;
  }

  /// DirCharacteristic - The type of directory this is, one of the DirType enum
  /// values.
  SrcMgr::CharacteristicKind getDirCharacteristic() const {
    return (SrcMgr::CharacteristicKind)DirCharacteristic;
  }

  /// Whether this describes a system header directory.
  bool isSystemHeaderDirectory() const {
    return getDirCharacteristic() != SrcMgr::C_User;
  }

  /// Whether this header map is building a framework or not.
  bool isIndexHeaderMap() const {
    return isHeaderMap() && IsIndexHeaderMap;
  }

  /// LookupFile - Lookup the specified file in this search path, returning it
  /// if it exists or returning null if not.
  ///
  /// \param Filename The file to look up relative to the search paths.
  ///
  /// \param HS The header search instance to search with.
  ///
  /// \param IncludeLoc the source location of the #include or #import
  /// directive.
  ///
  /// \param SearchPath If not NULL, will be set to the search path relative
  /// to which the file was found.
  ///
  /// \param RelativePath If not NULL, will be set to the path relative to
  /// SearchPath at which the file was found. This only differs from the
  /// Filename for framework includes.
  ///
  /// \param RequestingModule The module in which the lookup was performed.
  ///
  /// \param SuggestedModule If non-null, and the file found is semantically
  /// part of a known module, this will be set to the module that should
  /// be imported instead of preprocessing/parsing the file found.
  ///
  /// \param [out] InUserSpecifiedSystemFramework If the file is found,
  /// set to true if the file is located in a framework that has been
  /// user-specified to be treated as a system framework.
  ///
  /// \param [out] IsFrameworkFound For a framework directory set to true if
  /// specified '.framework' directory is found.
  ///
  /// \param [out] MappedName if this is a headermap which maps the filename to
  /// a framework include ("Foo.h" -> "Foo/Foo.h"), set the new name to this
  /// vector and point Filename to it.
  OptionalFileEntryRef
  LookupFile(StringRef &Filename, HeaderSearch &HS, SourceLocation IncludeLoc,
             SmallVectorImpl<char> *SearchPath,
             SmallVectorImpl<char> *RelativePath, Module *RequestingModule,
             ModuleMap::KnownHeader *SuggestedModule,
             bool &InUserSpecifiedSystemFramework, bool &IsFrameworkFound,
             bool &IsInHeaderMap, SmallVectorImpl<char> &MappedName,
             bool OpenFile = true) const;

private:
  OptionalFileEntryRef DoFrameworkLookup(
      StringRef Filename, HeaderSearch &HS, SmallVectorImpl<char> *SearchPath,
      SmallVectorImpl<char> *RelativePath, Module *RequestingModule,
      ModuleMap::KnownHeader *SuggestedModule,
      bool &InUserSpecifiedSystemFramework, bool &IsFrameworkFound) const;
};

}  // end namespace clang

#endif

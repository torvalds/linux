//===- DWARFLinkerDeclContext.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DWARFLINKER_CLASSIC_DWARFLINKERDECLCONTEXT_H
#define LLVM_DWARFLINKER_CLASSIC_DWARFLINKERDECLCONTEXT_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/NonRelocatableStringpool.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugLine.h"
#include "llvm/DebugInfo/DWARF/DWARFDie.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include <atomic>

namespace llvm {
namespace dwarf_linker {
namespace classic {

class CompileUnit;
struct DeclMapInfo;

/// Small helper that resolves and caches file paths. This helps reduce the
/// number of calls to realpath which is expensive. We assume the input are
/// files, and cache the realpath of their parent. This way we can quickly
/// resolve different files under the same path.
class CachedPathResolver {
public:
  /// Resolve a path by calling realpath and cache its result. The returned
  /// StringRef is interned in the given \p StringPool.
  StringRef resolve(const std::string &Path,
                    NonRelocatableStringpool &StringPool) {
    StringRef FileName = sys::path::filename(Path);
    StringRef ParentPath = sys::path::parent_path(Path);

    // If the ParentPath has not yet been resolved, resolve and cache it for
    // future look-ups.
    if (!ResolvedPaths.count(ParentPath)) {
      SmallString<256> RealPath;
      sys::fs::real_path(ParentPath, RealPath);
      ResolvedPaths.insert(
          {ParentPath, std::string(RealPath.c_str(), RealPath.size())});
    }

    // Join the file name again with the resolved path.
    SmallString<256> ResolvedPath(ResolvedPaths[ParentPath]);
    sys::path::append(ResolvedPath, FileName);
    return StringPool.internString(ResolvedPath);
  }

private:
  StringMap<std::string> ResolvedPaths;
};

/// A DeclContext is a named program scope that is used for ODR uniquing of
/// types.
///
/// The set of DeclContext for the ODR-subject parts of a Dwarf link is
/// expanded (and uniqued) with each new object file processed. We need to
/// determine the context of each DIE in an linked object file to see if the
/// corresponding type has already been emitted.
///
/// The contexts are conceptually organized as a tree (eg. a function scope is
/// contained in a namespace scope that contains other scopes), but
/// storing/accessing them in an actual tree is too inefficient: we need to be
/// able to very quickly query a context for a given child context by name.
/// Storing a StringMap in each DeclContext would be too space inefficient.
///
/// The solution here is to give each DeclContext a link to its parent (this
/// allows to walk up the tree), but to query the existence of a specific
/// DeclContext using a separate DenseMap keyed on the hash of the fully
/// qualified name of the context.
class DeclContext {
public:
  using Map = DenseSet<DeclContext *, DeclMapInfo>;

  DeclContext() : DefinedInClangModule(0), Parent(*this) {}

  DeclContext(unsigned Hash, uint32_t Line, uint32_t ByteSize, uint16_t Tag,
              StringRef Name, StringRef File, const DeclContext &Parent,
              DWARFDie LastSeenDIE = DWARFDie(), unsigned CUId = 0)
      : QualifiedNameHash(Hash), Line(Line), ByteSize(ByteSize), Tag(Tag),
        DefinedInClangModule(0), Name(Name), File(File), Parent(Parent),
        LastSeenDIE(LastSeenDIE), LastSeenCompileUnitID(CUId) {}

  uint32_t getQualifiedNameHash() const { return QualifiedNameHash; }

  bool setLastSeenDIE(CompileUnit &U, const DWARFDie &Die);

  void setHasCanonicalDIE() { HasCanonicalDIE = true; }

  bool hasCanonicalDIE() const { return HasCanonicalDIE; }

  uint32_t getCanonicalDIEOffset() const { return CanonicalDIEOffset; }
  void setCanonicalDIEOffset(uint32_t Offset) { CanonicalDIEOffset = Offset; }

  bool isDefinedInClangModule() const { return DefinedInClangModule; }
  void setDefinedInClangModule(bool Val) { DefinedInClangModule = Val; }

  uint16_t getTag() const { return Tag; }

private:
  friend DeclMapInfo;

  unsigned QualifiedNameHash = 0;
  uint32_t Line = 0;
  uint32_t ByteSize = 0;
  uint16_t Tag = dwarf::DW_TAG_compile_unit;
  unsigned DefinedInClangModule : 1;
  StringRef Name;
  StringRef File;
  const DeclContext &Parent;
  DWARFDie LastSeenDIE;
  uint32_t LastSeenCompileUnitID = 0;
  std::atomic<uint32_t> CanonicalDIEOffset = {0};
  bool HasCanonicalDIE = false;
};

/// This class gives a tree-like API to the DenseMap that stores the
/// DeclContext objects. It holds the BumpPtrAllocator where these objects will
/// be allocated.
class DeclContextTree {
public:
  /// Get the child of \a Context described by \a DIE in \a Unit. The
  /// required strings will be interned in \a StringPool.
  /// \returns The child DeclContext along with one bit that is set if
  /// this context is invalid.
  ///
  /// An invalid context means it shouldn't be considered for uniquing, but its
  /// not returning null, because some children of that context might be
  /// uniquing candidates.
  ///
  /// FIXME: The invalid bit along the return value is to emulate some
  /// dsymutil-classic functionality.
  PointerIntPair<DeclContext *, 1> getChildDeclContext(DeclContext &Context,
                                                       const DWARFDie &DIE,
                                                       CompileUnit &Unit,
                                                       bool InClangModule);

  DeclContext &getRoot() { return Root; }

private:
  BumpPtrAllocator Allocator;
  DeclContext Root;
  DeclContext::Map Contexts;

  /// Cached resolved paths from the line table.
  /// The key is <UniqueUnitID, FileIdx>.
  using ResolvedPathsMap = DenseMap<std::pair<unsigned, unsigned>, StringRef>;
  ResolvedPathsMap ResolvedPaths;

  /// Helper that resolves and caches fragments of file paths.
  CachedPathResolver PathResolver;

  /// String pool keeping real path bodies.
  NonRelocatableStringpool StringPool;

  StringRef getResolvedPath(CompileUnit &CU, unsigned FileNum,
                            const DWARFDebugLine::LineTable &LineTable);
};

/// Info type for the DenseMap storing the DeclContext pointers.
struct DeclMapInfo : private DenseMapInfo<DeclContext *> {
  using DenseMapInfo<DeclContext *>::getEmptyKey;
  using DenseMapInfo<DeclContext *>::getTombstoneKey;

  static unsigned getHashValue(const DeclContext *Ctxt) {
    return Ctxt->QualifiedNameHash;
  }

  static bool isEqual(const DeclContext *LHS, const DeclContext *RHS) {
    if (RHS == getEmptyKey() || RHS == getTombstoneKey())
      return RHS == LHS;
    return LHS->QualifiedNameHash == RHS->QualifiedNameHash &&
           LHS->Line == RHS->Line && LHS->ByteSize == RHS->ByteSize &&
           LHS->Name.data() == RHS->Name.data() &&
           LHS->File.data() == RHS->File.data() &&
           LHS->Parent.QualifiedNameHash == RHS->Parent.QualifiedNameHash;
  }
};

} // end of namespace classic
} // end of namespace dwarf_linker
} // end of namespace llvm

#endif // LLVM_DWARFLINKER_CLASSIC_DWARFLINKERDECLCONTEXT_H

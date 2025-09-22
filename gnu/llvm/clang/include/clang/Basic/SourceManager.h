//===- SourceManager.h - Track and cache source files -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Defines the SourceManager interface.
///
/// There are three different types of locations in a %file: a spelling
/// location, an expansion location, and a presumed location.
///
/// Given an example of:
/// \code
/// #define min(x, y) x < y ? x : y
/// \endcode
///
/// and then later on a use of min:
/// \code
/// #line 17
/// return min(a, b);
/// \endcode
///
/// The expansion location is the line in the source code where the macro
/// was expanded (the return statement), the spelling location is the
/// location in the source where the macro was originally defined,
/// and the presumed location is where the line directive states that
/// the line is 17, or any other line.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_SOURCEMANAGER_H
#define LLVM_CLANG_BASIC_SOURCEMANAGER_H

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileEntry.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/PagedVector.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MemoryBuffer.h"
#include <cassert>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace clang {

class ASTReader;
class ASTWriter;
class FileManager;
class LineTableInfo;
class SourceManager;

/// Public enums and private classes that are part of the
/// SourceManager implementation.
namespace SrcMgr {

/// Indicates whether a file or directory holds normal user code,
/// system code, or system code which is implicitly 'extern "C"' in C++ mode.
///
/// Entire directories can be tagged with this (this is maintained by
/// DirectoryLookup and friends) as can specific FileInfos when a \#pragma
/// system_header is seen or in various other cases.
///
enum CharacteristicKind {
  C_User,
  C_System,
  C_ExternCSystem,
  C_User_ModuleMap,
  C_System_ModuleMap
};

/// Determine whether a file / directory characteristic is for system code.
inline bool isSystem(CharacteristicKind CK) {
  return CK != C_User && CK != C_User_ModuleMap;
}

/// Determine whether a file characteristic is for a module map.
inline bool isModuleMap(CharacteristicKind CK) {
  return CK == C_User_ModuleMap || CK == C_System_ModuleMap;
}

/// Mapping of line offsets into a source file. This does not own the storage
/// for the line numbers.
class LineOffsetMapping {
public:
  explicit operator bool() const { return Storage; }
  unsigned size() const {
    assert(Storage);
    return Storage[0];
  }
  ArrayRef<unsigned> getLines() const {
    assert(Storage);
    return ArrayRef<unsigned>(Storage + 1, Storage + 1 + size());
  }
  const unsigned *begin() const { return getLines().begin(); }
  const unsigned *end() const { return getLines().end(); }
  const unsigned &operator[](int I) const { return getLines()[I]; }

  static LineOffsetMapping get(llvm::MemoryBufferRef Buffer,
                               llvm::BumpPtrAllocator &Alloc);

  LineOffsetMapping() = default;
  LineOffsetMapping(ArrayRef<unsigned> LineOffsets,
                    llvm::BumpPtrAllocator &Alloc);

private:
  /// First element is the size, followed by elements at off-by-one indexes.
  unsigned *Storage = nullptr;
};

/// One instance of this struct is kept for every file loaded or used.
///
/// This object owns the MemoryBuffer object.
class alignas(8) ContentCache {
  /// The actual buffer containing the characters from the input
  /// file.
  mutable std::unique_ptr<llvm::MemoryBuffer> Buffer;

public:
  /// Reference to the file entry representing this ContentCache.
  ///
  /// This reference does not own the FileEntry object.
  ///
  /// It is possible for this to be NULL if the ContentCache encapsulates
  /// an imaginary text buffer.
  ///
  /// FIXME: Make non-optional using a virtual file as needed, remove \c
  /// Filename and use \c OrigEntry.getNameAsRequested() instead.
  OptionalFileEntryRef OrigEntry;

  /// References the file which the contents were actually loaded from.
  ///
  /// Can be different from 'Entry' if we overridden the contents of one file
  /// with the contents of another file.
  OptionalFileEntryRef ContentsEntry;

  /// The filename that is used to access OrigEntry.
  ///
  /// FIXME: Remove this once OrigEntry is a FileEntryRef with a stable name.
  StringRef Filename;

  /// A bump pointer allocated array of offsets for each source line.
  ///
  /// This is lazily computed.  The lines are owned by the SourceManager
  /// BumpPointerAllocator object.
  mutable LineOffsetMapping SourceLineCache;

  /// Indicates whether the buffer itself was provided to override
  /// the actual file contents.
  ///
  /// When true, the original entry may be a virtual file that does not
  /// exist.
  LLVM_PREFERRED_TYPE(bool)
  unsigned BufferOverridden : 1;

  /// True if this content cache was initially created for a source file
  /// considered to be volatile (likely to change between stat and open).
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsFileVolatile : 1;

  /// True if this file may be transient, that is, if it might not
  /// exist at some later point in time when this content entry is used,
  /// after serialization and deserialization.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsTransient : 1;

  LLVM_PREFERRED_TYPE(bool)
  mutable unsigned IsBufferInvalid : 1;

  ContentCache()
      : OrigEntry(std::nullopt), ContentsEntry(std::nullopt),
        BufferOverridden(false), IsFileVolatile(false), IsTransient(false),
        IsBufferInvalid(false) {}

  ContentCache(FileEntryRef Ent) : ContentCache(Ent, Ent) {}

  ContentCache(FileEntryRef Ent, FileEntryRef contentEnt)
      : OrigEntry(Ent), ContentsEntry(contentEnt), BufferOverridden(false),
        IsFileVolatile(false), IsTransient(false), IsBufferInvalid(false) {}

  /// The copy ctor does not allow copies where source object has either
  /// a non-NULL Buffer or SourceLineCache.  Ownership of allocated memory
  /// is not transferred, so this is a logical error.
  ContentCache(const ContentCache &RHS)
      : BufferOverridden(false), IsFileVolatile(false), IsTransient(false),
        IsBufferInvalid(false) {
    OrigEntry = RHS.OrigEntry;
    ContentsEntry = RHS.ContentsEntry;

    assert(!RHS.Buffer && !RHS.SourceLineCache &&
           "Passed ContentCache object cannot own a buffer.");
  }

  ContentCache &operator=(const ContentCache &RHS) = delete;

  /// Returns the memory buffer for the associated content.
  ///
  /// \param Diag Object through which diagnostics will be emitted if the
  ///   buffer cannot be retrieved.
  ///
  /// \param Loc If specified, is the location that invalid file diagnostics
  ///   will be emitted at.
  std::optional<llvm::MemoryBufferRef>
  getBufferOrNone(DiagnosticsEngine &Diag, FileManager &FM,
                  SourceLocation Loc = SourceLocation()) const;

  /// Returns the size of the content encapsulated by this
  /// ContentCache.
  ///
  /// This can be the size of the source file or the size of an
  /// arbitrary scratch buffer.  If the ContentCache encapsulates a source
  /// file this size is retrieved from the file's FileEntry.
  unsigned getSize() const;

  /// Returns the number of bytes actually mapped for this
  /// ContentCache.
  ///
  /// This can be 0 if the MemBuffer was not actually expanded.
  unsigned getSizeBytesMapped() const;

  /// Returns the kind of memory used to back the memory buffer for
  /// this content cache.  This is used for performance analysis.
  llvm::MemoryBuffer::BufferKind getMemoryBufferKind() const;

  /// Return the buffer, only if it has been loaded.
  std::optional<llvm::MemoryBufferRef> getBufferIfLoaded() const {
    if (Buffer)
      return Buffer->getMemBufferRef();
    return std::nullopt;
  }

  /// Return a StringRef to the source buffer data, only if it has already
  /// been loaded.
  std::optional<StringRef> getBufferDataIfLoaded() const {
    if (Buffer)
      return Buffer->getBuffer();
    return std::nullopt;
  }

  /// Set the buffer.
  void setBuffer(std::unique_ptr<llvm::MemoryBuffer> B) {
    IsBufferInvalid = false;
    Buffer = std::move(B);
  }

  /// Set the buffer to one that's not owned (or to nullptr).
  ///
  /// \pre Buffer cannot already be set.
  void setUnownedBuffer(std::optional<llvm::MemoryBufferRef> B) {
    assert(!Buffer && "Expected to be called right after construction");
    if (B)
      setBuffer(llvm::MemoryBuffer::getMemBuffer(*B));
  }

  // If BufStr has an invalid BOM, returns the BOM name; otherwise, returns
  // nullptr
  static const char *getInvalidBOM(StringRef BufStr);
};

// Assert that the \c ContentCache objects will always be 8-byte aligned so
// that we can pack 3 bits of integer into pointers to such objects.
static_assert(alignof(ContentCache) >= 8,
              "ContentCache must be 8-byte aligned.");

/// Information about a FileID, basically just the logical file
/// that it represents and include stack information.
///
/// Each FileInfo has include stack information, indicating where it came
/// from. This information encodes the \#include chain that a token was
/// expanded from. The main include file has an invalid IncludeLoc.
///
/// FileInfo should not grow larger than ExpansionInfo. Doing so will
/// cause memory to bloat in compilations with many unloaded macro
/// expansions, since the two data structurs are stored in a union in
/// SLocEntry. Extra fields should instead go in "ContentCache *", which
/// stores file contents and other bits on the side.
///
class FileInfo {
  friend class clang::SourceManager;
  friend class clang::ASTWriter;
  friend class clang::ASTReader;

  /// The location of the \#include that brought in this file.
  ///
  /// This is an invalid SLOC for the main file (top of the \#include chain).
  SourceLocation IncludeLoc;

  /// Number of FileIDs (files and macros) that were created during
  /// preprocessing of this \#include, including this SLocEntry.
  ///
  /// Zero means the preprocessor didn't provide such info for this SLocEntry.
  unsigned NumCreatedFIDs : 31;

  /// Whether this FileInfo has any \#line directives.
  LLVM_PREFERRED_TYPE(bool)
  unsigned HasLineDirectives : 1;

  /// The content cache and the characteristic of the file.
  llvm::PointerIntPair<const ContentCache *, 3, CharacteristicKind>
      ContentAndKind;

public:
  /// Return a FileInfo object.
  static FileInfo get(SourceLocation IL, ContentCache &Con,
                      CharacteristicKind FileCharacter, StringRef Filename) {
    FileInfo X;
    X.IncludeLoc = IL;
    X.NumCreatedFIDs = 0;
    X.HasLineDirectives = false;
    X.ContentAndKind.setPointer(&Con);
    X.ContentAndKind.setInt(FileCharacter);
    Con.Filename = Filename;
    return X;
  }

  SourceLocation getIncludeLoc() const {
    return IncludeLoc;
  }

  const ContentCache &getContentCache() const {
    return *ContentAndKind.getPointer();
  }

  /// Return whether this is a system header or not.
  CharacteristicKind getFileCharacteristic() const {
    return ContentAndKind.getInt();
  }

  /// Return true if this FileID has \#line directives in it.
  bool hasLineDirectives() const { return HasLineDirectives; }

  /// Set the flag that indicates that this FileID has
  /// line table entries associated with it.
  void setHasLineDirectives() { HasLineDirectives = true; }

  /// Returns the name of the file that was used when the file was loaded from
  /// the underlying file system.
  StringRef getName() const { return getContentCache().Filename; }
};

/// Each ExpansionInfo encodes the expansion location - where
/// the token was ultimately expanded, and the SpellingLoc - where the actual
/// character data for the token came from.
class ExpansionInfo {
  // Really these are all SourceLocations.

  /// Where the spelling for the token can be found.
  SourceLocation SpellingLoc;

  /// In a macro expansion, ExpansionLocStart and ExpansionLocEnd
  /// indicate the start and end of the expansion. In object-like macros,
  /// they will be the same. In a function-like macro expansion, the start
  /// will be the identifier and the end will be the ')'. Finally, in
  /// macro-argument instantiations, the end will be 'SourceLocation()', an
  /// invalid location.
  SourceLocation ExpansionLocStart, ExpansionLocEnd;

  /// Whether the expansion range is a token range.
  bool ExpansionIsTokenRange;

public:
  SourceLocation getSpellingLoc() const {
    return SpellingLoc.isInvalid() ? getExpansionLocStart() : SpellingLoc;
  }

  SourceLocation getExpansionLocStart() const {
    return ExpansionLocStart;
  }

  SourceLocation getExpansionLocEnd() const {
    return ExpansionLocEnd.isInvalid() ? getExpansionLocStart()
                                       : ExpansionLocEnd;
  }

  bool isExpansionTokenRange() const { return ExpansionIsTokenRange; }

  CharSourceRange getExpansionLocRange() const {
    return CharSourceRange(
        SourceRange(getExpansionLocStart(), getExpansionLocEnd()),
        isExpansionTokenRange());
  }

  bool isMacroArgExpansion() const {
    // Note that this needs to return false for default constructed objects.
    return getExpansionLocStart().isValid() && ExpansionLocEnd.isInvalid();
  }

  bool isMacroBodyExpansion() const {
    return getExpansionLocStart().isValid() && ExpansionLocEnd.isValid();
  }

  bool isFunctionMacroExpansion() const {
    return getExpansionLocStart().isValid() &&
           getExpansionLocStart() != getExpansionLocEnd();
  }

  /// Return a ExpansionInfo for an expansion.
  ///
  /// Start and End specify the expansion range (where the macro is
  /// expanded), and SpellingLoc specifies the spelling location (where
  /// the characters from the token come from). All three can refer to
  /// normal File SLocs or expansion locations.
  static ExpansionInfo create(SourceLocation SpellingLoc, SourceLocation Start,
                              SourceLocation End,
                              bool ExpansionIsTokenRange = true) {
    ExpansionInfo X;
    X.SpellingLoc = SpellingLoc;
    X.ExpansionLocStart = Start;
    X.ExpansionLocEnd = End;
    X.ExpansionIsTokenRange = ExpansionIsTokenRange;
    return X;
  }

  /// Return a special ExpansionInfo for the expansion of
  /// a macro argument into a function-like macro's body.
  ///
  /// ExpansionLoc specifies the expansion location (where the macro is
  /// expanded). This doesn't need to be a range because a macro is always
  /// expanded at a macro parameter reference, and macro parameters are
  /// always exactly one token. SpellingLoc specifies the spelling location
  /// (where the characters from the token come from). ExpansionLoc and
  /// SpellingLoc can both refer to normal File SLocs or expansion locations.
  ///
  /// Given the code:
  /// \code
  ///   #define F(x) f(x)
  ///   F(42);
  /// \endcode
  ///
  /// When expanding '\c F(42)', the '\c x' would call this with an
  /// SpellingLoc pointing at '\c 42' and an ExpansionLoc pointing at its
  /// location in the definition of '\c F'.
  static ExpansionInfo createForMacroArg(SourceLocation SpellingLoc,
                                         SourceLocation ExpansionLoc) {
    // We store an intentionally invalid source location for the end of the
    // expansion range to mark that this is a macro argument location rather
    // than a normal one.
    return create(SpellingLoc, ExpansionLoc, SourceLocation());
  }

  /// Return a special ExpansionInfo representing a token that ends
  /// prematurely. This is used to model a '>>' token that has been split
  /// into '>' tokens and similar cases. Unlike for the other forms of
  /// expansion, the expansion range in this case is a character range, not
  /// a token range.
  static ExpansionInfo createForTokenSplit(SourceLocation SpellingLoc,
                                           SourceLocation Start,
                                           SourceLocation End) {
    return create(SpellingLoc, Start, End, false);
  }
};

// Assert that the \c FileInfo objects are no bigger than \c ExpansionInfo
// objects. This controls the size of \c SLocEntry, of which we have one for
// each macro expansion. The number of (unloaded) macro expansions can be
// very large. Any other fields needed in FileInfo should go in ContentCache.
static_assert(sizeof(FileInfo) <= sizeof(ExpansionInfo),
              "FileInfo must be no larger than ExpansionInfo.");

/// This is a discriminated union of FileInfo and ExpansionInfo.
///
/// SourceManager keeps an array of these objects, and they are uniquely
/// identified by the FileID datatype.
class SLocEntry {
  static constexpr int OffsetBits = 8 * sizeof(SourceLocation::UIntTy) - 1;
  SourceLocation::UIntTy Offset : OffsetBits;
  LLVM_PREFERRED_TYPE(bool)
  SourceLocation::UIntTy IsExpansion : 1;
  union {
    FileInfo File;
    ExpansionInfo Expansion;
  };

public:
  SLocEntry() : Offset(), IsExpansion(), File() {}

  SourceLocation::UIntTy getOffset() const { return Offset; }

  bool isExpansion() const { return IsExpansion; }
  bool isFile() const { return !isExpansion(); }

  const FileInfo &getFile() const {
    return const_cast<SLocEntry *>(this)->getFile();
  }

  FileInfo &getFile() {
    assert(isFile() && "Not a file SLocEntry!");
    return File;
  }

  const ExpansionInfo &getExpansion() const {
    assert(isExpansion() && "Not a macro expansion SLocEntry!");
    return Expansion;
  }

  /// Creates an incomplete SLocEntry that is only able to report its offset.
  static SLocEntry getOffsetOnly(SourceLocation::UIntTy Offset) {
    assert(!(Offset & (1ULL << OffsetBits)) && "Offset is too large");
    SLocEntry E;
    E.Offset = Offset;
    return E;
  }

  static SLocEntry get(SourceLocation::UIntTy Offset, const FileInfo &FI) {
    assert(!(Offset & (1ULL << OffsetBits)) && "Offset is too large");
    SLocEntry E;
    E.Offset = Offset;
    E.IsExpansion = false;
    E.File = FI;
    return E;
  }

  static SLocEntry get(SourceLocation::UIntTy Offset,
                       const ExpansionInfo &Expansion) {
    assert(!(Offset & (1ULL << OffsetBits)) && "Offset is too large");
    SLocEntry E;
    E.Offset = Offset;
    E.IsExpansion = true;
    new (&E.Expansion) ExpansionInfo(Expansion);
    return E;
  }
};

} // namespace SrcMgr

/// External source of source location entries.
class ExternalSLocEntrySource {
public:
  virtual ~ExternalSLocEntrySource();

  /// Read the source location entry with index ID, which will always be
  /// less than -1.
  ///
  /// \returns true if an error occurred that prevented the source-location
  /// entry from being loaded.
  virtual bool ReadSLocEntry(int ID) = 0;

  /// Get the index ID for the loaded SourceLocation offset.
  ///
  /// \returns Invalid index ID (0) if an error occurred that prevented the
  /// SLocEntry  from being loaded.
  virtual int getSLocEntryID(SourceLocation::UIntTy SLocOffset) = 0;

  /// Retrieve the module import location and name for the given ID, if
  /// in fact it was loaded from a module (rather than, say, a precompiled
  /// header).
  virtual std::pair<SourceLocation, StringRef> getModuleImportLoc(int ID) = 0;
};

/// Holds the cache used by isBeforeInTranslationUnit.
///
/// The cache structure is complex enough to be worth breaking out of
/// SourceManager.
class InBeforeInTUCacheEntry {
  /// The FileID's of the cached query.
  ///
  /// If these match up with a subsequent query, the result can be reused.
  FileID LQueryFID, RQueryFID;

  /// The relative order of FileIDs that the CommonFID *immediately* includes.
  ///
  /// This is used to compare macro expansion locations.
  bool LChildBeforeRChild;

  /// The file found in common between the two \#include traces, i.e.,
  /// the nearest common ancestor of the \#include tree.
  FileID CommonFID;

  /// The offset of the previous query in CommonFID.
  ///
  /// Usually, this represents the location of the \#include for QueryFID, but
  /// if LQueryFID is a parent of RQueryFID (or vice versa) then these can be a
  /// random token in the parent.
  unsigned LCommonOffset, RCommonOffset;

public:
  InBeforeInTUCacheEntry() = default;
  InBeforeInTUCacheEntry(FileID L, FileID R) : LQueryFID(L), RQueryFID(R) {
    assert(L != R);
  }

  /// Return true if the currently cached values match up with
  /// the specified LHS/RHS query.
  ///
  /// If not, we can't use the cache.
  bool isCacheValid() const {
    return CommonFID.isValid();
  }

  /// If the cache is valid, compute the result given the
  /// specified offsets in the LHS/RHS FileID's.
  bool getCachedResult(unsigned LOffset, unsigned ROffset) const {
    // If one of the query files is the common file, use the offset.  Otherwise,
    // use the #include loc in the common file.
    if (LQueryFID != CommonFID) LOffset = LCommonOffset;
    if (RQueryFID != CommonFID) ROffset = RCommonOffset;

    // It is common for multiple macro expansions to be "included" from the same
    // location (expansion location), in which case use the order of the FileIDs
    // to determine which came first. This will also take care the case where
    // one of the locations points at the inclusion/expansion point of the other
    // in which case its FileID will come before the other.
    if (LOffset == ROffset)
      return LChildBeforeRChild;

    return LOffset < ROffset;
  }

  /// Set up a new query.
  /// If it matches the old query, we can keep the cached answer.
  void setQueryFIDs(FileID LHS, FileID RHS) {
    assert(LHS != RHS);
    if (LQueryFID != LHS || RQueryFID != RHS) {
      LQueryFID = LHS;
      RQueryFID = RHS;
      CommonFID = FileID();
    }
  }

  void setCommonLoc(FileID commonFID, unsigned lCommonOffset,
                    unsigned rCommonOffset, bool LParentBeforeRParent) {
    CommonFID = commonFID;
    LCommonOffset = lCommonOffset;
    RCommonOffset = rCommonOffset;
    LChildBeforeRChild = LParentBeforeRParent;
  }
};

/// The stack used when building modules on demand, which is used
/// to provide a link between the source managers of the different compiler
/// instances.
using ModuleBuildStack = ArrayRef<std::pair<std::string, FullSourceLoc>>;

/// This class handles loading and caching of source files into memory.
///
/// This object owns the MemoryBuffer objects for all of the loaded
/// files and assigns unique FileID's for each unique \#include chain.
///
/// The SourceManager can be queried for information about SourceLocation
/// objects, turning them into either spelling or expansion locations. Spelling
/// locations represent where the bytes corresponding to a token came from and
/// expansion locations represent where the location is in the user's view. In
/// the case of a macro expansion, for example, the spelling location indicates
/// where the expanded token came from and the expansion location specifies
/// where it was expanded.
class SourceManager : public RefCountedBase<SourceManager> {
  /// DiagnosticsEngine object.
  DiagnosticsEngine &Diag;

  FileManager &FileMgr;

  mutable llvm::BumpPtrAllocator ContentCacheAlloc;

  /// Memoized information about all of the files tracked by this
  /// SourceManager.
  ///
  /// This map allows us to merge ContentCache entries based
  /// on their FileEntry*.  All ContentCache objects will thus have unique,
  /// non-null, FileEntry pointers.
  llvm::DenseMap<FileEntryRef, SrcMgr::ContentCache*> FileInfos;

  /// True if the ContentCache for files that are overridden by other
  /// files, should report the original file name. Defaults to true.
  bool OverridenFilesKeepOriginalName = true;

  /// True if non-system source files should be treated as volatile
  /// (likely to change while trying to use them). Defaults to false.
  bool UserFilesAreVolatile;

  /// True if all files read during this compilation should be treated
  /// as transient (may not be present in later compilations using a module
  /// file created from this compilation). Defaults to false.
  bool FilesAreTransient = false;

  struct OverriddenFilesInfoTy {
    /// Files that have been overridden with the contents from another
    /// file.
    llvm::DenseMap<const FileEntry *, FileEntryRef> OverriddenFiles;

    /// Files that were overridden with a memory buffer.
    llvm::DenseSet<const FileEntry *> OverriddenFilesWithBuffer;
  };

  /// Lazily create the object keeping overridden files info, since
  /// it is uncommonly used.
  std::unique_ptr<OverriddenFilesInfoTy> OverriddenFilesInfo;

  OverriddenFilesInfoTy &getOverriddenFilesInfo() {
    if (!OverriddenFilesInfo)
      OverriddenFilesInfo.reset(new OverriddenFilesInfoTy);
    return *OverriddenFilesInfo;
  }

  /// Information about various memory buffers that we have read in.
  ///
  /// All FileEntry* within the stored ContentCache objects are NULL,
  /// as they do not refer to a file.
  std::vector<SrcMgr::ContentCache*> MemBufferInfos;

  /// The table of SLocEntries that are local to this module.
  ///
  /// Positive FileIDs are indexes into this table. Entry 0 indicates an invalid
  /// expansion.
  SmallVector<SrcMgr::SLocEntry, 0> LocalSLocEntryTable;

  /// The table of SLocEntries that are loaded from other modules.
  ///
  /// Negative FileIDs are indexes into this table. To get from ID to an index,
  /// use (-ID - 2).
  llvm::PagedVector<SrcMgr::SLocEntry> LoadedSLocEntryTable;

  /// For each allocation in LoadedSLocEntryTable, we keep the first FileID.
  /// We assume exactly one allocation per AST file, and use that to determine
  /// whether two FileIDs come from the same AST file.
  SmallVector<FileID, 0> LoadedSLocEntryAllocBegin;

  /// The starting offset of the next local SLocEntry.
  ///
  /// This is LocalSLocEntryTable.back().Offset + the size of that entry.
  SourceLocation::UIntTy NextLocalOffset;

  /// The starting offset of the latest batch of loaded SLocEntries.
  ///
  /// This is LoadedSLocEntryTable.back().Offset, except that that entry might
  /// not have been loaded, so that value would be unknown.
  SourceLocation::UIntTy CurrentLoadedOffset;

  /// The highest possible offset is 2^31-1 (2^63-1 for 64-bit source
  /// locations), so CurrentLoadedOffset starts at 2^31 (2^63 resp.).
  static const SourceLocation::UIntTy MaxLoadedOffset =
      1ULL << (8 * sizeof(SourceLocation::UIntTy) - 1);

  /// A bitmap that indicates whether the entries of LoadedSLocEntryTable
  /// have already been loaded from the external source.
  ///
  /// Same indexing as LoadedSLocEntryTable.
  llvm::BitVector SLocEntryLoaded;

  /// A bitmap that indicates whether the entries of LoadedSLocEntryTable
  /// have already had their offset loaded from the external source.
  ///
  /// Superset of SLocEntryLoaded. Same indexing as SLocEntryLoaded.
  llvm::BitVector SLocEntryOffsetLoaded;

  /// An external source for source location entries.
  ExternalSLocEntrySource *ExternalSLocEntries = nullptr;

  /// A one-entry cache to speed up getFileID.
  ///
  /// LastFileIDLookup records the last FileID looked up or created, because it
  /// is very common to look up many tokens from the same file.
  mutable FileID LastFileIDLookup;

  /// Holds information for \#line directives.
  ///
  /// This is referenced by indices from SLocEntryTable.
  std::unique_ptr<LineTableInfo> LineTable;

  /// These ivars serve as a cache used in the getLineNumber
  /// method which is used to speedup getLineNumber calls to nearby locations.
  mutable FileID LastLineNoFileIDQuery;
  mutable const SrcMgr::ContentCache *LastLineNoContentCache;
  mutable unsigned LastLineNoFilePos;
  mutable unsigned LastLineNoResult;

  /// The file ID for the main source file of the translation unit.
  FileID MainFileID;

  /// The file ID for the precompiled preamble there is one.
  FileID PreambleFileID;

  // Statistics for -print-stats.
  mutable unsigned NumLinearScans = 0;
  mutable unsigned NumBinaryProbes = 0;

  /// Associates a FileID with its "included/expanded in" decomposed
  /// location.
  ///
  /// Used to cache results from and speed-up \c getDecomposedIncludedLoc
  /// function.
  mutable llvm::DenseMap<FileID, std::pair<FileID, unsigned>> IncludedLocMap;

  /// The key value into the IsBeforeInTUCache table.
  using IsBeforeInTUCacheKey = std::pair<FileID, FileID>;

  /// The IsBeforeInTranslationUnitCache is a mapping from FileID pairs
  /// to cache results.
  using InBeforeInTUCache =
      llvm::DenseMap<IsBeforeInTUCacheKey, InBeforeInTUCacheEntry>;

  /// Cache results for the isBeforeInTranslationUnit method.
  mutable InBeforeInTUCache IBTUCache;
  mutable InBeforeInTUCacheEntry IBTUCacheOverflow;

  /// Return the cache entry for comparing the given file IDs
  /// for isBeforeInTranslationUnit.
  InBeforeInTUCacheEntry &getInBeforeInTUCache(FileID LFID, FileID RFID) const;

  // Cache for the "fake" buffer used for error-recovery purposes.
  mutable std::unique_ptr<llvm::MemoryBuffer> FakeBufferForRecovery;

  mutable std::unique_ptr<SrcMgr::ContentCache> FakeContentCacheForRecovery;

  mutable std::unique_ptr<SrcMgr::SLocEntry> FakeSLocEntryForRecovery;

  /// Lazily computed map of macro argument chunks to their expanded
  /// source location.
  using MacroArgsMap = std::map<unsigned, SourceLocation>;

  mutable llvm::DenseMap<FileID, std::unique_ptr<MacroArgsMap>>
      MacroArgsCacheMap;

  /// The stack of modules being built, which is used to detect
  /// cycles in the module dependency graph as modules are being built, as
  /// well as to describe why we're rebuilding a particular module.
  ///
  /// There is no way to set this value from the command line. If we ever need
  /// to do so (e.g., if on-demand module construction moves out-of-process),
  /// we can add a cc1-level option to do so.
  SmallVector<std::pair<std::string, FullSourceLoc>, 2> StoredModuleBuildStack;

public:
  SourceManager(DiagnosticsEngine &Diag, FileManager &FileMgr,
                bool UserFilesAreVolatile = false);
  explicit SourceManager(const SourceManager &) = delete;
  SourceManager &operator=(const SourceManager &) = delete;
  ~SourceManager();

  void clearIDTables();

  /// Initialize this source manager suitably to replay the compilation
  /// described by \p Old. Requires that \p Old outlive \p *this.
  void initializeForReplay(const SourceManager &Old);

  DiagnosticsEngine &getDiagnostics() const { return Diag; }

  FileManager &getFileManager() const { return FileMgr; }

  /// Set true if the SourceManager should report the original file name
  /// for contents of files that were overridden by other files. Defaults to
  /// true.
  void setOverridenFilesKeepOriginalName(bool value) {
    OverridenFilesKeepOriginalName = value;
  }

  /// True if non-system source files should be treated as volatile
  /// (likely to change while trying to use them).
  bool userFilesAreVolatile() const { return UserFilesAreVolatile; }

  /// Retrieve the module build stack.
  ModuleBuildStack getModuleBuildStack() const {
    return StoredModuleBuildStack;
  }

  /// Set the module build stack.
  void setModuleBuildStack(ModuleBuildStack stack) {
    StoredModuleBuildStack.clear();
    StoredModuleBuildStack.append(stack.begin(), stack.end());
  }

  /// Push an entry to the module build stack.
  void pushModuleBuildStack(StringRef moduleName, FullSourceLoc importLoc) {
    StoredModuleBuildStack.push_back(std::make_pair(moduleName.str(),importLoc));
  }

  //===--------------------------------------------------------------------===//
  // MainFileID creation and querying methods.
  //===--------------------------------------------------------------------===//

  /// Returns the FileID of the main source file.
  FileID getMainFileID() const { return MainFileID; }

  /// Set the file ID for the main source file.
  void setMainFileID(FileID FID) {
    MainFileID = FID;
  }

  /// Returns true when the given FileEntry corresponds to the main file.
  ///
  /// The main file should be set prior to calling this function.
  bool isMainFile(const FileEntry &SourceFile);

  /// Set the file ID for the precompiled preamble.
  void setPreambleFileID(FileID Preamble) {
    assert(PreambleFileID.isInvalid() && "PreambleFileID already set!");
    PreambleFileID = Preamble;
  }

  /// Get the file ID for the precompiled preamble if there is one.
  FileID getPreambleFileID() const { return PreambleFileID; }

  //===--------------------------------------------------------------------===//
  // Methods to create new FileID's and macro expansions.
  //===--------------------------------------------------------------------===//

  /// Create a new FileID that represents the specified file
  /// being \#included from the specified IncludePosition.
  FileID createFileID(FileEntryRef SourceFile, SourceLocation IncludePos,
                      SrcMgr::CharacteristicKind FileCharacter,
                      int LoadedID = 0,
                      SourceLocation::UIntTy LoadedOffset = 0);

  /// Create a new FileID that represents the specified memory buffer.
  ///
  /// This does no caching of the buffer and takes ownership of the
  /// MemoryBuffer, so only pass a MemoryBuffer to this once.
  FileID createFileID(std::unique_ptr<llvm::MemoryBuffer> Buffer,
                      SrcMgr::CharacteristicKind FileCharacter = SrcMgr::C_User,
                      int LoadedID = 0, SourceLocation::UIntTy LoadedOffset = 0,
                      SourceLocation IncludeLoc = SourceLocation());

  /// Create a new FileID that represents the specified memory buffer.
  ///
  /// This does not take ownership of the MemoryBuffer. The memory buffer must
  /// outlive the SourceManager.
  FileID createFileID(const llvm::MemoryBufferRef &Buffer,
                      SrcMgr::CharacteristicKind FileCharacter = SrcMgr::C_User,
                      int LoadedID = 0, SourceLocation::UIntTy LoadedOffset = 0,
                      SourceLocation IncludeLoc = SourceLocation());

  /// Get the FileID for \p SourceFile if it exists. Otherwise, create a
  /// new FileID for the \p SourceFile.
  FileID getOrCreateFileID(FileEntryRef SourceFile,
                           SrcMgr::CharacteristicKind FileCharacter);

  /// Creates an expansion SLocEntry for the substitution of an argument into a
  /// function-like macro's body. Returns the start of the expansion.
  ///
  /// The macro argument was written at \p SpellingLoc with length \p Length.
  /// \p ExpansionLoc is the parameter name in the (expanded) macro body.
  SourceLocation createMacroArgExpansionLoc(SourceLocation SpellingLoc,
                                            SourceLocation ExpansionLoc,
                                            unsigned Length);

  /// Creates an expansion SLocEntry for a macro use. Returns its start.
  ///
  /// The macro body begins at \p SpellingLoc with length \p Length.
  /// The macro use spans [ExpansionLocStart, ExpansionLocEnd].
  SourceLocation createExpansionLoc(SourceLocation SpellingLoc,
                                    SourceLocation ExpansionLocStart,
                                    SourceLocation ExpansionLocEnd,
                                    unsigned Length,
                                    bool ExpansionIsTokenRange = true,
                                    int LoadedID = 0,
                                    SourceLocation::UIntTy LoadedOffset = 0);

  /// Return a new SourceLocation that encodes that the token starting
  /// at \p TokenStart ends prematurely at \p TokenEnd.
  SourceLocation createTokenSplitLoc(SourceLocation SpellingLoc,
                                     SourceLocation TokenStart,
                                     SourceLocation TokenEnd);

  /// Retrieve the memory buffer associated with the given file.
  ///
  /// Returns std::nullopt if the buffer is not valid.
  std::optional<llvm::MemoryBufferRef>
  getMemoryBufferForFileOrNone(FileEntryRef File);

  /// Retrieve the memory buffer associated with the given file.
  ///
  /// Returns a fake buffer if there isn't a real one.
  llvm::MemoryBufferRef getMemoryBufferForFileOrFake(FileEntryRef File) {
    if (auto B = getMemoryBufferForFileOrNone(File))
      return *B;
    return getFakeBufferForRecovery();
  }

  /// Override the contents of the given source file by providing an
  /// already-allocated buffer.
  ///
  /// \param SourceFile the source file whose contents will be overridden.
  ///
  /// \param Buffer the memory buffer whose contents will be used as the
  /// data in the given source file.
  void overrideFileContents(FileEntryRef SourceFile,
                            const llvm::MemoryBufferRef &Buffer) {
    overrideFileContents(SourceFile, llvm::MemoryBuffer::getMemBuffer(Buffer));
  }

  /// Override the contents of the given source file by providing an
  /// already-allocated buffer.
  ///
  /// \param SourceFile the source file whose contents will be overridden.
  ///
  /// \param Buffer the memory buffer whose contents will be used as the
  /// data in the given source file.
  void overrideFileContents(FileEntryRef SourceFile,
                            std::unique_ptr<llvm::MemoryBuffer> Buffer);

  /// Override the given source file with another one.
  ///
  /// \param SourceFile the source file which will be overridden.
  ///
  /// \param NewFile the file whose contents will be used as the
  /// data instead of the contents of the given source file.
  void overrideFileContents(const FileEntry *SourceFile, FileEntryRef NewFile);

  /// Returns true if the file contents have been overridden.
  bool isFileOverridden(const FileEntry *File) const {
    if (OverriddenFilesInfo) {
      if (OverriddenFilesInfo->OverriddenFilesWithBuffer.count(File))
        return true;
      if (OverriddenFilesInfo->OverriddenFiles.contains(File))
        return true;
    }
    return false;
  }

  /// Bypass the overridden contents of a file.  This creates a new FileEntry
  /// and initializes the content cache for it.  Returns std::nullopt if there
  /// is no such file in the filesystem.
  ///
  /// This should be called before parsing has begun.
  OptionalFileEntryRef bypassFileContentsOverride(FileEntryRef File);

  /// Specify that a file is transient.
  void setFileIsTransient(FileEntryRef SourceFile);

  /// Specify that all files that are read during this compilation are
  /// transient.
  void setAllFilesAreTransient(bool Transient) {
    FilesAreTransient = Transient;
  }

  //===--------------------------------------------------------------------===//
  // FileID manipulation methods.
  //===--------------------------------------------------------------------===//

  /// Return the buffer for the specified FileID.
  ///
  /// If there is an error opening this buffer the first time, return
  /// std::nullopt.
  std::optional<llvm::MemoryBufferRef>
  getBufferOrNone(FileID FID, SourceLocation Loc = SourceLocation()) const {
    if (auto *Entry = getSLocEntryForFile(FID))
      return Entry->getFile().getContentCache().getBufferOrNone(
          Diag, getFileManager(), Loc);
    return std::nullopt;
  }

  /// Return the buffer for the specified FileID.
  ///
  /// If there is an error opening this buffer the first time, this
  /// manufactures a temporary buffer and returns it.
  llvm::MemoryBufferRef
  getBufferOrFake(FileID FID, SourceLocation Loc = SourceLocation()) const {
    if (auto B = getBufferOrNone(FID, Loc))
      return *B;
    return getFakeBufferForRecovery();
  }

  /// Returns the FileEntry record for the provided FileID.
  const FileEntry *getFileEntryForID(FileID FID) const {
    if (auto FE = getFileEntryRefForID(FID))
      return *FE;
    return nullptr;
  }

  /// Returns the FileEntryRef for the provided FileID.
  OptionalFileEntryRef getFileEntryRefForID(FileID FID) const {
    if (auto *Entry = getSLocEntryForFile(FID))
      return Entry->getFile().getContentCache().OrigEntry;
    return std::nullopt;
  }

  /// Returns the filename for the provided FileID, unless it's a built-in
  /// buffer that's not represented by a filename.
  ///
  /// Returns std::nullopt for non-files and built-in files.
  std::optional<StringRef> getNonBuiltinFilenameForID(FileID FID) const;

  /// Returns the FileEntry record for the provided SLocEntry.
  const FileEntry *
  getFileEntryForSLocEntry(const SrcMgr::SLocEntry &SLocEntry) const {
    if (auto FE = SLocEntry.getFile().getContentCache().OrigEntry)
      return *FE;
    return nullptr;
  }

  /// Return a StringRef to the source buffer data for the
  /// specified FileID.
  ///
  /// \param FID The file ID whose contents will be returned.
  /// \param Invalid If non-NULL, will be set true if an error occurred.
  StringRef getBufferData(FileID FID, bool *Invalid = nullptr) const;

  /// Return a StringRef to the source buffer data for the
  /// specified FileID, returning std::nullopt if invalid.
  ///
  /// \param FID The file ID whose contents will be returned.
  std::optional<StringRef> getBufferDataOrNone(FileID FID) const;

  /// Return a StringRef to the source buffer data for the
  /// specified FileID, returning std::nullopt if it's not yet loaded.
  ///
  /// \param FID The file ID whose contents will be returned.
  std::optional<StringRef> getBufferDataIfLoaded(FileID FID) const;

  /// Get the number of FileIDs (files and macros) that were created
  /// during preprocessing of \p FID, including it.
  unsigned getNumCreatedFIDsForFileID(FileID FID) const {
    if (auto *Entry = getSLocEntryForFile(FID))
      return Entry->getFile().NumCreatedFIDs;
    return 0;
  }

  /// Set the number of FileIDs (files and macros) that were created
  /// during preprocessing of \p FID, including it.
  void setNumCreatedFIDsForFileID(FileID FID, unsigned NumFIDs,
                                  bool Force = false) {
    auto *Entry = getSLocEntryForFile(FID);
    if (!Entry)
      return;
    assert((Force || Entry->getFile().NumCreatedFIDs == 0) && "Already set!");
    Entry->getFile().NumCreatedFIDs = NumFIDs;
  }

  //===--------------------------------------------------------------------===//
  // SourceLocation manipulation methods.
  //===--------------------------------------------------------------------===//

  /// Return the FileID for a SourceLocation.
  ///
  /// This is a very hot method that is used for all SourceManager queries
  /// that start with a SourceLocation object.  It is responsible for finding
  /// the entry in SLocEntryTable which contains the specified location.
  ///
  FileID getFileID(SourceLocation SpellingLoc) const {
    return getFileID(SpellingLoc.getOffset());
  }

  /// Return the filename of the file containing a SourceLocation.
  StringRef getFilename(SourceLocation SpellingLoc) const;

  /// Return the source location corresponding to the first byte of
  /// the specified file.
  SourceLocation getLocForStartOfFile(FileID FID) const {
    if (auto *Entry = getSLocEntryForFile(FID))
      return SourceLocation::getFileLoc(Entry->getOffset());
    return SourceLocation();
  }

  /// Return the source location corresponding to the last byte of the
  /// specified file.
  SourceLocation getLocForEndOfFile(FileID FID) const {
    if (auto *Entry = getSLocEntryForFile(FID))
      return SourceLocation::getFileLoc(Entry->getOffset() +
                                        getFileIDSize(FID));
    return SourceLocation();
  }

  /// Returns the include location if \p FID is a \#include'd file
  /// otherwise it returns an invalid location.
  SourceLocation getIncludeLoc(FileID FID) const {
    if (auto *Entry = getSLocEntryForFile(FID))
      return Entry->getFile().getIncludeLoc();
    return SourceLocation();
  }

  // Returns the import location if the given source location is
  // located within a module, or an invalid location if the source location
  // is within the current translation unit.
  std::pair<SourceLocation, StringRef>
  getModuleImportLoc(SourceLocation Loc) const {
    FileID FID = getFileID(Loc);

    // Positive file IDs are in the current translation unit, and -1 is a
    // placeholder.
    if (FID.ID >= -1)
      return std::make_pair(SourceLocation(), "");

    return ExternalSLocEntries->getModuleImportLoc(FID.ID);
  }

  /// Given a SourceLocation object \p Loc, return the expansion
  /// location referenced by the ID.
  SourceLocation getExpansionLoc(SourceLocation Loc) const {
    // Handle the non-mapped case inline, defer to out of line code to handle
    // expansions.
    if (Loc.isFileID()) return Loc;
    return getExpansionLocSlowCase(Loc);
  }

  /// Given \p Loc, if it is a macro location return the expansion
  /// location or the spelling location, depending on if it comes from a
  /// macro argument or not.
  SourceLocation getFileLoc(SourceLocation Loc) const {
    if (Loc.isFileID()) return Loc;
    return getFileLocSlowCase(Loc);
  }

  /// Return the start/end of the expansion information for an
  /// expansion location.
  ///
  /// \pre \p Loc is required to be an expansion location.
  CharSourceRange getImmediateExpansionRange(SourceLocation Loc) const;

  /// Given a SourceLocation object, return the range of
  /// tokens covered by the expansion in the ultimate file.
  CharSourceRange getExpansionRange(SourceLocation Loc) const;

  /// Given a SourceRange object, return the range of
  /// tokens or characters covered by the expansion in the ultimate file.
  CharSourceRange getExpansionRange(SourceRange Range) const {
    SourceLocation Begin = getExpansionRange(Range.getBegin()).getBegin();
    CharSourceRange End = getExpansionRange(Range.getEnd());
    return CharSourceRange(SourceRange(Begin, End.getEnd()),
                           End.isTokenRange());
  }

  /// Given a CharSourceRange object, return the range of
  /// tokens or characters covered by the expansion in the ultimate file.
  CharSourceRange getExpansionRange(CharSourceRange Range) const {
    CharSourceRange Expansion = getExpansionRange(Range.getAsRange());
    if (Expansion.getEnd() == Range.getEnd())
      Expansion.setTokenRange(Range.isTokenRange());
    return Expansion;
  }

  /// Given a SourceLocation object, return the spelling
  /// location referenced by the ID.
  ///
  /// This is the place where the characters that make up the lexed token
  /// can be found.
  SourceLocation getSpellingLoc(SourceLocation Loc) const {
    // Handle the non-mapped case inline, defer to out of line code to handle
    // expansions.
    if (Loc.isFileID()) return Loc;
    return getSpellingLocSlowCase(Loc);
  }

  /// Given a SourceLocation object, return the spelling location
  /// referenced by the ID.
  ///
  /// This is the first level down towards the place where the characters
  /// that make up the lexed token can be found.  This should not generally
  /// be used by clients.
  SourceLocation getImmediateSpellingLoc(SourceLocation Loc) const;

  /// Form a SourceLocation from a FileID and Offset pair.
  SourceLocation getComposedLoc(FileID FID, unsigned Offset) const {
    auto *Entry = getSLocEntryOrNull(FID);
    if (!Entry)
      return SourceLocation();

    SourceLocation::UIntTy GlobalOffset = Entry->getOffset() + Offset;
    return Entry->isFile() ? SourceLocation::getFileLoc(GlobalOffset)
                           : SourceLocation::getMacroLoc(GlobalOffset);
  }

  /// Decompose the specified location into a raw FileID + Offset pair.
  ///
  /// The first element is the FileID, the second is the offset from the
  /// start of the buffer of the location.
  std::pair<FileID, unsigned> getDecomposedLoc(SourceLocation Loc) const {
    FileID FID = getFileID(Loc);
    auto *Entry = getSLocEntryOrNull(FID);
    if (!Entry)
      return std::make_pair(FileID(), 0);
    return std::make_pair(FID, Loc.getOffset() - Entry->getOffset());
  }

  /// Decompose the specified location into a raw FileID + Offset pair.
  ///
  /// If the location is an expansion record, walk through it until we find
  /// the final location expanded.
  std::pair<FileID, unsigned>
  getDecomposedExpansionLoc(SourceLocation Loc) const {
    FileID FID = getFileID(Loc);
    auto *E = getSLocEntryOrNull(FID);
    if (!E)
      return std::make_pair(FileID(), 0);

    unsigned Offset = Loc.getOffset()-E->getOffset();
    if (Loc.isFileID())
      return std::make_pair(FID, Offset);

    return getDecomposedExpansionLocSlowCase(E);
  }

  /// Decompose the specified location into a raw FileID + Offset pair.
  ///
  /// If the location is an expansion record, walk through it until we find
  /// its spelling record.
  std::pair<FileID, unsigned>
  getDecomposedSpellingLoc(SourceLocation Loc) const {
    FileID FID = getFileID(Loc);
    auto *E = getSLocEntryOrNull(FID);
    if (!E)
      return std::make_pair(FileID(), 0);

    unsigned Offset = Loc.getOffset()-E->getOffset();
    if (Loc.isFileID())
      return std::make_pair(FID, Offset);
    return getDecomposedSpellingLocSlowCase(E, Offset);
  }

  /// Returns the "included/expanded in" decomposed location of the given
  /// FileID.
  std::pair<FileID, unsigned> getDecomposedIncludedLoc(FileID FID) const;

  /// Returns the offset from the start of the file that the
  /// specified SourceLocation represents.
  ///
  /// This is not very meaningful for a macro ID.
  unsigned getFileOffset(SourceLocation SpellingLoc) const {
    return getDecomposedLoc(SpellingLoc).second;
  }

  /// Tests whether the given source location represents a macro
  /// argument's expansion into the function-like macro definition.
  ///
  /// \param StartLoc If non-null and function returns true, it is set to the
  /// start location of the macro argument expansion.
  ///
  /// Such source locations only appear inside of the expansion
  /// locations representing where a particular function-like macro was
  /// expanded.
  bool isMacroArgExpansion(SourceLocation Loc,
                           SourceLocation *StartLoc = nullptr) const;

  /// Tests whether the given source location represents the expansion of
  /// a macro body.
  ///
  /// This is equivalent to testing whether the location is part of a macro
  /// expansion but not the expansion of an argument to a function-like macro.
  bool isMacroBodyExpansion(SourceLocation Loc) const;

  /// Returns true if the given MacroID location points at the beginning
  /// of the immediate macro expansion.
  ///
  /// \param MacroBegin If non-null and function returns true, it is set to the
  /// begin location of the immediate macro expansion.
  bool isAtStartOfImmediateMacroExpansion(SourceLocation Loc,
                                    SourceLocation *MacroBegin = nullptr) const;

  /// Returns true if the given MacroID location points at the character
  /// end of the immediate macro expansion.
  ///
  /// \param MacroEnd If non-null and function returns true, it is set to the
  /// character end location of the immediate macro expansion.
  bool
  isAtEndOfImmediateMacroExpansion(SourceLocation Loc,
                                   SourceLocation *MacroEnd = nullptr) const;

  /// Returns true if \p Loc is inside the [\p Start, +\p Length)
  /// chunk of the source location address space.
  ///
  /// If it's true and \p RelativeOffset is non-null, it will be set to the
  /// relative offset of \p Loc inside the chunk.
  bool
  isInSLocAddrSpace(SourceLocation Loc, SourceLocation Start, unsigned Length,
                    SourceLocation::UIntTy *RelativeOffset = nullptr) const {
    assert(((Start.getOffset() < NextLocalOffset &&
               Start.getOffset()+Length <= NextLocalOffset) ||
            (Start.getOffset() >= CurrentLoadedOffset &&
                Start.getOffset()+Length < MaxLoadedOffset)) &&
           "Chunk is not valid SLoc address space");
    SourceLocation::UIntTy LocOffs = Loc.getOffset();
    SourceLocation::UIntTy BeginOffs = Start.getOffset();
    SourceLocation::UIntTy EndOffs = BeginOffs + Length;
    if (LocOffs >= BeginOffs && LocOffs < EndOffs) {
      if (RelativeOffset)
        *RelativeOffset = LocOffs - BeginOffs;
      return true;
    }

    return false;
  }

  /// Return true if both \p LHS and \p RHS are in the local source
  /// location address space or the loaded one.
  ///
  /// If it's true and \p RelativeOffset is non-null, it will be set to the
  /// offset of \p RHS relative to \p LHS.
  bool isInSameSLocAddrSpace(SourceLocation LHS, SourceLocation RHS,
                             SourceLocation::IntTy *RelativeOffset) const {
    SourceLocation::UIntTy LHSOffs = LHS.getOffset(), RHSOffs = RHS.getOffset();
    bool LHSLoaded = LHSOffs >= CurrentLoadedOffset;
    bool RHSLoaded = RHSOffs >= CurrentLoadedOffset;

    if (LHSLoaded == RHSLoaded) {
      if (RelativeOffset)
        *RelativeOffset = RHSOffs - LHSOffs;
      return true;
    }

    return false;
  }

  //===--------------------------------------------------------------------===//
  // Queries about the code at a SourceLocation.
  //===--------------------------------------------------------------------===//

  /// Return a pointer to the start of the specified location
  /// in the appropriate spelling MemoryBuffer.
  ///
  /// \param Invalid If non-NULL, will be set \c true if an error occurs.
  const char *getCharacterData(SourceLocation SL,
                               bool *Invalid = nullptr) const;

  /// Return the column # for the specified file position.
  ///
  /// This is significantly cheaper to compute than the line number.  This
  /// returns zero if the column number isn't known.  This may only be called
  /// on a file sloc, so you must choose a spelling or expansion location
  /// before calling this method.
  unsigned getColumnNumber(FileID FID, unsigned FilePos,
                           bool *Invalid = nullptr) const;
  unsigned getSpellingColumnNumber(SourceLocation Loc,
                                   bool *Invalid = nullptr) const;
  unsigned getExpansionColumnNumber(SourceLocation Loc,
                                    bool *Invalid = nullptr) const;
  unsigned getPresumedColumnNumber(SourceLocation Loc,
                                   bool *Invalid = nullptr) const;

  /// Given a SourceLocation, return the spelling line number
  /// for the position indicated.
  ///
  /// This requires building and caching a table of line offsets for the
  /// MemoryBuffer, so this is not cheap: use only when about to emit a
  /// diagnostic.
  unsigned getLineNumber(FileID FID, unsigned FilePos, bool *Invalid = nullptr) const;
  unsigned getSpellingLineNumber(SourceLocation Loc, bool *Invalid = nullptr) const;
  unsigned getExpansionLineNumber(SourceLocation Loc, bool *Invalid = nullptr) const;
  unsigned getPresumedLineNumber(SourceLocation Loc, bool *Invalid = nullptr) const;

  /// Return the filename or buffer identifier of the buffer the
  /// location is in.
  ///
  /// Note that this name does not respect \#line directives.  Use
  /// getPresumedLoc for normal clients.
  StringRef getBufferName(SourceLocation Loc, bool *Invalid = nullptr) const;

  /// Return the file characteristic of the specified source
  /// location, indicating whether this is a normal file, a system
  /// header, or an "implicit extern C" system header.
  ///
  /// This state can be modified with flags on GNU linemarker directives like:
  /// \code
  ///   # 4 "foo.h" 3
  /// \endcode
  /// which changes all source locations in the current file after that to be
  /// considered to be from a system header.
  SrcMgr::CharacteristicKind getFileCharacteristic(SourceLocation Loc) const;

  /// Returns the "presumed" location of a SourceLocation specifies.
  ///
  /// A "presumed location" can be modified by \#line or GNU line marker
  /// directives.  This provides a view on the data that a user should see
  /// in diagnostics, for example.
  ///
  /// Note that a presumed location is always given as the expansion point of
  /// an expansion location, not at the spelling location.
  ///
  /// \returns The presumed location of the specified SourceLocation. If the
  /// presumed location cannot be calculated (e.g., because \p Loc is invalid
  /// or the file containing \p Loc has changed on disk), returns an invalid
  /// presumed location.
  PresumedLoc getPresumedLoc(SourceLocation Loc,
                             bool UseLineDirectives = true) const;

  /// Returns whether the PresumedLoc for a given SourceLocation is
  /// in the main file.
  ///
  /// This computes the "presumed" location for a SourceLocation, then checks
  /// whether it came from a file other than the main file. This is different
  /// from isWrittenInMainFile() because it takes line marker directives into
  /// account.
  bool isInMainFile(SourceLocation Loc) const;

  /// Returns true if the spelling locations for both SourceLocations
  /// are part of the same file buffer.
  ///
  /// This check ignores line marker directives.
  bool isWrittenInSameFile(SourceLocation Loc1, SourceLocation Loc2) const {
    return getFileID(Loc1) == getFileID(Loc2);
  }

  /// Returns true if the spelling location for the given location
  /// is in the main file buffer.
  ///
  /// This check ignores line marker directives.
  bool isWrittenInMainFile(SourceLocation Loc) const {
    return getFileID(Loc) == getMainFileID();
  }

  /// Returns whether \p Loc is located in a <built-in> file.
  bool isWrittenInBuiltinFile(SourceLocation Loc) const {
    PresumedLoc Presumed = getPresumedLoc(Loc);
    if (Presumed.isInvalid())
      return false;
    StringRef Filename(Presumed.getFilename());
    return Filename == "<built-in>";
  }

  /// Returns whether \p Loc is located in a <command line> file.
  bool isWrittenInCommandLineFile(SourceLocation Loc) const {
    PresumedLoc Presumed = getPresumedLoc(Loc);
    if (Presumed.isInvalid())
      return false;
    StringRef Filename(Presumed.getFilename());
    return Filename == "<command line>";
  }

  /// Returns whether \p Loc is located in a <scratch space> file.
  bool isWrittenInScratchSpace(SourceLocation Loc) const {
    PresumedLoc Presumed = getPresumedLoc(Loc);
    if (Presumed.isInvalid())
      return false;
    StringRef Filename(Presumed.getFilename());
    return Filename == "<scratch space>";
  }

  /// Returns if a SourceLocation is in a system header.
  bool isInSystemHeader(SourceLocation Loc) const {
    if (Loc.isInvalid())
      return false;
    return isSystem(getFileCharacteristic(Loc));
  }

  /// Returns if a SourceLocation is in an "extern C" system header.
  bool isInExternCSystemHeader(SourceLocation Loc) const {
    return getFileCharacteristic(Loc) == SrcMgr::C_ExternCSystem;
  }

  /// Returns whether \p Loc is expanded from a macro in a system header.
  bool isInSystemMacro(SourceLocation loc) const {
    if (!loc.isMacroID())
      return false;

    // This happens when the macro is the result of a paste, in that case
    // its spelling is the scratch memory, so we take the parent context.
    // There can be several level of token pasting.
    if (isWrittenInScratchSpace(getSpellingLoc(loc))) {
      do {
        loc = getImmediateMacroCallerLoc(loc);
      } while (isWrittenInScratchSpace(getSpellingLoc(loc)));
      return isInSystemMacro(loc);
    }

    return isInSystemHeader(getSpellingLoc(loc));
  }

  /// The size of the SLocEntry that \p FID represents.
  unsigned getFileIDSize(FileID FID) const;

  /// Given a specific FileID, returns true if \p Loc is inside that
  /// FileID chunk and sets relative offset (offset of \p Loc from beginning
  /// of FileID) to \p relativeOffset.
  bool isInFileID(SourceLocation Loc, FileID FID,
                  unsigned *RelativeOffset = nullptr) const {
    SourceLocation::UIntTy Offs = Loc.getOffset();
    if (isOffsetInFileID(FID, Offs)) {
      if (RelativeOffset)
        *RelativeOffset = Offs - getSLocEntry(FID).getOffset();
      return true;
    }

    return false;
  }

  //===--------------------------------------------------------------------===//
  // Line Table Manipulation Routines
  //===--------------------------------------------------------------------===//

  /// Return the uniqued ID for the specified filename.
  unsigned getLineTableFilenameID(StringRef Str);

  /// Add a line note to the line table for the FileID and offset
  /// specified by Loc.
  ///
  /// If FilenameID is -1, it is considered to be unspecified.
  void AddLineNote(SourceLocation Loc, unsigned LineNo, int FilenameID,
                   bool IsFileEntry, bool IsFileExit,
                   SrcMgr::CharacteristicKind FileKind);

  /// Determine if the source manager has a line table.
  bool hasLineTable() const { return LineTable != nullptr; }

  /// Retrieve the stored line table.
  LineTableInfo &getLineTable();

  //===--------------------------------------------------------------------===//
  // Queries for performance analysis.
  //===--------------------------------------------------------------------===//

  /// Return the total amount of physical memory allocated by the
  /// ContentCache allocator.
  size_t getContentCacheSize() const {
    return ContentCacheAlloc.getTotalMemory();
  }

  struct MemoryBufferSizes {
    const size_t malloc_bytes;
    const size_t mmap_bytes;

    MemoryBufferSizes(size_t malloc_bytes, size_t mmap_bytes)
      : malloc_bytes(malloc_bytes), mmap_bytes(mmap_bytes) {}
  };

  /// Return the amount of memory used by memory buffers, breaking down
  /// by heap-backed versus mmap'ed memory.
  MemoryBufferSizes getMemoryBufferSizes() const;

  /// Return the amount of memory used for various side tables and
  /// data structures in the SourceManager.
  size_t getDataStructureSizes() const;

  //===--------------------------------------------------------------------===//
  // Other miscellaneous methods.
  //===--------------------------------------------------------------------===//

  /// Get the source location for the given file:line:col triplet.
  ///
  /// If the source file is included multiple times, the source location will
  /// be based upon the first inclusion.
  SourceLocation translateFileLineCol(const FileEntry *SourceFile,
                                      unsigned Line, unsigned Col) const;

  /// Get the FileID for the given file.
  ///
  /// If the source file is included multiple times, the FileID will be the
  /// first inclusion.
  FileID translateFile(const FileEntry *SourceFile) const;
  FileID translateFile(FileEntryRef SourceFile) const {
    return translateFile(&SourceFile.getFileEntry());
  }

  /// Get the source location in \p FID for the given line:col.
  /// Returns null location if \p FID is not a file SLocEntry.
  SourceLocation translateLineCol(FileID FID,
                                  unsigned Line, unsigned Col) const;

  /// If \p Loc points inside a function macro argument, the returned
  /// location will be the macro location in which the argument was expanded.
  /// If a macro argument is used multiple times, the expanded location will
  /// be at the first expansion of the argument.
  /// e.g.
  ///   MY_MACRO(foo);
  ///             ^
  /// Passing a file location pointing at 'foo', will yield a macro location
  /// where 'foo' was expanded into.
  SourceLocation getMacroArgExpandedLocation(SourceLocation Loc) const;

  /// Determines the order of 2 source locations in the translation unit.
  ///
  /// \returns true if LHS source location comes before RHS, false otherwise.
  bool isBeforeInTranslationUnit(SourceLocation LHS, SourceLocation RHS) const;

  /// Determines whether the two decomposed source location is in the
  ///        same translation unit. As a byproduct, it also calculates the order
  ///        of the source locations in case they are in the same TU.
  ///
  /// \returns Pair of bools the first component is true if the two locations
  ///          are in the same TU. The second bool is true if the first is true
  ///          and \p LOffs is before \p ROffs.
  std::pair<bool, bool>
  isInTheSameTranslationUnit(std::pair<FileID, unsigned> &LOffs,
                             std::pair<FileID, unsigned> &ROffs) const;

  /// \param Loc a source location in a loaded AST (of a PCH/Module file).
  /// \returns a FileID uniquely identifies the AST of a loaded
  /// module/PCH where `Loc` is at.
  FileID getUniqueLoadedASTFileID(SourceLocation Loc) const;

  /// Determines whether the two decomposed source location is in the same TU.
  bool isInTheSameTranslationUnitImpl(
      const std::pair<FileID, unsigned> &LOffs,
      const std::pair<FileID, unsigned> &ROffs) const;

  /// Determines the order of 2 source locations in the "source location
  /// address space".
  bool isBeforeInSLocAddrSpace(SourceLocation LHS, SourceLocation RHS) const {
    return isBeforeInSLocAddrSpace(LHS, RHS.getOffset());
  }

  /// Determines the order of a source location and a source location
  /// offset in the "source location address space".
  ///
  /// Note that we always consider source locations loaded from
  bool isBeforeInSLocAddrSpace(SourceLocation LHS,
                               SourceLocation::UIntTy RHS) const {
    SourceLocation::UIntTy LHSOffset = LHS.getOffset();
    bool LHSLoaded = LHSOffset >= CurrentLoadedOffset;
    bool RHSLoaded = RHS >= CurrentLoadedOffset;
    if (LHSLoaded == RHSLoaded)
      return LHSOffset < RHS;

    return LHSLoaded;
  }

  /// Return true if the Point is within Start and End.
  bool isPointWithin(SourceLocation Location, SourceLocation Start,
                     SourceLocation End) const {
    return Location == Start || Location == End ||
           (isBeforeInTranslationUnit(Start, Location) &&
            isBeforeInTranslationUnit(Location, End));
  }

  // Iterators over FileInfos.
  using fileinfo_iterator =
      llvm::DenseMap<FileEntryRef, SrcMgr::ContentCache *>::const_iterator;

  fileinfo_iterator fileinfo_begin() const { return FileInfos.begin(); }
  fileinfo_iterator fileinfo_end() const { return FileInfos.end(); }
  bool hasFileInfo(const FileEntry *File) const {
    return FileInfos.find_as(File) != FileInfos.end();
  }

  /// Print statistics to stderr.
  void PrintStats() const;

  void dump() const;

  // Produce notes describing the current source location address space usage.
  void noteSLocAddressSpaceUsage(DiagnosticsEngine &Diag,
                                 std::optional<unsigned> MaxNotes = 32) const;

  /// Get the number of local SLocEntries we have.
  unsigned local_sloc_entry_size() const { return LocalSLocEntryTable.size(); }

  /// Get a local SLocEntry. This is exposed for indexing.
  const SrcMgr::SLocEntry &getLocalSLocEntry(unsigned Index) const {
    return const_cast<SourceManager *>(this)->getLocalSLocEntry(Index);
  }

  /// Get a local SLocEntry. This is exposed for indexing.
  SrcMgr::SLocEntry &getLocalSLocEntry(unsigned Index) {
    assert(Index < LocalSLocEntryTable.size() && "Invalid index");
    return LocalSLocEntryTable[Index];
  }

  /// Get the number of loaded SLocEntries we have.
  unsigned loaded_sloc_entry_size() const { return LoadedSLocEntryTable.size();}

  /// Get a loaded SLocEntry. This is exposed for indexing.
  const SrcMgr::SLocEntry &getLoadedSLocEntry(unsigned Index,
                                              bool *Invalid = nullptr) const {
    return const_cast<SourceManager *>(this)->getLoadedSLocEntry(Index,
                                                                 Invalid);
  }

  /// Get a loaded SLocEntry. This is exposed for indexing.
  SrcMgr::SLocEntry &getLoadedSLocEntry(unsigned Index,
                                        bool *Invalid = nullptr) {
    assert(Index < LoadedSLocEntryTable.size() && "Invalid index");
    if (SLocEntryLoaded[Index])
      return LoadedSLocEntryTable[Index];
    return loadSLocEntry(Index, Invalid);
  }

  const SrcMgr::SLocEntry &getSLocEntry(FileID FID,
                                        bool *Invalid = nullptr) const {
    return const_cast<SourceManager *>(this)->getSLocEntry(FID, Invalid);
  }

  SrcMgr::SLocEntry &getSLocEntry(FileID FID, bool *Invalid = nullptr) {
    if (FID.ID == 0 || FID.ID == -1) {
      if (Invalid) *Invalid = true;
      return LocalSLocEntryTable[0];
    }
    return getSLocEntryByID(FID.ID, Invalid);
  }

  SourceLocation::UIntTy getNextLocalOffset() const { return NextLocalOffset; }

  void setExternalSLocEntrySource(ExternalSLocEntrySource *Source) {
    assert(LoadedSLocEntryTable.empty() &&
           "Invalidating existing loaded entries");
    ExternalSLocEntries = Source;
  }

  /// Allocate a number of loaded SLocEntries, which will be actually
  /// loaded on demand from the external source.
  ///
  /// NumSLocEntries will be allocated, which occupy a total of TotalSize space
  /// in the global source view. The lowest ID and the base offset of the
  /// entries will be returned.
  std::pair<int, SourceLocation::UIntTy>
  AllocateLoadedSLocEntries(unsigned NumSLocEntries,
                            SourceLocation::UIntTy TotalSize);

  /// Returns true if \p Loc came from a PCH/Module.
  bool isLoadedSourceLocation(SourceLocation Loc) const {
    return isLoadedOffset(Loc.getOffset());
  }

  /// Returns true if \p Loc did not come from a PCH/Module.
  bool isLocalSourceLocation(SourceLocation Loc) const {
    return isLocalOffset(Loc.getOffset());
  }

  /// Returns true if \p FID came from a PCH/Module.
  bool isLoadedFileID(FileID FID) const {
    assert(FID.ID != -1 && "Using FileID sentinel value");
    return FID.ID < 0;
  }

  /// Returns true if \p FID did not come from a PCH/Module.
  bool isLocalFileID(FileID FID) const {
    return !isLoadedFileID(FID);
  }

  /// Gets the location of the immediate macro caller, one level up the stack
  /// toward the initial macro typed into the source.
  SourceLocation getImmediateMacroCallerLoc(SourceLocation Loc) const {
    if (!Loc.isMacroID()) return Loc;

    // When we have the location of (part of) an expanded parameter, its
    // spelling location points to the argument as expanded in the macro call,
    // and therefore is used to locate the macro caller.
    if (isMacroArgExpansion(Loc))
      return getImmediateSpellingLoc(Loc);

    // Otherwise, the caller of the macro is located where this macro is
    // expanded (while the spelling is part of the macro definition).
    return getImmediateExpansionRange(Loc).getBegin();
  }

  /// \return Location of the top-level macro caller.
  SourceLocation getTopMacroCallerLoc(SourceLocation Loc) const;

private:
  friend class ASTReader;
  friend class ASTWriter;

  llvm::MemoryBufferRef getFakeBufferForRecovery() const;
  SrcMgr::ContentCache &getFakeContentCacheForRecovery() const;

  const SrcMgr::SLocEntry &loadSLocEntry(unsigned Index, bool *Invalid) const;
  SrcMgr::SLocEntry &loadSLocEntry(unsigned Index, bool *Invalid);

  const SrcMgr::SLocEntry *getSLocEntryOrNull(FileID FID) const {
    return const_cast<SourceManager *>(this)->getSLocEntryOrNull(FID);
  }

  SrcMgr::SLocEntry *getSLocEntryOrNull(FileID FID) {
    bool Invalid = false;
    SrcMgr::SLocEntry &Entry = getSLocEntry(FID, &Invalid);
    return Invalid ? nullptr : &Entry;
  }

  const SrcMgr::SLocEntry *getSLocEntryForFile(FileID FID) const {
    return const_cast<SourceManager *>(this)->getSLocEntryForFile(FID);
  }

  SrcMgr::SLocEntry *getSLocEntryForFile(FileID FID) {
    if (auto *Entry = getSLocEntryOrNull(FID))
      if (Entry->isFile())
        return Entry;
    return nullptr;
  }

  /// Get the entry with the given unwrapped FileID.
  /// Invalid will not be modified for Local IDs.
  const SrcMgr::SLocEntry &getSLocEntryByID(int ID,
                                            bool *Invalid = nullptr) const {
    return const_cast<SourceManager *>(this)->getSLocEntryByID(ID, Invalid);
  }

  SrcMgr::SLocEntry &getSLocEntryByID(int ID, bool *Invalid = nullptr) {
    assert(ID != -1 && "Using FileID sentinel value");
    if (ID < 0)
      return getLoadedSLocEntryByID(ID, Invalid);
    return getLocalSLocEntry(static_cast<unsigned>(ID));
  }

  const SrcMgr::SLocEntry &
  getLoadedSLocEntryByID(int ID, bool *Invalid = nullptr) const {
    return const_cast<SourceManager *>(this)->getLoadedSLocEntryByID(ID,
                                                                     Invalid);
  }

  SrcMgr::SLocEntry &getLoadedSLocEntryByID(int ID, bool *Invalid = nullptr) {
    return getLoadedSLocEntry(static_cast<unsigned>(-ID - 2), Invalid);
  }

  FileID getFileID(SourceLocation::UIntTy SLocOffset) const {
    // If our one-entry cache covers this offset, just return it.
    if (isOffsetInFileID(LastFileIDLookup, SLocOffset))
      return LastFileIDLookup;

    return getFileIDSlow(SLocOffset);
  }

  bool isLocalOffset(SourceLocation::UIntTy SLocOffset) const {
    return SLocOffset < CurrentLoadedOffset;
  }

  bool isLoadedOffset(SourceLocation::UIntTy SLocOffset) const {
    return SLocOffset >= CurrentLoadedOffset;
  }

  /// Implements the common elements of storing an expansion info struct into
  /// the SLocEntry table and producing a source location that refers to it.
  SourceLocation
  createExpansionLocImpl(const SrcMgr::ExpansionInfo &Expansion,
                         unsigned Length, int LoadedID = 0,
                         SourceLocation::UIntTy LoadedOffset = 0);

  /// Return true if the specified FileID contains the
  /// specified SourceLocation offset.  This is a very hot method.
  inline bool isOffsetInFileID(FileID FID,
                               SourceLocation::UIntTy SLocOffset) const {
    const SrcMgr::SLocEntry &Entry = getSLocEntry(FID);
    // If the entry is after the offset, it can't contain it.
    if (SLocOffset < Entry.getOffset()) return false;

    // If this is the very last entry then it does.
    if (FID.ID == -2)
      return true;

    // If it is the last local entry, then it does if the location is local.
    if (FID.ID+1 == static_cast<int>(LocalSLocEntryTable.size()))
      return SLocOffset < NextLocalOffset;

    // Otherwise, the entry after it has to not include it. This works for both
    // local and loaded entries.
    return SLocOffset < getSLocEntryByID(FID.ID+1).getOffset();
  }

  /// Returns the previous in-order FileID or an invalid FileID if there
  /// is no previous one.
  FileID getPreviousFileID(FileID FID) const;

  /// Returns the next in-order FileID or an invalid FileID if there is
  /// no next one.
  FileID getNextFileID(FileID FID) const;

  /// Create a new fileID for the specified ContentCache and
  /// include position.
  ///
  /// This works regardless of whether the ContentCache corresponds to a
  /// file or some other input source.
  FileID createFileIDImpl(SrcMgr::ContentCache &File, StringRef Filename,
                          SourceLocation IncludePos,
                          SrcMgr::CharacteristicKind DirCharacter, int LoadedID,
                          SourceLocation::UIntTy LoadedOffset);

  SrcMgr::ContentCache &getOrCreateContentCache(FileEntryRef SourceFile,
                                                bool isSystemFile = false);

  /// Create a new ContentCache for the specified  memory buffer.
  SrcMgr::ContentCache &
  createMemBufferContentCache(std::unique_ptr<llvm::MemoryBuffer> Buf);

  FileID getFileIDSlow(SourceLocation::UIntTy SLocOffset) const;
  FileID getFileIDLocal(SourceLocation::UIntTy SLocOffset) const;
  FileID getFileIDLoaded(SourceLocation::UIntTy SLocOffset) const;

  SourceLocation getExpansionLocSlowCase(SourceLocation Loc) const;
  SourceLocation getSpellingLocSlowCase(SourceLocation Loc) const;
  SourceLocation getFileLocSlowCase(SourceLocation Loc) const;

  std::pair<FileID, unsigned>
  getDecomposedExpansionLocSlowCase(const SrcMgr::SLocEntry *E) const;
  std::pair<FileID, unsigned>
  getDecomposedSpellingLocSlowCase(const SrcMgr::SLocEntry *E,
                                   unsigned Offset) const;
  void computeMacroArgsCache(MacroArgsMap &MacroArgsCache, FileID FID) const;
  void associateFileChunkWithMacroArgExp(MacroArgsMap &MacroArgsCache,
                                         FileID FID,
                                         SourceLocation SpellLoc,
                                         SourceLocation ExpansionLoc,
                                         unsigned ExpansionLength) const;
  void updateSlocUsageStats() const;
};

/// Comparison function object.
template<typename T>
class BeforeThanCompare;

/// Compare two source locations.
template<>
class BeforeThanCompare<SourceLocation> {
  SourceManager &SM;

public:
  explicit BeforeThanCompare(SourceManager &SM) : SM(SM) {}

  bool operator()(SourceLocation LHS, SourceLocation RHS) const {
    return SM.isBeforeInTranslationUnit(LHS, RHS);
  }
};

/// Compare two non-overlapping source ranges.
template<>
class BeforeThanCompare<SourceRange> {
  SourceManager &SM;

public:
  explicit BeforeThanCompare(SourceManager &SM) : SM(SM) {}

  bool operator()(SourceRange LHS, SourceRange RHS) const {
    return SM.isBeforeInTranslationUnit(LHS.getBegin(), RHS.getBegin());
  }
};

/// SourceManager and necessary dependencies (e.g. VFS, FileManager) for a
/// single in-memorty file.
class SourceManagerForFile {
public:
  /// Creates SourceManager and necessary dependencies (e.g. VFS, FileManager).
  /// The main file in the SourceManager will be \p FileName with \p Content.
  SourceManagerForFile(StringRef FileName, StringRef Content);

  SourceManager &get() {
    assert(SourceMgr);
    return *SourceMgr;
  }

private:
  // The order of these fields are important - they should be in the same order
  // as they are created in `createSourceManagerForFile` so that they can be
  // deleted in the reverse order as they are created.
  std::unique_ptr<FileManager> FileMgr;
  std::unique_ptr<DiagnosticsEngine> Diagnostics;
  std::unique_ptr<SourceManager> SourceMgr;
};

} // namespace clang

#endif // LLVM_CLANG_BASIC_SOURCEMANAGER_H

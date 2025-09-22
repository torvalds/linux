//===- SourceManager.cpp - Track and cache source files -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements the SourceManager interface.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManagerInternals.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Capacity.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

using namespace clang;
using namespace SrcMgr;
using llvm::MemoryBuffer;

#define DEBUG_TYPE "source-manager"

// Reaching a limit of 2^31 results in a hard error. This metric allows to track
// if particular invocation of the compiler is close to it.
STATISTIC(MaxUsedSLocBytes, "Maximum number of bytes used by source locations "
                            "(both loaded and local).");

//===----------------------------------------------------------------------===//
// SourceManager Helper Classes
//===----------------------------------------------------------------------===//

/// getSizeBytesMapped - Returns the number of bytes actually mapped for this
/// ContentCache. This can be 0 if the MemBuffer was not actually expanded.
unsigned ContentCache::getSizeBytesMapped() const {
  return Buffer ? Buffer->getBufferSize() : 0;
}

/// Returns the kind of memory used to back the memory buffer for
/// this content cache.  This is used for performance analysis.
llvm::MemoryBuffer::BufferKind ContentCache::getMemoryBufferKind() const {
  if (Buffer == nullptr) {
    assert(0 && "Buffer should never be null");
    return llvm::MemoryBuffer::MemoryBuffer_Malloc;
  }
  return Buffer->getBufferKind();
}

/// getSize - Returns the size of the content encapsulated by this ContentCache.
///  This can be the size of the source file or the size of an arbitrary
///  scratch buffer.  If the ContentCache encapsulates a source file, that
///  file is not lazily brought in from disk to satisfy this query.
unsigned ContentCache::getSize() const {
  return Buffer ? (unsigned)Buffer->getBufferSize()
                : (unsigned)ContentsEntry->getSize();
}

const char *ContentCache::getInvalidBOM(StringRef BufStr) {
  // If the buffer is valid, check to see if it has a UTF Byte Order Mark
  // (BOM).  We only support UTF-8 with and without a BOM right now.  See
  // http://en.wikipedia.org/wiki/Byte_order_mark for more information.
  const char *InvalidBOM =
      llvm::StringSwitch<const char *>(BufStr)
          .StartsWith(llvm::StringLiteral::withInnerNUL("\x00\x00\xFE\xFF"),
                      "UTF-32 (BE)")
          .StartsWith(llvm::StringLiteral::withInnerNUL("\xFF\xFE\x00\x00"),
                      "UTF-32 (LE)")
          .StartsWith("\xFE\xFF", "UTF-16 (BE)")
          .StartsWith("\xFF\xFE", "UTF-16 (LE)")
          .StartsWith("\x2B\x2F\x76", "UTF-7")
          .StartsWith("\xF7\x64\x4C", "UTF-1")
          .StartsWith("\xDD\x73\x66\x73", "UTF-EBCDIC")
          .StartsWith("\x0E\xFE\xFF", "SCSU")
          .StartsWith("\xFB\xEE\x28", "BOCU-1")
          .StartsWith("\x84\x31\x95\x33", "GB-18030")
          .Default(nullptr);

  return InvalidBOM;
}

std::optional<llvm::MemoryBufferRef>
ContentCache::getBufferOrNone(DiagnosticsEngine &Diag, FileManager &FM,
                              SourceLocation Loc) const {
  // Lazily create the Buffer for ContentCaches that wrap files.  If we already
  // computed it, just return what we have.
  if (IsBufferInvalid)
    return std::nullopt;
  if (Buffer)
    return Buffer->getMemBufferRef();
  if (!ContentsEntry)
    return std::nullopt;

  // Start with the assumption that the buffer is invalid to simplify early
  // return paths.
  IsBufferInvalid = true;

  auto BufferOrError = FM.getBufferForFile(*ContentsEntry, IsFileVolatile);

  // If we were unable to open the file, then we are in an inconsistent
  // situation where the content cache referenced a file which no longer
  // exists. Most likely, we were using a stat cache with an invalid entry but
  // the file could also have been removed during processing. Since we can't
  // really deal with this situation, just create an empty buffer.
  if (!BufferOrError) {
    if (Diag.isDiagnosticInFlight())
      Diag.SetDelayedDiagnostic(diag::err_cannot_open_file,
                                ContentsEntry->getName(),
                                BufferOrError.getError().message());
    else
      Diag.Report(Loc, diag::err_cannot_open_file)
          << ContentsEntry->getName() << BufferOrError.getError().message();

    return std::nullopt;
  }

  Buffer = std::move(*BufferOrError);

  // Check that the file's size fits in an 'unsigned' (with room for a
  // past-the-end value). This is deeply regrettable, but various parts of
  // Clang (including elsewhere in this file!) use 'unsigned' to represent file
  // offsets, line numbers, string literal lengths, and so on, and fail
  // miserably on large source files.
  //
  // Note: ContentsEntry could be a named pipe, in which case
  // ContentsEntry::getSize() could have the wrong size. Use
  // MemoryBuffer::getBufferSize() instead.
  if (Buffer->getBufferSize() >= std::numeric_limits<unsigned>::max()) {
    if (Diag.isDiagnosticInFlight())
      Diag.SetDelayedDiagnostic(diag::err_file_too_large,
                                ContentsEntry->getName());
    else
      Diag.Report(Loc, diag::err_file_too_large)
        << ContentsEntry->getName();

    return std::nullopt;
  }

  // Unless this is a named pipe (in which case we can handle a mismatch),
  // check that the file's size is the same as in the file entry (which may
  // have come from a stat cache).
  if (!ContentsEntry->isNamedPipe() &&
      Buffer->getBufferSize() != (size_t)ContentsEntry->getSize()) {
    if (Diag.isDiagnosticInFlight())
      Diag.SetDelayedDiagnostic(diag::err_file_modified,
                                ContentsEntry->getName());
    else
      Diag.Report(Loc, diag::err_file_modified)
        << ContentsEntry->getName();

    return std::nullopt;
  }

  // If the buffer is valid, check to see if it has a UTF Byte Order Mark
  // (BOM).  We only support UTF-8 with and without a BOM right now.  See
  // http://en.wikipedia.org/wiki/Byte_order_mark for more information.
  StringRef BufStr = Buffer->getBuffer();
  const char *InvalidBOM = getInvalidBOM(BufStr);

  if (InvalidBOM) {
    Diag.Report(Loc, diag::err_unsupported_bom)
      << InvalidBOM << ContentsEntry->getName();
    return std::nullopt;
  }

  // Buffer has been validated.
  IsBufferInvalid = false;
  return Buffer->getMemBufferRef();
}

unsigned LineTableInfo::getLineTableFilenameID(StringRef Name) {
  auto IterBool = FilenameIDs.try_emplace(Name, FilenamesByID.size());
  if (IterBool.second)
    FilenamesByID.push_back(&*IterBool.first);
  return IterBool.first->second;
}

/// Add a line note to the line table that indicates that there is a \#line or
/// GNU line marker at the specified FID/Offset location which changes the
/// presumed location to LineNo/FilenameID. If EntryExit is 0, then this doesn't
/// change the presumed \#include stack.  If it is 1, this is a file entry, if
/// it is 2 then this is a file exit. FileKind specifies whether this is a
/// system header or extern C system header.
void LineTableInfo::AddLineNote(FileID FID, unsigned Offset, unsigned LineNo,
                                int FilenameID, unsigned EntryExit,
                                SrcMgr::CharacteristicKind FileKind) {
  std::vector<LineEntry> &Entries = LineEntries[FID];

  assert((Entries.empty() || Entries.back().FileOffset < Offset) &&
         "Adding line entries out of order!");

  unsigned IncludeOffset = 0;
  if (EntryExit == 1) {
    // Push #include
    IncludeOffset = Offset-1;
  } else {
    const auto *PrevEntry = Entries.empty() ? nullptr : &Entries.back();
    if (EntryExit == 2) {
      // Pop #include
      assert(PrevEntry && PrevEntry->IncludeOffset &&
             "PPDirectives should have caught case when popping empty include "
             "stack");
      PrevEntry = FindNearestLineEntry(FID, PrevEntry->IncludeOffset);
    }
    if (PrevEntry) {
      IncludeOffset = PrevEntry->IncludeOffset;
      if (FilenameID == -1) {
        // An unspecified FilenameID means use the previous (or containing)
        // filename if available, or the main source file otherwise.
        FilenameID = PrevEntry->FilenameID;
      }
    }
  }

  Entries.push_back(LineEntry::get(Offset, LineNo, FilenameID, FileKind,
                                   IncludeOffset));
}

/// FindNearestLineEntry - Find the line entry nearest to FID that is before
/// it.  If there is no line entry before Offset in FID, return null.
const LineEntry *LineTableInfo::FindNearestLineEntry(FileID FID,
                                                     unsigned Offset) {
  const std::vector<LineEntry> &Entries = LineEntries[FID];
  assert(!Entries.empty() && "No #line entries for this FID after all!");

  // It is very common for the query to be after the last #line, check this
  // first.
  if (Entries.back().FileOffset <= Offset)
    return &Entries.back();

  // Do a binary search to find the maximal element that is still before Offset.
  std::vector<LineEntry>::const_iterator I = llvm::upper_bound(Entries, Offset);
  if (I == Entries.begin())
    return nullptr;
  return &*--I;
}

/// Add a new line entry that has already been encoded into
/// the internal representation of the line table.
void LineTableInfo::AddEntry(FileID FID,
                             const std::vector<LineEntry> &Entries) {
  LineEntries[FID] = Entries;
}

/// getLineTableFilenameID - Return the uniqued ID for the specified filename.
unsigned SourceManager::getLineTableFilenameID(StringRef Name) {
  return getLineTable().getLineTableFilenameID(Name);
}

/// AddLineNote - Add a line note to the line table for the FileID and offset
/// specified by Loc.  If FilenameID is -1, it is considered to be
/// unspecified.
void SourceManager::AddLineNote(SourceLocation Loc, unsigned LineNo,
                                int FilenameID, bool IsFileEntry,
                                bool IsFileExit,
                                SrcMgr::CharacteristicKind FileKind) {
  std::pair<FileID, unsigned> LocInfo = getDecomposedExpansionLoc(Loc);

  bool Invalid = false;
  SLocEntry &Entry = getSLocEntry(LocInfo.first, &Invalid);
  if (!Entry.isFile() || Invalid)
    return;

  SrcMgr::FileInfo &FileInfo = Entry.getFile();

  // Remember that this file has #line directives now if it doesn't already.
  FileInfo.setHasLineDirectives();

  (void) getLineTable();

  unsigned EntryExit = 0;
  if (IsFileEntry)
    EntryExit = 1;
  else if (IsFileExit)
    EntryExit = 2;

  LineTable->AddLineNote(LocInfo.first, LocInfo.second, LineNo, FilenameID,
                         EntryExit, FileKind);
}

LineTableInfo &SourceManager::getLineTable() {
  if (!LineTable)
    LineTable.reset(new LineTableInfo());
  return *LineTable;
}

//===----------------------------------------------------------------------===//
// Private 'Create' methods.
//===----------------------------------------------------------------------===//

SourceManager::SourceManager(DiagnosticsEngine &Diag, FileManager &FileMgr,
                             bool UserFilesAreVolatile)
  : Diag(Diag), FileMgr(FileMgr), UserFilesAreVolatile(UserFilesAreVolatile) {
  clearIDTables();
  Diag.setSourceManager(this);
}

SourceManager::~SourceManager() {
  // Delete FileEntry objects corresponding to content caches.  Since the actual
  // content cache objects are bump pointer allocated, we just have to run the
  // dtors, but we call the deallocate method for completeness.
  for (unsigned i = 0, e = MemBufferInfos.size(); i != e; ++i) {
    if (MemBufferInfos[i]) {
      MemBufferInfos[i]->~ContentCache();
      ContentCacheAlloc.Deallocate(MemBufferInfos[i]);
    }
  }
  for (auto I = FileInfos.begin(), E = FileInfos.end(); I != E; ++I) {
    if (I->second) {
      I->second->~ContentCache();
      ContentCacheAlloc.Deallocate(I->second);
    }
  }
}

void SourceManager::clearIDTables() {
  MainFileID = FileID();
  LocalSLocEntryTable.clear();
  LoadedSLocEntryTable.clear();
  SLocEntryLoaded.clear();
  SLocEntryOffsetLoaded.clear();
  LastLineNoFileIDQuery = FileID();
  LastLineNoContentCache = nullptr;
  LastFileIDLookup = FileID();

  if (LineTable)
    LineTable->clear();

  // Use up FileID #0 as an invalid expansion.
  NextLocalOffset = 0;
  CurrentLoadedOffset = MaxLoadedOffset;
  createExpansionLoc(SourceLocation(), SourceLocation(), SourceLocation(), 1);
}

bool SourceManager::isMainFile(const FileEntry &SourceFile) {
  assert(MainFileID.isValid() && "expected initialized SourceManager");
  if (auto *FE = getFileEntryForID(MainFileID))
    return FE->getUID() == SourceFile.getUID();
  return false;
}

void SourceManager::initializeForReplay(const SourceManager &Old) {
  assert(MainFileID.isInvalid() && "expected uninitialized SourceManager");

  auto CloneContentCache = [&](const ContentCache *Cache) -> ContentCache * {
    auto *Clone = new (ContentCacheAlloc.Allocate<ContentCache>()) ContentCache;
    Clone->OrigEntry = Cache->OrigEntry;
    Clone->ContentsEntry = Cache->ContentsEntry;
    Clone->BufferOverridden = Cache->BufferOverridden;
    Clone->IsFileVolatile = Cache->IsFileVolatile;
    Clone->IsTransient = Cache->IsTransient;
    Clone->setUnownedBuffer(Cache->getBufferIfLoaded());
    return Clone;
  };

  // Ensure all SLocEntries are loaded from the external source.
  for (unsigned I = 0, N = Old.LoadedSLocEntryTable.size(); I != N; ++I)
    if (!Old.SLocEntryLoaded[I])
      Old.loadSLocEntry(I, nullptr);

  // Inherit any content cache data from the old source manager.
  for (auto &FileInfo : Old.FileInfos) {
    SrcMgr::ContentCache *&Slot = FileInfos[FileInfo.first];
    if (Slot)
      continue;
    Slot = CloneContentCache(FileInfo.second);
  }
}

ContentCache &SourceManager::getOrCreateContentCache(FileEntryRef FileEnt,
                                                     bool isSystemFile) {
  // Do we already have information about this file?
  ContentCache *&Entry = FileInfos[FileEnt];
  if (Entry)
    return *Entry;

  // Nope, create a new Cache entry.
  Entry = ContentCacheAlloc.Allocate<ContentCache>();

  if (OverriddenFilesInfo) {
    // If the file contents are overridden with contents from another file,
    // pass that file to ContentCache.
    auto overI = OverriddenFilesInfo->OverriddenFiles.find(FileEnt);
    if (overI == OverriddenFilesInfo->OverriddenFiles.end())
      new (Entry) ContentCache(FileEnt);
    else
      new (Entry) ContentCache(OverridenFilesKeepOriginalName ? FileEnt
                                                              : overI->second,
                               overI->second);
  } else {
    new (Entry) ContentCache(FileEnt);
  }

  Entry->IsFileVolatile = UserFilesAreVolatile && !isSystemFile;
  Entry->IsTransient = FilesAreTransient;
  Entry->BufferOverridden |= FileEnt.isNamedPipe();

  return *Entry;
}

/// Create a new ContentCache for the specified memory buffer.
/// This does no caching.
ContentCache &SourceManager::createMemBufferContentCache(
    std::unique_ptr<llvm::MemoryBuffer> Buffer) {
  // Add a new ContentCache to the MemBufferInfos list and return it.
  ContentCache *Entry = ContentCacheAlloc.Allocate<ContentCache>();
  new (Entry) ContentCache();
  MemBufferInfos.push_back(Entry);
  Entry->setBuffer(std::move(Buffer));
  return *Entry;
}

const SrcMgr::SLocEntry &SourceManager::loadSLocEntry(unsigned Index,
                                                      bool *Invalid) const {
  return const_cast<SourceManager *>(this)->loadSLocEntry(Index, Invalid);
}

SrcMgr::SLocEntry &SourceManager::loadSLocEntry(unsigned Index, bool *Invalid) {
  assert(!SLocEntryLoaded[Index]);
  if (ExternalSLocEntries->ReadSLocEntry(-(static_cast<int>(Index) + 2))) {
    if (Invalid)
      *Invalid = true;
    // If the file of the SLocEntry changed we could still have loaded it.
    if (!SLocEntryLoaded[Index]) {
      // Try to recover; create a SLocEntry so the rest of clang can handle it.
      if (!FakeSLocEntryForRecovery)
        FakeSLocEntryForRecovery = std::make_unique<SLocEntry>(SLocEntry::get(
            0, FileInfo::get(SourceLocation(), getFakeContentCacheForRecovery(),
                             SrcMgr::C_User, "")));
      return *FakeSLocEntryForRecovery;
    }
  }

  return LoadedSLocEntryTable[Index];
}

std::pair<int, SourceLocation::UIntTy>
SourceManager::AllocateLoadedSLocEntries(unsigned NumSLocEntries,
                                         SourceLocation::UIntTy TotalSize) {
  assert(ExternalSLocEntries && "Don't have an external sloc source");
  // Make sure we're not about to run out of source locations.
  if (CurrentLoadedOffset < TotalSize ||
      CurrentLoadedOffset - TotalSize < NextLocalOffset) {
    return std::make_pair(0, 0);
  }
  LoadedSLocEntryTable.resize(LoadedSLocEntryTable.size() + NumSLocEntries);
  SLocEntryLoaded.resize(LoadedSLocEntryTable.size());
  SLocEntryOffsetLoaded.resize(LoadedSLocEntryTable.size());
  CurrentLoadedOffset -= TotalSize;
  updateSlocUsageStats();
  int BaseID = -int(LoadedSLocEntryTable.size()) - 1;
  LoadedSLocEntryAllocBegin.push_back(FileID::get(BaseID));
  return std::make_pair(BaseID, CurrentLoadedOffset);
}

/// As part of recovering from missing or changed content, produce a
/// fake, non-empty buffer.
llvm::MemoryBufferRef SourceManager::getFakeBufferForRecovery() const {
  if (!FakeBufferForRecovery)
    FakeBufferForRecovery =
        llvm::MemoryBuffer::getMemBuffer("<<<INVALID BUFFER>>");

  return *FakeBufferForRecovery;
}

/// As part of recovering from missing or changed content, produce a
/// fake content cache.
SrcMgr::ContentCache &SourceManager::getFakeContentCacheForRecovery() const {
  if (!FakeContentCacheForRecovery) {
    FakeContentCacheForRecovery = std::make_unique<SrcMgr::ContentCache>();
    FakeContentCacheForRecovery->setUnownedBuffer(getFakeBufferForRecovery());
  }
  return *FakeContentCacheForRecovery;
}

/// Returns the previous in-order FileID or an invalid FileID if there
/// is no previous one.
FileID SourceManager::getPreviousFileID(FileID FID) const {
  if (FID.isInvalid())
    return FileID();

  int ID = FID.ID;
  if (ID == -1)
    return FileID();

  if (ID > 0) {
    if (ID-1 == 0)
      return FileID();
  } else if (unsigned(-(ID-1) - 2) >= LoadedSLocEntryTable.size()) {
    return FileID();
  }

  return FileID::get(ID-1);
}

/// Returns the next in-order FileID or an invalid FileID if there is
/// no next one.
FileID SourceManager::getNextFileID(FileID FID) const {
  if (FID.isInvalid())
    return FileID();

  int ID = FID.ID;
  if (ID > 0) {
    if (unsigned(ID+1) >= local_sloc_entry_size())
      return FileID();
  } else if (ID+1 >= -1) {
    return FileID();
  }

  return FileID::get(ID+1);
}

//===----------------------------------------------------------------------===//
// Methods to create new FileID's and macro expansions.
//===----------------------------------------------------------------------===//

/// Create a new FileID that represents the specified file
/// being \#included from the specified IncludePosition.
FileID SourceManager::createFileID(FileEntryRef SourceFile,
                                   SourceLocation IncludePos,
                                   SrcMgr::CharacteristicKind FileCharacter,
                                   int LoadedID,
                                   SourceLocation::UIntTy LoadedOffset) {
  SrcMgr::ContentCache &IR = getOrCreateContentCache(SourceFile,
                                                     isSystem(FileCharacter));

  // If this is a named pipe, immediately load the buffer to ensure subsequent
  // calls to ContentCache::getSize() are accurate.
  if (IR.ContentsEntry->isNamedPipe())
    (void)IR.getBufferOrNone(Diag, getFileManager(), SourceLocation());

  return createFileIDImpl(IR, SourceFile.getName(), IncludePos, FileCharacter,
                          LoadedID, LoadedOffset);
}

/// Create a new FileID that represents the specified memory buffer.
///
/// This does no caching of the buffer and takes ownership of the
/// MemoryBuffer, so only pass a MemoryBuffer to this once.
FileID SourceManager::createFileID(std::unique_ptr<llvm::MemoryBuffer> Buffer,
                                   SrcMgr::CharacteristicKind FileCharacter,
                                   int LoadedID,
                                   SourceLocation::UIntTy LoadedOffset,
                                   SourceLocation IncludeLoc) {
  StringRef Name = Buffer->getBufferIdentifier();
  return createFileIDImpl(createMemBufferContentCache(std::move(Buffer)), Name,
                          IncludeLoc, FileCharacter, LoadedID, LoadedOffset);
}

/// Create a new FileID that represents the specified memory buffer.
///
/// This does not take ownership of the MemoryBuffer. The memory buffer must
/// outlive the SourceManager.
FileID SourceManager::createFileID(const llvm::MemoryBufferRef &Buffer,
                                   SrcMgr::CharacteristicKind FileCharacter,
                                   int LoadedID,
                                   SourceLocation::UIntTy LoadedOffset,
                                   SourceLocation IncludeLoc) {
  return createFileID(llvm::MemoryBuffer::getMemBuffer(Buffer), FileCharacter,
                      LoadedID, LoadedOffset, IncludeLoc);
}

/// Get the FileID for \p SourceFile if it exists. Otherwise, create a
/// new FileID for the \p SourceFile.
FileID
SourceManager::getOrCreateFileID(FileEntryRef SourceFile,
                                 SrcMgr::CharacteristicKind FileCharacter) {
  FileID ID = translateFile(SourceFile);
  return ID.isValid() ? ID : createFileID(SourceFile, SourceLocation(),
					  FileCharacter);
}

/// createFileID - Create a new FileID for the specified ContentCache and
/// include position.  This works regardless of whether the ContentCache
/// corresponds to a file or some other input source.
FileID SourceManager::createFileIDImpl(ContentCache &File, StringRef Filename,
                                       SourceLocation IncludePos,
                                       SrcMgr::CharacteristicKind FileCharacter,
                                       int LoadedID,
                                       SourceLocation::UIntTy LoadedOffset) {
  if (LoadedID < 0) {
    assert(LoadedID != -1 && "Loading sentinel FileID");
    unsigned Index = unsigned(-LoadedID) - 2;
    assert(Index < LoadedSLocEntryTable.size() && "FileID out of range");
    assert(!SLocEntryLoaded[Index] && "FileID already loaded");
    LoadedSLocEntryTable[Index] = SLocEntry::get(
        LoadedOffset, FileInfo::get(IncludePos, File, FileCharacter, Filename));
    SLocEntryLoaded[Index] = SLocEntryOffsetLoaded[Index] = true;
    return FileID::get(LoadedID);
  }
  unsigned FileSize = File.getSize();
  if (!(NextLocalOffset + FileSize + 1 > NextLocalOffset &&
        NextLocalOffset + FileSize + 1 <= CurrentLoadedOffset)) {
    Diag.Report(IncludePos, diag::err_sloc_space_too_large);
    noteSLocAddressSpaceUsage(Diag);
    return FileID();
  }
  LocalSLocEntryTable.push_back(
      SLocEntry::get(NextLocalOffset,
                     FileInfo::get(IncludePos, File, FileCharacter, Filename)));
  // We do a +1 here because we want a SourceLocation that means "the end of the
  // file", e.g. for the "no newline at the end of the file" diagnostic.
  NextLocalOffset += FileSize + 1;
  updateSlocUsageStats();

  // Set LastFileIDLookup to the newly created file.  The next getFileID call is
  // almost guaranteed to be from that file.
  FileID FID = FileID::get(LocalSLocEntryTable.size()-1);
  return LastFileIDLookup = FID;
}

SourceLocation SourceManager::createMacroArgExpansionLoc(
    SourceLocation SpellingLoc, SourceLocation ExpansionLoc, unsigned Length) {
  ExpansionInfo Info = ExpansionInfo::createForMacroArg(SpellingLoc,
                                                        ExpansionLoc);
  return createExpansionLocImpl(Info, Length);
}

SourceLocation SourceManager::createExpansionLoc(
    SourceLocation SpellingLoc, SourceLocation ExpansionLocStart,
    SourceLocation ExpansionLocEnd, unsigned Length,
    bool ExpansionIsTokenRange, int LoadedID,
    SourceLocation::UIntTy LoadedOffset) {
  ExpansionInfo Info = ExpansionInfo::create(
      SpellingLoc, ExpansionLocStart, ExpansionLocEnd, ExpansionIsTokenRange);
  return createExpansionLocImpl(Info, Length, LoadedID, LoadedOffset);
}

SourceLocation SourceManager::createTokenSplitLoc(SourceLocation Spelling,
                                                  SourceLocation TokenStart,
                                                  SourceLocation TokenEnd) {
  assert(getFileID(TokenStart) == getFileID(TokenEnd) &&
         "token spans multiple files");
  return createExpansionLocImpl(
      ExpansionInfo::createForTokenSplit(Spelling, TokenStart, TokenEnd),
      TokenEnd.getOffset() - TokenStart.getOffset());
}

SourceLocation
SourceManager::createExpansionLocImpl(const ExpansionInfo &Info,
                                      unsigned Length, int LoadedID,
                                      SourceLocation::UIntTy LoadedOffset) {
  if (LoadedID < 0) {
    assert(LoadedID != -1 && "Loading sentinel FileID");
    unsigned Index = unsigned(-LoadedID) - 2;
    assert(Index < LoadedSLocEntryTable.size() && "FileID out of range");
    assert(!SLocEntryLoaded[Index] && "FileID already loaded");
    LoadedSLocEntryTable[Index] = SLocEntry::get(LoadedOffset, Info);
    SLocEntryLoaded[Index] = SLocEntryOffsetLoaded[Index] = true;
    return SourceLocation::getMacroLoc(LoadedOffset);
  }
  LocalSLocEntryTable.push_back(SLocEntry::get(NextLocalOffset, Info));
  if (NextLocalOffset + Length + 1 <= NextLocalOffset ||
      NextLocalOffset + Length + 1 > CurrentLoadedOffset) {
    Diag.Report(SourceLocation(), diag::err_sloc_space_too_large);
    // FIXME: call `noteSLocAddressSpaceUsage` to report details to users and
    // use a source location from `Info` to point at an error.
    // Currently, both cause Clang to run indefinitely, this needs to be fixed.
    // FIXME: return an error instead of crashing. Returning invalid source
    // locations causes compiler to run indefinitely.
    llvm::report_fatal_error("ran out of source locations");
  }
  // See createFileID for that +1.
  NextLocalOffset += Length + 1;
  updateSlocUsageStats();
  return SourceLocation::getMacroLoc(NextLocalOffset - (Length + 1));
}

std::optional<llvm::MemoryBufferRef>
SourceManager::getMemoryBufferForFileOrNone(FileEntryRef File) {
  SrcMgr::ContentCache &IR = getOrCreateContentCache(File);
  return IR.getBufferOrNone(Diag, getFileManager(), SourceLocation());
}

void SourceManager::overrideFileContents(
    FileEntryRef SourceFile, std::unique_ptr<llvm::MemoryBuffer> Buffer) {
  SrcMgr::ContentCache &IR = getOrCreateContentCache(SourceFile);

  IR.setBuffer(std::move(Buffer));
  IR.BufferOverridden = true;

  getOverriddenFilesInfo().OverriddenFilesWithBuffer.insert(SourceFile);
}

void SourceManager::overrideFileContents(const FileEntry *SourceFile,
                                         FileEntryRef NewFile) {
  assert(SourceFile->getSize() == NewFile.getSize() &&
         "Different sizes, use the FileManager to create a virtual file with "
         "the correct size");
  assert(FileInfos.find_as(SourceFile) == FileInfos.end() &&
         "This function should be called at the initialization stage, before "
         "any parsing occurs.");
  // FileEntryRef is not default-constructible.
  auto Pair = getOverriddenFilesInfo().OverriddenFiles.insert(
      std::make_pair(SourceFile, NewFile));
  if (!Pair.second)
    Pair.first->second = NewFile;
}

OptionalFileEntryRef
SourceManager::bypassFileContentsOverride(FileEntryRef File) {
  assert(isFileOverridden(&File.getFileEntry()));
  OptionalFileEntryRef BypassFile = FileMgr.getBypassFile(File);

  // If the file can't be found in the FS, give up.
  if (!BypassFile)
    return std::nullopt;

  (void)getOrCreateContentCache(*BypassFile);
  return BypassFile;
}

void SourceManager::setFileIsTransient(FileEntryRef File) {
  getOrCreateContentCache(File).IsTransient = true;
}

std::optional<StringRef>
SourceManager::getNonBuiltinFilenameForID(FileID FID) const {
  if (const SrcMgr::SLocEntry *Entry = getSLocEntryForFile(FID))
    if (Entry->getFile().getContentCache().OrigEntry)
      return Entry->getFile().getName();
  return std::nullopt;
}

StringRef SourceManager::getBufferData(FileID FID, bool *Invalid) const {
  auto B = getBufferDataOrNone(FID);
  if (Invalid)
    *Invalid = !B;
  return B ? *B : "<<<<<INVALID SOURCE LOCATION>>>>>";
}

std::optional<StringRef>
SourceManager::getBufferDataIfLoaded(FileID FID) const {
  if (const SrcMgr::SLocEntry *Entry = getSLocEntryForFile(FID))
    return Entry->getFile().getContentCache().getBufferDataIfLoaded();
  return std::nullopt;
}

std::optional<StringRef> SourceManager::getBufferDataOrNone(FileID FID) const {
  if (const SrcMgr::SLocEntry *Entry = getSLocEntryForFile(FID))
    if (auto B = Entry->getFile().getContentCache().getBufferOrNone(
            Diag, getFileManager(), SourceLocation()))
      return B->getBuffer();
  return std::nullopt;
}

//===----------------------------------------------------------------------===//
// SourceLocation manipulation methods.
//===----------------------------------------------------------------------===//

/// Return the FileID for a SourceLocation.
///
/// This is the cache-miss path of getFileID. Not as hot as that function, but
/// still very important. It is responsible for finding the entry in the
/// SLocEntry tables that contains the specified location.
FileID SourceManager::getFileIDSlow(SourceLocation::UIntTy SLocOffset) const {
  if (!SLocOffset)
    return FileID::get(0);

  // Now it is time to search for the correct file. See where the SLocOffset
  // sits in the global view and consult local or loaded buffers for it.
  if (SLocOffset < NextLocalOffset)
    return getFileIDLocal(SLocOffset);
  return getFileIDLoaded(SLocOffset);
}

/// Return the FileID for a SourceLocation with a low offset.
///
/// This function knows that the SourceLocation is in a local buffer, not a
/// loaded one.
FileID SourceManager::getFileIDLocal(SourceLocation::UIntTy SLocOffset) const {
  assert(SLocOffset < NextLocalOffset && "Bad function choice");

  // After the first and second level caches, I see two common sorts of
  // behavior: 1) a lot of searched FileID's are "near" the cached file
  // location or are "near" the cached expansion location. 2) others are just
  // completely random and may be a very long way away.
  //
  // To handle this, we do a linear search for up to 8 steps to catch #1 quickly
  // then we fall back to a less cache efficient, but more scalable, binary
  // search to find the location.

  // See if this is near the file point - worst case we start scanning from the
  // most newly created FileID.

  // LessIndex - This is the lower bound of the range that we're searching.
  // We know that the offset corresponding to the FileID is less than
  // SLocOffset.
  unsigned LessIndex = 0;
  // upper bound of the search range.
  unsigned GreaterIndex = LocalSLocEntryTable.size();
  if (LastFileIDLookup.ID >= 0) {
    // Use the LastFileIDLookup to prune the search space.
    if (LocalSLocEntryTable[LastFileIDLookup.ID].getOffset() < SLocOffset)
      LessIndex = LastFileIDLookup.ID;
    else
      GreaterIndex = LastFileIDLookup.ID;
  }

  // Find the FileID that contains this.
  unsigned NumProbes = 0;
  while (true) {
    --GreaterIndex;
    assert(GreaterIndex < LocalSLocEntryTable.size());
    if (LocalSLocEntryTable[GreaterIndex].getOffset() <= SLocOffset) {
      FileID Res = FileID::get(int(GreaterIndex));
      // Remember it.  We have good locality across FileID lookups.
      LastFileIDLookup = Res;
      NumLinearScans += NumProbes+1;
      return Res;
    }
    if (++NumProbes == 8)
      break;
  }

  NumProbes = 0;
  while (true) {
    unsigned MiddleIndex = (GreaterIndex-LessIndex)/2+LessIndex;
    SourceLocation::UIntTy MidOffset =
        getLocalSLocEntry(MiddleIndex).getOffset();

    ++NumProbes;

    // If the offset of the midpoint is too large, chop the high side of the
    // range to the midpoint.
    if (MidOffset > SLocOffset) {
      GreaterIndex = MiddleIndex;
      continue;
    }

    // If the middle index contains the value, succeed and return.
    if (MiddleIndex + 1 == LocalSLocEntryTable.size() ||
        SLocOffset < getLocalSLocEntry(MiddleIndex + 1).getOffset()) {
      FileID Res = FileID::get(MiddleIndex);

      // Remember it.  We have good locality across FileID lookups.
      LastFileIDLookup = Res;
      NumBinaryProbes += NumProbes;
      return Res;
    }

    // Otherwise, move the low-side up to the middle index.
    LessIndex = MiddleIndex;
  }
}

/// Return the FileID for a SourceLocation with a high offset.
///
/// This function knows that the SourceLocation is in a loaded buffer, not a
/// local one.
FileID SourceManager::getFileIDLoaded(SourceLocation::UIntTy SLocOffset) const {
  if (SLocOffset < CurrentLoadedOffset) {
    assert(0 && "Invalid SLocOffset or bad function choice");
    return FileID();
  }

  return FileID::get(ExternalSLocEntries->getSLocEntryID(SLocOffset));
}

SourceLocation SourceManager::
getExpansionLocSlowCase(SourceLocation Loc) const {
  do {
    // Note: If Loc indicates an offset into a token that came from a macro
    // expansion (e.g. the 5th character of the token) we do not want to add
    // this offset when going to the expansion location.  The expansion
    // location is the macro invocation, which the offset has nothing to do
    // with.  This is unlike when we get the spelling loc, because the offset
    // directly correspond to the token whose spelling we're inspecting.
    Loc = getSLocEntry(getFileID(Loc)).getExpansion().getExpansionLocStart();
  } while (!Loc.isFileID());

  return Loc;
}

SourceLocation SourceManager::getSpellingLocSlowCase(SourceLocation Loc) const {
  do {
    std::pair<FileID, unsigned> LocInfo = getDecomposedLoc(Loc);
    Loc = getSLocEntry(LocInfo.first).getExpansion().getSpellingLoc();
    Loc = Loc.getLocWithOffset(LocInfo.second);
  } while (!Loc.isFileID());
  return Loc;
}

SourceLocation SourceManager::getFileLocSlowCase(SourceLocation Loc) const {
  do {
    if (isMacroArgExpansion(Loc))
      Loc = getImmediateSpellingLoc(Loc);
    else
      Loc = getImmediateExpansionRange(Loc).getBegin();
  } while (!Loc.isFileID());
  return Loc;
}


std::pair<FileID, unsigned>
SourceManager::getDecomposedExpansionLocSlowCase(
                                             const SrcMgr::SLocEntry *E) const {
  // If this is an expansion record, walk through all the expansion points.
  FileID FID;
  SourceLocation Loc;
  unsigned Offset;
  do {
    Loc = E->getExpansion().getExpansionLocStart();

    FID = getFileID(Loc);
    E = &getSLocEntry(FID);
    Offset = Loc.getOffset()-E->getOffset();
  } while (!Loc.isFileID());

  return std::make_pair(FID, Offset);
}

std::pair<FileID, unsigned>
SourceManager::getDecomposedSpellingLocSlowCase(const SrcMgr::SLocEntry *E,
                                                unsigned Offset) const {
  // If this is an expansion record, walk through all the expansion points.
  FileID FID;
  SourceLocation Loc;
  do {
    Loc = E->getExpansion().getSpellingLoc();
    Loc = Loc.getLocWithOffset(Offset);

    FID = getFileID(Loc);
    E = &getSLocEntry(FID);
    Offset = Loc.getOffset()-E->getOffset();
  } while (!Loc.isFileID());

  return std::make_pair(FID, Offset);
}

/// getImmediateSpellingLoc - Given a SourceLocation object, return the
/// spelling location referenced by the ID.  This is the first level down
/// towards the place where the characters that make up the lexed token can be
/// found.  This should not generally be used by clients.
SourceLocation SourceManager::getImmediateSpellingLoc(SourceLocation Loc) const{
  if (Loc.isFileID()) return Loc;
  std::pair<FileID, unsigned> LocInfo = getDecomposedLoc(Loc);
  Loc = getSLocEntry(LocInfo.first).getExpansion().getSpellingLoc();
  return Loc.getLocWithOffset(LocInfo.second);
}

/// Return the filename of the file containing a SourceLocation.
StringRef SourceManager::getFilename(SourceLocation SpellingLoc) const {
  if (OptionalFileEntryRef F = getFileEntryRefForID(getFileID(SpellingLoc)))
    return F->getName();
  return StringRef();
}

/// getImmediateExpansionRange - Loc is required to be an expansion location.
/// Return the start/end of the expansion information.
CharSourceRange
SourceManager::getImmediateExpansionRange(SourceLocation Loc) const {
  assert(Loc.isMacroID() && "Not a macro expansion loc!");
  const ExpansionInfo &Expansion = getSLocEntry(getFileID(Loc)).getExpansion();
  return Expansion.getExpansionLocRange();
}

SourceLocation SourceManager::getTopMacroCallerLoc(SourceLocation Loc) const {
  while (isMacroArgExpansion(Loc))
    Loc = getImmediateSpellingLoc(Loc);
  return Loc;
}

/// getExpansionRange - Given a SourceLocation object, return the range of
/// tokens covered by the expansion in the ultimate file.
CharSourceRange SourceManager::getExpansionRange(SourceLocation Loc) const {
  if (Loc.isFileID())
    return CharSourceRange(SourceRange(Loc, Loc), true);

  CharSourceRange Res = getImmediateExpansionRange(Loc);

  // Fully resolve the start and end locations to their ultimate expansion
  // points.
  while (!Res.getBegin().isFileID())
    Res.setBegin(getImmediateExpansionRange(Res.getBegin()).getBegin());
  while (!Res.getEnd().isFileID()) {
    CharSourceRange EndRange = getImmediateExpansionRange(Res.getEnd());
    Res.setEnd(EndRange.getEnd());
    Res.setTokenRange(EndRange.isTokenRange());
  }
  return Res;
}

bool SourceManager::isMacroArgExpansion(SourceLocation Loc,
                                        SourceLocation *StartLoc) const {
  if (!Loc.isMacroID()) return false;

  FileID FID = getFileID(Loc);
  const SrcMgr::ExpansionInfo &Expansion = getSLocEntry(FID).getExpansion();
  if (!Expansion.isMacroArgExpansion()) return false;

  if (StartLoc)
    *StartLoc = Expansion.getExpansionLocStart();
  return true;
}

bool SourceManager::isMacroBodyExpansion(SourceLocation Loc) const {
  if (!Loc.isMacroID()) return false;

  FileID FID = getFileID(Loc);
  const SrcMgr::ExpansionInfo &Expansion = getSLocEntry(FID).getExpansion();
  return Expansion.isMacroBodyExpansion();
}

bool SourceManager::isAtStartOfImmediateMacroExpansion(SourceLocation Loc,
                                             SourceLocation *MacroBegin) const {
  assert(Loc.isValid() && Loc.isMacroID() && "Expected a valid macro loc");

  std::pair<FileID, unsigned> DecompLoc = getDecomposedLoc(Loc);
  if (DecompLoc.second > 0)
    return false; // Does not point at the start of expansion range.

  bool Invalid = false;
  const SrcMgr::ExpansionInfo &ExpInfo =
      getSLocEntry(DecompLoc.first, &Invalid).getExpansion();
  if (Invalid)
    return false;
  SourceLocation ExpLoc = ExpInfo.getExpansionLocStart();

  if (ExpInfo.isMacroArgExpansion()) {
    // For macro argument expansions, check if the previous FileID is part of
    // the same argument expansion, in which case this Loc is not at the
    // beginning of the expansion.
    FileID PrevFID = getPreviousFileID(DecompLoc.first);
    if (!PrevFID.isInvalid()) {
      const SrcMgr::SLocEntry &PrevEntry = getSLocEntry(PrevFID, &Invalid);
      if (Invalid)
        return false;
      if (PrevEntry.isExpansion() &&
          PrevEntry.getExpansion().getExpansionLocStart() == ExpLoc)
        return false;
    }
  }

  if (MacroBegin)
    *MacroBegin = ExpLoc;
  return true;
}

bool SourceManager::isAtEndOfImmediateMacroExpansion(SourceLocation Loc,
                                               SourceLocation *MacroEnd) const {
  assert(Loc.isValid() && Loc.isMacroID() && "Expected a valid macro loc");

  FileID FID = getFileID(Loc);
  SourceLocation NextLoc = Loc.getLocWithOffset(1);
  if (isInFileID(NextLoc, FID))
    return false; // Does not point at the end of expansion range.

  bool Invalid = false;
  const SrcMgr::ExpansionInfo &ExpInfo =
      getSLocEntry(FID, &Invalid).getExpansion();
  if (Invalid)
    return false;

  if (ExpInfo.isMacroArgExpansion()) {
    // For macro argument expansions, check if the next FileID is part of the
    // same argument expansion, in which case this Loc is not at the end of the
    // expansion.
    FileID NextFID = getNextFileID(FID);
    if (!NextFID.isInvalid()) {
      const SrcMgr::SLocEntry &NextEntry = getSLocEntry(NextFID, &Invalid);
      if (Invalid)
        return false;
      if (NextEntry.isExpansion() &&
          NextEntry.getExpansion().getExpansionLocStart() ==
              ExpInfo.getExpansionLocStart())
        return false;
    }
  }

  if (MacroEnd)
    *MacroEnd = ExpInfo.getExpansionLocEnd();
  return true;
}

//===----------------------------------------------------------------------===//
// Queries about the code at a SourceLocation.
//===----------------------------------------------------------------------===//

/// getCharacterData - Return a pointer to the start of the specified location
/// in the appropriate MemoryBuffer.
const char *SourceManager::getCharacterData(SourceLocation SL,
                                            bool *Invalid) const {
  // Note that this is a hot function in the getSpelling() path, which is
  // heavily used by -E mode.
  std::pair<FileID, unsigned> LocInfo = getDecomposedSpellingLoc(SL);

  // Note that calling 'getBuffer()' may lazily page in a source file.
  bool CharDataInvalid = false;
  const SLocEntry &Entry = getSLocEntry(LocInfo.first, &CharDataInvalid);
  if (CharDataInvalid || !Entry.isFile()) {
    if (Invalid)
      *Invalid = true;

    return "<<<<INVALID BUFFER>>>>";
  }
  std::optional<llvm::MemoryBufferRef> Buffer =
      Entry.getFile().getContentCache().getBufferOrNone(Diag, getFileManager(),
                                                        SourceLocation());
  if (Invalid)
    *Invalid = !Buffer;
  return Buffer ? Buffer->getBufferStart() + LocInfo.second
                : "<<<<INVALID BUFFER>>>>";
}

/// getColumnNumber - Return the column # for the specified file position.
/// this is significantly cheaper to compute than the line number.
unsigned SourceManager::getColumnNumber(FileID FID, unsigned FilePos,
                                        bool *Invalid) const {
  std::optional<llvm::MemoryBufferRef> MemBuf = getBufferOrNone(FID);
  if (Invalid)
    *Invalid = !MemBuf;

  if (!MemBuf)
    return 1;

  // It is okay to request a position just past the end of the buffer.
  if (FilePos > MemBuf->getBufferSize()) {
    if (Invalid)
      *Invalid = true;
    return 1;
  }

  const char *Buf = MemBuf->getBufferStart();
  // See if we just calculated the line number for this FilePos and can use
  // that to lookup the start of the line instead of searching for it.
  if (LastLineNoFileIDQuery == FID && LastLineNoContentCache->SourceLineCache &&
      LastLineNoResult < LastLineNoContentCache->SourceLineCache.size()) {
    const unsigned *SourceLineCache =
        LastLineNoContentCache->SourceLineCache.begin();
    unsigned LineStart = SourceLineCache[LastLineNoResult - 1];
    unsigned LineEnd = SourceLineCache[LastLineNoResult];
    if (FilePos >= LineStart && FilePos < LineEnd) {
      // LineEnd is the LineStart of the next line.
      // A line ends with separator LF or CR+LF on Windows.
      // FilePos might point to the last separator,
      // but we need a column number at most 1 + the last column.
      if (FilePos + 1 == LineEnd && FilePos > LineStart) {
        if (Buf[FilePos - 1] == '\r' || Buf[FilePos - 1] == '\n')
          --FilePos;
      }
      return FilePos - LineStart + 1;
    }
  }

  unsigned LineStart = FilePos;
  while (LineStart && Buf[LineStart-1] != '\n' && Buf[LineStart-1] != '\r')
    --LineStart;
  return FilePos-LineStart+1;
}

// isInvalid - Return the result of calling loc.isInvalid(), and
// if Invalid is not null, set its value to same.
template<typename LocType>
static bool isInvalid(LocType Loc, bool *Invalid) {
  bool MyInvalid = Loc.isInvalid();
  if (Invalid)
    *Invalid = MyInvalid;
  return MyInvalid;
}

unsigned SourceManager::getSpellingColumnNumber(SourceLocation Loc,
                                                bool *Invalid) const {
  if (isInvalid(Loc, Invalid)) return 0;
  std::pair<FileID, unsigned> LocInfo = getDecomposedSpellingLoc(Loc);
  return getColumnNumber(LocInfo.first, LocInfo.second, Invalid);
}

unsigned SourceManager::getExpansionColumnNumber(SourceLocation Loc,
                                                 bool *Invalid) const {
  if (isInvalid(Loc, Invalid)) return 0;
  std::pair<FileID, unsigned> LocInfo = getDecomposedExpansionLoc(Loc);
  return getColumnNumber(LocInfo.first, LocInfo.second, Invalid);
}

unsigned SourceManager::getPresumedColumnNumber(SourceLocation Loc,
                                                bool *Invalid) const {
  PresumedLoc PLoc = getPresumedLoc(Loc);
  if (isInvalid(PLoc, Invalid)) return 0;
  return PLoc.getColumn();
}

// Check if mutli-byte word x has bytes between m and n, included. This may also
// catch bytes equal to n + 1.
// The returned value holds a 0x80 at each byte position that holds a match.
// see http://graphics.stanford.edu/~seander/bithacks.html#HasBetweenInWord
template <class T>
static constexpr inline T likelyhasbetween(T x, unsigned char m,
                                           unsigned char n) {
  return ((x - ~static_cast<T>(0) / 255 * (n + 1)) & ~x &
          ((x & ~static_cast<T>(0) / 255 * 127) +
           (~static_cast<T>(0) / 255 * (127 - (m - 1))))) &
         ~static_cast<T>(0) / 255 * 128;
}

LineOffsetMapping LineOffsetMapping::get(llvm::MemoryBufferRef Buffer,
                                         llvm::BumpPtrAllocator &Alloc) {

  // Find the file offsets of all of the *physical* source lines.  This does
  // not look at trigraphs, escaped newlines, or anything else tricky.
  SmallVector<unsigned, 256> LineOffsets;

  // Line #1 starts at char 0.
  LineOffsets.push_back(0);

  const unsigned char *Start = (const unsigned char *)Buffer.getBufferStart();
  const unsigned char *End = (const unsigned char *)Buffer.getBufferEnd();
  const unsigned char *Buf = Start;

  uint64_t Word;

  // scan sizeof(Word) bytes at a time for new lines.
  // This is much faster than scanning each byte independently.
  if ((unsigned long)(End - Start) > sizeof(Word)) {
    do {
      Word = llvm::support::endian::read64(Buf, llvm::endianness::little);
      // no new line => jump over sizeof(Word) bytes.
      auto Mask = likelyhasbetween(Word, '\n', '\r');
      if (!Mask) {
        Buf += sizeof(Word);
        continue;
      }

      // At that point, Mask contains 0x80 set at each byte that holds a value
      // in [\n, \r + 1 [

      // Scan for the next newline - it's very likely there's one.
      unsigned N = llvm::countr_zero(Mask) - 7; // -7 because 0x80 is the marker
      Word >>= N;
      Buf += N / 8 + 1;
      unsigned char Byte = Word;
      switch (Byte) {
      case '\r':
        // If this is \r\n, skip both characters.
        if (*Buf == '\n') {
          ++Buf;
        }
        [[fallthrough]];
      case '\n':
        LineOffsets.push_back(Buf - Start);
      };
    } while (Buf < End - sizeof(Word) - 1);
  }

  // Handle tail using a regular check.
  while (Buf < End) {
    if (*Buf == '\n') {
      LineOffsets.push_back(Buf - Start + 1);
    } else if (*Buf == '\r') {
      // If this is \r\n, skip both characters.
      if (Buf + 1 < End && Buf[1] == '\n') {
        ++Buf;
      }
      LineOffsets.push_back(Buf - Start + 1);
    }
    ++Buf;
  }

  return LineOffsetMapping(LineOffsets, Alloc);
}

LineOffsetMapping::LineOffsetMapping(ArrayRef<unsigned> LineOffsets,
                                     llvm::BumpPtrAllocator &Alloc)
    : Storage(Alloc.Allocate<unsigned>(LineOffsets.size() + 1)) {
  Storage[0] = LineOffsets.size();
  std::copy(LineOffsets.begin(), LineOffsets.end(), Storage + 1);
}

/// getLineNumber - Given a SourceLocation, return the spelling line number
/// for the position indicated.  This requires building and caching a table of
/// line offsets for the MemoryBuffer, so this is not cheap: use only when
/// about to emit a diagnostic.
unsigned SourceManager::getLineNumber(FileID FID, unsigned FilePos,
                                      bool *Invalid) const {
  if (FID.isInvalid()) {
    if (Invalid)
      *Invalid = true;
    return 1;
  }

  const ContentCache *Content;
  if (LastLineNoFileIDQuery == FID)
    Content = LastLineNoContentCache;
  else {
    bool MyInvalid = false;
    const SLocEntry &Entry = getSLocEntry(FID, &MyInvalid);
    if (MyInvalid || !Entry.isFile()) {
      if (Invalid)
        *Invalid = true;
      return 1;
    }

    Content = &Entry.getFile().getContentCache();
  }

  // If this is the first use of line information for this buffer, compute the
  // SourceLineCache for it on demand.
  if (!Content->SourceLineCache) {
    std::optional<llvm::MemoryBufferRef> Buffer =
        Content->getBufferOrNone(Diag, getFileManager(), SourceLocation());
    if (Invalid)
      *Invalid = !Buffer;
    if (!Buffer)
      return 1;

    Content->SourceLineCache =
        LineOffsetMapping::get(*Buffer, ContentCacheAlloc);
  } else if (Invalid)
    *Invalid = false;

  // Okay, we know we have a line number table.  Do a binary search to find the
  // line number that this character position lands on.
  const unsigned *SourceLineCache = Content->SourceLineCache.begin();
  const unsigned *SourceLineCacheStart = SourceLineCache;
  const unsigned *SourceLineCacheEnd = Content->SourceLineCache.end();

  unsigned QueriedFilePos = FilePos+1;

  // FIXME: I would like to be convinced that this code is worth being as
  // complicated as it is, binary search isn't that slow.
  //
  // If it is worth being optimized, then in my opinion it could be more
  // performant, simpler, and more obviously correct by just "galloping" outward
  // from the queried file position. In fact, this could be incorporated into a
  // generic algorithm such as lower_bound_with_hint.
  //
  // If someone gives me a test case where this matters, and I will do it! - DWD

  // If the previous query was to the same file, we know both the file pos from
  // that query and the line number returned.  This allows us to narrow the
  // search space from the entire file to something near the match.
  if (LastLineNoFileIDQuery == FID) {
    if (QueriedFilePos >= LastLineNoFilePos) {
      // FIXME: Potential overflow?
      SourceLineCache = SourceLineCache+LastLineNoResult-1;

      // The query is likely to be nearby the previous one.  Here we check to
      // see if it is within 5, 10 or 20 lines.  It can be far away in cases
      // where big comment blocks and vertical whitespace eat up lines but
      // contribute no tokens.
      if (SourceLineCache+5 < SourceLineCacheEnd) {
        if (SourceLineCache[5] > QueriedFilePos)
          SourceLineCacheEnd = SourceLineCache+5;
        else if (SourceLineCache+10 < SourceLineCacheEnd) {
          if (SourceLineCache[10] > QueriedFilePos)
            SourceLineCacheEnd = SourceLineCache+10;
          else if (SourceLineCache+20 < SourceLineCacheEnd) {
            if (SourceLineCache[20] > QueriedFilePos)
              SourceLineCacheEnd = SourceLineCache+20;
          }
        }
      }
    } else {
      if (LastLineNoResult < Content->SourceLineCache.size())
        SourceLineCacheEnd = SourceLineCache+LastLineNoResult+1;
    }
  }

  const unsigned *Pos =
      std::lower_bound(SourceLineCache, SourceLineCacheEnd, QueriedFilePos);
  unsigned LineNo = Pos-SourceLineCacheStart;

  LastLineNoFileIDQuery = FID;
  LastLineNoContentCache = Content;
  LastLineNoFilePos = QueriedFilePos;
  LastLineNoResult = LineNo;
  return LineNo;
}

unsigned SourceManager::getSpellingLineNumber(SourceLocation Loc,
                                              bool *Invalid) const {
  if (isInvalid(Loc, Invalid)) return 0;
  std::pair<FileID, unsigned> LocInfo = getDecomposedSpellingLoc(Loc);
  return getLineNumber(LocInfo.first, LocInfo.second);
}
unsigned SourceManager::getExpansionLineNumber(SourceLocation Loc,
                                               bool *Invalid) const {
  if (isInvalid(Loc, Invalid)) return 0;
  std::pair<FileID, unsigned> LocInfo = getDecomposedExpansionLoc(Loc);
  return getLineNumber(LocInfo.first, LocInfo.second);
}
unsigned SourceManager::getPresumedLineNumber(SourceLocation Loc,
                                              bool *Invalid) const {
  PresumedLoc PLoc = getPresumedLoc(Loc);
  if (isInvalid(PLoc, Invalid)) return 0;
  return PLoc.getLine();
}

/// getFileCharacteristic - return the file characteristic of the specified
/// source location, indicating whether this is a normal file, a system
/// header, or an "implicit extern C" system header.
///
/// This state can be modified with flags on GNU linemarker directives like:
///   # 4 "foo.h" 3
/// which changes all source locations in the current file after that to be
/// considered to be from a system header.
SrcMgr::CharacteristicKind
SourceManager::getFileCharacteristic(SourceLocation Loc) const {
  assert(Loc.isValid() && "Can't get file characteristic of invalid loc!");
  std::pair<FileID, unsigned> LocInfo = getDecomposedExpansionLoc(Loc);
  const SLocEntry *SEntry = getSLocEntryForFile(LocInfo.first);
  if (!SEntry)
    return C_User;

  const SrcMgr::FileInfo &FI = SEntry->getFile();

  // If there are no #line directives in this file, just return the whole-file
  // state.
  if (!FI.hasLineDirectives())
    return FI.getFileCharacteristic();

  assert(LineTable && "Can't have linetable entries without a LineTable!");
  // See if there is a #line directive before the location.
  const LineEntry *Entry =
    LineTable->FindNearestLineEntry(LocInfo.first, LocInfo.second);

  // If this is before the first line marker, use the file characteristic.
  if (!Entry)
    return FI.getFileCharacteristic();

  return Entry->FileKind;
}

/// Return the filename or buffer identifier of the buffer the location is in.
/// Note that this name does not respect \#line directives.  Use getPresumedLoc
/// for normal clients.
StringRef SourceManager::getBufferName(SourceLocation Loc,
                                       bool *Invalid) const {
  if (isInvalid(Loc, Invalid)) return "<invalid loc>";

  auto B = getBufferOrNone(getFileID(Loc));
  if (Invalid)
    *Invalid = !B;
  return B ? B->getBufferIdentifier() : "<invalid buffer>";
}

/// getPresumedLoc - This method returns the "presumed" location of a
/// SourceLocation specifies.  A "presumed location" can be modified by \#line
/// or GNU line marker directives.  This provides a view on the data that a
/// user should see in diagnostics, for example.
///
/// Note that a presumed location is always given as the expansion point of an
/// expansion location, not at the spelling location.
PresumedLoc SourceManager::getPresumedLoc(SourceLocation Loc,
                                          bool UseLineDirectives) const {
  if (Loc.isInvalid()) return PresumedLoc();

  // Presumed locations are always for expansion points.
  std::pair<FileID, unsigned> LocInfo = getDecomposedExpansionLoc(Loc);

  bool Invalid = false;
  const SLocEntry &Entry = getSLocEntry(LocInfo.first, &Invalid);
  if (Invalid || !Entry.isFile())
    return PresumedLoc();

  const SrcMgr::FileInfo &FI = Entry.getFile();
  const SrcMgr::ContentCache *C = &FI.getContentCache();

  // To get the source name, first consult the FileEntry (if one exists)
  // before the MemBuffer as this will avoid unnecessarily paging in the
  // MemBuffer.
  FileID FID = LocInfo.first;
  StringRef Filename;
  if (C->OrigEntry)
    Filename = C->OrigEntry->getName();
  else if (auto Buffer = C->getBufferOrNone(Diag, getFileManager()))
    Filename = Buffer->getBufferIdentifier();

  unsigned LineNo = getLineNumber(LocInfo.first, LocInfo.second, &Invalid);
  if (Invalid)
    return PresumedLoc();
  unsigned ColNo  = getColumnNumber(LocInfo.first, LocInfo.second, &Invalid);
  if (Invalid)
    return PresumedLoc();

  SourceLocation IncludeLoc = FI.getIncludeLoc();

  // If we have #line directives in this file, update and overwrite the physical
  // location info if appropriate.
  if (UseLineDirectives && FI.hasLineDirectives()) {
    assert(LineTable && "Can't have linetable entries without a LineTable!");
    // See if there is a #line directive before this.  If so, get it.
    if (const LineEntry *Entry =
          LineTable->FindNearestLineEntry(LocInfo.first, LocInfo.second)) {
      // If the LineEntry indicates a filename, use it.
      if (Entry->FilenameID != -1) {
        Filename = LineTable->getFilename(Entry->FilenameID);
        // The contents of files referenced by #line are not in the
        // SourceManager
        FID = FileID::get(0);
      }

      // Use the line number specified by the LineEntry.  This line number may
      // be multiple lines down from the line entry.  Add the difference in
      // physical line numbers from the query point and the line marker to the
      // total.
      unsigned MarkerLineNo = getLineNumber(LocInfo.first, Entry->FileOffset);
      LineNo = Entry->LineNo + (LineNo-MarkerLineNo-1);

      // Note that column numbers are not molested by line markers.

      // Handle virtual #include manipulation.
      if (Entry->IncludeOffset) {
        IncludeLoc = getLocForStartOfFile(LocInfo.first);
        IncludeLoc = IncludeLoc.getLocWithOffset(Entry->IncludeOffset);
      }
    }
  }

  return PresumedLoc(Filename.data(), FID, LineNo, ColNo, IncludeLoc);
}

/// Returns whether the PresumedLoc for a given SourceLocation is
/// in the main file.
///
/// This computes the "presumed" location for a SourceLocation, then checks
/// whether it came from a file other than the main file. This is different
/// from isWrittenInMainFile() because it takes line marker directives into
/// account.
bool SourceManager::isInMainFile(SourceLocation Loc) const {
  if (Loc.isInvalid()) return false;

  // Presumed locations are always for expansion points.
  std::pair<FileID, unsigned> LocInfo = getDecomposedExpansionLoc(Loc);

  const SLocEntry *Entry = getSLocEntryForFile(LocInfo.first);
  if (!Entry)
    return false;

  const SrcMgr::FileInfo &FI = Entry->getFile();

  // Check if there is a line directive for this location.
  if (FI.hasLineDirectives())
    if (const LineEntry *Entry =
            LineTable->FindNearestLineEntry(LocInfo.first, LocInfo.second))
      if (Entry->IncludeOffset)
        return false;

  return FI.getIncludeLoc().isInvalid();
}

/// The size of the SLocEntry that \p FID represents.
unsigned SourceManager::getFileIDSize(FileID FID) const {
  bool Invalid = false;
  const SrcMgr::SLocEntry &Entry = getSLocEntry(FID, &Invalid);
  if (Invalid)
    return 0;

  int ID = FID.ID;
  SourceLocation::UIntTy NextOffset;
  if ((ID > 0 && unsigned(ID+1) == local_sloc_entry_size()))
    NextOffset = getNextLocalOffset();
  else if (ID+1 == -1)
    NextOffset = MaxLoadedOffset;
  else
    NextOffset = getSLocEntry(FileID::get(ID+1)).getOffset();

  return NextOffset - Entry.getOffset() - 1;
}

//===----------------------------------------------------------------------===//
// Other miscellaneous methods.
//===----------------------------------------------------------------------===//

/// Get the source location for the given file:line:col triplet.
///
/// If the source file is included multiple times, the source location will
/// be based upon an arbitrary inclusion.
SourceLocation SourceManager::translateFileLineCol(const FileEntry *SourceFile,
                                                  unsigned Line,
                                                  unsigned Col) const {
  assert(SourceFile && "Null source file!");
  assert(Line && Col && "Line and column should start from 1!");

  FileID FirstFID = translateFile(SourceFile);
  return translateLineCol(FirstFID, Line, Col);
}

/// Get the FileID for the given file.
///
/// If the source file is included multiple times, the FileID will be the
/// first inclusion.
FileID SourceManager::translateFile(const FileEntry *SourceFile) const {
  assert(SourceFile && "Null source file!");

  // First, check the main file ID, since it is common to look for a
  // location in the main file.
  if (MainFileID.isValid()) {
    bool Invalid = false;
    const SLocEntry &MainSLoc = getSLocEntry(MainFileID, &Invalid);
    if (Invalid)
      return FileID();

    if (MainSLoc.isFile()) {
      if (MainSLoc.getFile().getContentCache().OrigEntry == SourceFile)
        return MainFileID;
    }
  }

  // The location we're looking for isn't in the main file; look
  // through all of the local source locations.
  for (unsigned I = 0, N = local_sloc_entry_size(); I != N; ++I) {
    const SLocEntry &SLoc = getLocalSLocEntry(I);
    if (SLoc.isFile() &&
        SLoc.getFile().getContentCache().OrigEntry == SourceFile)
      return FileID::get(I);
  }

  // If that still didn't help, try the modules.
  for (unsigned I = 0, N = loaded_sloc_entry_size(); I != N; ++I) {
    const SLocEntry &SLoc = getLoadedSLocEntry(I);
    if (SLoc.isFile() &&
        SLoc.getFile().getContentCache().OrigEntry == SourceFile)
      return FileID::get(-int(I) - 2);
  }

  return FileID();
}

/// Get the source location in \arg FID for the given line:col.
/// Returns null location if \arg FID is not a file SLocEntry.
SourceLocation SourceManager::translateLineCol(FileID FID,
                                               unsigned Line,
                                               unsigned Col) const {
  // Lines are used as a one-based index into a zero-based array. This assert
  // checks for possible buffer underruns.
  assert(Line && Col && "Line and column should start from 1!");

  if (FID.isInvalid())
    return SourceLocation();

  bool Invalid = false;
  const SLocEntry &Entry = getSLocEntry(FID, &Invalid);
  if (Invalid)
    return SourceLocation();

  if (!Entry.isFile())
    return SourceLocation();

  SourceLocation FileLoc = SourceLocation::getFileLoc(Entry.getOffset());

  if (Line == 1 && Col == 1)
    return FileLoc;

  const ContentCache *Content = &Entry.getFile().getContentCache();

  // If this is the first use of line information for this buffer, compute the
  // SourceLineCache for it on demand.
  std::optional<llvm::MemoryBufferRef> Buffer =
      Content->getBufferOrNone(Diag, getFileManager());
  if (!Buffer)
    return SourceLocation();
  if (!Content->SourceLineCache)
    Content->SourceLineCache =
        LineOffsetMapping::get(*Buffer, ContentCacheAlloc);

  if (Line > Content->SourceLineCache.size()) {
    unsigned Size = Buffer->getBufferSize();
    if (Size > 0)
      --Size;
    return FileLoc.getLocWithOffset(Size);
  }

  unsigned FilePos = Content->SourceLineCache[Line - 1];
  const char *Buf = Buffer->getBufferStart() + FilePos;
  unsigned BufLength = Buffer->getBufferSize() - FilePos;
  if (BufLength == 0)
    return FileLoc.getLocWithOffset(FilePos);

  unsigned i = 0;

  // Check that the given column is valid.
  while (i < BufLength-1 && i < Col-1 && Buf[i] != '\n' && Buf[i] != '\r')
    ++i;
  return FileLoc.getLocWithOffset(FilePos + i);
}

/// Compute a map of macro argument chunks to their expanded source
/// location. Chunks that are not part of a macro argument will map to an
/// invalid source location. e.g. if a file contains one macro argument at
/// offset 100 with length 10, this is how the map will be formed:
///     0   -> SourceLocation()
///     100 -> Expanded macro arg location
///     110 -> SourceLocation()
void SourceManager::computeMacroArgsCache(MacroArgsMap &MacroArgsCache,
                                          FileID FID) const {
  assert(FID.isValid());

  // Initially no macro argument chunk is present.
  MacroArgsCache.insert(std::make_pair(0, SourceLocation()));

  int ID = FID.ID;
  while (true) {
    ++ID;
    // Stop if there are no more FileIDs to check.
    if (ID > 0) {
      if (unsigned(ID) >= local_sloc_entry_size())
        return;
    } else if (ID == -1) {
      return;
    }

    bool Invalid = false;
    const SrcMgr::SLocEntry &Entry = getSLocEntryByID(ID, &Invalid);
    if (Invalid)
      return;
    if (Entry.isFile()) {
      auto& File = Entry.getFile();
      if (File.getFileCharacteristic() == C_User_ModuleMap ||
          File.getFileCharacteristic() == C_System_ModuleMap)
        continue;

      SourceLocation IncludeLoc = File.getIncludeLoc();
      bool IncludedInFID =
          (IncludeLoc.isValid() && isInFileID(IncludeLoc, FID)) ||
          // Predefined header doesn't have a valid include location in main
          // file, but any files created by it should still be skipped when
          // computing macro args expanded in the main file.
          (FID == MainFileID && Entry.getFile().getName() == "<built-in>");
      if (IncludedInFID) {
        // Skip the files/macros of the #include'd file, we only care about
        // macros that lexed macro arguments from our file.
        if (Entry.getFile().NumCreatedFIDs)
          ID += Entry.getFile().NumCreatedFIDs - 1 /*because of next ++ID*/;
        continue;
      }
      // If file was included but not from FID, there is no more files/macros
      // that may be "contained" in this file.
      if (IncludeLoc.isValid())
        return;
      continue;
    }

    const ExpansionInfo &ExpInfo = Entry.getExpansion();

    if (ExpInfo.getExpansionLocStart().isFileID()) {
      if (!isInFileID(ExpInfo.getExpansionLocStart(), FID))
        return; // No more files/macros that may be "contained" in this file.
    }

    if (!ExpInfo.isMacroArgExpansion())
      continue;

    associateFileChunkWithMacroArgExp(MacroArgsCache, FID,
                                 ExpInfo.getSpellingLoc(),
                                 SourceLocation::getMacroLoc(Entry.getOffset()),
                                 getFileIDSize(FileID::get(ID)));
  }
}

void SourceManager::associateFileChunkWithMacroArgExp(
                                         MacroArgsMap &MacroArgsCache,
                                         FileID FID,
                                         SourceLocation SpellLoc,
                                         SourceLocation ExpansionLoc,
                                         unsigned ExpansionLength) const {
  if (!SpellLoc.isFileID()) {
    SourceLocation::UIntTy SpellBeginOffs = SpellLoc.getOffset();
    SourceLocation::UIntTy SpellEndOffs = SpellBeginOffs + ExpansionLength;

    // The spelling range for this macro argument expansion can span multiple
    // consecutive FileID entries. Go through each entry contained in the
    // spelling range and if one is itself a macro argument expansion, recurse
    // and associate the file chunk that it represents.

    FileID SpellFID; // Current FileID in the spelling range.
    unsigned SpellRelativeOffs;
    std::tie(SpellFID, SpellRelativeOffs) = getDecomposedLoc(SpellLoc);
    while (true) {
      const SLocEntry &Entry = getSLocEntry(SpellFID);
      SourceLocation::UIntTy SpellFIDBeginOffs = Entry.getOffset();
      unsigned SpellFIDSize = getFileIDSize(SpellFID);
      SourceLocation::UIntTy SpellFIDEndOffs = SpellFIDBeginOffs + SpellFIDSize;
      const ExpansionInfo &Info = Entry.getExpansion();
      if (Info.isMacroArgExpansion()) {
        unsigned CurrSpellLength;
        if (SpellFIDEndOffs < SpellEndOffs)
          CurrSpellLength = SpellFIDSize - SpellRelativeOffs;
        else
          CurrSpellLength = ExpansionLength;
        associateFileChunkWithMacroArgExp(MacroArgsCache, FID,
                      Info.getSpellingLoc().getLocWithOffset(SpellRelativeOffs),
                      ExpansionLoc, CurrSpellLength);
      }

      if (SpellFIDEndOffs >= SpellEndOffs)
        return; // we covered all FileID entries in the spelling range.

      // Move to the next FileID entry in the spelling range.
      unsigned advance = SpellFIDSize - SpellRelativeOffs + 1;
      ExpansionLoc = ExpansionLoc.getLocWithOffset(advance);
      ExpansionLength -= advance;
      ++SpellFID.ID;
      SpellRelativeOffs = 0;
    }
  }

  assert(SpellLoc.isFileID());

  unsigned BeginOffs;
  if (!isInFileID(SpellLoc, FID, &BeginOffs))
    return;

  unsigned EndOffs = BeginOffs + ExpansionLength;

  // Add a new chunk for this macro argument. A previous macro argument chunk
  // may have been lexed again, so e.g. if the map is
  //     0   -> SourceLocation()
  //     100 -> Expanded loc #1
  //     110 -> SourceLocation()
  // and we found a new macro FileID that lexed from offset 105 with length 3,
  // the new map will be:
  //     0   -> SourceLocation()
  //     100 -> Expanded loc #1
  //     105 -> Expanded loc #2
  //     108 -> Expanded loc #1
  //     110 -> SourceLocation()
  //
  // Since re-lexed macro chunks will always be the same size or less of
  // previous chunks, we only need to find where the ending of the new macro
  // chunk is mapped to and update the map with new begin/end mappings.

  MacroArgsMap::iterator I = MacroArgsCache.upper_bound(EndOffs);
  --I;
  SourceLocation EndOffsMappedLoc = I->second;
  MacroArgsCache[BeginOffs] = ExpansionLoc;
  MacroArgsCache[EndOffs] = EndOffsMappedLoc;
}

void SourceManager::updateSlocUsageStats() const {
  SourceLocation::UIntTy UsedBytes =
      NextLocalOffset + (MaxLoadedOffset - CurrentLoadedOffset);
  MaxUsedSLocBytes.updateMax(UsedBytes);
}

/// If \arg Loc points inside a function macro argument, the returned
/// location will be the macro location in which the argument was expanded.
/// If a macro argument is used multiple times, the expanded location will
/// be at the first expansion of the argument.
/// e.g.
///   MY_MACRO(foo);
///             ^
/// Passing a file location pointing at 'foo', will yield a macro location
/// where 'foo' was expanded into.
SourceLocation
SourceManager::getMacroArgExpandedLocation(SourceLocation Loc) const {
  if (Loc.isInvalid() || !Loc.isFileID())
    return Loc;

  FileID FID;
  unsigned Offset;
  std::tie(FID, Offset) = getDecomposedLoc(Loc);
  if (FID.isInvalid())
    return Loc;

  std::unique_ptr<MacroArgsMap> &MacroArgsCache = MacroArgsCacheMap[FID];
  if (!MacroArgsCache) {
    MacroArgsCache = std::make_unique<MacroArgsMap>();
    computeMacroArgsCache(*MacroArgsCache, FID);
  }

  assert(!MacroArgsCache->empty());
  MacroArgsMap::iterator I = MacroArgsCache->upper_bound(Offset);
  // In case every element in MacroArgsCache is greater than Offset we can't
  // decrement the iterator.
  if (I == MacroArgsCache->begin())
    return Loc;

  --I;

  SourceLocation::UIntTy MacroArgBeginOffs = I->first;
  SourceLocation MacroArgExpandedLoc = I->second;
  if (MacroArgExpandedLoc.isValid())
    return MacroArgExpandedLoc.getLocWithOffset(Offset - MacroArgBeginOffs);

  return Loc;
}

std::pair<FileID, unsigned>
SourceManager::getDecomposedIncludedLoc(FileID FID) const {
  if (FID.isInvalid())
    return std::make_pair(FileID(), 0);

  // Uses IncludedLocMap to retrieve/cache the decomposed loc.

  using DecompTy = std::pair<FileID, unsigned>;
  auto InsertOp = IncludedLocMap.try_emplace(FID);
  DecompTy &DecompLoc = InsertOp.first->second;
  if (!InsertOp.second)
    return DecompLoc; // already in map.

  SourceLocation UpperLoc;
  bool Invalid = false;
  const SrcMgr::SLocEntry &Entry = getSLocEntry(FID, &Invalid);
  if (!Invalid) {
    if (Entry.isExpansion())
      UpperLoc = Entry.getExpansion().getExpansionLocStart();
    else
      UpperLoc = Entry.getFile().getIncludeLoc();
  }

  if (UpperLoc.isValid())
    DecompLoc = getDecomposedLoc(UpperLoc);

  return DecompLoc;
}

FileID SourceManager::getUniqueLoadedASTFileID(SourceLocation Loc) const {
  assert(isLoadedSourceLocation(Loc) &&
         "Must be a source location in a loaded PCH/Module file");

  auto [FID, Ignore] = getDecomposedLoc(Loc);
  // `LoadedSLocEntryAllocBegin` stores the sorted lowest FID of each loaded
  // allocation. Later allocations have lower FileIDs. The call below is to find
  // the lowest FID of a loaded allocation from any FID in the same allocation.
  // The lowest FID is used to identify a loaded allocation.
  const FileID *FirstFID =
      llvm::lower_bound(LoadedSLocEntryAllocBegin, FID, std::greater<FileID>{});

  assert(FirstFID &&
         "The failure to find the first FileID of a "
         "loaded AST from a loaded source location was unexpected.");
  return *FirstFID;
}

bool SourceManager::isInTheSameTranslationUnitImpl(
    const std::pair<FileID, unsigned> &LOffs,
    const std::pair<FileID, unsigned> &ROffs) const {
  // If one is local while the other is loaded.
  if (isLoadedFileID(LOffs.first) != isLoadedFileID(ROffs.first))
    return false;

  if (isLoadedFileID(LOffs.first) && isLoadedFileID(ROffs.first)) {
    auto FindSLocEntryAlloc = [this](FileID FID) {
      // Loaded FileIDs are negative, we store the lowest FileID from each
      // allocation, later allocations have lower FileIDs.
      return llvm::lower_bound(LoadedSLocEntryAllocBegin, FID,
                               std::greater<FileID>{});
    };

    // If both are loaded from different AST files.
    if (FindSLocEntryAlloc(LOffs.first) != FindSLocEntryAlloc(ROffs.first))
      return false;
  }

  return true;
}

/// Given a decomposed source location, move it up the include/expansion stack
/// to the parent source location within the same translation unit.  If this is
/// possible, return the decomposed version of the parent in Loc and return
/// false.  If Loc is a top-level entry, return true and don't modify it.
static bool
MoveUpTranslationUnitIncludeHierarchy(std::pair<FileID, unsigned> &Loc,
                                      const SourceManager &SM) {
  std::pair<FileID, unsigned> UpperLoc = SM.getDecomposedIncludedLoc(Loc.first);
  if (UpperLoc.first.isInvalid() ||
      !SM.isInTheSameTranslationUnitImpl(UpperLoc, Loc))
    return true; // We reached the top.

  Loc = UpperLoc;
  return false;
}

/// Return the cache entry for comparing the given file IDs
/// for isBeforeInTranslationUnit.
InBeforeInTUCacheEntry &SourceManager::getInBeforeInTUCache(FileID LFID,
                                                            FileID RFID) const {
  // This is a magic number for limiting the cache size.  It was experimentally
  // derived from a small Objective-C project (where the cache filled
  // out to ~250 items).  We can make it larger if necessary.
  // FIXME: this is almost certainly full these days. Use an LRU cache?
  enum { MagicCacheSize = 300 };
  IsBeforeInTUCacheKey Key(LFID, RFID);

  // If the cache size isn't too large, do a lookup and if necessary default
  // construct an entry.  We can then return it to the caller for direct
  // use.  When they update the value, the cache will get automatically
  // updated as well.
  if (IBTUCache.size() < MagicCacheSize)
    return IBTUCache.try_emplace(Key, LFID, RFID).first->second;

  // Otherwise, do a lookup that will not construct a new value.
  InBeforeInTUCache::iterator I = IBTUCache.find(Key);
  if (I != IBTUCache.end())
    return I->second;

  // Fall back to the overflow value.
  IBTUCacheOverflow.setQueryFIDs(LFID, RFID);
  return IBTUCacheOverflow;
}

/// Determines the order of 2 source locations in the translation unit.
///
/// \returns true if LHS source location comes before RHS, false otherwise.
bool SourceManager::isBeforeInTranslationUnit(SourceLocation LHS,
                                              SourceLocation RHS) const {
  assert(LHS.isValid() && RHS.isValid() && "Passed invalid source location!");
  if (LHS == RHS)
    return false;

  std::pair<FileID, unsigned> LOffs = getDecomposedLoc(LHS);
  std::pair<FileID, unsigned> ROffs = getDecomposedLoc(RHS);

  // getDecomposedLoc may have failed to return a valid FileID because, e.g. it
  // is a serialized one referring to a file that was removed after we loaded
  // the PCH.
  if (LOffs.first.isInvalid() || ROffs.first.isInvalid())
    return LOffs.first.isInvalid() && !ROffs.first.isInvalid();

  std::pair<bool, bool> InSameTU = isInTheSameTranslationUnit(LOffs, ROffs);
  if (InSameTU.first)
    return InSameTU.second;
  // TODO: This should be unreachable, but some clients are calling this
  //       function before making sure LHS and RHS are in the same TU.
  return LOffs.first < ROffs.first;
}

std::pair<bool, bool> SourceManager::isInTheSameTranslationUnit(
    std::pair<FileID, unsigned> &LOffs,
    std::pair<FileID, unsigned> &ROffs) const {
  // If the source locations are not in the same TU, return early.
  if (!isInTheSameTranslationUnitImpl(LOffs, ROffs))
    return std::make_pair(false, false);

  // If the source locations are in the same file, just compare offsets.
  if (LOffs.first == ROffs.first)
    return std::make_pair(true, LOffs.second < ROffs.second);

  // If we are comparing a source location with multiple locations in the same
  // file, we get a big win by caching the result.
  InBeforeInTUCacheEntry &IsBeforeInTUCache =
    getInBeforeInTUCache(LOffs.first, ROffs.first);

  // If we are comparing a source location with multiple locations in the same
  // file, we get a big win by caching the result.
  if (IsBeforeInTUCache.isCacheValid())
    return std::make_pair(
        true, IsBeforeInTUCache.getCachedResult(LOffs.second, ROffs.second));

  // Okay, we missed in the cache, we'll compute the answer and populate it.
  // We need to find the common ancestor. The only way of doing this is to
  // build the complete include chain for one and then walking up the chain
  // of the other looking for a match.

  // A location within a FileID on the path up from LOffs to the main file.
  struct Entry {
    std::pair<FileID, unsigned> DecomposedLoc; // FileID redundant, but clearer.
    FileID ChildFID; // Used for breaking ties. Invalid for the initial loc.
  };
  llvm::SmallDenseMap<FileID, Entry, 16> LChain;

  FileID LChild;
  do {
    LChain.try_emplace(LOffs.first, Entry{LOffs, LChild});
    // We catch the case where LOffs is in a file included by ROffs and
    // quit early. The other way round unfortunately remains suboptimal.
    if (LOffs.first == ROffs.first)
      break;
    LChild = LOffs.first;
  } while (!MoveUpTranslationUnitIncludeHierarchy(LOffs, *this));

  FileID RChild;
  do {
    auto LIt = LChain.find(ROffs.first);
    if (LIt != LChain.end()) {
      // Compare the locations within the common file and cache them.
      LOffs = LIt->second.DecomposedLoc;
      LChild = LIt->second.ChildFID;
      // The relative order of LChild and RChild is a tiebreaker when
      // - locs expand to the same location (occurs in macro arg expansion)
      // - one loc is a parent of the other (we consider the parent as "first")
      // For the parent entry to be first, its invalid child file ID must
      // compare smaller to the valid child file ID of the other entry.
      // However loaded FileIDs are <0, so we perform *unsigned* comparison!
      // This changes the relative order of local vs loaded FileIDs, but it
      // doesn't matter as these are never mixed in macro expansion.
      unsigned LChildID = LChild.ID;
      unsigned RChildID = RChild.ID;
      assert(((LOffs.second != ROffs.second) ||
              (LChildID == 0 || RChildID == 0) ||
              isInSameSLocAddrSpace(getComposedLoc(LChild, 0),
                                    getComposedLoc(RChild, 0), nullptr)) &&
             "Mixed local/loaded FileIDs with same include location?");
      IsBeforeInTUCache.setCommonLoc(LOffs.first, LOffs.second, ROffs.second,
                                     LChildID < RChildID);
      return std::make_pair(
          true, IsBeforeInTUCache.getCachedResult(LOffs.second, ROffs.second));
    }
    RChild = ROffs.first;
  } while (!MoveUpTranslationUnitIncludeHierarchy(ROffs, *this));

  // If we found no match, the location is either in a built-ins buffer or
  // associated with global inline asm. PR5662 and PR22576 are examples.

  StringRef LB = getBufferOrFake(LOffs.first).getBufferIdentifier();
  StringRef RB = getBufferOrFake(ROffs.first).getBufferIdentifier();

  bool LIsBuiltins = LB == "<built-in>";
  bool RIsBuiltins = RB == "<built-in>";
  // Sort built-in before non-built-in.
  if (LIsBuiltins || RIsBuiltins) {
    if (LIsBuiltins != RIsBuiltins)
      return std::make_pair(true, LIsBuiltins);
    // Both are in built-in buffers, but from different files. We just claim
    // that lower IDs come first.
    return std::make_pair(true, LOffs.first < ROffs.first);
  }

  bool LIsAsm = LB == "<inline asm>";
  bool RIsAsm = RB == "<inline asm>";
  // Sort assembler after built-ins, but before the rest.
  if (LIsAsm || RIsAsm) {
    if (LIsAsm != RIsAsm)
      return std::make_pair(true, RIsAsm);
    assert(LOffs.first == ROffs.first);
    return std::make_pair(true, false);
  }

  bool LIsScratch = LB == "<scratch space>";
  bool RIsScratch = RB == "<scratch space>";
  // Sort scratch after inline asm, but before the rest.
  if (LIsScratch || RIsScratch) {
    if (LIsScratch != RIsScratch)
      return std::make_pair(true, LIsScratch);
    return std::make_pair(true, LOffs.second < ROffs.second);
  }

  llvm_unreachable("Unsortable locations found");
}

void SourceManager::PrintStats() const {
  llvm::errs() << "\n*** Source Manager Stats:\n";
  llvm::errs() << FileInfos.size() << " files mapped, " << MemBufferInfos.size()
               << " mem buffers mapped.\n";
  llvm::errs() << LocalSLocEntryTable.size() << " local SLocEntries allocated ("
               << llvm::capacity_in_bytes(LocalSLocEntryTable)
               << " bytes of capacity), " << NextLocalOffset
               << "B of SLoc address space used.\n";
  llvm::errs() << LoadedSLocEntryTable.size()
               << " loaded SLocEntries allocated ("
               << llvm::capacity_in_bytes(LoadedSLocEntryTable)
               << " bytes of capacity), "
               << MaxLoadedOffset - CurrentLoadedOffset
               << "B of SLoc address space used.\n";

  unsigned NumLineNumsComputed = 0;
  unsigned NumFileBytesMapped = 0;
  for (fileinfo_iterator I = fileinfo_begin(), E = fileinfo_end(); I != E; ++I){
    NumLineNumsComputed += bool(I->second->SourceLineCache);
    NumFileBytesMapped  += I->second->getSizeBytesMapped();
  }
  unsigned NumMacroArgsComputed = MacroArgsCacheMap.size();

  llvm::errs() << NumFileBytesMapped << " bytes of files mapped, "
               << NumLineNumsComputed << " files with line #'s computed, "
               << NumMacroArgsComputed << " files with macro args computed.\n";
  llvm::errs() << "FileID scans: " << NumLinearScans << " linear, "
               << NumBinaryProbes << " binary.\n";
}

LLVM_DUMP_METHOD void SourceManager::dump() const {
  llvm::raw_ostream &out = llvm::errs();

  auto DumpSLocEntry = [&](int ID, const SrcMgr::SLocEntry &Entry,
                           std::optional<SourceLocation::UIntTy> NextStart) {
    out << "SLocEntry <FileID " << ID << "> " << (Entry.isFile() ? "file" : "expansion")
        << " <SourceLocation " << Entry.getOffset() << ":";
    if (NextStart)
      out << *NextStart << ">\n";
    else
      out << "???\?>\n";
    if (Entry.isFile()) {
      auto &FI = Entry.getFile();
      if (FI.NumCreatedFIDs)
        out << "  covers <FileID " << ID << ":" << int(ID + FI.NumCreatedFIDs)
            << ">\n";
      if (FI.getIncludeLoc().isValid())
        out << "  included from " << FI.getIncludeLoc().getOffset() << "\n";
      auto &CC = FI.getContentCache();
      out << "  for " << (CC.OrigEntry ? CC.OrigEntry->getName() : "<none>")
          << "\n";
      if (CC.BufferOverridden)
        out << "  contents overridden\n";
      if (CC.ContentsEntry != CC.OrigEntry) {
        out << "  contents from "
            << (CC.ContentsEntry ? CC.ContentsEntry->getName() : "<none>")
            << "\n";
      }
    } else {
      auto &EI = Entry.getExpansion();
      out << "  spelling from " << EI.getSpellingLoc().getOffset() << "\n";
      out << "  macro " << (EI.isMacroArgExpansion() ? "arg" : "body")
          << " range <" << EI.getExpansionLocStart().getOffset() << ":"
          << EI.getExpansionLocEnd().getOffset() << ">\n";
    }
  };

  // Dump local SLocEntries.
  for (unsigned ID = 0, NumIDs = LocalSLocEntryTable.size(); ID != NumIDs; ++ID) {
    DumpSLocEntry(ID, LocalSLocEntryTable[ID],
                  ID == NumIDs - 1 ? NextLocalOffset
                                   : LocalSLocEntryTable[ID + 1].getOffset());
  }
  // Dump loaded SLocEntries.
  std::optional<SourceLocation::UIntTy> NextStart;
  for (unsigned Index = 0; Index != LoadedSLocEntryTable.size(); ++Index) {
    int ID = -(int)Index - 2;
    if (SLocEntryLoaded[Index]) {
      DumpSLocEntry(ID, LoadedSLocEntryTable[Index], NextStart);
      NextStart = LoadedSLocEntryTable[Index].getOffset();
    } else {
      NextStart = std::nullopt;
    }
  }
}

void SourceManager::noteSLocAddressSpaceUsage(
    DiagnosticsEngine &Diag, std::optional<unsigned> MaxNotes) const {
  struct Info {
    // A location where this file was entered.
    SourceLocation Loc;
    // Number of times this FileEntry was entered.
    unsigned Inclusions = 0;
    // Size usage from the file itself.
    uint64_t DirectSize = 0;
    // Total size usage from the file and its macro expansions.
    uint64_t TotalSize = 0;
  };
  using UsageMap = llvm::MapVector<const FileEntry*, Info>;

  UsageMap Usage;
  uint64_t CountedSize = 0;

  auto AddUsageForFileID = [&](FileID ID) {
    // The +1 here is because getFileIDSize doesn't include the extra byte for
    // the one-past-the-end location.
    unsigned Size = getFileIDSize(ID) + 1;

    // Find the file that used this address space, either directly or by
    // macro expansion.
    SourceLocation FileStart = getFileLoc(getComposedLoc(ID, 0));
    FileID FileLocID = getFileID(FileStart);
    const FileEntry *Entry = getFileEntryForID(FileLocID);

    Info &EntryInfo = Usage[Entry];
    if (EntryInfo.Loc.isInvalid())
      EntryInfo.Loc = FileStart;
    if (ID == FileLocID) {
      ++EntryInfo.Inclusions;
      EntryInfo.DirectSize += Size;
    }
    EntryInfo.TotalSize += Size;
    CountedSize += Size;
  };

  // Loaded SLocEntries have indexes counting downwards from -2.
  for (size_t Index = 0; Index != LoadedSLocEntryTable.size(); ++Index) {
    AddUsageForFileID(FileID::get(-2 - Index));
  }
  // Local SLocEntries have indexes counting upwards from 0.
  for (size_t Index = 0; Index != LocalSLocEntryTable.size(); ++Index) {
    AddUsageForFileID(FileID::get(Index));
  }

  // Sort the usage by size from largest to smallest. Break ties by raw source
  // location.
  auto SortedUsage = Usage.takeVector();
  auto Cmp = [](const UsageMap::value_type &A, const UsageMap::value_type &B) {
    return A.second.TotalSize > B.second.TotalSize ||
           (A.second.TotalSize == B.second.TotalSize &&
            A.second.Loc < B.second.Loc);
  };
  auto SortedEnd = SortedUsage.end();
  if (MaxNotes && SortedUsage.size() > *MaxNotes) {
    SortedEnd = SortedUsage.begin() + *MaxNotes;
    std::nth_element(SortedUsage.begin(), SortedEnd, SortedUsage.end(), Cmp);
  }
  std::sort(SortedUsage.begin(), SortedEnd, Cmp);

  // Produce note on sloc address space usage total.
  uint64_t LocalUsage = NextLocalOffset;
  uint64_t LoadedUsage = MaxLoadedOffset - CurrentLoadedOffset;
  int UsagePercent = static_cast<int>(100.0 * double(LocalUsage + LoadedUsage) /
                                      MaxLoadedOffset);
  Diag.Report(SourceLocation(), diag::note_total_sloc_usage)
    << LocalUsage << LoadedUsage << (LocalUsage + LoadedUsage) << UsagePercent;

  // Produce notes on sloc address space usage for each file with a high usage.
  uint64_t ReportedSize = 0;
  for (auto &[Entry, FileInfo] :
       llvm::make_range(SortedUsage.begin(), SortedEnd)) {
    Diag.Report(FileInfo.Loc, diag::note_file_sloc_usage)
        << FileInfo.Inclusions << FileInfo.DirectSize
        << (FileInfo.TotalSize - FileInfo.DirectSize);
    ReportedSize += FileInfo.TotalSize;
  }

  // Describe any remaining usage not reported in the per-file usage.
  if (ReportedSize != CountedSize) {
    Diag.Report(SourceLocation(), diag::note_file_misc_sloc_usage)
        << (SortedUsage.end() - SortedEnd) << CountedSize - ReportedSize;
  }
}

ExternalSLocEntrySource::~ExternalSLocEntrySource() = default;

/// Return the amount of memory used by memory buffers, breaking down
/// by heap-backed versus mmap'ed memory.
SourceManager::MemoryBufferSizes SourceManager::getMemoryBufferSizes() const {
  size_t malloc_bytes = 0;
  size_t mmap_bytes = 0;

  for (unsigned i = 0, e = MemBufferInfos.size(); i != e; ++i)
    if (size_t sized_mapped = MemBufferInfos[i]->getSizeBytesMapped())
      switch (MemBufferInfos[i]->getMemoryBufferKind()) {
        case llvm::MemoryBuffer::MemoryBuffer_MMap:
          mmap_bytes += sized_mapped;
          break;
        case llvm::MemoryBuffer::MemoryBuffer_Malloc:
          malloc_bytes += sized_mapped;
          break;
      }

  return MemoryBufferSizes(malloc_bytes, mmap_bytes);
}

size_t SourceManager::getDataStructureSizes() const {
  size_t size = llvm::capacity_in_bytes(MemBufferInfos) +
                llvm::capacity_in_bytes(LocalSLocEntryTable) +
                llvm::capacity_in_bytes(LoadedSLocEntryTable) +
                llvm::capacity_in_bytes(SLocEntryLoaded) +
                llvm::capacity_in_bytes(FileInfos);

  if (OverriddenFilesInfo)
    size += llvm::capacity_in_bytes(OverriddenFilesInfo->OverriddenFiles);

  return size;
}

SourceManagerForFile::SourceManagerForFile(StringRef FileName,
                                           StringRef Content) {
  // This is referenced by `FileMgr` and will be released by `FileMgr` when it
  // is deleted.
  IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> InMemoryFileSystem(
      new llvm::vfs::InMemoryFileSystem);
  InMemoryFileSystem->addFile(
      FileName, 0,
      llvm::MemoryBuffer::getMemBuffer(Content, FileName,
                                       /*RequiresNullTerminator=*/false));
  // This is passed to `SM` as reference, so the pointer has to be referenced
  // in `Environment` so that `FileMgr` can out-live this function scope.
  FileMgr =
      std::make_unique<FileManager>(FileSystemOptions(), InMemoryFileSystem);
  // This is passed to `SM` as reference, so the pointer has to be referenced
  // by `Environment` due to the same reason above.
  Diagnostics = std::make_unique<DiagnosticsEngine>(
      IntrusiveRefCntPtr<DiagnosticIDs>(new DiagnosticIDs),
      new DiagnosticOptions);
  SourceMgr = std::make_unique<SourceManager>(*Diagnostics, *FileMgr);
  FileEntryRef FE = llvm::cantFail(FileMgr->getFileRef(FileName));
  FileID ID =
      SourceMgr->createFileID(FE, SourceLocation(), clang::SrcMgr::C_User);
  assert(ID.isValid());
  SourceMgr->setMainFileID(ID);
}

//===--- GlobalModuleIndex.cpp - Global Module Index ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the GlobalModuleIndex class.
//
//===----------------------------------------------------------------------===//

#include "clang/Serialization/GlobalModuleIndex.h"
#include "ASTReaderInternals.h"
#include "clang/Basic/FileManager.h"
#include "clang/Serialization/ASTBitCodes.h"
#include "clang/Serialization/ModuleFile.h"
#include "clang/Serialization/PCHContainerOperations.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Bitstream/BitstreamReader.h"
#include "llvm/Bitstream/BitstreamWriter.h"
#include "llvm/Support/DJB.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/LockFileManager.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/OnDiskHashTable.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdio>
using namespace clang;
using namespace serialization;

//----------------------------------------------------------------------------//
// Shared constants
//----------------------------------------------------------------------------//
namespace {
  enum {
    /// The block containing the index.
    GLOBAL_INDEX_BLOCK_ID = llvm::bitc::FIRST_APPLICATION_BLOCKID
  };

  /// Describes the record types in the index.
  enum IndexRecordTypes {
    /// Contains version information and potentially other metadata,
    /// used to determine if we can read this global index file.
    INDEX_METADATA,
    /// Describes a module, including its file name and dependencies.
    MODULE,
    /// The index for identifiers.
    IDENTIFIER_INDEX
  };
}

/// The name of the global index file.
static const char * const IndexFileName = "modules.idx";

/// The global index file version.
static const unsigned CurrentVersion = 1;

//----------------------------------------------------------------------------//
// Global module index reader.
//----------------------------------------------------------------------------//

namespace {

/// Trait used to read the identifier index from the on-disk hash
/// table.
class IdentifierIndexReaderTrait {
public:
  typedef StringRef external_key_type;
  typedef StringRef internal_key_type;
  typedef SmallVector<unsigned, 2> data_type;
  typedef unsigned hash_value_type;
  typedef unsigned offset_type;

  static bool EqualKey(const internal_key_type& a, const internal_key_type& b) {
    return a == b;
  }

  static hash_value_type ComputeHash(const internal_key_type& a) {
    return llvm::djbHash(a);
  }

  static std::pair<unsigned, unsigned>
  ReadKeyDataLength(const unsigned char*& d) {
    using namespace llvm::support;
    unsigned KeyLen = endian::readNext<uint16_t, llvm::endianness::little>(d);
    unsigned DataLen = endian::readNext<uint16_t, llvm::endianness::little>(d);
    return std::make_pair(KeyLen, DataLen);
  }

  static const internal_key_type&
  GetInternalKey(const external_key_type& x) { return x; }

  static const external_key_type&
  GetExternalKey(const internal_key_type& x) { return x; }

  static internal_key_type ReadKey(const unsigned char* d, unsigned n) {
    return StringRef((const char *)d, n);
  }

  static data_type ReadData(const internal_key_type& k,
                            const unsigned char* d,
                            unsigned DataLen) {
    using namespace llvm::support;

    data_type Result;
    while (DataLen > 0) {
      unsigned ID = endian::readNext<uint32_t, llvm::endianness::little>(d);
      Result.push_back(ID);
      DataLen -= 4;
    }

    return Result;
  }
};

typedef llvm::OnDiskIterableChainedHashTable<IdentifierIndexReaderTrait>
    IdentifierIndexTable;

}

GlobalModuleIndex::GlobalModuleIndex(
    std::unique_ptr<llvm::MemoryBuffer> IndexBuffer,
    llvm::BitstreamCursor Cursor)
    : Buffer(std::move(IndexBuffer)), IdentifierIndex(), NumIdentifierLookups(),
      NumIdentifierLookupHits() {
  auto Fail = [&](llvm::Error &&Err) {
    report_fatal_error("Module index '" + Buffer->getBufferIdentifier() +
                       "' failed: " + toString(std::move(Err)));
  };

  llvm::TimeTraceScope TimeScope("Module LoadIndex");
  // Read the global index.
  bool InGlobalIndexBlock = false;
  bool Done = false;
  while (!Done) {
    llvm::BitstreamEntry Entry;
    if (Expected<llvm::BitstreamEntry> Res = Cursor.advance())
      Entry = Res.get();
    else
      Fail(Res.takeError());

    switch (Entry.Kind) {
    case llvm::BitstreamEntry::Error:
      return;

    case llvm::BitstreamEntry::EndBlock:
      if (InGlobalIndexBlock) {
        InGlobalIndexBlock = false;
        Done = true;
        continue;
      }
      return;


    case llvm::BitstreamEntry::Record:
      // Entries in the global index block are handled below.
      if (InGlobalIndexBlock)
        break;

      return;

    case llvm::BitstreamEntry::SubBlock:
      if (!InGlobalIndexBlock && Entry.ID == GLOBAL_INDEX_BLOCK_ID) {
        if (llvm::Error Err = Cursor.EnterSubBlock(GLOBAL_INDEX_BLOCK_ID))
          Fail(std::move(Err));
        InGlobalIndexBlock = true;
      } else if (llvm::Error Err = Cursor.SkipBlock())
        Fail(std::move(Err));
      continue;
    }

    SmallVector<uint64_t, 64> Record;
    StringRef Blob;
    Expected<unsigned> MaybeIndexRecord =
        Cursor.readRecord(Entry.ID, Record, &Blob);
    if (!MaybeIndexRecord)
      Fail(MaybeIndexRecord.takeError());
    IndexRecordTypes IndexRecord =
        static_cast<IndexRecordTypes>(MaybeIndexRecord.get());
    switch (IndexRecord) {
    case INDEX_METADATA:
      // Make sure that the version matches.
      if (Record.size() < 1 || Record[0] != CurrentVersion)
        return;
      break;

    case MODULE: {
      unsigned Idx = 0;
      unsigned ID = Record[Idx++];

      // Make room for this module's information.
      if (ID == Modules.size())
        Modules.push_back(ModuleInfo());
      else
        Modules.resize(ID + 1);

      // Size/modification time for this module file at the time the
      // global index was built.
      Modules[ID].Size = Record[Idx++];
      Modules[ID].ModTime = Record[Idx++];

      // File name.
      unsigned NameLen = Record[Idx++];
      Modules[ID].FileName.assign(Record.begin() + Idx,
                                  Record.begin() + Idx + NameLen);
      Idx += NameLen;

      // Dependencies
      unsigned NumDeps = Record[Idx++];
      Modules[ID].Dependencies.insert(Modules[ID].Dependencies.end(),
                                      Record.begin() + Idx,
                                      Record.begin() + Idx + NumDeps);
      Idx += NumDeps;

      // Make sure we're at the end of the record.
      assert(Idx == Record.size() && "More module info?");

      // Record this module as an unresolved module.
      // FIXME: this doesn't work correctly for module names containing path
      // separators.
      StringRef ModuleName = llvm::sys::path::stem(Modules[ID].FileName);
      // Remove the -<hash of ModuleMapPath>
      ModuleName = ModuleName.rsplit('-').first;
      UnresolvedModules[ModuleName] = ID;
      break;
    }

    case IDENTIFIER_INDEX:
      // Wire up the identifier index.
      if (Record[0]) {
        IdentifierIndex = IdentifierIndexTable::Create(
            (const unsigned char *)Blob.data() + Record[0],
            (const unsigned char *)Blob.data() + sizeof(uint32_t),
            (const unsigned char *)Blob.data(), IdentifierIndexReaderTrait());
      }
      break;
    }
  }
}

GlobalModuleIndex::~GlobalModuleIndex() {
  delete static_cast<IdentifierIndexTable *>(IdentifierIndex);
}

std::pair<GlobalModuleIndex *, llvm::Error>
GlobalModuleIndex::readIndex(StringRef Path) {
  // Load the index file, if it's there.
  llvm::SmallString<128> IndexPath;
  IndexPath += Path;
  llvm::sys::path::append(IndexPath, IndexFileName);

  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> BufferOrErr =
      llvm::MemoryBuffer::getFile(IndexPath.c_str());
  if (!BufferOrErr)
    return std::make_pair(nullptr,
                          llvm::errorCodeToError(BufferOrErr.getError()));
  std::unique_ptr<llvm::MemoryBuffer> Buffer = std::move(BufferOrErr.get());

  /// The main bitstream cursor for the main block.
  llvm::BitstreamCursor Cursor(*Buffer);

  // Sniff for the signature.
  for (unsigned char C : {'B', 'C', 'G', 'I'}) {
    if (Expected<llvm::SimpleBitstreamCursor::word_t> Res = Cursor.Read(8)) {
      if (Res.get() != C)
        return std::make_pair(
            nullptr, llvm::createStringError(std::errc::illegal_byte_sequence,
                                             "expected signature BCGI"));
    } else
      return std::make_pair(nullptr, Res.takeError());
  }

  return std::make_pair(new GlobalModuleIndex(std::move(Buffer), std::move(Cursor)),
                        llvm::Error::success());
}

void GlobalModuleIndex::getModuleDependencies(
       ModuleFile *File,
       SmallVectorImpl<ModuleFile *> &Dependencies) {
  // Look for information about this module file.
  llvm::DenseMap<ModuleFile *, unsigned>::iterator Known
    = ModulesByFile.find(File);
  if (Known == ModulesByFile.end())
    return;

  // Record dependencies.
  Dependencies.clear();
  ArrayRef<unsigned> StoredDependencies = Modules[Known->second].Dependencies;
  for (unsigned I = 0, N = StoredDependencies.size(); I != N; ++I) {
    if (ModuleFile *MF = Modules[I].File)
      Dependencies.push_back(MF);
  }
}

bool GlobalModuleIndex::lookupIdentifier(StringRef Name, HitSet &Hits) {
  Hits.clear();

  // If there's no identifier index, there is nothing we can do.
  if (!IdentifierIndex)
    return false;

  // Look into the identifier index.
  ++NumIdentifierLookups;
  IdentifierIndexTable &Table
    = *static_cast<IdentifierIndexTable *>(IdentifierIndex);
  IdentifierIndexTable::iterator Known = Table.find(Name);
  if (Known == Table.end()) {
    return false;
  }

  SmallVector<unsigned, 2> ModuleIDs = *Known;
  for (unsigned I = 0, N = ModuleIDs.size(); I != N; ++I) {
    if (ModuleFile *MF = Modules[ModuleIDs[I]].File)
      Hits.insert(MF);
  }

  ++NumIdentifierLookupHits;
  return true;
}

bool GlobalModuleIndex::loadedModuleFile(ModuleFile *File) {
  // Look for the module in the global module index based on the module name.
  StringRef Name = File->ModuleName;
  llvm::StringMap<unsigned>::iterator Known = UnresolvedModules.find(Name);
  if (Known == UnresolvedModules.end()) {
    return true;
  }

  // Rectify this module with the global module index.
  ModuleInfo &Info = Modules[Known->second];

  //  If the size and modification time match what we expected, record this
  // module file.
  bool Failed = true;
  if (File->File.getSize() == Info.Size &&
      File->File.getModificationTime() == Info.ModTime) {
    Info.File = File;
    ModulesByFile[File] = Known->second;

    Failed = false;
  }

  // One way or another, we have resolved this module file.
  UnresolvedModules.erase(Known);
  return Failed;
}

void GlobalModuleIndex::printStats() {
  std::fprintf(stderr, "*** Global Module Index Statistics:\n");
  if (NumIdentifierLookups) {
    fprintf(stderr, "  %u / %u identifier lookups succeeded (%f%%)\n",
            NumIdentifierLookupHits, NumIdentifierLookups,
            (double)NumIdentifierLookupHits*100.0/NumIdentifierLookups);
  }
  std::fprintf(stderr, "\n");
}

LLVM_DUMP_METHOD void GlobalModuleIndex::dump() {
  llvm::errs() << "*** Global Module Index Dump:\n";
  llvm::errs() << "Module files:\n";
  for (auto &MI : Modules) {
    llvm::errs() << "** " << MI.FileName << "\n";
    if (MI.File)
      MI.File->dump();
    else
      llvm::errs() << "\n";
  }
  llvm::errs() << "\n";
}

//----------------------------------------------------------------------------//
// Global module index writer.
//----------------------------------------------------------------------------//

namespace {
  /// Provides information about a specific module file.
  struct ModuleFileInfo {
    /// The numberic ID for this module file.
    unsigned ID;

    /// The set of modules on which this module depends. Each entry is
    /// a module ID.
    SmallVector<unsigned, 4> Dependencies;
    ASTFileSignature Signature;
  };

  struct ImportedModuleFileInfo {
    off_t StoredSize;
    time_t StoredModTime;
    ASTFileSignature StoredSignature;
    ImportedModuleFileInfo(off_t Size, time_t ModTime, ASTFileSignature Sig)
        : StoredSize(Size), StoredModTime(ModTime), StoredSignature(Sig) {}
  };

  /// Builder that generates the global module index file.
  class GlobalModuleIndexBuilder {
    FileManager &FileMgr;
    const PCHContainerReader &PCHContainerRdr;

    /// Mapping from files to module file information.
    using ModuleFilesMap = llvm::MapVector<FileEntryRef, ModuleFileInfo>;

    /// Information about each of the known module files.
    ModuleFilesMap ModuleFiles;

    /// Mapping from the imported module file to the imported
    /// information.
    using ImportedModuleFilesMap =
        std::multimap<FileEntryRef, ImportedModuleFileInfo>;

    /// Information about each importing of a module file.
    ImportedModuleFilesMap ImportedModuleFiles;

    /// Mapping from identifiers to the list of module file IDs that
    /// consider this identifier to be interesting.
    typedef llvm::StringMap<SmallVector<unsigned, 2> > InterestingIdentifierMap;

    /// A mapping from all interesting identifiers to the set of module
    /// files in which those identifiers are considered interesting.
    InterestingIdentifierMap InterestingIdentifiers;

    /// Write the block-info block for the global module index file.
    void emitBlockInfoBlock(llvm::BitstreamWriter &Stream);

    /// Retrieve the module file information for the given file.
    ModuleFileInfo &getModuleFileInfo(FileEntryRef File) {
      auto Known = ModuleFiles.find(File);
      if (Known != ModuleFiles.end())
        return Known->second;

      unsigned NewID = ModuleFiles.size();
      ModuleFileInfo &Info = ModuleFiles[File];
      Info.ID = NewID;
      return Info;
    }

  public:
    explicit GlobalModuleIndexBuilder(
        FileManager &FileMgr, const PCHContainerReader &PCHContainerRdr)
        : FileMgr(FileMgr), PCHContainerRdr(PCHContainerRdr) {}

    /// Load the contents of the given module file into the builder.
    llvm::Error loadModuleFile(FileEntryRef File);

    /// Write the index to the given bitstream.
    /// \returns true if an error occurred, false otherwise.
    bool writeIndex(llvm::BitstreamWriter &Stream);
  };
}

static void emitBlockID(unsigned ID, const char *Name,
                        llvm::BitstreamWriter &Stream,
                        SmallVectorImpl<uint64_t> &Record) {
  Record.clear();
  Record.push_back(ID);
  Stream.EmitRecord(llvm::bitc::BLOCKINFO_CODE_SETBID, Record);

  // Emit the block name if present.
  if (!Name || Name[0] == 0) return;
  Record.clear();
  while (*Name)
    Record.push_back(*Name++);
  Stream.EmitRecord(llvm::bitc::BLOCKINFO_CODE_BLOCKNAME, Record);
}

static void emitRecordID(unsigned ID, const char *Name,
                         llvm::BitstreamWriter &Stream,
                         SmallVectorImpl<uint64_t> &Record) {
  Record.clear();
  Record.push_back(ID);
  while (*Name)
    Record.push_back(*Name++);
  Stream.EmitRecord(llvm::bitc::BLOCKINFO_CODE_SETRECORDNAME, Record);
}

void
GlobalModuleIndexBuilder::emitBlockInfoBlock(llvm::BitstreamWriter &Stream) {
  SmallVector<uint64_t, 64> Record;
  Stream.EnterBlockInfoBlock();

#define BLOCK(X) emitBlockID(X ## _ID, #X, Stream, Record)
#define RECORD(X) emitRecordID(X, #X, Stream, Record)
  BLOCK(GLOBAL_INDEX_BLOCK);
  RECORD(INDEX_METADATA);
  RECORD(MODULE);
  RECORD(IDENTIFIER_INDEX);
#undef RECORD
#undef BLOCK

  Stream.ExitBlock();
}

namespace {
  class InterestingASTIdentifierLookupTrait
    : public serialization::reader::ASTIdentifierLookupTraitBase {

  public:
    /// The identifier and whether it is "interesting".
    typedef std::pair<StringRef, bool> data_type;

    data_type ReadData(const internal_key_type& k,
                       const unsigned char* d,
                       unsigned DataLen) {
      // The first bit indicates whether this identifier is interesting.
      // That's all we care about.
      using namespace llvm::support;
      IdentifierID RawID =
          endian::readNext<IdentifierID, llvm::endianness::little>(d);
      bool IsInteresting = RawID & 0x01;
      return std::make_pair(k, IsInteresting);
    }
  };
}

llvm::Error GlobalModuleIndexBuilder::loadModuleFile(FileEntryRef File) {
  // Open the module file.

  auto Buffer = FileMgr.getBufferForFile(File, /*isVolatile=*/true);
  if (!Buffer)
    return llvm::createStringError(Buffer.getError(),
                                   "failed getting buffer for module file");

  // Initialize the input stream
  llvm::BitstreamCursor InStream(PCHContainerRdr.ExtractPCH(**Buffer));

  // Sniff for the signature.
  for (unsigned char C : {'C', 'P', 'C', 'H'})
    if (Expected<llvm::SimpleBitstreamCursor::word_t> Res = InStream.Read(8)) {
      if (Res.get() != C)
        return llvm::createStringError(std::errc::illegal_byte_sequence,
                                       "expected signature CPCH");
    } else
      return Res.takeError();

  // Record this module file and assign it a unique ID (if it doesn't have
  // one already).
  unsigned ID = getModuleFileInfo(File).ID;

  // Search for the blocks and records we care about.
  enum { Other, ControlBlock, ASTBlock, DiagnosticOptionsBlock } State = Other;
  bool Done = false;
  while (!Done) {
    Expected<llvm::BitstreamEntry> MaybeEntry = InStream.advance();
    if (!MaybeEntry)
      return MaybeEntry.takeError();
    llvm::BitstreamEntry Entry = MaybeEntry.get();

    switch (Entry.Kind) {
    case llvm::BitstreamEntry::Error:
      Done = true;
      continue;

    case llvm::BitstreamEntry::Record:
      // In the 'other' state, just skip the record. We don't care.
      if (State == Other) {
        if (llvm::Expected<unsigned> Skipped = InStream.skipRecord(Entry.ID))
          continue;
        else
          return Skipped.takeError();
      }

      // Handle potentially-interesting records below.
      break;

    case llvm::BitstreamEntry::SubBlock:
      if (Entry.ID == CONTROL_BLOCK_ID) {
        if (llvm::Error Err = InStream.EnterSubBlock(CONTROL_BLOCK_ID))
          return Err;

        // Found the control block.
        State = ControlBlock;
        continue;
      }

      if (Entry.ID == AST_BLOCK_ID) {
        if (llvm::Error Err = InStream.EnterSubBlock(AST_BLOCK_ID))
          return Err;

        // Found the AST block.
        State = ASTBlock;
        continue;
      }

      if (Entry.ID == UNHASHED_CONTROL_BLOCK_ID) {
        if (llvm::Error Err = InStream.EnterSubBlock(UNHASHED_CONTROL_BLOCK_ID))
          return Err;

        // Found the Diagnostic Options block.
        State = DiagnosticOptionsBlock;
        continue;
      }

      if (llvm::Error Err = InStream.SkipBlock())
        return Err;

      continue;

    case llvm::BitstreamEntry::EndBlock:
      State = Other;
      continue;
    }

    // Read the given record.
    SmallVector<uint64_t, 64> Record;
    StringRef Blob;
    Expected<unsigned> MaybeCode = InStream.readRecord(Entry.ID, Record, &Blob);
    if (!MaybeCode)
      return MaybeCode.takeError();
    unsigned Code = MaybeCode.get();

    // Handle module dependencies.
    if (State == ControlBlock && Code == IMPORTS) {
      // Load each of the imported PCH files.
      unsigned Idx = 0, N = Record.size();
      while (Idx < N) {
        // Read information about the AST file.

        // Skip the imported kind
        ++Idx;

        // Skip if it is standard C++ module
        ++Idx;

        // Skip the import location
        ++Idx;

        // Load stored size/modification time.
        off_t StoredSize = (off_t)Record[Idx++];
        time_t StoredModTime = (time_t)Record[Idx++];

        // Skip the stored signature.
        // FIXME: we could read the signature out of the import and validate it.
        auto FirstSignatureByte = Record.begin() + Idx;
        ASTFileSignature StoredSignature = ASTFileSignature::create(
            FirstSignatureByte, FirstSignatureByte + ASTFileSignature::size);
        Idx += ASTFileSignature::size;

        // Skip the module name (currently this is only used for prebuilt
        // modules while here we are only dealing with cached).
        Idx += Record[Idx] + 1;

        // Retrieve the imported file name.
        unsigned Length = Record[Idx++];
        SmallString<128> ImportedFile(Record.begin() + Idx,
                                      Record.begin() + Idx + Length);
        Idx += Length;

        // Find the imported module file.
        auto DependsOnFile =
            FileMgr.getOptionalFileRef(ImportedFile, /*OpenFile=*/false,
                                       /*CacheFailure=*/false);

        if (!DependsOnFile)
          return llvm::createStringError(std::errc::bad_file_descriptor,
                                         "imported file \"%s\" not found",
                                         ImportedFile.c_str());

        // Save the information in ImportedModuleFileInfo so we can verify after
        // loading all pcms.
        ImportedModuleFiles.insert(std::make_pair(
            *DependsOnFile, ImportedModuleFileInfo(StoredSize, StoredModTime,
                                                   StoredSignature)));

        // Record the dependency.
        unsigned DependsOnID = getModuleFileInfo(*DependsOnFile).ID;
        getModuleFileInfo(File).Dependencies.push_back(DependsOnID);
      }

      continue;
    }

    // Handle the identifier table
    if (State == ASTBlock && Code == IDENTIFIER_TABLE && Record[0] > 0) {
      typedef llvm::OnDiskIterableChainedHashTable<
          InterestingASTIdentifierLookupTrait> InterestingIdentifierTable;
      std::unique_ptr<InterestingIdentifierTable> Table(
          InterestingIdentifierTable::Create(
              (const unsigned char *)Blob.data() + Record[0],
              (const unsigned char *)Blob.data() + sizeof(uint32_t),
              (const unsigned char *)Blob.data()));
      for (InterestingIdentifierTable::data_iterator D = Table->data_begin(),
                                                     DEnd = Table->data_end();
           D != DEnd; ++D) {
        std::pair<StringRef, bool> Ident = *D;
        if (Ident.second)
          InterestingIdentifiers[Ident.first].push_back(ID);
        else
          (void)InterestingIdentifiers[Ident.first];
      }
    }

    // Get Signature.
    if (State == DiagnosticOptionsBlock && Code == SIGNATURE) {
      auto Signature = ASTFileSignature::create(Blob.begin(), Blob.end());
      assert(Signature != ASTFileSignature::createDummy() &&
             "Dummy AST file signature not backpatched in ASTWriter.");
      getModuleFileInfo(File).Signature = Signature;
    }

    // We don't care about this record.
  }

  return llvm::Error::success();
}

namespace {

/// Trait used to generate the identifier index as an on-disk hash
/// table.
class IdentifierIndexWriterTrait {
public:
  typedef StringRef key_type;
  typedef StringRef key_type_ref;
  typedef SmallVector<unsigned, 2> data_type;
  typedef const SmallVector<unsigned, 2> &data_type_ref;
  typedef unsigned hash_value_type;
  typedef unsigned offset_type;

  static hash_value_type ComputeHash(key_type_ref Key) {
    return llvm::djbHash(Key);
  }

  std::pair<unsigned,unsigned>
  EmitKeyDataLength(raw_ostream& Out, key_type_ref Key, data_type_ref Data) {
    using namespace llvm::support;
    endian::Writer LE(Out, llvm::endianness::little);
    unsigned KeyLen = Key.size();
    unsigned DataLen = Data.size() * 4;
    LE.write<uint16_t>(KeyLen);
    LE.write<uint16_t>(DataLen);
    return std::make_pair(KeyLen, DataLen);
  }

  void EmitKey(raw_ostream& Out, key_type_ref Key, unsigned KeyLen) {
    Out.write(Key.data(), KeyLen);
  }

  void EmitData(raw_ostream& Out, key_type_ref Key, data_type_ref Data,
                unsigned DataLen) {
    using namespace llvm::support;
    for (unsigned I = 0, N = Data.size(); I != N; ++I)
      endian::write<uint32_t>(Out, Data[I], llvm::endianness::little);
  }
};

}

bool GlobalModuleIndexBuilder::writeIndex(llvm::BitstreamWriter &Stream) {
  for (auto MapEntry : ImportedModuleFiles) {
    auto File = MapEntry.first;
    ImportedModuleFileInfo &Info = MapEntry.second;
    if (getModuleFileInfo(File).Signature) {
      if (getModuleFileInfo(File).Signature != Info.StoredSignature)
        // Verify Signature.
        return true;
    } else if (Info.StoredSize != File.getSize() ||
               Info.StoredModTime != File.getModificationTime())
      // Verify Size and ModTime.
      return true;
  }

  using namespace llvm;
  llvm::TimeTraceScope TimeScope("Module WriteIndex");

  // Emit the file header.
  Stream.Emit((unsigned)'B', 8);
  Stream.Emit((unsigned)'C', 8);
  Stream.Emit((unsigned)'G', 8);
  Stream.Emit((unsigned)'I', 8);

  // Write the block-info block, which describes the records in this bitcode
  // file.
  emitBlockInfoBlock(Stream);

  Stream.EnterSubblock(GLOBAL_INDEX_BLOCK_ID, 3);

  // Write the metadata.
  SmallVector<uint64_t, 2> Record;
  Record.push_back(CurrentVersion);
  Stream.EmitRecord(INDEX_METADATA, Record);

  // Write the set of known module files.
  for (ModuleFilesMap::iterator M = ModuleFiles.begin(),
                                MEnd = ModuleFiles.end();
       M != MEnd; ++M) {
    Record.clear();
    Record.push_back(M->second.ID);
    Record.push_back(M->first.getSize());
    Record.push_back(M->first.getModificationTime());

    // File name
    StringRef Name(M->first.getName());
    Record.push_back(Name.size());
    Record.append(Name.begin(), Name.end());

    // Dependencies
    Record.push_back(M->second.Dependencies.size());
    Record.append(M->second.Dependencies.begin(), M->second.Dependencies.end());
    Stream.EmitRecord(MODULE, Record);
  }

  // Write the identifier -> module file mapping.
  {
    llvm::OnDiskChainedHashTableGenerator<IdentifierIndexWriterTrait> Generator;
    IdentifierIndexWriterTrait Trait;

    // Populate the hash table.
    for (InterestingIdentifierMap::iterator I = InterestingIdentifiers.begin(),
                                            IEnd = InterestingIdentifiers.end();
         I != IEnd; ++I) {
      Generator.insert(I->first(), I->second, Trait);
    }

    // Create the on-disk hash table in a buffer.
    SmallString<4096> IdentifierTable;
    uint32_t BucketOffset;
    {
      using namespace llvm::support;
      llvm::raw_svector_ostream Out(IdentifierTable);
      // Make sure that no bucket is at offset 0
      endian::write<uint32_t>(Out, 0, llvm::endianness::little);
      BucketOffset = Generator.Emit(Out, Trait);
    }

    // Create a blob abbreviation
    auto Abbrev = std::make_shared<BitCodeAbbrev>();
    Abbrev->Add(BitCodeAbbrevOp(IDENTIFIER_INDEX));
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32));
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));
    unsigned IDTableAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

    // Write the identifier table
    uint64_t Record[] = {IDENTIFIER_INDEX, BucketOffset};
    Stream.EmitRecordWithBlob(IDTableAbbrev, Record, IdentifierTable);
  }

  Stream.ExitBlock();
  return false;
}

llvm::Error
GlobalModuleIndex::writeIndex(FileManager &FileMgr,
                              const PCHContainerReader &PCHContainerRdr,
                              StringRef Path) {
  llvm::SmallString<128> IndexPath;
  IndexPath += Path;
  llvm::sys::path::append(IndexPath, IndexFileName);

  // Coordinate building the global index file with other processes that might
  // try to do the same.
  llvm::LockFileManager Locked(IndexPath);
  switch (Locked) {
  case llvm::LockFileManager::LFS_Error:
    return llvm::createStringError(std::errc::io_error, "LFS error");

  case llvm::LockFileManager::LFS_Owned:
    // We're responsible for building the index ourselves. Do so below.
    break;

  case llvm::LockFileManager::LFS_Shared:
    // Someone else is responsible for building the index. We don't care
    // when they finish, so we're done.
    return llvm::createStringError(std::errc::device_or_resource_busy,
                                   "someone else is building the index");
  }

  // The module index builder.
  GlobalModuleIndexBuilder Builder(FileMgr, PCHContainerRdr);

  // Load each of the module files.
  std::error_code EC;
  for (llvm::sys::fs::directory_iterator D(Path, EC), DEnd;
       D != DEnd && !EC;
       D.increment(EC)) {
    // If this isn't a module file, we don't care.
    if (llvm::sys::path::extension(D->path()) != ".pcm") {
      // ... unless it's a .pcm.lock file, which indicates that someone is
      // in the process of rebuilding a module. They'll rebuild the index
      // at the end of that translation unit, so we don't have to.
      if (llvm::sys::path::extension(D->path()) == ".pcm.lock")
        return llvm::createStringError(std::errc::device_or_resource_busy,
                                       "someone else is building the index");

      continue;
    }

    // If we can't find the module file, skip it.
    auto ModuleFile = FileMgr.getOptionalFileRef(D->path());
    if (!ModuleFile)
      continue;

    // Load this module file.
    if (llvm::Error Err = Builder.loadModuleFile(*ModuleFile))
      return Err;
  }

  // The output buffer, into which the global index will be written.
  SmallString<16> OutputBuffer;
  {
    llvm::BitstreamWriter OutputStream(OutputBuffer);
    if (Builder.writeIndex(OutputStream))
      return llvm::createStringError(std::errc::io_error,
                                     "failed writing index");
  }

  return llvm::writeToOutput(IndexPath, [&OutputBuffer](llvm::raw_ostream &OS) {
    OS << OutputBuffer;
    return llvm::Error::success();
  });
}

namespace {
  class GlobalIndexIdentifierIterator : public IdentifierIterator {
    /// The current position within the identifier lookup table.
    IdentifierIndexTable::key_iterator Current;

    /// The end position within the identifier lookup table.
    IdentifierIndexTable::key_iterator End;

  public:
    explicit GlobalIndexIdentifierIterator(IdentifierIndexTable &Idx) {
      Current = Idx.key_begin();
      End = Idx.key_end();
    }

    StringRef Next() override {
      if (Current == End)
        return StringRef();

      StringRef Result = *Current;
      ++Current;
      return Result;
    }
  };
}

IdentifierIterator *GlobalModuleIndex::createIdentifierIterator() const {
  IdentifierIndexTable &Table =
    *static_cast<IdentifierIndexTable *>(IdentifierIndex);
  return new GlobalIndexIdentifierIterator(Table);
}

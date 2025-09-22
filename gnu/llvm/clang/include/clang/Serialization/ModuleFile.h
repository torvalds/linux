//===- ModuleFile.h - Module file description -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Module class, which describes a module that has
//  been loaded from an AST file.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SERIALIZATION_MODULEFILE_H
#define LLVM_CLANG_SERIALIZATION_MODULEFILE_H

#include "clang/Basic/FileManager.h"
#include "clang/Basic/Module.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Serialization/ASTBitCodes.h"
#include "clang/Serialization/ContinuousRangeMap.h"
#include "clang/Serialization/ModuleFileExtension.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Bitstream/BitstreamReader.h"
#include "llvm/Support/Endian.h"
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace clang {

namespace serialization {

/// Specifies the kind of module that has been loaded.
enum ModuleKind {
  /// File is an implicitly-loaded module.
  MK_ImplicitModule,

  /// File is an explicitly-loaded module.
  MK_ExplicitModule,

  /// File is a PCH file treated as such.
  MK_PCH,

  /// File is a PCH file treated as the preamble.
  MK_Preamble,

  /// File is a PCH file treated as the actual main file.
  MK_MainFile,

  /// File is from a prebuilt module path.
  MK_PrebuiltModule
};

/// The input file info that has been loaded from an AST file.
struct InputFileInfo {
  std::string FilenameAsRequested;
  std::string Filename;
  uint64_t ContentHash;
  off_t StoredSize;
  time_t StoredTime;
  bool Overridden;
  bool Transient;
  bool TopLevel;
  bool ModuleMap;
};

/// The input file that has been loaded from this AST file, along with
/// bools indicating whether this was an overridden buffer or if it was
/// out-of-date or not-found.
class InputFile {
  enum {
    Overridden = 1,
    OutOfDate = 2,
    NotFound = 3
  };
  llvm::PointerIntPair<const FileEntryRef::MapEntry *, 2, unsigned> Val;

public:
  InputFile() = default;

  InputFile(FileEntryRef File, bool isOverridden = false,
            bool isOutOfDate = false) {
    assert(!(isOverridden && isOutOfDate) &&
           "an overridden cannot be out-of-date");
    unsigned intVal = 0;
    if (isOverridden)
      intVal = Overridden;
    else if (isOutOfDate)
      intVal = OutOfDate;
    Val.setPointerAndInt(&File.getMapEntry(), intVal);
  }

  static InputFile getNotFound() {
    InputFile File;
    File.Val.setInt(NotFound);
    return File;
  }

  OptionalFileEntryRef getFile() const {
    if (auto *P = Val.getPointer())
      return FileEntryRef(*P);
    return std::nullopt;
  }
  bool isOverridden() const { return Val.getInt() == Overridden; }
  bool isOutOfDate() const { return Val.getInt() == OutOfDate; }
  bool isNotFound() const { return Val.getInt() == NotFound; }
};

/// Information about a module that has been loaded by the ASTReader.
///
/// Each instance of the Module class corresponds to a single AST file, which
/// may be a precompiled header, precompiled preamble, a module, or an AST file
/// of some sort loaded as the main file, all of which are specific formulations
/// of the general notion of a "module". A module may depend on any number of
/// other modules.
class ModuleFile {
public:
  ModuleFile(ModuleKind Kind, FileEntryRef File, unsigned Generation)
      : Kind(Kind), File(File), Generation(Generation) {}
  ~ModuleFile();

  // === General information ===

  /// The index of this module in the list of modules.
  unsigned Index = 0;

  /// The type of this module.
  ModuleKind Kind;

  /// The file name of the module file.
  std::string FileName;

  /// The name of the module.
  std::string ModuleName;

  /// The base directory of the module.
  std::string BaseDirectory;

  std::string getTimestampFilename() const {
    return FileName + ".timestamp";
  }

  /// The original source file name that was used to build the
  /// primary AST file, which may have been modified for
  /// relocatable-pch support.
  std::string OriginalSourceFileName;

  /// The actual original source file name that was used to
  /// build this AST file.
  std::string ActualOriginalSourceFileName;

  /// The file ID for the original source file that was used to
  /// build this AST file.
  FileID OriginalSourceFileID;

  std::string ModuleMapPath;

  /// Whether this precompiled header is a relocatable PCH file.
  bool RelocatablePCH = false;

  /// Whether this module file is a standard C++ module.
  bool StandardCXXModule = false;

  /// Whether timestamps are included in this module file.
  bool HasTimestamps = false;

  /// Whether the top-level module has been read from the AST file.
  bool DidReadTopLevelSubmodule = false;

  /// The file entry for the module file.
  FileEntryRef File;

  /// The signature of the module file, which may be used instead of the size
  /// and modification time to identify this particular file.
  ASTFileSignature Signature;

  /// The signature of the AST block of the module file, this can be used to
  /// unique module files based on AST contents.
  ASTFileSignature ASTBlockHash;

  /// The bit vector denoting usage of each header search entry (true = used).
  llvm::BitVector SearchPathUsage;

  /// The bit vector denoting usage of each VFS entry (true = used).
  llvm::BitVector VFSUsage;

  /// Whether this module has been directly imported by the
  /// user.
  bool DirectlyImported = false;

  /// The generation of which this module file is a part.
  unsigned Generation;

  /// The memory buffer that stores the data associated with
  /// this AST file, owned by the InMemoryModuleCache.
  llvm::MemoryBuffer *Buffer = nullptr;

  /// The size of this file, in bits.
  uint64_t SizeInBits = 0;

  /// The global bit offset (or base) of this module
  uint64_t GlobalBitOffset = 0;

  /// The bit offset of the AST block of this module.
  uint64_t ASTBlockStartOffset = 0;

  /// The serialized bitstream data for this file.
  StringRef Data;

  /// The main bitstream cursor for the main block.
  llvm::BitstreamCursor Stream;

  /// The source location where the module was explicitly or implicitly
  /// imported in the local translation unit.
  ///
  /// If module A depends on and imports module B, both modules will have the
  /// same DirectImportLoc, but different ImportLoc (B's ImportLoc will be a
  /// source location inside module A).
  ///
  /// WARNING: This is largely useless. It doesn't tell you when a module was
  /// made visible, just when the first submodule of that module was imported.
  SourceLocation DirectImportLoc;

  /// The source location where this module was first imported.
  SourceLocation ImportLoc;

  /// The first source location in this module.
  SourceLocation FirstLoc;

  /// The list of extension readers that are attached to this module
  /// file.
  std::vector<std::unique_ptr<ModuleFileExtensionReader>> ExtensionReaders;

  /// The module offset map data for this file. If non-empty, the various
  /// ContinuousRangeMaps described below have not yet been populated.
  StringRef ModuleOffsetMap;

  // === Input Files ===

  /// The cursor to the start of the input-files block.
  llvm::BitstreamCursor InputFilesCursor;

  /// Absolute offset of the start of the input-files block.
  uint64_t InputFilesOffsetBase = 0;

  /// Relative offsets for all of the input file entries in the AST file.
  const llvm::support::unaligned_uint64_t *InputFileOffsets = nullptr;

  /// The input files that have been loaded from this AST file.
  std::vector<InputFile> InputFilesLoaded;

  /// The input file infos that have been loaded from this AST file.
  std::vector<InputFileInfo> InputFileInfosLoaded;

  // All user input files reside at the index range [0, NumUserInputFiles), and
  // system input files reside at [NumUserInputFiles, InputFilesLoaded.size()).
  unsigned NumUserInputFiles = 0;

  /// If non-zero, specifies the time when we last validated input
  /// files.  Zero means we never validated them.
  ///
  /// The time is specified in seconds since the start of the Epoch.
  uint64_t InputFilesValidationTimestamp = 0;

  // === Source Locations ===

  /// Cursor used to read source location entries.
  llvm::BitstreamCursor SLocEntryCursor;

  /// The bit offset to the start of the SOURCE_MANAGER_BLOCK.
  uint64_t SourceManagerBlockStartOffset = 0;

  /// The number of source location entries in this AST file.
  unsigned LocalNumSLocEntries = 0;

  /// The base ID in the source manager's view of this module.
  int SLocEntryBaseID = 0;

  /// The base offset in the source manager's view of this module.
  SourceLocation::UIntTy SLocEntryBaseOffset = 0;

  /// Base file offset for the offsets in SLocEntryOffsets. Real file offset
  /// for the entry is SLocEntryOffsetsBase + SLocEntryOffsets[i].
  uint64_t SLocEntryOffsetsBase = 0;

  /// Offsets for all of the source location entries in the
  /// AST file.
  const uint32_t *SLocEntryOffsets = nullptr;

  // === Identifiers ===

  /// The number of identifiers in this AST file.
  unsigned LocalNumIdentifiers = 0;

  /// Offsets into the identifier table data.
  ///
  /// This array is indexed by the identifier ID (-1), and provides
  /// the offset into IdentifierTableData where the string data is
  /// stored.
  const uint32_t *IdentifierOffsets = nullptr;

  /// Base identifier ID for identifiers local to this module.
  serialization::IdentifierID BaseIdentifierID = 0;

  /// Actual data for the on-disk hash table of identifiers.
  ///
  /// This pointer points into a memory buffer, where the on-disk hash
  /// table for identifiers actually lives.
  const unsigned char *IdentifierTableData = nullptr;

  /// A pointer to an on-disk hash table of opaque type
  /// IdentifierHashTable.
  void *IdentifierLookupTable = nullptr;

  /// Offsets of identifiers that we're going to preload within
  /// IdentifierTableData.
  std::vector<unsigned> PreloadIdentifierOffsets;

  // === Macros ===

  /// The cursor to the start of the preprocessor block, which stores
  /// all of the macro definitions.
  llvm::BitstreamCursor MacroCursor;

  /// The number of macros in this AST file.
  unsigned LocalNumMacros = 0;

  /// Base file offset for the offsets in MacroOffsets. Real file offset for
  /// the entry is MacroOffsetsBase + MacroOffsets[i].
  uint64_t MacroOffsetsBase = 0;

  /// Offsets of macros in the preprocessor block.
  ///
  /// This array is indexed by the macro ID (-1), and provides
  /// the offset into the preprocessor block where macro definitions are
  /// stored.
  const uint32_t *MacroOffsets = nullptr;

  /// Base macro ID for macros local to this module.
  serialization::MacroID BaseMacroID = 0;

  /// Remapping table for macro IDs in this module.
  ContinuousRangeMap<uint32_t, int, 2> MacroRemap;

  /// The offset of the start of the set of defined macros.
  uint64_t MacroStartOffset = 0;

  // === Detailed PreprocessingRecord ===

  /// The cursor to the start of the (optional) detailed preprocessing
  /// record block.
  llvm::BitstreamCursor PreprocessorDetailCursor;

  /// The offset of the start of the preprocessor detail cursor.
  uint64_t PreprocessorDetailStartOffset = 0;

  /// Base preprocessed entity ID for preprocessed entities local to
  /// this module.
  serialization::PreprocessedEntityID BasePreprocessedEntityID = 0;

  /// Remapping table for preprocessed entity IDs in this module.
  ContinuousRangeMap<uint32_t, int, 2> PreprocessedEntityRemap;

  const PPEntityOffset *PreprocessedEntityOffsets = nullptr;
  unsigned NumPreprocessedEntities = 0;

  /// Base ID for preprocessed skipped ranges local to this module.
  unsigned BasePreprocessedSkippedRangeID = 0;

  const PPSkippedRange *PreprocessedSkippedRangeOffsets = nullptr;
  unsigned NumPreprocessedSkippedRanges = 0;

  // === Header search information ===

  /// The number of local HeaderFileInfo structures.
  unsigned LocalNumHeaderFileInfos = 0;

  /// Actual data for the on-disk hash table of header file
  /// information.
  ///
  /// This pointer points into a memory buffer, where the on-disk hash
  /// table for header file information actually lives.
  const char *HeaderFileInfoTableData = nullptr;

  /// The on-disk hash table that contains information about each of
  /// the header files.
  void *HeaderFileInfoTable = nullptr;

  // === Submodule information ===

  /// The number of submodules in this module.
  unsigned LocalNumSubmodules = 0;

  /// Base submodule ID for submodules local to this module.
  serialization::SubmoduleID BaseSubmoduleID = 0;

  /// Remapping table for submodule IDs in this module.
  ContinuousRangeMap<uint32_t, int, 2> SubmoduleRemap;

  // === Selectors ===

  /// The number of selectors new to this file.
  ///
  /// This is the number of entries in SelectorOffsets.
  unsigned LocalNumSelectors = 0;

  /// Offsets into the selector lookup table's data array
  /// where each selector resides.
  const uint32_t *SelectorOffsets = nullptr;

  /// Base selector ID for selectors local to this module.
  serialization::SelectorID BaseSelectorID = 0;

  /// Remapping table for selector IDs in this module.
  ContinuousRangeMap<uint32_t, int, 2> SelectorRemap;

  /// A pointer to the character data that comprises the selector table
  ///
  /// The SelectorOffsets table refers into this memory.
  const unsigned char *SelectorLookupTableData = nullptr;

  /// A pointer to an on-disk hash table of opaque type
  /// ASTSelectorLookupTable.
  ///
  /// This hash table provides the IDs of all selectors, and the associated
  /// instance and factory methods.
  void *SelectorLookupTable = nullptr;

  // === Declarations ===

  /// DeclsCursor - This is a cursor to the start of the DECLTYPES_BLOCK block.
  /// It has read all the abbreviations at the start of the block and is ready
  /// to jump around with these in context.
  llvm::BitstreamCursor DeclsCursor;

  /// The offset to the start of the DECLTYPES_BLOCK block.
  uint64_t DeclsBlockStartOffset = 0;

  /// The number of declarations in this AST file.
  unsigned LocalNumDecls = 0;

  /// Offset of each declaration within the bitstream, indexed
  /// by the declaration ID (-1).
  const DeclOffset *DeclOffsets = nullptr;

  /// Base declaration index in ASTReader for declarations local to this module.
  unsigned BaseDeclIndex = 0;

  /// Array of file-level DeclIDs sorted by file.
  const serialization::unaligned_decl_id_t *FileSortedDecls = nullptr;
  unsigned NumFileSortedDecls = 0;

  /// Array of category list location information within this
  /// module file, sorted by the definition ID.
  const serialization::ObjCCategoriesInfo *ObjCCategoriesMap = nullptr;

  /// The number of redeclaration info entries in ObjCCategoriesMap.
  unsigned LocalNumObjCCategoriesInMap = 0;

  /// The Objective-C category lists for categories known to this
  /// module.
  SmallVector<uint64_t, 1> ObjCCategories;

  // === Types ===

  /// The number of types in this AST file.
  unsigned LocalNumTypes = 0;

  /// Offset of each type within the bitstream, indexed by the
  /// type ID, or the representation of a Type*.
  const UnalignedUInt64 *TypeOffsets = nullptr;

  /// Base type ID for types local to this module as represented in
  /// the global type ID space.
  serialization::TypeID BaseTypeIndex = 0;

  // === Miscellaneous ===

  /// Diagnostic IDs and their mappings that the user changed.
  SmallVector<uint64_t, 8> PragmaDiagMappings;

  /// List of modules which depend on this module
  llvm::SetVector<ModuleFile *> ImportedBy;

  /// List of modules which this module directly imported
  llvm::SetVector<ModuleFile *> Imports;

  /// List of modules which this modules dependent on. Different
  /// from `Imports`, this includes indirectly imported modules too.
  /// The order of TransitiveImports is significant. It should keep
  /// the same order with that module file manager when we write
  /// the current module file. The value of the member will be initialized
  /// in `ASTReader::ReadModuleOffsetMap`.
  llvm::SmallVector<ModuleFile *, 16> TransitiveImports;

  /// Determine whether this module was directly imported at
  /// any point during translation.
  bool isDirectlyImported() const { return DirectlyImported; }

  /// Is this a module file for a module (rather than a PCH or similar).
  bool isModule() const {
    return Kind == MK_ImplicitModule || Kind == MK_ExplicitModule ||
           Kind == MK_PrebuiltModule;
  }

  /// Dump debugging output for this module.
  void dump();
};

} // namespace serialization

} // namespace clang

#endif // LLVM_CLANG_SERIALIZATION_MODULEFILE_H

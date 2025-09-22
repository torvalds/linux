//===- ASTBitCodes.h - Enum values for the PCH bitcode format ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header defines Bitcode enum values for Clang serialized AST files.
//
// The enum values defined in this file should be considered permanent.  If
// new features are added, they should have values added at the end of the
// respective lists.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SERIALIZATION_ASTBITCODES_H
#define LLVM_CLANG_SERIALIZATION_ASTBITCODES_H

#include "clang/AST/DeclID.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/Type.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Serialization/SourceLocationEncoding.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/Bitstream/BitCodes.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>
#include <cstdint>

namespace clang {
namespace serialization {

/// AST file major version number supported by this version of
/// Clang.
///
/// Whenever the AST file format changes in a way that makes it
/// incompatible with previous versions (such that a reader
/// designed for the previous version could not support reading
/// the new version), this number should be increased.
///
/// Version 4 of AST files also requires that the version control branch and
/// revision match exactly, since there is no backward compatibility of
/// AST files at this time.
const unsigned VERSION_MAJOR = 31;

/// AST file minor version number supported by this version of
/// Clang.
///
/// Whenever the AST format changes in a way that is still
/// compatible with previous versions (such that a reader designed
/// for the previous version could still support reading the new
/// version by ignoring new kinds of subblocks), this number
/// should be increased.
const unsigned VERSION_MINOR = 1;

/// An ID number that refers to an identifier in an AST file.
///
/// The ID numbers of identifiers are consecutive (in order of discovery)
/// and start at 1. 0 is reserved for NULL.
using IdentifierID = uint64_t;

/// The number of predefined identifier IDs.
const unsigned int NUM_PREDEF_IDENT_IDS = 1;

/// An ID number that refers to a declaration in an AST file. See the comments
/// in DeclIDBase for details.
using DeclID = DeclIDBase::DeclID;

/// An ID number that refers to a type in an AST file.
///
/// The ID of a type is partitioned into three parts:
/// - the lower three bits are used to store the const/volatile/restrict
///   qualifiers (as with QualType).
/// - the next 29 bits provide a type index in the corresponding
///   module file.
/// - the upper 32 bits provide a module file index.
///
/// The type index values are partitioned into two
/// sets. The values below NUM_PREDEF_TYPE_IDs are predefined type
/// IDs (based on the PREDEF_TYPE_*_ID constants), with 0 as a
/// placeholder for "no type". The module file index for predefined
/// types are always 0 since they don't belong to any modules.
/// Values from NUM_PREDEF_TYPE_IDs are other types that have
/// serialized representations.
using TypeID = uint64_t;
/// Same with TypeID except that the LocalTypeID is only meaningful
/// with the corresponding ModuleFile.
///
/// FIXME: Make TypeID and LocalTypeID a class to improve the type
/// safety.
using LocalTypeID = TypeID;

/// A type index; the type ID with the qualifier bits removed.
/// Keep structure alignment 32-bit since the blob is assumed as 32-bit
/// aligned.
class TypeIdx {
  uint32_t ModuleFileIndex = 0;
  uint32_t Idx = 0;

public:
  TypeIdx() = default;

  explicit TypeIdx(uint32_t ModuleFileIdx, uint32_t Idx)
      : ModuleFileIndex(ModuleFileIdx), Idx(Idx) {}

  uint32_t getModuleFileIndex() const { return ModuleFileIndex; }

  uint64_t getValue() const { return ((uint64_t)ModuleFileIndex << 32) | Idx; }

  TypeID asTypeID(unsigned FastQuals) const {
    if (Idx == uint32_t(-1))
      return TypeID(-1);

    unsigned Index = (Idx << Qualifiers::FastWidth) | FastQuals;
    return ((uint64_t)ModuleFileIndex << 32) | Index;
  }

  static TypeIdx fromTypeID(TypeID ID) {
    if (ID == TypeID(-1))
      return TypeIdx(0, -1);

    return TypeIdx(ID >> 32, (ID & llvm::maskTrailingOnes<TypeID>(32)) >>
                                 Qualifiers::FastWidth);
  }
};

static_assert(alignof(TypeIdx) == 4);

/// A structure for putting "fast"-unqualified QualTypes into a
/// DenseMap.  This uses the standard pointer hash function.
struct UnsafeQualTypeDenseMapInfo {
  static bool isEqual(QualType A, QualType B) { return A == B; }

  static QualType getEmptyKey() {
    return QualType::getFromOpaquePtr((void *)1);
  }

  static QualType getTombstoneKey() {
    return QualType::getFromOpaquePtr((void *)2);
  }

  static unsigned getHashValue(QualType T) {
    assert(!T.getLocalFastQualifiers() &&
           "hash invalid for types with fast quals");
    uintptr_t v = reinterpret_cast<uintptr_t>(T.getAsOpaquePtr());
    return (unsigned(v) >> 4) ^ (unsigned(v) >> 9);
  }
};

/// An ID number that refers to a macro in an AST file.
using MacroID = uint32_t;

/// A global ID number that refers to a macro in an AST file.
using GlobalMacroID = uint32_t;

/// A local to a module ID number that refers to a macro in an
/// AST file.
using LocalMacroID = uint32_t;

/// The number of predefined macro IDs.
const unsigned int NUM_PREDEF_MACRO_IDS = 1;

/// An ID number that refers to an ObjC selector in an AST file.
using SelectorID = uint32_t;

/// The number of predefined selector IDs.
const unsigned int NUM_PREDEF_SELECTOR_IDS = 1;

/// An ID number that refers to a set of CXXBaseSpecifiers in an
/// AST file.
using CXXBaseSpecifiersID = uint32_t;

/// An ID number that refers to a list of CXXCtorInitializers in an
/// AST file.
using CXXCtorInitializersID = uint32_t;

/// An ID number that refers to an entity in the detailed
/// preprocessing record.
using PreprocessedEntityID = uint32_t;

/// An ID number that refers to a submodule in a module file.
using SubmoduleID = uint32_t;

/// The number of predefined submodule IDs.
const unsigned int NUM_PREDEF_SUBMODULE_IDS = 1;

/// 32 aligned uint64_t in the AST file. Use splitted 64-bit integer into
/// low/high parts to keep structure alignment 32-bit (it is important
/// because blobs in bitstream are 32-bit aligned). This structure is
/// serialized "as is" to the AST file.
class UnalignedUInt64 {
  uint32_t BitLow = 0;
  uint32_t BitHigh = 0;

public:
  UnalignedUInt64() = default;
  UnalignedUInt64(uint64_t BitOffset) { set(BitOffset); }

  void set(uint64_t Offset) {
    BitLow = Offset;
    BitHigh = Offset >> 32;
  }

  uint64_t get() const { return BitLow | (uint64_t(BitHigh) << 32); }
};

/// Source range/offset of a preprocessed entity.
class PPEntityOffset {
  using RawLocEncoding = SourceLocationEncoding::RawLocEncoding;

  /// Raw source location of beginning of range.
  UnalignedUInt64 Begin;

  /// Raw source location of end of range.
  UnalignedUInt64 End;

  /// Offset in the AST file relative to ModuleFile::MacroOffsetsBase.
  uint32_t BitOffset;

public:
  PPEntityOffset(RawLocEncoding Begin, RawLocEncoding End, uint32_t BitOffset)
      : Begin(Begin), End(End), BitOffset(BitOffset) {}

  RawLocEncoding getBegin() const { return Begin.get(); }
  RawLocEncoding getEnd() const { return End.get(); }

  uint32_t getOffset() const { return BitOffset; }
};

/// Source range of a skipped preprocessor region
class PPSkippedRange {
  using RawLocEncoding = SourceLocationEncoding::RawLocEncoding;

  /// Raw source location of beginning of range.
  UnalignedUInt64 Begin;
  /// Raw source location of end of range.
  UnalignedUInt64 End;

public:
  PPSkippedRange(RawLocEncoding Begin, RawLocEncoding End)
      : Begin(Begin), End(End) {}

  RawLocEncoding getBegin() const { return Begin.get(); }
  RawLocEncoding getEnd() const { return End.get(); }
};

/// Source location and bit offset of a declaration. Keep
/// structure alignment 32-bit since the blob is assumed as 32-bit aligned.
class DeclOffset {
  using RawLocEncoding = SourceLocationEncoding::RawLocEncoding;

  /// Raw source location.
  UnalignedUInt64 RawLoc;

  /// Offset relative to the start of the DECLTYPES_BLOCK block.
  UnalignedUInt64 BitOffset;

public:
  DeclOffset() = default;
  DeclOffset(RawLocEncoding RawLoc, uint64_t BitOffset,
             uint64_t DeclTypesBlockStartOffset)
      : RawLoc(RawLoc) {
    setBitOffset(BitOffset, DeclTypesBlockStartOffset);
  }

  void setRawLoc(RawLocEncoding Loc) { RawLoc = Loc; }

  RawLocEncoding getRawLoc() const { return RawLoc.get(); }

  void setBitOffset(uint64_t Offset, const uint64_t DeclTypesBlockStartOffset) {
    BitOffset.set(Offset - DeclTypesBlockStartOffset);
  }

  uint64_t getBitOffset(const uint64_t DeclTypesBlockStartOffset) const {
    return BitOffset.get() + DeclTypesBlockStartOffset;
  }
};

// The unaligned decl ID used in the Blobs of bistreams.
using unaligned_decl_id_t =
    llvm::support::detail::packed_endian_specific_integral<
        serialization::DeclID, llvm::endianness::native,
        llvm::support::unaligned>;

/// The number of predefined preprocessed entity IDs.
const unsigned int NUM_PREDEF_PP_ENTITY_IDS = 1;

/// Describes the various kinds of blocks that occur within
/// an AST file.
enum BlockIDs {
  /// The AST block, which acts as a container around the
  /// full AST block.
  AST_BLOCK_ID = llvm::bitc::FIRST_APPLICATION_BLOCKID,

  /// The block containing information about the source
  /// manager.
  SOURCE_MANAGER_BLOCK_ID,

  /// The block containing information about the
  /// preprocessor.
  PREPROCESSOR_BLOCK_ID,

  /// The block containing the definitions of all of the
  /// types and decls used within the AST file.
  DECLTYPES_BLOCK_ID,

  /// The block containing the detailed preprocessing record.
  PREPROCESSOR_DETAIL_BLOCK_ID,

  /// The block containing the submodule structure.
  SUBMODULE_BLOCK_ID,

  /// The block containing comments.
  COMMENTS_BLOCK_ID,

  /// The control block, which contains all of the
  /// information that needs to be validated prior to committing
  /// to loading the AST file.
  CONTROL_BLOCK_ID,

  /// The block of input files, which were used as inputs
  /// to create this AST file.
  ///
  /// This block is part of the control block.
  INPUT_FILES_BLOCK_ID,

  /// The block of configuration options, used to check that
  /// a module is being used in a configuration compatible with the
  /// configuration in which it was built.
  ///
  /// This block is part of the control block.
  OPTIONS_BLOCK_ID,

  /// A block containing a module file extension.
  EXTENSION_BLOCK_ID,

  /// A block with unhashed content.
  ///
  /// These records should not change the \a ASTFileSignature.  See \a
  /// UnhashedControlBlockRecordTypes for the list of records.
  UNHASHED_CONTROL_BLOCK_ID,
};

/// Record types that occur within the control block.
enum ControlRecordTypes {
  /// AST file metadata, including the AST file version number
  /// and information about the compiler used to build this AST file.
  METADATA = 1,

  /// Record code for the list of other AST files imported by
  /// this AST file.
  IMPORTS,

  /// Record code for the original file that was used to
  /// generate the AST file, including both its file ID and its
  /// name.
  ORIGINAL_FILE,

  /// Record code for file ID of the file or buffer that was used to
  /// generate the AST file.
  ORIGINAL_FILE_ID,

  /// Offsets into the input-files block where input files
  /// reside.
  INPUT_FILE_OFFSETS,

  /// Record code for the module name.
  MODULE_NAME,

  /// Record code for the module map file that was used to build this
  /// AST file.
  MODULE_MAP_FILE,

  /// Record code for the module build directory.
  MODULE_DIRECTORY,
};

/// Record types that occur within the options block inside
/// the control block.
enum OptionsRecordTypes {
  /// Record code for the language options table.
  ///
  /// The record with this code contains the contents of the
  /// LangOptions structure. We serialize the entire contents of
  /// the structure, and let the reader decide which options are
  /// actually important to check.
  LANGUAGE_OPTIONS = 1,

  /// Record code for the target options table.
  TARGET_OPTIONS,

  /// Record code for the filesystem options table.
  FILE_SYSTEM_OPTIONS,

  /// Record code for the headers search options table.
  HEADER_SEARCH_OPTIONS,

  /// Record code for the preprocessor options table.
  PREPROCESSOR_OPTIONS,
};

/// Record codes for the unhashed control block.
enum UnhashedControlBlockRecordTypes {
  /// Record code for the signature that identifiers this AST file.
  SIGNATURE = 1,

  /// Record code for the content hash of the AST block.
  AST_BLOCK_HASH,

  /// Record code for the diagnostic options table.
  DIAGNOSTIC_OPTIONS,

  /// Record code for the headers search paths.
  HEADER_SEARCH_PATHS,

  /// Record code for \#pragma diagnostic mappings.
  DIAG_PRAGMA_MAPPINGS,

  /// Record code for the indices of used header search entries.
  HEADER_SEARCH_ENTRY_USAGE,

  /// Record code for the indices of used VFSs.
  VFS_USAGE,
};

/// Record code for extension blocks.
enum ExtensionBlockRecordTypes {
  /// Metadata describing this particular extension.
  EXTENSION_METADATA = 1,

  /// The first record ID allocated to the extensions themselves.
  FIRST_EXTENSION_RECORD_ID = 4
};

/// Record types that occur within the input-files block
/// inside the control block.
enum InputFileRecordTypes {
  /// An input file.
  INPUT_FILE = 1,

  /// The input file content hash
  INPUT_FILE_HASH
};

/// Record types that occur within the AST block itself.
enum ASTRecordTypes {
  /// Record code for the offsets of each type.
  ///
  /// The TYPE_OFFSET constant describes the record that occurs
  /// within the AST block. The record itself is an array of offsets that
  /// point into the declarations and types block (identified by
  /// DECLTYPES_BLOCK_ID). The index into the array is based on the ID
  /// of a type. For a given type ID @c T, the lower three bits of
  /// @c T are its qualifiers (const, volatile, restrict), as in
  /// the QualType class. The upper bits, after being shifted and
  /// subtracting NUM_PREDEF_TYPE_IDS, are used to index into the
  /// TYPE_OFFSET block to determine the offset of that type's
  /// corresponding record within the DECLTYPES_BLOCK_ID block.
  TYPE_OFFSET = 1,

  /// Record code for the offsets of each decl.
  ///
  /// The DECL_OFFSET constant describes the record that occurs
  /// within the block identified by DECL_OFFSETS_BLOCK_ID within
  /// the AST block. The record itself is an array of offsets that
  /// point into the declarations and types block (identified by
  /// DECLTYPES_BLOCK_ID). The declaration ID is an index into this
  /// record, after subtracting one to account for the use of
  /// declaration ID 0 for a NULL declaration pointer. Index 0 is
  /// reserved for the translation unit declaration.
  DECL_OFFSET = 2,

  /// Record code for the table of offsets of each
  /// identifier ID.
  ///
  /// The offset table contains offsets into the blob stored in
  /// the IDENTIFIER_TABLE record. Each offset points to the
  /// NULL-terminated string that corresponds to that identifier.
  IDENTIFIER_OFFSET = 3,

  /// This is so that older clang versions, before the introduction
  /// of the control block, can read and reject the newer PCH format.
  /// *DON'T CHANGE THIS NUMBER*.
  METADATA_OLD_FORMAT = 4,

  /// Record code for the identifier table.
  ///
  /// The identifier table is a simple blob that contains
  /// NULL-terminated strings for all of the identifiers
  /// referenced by the AST file. The IDENTIFIER_OFFSET table
  /// contains the mapping from identifier IDs to the characters
  /// in this blob. Note that the starting offsets of all of the
  /// identifiers are odd, so that, when the identifier offset
  /// table is loaded in, we can use the low bit to distinguish
  /// between offsets (for unresolved identifier IDs) and
  /// IdentifierInfo pointers (for already-resolved identifier
  /// IDs).
  IDENTIFIER_TABLE = 5,

  /// Record code for the array of eagerly deserialized decls.
  ///
  /// The AST file contains a list of all of the declarations that should be
  /// eagerly deserialized present within the parsed headers, stored as an
  /// array of declaration IDs. These declarations will be
  /// reported to the AST consumer after the AST file has been
  /// read, since their presence can affect the semantics of the
  /// program (e.g., for code generation).
  EAGERLY_DESERIALIZED_DECLS = 6,

  /// Record code for the set of non-builtin, special
  /// types.
  ///
  /// This record contains the type IDs for the various type nodes
  /// that are constructed during semantic analysis (e.g.,
  /// __builtin_va_list). The SPECIAL_TYPE_* constants provide
  /// offsets into this record.
  SPECIAL_TYPES = 7,

  /// Record code for the extra statistics we gather while
  /// generating an AST file.
  STATISTICS = 8,

  /// Record code for the array of tentative definitions.
  TENTATIVE_DEFINITIONS = 9,

  // ID 10 used to be for a list of extern "C" declarations.

  /// Record code for the table of offsets into the
  /// Objective-C method pool.
  SELECTOR_OFFSETS = 11,

  /// Record code for the Objective-C method pool,
  METHOD_POOL = 12,

  /// The value of the next __COUNTER__ to dispense.
  /// [PP_COUNTER_VALUE, Val]
  PP_COUNTER_VALUE = 13,

  /// Record code for the table of offsets into the block
  /// of source-location information.
  SOURCE_LOCATION_OFFSETS = 14,

  // ID 15 used to be for source location entry preloads.

  /// Record code for the set of ext_vector type names.
  EXT_VECTOR_DECLS = 16,

  /// Record code for the array of unused file scoped decls.
  UNUSED_FILESCOPED_DECLS = 17,

  /// Record code for the table of offsets to entries in the
  /// preprocessing record.
  PPD_ENTITIES_OFFSETS = 18,

  /// Record code for the array of VTable uses.
  VTABLE_USES = 19,

  // ID 20 used to be for a list of dynamic classes.

  /// Record code for referenced selector pool.
  REFERENCED_SELECTOR_POOL = 21,

  /// Record code for an update to the TU's lexically contained
  /// declarations.
  TU_UPDATE_LEXICAL = 22,

  // ID 23 used to be for a list of local redeclarations.

  /// Record code for declarations that Sema keeps references of.
  SEMA_DECL_REFS = 24,

  /// Record code for weak undeclared identifiers.
  WEAK_UNDECLARED_IDENTIFIERS = 25,

  /// Record code for pending implicit instantiations.
  PENDING_IMPLICIT_INSTANTIATIONS = 26,

  // ID 27 used to be for a list of replacement decls.

  /// Record code for an update to a decl context's lookup table.
  ///
  /// In practice, this should only be used for the TU and namespaces.
  UPDATE_VISIBLE = 28,

  /// Record for offsets of DECL_UPDATES records for declarations
  /// that were modified after being deserialized and need updates.
  DECL_UPDATE_OFFSETS = 29,

  // ID 30 used to be a decl update record. These are now in the DECLTYPES
  // block.

  // ID 31 used to be a list of offsets to DECL_CXX_BASE_SPECIFIERS records.

  // ID 32 used to be the code for \#pragma diagnostic mappings.

  /// Record code for special CUDA declarations.
  CUDA_SPECIAL_DECL_REFS = 33,

  /// Record code for header search information.
  HEADER_SEARCH_TABLE = 34,

  /// Record code for floating point \#pragma options.
  FP_PRAGMA_OPTIONS = 35,

  /// Record code for enabled OpenCL extensions.
  OPENCL_EXTENSIONS = 36,

  /// The list of delegating constructor declarations.
  DELEGATING_CTORS = 37,

  /// Record code for the set of known namespaces, which are used
  /// for typo correction.
  KNOWN_NAMESPACES = 38,

  /// Record code for the remapping information used to relate
  /// loaded modules to the various offsets and IDs(e.g., source location
  /// offests, declaration and type IDs) that are used in that module to
  /// refer to other modules.
  MODULE_OFFSET_MAP = 39,

  /// Record code for the source manager line table information,
  /// which stores information about \#line directives.
  SOURCE_MANAGER_LINE_TABLE = 40,

  /// Record code for map of Objective-C class definition IDs to the
  /// ObjC categories in a module that are attached to that class.
  OBJC_CATEGORIES_MAP = 41,

  /// Record code for a file sorted array of DeclIDs in a module.
  FILE_SORTED_DECLS = 42,

  /// Record code for an array of all of the (sub)modules that were
  /// imported by the AST file.
  IMPORTED_MODULES = 43,

  // ID 44 used to be a table of merged canonical declarations.
  // ID 45 used to be a list of declaration IDs of local redeclarations.

  /// Record code for the array of Objective-C categories (including
  /// extensions).
  ///
  /// This array can only be interpreted properly using the Objective-C
  /// categories map.
  OBJC_CATEGORIES = 46,

  /// Record code for the table of offsets of each macro ID.
  ///
  /// The offset table contains offsets into the blob stored in
  /// the preprocessor block. Each offset points to the corresponding
  /// macro definition.
  MACRO_OFFSET = 47,

  /// A list of "interesting" identifiers. Only used in C++ (where we
  /// don't normally do lookups into the serialized identifier table). These
  /// are eagerly deserialized.
  INTERESTING_IDENTIFIERS = 48,

  /// Record code for undefined but used functions and variables that
  /// need a definition in this TU.
  UNDEFINED_BUT_USED = 49,

  /// Record code for late parsed template functions.
  LATE_PARSED_TEMPLATE = 50,

  /// Record code for \#pragma optimize options.
  OPTIMIZE_PRAGMA_OPTIONS = 51,

  /// Record code for potentially unused local typedef names.
  UNUSED_LOCAL_TYPEDEF_NAME_CANDIDATES = 52,

  // ID 53 used to be a table of constructor initializer records.

  /// Delete expressions that will be analyzed later.
  DELETE_EXPRS_TO_ANALYZE = 54,

  /// Record code for \#pragma ms_struct options.
  MSSTRUCT_PRAGMA_OPTIONS = 55,

  /// Record code for \#pragma ms_struct options.
  POINTERS_TO_MEMBERS_PRAGMA_OPTIONS = 56,

  /// Number of unmatched #pragma clang cuda_force_host_device begin
  /// directives we've seen.
  CUDA_PRAGMA_FORCE_HOST_DEVICE_DEPTH = 57,

  /// Record code for types associated with OpenCL extensions.
  OPENCL_EXTENSION_TYPES = 58,

  /// Record code for declarations associated with OpenCL extensions.
  OPENCL_EXTENSION_DECLS = 59,

  MODULAR_CODEGEN_DECLS = 60,

  /// Record code for \#pragma align/pack options.
  ALIGN_PACK_PRAGMA_OPTIONS = 61,

  /// The stack of open #ifs/#ifdefs recorded in a preamble.
  PP_CONDITIONAL_STACK = 62,

  /// A table of skipped ranges within the preprocessing record.
  PPD_SKIPPED_RANGES = 63,

  /// Record code for the Decls to be checked for deferred diags.
  DECLS_TO_CHECK_FOR_DEFERRED_DIAGS = 64,

  /// Record code for \#pragma float_control options.
  FLOAT_CONTROL_PRAGMA_OPTIONS = 65,

  /// ID 66 used to be the list of included files.

  /// Record code for an unterminated \#pragma clang assume_nonnull begin
  /// recorded in a preamble.
  PP_ASSUME_NONNULL_LOC = 67,

  /// Record code for lexical and visible block for delayed namespace in
  /// reduced BMI.
  DELAYED_NAMESPACE_LEXICAL_VISIBLE_RECORD = 68,

  /// Record code for \#pragma clang unsafe_buffer_usage begin/end
  PP_UNSAFE_BUFFER_USAGE = 69,

  /// Record code for vtables to emit.
  VTABLES_TO_EMIT = 70,
};

/// Record types used within a source manager block.
enum SourceManagerRecordTypes {
  /// Describes a source location entry (SLocEntry) for a
  /// file.
  SM_SLOC_FILE_ENTRY = 1,

  /// Describes a source location entry (SLocEntry) for a
  /// buffer.
  SM_SLOC_BUFFER_ENTRY = 2,

  /// Describes a blob that contains the data for a buffer
  /// entry. This kind of record always directly follows a
  /// SM_SLOC_BUFFER_ENTRY record or a SM_SLOC_FILE_ENTRY with an
  /// overridden buffer.
  SM_SLOC_BUFFER_BLOB = 3,

  /// Describes a zlib-compressed blob that contains the data for
  /// a buffer entry.
  SM_SLOC_BUFFER_BLOB_COMPRESSED = 4,

  /// Describes a source location entry (SLocEntry) for a
  /// macro expansion.
  SM_SLOC_EXPANSION_ENTRY = 5
};

/// Record types used within a preprocessor block.
enum PreprocessorRecordTypes {
  // The macros in the PP section are a PP_MACRO_* instance followed by a
  // list of PP_TOKEN instances for each token in the definition.

  /// An object-like macro definition.
  /// [PP_MACRO_OBJECT_LIKE, IdentInfoID, SLoc, IsUsed]
  PP_MACRO_OBJECT_LIKE = 1,

  /// A function-like macro definition.
  /// [PP_MACRO_FUNCTION_LIKE, \<ObjectLikeStuff>, IsC99Varargs,
  /// IsGNUVarars, NumArgs, ArgIdentInfoID* ]
  PP_MACRO_FUNCTION_LIKE = 2,

  /// Describes one token.
  /// [PP_TOKEN, SLoc, Length, IdentInfoID, Kind, Flags]
  PP_TOKEN = 3,

  /// The macro directives history for a particular identifier.
  PP_MACRO_DIRECTIVE_HISTORY = 4,

  /// A macro directive exported by a module.
  /// [PP_MODULE_MACRO, SubmoduleID, MacroID, (Overridden SubmoduleID)*]
  PP_MODULE_MACRO = 5,
};

/// Record types used within a preprocessor detail block.
enum PreprocessorDetailRecordTypes {
  /// Describes a macro expansion within the preprocessing record.
  PPD_MACRO_EXPANSION = 0,

  /// Describes a macro definition within the preprocessing record.
  PPD_MACRO_DEFINITION = 1,

  /// Describes an inclusion directive within the preprocessing
  /// record.
  PPD_INCLUSION_DIRECTIVE = 2
};

/// Record types used within a submodule description block.
enum SubmoduleRecordTypes {
  /// Metadata for submodules as a whole.
  SUBMODULE_METADATA = 0,

  /// Defines the major attributes of a submodule, including its
  /// name and parent.
  SUBMODULE_DEFINITION = 1,

  /// Specifies the umbrella header used to create this module,
  /// if any.
  SUBMODULE_UMBRELLA_HEADER = 2,

  /// Specifies a header that falls into this (sub)module.
  SUBMODULE_HEADER = 3,

  /// Specifies a top-level header that falls into this (sub)module.
  SUBMODULE_TOPHEADER = 4,

  /// Specifies an umbrella directory.
  SUBMODULE_UMBRELLA_DIR = 5,

  /// Specifies the submodules that are imported by this
  /// submodule.
  SUBMODULE_IMPORTS = 6,

  /// Specifies the submodules that are re-exported from this
  /// submodule.
  SUBMODULE_EXPORTS = 7,

  /// Specifies a required feature.
  SUBMODULE_REQUIRES = 8,

  /// Specifies a header that has been explicitly excluded
  /// from this submodule.
  SUBMODULE_EXCLUDED_HEADER = 9,

  /// Specifies a library or framework to link against.
  SUBMODULE_LINK_LIBRARY = 10,

  /// Specifies a configuration macro for this module.
  SUBMODULE_CONFIG_MACRO = 11,

  /// Specifies a conflict with another module.
  SUBMODULE_CONFLICT = 12,

  /// Specifies a header that is private to this submodule.
  SUBMODULE_PRIVATE_HEADER = 13,

  /// Specifies a header that is part of the module but must be
  /// textually included.
  SUBMODULE_TEXTUAL_HEADER = 14,

  /// Specifies a header that is private to this submodule but
  /// must be textually included.
  SUBMODULE_PRIVATE_TEXTUAL_HEADER = 15,

  /// Specifies some declarations with initializers that must be
  /// emitted to initialize the module.
  SUBMODULE_INITIALIZERS = 16,

  /// Specifies the name of the module that will eventually
  /// re-export the entities in this module.
  SUBMODULE_EXPORT_AS = 17,

  /// Specifies affecting modules that were not imported.
  SUBMODULE_AFFECTING_MODULES = 18,
};

/// Record types used within a comments block.
enum CommentRecordTypes { COMMENTS_RAW_COMMENT = 0 };

/// \defgroup ASTAST AST file AST constants
///
/// The constants in this group describe various components of the
/// abstract syntax tree within an AST file.
///
/// @{

/// Predefined type IDs.
///
/// These type IDs correspond to predefined types in the AST
/// context, such as built-in types (int) and special place-holder
/// types (the \<overload> and \<dependent> type markers). Such
/// types are never actually serialized, since they will be built
/// by the AST context when it is created.
enum PredefinedTypeIDs {
  /// The NULL type.
  PREDEF_TYPE_NULL_ID = 0,

  /// The void type.
  PREDEF_TYPE_VOID_ID = 1,

  /// The 'bool' or '_Bool' type.
  PREDEF_TYPE_BOOL_ID = 2,

  /// The 'char' type, when it is unsigned.
  PREDEF_TYPE_CHAR_U_ID = 3,

  /// The 'unsigned char' type.
  PREDEF_TYPE_UCHAR_ID = 4,

  /// The 'unsigned short' type.
  PREDEF_TYPE_USHORT_ID = 5,

  /// The 'unsigned int' type.
  PREDEF_TYPE_UINT_ID = 6,

  /// The 'unsigned long' type.
  PREDEF_TYPE_ULONG_ID = 7,

  /// The 'unsigned long long' type.
  PREDEF_TYPE_ULONGLONG_ID = 8,

  /// The 'char' type, when it is signed.
  PREDEF_TYPE_CHAR_S_ID = 9,

  /// The 'signed char' type.
  PREDEF_TYPE_SCHAR_ID = 10,

  /// The C++ 'wchar_t' type.
  PREDEF_TYPE_WCHAR_ID = 11,

  /// The (signed) 'short' type.
  PREDEF_TYPE_SHORT_ID = 12,

  /// The (signed) 'int' type.
  PREDEF_TYPE_INT_ID = 13,

  /// The (signed) 'long' type.
  PREDEF_TYPE_LONG_ID = 14,

  /// The (signed) 'long long' type.
  PREDEF_TYPE_LONGLONG_ID = 15,

  /// The 'float' type.
  PREDEF_TYPE_FLOAT_ID = 16,

  /// The 'double' type.
  PREDEF_TYPE_DOUBLE_ID = 17,

  /// The 'long double' type.
  PREDEF_TYPE_LONGDOUBLE_ID = 18,

  /// The placeholder type for overloaded function sets.
  PREDEF_TYPE_OVERLOAD_ID = 19,

  /// The placeholder type for dependent types.
  PREDEF_TYPE_DEPENDENT_ID = 20,

  /// The '__uint128_t' type.
  PREDEF_TYPE_UINT128_ID = 21,

  /// The '__int128_t' type.
  PREDEF_TYPE_INT128_ID = 22,

  /// The type of 'nullptr'.
  PREDEF_TYPE_NULLPTR_ID = 23,

  /// The C++ 'char16_t' type.
  PREDEF_TYPE_CHAR16_ID = 24,

  /// The C++ 'char32_t' type.
  PREDEF_TYPE_CHAR32_ID = 25,

  /// The ObjC 'id' type.
  PREDEF_TYPE_OBJC_ID = 26,

  /// The ObjC 'Class' type.
  PREDEF_TYPE_OBJC_CLASS = 27,

  /// The ObjC 'SEL' type.
  PREDEF_TYPE_OBJC_SEL = 28,

  /// The 'unknown any' placeholder type.
  PREDEF_TYPE_UNKNOWN_ANY = 29,

  /// The placeholder type for bound member functions.
  PREDEF_TYPE_BOUND_MEMBER = 30,

  /// The "auto" deduction type.
  PREDEF_TYPE_AUTO_DEDUCT = 31,

  /// The "auto &&" deduction type.
  PREDEF_TYPE_AUTO_RREF_DEDUCT = 32,

  /// The OpenCL 'half' / ARM NEON __fp16 type.
  PREDEF_TYPE_HALF_ID = 33,

  /// ARC's unbridged-cast placeholder type.
  PREDEF_TYPE_ARC_UNBRIDGED_CAST = 34,

  /// The pseudo-object placeholder type.
  PREDEF_TYPE_PSEUDO_OBJECT = 35,

  /// The placeholder type for builtin functions.
  PREDEF_TYPE_BUILTIN_FN = 36,

  /// OpenCL event type.
  PREDEF_TYPE_EVENT_ID = 37,

  /// OpenCL clk event type.
  PREDEF_TYPE_CLK_EVENT_ID = 38,

  /// OpenCL sampler type.
  PREDEF_TYPE_SAMPLER_ID = 39,

  /// OpenCL queue type.
  PREDEF_TYPE_QUEUE_ID = 40,

  /// OpenCL reserve_id type.
  PREDEF_TYPE_RESERVE_ID_ID = 41,

  /// The placeholder type for an array section.
  PREDEF_TYPE_ARRAY_SECTION = 42,

  /// The '__float128' type
  PREDEF_TYPE_FLOAT128_ID = 43,

  /// The '_Float16' type
  PREDEF_TYPE_FLOAT16_ID = 44,

  /// The C++ 'char8_t' type.
  PREDEF_TYPE_CHAR8_ID = 45,

  /// \brief The 'short _Accum' type
  PREDEF_TYPE_SHORT_ACCUM_ID = 46,

  /// \brief The '_Accum' type
  PREDEF_TYPE_ACCUM_ID = 47,

  /// \brief The 'long _Accum' type
  PREDEF_TYPE_LONG_ACCUM_ID = 48,

  /// \brief The 'unsigned short _Accum' type
  PREDEF_TYPE_USHORT_ACCUM_ID = 49,

  /// \brief The 'unsigned _Accum' type
  PREDEF_TYPE_UACCUM_ID = 50,

  /// \brief The 'unsigned long _Accum' type
  PREDEF_TYPE_ULONG_ACCUM_ID = 51,

  /// \brief The 'short _Fract' type
  PREDEF_TYPE_SHORT_FRACT_ID = 52,

  /// \brief The '_Fract' type
  PREDEF_TYPE_FRACT_ID = 53,

  /// \brief The 'long _Fract' type
  PREDEF_TYPE_LONG_FRACT_ID = 54,

  /// \brief The 'unsigned short _Fract' type
  PREDEF_TYPE_USHORT_FRACT_ID = 55,

  /// \brief The 'unsigned _Fract' type
  PREDEF_TYPE_UFRACT_ID = 56,

  /// \brief The 'unsigned long _Fract' type
  PREDEF_TYPE_ULONG_FRACT_ID = 57,

  /// \brief The '_Sat short _Accum' type
  PREDEF_TYPE_SAT_SHORT_ACCUM_ID = 58,

  /// \brief The '_Sat _Accum' type
  PREDEF_TYPE_SAT_ACCUM_ID = 59,

  /// \brief The '_Sat long _Accum' type
  PREDEF_TYPE_SAT_LONG_ACCUM_ID = 60,

  /// \brief The '_Sat unsigned short _Accum' type
  PREDEF_TYPE_SAT_USHORT_ACCUM_ID = 61,

  /// \brief The '_Sat unsigned _Accum' type
  PREDEF_TYPE_SAT_UACCUM_ID = 62,

  /// \brief The '_Sat unsigned long _Accum' type
  PREDEF_TYPE_SAT_ULONG_ACCUM_ID = 63,

  /// \brief The '_Sat short _Fract' type
  PREDEF_TYPE_SAT_SHORT_FRACT_ID = 64,

  /// \brief The '_Sat _Fract' type
  PREDEF_TYPE_SAT_FRACT_ID = 65,

  /// \brief The '_Sat long _Fract' type
  PREDEF_TYPE_SAT_LONG_FRACT_ID = 66,

  /// \brief The '_Sat unsigned short _Fract' type
  PREDEF_TYPE_SAT_USHORT_FRACT_ID = 67,

  /// \brief The '_Sat unsigned _Fract' type
  PREDEF_TYPE_SAT_UFRACT_ID = 68,

  /// \brief The '_Sat unsigned long _Fract' type
  PREDEF_TYPE_SAT_ULONG_FRACT_ID = 69,

  /// The placeholder type for OpenMP array shaping operation.
  PREDEF_TYPE_OMP_ARRAY_SHAPING = 70,

  /// The placeholder type for OpenMP iterator expression.
  PREDEF_TYPE_OMP_ITERATOR = 71,

  /// A placeholder type for incomplete matrix index operations.
  PREDEF_TYPE_INCOMPLETE_MATRIX_IDX = 72,

  /// \brief The '__bf16' type
  PREDEF_TYPE_BFLOAT16_ID = 73,

  /// \brief The '__ibm128' type
  PREDEF_TYPE_IBM128_ID = 74,

/// OpenCL image types with auto numeration
#define IMAGE_TYPE(ImgType, Id, SingletonId, Access, Suffix)                   \
  PREDEF_TYPE_##Id##_ID,
#include "clang/Basic/OpenCLImageTypes.def"
/// \brief OpenCL extension types with auto numeration
#define EXT_OPAQUE_TYPE(ExtType, Id, Ext) PREDEF_TYPE_##Id##_ID,
#include "clang/Basic/OpenCLExtensionTypes.def"
// \brief SVE types with auto numeration
#define SVE_TYPE(Name, Id, SingletonId) PREDEF_TYPE_##Id##_ID,
#include "clang/Basic/AArch64SVEACLETypes.def"
// \brief  PowerPC MMA types with auto numeration
#define PPC_VECTOR_TYPE(Name, Id, Size) PREDEF_TYPE_##Id##_ID,
#include "clang/Basic/PPCTypes.def"
// \brief RISC-V V types with auto numeration
#define RVV_TYPE(Name, Id, SingletonId) PREDEF_TYPE_##Id##_ID,
#include "clang/Basic/RISCVVTypes.def"
// \brief WebAssembly reference types with auto numeration
#define WASM_TYPE(Name, Id, SingletonId) PREDEF_TYPE_##Id##_ID,
#include "clang/Basic/WebAssemblyReferenceTypes.def"
// \brief AMDGPU types with auto numeration
#define AMDGPU_TYPE(Name, Id, SingletonId) PREDEF_TYPE_##Id##_ID,
#include "clang/Basic/AMDGPUTypes.def"

  /// The placeholder type for unresolved templates.
  PREDEF_TYPE_UNRESOLVED_TEMPLATE,
  // Sentinel value. Considered a predefined type but not useable as one.
  PREDEF_TYPE_LAST_ID
};

/// The number of predefined type IDs that are reserved for
/// the PREDEF_TYPE_* constants.
///
/// Type IDs for non-predefined types will start at
/// NUM_PREDEF_TYPE_IDs.
const unsigned NUM_PREDEF_TYPE_IDS = 504;

// Ensure we do not overrun the predefined types we reserved
// in the enum PredefinedTypeIDs above.
static_assert(PREDEF_TYPE_LAST_ID < NUM_PREDEF_TYPE_IDS,
              "Too many enumerators in PredefinedTypeIDs. Review the value of "
              "NUM_PREDEF_TYPE_IDS");

/// Record codes for each kind of type.
///
/// These constants describe the type records that can occur within a
/// block identified by DECLTYPES_BLOCK_ID in the AST file. Each
/// constant describes a record for a specific type class in the
/// AST. Note that DeclCode values share this code space.
enum TypeCode {
#define TYPE_BIT_CODE(CLASS_ID, CODE_ID, CODE_VALUE)                           \
  TYPE_##CODE_ID = CODE_VALUE,
#include "clang/Serialization/TypeBitCodes.def"

  /// An ExtQualType record.
  TYPE_EXT_QUAL = 1
};

/// The type IDs for special types constructed by semantic
/// analysis.
///
/// The constants in this enumeration are indices into the
/// SPECIAL_TYPES record.
enum SpecialTypeIDs {
  /// CFConstantString type
  SPECIAL_TYPE_CF_CONSTANT_STRING = 0,

  /// C FILE typedef type
  SPECIAL_TYPE_FILE = 1,

  /// C jmp_buf typedef type
  SPECIAL_TYPE_JMP_BUF = 2,

  /// C sigjmp_buf typedef type
  SPECIAL_TYPE_SIGJMP_BUF = 3,

  /// Objective-C "id" redefinition type
  SPECIAL_TYPE_OBJC_ID_REDEFINITION = 4,

  /// Objective-C "Class" redefinition type
  SPECIAL_TYPE_OBJC_CLASS_REDEFINITION = 5,

  /// Objective-C "SEL" redefinition type
  SPECIAL_TYPE_OBJC_SEL_REDEFINITION = 6,

  /// C ucontext_t typedef type
  SPECIAL_TYPE_UCONTEXT_T = 7
};

/// The number of special type IDs.
const unsigned NumSpecialTypeIDs = 8;

/// Record of updates for a declaration that was modified after
/// being deserialized. This can occur within DECLTYPES_BLOCK_ID.
const unsigned int DECL_UPDATES = 49;

/// Record code for a list of local redeclarations of a declaration.
/// This can occur within DECLTYPES_BLOCK_ID.
const unsigned int LOCAL_REDECLARATIONS = 50;

/// Record codes for each kind of declaration.
///
/// These constants describe the declaration records that can occur within
/// a declarations block (identified by DECLTYPES_BLOCK_ID). Each
/// constant describes a record for a specific declaration class
/// in the AST. Note that TypeCode values share this code space.
enum DeclCode {
  /// A TypedefDecl record.
  DECL_TYPEDEF = 51,
  /// A TypeAliasDecl record.

  DECL_TYPEALIAS,

  /// An EnumDecl record.
  DECL_ENUM,

  /// A RecordDecl record.
  DECL_RECORD,

  /// An EnumConstantDecl record.
  DECL_ENUM_CONSTANT,

  /// A FunctionDecl record.
  DECL_FUNCTION,

  /// A ObjCMethodDecl record.
  DECL_OBJC_METHOD,

  /// A ObjCInterfaceDecl record.
  DECL_OBJC_INTERFACE,

  /// A ObjCProtocolDecl record.
  DECL_OBJC_PROTOCOL,

  /// A ObjCIvarDecl record.
  DECL_OBJC_IVAR,

  /// A ObjCAtDefsFieldDecl record.
  DECL_OBJC_AT_DEFS_FIELD,

  /// A ObjCCategoryDecl record.
  DECL_OBJC_CATEGORY,

  /// A ObjCCategoryImplDecl record.
  DECL_OBJC_CATEGORY_IMPL,

  /// A ObjCImplementationDecl record.
  DECL_OBJC_IMPLEMENTATION,

  /// A ObjCCompatibleAliasDecl record.
  DECL_OBJC_COMPATIBLE_ALIAS,

  /// A ObjCPropertyDecl record.
  DECL_OBJC_PROPERTY,

  /// A ObjCPropertyImplDecl record.
  DECL_OBJC_PROPERTY_IMPL,

  /// A FieldDecl record.
  DECL_FIELD,

  /// A MSPropertyDecl record.
  DECL_MS_PROPERTY,

  /// A MSGuidDecl record.
  DECL_MS_GUID,

  /// A TemplateParamObjectDecl record.
  DECL_TEMPLATE_PARAM_OBJECT,

  /// A VarDecl record.
  DECL_VAR,

  /// An ImplicitParamDecl record.
  DECL_IMPLICIT_PARAM,

  /// A ParmVarDecl record.
  DECL_PARM_VAR,

  /// A DecompositionDecl record.
  DECL_DECOMPOSITION,

  /// A BindingDecl record.
  DECL_BINDING,

  /// A FileScopeAsmDecl record.
  DECL_FILE_SCOPE_ASM,

  /// A TopLevelStmtDecl record.
  DECL_TOP_LEVEL_STMT_DECL,

  /// A BlockDecl record.
  DECL_BLOCK,

  /// A CapturedDecl record.
  DECL_CAPTURED,

  /// A record that stores the set of declarations that are
  /// lexically stored within a given DeclContext.
  ///
  /// The record itself is a blob that is an array of declaration IDs,
  /// in the order in which those declarations were added to the
  /// declaration context. This data is used when iterating over
  /// the contents of a DeclContext, e.g., via
  /// DeclContext::decls_begin() and DeclContext::decls_end().
  DECL_CONTEXT_LEXICAL,

  /// A record that stores the set of declarations that are
  /// visible from a given DeclContext.
  ///
  /// The record itself stores a set of mappings, each of which
  /// associates a declaration name with one or more declaration
  /// IDs. This data is used when performing qualified name lookup
  /// into a DeclContext via DeclContext::lookup.
  DECL_CONTEXT_VISIBLE,

  /// A LabelDecl record.
  DECL_LABEL,

  /// A NamespaceDecl record.
  DECL_NAMESPACE,

  /// A NamespaceAliasDecl record.
  DECL_NAMESPACE_ALIAS,

  /// A UsingDecl record.
  DECL_USING,

  /// A UsingEnumDecl record.
  DECL_USING_ENUM,

  /// A UsingPackDecl record.
  DECL_USING_PACK,

  /// A UsingShadowDecl record.
  DECL_USING_SHADOW,

  /// A ConstructorUsingShadowDecl record.
  DECL_CONSTRUCTOR_USING_SHADOW,

  /// A UsingDirecitveDecl record.
  DECL_USING_DIRECTIVE,

  /// An UnresolvedUsingValueDecl record.
  DECL_UNRESOLVED_USING_VALUE,

  /// An UnresolvedUsingTypenameDecl record.
  DECL_UNRESOLVED_USING_TYPENAME,

  /// A LinkageSpecDecl record.
  DECL_LINKAGE_SPEC,

  /// An ExportDecl record.
  DECL_EXPORT,

  /// A CXXRecordDecl record.
  DECL_CXX_RECORD,

  /// A CXXDeductionGuideDecl record.
  DECL_CXX_DEDUCTION_GUIDE,

  /// A CXXMethodDecl record.
  DECL_CXX_METHOD,

  /// A CXXConstructorDecl record.
  DECL_CXX_CONSTRUCTOR,

  /// A CXXDestructorDecl record.
  DECL_CXX_DESTRUCTOR,

  /// A CXXConversionDecl record.
  DECL_CXX_CONVERSION,

  /// An AccessSpecDecl record.
  DECL_ACCESS_SPEC,

  /// A FriendDecl record.
  DECL_FRIEND,

  /// A FriendTemplateDecl record.
  DECL_FRIEND_TEMPLATE,

  /// A ClassTemplateDecl record.
  DECL_CLASS_TEMPLATE,

  /// A ClassTemplateSpecializationDecl record.
  DECL_CLASS_TEMPLATE_SPECIALIZATION,

  /// A ClassTemplatePartialSpecializationDecl record.
  DECL_CLASS_TEMPLATE_PARTIAL_SPECIALIZATION,

  /// A VarTemplateDecl record.
  DECL_VAR_TEMPLATE,

  /// A VarTemplateSpecializationDecl record.
  DECL_VAR_TEMPLATE_SPECIALIZATION,

  /// A VarTemplatePartialSpecializationDecl record.
  DECL_VAR_TEMPLATE_PARTIAL_SPECIALIZATION,

  /// A FunctionTemplateDecl record.
  DECL_FUNCTION_TEMPLATE,

  /// A TemplateTypeParmDecl record.
  DECL_TEMPLATE_TYPE_PARM,

  /// A NonTypeTemplateParmDecl record.
  DECL_NON_TYPE_TEMPLATE_PARM,

  /// A TemplateTemplateParmDecl record.
  DECL_TEMPLATE_TEMPLATE_PARM,

  /// A TypeAliasTemplateDecl record.
  DECL_TYPE_ALIAS_TEMPLATE,

  /// \brief A ConceptDecl record.
  DECL_CONCEPT,

  /// An UnresolvedUsingIfExistsDecl record.
  DECL_UNRESOLVED_USING_IF_EXISTS,

  /// \brief A StaticAssertDecl record.
  DECL_STATIC_ASSERT,

  /// A record containing CXXBaseSpecifiers.
  DECL_CXX_BASE_SPECIFIERS,

  /// A record containing CXXCtorInitializers.
  DECL_CXX_CTOR_INITIALIZERS,

  /// A IndirectFieldDecl record.
  DECL_INDIRECTFIELD,

  /// A NonTypeTemplateParmDecl record that stores an expanded
  /// non-type template parameter pack.
  DECL_EXPANDED_NON_TYPE_TEMPLATE_PARM_PACK,

  /// A TemplateTemplateParmDecl record that stores an expanded
  /// template template parameter pack.
  DECL_EXPANDED_TEMPLATE_TEMPLATE_PARM_PACK,

  /// An ImportDecl recording a module import.
  DECL_IMPORT,

  /// An OMPThreadPrivateDecl record.
  DECL_OMP_THREADPRIVATE,

  /// An OMPRequiresDecl record.
  DECL_OMP_REQUIRES,

  /// An OMPAllocateDcl record.
  DECL_OMP_ALLOCATE,

  /// An EmptyDecl record.
  DECL_EMPTY,

  /// An LifetimeExtendedTemporaryDecl record.
  DECL_LIFETIME_EXTENDED_TEMPORARY,

  /// A RequiresExprBodyDecl record.
  DECL_REQUIRES_EXPR_BODY,

  /// An ObjCTypeParamDecl record.
  DECL_OBJC_TYPE_PARAM,

  /// An OMPCapturedExprDecl record.
  DECL_OMP_CAPTUREDEXPR,

  /// A PragmaCommentDecl record.
  DECL_PRAGMA_COMMENT,

  /// A PragmaDetectMismatchDecl record.
  DECL_PRAGMA_DETECT_MISMATCH,

  /// An OMPDeclareMapperDecl record.
  DECL_OMP_DECLARE_MAPPER,

  /// An OMPDeclareReductionDecl record.
  DECL_OMP_DECLARE_REDUCTION,

  /// A UnnamedGlobalConstantDecl record.
  DECL_UNNAMED_GLOBAL_CONSTANT,

  /// A HLSLBufferDecl record.
  DECL_HLSL_BUFFER,

  /// An ImplicitConceptSpecializationDecl record.
  DECL_IMPLICIT_CONCEPT_SPECIALIZATION,

  DECL_LAST = DECL_IMPLICIT_CONCEPT_SPECIALIZATION
};

/// Record codes for each kind of statement or expression.
///
/// These constants describe the records that describe statements
/// or expressions. These records  occur within type and declarations
/// block, so they begin with record values of 128.  Each constant
/// describes a record for a specific statement or expression class in the
/// AST.
enum StmtCode {
  /// A marker record that indicates that we are at the end
  /// of an expression.
  STMT_STOP = DECL_LAST + 1,

  /// A NULL expression.
  STMT_NULL_PTR,

  /// A reference to a previously [de]serialized Stmt record.
  STMT_REF_PTR,

  /// A NullStmt record.
  STMT_NULL,

  /// A CompoundStmt record.
  STMT_COMPOUND,

  /// A CaseStmt record.
  STMT_CASE,

  /// A DefaultStmt record.
  STMT_DEFAULT,

  /// A LabelStmt record.
  STMT_LABEL,

  /// An AttributedStmt record.
  STMT_ATTRIBUTED,

  /// An IfStmt record.
  STMT_IF,

  /// A SwitchStmt record.
  STMT_SWITCH,

  /// A WhileStmt record.
  STMT_WHILE,

  /// A DoStmt record.
  STMT_DO,

  /// A ForStmt record.
  STMT_FOR,

  /// A GotoStmt record.
  STMT_GOTO,

  /// An IndirectGotoStmt record.
  STMT_INDIRECT_GOTO,

  /// A ContinueStmt record.
  STMT_CONTINUE,

  /// A BreakStmt record.
  STMT_BREAK,

  /// A ReturnStmt record.
  STMT_RETURN,

  /// A DeclStmt record.
  STMT_DECL,

  /// A CapturedStmt record.
  STMT_CAPTURED,

  /// A GCC-style AsmStmt record.
  STMT_GCCASM,

  /// A MS-style AsmStmt record.
  STMT_MSASM,

  /// A constant expression context.
  EXPR_CONSTANT,

  /// A PredefinedExpr record.
  EXPR_PREDEFINED,

  /// A DeclRefExpr record.
  EXPR_DECL_REF,

  /// An IntegerLiteral record.
  EXPR_INTEGER_LITERAL,

  /// A FloatingLiteral record.
  EXPR_FLOATING_LITERAL,

  /// An ImaginaryLiteral record.
  EXPR_IMAGINARY_LITERAL,

  /// A StringLiteral record.
  EXPR_STRING_LITERAL,

  /// A CharacterLiteral record.
  EXPR_CHARACTER_LITERAL,

  /// A ParenExpr record.
  EXPR_PAREN,

  /// A ParenListExpr record.
  EXPR_PAREN_LIST,

  /// A UnaryOperator record.
  EXPR_UNARY_OPERATOR,

  /// An OffsetOfExpr record.
  EXPR_OFFSETOF,

  /// A SizefAlignOfExpr record.
  EXPR_SIZEOF_ALIGN_OF,

  /// An ArraySubscriptExpr record.
  EXPR_ARRAY_SUBSCRIPT,

  /// An MatrixSubscriptExpr record.
  EXPR_MATRIX_SUBSCRIPT,

  /// A CallExpr record.
  EXPR_CALL,

  /// A MemberExpr record.
  EXPR_MEMBER,

  /// A BinaryOperator record.
  EXPR_BINARY_OPERATOR,

  /// A CompoundAssignOperator record.
  EXPR_COMPOUND_ASSIGN_OPERATOR,

  /// A ConditionOperator record.
  EXPR_CONDITIONAL_OPERATOR,

  /// An ImplicitCastExpr record.
  EXPR_IMPLICIT_CAST,

  /// A CStyleCastExpr record.
  EXPR_CSTYLE_CAST,

  /// A CompoundLiteralExpr record.
  EXPR_COMPOUND_LITERAL,

  /// An ExtVectorElementExpr record.
  EXPR_EXT_VECTOR_ELEMENT,

  /// An InitListExpr record.
  EXPR_INIT_LIST,

  /// A DesignatedInitExpr record.
  EXPR_DESIGNATED_INIT,

  /// A DesignatedInitUpdateExpr record.
  EXPR_DESIGNATED_INIT_UPDATE,

  /// An NoInitExpr record.
  EXPR_NO_INIT,

  /// An ArrayInitLoopExpr record.
  EXPR_ARRAY_INIT_LOOP,

  /// An ArrayInitIndexExpr record.
  EXPR_ARRAY_INIT_INDEX,

  /// An ImplicitValueInitExpr record.
  EXPR_IMPLICIT_VALUE_INIT,

  /// A VAArgExpr record.
  EXPR_VA_ARG,

  /// An AddrLabelExpr record.
  EXPR_ADDR_LABEL,

  /// A StmtExpr record.
  EXPR_STMT,

  /// A ChooseExpr record.
  EXPR_CHOOSE,

  /// A GNUNullExpr record.
  EXPR_GNU_NULL,

  /// A SourceLocExpr record.
  EXPR_SOURCE_LOC,

  /// A EmbedExpr record.
  EXPR_BUILTIN_PP_EMBED,

  /// A ShuffleVectorExpr record.
  EXPR_SHUFFLE_VECTOR,

  /// A ConvertVectorExpr record.
  EXPR_CONVERT_VECTOR,

  /// BlockExpr
  EXPR_BLOCK,

  /// A GenericSelectionExpr record.
  EXPR_GENERIC_SELECTION,

  /// A PseudoObjectExpr record.
  EXPR_PSEUDO_OBJECT,

  /// An AtomicExpr record.
  EXPR_ATOMIC,

  /// A RecoveryExpr record.
  EXPR_RECOVERY,

  // Objective-C

  /// An ObjCStringLiteral record.
  EXPR_OBJC_STRING_LITERAL,

  EXPR_OBJC_BOXED_EXPRESSION,
  EXPR_OBJC_ARRAY_LITERAL,
  EXPR_OBJC_DICTIONARY_LITERAL,

  /// An ObjCEncodeExpr record.
  EXPR_OBJC_ENCODE,

  /// An ObjCSelectorExpr record.
  EXPR_OBJC_SELECTOR_EXPR,

  /// An ObjCProtocolExpr record.
  EXPR_OBJC_PROTOCOL_EXPR,

  /// An ObjCIvarRefExpr record.
  EXPR_OBJC_IVAR_REF_EXPR,

  /// An ObjCPropertyRefExpr record.
  EXPR_OBJC_PROPERTY_REF_EXPR,

  /// An ObjCSubscriptRefExpr record.
  EXPR_OBJC_SUBSCRIPT_REF_EXPR,

  /// UNUSED
  EXPR_OBJC_KVC_REF_EXPR,

  /// An ObjCMessageExpr record.
  EXPR_OBJC_MESSAGE_EXPR,

  /// An ObjCIsa Expr record.
  EXPR_OBJC_ISA,

  /// An ObjCIndirectCopyRestoreExpr record.
  EXPR_OBJC_INDIRECT_COPY_RESTORE,

  /// An ObjCForCollectionStmt record.
  STMT_OBJC_FOR_COLLECTION,

  /// An ObjCAtCatchStmt record.
  STMT_OBJC_CATCH,

  /// An ObjCAtFinallyStmt record.
  STMT_OBJC_FINALLY,

  /// An ObjCAtTryStmt record.
  STMT_OBJC_AT_TRY,

  /// An ObjCAtSynchronizedStmt record.
  STMT_OBJC_AT_SYNCHRONIZED,

  /// An ObjCAtThrowStmt record.
  STMT_OBJC_AT_THROW,

  /// An ObjCAutoreleasePoolStmt record.
  STMT_OBJC_AUTORELEASE_POOL,

  /// An ObjCBoolLiteralExpr record.
  EXPR_OBJC_BOOL_LITERAL,

  /// An ObjCAvailabilityCheckExpr record.
  EXPR_OBJC_AVAILABILITY_CHECK,

  // C++

  /// A CXXCatchStmt record.
  STMT_CXX_CATCH,

  /// A CXXTryStmt record.
  STMT_CXX_TRY,
  /// A CXXForRangeStmt record.

  STMT_CXX_FOR_RANGE,

  /// A CXXOperatorCallExpr record.
  EXPR_CXX_OPERATOR_CALL,

  /// A CXXMemberCallExpr record.
  EXPR_CXX_MEMBER_CALL,

  /// A CXXRewrittenBinaryOperator record.
  EXPR_CXX_REWRITTEN_BINARY_OPERATOR,

  /// A CXXConstructExpr record.
  EXPR_CXX_CONSTRUCT,

  /// A CXXInheritedCtorInitExpr record.
  EXPR_CXX_INHERITED_CTOR_INIT,

  /// A CXXTemporaryObjectExpr record.
  EXPR_CXX_TEMPORARY_OBJECT,

  /// A CXXStaticCastExpr record.
  EXPR_CXX_STATIC_CAST,

  /// A CXXDynamicCastExpr record.
  EXPR_CXX_DYNAMIC_CAST,

  /// A CXXReinterpretCastExpr record.
  EXPR_CXX_REINTERPRET_CAST,

  /// A CXXConstCastExpr record.
  EXPR_CXX_CONST_CAST,

  /// A CXXAddrspaceCastExpr record.
  EXPR_CXX_ADDRSPACE_CAST,

  /// A CXXFunctionalCastExpr record.
  EXPR_CXX_FUNCTIONAL_CAST,

  /// A BuiltinBitCastExpr record.
  EXPR_BUILTIN_BIT_CAST,

  /// A UserDefinedLiteral record.
  EXPR_USER_DEFINED_LITERAL,

  /// A CXXStdInitializerListExpr record.
  EXPR_CXX_STD_INITIALIZER_LIST,

  /// A CXXBoolLiteralExpr record.
  EXPR_CXX_BOOL_LITERAL,

  /// A CXXParenListInitExpr record.
  EXPR_CXX_PAREN_LIST_INIT,

  EXPR_CXX_NULL_PTR_LITERAL, // CXXNullPtrLiteralExpr
  EXPR_CXX_TYPEID_EXPR,      // CXXTypeidExpr (of expr).
  EXPR_CXX_TYPEID_TYPE,      // CXXTypeidExpr (of type).
  EXPR_CXX_THIS,             // CXXThisExpr
  EXPR_CXX_THROW,            // CXXThrowExpr
  EXPR_CXX_DEFAULT_ARG,      // CXXDefaultArgExpr
  EXPR_CXX_DEFAULT_INIT,     // CXXDefaultInitExpr
  EXPR_CXX_BIND_TEMPORARY,   // CXXBindTemporaryExpr

  EXPR_CXX_SCALAR_VALUE_INIT, // CXXScalarValueInitExpr
  EXPR_CXX_NEW,               // CXXNewExpr
  EXPR_CXX_DELETE,            // CXXDeleteExpr
  EXPR_CXX_PSEUDO_DESTRUCTOR, // CXXPseudoDestructorExpr

  EXPR_EXPR_WITH_CLEANUPS, // ExprWithCleanups

  EXPR_CXX_DEPENDENT_SCOPE_MEMBER,   // CXXDependentScopeMemberExpr
  EXPR_CXX_DEPENDENT_SCOPE_DECL_REF, // DependentScopeDeclRefExpr
  EXPR_CXX_UNRESOLVED_CONSTRUCT,     // CXXUnresolvedConstructExpr
  EXPR_CXX_UNRESOLVED_MEMBER,        // UnresolvedMemberExpr
  EXPR_CXX_UNRESOLVED_LOOKUP,        // UnresolvedLookupExpr

  EXPR_CXX_EXPRESSION_TRAIT, // ExpressionTraitExpr
  EXPR_CXX_NOEXCEPT,         // CXXNoexceptExpr

  EXPR_OPAQUE_VALUE,                // OpaqueValueExpr
  EXPR_BINARY_CONDITIONAL_OPERATOR, // BinaryConditionalOperator
  EXPR_TYPE_TRAIT,                  // TypeTraitExpr
  EXPR_ARRAY_TYPE_TRAIT,            // ArrayTypeTraitIntExpr

  EXPR_PACK_EXPANSION,                    // PackExpansionExpr
  EXPR_PACK_INDEXING,                     // PackIndexingExpr
  EXPR_SIZEOF_PACK,                       // SizeOfPackExpr
  EXPR_SUBST_NON_TYPE_TEMPLATE_PARM,      // SubstNonTypeTemplateParmExpr
  EXPR_SUBST_NON_TYPE_TEMPLATE_PARM_PACK, // SubstNonTypeTemplateParmPackExpr
  EXPR_FUNCTION_PARM_PACK,                // FunctionParmPackExpr
  EXPR_MATERIALIZE_TEMPORARY,             // MaterializeTemporaryExpr
  EXPR_CXX_FOLD,                          // CXXFoldExpr
  EXPR_CONCEPT_SPECIALIZATION,            // ConceptSpecializationExpr
  EXPR_REQUIRES,                          // RequiresExpr

  // CUDA
  EXPR_CUDA_KERNEL_CALL, // CUDAKernelCallExpr

  // OpenCL
  EXPR_ASTYPE, // AsTypeExpr

  // Microsoft
  EXPR_CXX_PROPERTY_REF_EXPR,       // MSPropertyRefExpr
  EXPR_CXX_PROPERTY_SUBSCRIPT_EXPR, // MSPropertySubscriptExpr
  EXPR_CXX_UUIDOF_EXPR,             // CXXUuidofExpr (of expr).
  EXPR_CXX_UUIDOF_TYPE,             // CXXUuidofExpr (of type).
  STMT_SEH_LEAVE,                   // SEHLeaveStmt
  STMT_SEH_EXCEPT,                  // SEHExceptStmt
  STMT_SEH_FINALLY,                 // SEHFinallyStmt
  STMT_SEH_TRY,                     // SEHTryStmt

  // OpenMP directives
  STMT_OMP_META_DIRECTIVE,
  STMT_OMP_CANONICAL_LOOP,
  STMT_OMP_PARALLEL_DIRECTIVE,
  STMT_OMP_SIMD_DIRECTIVE,
  STMT_OMP_TILE_DIRECTIVE,
  STMT_OMP_UNROLL_DIRECTIVE,
  STMT_OMP_REVERSE_DIRECTIVE,
  STMT_OMP_INTERCHANGE_DIRECTIVE,
  STMT_OMP_FOR_DIRECTIVE,
  STMT_OMP_FOR_SIMD_DIRECTIVE,
  STMT_OMP_SECTIONS_DIRECTIVE,
  STMT_OMP_SECTION_DIRECTIVE,
  STMT_OMP_SINGLE_DIRECTIVE,
  STMT_OMP_MASTER_DIRECTIVE,
  STMT_OMP_CRITICAL_DIRECTIVE,
  STMT_OMP_PARALLEL_FOR_DIRECTIVE,
  STMT_OMP_PARALLEL_FOR_SIMD_DIRECTIVE,
  STMT_OMP_PARALLEL_MASTER_DIRECTIVE,
  STMT_OMP_PARALLEL_MASKED_DIRECTIVE,
  STMT_OMP_PARALLEL_SECTIONS_DIRECTIVE,
  STMT_OMP_TASK_DIRECTIVE,
  STMT_OMP_TASKYIELD_DIRECTIVE,
  STMT_OMP_ERROR_DIRECTIVE,
  STMT_OMP_BARRIER_DIRECTIVE,
  STMT_OMP_TASKWAIT_DIRECTIVE,
  STMT_OMP_FLUSH_DIRECTIVE,
  STMT_OMP_DEPOBJ_DIRECTIVE,
  STMT_OMP_SCAN_DIRECTIVE,
  STMT_OMP_ORDERED_DIRECTIVE,
  STMT_OMP_ATOMIC_DIRECTIVE,
  STMT_OMP_TARGET_DIRECTIVE,
  STMT_OMP_TARGET_DATA_DIRECTIVE,
  STMT_OMP_TARGET_ENTER_DATA_DIRECTIVE,
  STMT_OMP_TARGET_EXIT_DATA_DIRECTIVE,
  STMT_OMP_TARGET_PARALLEL_DIRECTIVE,
  STMT_OMP_TARGET_PARALLEL_FOR_DIRECTIVE,
  STMT_OMP_TEAMS_DIRECTIVE,
  STMT_OMP_TASKGROUP_DIRECTIVE,
  STMT_OMP_CANCELLATION_POINT_DIRECTIVE,
  STMT_OMP_CANCEL_DIRECTIVE,
  STMT_OMP_TASKLOOP_DIRECTIVE,
  STMT_OMP_TASKLOOP_SIMD_DIRECTIVE,
  STMT_OMP_MASTER_TASKLOOP_DIRECTIVE,
  STMT_OMP_MASTER_TASKLOOP_SIMD_DIRECTIVE,
  STMT_OMP_PARALLEL_MASTER_TASKLOOP_DIRECTIVE,
  STMT_OMP_PARALLEL_MASTER_TASKLOOP_SIMD_DIRECTIVE,
  STMT_OMP_MASKED_TASKLOOP_DIRECTIVE,
  STMT_OMP_MASKED_TASKLOOP_SIMD_DIRECTIVE,
  STMT_OMP_PARALLEL_MASKED_TASKLOOP_DIRECTIVE,
  STMT_OMP_PARALLEL_MASKED_TASKLOOP_SIMD_DIRECTIVE,
  STMT_OMP_DISTRIBUTE_DIRECTIVE,
  STMT_OMP_TARGET_UPDATE_DIRECTIVE,
  STMT_OMP_DISTRIBUTE_PARALLEL_FOR_DIRECTIVE,
  STMT_OMP_DISTRIBUTE_PARALLEL_FOR_SIMD_DIRECTIVE,
  STMT_OMP_DISTRIBUTE_SIMD_DIRECTIVE,
  STMT_OMP_TARGET_PARALLEL_FOR_SIMD_DIRECTIVE,
  STMT_OMP_TARGET_SIMD_DIRECTIVE,
  STMT_OMP_TEAMS_DISTRIBUTE_DIRECTIVE,
  STMT_OMP_TEAMS_DISTRIBUTE_SIMD_DIRECTIVE,
  STMT_OMP_TEAMS_DISTRIBUTE_PARALLEL_FOR_SIMD_DIRECTIVE,
  STMT_OMP_TEAMS_DISTRIBUTE_PARALLEL_FOR_DIRECTIVE,
  STMT_OMP_TARGET_TEAMS_DIRECTIVE,
  STMT_OMP_TARGET_TEAMS_DISTRIBUTE_DIRECTIVE,
  STMT_OMP_TARGET_TEAMS_DISTRIBUTE_PARALLEL_FOR_DIRECTIVE,
  STMT_OMP_TARGET_TEAMS_DISTRIBUTE_PARALLEL_FOR_SIMD_DIRECTIVE,
  STMT_OMP_TARGET_TEAMS_DISTRIBUTE_SIMD_DIRECTIVE,
  STMT_OMP_SCOPE_DIRECTIVE,
  STMT_OMP_INTEROP_DIRECTIVE,
  STMT_OMP_DISPATCH_DIRECTIVE,
  STMT_OMP_MASKED_DIRECTIVE,
  STMT_OMP_GENERIC_LOOP_DIRECTIVE,
  STMT_OMP_TEAMS_GENERIC_LOOP_DIRECTIVE,
  STMT_OMP_TARGET_TEAMS_GENERIC_LOOP_DIRECTIVE,
  STMT_OMP_PARALLEL_GENERIC_LOOP_DIRECTIVE,
  STMT_OMP_TARGET_PARALLEL_GENERIC_LOOP_DIRECTIVE,
  EXPR_ARRAY_SECTION,
  EXPR_OMP_ARRAY_SHAPING,
  EXPR_OMP_ITERATOR,

  // ARC
  EXPR_OBJC_BRIDGED_CAST, // ObjCBridgedCastExpr

  STMT_MS_DEPENDENT_EXISTS, // MSDependentExistsStmt
  EXPR_LAMBDA,              // LambdaExpr
  STMT_COROUTINE_BODY,
  STMT_CORETURN,
  EXPR_COAWAIT,
  EXPR_COYIELD,
  EXPR_DEPENDENT_COAWAIT,

  // FixedPointLiteral
  EXPR_FIXEDPOINT_LITERAL,

  // SYCLUniqueStableNameExpr
  EXPR_SYCL_UNIQUE_STABLE_NAME,

  // OpenACC Constructs
  STMT_OPENACC_COMPUTE_CONSTRUCT,
  STMT_OPENACC_LOOP_CONSTRUCT,
};

/// The kinds of designators that can occur in a
/// DesignatedInitExpr.
enum DesignatorTypes {
  /// Field designator where only the field name is known.
  DESIG_FIELD_NAME = 0,

  /// Field designator where the field has been resolved to
  /// a declaration.
  DESIG_FIELD_DECL = 1,

  /// Array designator.
  DESIG_ARRAY = 2,

  /// GNU array range designator.
  DESIG_ARRAY_RANGE = 3
};

/// The different kinds of data that can occur in a
/// CtorInitializer.
enum CtorInitializerType {
  CTOR_INITIALIZER_BASE,
  CTOR_INITIALIZER_DELEGATING,
  CTOR_INITIALIZER_MEMBER,
  CTOR_INITIALIZER_INDIRECT_MEMBER
};

/// Kinds of cleanup objects owned by ExprWithCleanups.
enum CleanupObjectKind { COK_Block, COK_CompoundLiteral };

/// Describes the categories of an Objective-C class.
struct ObjCCategoriesInfo {
  // The ID of the definition. Use unaligned_decl_id_t to keep
  // ObjCCategoriesInfo 32-bit aligned.
  unaligned_decl_id_t DefinitionID;

  // Offset into the array of category lists.
  unsigned Offset;

  ObjCCategoriesInfo() = default;
  ObjCCategoriesInfo(LocalDeclID ID, unsigned Offset)
      : DefinitionID(ID.getRawValue()), Offset(Offset) {}

  DeclID getDefinitionID() const { return DefinitionID; }

  friend bool operator<(const ObjCCategoriesInfo &X,
                        const ObjCCategoriesInfo &Y) {
    return X.getDefinitionID() < Y.getDefinitionID();
  }

  friend bool operator>(const ObjCCategoriesInfo &X,
                        const ObjCCategoriesInfo &Y) {
    return X.getDefinitionID() > Y.getDefinitionID();
  }

  friend bool operator<=(const ObjCCategoriesInfo &X,
                         const ObjCCategoriesInfo &Y) {
    return X.getDefinitionID() <= Y.getDefinitionID();
  }

  friend bool operator>=(const ObjCCategoriesInfo &X,
                         const ObjCCategoriesInfo &Y) {
    return X.getDefinitionID() >= Y.getDefinitionID();
  }
};

static_assert(alignof(ObjCCategoriesInfo) <= 4);
static_assert(std::is_standard_layout_v<ObjCCategoriesInfo> &&
              std::is_trivial_v<ObjCCategoriesInfo>);

/// A key used when looking up entities by \ref DeclarationName.
///
/// Different \ref DeclarationNames are mapped to different keys, but the
/// same key can occasionally represent multiple names (for names that
/// contain types, in particular).
class DeclarationNameKey {
  using NameKind = unsigned;

  NameKind Kind = 0;
  uint64_t Data = 0;

public:
  DeclarationNameKey() = default;
  DeclarationNameKey(DeclarationName Name);
  DeclarationNameKey(NameKind Kind, uint64_t Data) : Kind(Kind), Data(Data) {}

  NameKind getKind() const { return Kind; }

  IdentifierInfo *getIdentifier() const {
    assert(Kind == DeclarationName::Identifier ||
           Kind == DeclarationName::CXXLiteralOperatorName ||
           Kind == DeclarationName::CXXDeductionGuideName);
    return (IdentifierInfo *)Data;
  }

  Selector getSelector() const {
    assert(Kind == DeclarationName::ObjCZeroArgSelector ||
           Kind == DeclarationName::ObjCOneArgSelector ||
           Kind == DeclarationName::ObjCMultiArgSelector);
    return Selector(Data);
  }

  OverloadedOperatorKind getOperatorKind() const {
    assert(Kind == DeclarationName::CXXOperatorName);
    return (OverloadedOperatorKind)Data;
  }

  /// Compute a fingerprint of this key for use in on-disk hash table.
  unsigned getHash() const;

  friend bool operator==(const DeclarationNameKey &A,
                         const DeclarationNameKey &B) {
    return A.Kind == B.Kind && A.Data == B.Data;
  }
};

/// @}

} // namespace serialization
} // namespace clang

namespace llvm {

template <> struct DenseMapInfo<clang::serialization::DeclarationNameKey> {
  static clang::serialization::DeclarationNameKey getEmptyKey() {
    return clang::serialization::DeclarationNameKey(-1, 1);
  }

  static clang::serialization::DeclarationNameKey getTombstoneKey() {
    return clang::serialization::DeclarationNameKey(-1, 2);
  }

  static unsigned
  getHashValue(const clang::serialization::DeclarationNameKey &Key) {
    return Key.getHash();
  }

  static bool isEqual(const clang::serialization::DeclarationNameKey &L,
                      const clang::serialization::DeclarationNameKey &R) {
    return L == R;
  }
};

} // namespace llvm

#endif // LLVM_CLANG_SERIALIZATION_ASTBITCODES_H

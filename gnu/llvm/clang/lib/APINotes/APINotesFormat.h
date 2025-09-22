//===-- APINotesWriter.h - API Notes Writer ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_APINOTES_APINOTESFORMAT_H
#define LLVM_CLANG_LIB_APINOTES_APINOTESFORMAT_H

#include "clang/APINotes/Types.h"
#include "llvm/ADT/PointerEmbeddedInt.h"
#include "llvm/Bitcode/BitcodeConvenience.h"

namespace clang {
namespace api_notes {
/// Magic number for API notes files.
const unsigned char API_NOTES_SIGNATURE[] = {0xE2, 0x9C, 0xA8, 0x01};

/// API notes file major version number.
const uint16_t VERSION_MAJOR = 0;

/// API notes file minor version number.
///
/// When the format changes IN ANY WAY, this number should be incremented.
const uint16_t VERSION_MINOR = 27; // SingleDeclTableKey

const uint8_t kSwiftCopyable = 1;
const uint8_t kSwiftNonCopyable = 2;

using IdentifierID = llvm::PointerEmbeddedInt<unsigned, 31>;
using IdentifierIDField = llvm::BCVBR<16>;

using SelectorID = llvm::PointerEmbeddedInt<unsigned, 31>;
using SelectorIDField = llvm::BCVBR<16>;

/// The various types of blocks that can occur within a API notes file.
///
/// These IDs must \em not be renumbered or reordered without incrementing
/// VERSION_MAJOR.
enum BlockID {
  /// The control block, which contains all of the information that needs to
  /// be validated prior to committing to loading the API notes file.
  ///
  /// \sa control_block
  CONTROL_BLOCK_ID = llvm::bitc::FIRST_APPLICATION_BLOCKID,

  /// The identifier data block, which maps identifier strings to IDs.
  IDENTIFIER_BLOCK_ID,

  /// The Objective-C context data block, which contains information about
  /// Objective-C classes and protocols.
  OBJC_CONTEXT_BLOCK_ID,

  /// The Objective-C property data block, which maps Objective-C
  /// (class name, property name) pairs to information about the
  /// property.
  OBJC_PROPERTY_BLOCK_ID,

  /// The Objective-C property data block, which maps Objective-C
  /// (class name, selector, is_instance_method) tuples to information
  /// about the method.
  OBJC_METHOD_BLOCK_ID,

  /// The C++ method data block, which maps C++ (context id, method name) pairs
  /// to information about the method.
  CXX_METHOD_BLOCK_ID,

  /// The Objective-C selector data block, which maps Objective-C
  /// selector names (# of pieces, identifier IDs) to the selector ID
  /// used in other tables.
  OBJC_SELECTOR_BLOCK_ID,

  /// The global variables data block, which maps global variable names to
  /// information about the global variable.
  GLOBAL_VARIABLE_BLOCK_ID,

  /// The (global) functions data block, which maps global function names to
  /// information about the global function.
  GLOBAL_FUNCTION_BLOCK_ID,

  /// The tag data block, which maps tag names to information about
  /// the tags.
  TAG_BLOCK_ID,

  /// The typedef data block, which maps typedef names to information about
  /// the typedefs.
  TYPEDEF_BLOCK_ID,

  /// The enum constant data block, which maps enumerator names to
  /// information about the enumerators.
  ENUM_CONSTANT_BLOCK_ID,
};

namespace control_block {
// These IDs must \em not be renumbered or reordered without incrementing
// VERSION_MAJOR.
enum {
  METADATA = 1,
  MODULE_NAME = 2,
  MODULE_OPTIONS = 3,
  SOURCE_FILE = 4,
};

using MetadataLayout =
    llvm::BCRecordLayout<METADATA,          // ID
                         llvm::BCFixed<16>, // Module format major version
                         llvm::BCFixed<16>  // Module format minor version
                         >;

using ModuleNameLayout = llvm::BCRecordLayout<MODULE_NAME,
                                              llvm::BCBlob // Module name
                                              >;

using ModuleOptionsLayout =
    llvm::BCRecordLayout<MODULE_OPTIONS,
                         llvm::BCFixed<1> // SwiftInferImportAsMember
                         >;

using SourceFileLayout = llvm::BCRecordLayout<SOURCE_FILE,
                                              llvm::BCVBR<16>, // file size
                                              llvm::BCVBR<16>  // creation time
                                              >;
} // namespace control_block

namespace identifier_block {
enum {
  IDENTIFIER_DATA = 1,
};

using IdentifierDataLayout = llvm::BCRecordLayout<
    IDENTIFIER_DATA, // record ID
    llvm::BCVBR<16>, // table offset within the blob (see below)
    llvm::BCBlob     // map from identifier strings to decl kinds / decl IDs
    >;
} // namespace identifier_block

namespace context_block {
enum {
  CONTEXT_ID_DATA = 1,
  CONTEXT_INFO_DATA = 2,
};

using ContextIDLayout =
    llvm::BCRecordLayout<CONTEXT_ID_DATA, // record ID
                         llvm::BCVBR<16>, // table offset within the blob (see
                                          // below)
                         llvm::BCBlob // map from ObjC class names/protocol (as
                                      // IDs) to context IDs
                         >;

using ContextInfoLayout = llvm::BCRecordLayout<
    CONTEXT_INFO_DATA, // record ID
    llvm::BCVBR<16>,   // table offset within the blob (see below)
    llvm::BCBlob       // map from ObjC context IDs to context information.
    >;
} // namespace context_block

namespace objc_property_block {
enum {
  OBJC_PROPERTY_DATA = 1,
};

using ObjCPropertyDataLayout = llvm::BCRecordLayout<
    OBJC_PROPERTY_DATA, // record ID
    llvm::BCVBR<16>,    // table offset within the blob (see below)
    llvm::BCBlob        // map from ObjC (class name, property name) pairs to
                        // ObjC property information
    >;
} // namespace objc_property_block

namespace objc_method_block {
enum {
  OBJC_METHOD_DATA = 1,
};

using ObjCMethodDataLayout =
    llvm::BCRecordLayout<OBJC_METHOD_DATA, // record ID
                         llvm::BCVBR<16>,  // table offset within the blob (see
                                           // below)
                         llvm::BCBlob // map from ObjC (class names, selector,
                                      // is-instance-method) tuples to ObjC
                                      // method information
                         >;
} // namespace objc_method_block

namespace cxx_method_block {
enum {
  CXX_METHOD_DATA = 1,
};

using CXXMethodDataLayout =
    llvm::BCRecordLayout<CXX_METHOD_DATA, // record ID
                         llvm::BCVBR<16>, // table offset within the blob (see
                                          // below)
                         llvm::BCBlob     // map from C++ (context id, name)
                                          // tuples to C++ method information
                         >;
} // namespace cxx_method_block

namespace objc_selector_block {
enum {
  OBJC_SELECTOR_DATA = 1,
};

using ObjCSelectorDataLayout =
    llvm::BCRecordLayout<OBJC_SELECTOR_DATA, // record ID
                         llvm::BCVBR<16>, // table offset within the blob (see
                                          // below)
                         llvm::BCBlob // map from (# pieces, identifier IDs) to
                                      // Objective-C selector ID.
                         >;
} // namespace objc_selector_block

namespace global_variable_block {
enum { GLOBAL_VARIABLE_DATA = 1 };

using GlobalVariableDataLayout = llvm::BCRecordLayout<
    GLOBAL_VARIABLE_DATA, // record ID
    llvm::BCVBR<16>,      // table offset within the blob (see below)
    llvm::BCBlob          // map from name to global variable information
    >;
} // namespace global_variable_block

namespace global_function_block {
enum { GLOBAL_FUNCTION_DATA = 1 };

using GlobalFunctionDataLayout = llvm::BCRecordLayout<
    GLOBAL_FUNCTION_DATA, // record ID
    llvm::BCVBR<16>,      // table offset within the blob (see below)
    llvm::BCBlob          // map from name to global function information
    >;
} // namespace global_function_block

namespace tag_block {
enum { TAG_DATA = 1 };

using TagDataLayout =
    llvm::BCRecordLayout<TAG_DATA,        // record ID
                         llvm::BCVBR<16>, // table offset within the blob (see
                                          // below)
                         llvm::BCBlob     // map from name to tag information
                         >;
} // namespace tag_block

namespace typedef_block {
enum { TYPEDEF_DATA = 1 };

using TypedefDataLayout =
    llvm::BCRecordLayout<TYPEDEF_DATA,    // record ID
                         llvm::BCVBR<16>, // table offset within the blob (see
                                          // below)
                         llvm::BCBlob // map from name to typedef information
                         >;
} // namespace typedef_block

namespace enum_constant_block {
enum { ENUM_CONSTANT_DATA = 1 };

using EnumConstantDataLayout =
    llvm::BCRecordLayout<ENUM_CONSTANT_DATA, // record ID
                         llvm::BCVBR<16>, // table offset within the blob (see
                                          // below)
                         llvm::BCBlob // map from name to enumerator information
                         >;
} // namespace enum_constant_block

/// A stored Objective-C selector.
struct StoredObjCSelector {
  unsigned NumArgs;
  llvm::SmallVector<IdentifierID, 2> Identifiers;
};

/// A stored Objective-C or C++ context, represented by the ID of its parent
/// context, the kind of this context (Objective-C class / C++ namespace / etc),
/// and the ID of this context.
struct ContextTableKey {
  uint32_t parentContextID;
  uint8_t contextKind;
  uint32_t contextID;

  ContextTableKey() : parentContextID(-1), contextKind(-1), contextID(-1) {}

  ContextTableKey(uint32_t parentContextID, uint8_t contextKind,
                  uint32_t contextID)
      : parentContextID(parentContextID), contextKind(contextKind),
        contextID(contextID) {}

  ContextTableKey(std::optional<ContextID> ParentContextID, ContextKind Kind,
                  uint32_t ContextID)
      : parentContextID(ParentContextID ? ParentContextID->Value : -1),
        contextKind(static_cast<uint8_t>(Kind)), contextID(ContextID) {}

  ContextTableKey(std::optional<Context> ParentContext, ContextKind Kind,
                  uint32_t ContextID)
      : ContextTableKey(ParentContext ? std::make_optional(ParentContext->id)
                                      : std::nullopt,
                        Kind, ContextID) {}

  llvm::hash_code hashValue() const {
    return llvm::hash_value(
        std::tuple{parentContextID, contextKind, contextID});
  }
};

inline bool operator==(const ContextTableKey &lhs, const ContextTableKey &rhs) {
  return lhs.parentContextID == rhs.parentContextID &&
         lhs.contextKind == rhs.contextKind && lhs.contextID == rhs.contextID;
}

/// A stored Objective-C or C++ declaration, represented by the ID of its parent
/// context, and the name of the declaration.
struct SingleDeclTableKey {
  uint32_t parentContextID;
  uint32_t nameID;

  SingleDeclTableKey() : parentContextID(-1), nameID(-1) {}

  SingleDeclTableKey(uint32_t ParentContextID, uint32_t NameID)
      : parentContextID(ParentContextID), nameID(NameID) {}

  SingleDeclTableKey(std::optional<Context> ParentCtx, IdentifierID NameID)
      : parentContextID(ParentCtx ? ParentCtx->id.Value
                                  : static_cast<uint32_t>(-1)),
        nameID(NameID) {}

  llvm::hash_code hashValue() const {
    return llvm::hash_value(std::make_pair(parentContextID, nameID));
  }
};

inline bool operator==(const SingleDeclTableKey &lhs,
                       const SingleDeclTableKey &rhs) {
  return lhs.parentContextID == rhs.parentContextID && lhs.nameID == rhs.nameID;
}

} // namespace api_notes
} // namespace clang

namespace llvm {
template <> struct DenseMapInfo<clang::api_notes::StoredObjCSelector> {
  typedef DenseMapInfo<unsigned> UnsignedInfo;

  static inline clang::api_notes::StoredObjCSelector getEmptyKey() {
    return clang::api_notes::StoredObjCSelector{UnsignedInfo::getEmptyKey(),
                                                {}};
  }

  static inline clang::api_notes::StoredObjCSelector getTombstoneKey() {
    return clang::api_notes::StoredObjCSelector{UnsignedInfo::getTombstoneKey(),
                                                {}};
  }

  static unsigned
  getHashValue(const clang::api_notes::StoredObjCSelector &Selector) {
    auto hash = llvm::hash_value(Selector.NumArgs);
    hash = hash_combine(hash, Selector.Identifiers.size());
    for (auto piece : Selector.Identifiers)
      hash = hash_combine(hash, static_cast<unsigned>(piece));
    // FIXME: Mix upper/lower 32-bit values together to produce
    // unsigned rather than truncating.
    return hash;
  }

  static bool isEqual(const clang::api_notes::StoredObjCSelector &LHS,
                      const clang::api_notes::StoredObjCSelector &RHS) {
    return LHS.NumArgs == RHS.NumArgs && LHS.Identifiers == RHS.Identifiers;
  }
};

template <> struct DenseMapInfo<clang::api_notes::ContextTableKey> {
  static inline clang::api_notes::ContextTableKey getEmptyKey() {
    return clang::api_notes::ContextTableKey();
  }

  static inline clang::api_notes::ContextTableKey getTombstoneKey() {
    return clang::api_notes::ContextTableKey{
        DenseMapInfo<uint32_t>::getTombstoneKey(),
        DenseMapInfo<uint8_t>::getTombstoneKey(),
        DenseMapInfo<uint32_t>::getTombstoneKey()};
  }

  static unsigned getHashValue(const clang::api_notes::ContextTableKey &value) {
    return value.hashValue();
  }

  static bool isEqual(const clang::api_notes::ContextTableKey &lhs,
                      const clang::api_notes::ContextTableKey &rhs) {
    return lhs == rhs;
  }
};

template <> struct DenseMapInfo<clang::api_notes::SingleDeclTableKey> {
  static inline clang::api_notes::SingleDeclTableKey getEmptyKey() {
    return clang::api_notes::SingleDeclTableKey();
  }

  static inline clang::api_notes::SingleDeclTableKey getTombstoneKey() {
    return clang::api_notes::SingleDeclTableKey{
        DenseMapInfo<uint32_t>::getTombstoneKey(),
        DenseMapInfo<uint32_t>::getTombstoneKey()};
  }

  static unsigned
  getHashValue(const clang::api_notes::SingleDeclTableKey &value) {
    return value.hashValue();
  }

  static bool isEqual(const clang::api_notes::SingleDeclTableKey &lhs,
                      const clang::api_notes::SingleDeclTableKey &rhs) {
    return lhs == rhs;
  }
};

} // namespace llvm

#endif

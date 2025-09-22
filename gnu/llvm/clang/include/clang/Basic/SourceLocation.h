//===- SourceLocation.h - Compact identifier for Source Files ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Defines the clang::SourceLocation class and associated facilities.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_SOURCELOCATION_H
#define LLVM_CLANG_BASIC_SOURCELOCATION_H

#include "clang/Basic/FileEntry.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/StringRef.h"
#include <cassert>
#include <cstdint>
#include <string>
#include <utility>

namespace llvm {

class FoldingSetNodeID;
template <typename T, typename Enable> struct FoldingSetTrait;

} // namespace llvm

namespace clang {

class SourceManager;

/// An opaque identifier used by SourceManager which refers to a
/// source file (MemoryBuffer) along with its \#include path and \#line data.
///
class FileID {
  /// A mostly-opaque identifier, where 0 is "invalid", >0 is
  /// this module, and <-1 is something loaded from another module.
  int ID = 0;

public:
  bool isValid() const { return ID != 0; }
  bool isInvalid() const { return ID == 0; }

  bool operator==(const FileID &RHS) const { return ID == RHS.ID; }
  bool operator<(const FileID &RHS) const { return ID < RHS.ID; }
  bool operator<=(const FileID &RHS) const { return ID <= RHS.ID; }
  bool operator!=(const FileID &RHS) const { return !(*this == RHS); }
  bool operator>(const FileID &RHS) const { return RHS < *this; }
  bool operator>=(const FileID &RHS) const { return RHS <= *this; }

  static FileID getSentinel() { return get(-1); }
  unsigned getHashValue() const { return static_cast<unsigned>(ID); }

private:
  friend class ASTWriter;
  friend class ASTReader;
  friend class SourceManager;
  friend class SourceManagerTestHelper;

  static FileID get(int V) {
    FileID F;
    F.ID = V;
    return F;
  }

  int getOpaqueValue() const { return ID; }
};

/// Encodes a location in the source. The SourceManager can decode this
/// to get at the full include stack, line and column information.
///
/// Technically, a source location is simply an offset into the manager's view
/// of the input source, which is all input buffers (including macro
/// expansions) concatenated in an effectively arbitrary order. The manager
/// actually maintains two blocks of input buffers. One, starting at offset
/// 0 and growing upwards, contains all buffers from this module. The other,
/// starting at the highest possible offset and growing downwards, contains
/// buffers of loaded modules.
///
/// In addition, one bit of SourceLocation is used for quick access to the
/// information whether the location is in a file or a macro expansion.
///
/// It is important that this type remains small. It is currently 32 bits wide.
class SourceLocation {
  friend class ASTReader;
  friend class ASTWriter;
  friend class SourceManager;
  friend struct llvm::FoldingSetTrait<SourceLocation, void>;
  friend class SourceLocationEncoding;

public:
  using UIntTy = uint32_t;
  using IntTy = int32_t;

private:
  UIntTy ID = 0;

  enum : UIntTy { MacroIDBit = 1ULL << (8 * sizeof(UIntTy) - 1) };

public:
  bool isFileID() const  { return (ID & MacroIDBit) == 0; }
  bool isMacroID() const { return (ID & MacroIDBit) != 0; }

  /// Return true if this is a valid SourceLocation object.
  ///
  /// Invalid SourceLocations are often used when events have no corresponding
  /// location in the source (e.g. a diagnostic is required for a command line
  /// option).
  bool isValid() const { return ID != 0; }
  bool isInvalid() const { return ID == 0; }

private:
  /// Return the offset into the manager's global input view.
  UIntTy getOffset() const { return ID & ~MacroIDBit; }

  static SourceLocation getFileLoc(UIntTy ID) {
    assert((ID & MacroIDBit) == 0 && "Ran out of source locations!");
    SourceLocation L;
    L.ID = ID;
    return L;
  }

  static SourceLocation getMacroLoc(UIntTy ID) {
    assert((ID & MacroIDBit) == 0 && "Ran out of source locations!");
    SourceLocation L;
    L.ID = MacroIDBit | ID;
    return L;
  }

public:
  /// Return a source location with the specified offset from this
  /// SourceLocation.
  SourceLocation getLocWithOffset(IntTy Offset) const {
    assert(((getOffset()+Offset) & MacroIDBit) == 0 && "offset overflow");
    SourceLocation L;
    L.ID = ID+Offset;
    return L;
  }

  /// When a SourceLocation itself cannot be used, this returns
  /// an (opaque) 32-bit integer encoding for it.
  ///
  /// This should only be passed to SourceLocation::getFromRawEncoding, it
  /// should not be inspected directly.
  UIntTy getRawEncoding() const { return ID; }

  /// Turn a raw encoding of a SourceLocation object into
  /// a real SourceLocation.
  ///
  /// \see getRawEncoding.
  static SourceLocation getFromRawEncoding(UIntTy Encoding) {
    SourceLocation X;
    X.ID = Encoding;
    return X;
  }

  /// When a SourceLocation itself cannot be used, this returns
  /// an (opaque) pointer encoding for it.
  ///
  /// This should only be passed to SourceLocation::getFromPtrEncoding, it
  /// should not be inspected directly.
  void* getPtrEncoding() const {
    // Double cast to avoid a warning "cast to pointer from integer of different
    // size".
    return (void*)(uintptr_t)getRawEncoding();
  }

  /// Turn a pointer encoding of a SourceLocation object back
  /// into a real SourceLocation.
  static SourceLocation getFromPtrEncoding(const void *Encoding) {
    return getFromRawEncoding((SourceLocation::UIntTy)(uintptr_t)Encoding);
  }

  static bool isPairOfFileLocations(SourceLocation Start, SourceLocation End) {
    return Start.isValid() && Start.isFileID() && End.isValid() &&
           End.isFileID();
  }

  unsigned getHashValue() const;
  void print(raw_ostream &OS, const SourceManager &SM) const;
  std::string printToString(const SourceManager &SM) const;
  void dump(const SourceManager &SM) const;
};

inline bool operator==(const SourceLocation &LHS, const SourceLocation &RHS) {
  return LHS.getRawEncoding() == RHS.getRawEncoding();
}

inline bool operator!=(const SourceLocation &LHS, const SourceLocation &RHS) {
  return !(LHS == RHS);
}

// Ordering is meaningful only if LHS and RHS have the same FileID!
// Otherwise use SourceManager::isBeforeInTranslationUnit().
inline bool operator<(const SourceLocation &LHS, const SourceLocation &RHS) {
  return LHS.getRawEncoding() < RHS.getRawEncoding();
}
inline bool operator>(const SourceLocation &LHS, const SourceLocation &RHS) {
  return LHS.getRawEncoding() > RHS.getRawEncoding();
}
inline bool operator<=(const SourceLocation &LHS, const SourceLocation &RHS) {
  return LHS.getRawEncoding() <= RHS.getRawEncoding();
}
inline bool operator>=(const SourceLocation &LHS, const SourceLocation &RHS) {
  return LHS.getRawEncoding() >= RHS.getRawEncoding();
}

/// A trivial tuple used to represent a source range.
class SourceRange {
  SourceLocation B;
  SourceLocation E;

public:
  SourceRange() = default;
  SourceRange(SourceLocation loc) : B(loc), E(loc) {}
  SourceRange(SourceLocation begin, SourceLocation end) : B(begin), E(end) {}

  SourceLocation getBegin() const { return B; }
  SourceLocation getEnd() const { return E; }

  void setBegin(SourceLocation b) { B = b; }
  void setEnd(SourceLocation e) { E = e; }

  bool isValid() const { return B.isValid() && E.isValid(); }
  bool isInvalid() const { return !isValid(); }

  bool operator==(const SourceRange &X) const {
    return B == X.B && E == X.E;
  }

  bool operator!=(const SourceRange &X) const {
    return B != X.B || E != X.E;
  }

  // Returns true iff other is wholly contained within this range.
  bool fullyContains(const SourceRange &other) const {
    return B <= other.B && E >= other.E;
  }

  void print(raw_ostream &OS, const SourceManager &SM) const;
  std::string printToString(const SourceManager &SM) const;
  void dump(const SourceManager &SM) const;
};

/// Represents a character-granular source range.
///
/// The underlying SourceRange can either specify the starting/ending character
/// of the range, or it can specify the start of the range and the start of the
/// last token of the range (a "token range").  In the token range case, the
/// size of the last token must be measured to determine the actual end of the
/// range.
class CharSourceRange {
  SourceRange Range;
  bool IsTokenRange = false;

public:
  CharSourceRange() = default;
  CharSourceRange(SourceRange R, bool ITR) : Range(R), IsTokenRange(ITR) {}

  static CharSourceRange getTokenRange(SourceRange R) {
    return CharSourceRange(R, true);
  }

  static CharSourceRange getCharRange(SourceRange R) {
    return CharSourceRange(R, false);
  }

  static CharSourceRange getTokenRange(SourceLocation B, SourceLocation E) {
    return getTokenRange(SourceRange(B, E));
  }

  static CharSourceRange getCharRange(SourceLocation B, SourceLocation E) {
    return getCharRange(SourceRange(B, E));
  }

  /// Return true if the end of this range specifies the start of
  /// the last token.  Return false if the end of this range specifies the last
  /// character in the range.
  bool isTokenRange() const { return IsTokenRange; }
  bool isCharRange() const { return !IsTokenRange; }

  SourceLocation getBegin() const { return Range.getBegin(); }
  SourceLocation getEnd() const { return Range.getEnd(); }
  SourceRange getAsRange() const { return Range; }

  void setBegin(SourceLocation b) { Range.setBegin(b); }
  void setEnd(SourceLocation e) { Range.setEnd(e); }
  void setTokenRange(bool TR) { IsTokenRange = TR; }

  bool isValid() const { return Range.isValid(); }
  bool isInvalid() const { return !isValid(); }
};

/// Represents an unpacked "presumed" location which can be presented
/// to the user.
///
/// A 'presumed' location can be modified by \#line and GNU line marker
/// directives and is always the expansion point of a normal location.
///
/// You can get a PresumedLoc from a SourceLocation with SourceManager.
class PresumedLoc {
  const char *Filename = nullptr;
  FileID ID;
  unsigned Line, Col;
  SourceLocation IncludeLoc;

public:
  PresumedLoc() = default;
  PresumedLoc(const char *FN, FileID FID, unsigned Ln, unsigned Co,
              SourceLocation IL)
      : Filename(FN), ID(FID), Line(Ln), Col(Co), IncludeLoc(IL) {}

  /// Return true if this object is invalid or uninitialized.
  ///
  /// This occurs when created with invalid source locations or when walking
  /// off the top of a \#include stack.
  bool isInvalid() const { return Filename == nullptr; }
  bool isValid() const { return Filename != nullptr; }

  /// Return the presumed filename of this location.
  ///
  /// This can be affected by \#line etc.
  const char *getFilename() const {
    assert(isValid());
    return Filename;
  }

  FileID getFileID() const {
    assert(isValid());
    return ID;
  }

  /// Return the presumed line number of this location.
  ///
  /// This can be affected by \#line etc.
  unsigned getLine() const {
    assert(isValid());
    return Line;
  }

  /// Return the presumed column number of this location.
  ///
  /// This cannot be affected by \#line, but is packaged here for convenience.
  unsigned getColumn() const {
    assert(isValid());
    return Col;
  }

  /// Return the presumed include location of this location.
  ///
  /// This can be affected by GNU linemarker directives.
  SourceLocation getIncludeLoc() const {
    assert(isValid());
    return IncludeLoc;
  }
};

/// A SourceLocation and its associated SourceManager.
///
/// This is useful for argument passing to functions that expect both objects.
///
/// This class does not guarantee the presence of either the SourceManager or
/// a valid SourceLocation. Clients should use `isValid()` and `hasManager()`
/// before calling the member functions.
class FullSourceLoc : public SourceLocation {
  const SourceManager *SrcMgr = nullptr;

public:
  /// Creates a FullSourceLoc where isValid() returns \c false.
  FullSourceLoc() = default;

  explicit FullSourceLoc(SourceLocation Loc, const SourceManager &SM)
      : SourceLocation(Loc), SrcMgr(&SM) {}

  /// Checks whether the SourceManager is present.
  bool hasManager() const { return SrcMgr != nullptr; }

  /// \pre hasManager()
  const SourceManager &getManager() const {
    assert(SrcMgr && "SourceManager is NULL.");
    return *SrcMgr;
  }

  FileID getFileID() const;

  FullSourceLoc getExpansionLoc() const;
  FullSourceLoc getSpellingLoc() const;
  FullSourceLoc getFileLoc() const;
  PresumedLoc getPresumedLoc(bool UseLineDirectives = true) const;
  bool isMacroArgExpansion(FullSourceLoc *StartLoc = nullptr) const;
  FullSourceLoc getImmediateMacroCallerLoc() const;
  std::pair<FullSourceLoc, StringRef> getModuleImportLoc() const;
  unsigned getFileOffset() const;

  unsigned getExpansionLineNumber(bool *Invalid = nullptr) const;
  unsigned getExpansionColumnNumber(bool *Invalid = nullptr) const;

  /// Decompose the underlying \c SourceLocation into a raw (FileID + Offset)
  /// pair, after walking through all expansion records.
  ///
  /// \see SourceManager::getDecomposedExpansionLoc
  std::pair<FileID, unsigned> getDecomposedExpansionLoc() const;

  unsigned getSpellingLineNumber(bool *Invalid = nullptr) const;
  unsigned getSpellingColumnNumber(bool *Invalid = nullptr) const;

  const char *getCharacterData(bool *Invalid = nullptr) const;

  unsigned getLineNumber(bool *Invalid = nullptr) const;
  unsigned getColumnNumber(bool *Invalid = nullptr) const;

  const FileEntry *getFileEntry() const;
  OptionalFileEntryRef getFileEntryRef() const;

  /// Return a StringRef to the source buffer data for the
  /// specified FileID.
  StringRef getBufferData(bool *Invalid = nullptr) const;

  /// Decompose the specified location into a raw FileID + Offset pair.
  ///
  /// The first element is the FileID, the second is the offset from the
  /// start of the buffer of the location.
  std::pair<FileID, unsigned> getDecomposedLoc() const;

  bool isInSystemHeader() const;

  /// Determines the order of 2 source locations in the translation unit.
  ///
  /// \returns true if this source location comes before 'Loc', false otherwise.
  bool isBeforeInTranslationUnitThan(SourceLocation Loc) const;

  /// Determines the order of 2 source locations in the translation unit.
  ///
  /// \returns true if this source location comes before 'Loc', false otherwise.
  bool isBeforeInTranslationUnitThan(FullSourceLoc Loc) const {
    assert(Loc.isValid());
    assert(SrcMgr == Loc.SrcMgr && "Loc comes from another SourceManager!");
    return isBeforeInTranslationUnitThan((SourceLocation)Loc);
  }

  /// Comparison function class, useful for sorting FullSourceLocs.
  struct BeforeThanCompare {
    bool operator()(const FullSourceLoc& lhs, const FullSourceLoc& rhs) const {
      return lhs.isBeforeInTranslationUnitThan(rhs);
    }
  };

  /// Prints information about this FullSourceLoc to stderr.
  ///
  /// This is useful for debugging.
  void dump() const;

  friend bool
  operator==(const FullSourceLoc &LHS, const FullSourceLoc &RHS) {
    return LHS.getRawEncoding() == RHS.getRawEncoding() &&
          LHS.SrcMgr == RHS.SrcMgr;
  }

  friend bool
  operator!=(const FullSourceLoc &LHS, const FullSourceLoc &RHS) {
    return !(LHS == RHS);
  }
};

} // namespace clang

namespace llvm {

  /// Define DenseMapInfo so that FileID's can be used as keys in DenseMap and
  /// DenseSets.
  template <>
  struct DenseMapInfo<clang::FileID, void> {
    static clang::FileID getEmptyKey() {
      return {};
    }

    static clang::FileID getTombstoneKey() {
      return clang::FileID::getSentinel();
    }

    static unsigned getHashValue(clang::FileID S) {
      return S.getHashValue();
    }

    static bool isEqual(clang::FileID LHS, clang::FileID RHS) {
      return LHS == RHS;
    }
  };

  /// Define DenseMapInfo so that SourceLocation's can be used as keys in
  /// DenseMap and DenseSet. This trait class is eqivalent to
  /// DenseMapInfo<unsigned> which uses SourceLocation::ID is used as a key.
  template <> struct DenseMapInfo<clang::SourceLocation, void> {
    static clang::SourceLocation getEmptyKey() {
      constexpr clang::SourceLocation::UIntTy Zero = 0;
      return clang::SourceLocation::getFromRawEncoding(~Zero);
    }

    static clang::SourceLocation getTombstoneKey() {
      constexpr clang::SourceLocation::UIntTy Zero = 0;
      return clang::SourceLocation::getFromRawEncoding(~Zero - 1);
    }

    static unsigned getHashValue(clang::SourceLocation Loc) {
      return Loc.getHashValue();
    }

    static bool isEqual(clang::SourceLocation LHS, clang::SourceLocation RHS) {
      return LHS == RHS;
    }
  };

  // Allow calling FoldingSetNodeID::Add with SourceLocation object as parameter
  template <> struct FoldingSetTrait<clang::SourceLocation, void> {
    static void Profile(const clang::SourceLocation &X, FoldingSetNodeID &ID);
  };

} // namespace llvm

#endif // LLVM_CLANG_BASIC_SOURCELOCATION_H

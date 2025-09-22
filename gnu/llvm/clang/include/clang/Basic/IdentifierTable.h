//===- IdentifierTable.h - Hash table for identifier lookup -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Defines the clang::IdentifierInfo, clang::IdentifierTable, and
/// clang::Selector interfaces.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_IDENTIFIERTABLE_H
#define LLVM_CLANG_BASIC_IDENTIFIERTABLE_H

#include "clang/Basic/Builtins.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/TokenKinds.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/PointerLikeTypeTraits.h"
#include "llvm/Support/type_traits.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

namespace clang {

class DeclarationName;
class DeclarationNameTable;
class IdentifierInfo;
class LangOptions;
class MultiKeywordSelector;
class SourceLocation;

enum class ReservedIdentifierStatus {
  NotReserved = 0,
  StartsWithUnderscoreAtGlobalScope,
  StartsWithUnderscoreAndIsExternC,
  StartsWithDoubleUnderscore,
  StartsWithUnderscoreFollowedByCapitalLetter,
  ContainsDoubleUnderscore,
};

enum class ReservedLiteralSuffixIdStatus {
  NotReserved = 0,
  NotStartsWithUnderscore,
  ContainsDoubleUnderscore,
};

/// Determine whether an identifier is reserved for use as a name at global
/// scope. Such identifiers might be implementation-specific global functions
/// or variables.
inline bool isReservedAtGlobalScope(ReservedIdentifierStatus Status) {
  return Status != ReservedIdentifierStatus::NotReserved;
}

/// Determine whether an identifier is reserved in all contexts. Such
/// identifiers might be implementation-specific keywords or macros, for
/// example.
inline bool isReservedInAllContexts(ReservedIdentifierStatus Status) {
  return Status != ReservedIdentifierStatus::NotReserved &&
         Status != ReservedIdentifierStatus::StartsWithUnderscoreAtGlobalScope &&
         Status != ReservedIdentifierStatus::StartsWithUnderscoreAndIsExternC;
}

/// A simple pair of identifier info and location.
using IdentifierLocPair = std::pair<IdentifierInfo *, SourceLocation>;

/// IdentifierInfo and other related classes are aligned to
/// 8 bytes so that DeclarationName can use the lower 3 bits
/// of a pointer to one of these classes.
enum { IdentifierInfoAlignment = 8 };

static constexpr int InterestingIdentifierBits = 16;

/// The "layout" of InterestingIdentifier is:
///  - ObjCKeywordKind enumerators
///  - NotableIdentifierKind enumerators
///  - Builtin::ID enumerators
///  - NotInterestingIdentifier
enum class InterestingIdentifier {
#define OBJC_AT_KEYWORD(X) objc_##X,
#include "clang/Basic/TokenKinds.def"
  NUM_OBJC_KEYWORDS,

#define NOTABLE_IDENTIFIER(X) X,
#include "clang/Basic/TokenKinds.def"
  NUM_OBJC_KEYWORDS_AND_NOTABLE_IDENTIFIERS,

  NotBuiltin,
#define BUILTIN(ID, TYPE, ATTRS) BI##ID,
#include "clang/Basic/Builtins.inc"
  FirstTSBuiltin,

  NotInterestingIdentifier = 65534
};

/// One of these records is kept for each identifier that
/// is lexed.  This contains information about whether the token was \#define'd,
/// is a language keyword, or if it is a front-end token of some sort (e.g. a
/// variable or function name).  The preprocessor keeps this information in a
/// set, and all tok::identifier tokens have a pointer to one of these.
/// It is aligned to 8 bytes because DeclarationName needs the lower 3 bits.
class alignas(IdentifierInfoAlignment) IdentifierInfo {
  friend class IdentifierTable;

  // Front-end token ID or tok::identifier.
  LLVM_PREFERRED_TYPE(tok::TokenKind)
  unsigned TokenID : 9;

  LLVM_PREFERRED_TYPE(InterestingIdentifier)
  unsigned InterestingIdentifierID : InterestingIdentifierBits;

  // True if there is a #define for this.
  LLVM_PREFERRED_TYPE(bool)
  unsigned HasMacro : 1;

  // True if there was a #define for this.
  LLVM_PREFERRED_TYPE(bool)
  unsigned HadMacro : 1;

  // True if the identifier is a language extension.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsExtension : 1;

  // True if the identifier is a keyword in a newer or proposed Standard.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsFutureCompatKeyword : 1;

  // True if the identifier is poisoned.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsPoisoned : 1;

  // True if the identifier is a C++ operator keyword.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsCPPOperatorKeyword : 1;

  // Internal bit set by the member function RecomputeNeedsHandleIdentifier.
  // See comment about RecomputeNeedsHandleIdentifier for more info.
  LLVM_PREFERRED_TYPE(bool)
  unsigned NeedsHandleIdentifier : 1;

  // True if the identifier was loaded (at least partially) from an AST file.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsFromAST : 1;

  // True if the identifier has changed from the definition
  // loaded from an AST file.
  LLVM_PREFERRED_TYPE(bool)
  unsigned ChangedAfterLoad : 1;

  // True if the identifier's frontend information has changed from the
  // definition loaded from an AST file.
  LLVM_PREFERRED_TYPE(bool)
  unsigned FEChangedAfterLoad : 1;

  // True if revertTokenIDToIdentifier was called.
  LLVM_PREFERRED_TYPE(bool)
  unsigned RevertedTokenID : 1;

  // True if there may be additional information about
  // this identifier stored externally.
  LLVM_PREFERRED_TYPE(bool)
  unsigned OutOfDate : 1;

  // True if this is the 'import' contextual keyword.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsModulesImport : 1;

  // True if this is a mangled OpenMP variant name.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsMangledOpenMPVariantName : 1;

  // True if this is a deprecated macro.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsDeprecatedMacro : 1;

  // True if this macro is unsafe in headers.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsRestrictExpansion : 1;

  // True if this macro is final.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsFinal : 1;

  // 22 bits left in a 64-bit word.

  // Managed by the language front-end.
  void *FETokenInfo = nullptr;

  llvm::StringMapEntry<IdentifierInfo *> *Entry = nullptr;

  IdentifierInfo()
      : TokenID(tok::identifier),
        InterestingIdentifierID(llvm::to_underlying(
            InterestingIdentifier::NotInterestingIdentifier)),
        HasMacro(false), HadMacro(false), IsExtension(false),
        IsFutureCompatKeyword(false), IsPoisoned(false),
        IsCPPOperatorKeyword(false), NeedsHandleIdentifier(false),
        IsFromAST(false), ChangedAfterLoad(false), FEChangedAfterLoad(false),
        RevertedTokenID(false), OutOfDate(false), IsModulesImport(false),
        IsMangledOpenMPVariantName(false), IsDeprecatedMacro(false),
        IsRestrictExpansion(false), IsFinal(false) {}

public:
  IdentifierInfo(const IdentifierInfo &) = delete;
  IdentifierInfo &operator=(const IdentifierInfo &) = delete;
  IdentifierInfo(IdentifierInfo &&) = delete;
  IdentifierInfo &operator=(IdentifierInfo &&) = delete;

  /// Return true if this is the identifier for the specified string.
  ///
  /// This is intended to be used for string literals only: II->isStr("foo").
  template <std::size_t StrLen>
  bool isStr(const char (&Str)[StrLen]) const {
    return getLength() == StrLen-1 &&
           memcmp(getNameStart(), Str, StrLen-1) == 0;
  }

  /// Return true if this is the identifier for the specified StringRef.
  bool isStr(llvm::StringRef Str) const {
    llvm::StringRef ThisStr(getNameStart(), getLength());
    return ThisStr == Str;
  }

  /// Return the beginning of the actual null-terminated string for this
  /// identifier.
  const char *getNameStart() const { return Entry->getKeyData(); }

  /// Efficiently return the length of this identifier info.
  unsigned getLength() const { return Entry->getKeyLength(); }

  /// Return the actual identifier string.
  StringRef getName() const {
    return StringRef(getNameStart(), getLength());
  }

  /// Return true if this identifier is \#defined to some other value.
  /// \note The current definition may be in a module and not currently visible.
  bool hasMacroDefinition() const {
    return HasMacro;
  }
  void setHasMacroDefinition(bool Val) {
    if (HasMacro == Val) return;

    HasMacro = Val;
    if (Val) {
      NeedsHandleIdentifier = true;
      HadMacro = true;
    } else {
      // If this is a final macro, make the deprecation and header unsafe bits
      // stick around after the undefinition so they apply to any redefinitions.
      if (!IsFinal) {
        // Because calling the setters of these calls recomputes, just set them
        // manually to avoid recomputing a bunch of times.
        IsDeprecatedMacro = false;
        IsRestrictExpansion = false;
      }
      RecomputeNeedsHandleIdentifier();
    }
  }
  /// Returns true if this identifier was \#defined to some value at any
  /// moment. In this case there should be an entry for the identifier in the
  /// macro history table in Preprocessor.
  bool hadMacroDefinition() const {
    return HadMacro;
  }

  bool isDeprecatedMacro() const { return IsDeprecatedMacro; }

  void setIsDeprecatedMacro(bool Val) {
    if (IsDeprecatedMacro == Val)
      return;
    IsDeprecatedMacro = Val;
    if (Val)
      NeedsHandleIdentifier = true;
    else
      RecomputeNeedsHandleIdentifier();
  }

  bool isRestrictExpansion() const { return IsRestrictExpansion; }

  void setIsRestrictExpansion(bool Val) {
    if (IsRestrictExpansion == Val)
      return;
    IsRestrictExpansion = Val;
    if (Val)
      NeedsHandleIdentifier = true;
    else
      RecomputeNeedsHandleIdentifier();
  }

  bool isFinal() const { return IsFinal; }

  void setIsFinal(bool Val) { IsFinal = Val; }

  /// If this is a source-language token (e.g. 'for'), this API
  /// can be used to cause the lexer to map identifiers to source-language
  /// tokens.
  tok::TokenKind getTokenID() const { return (tok::TokenKind)TokenID; }

  /// True if revertTokenIDToIdentifier() was called.
  bool hasRevertedTokenIDToIdentifier() const { return RevertedTokenID; }

  /// Revert TokenID to tok::identifier; used for GNU libstdc++ 4.2
  /// compatibility.
  ///
  /// TokenID is normally read-only but there are 2 instances where we revert it
  /// to tok::identifier for libstdc++ 4.2. Keep track of when this happens
  /// using this method so we can inform serialization about it.
  void revertTokenIDToIdentifier() {
    assert(TokenID != tok::identifier && "Already at tok::identifier");
    TokenID = tok::identifier;
    RevertedTokenID = true;
  }
  void revertIdentifierToTokenID(tok::TokenKind TK) {
    assert(TokenID == tok::identifier && "Should be at tok::identifier");
    TokenID = TK;
    RevertedTokenID = false;
  }

  /// Return the preprocessor keyword ID for this identifier.
  ///
  /// For example, "define" will return tok::pp_define.
  tok::PPKeywordKind getPPKeywordID() const;

  /// Return the Objective-C keyword ID for the this identifier.
  ///
  /// For example, 'class' will return tok::objc_class if ObjC is enabled.
  tok::ObjCKeywordKind getObjCKeywordID() const {
    assert(0 == llvm::to_underlying(InterestingIdentifier::objc_not_keyword));
    auto Value = static_cast<InterestingIdentifier>(InterestingIdentifierID);
    if (Value < InterestingIdentifier::NUM_OBJC_KEYWORDS)
      return static_cast<tok::ObjCKeywordKind>(InterestingIdentifierID);
    return tok::objc_not_keyword;
  }
  void setObjCKeywordID(tok::ObjCKeywordKind ID) {
    assert(0 == llvm::to_underlying(InterestingIdentifier::objc_not_keyword));
    InterestingIdentifierID = ID;
    assert(getObjCKeywordID() == ID && "ID too large for field!");
  }

  /// Return a value indicating whether this is a builtin function.
  unsigned getBuiltinID() const {
    auto Value = static_cast<InterestingIdentifier>(InterestingIdentifierID);
    if (Value >
            InterestingIdentifier::NUM_OBJC_KEYWORDS_AND_NOTABLE_IDENTIFIERS &&
        Value != InterestingIdentifier::NotInterestingIdentifier) {
      auto FirstBuiltin =
          llvm::to_underlying(InterestingIdentifier::NotBuiltin);
      return static_cast<Builtin::ID>(InterestingIdentifierID - FirstBuiltin);
    }
    return Builtin::ID::NotBuiltin;
  }
  void setBuiltinID(unsigned ID) {
    assert(ID != Builtin::ID::NotBuiltin);
    auto FirstBuiltin = llvm::to_underlying(InterestingIdentifier::NotBuiltin);
    InterestingIdentifierID = ID + FirstBuiltin;
    assert(getBuiltinID() == ID && "ID too large for field!");
  }
  void clearBuiltinID() {
    InterestingIdentifierID =
        llvm::to_underlying(InterestingIdentifier::NotInterestingIdentifier);
  }

  tok::NotableIdentifierKind getNotableIdentifierID() const {
    auto Value = static_cast<InterestingIdentifier>(InterestingIdentifierID);
    if (Value > InterestingIdentifier::NUM_OBJC_KEYWORDS &&
        Value <
            InterestingIdentifier::NUM_OBJC_KEYWORDS_AND_NOTABLE_IDENTIFIERS) {
      auto FirstNotableIdentifier =
          1 + llvm::to_underlying(InterestingIdentifier::NUM_OBJC_KEYWORDS);
      return static_cast<tok::NotableIdentifierKind>(InterestingIdentifierID -
                                                     FirstNotableIdentifier);
    }
    return tok::not_notable;
  }
  void setNotableIdentifierID(unsigned ID) {
    assert(ID != tok::not_notable);
    auto FirstNotableIdentifier =
        1 + llvm::to_underlying(InterestingIdentifier::NUM_OBJC_KEYWORDS);
    InterestingIdentifierID = ID + FirstNotableIdentifier;
    assert(getNotableIdentifierID() == ID && "ID too large for field!");
  }

  unsigned getObjCOrBuiltinID() const { return InterestingIdentifierID; }
  void setObjCOrBuiltinID(unsigned ID) { InterestingIdentifierID = ID; }

  /// get/setExtension - Initialize information about whether or not this
  /// language token is an extension.  This controls extension warnings, and is
  /// only valid if a custom token ID is set.
  bool isExtensionToken() const { return IsExtension; }
  void setIsExtensionToken(bool Val) {
    IsExtension = Val;
    if (Val)
      NeedsHandleIdentifier = true;
    else
      RecomputeNeedsHandleIdentifier();
  }

  /// is/setIsFutureCompatKeyword - Initialize information about whether or not
  /// this language token is a keyword in a newer or proposed Standard. This
  /// controls compatibility warnings, and is only true when not parsing the
  /// corresponding Standard. Once a compatibility problem has been diagnosed
  /// with this keyword, the flag will be cleared.
  bool isFutureCompatKeyword() const { return IsFutureCompatKeyword; }
  void setIsFutureCompatKeyword(bool Val) {
    IsFutureCompatKeyword = Val;
    if (Val)
      NeedsHandleIdentifier = true;
    else
      RecomputeNeedsHandleIdentifier();
  }

  /// setIsPoisoned - Mark this identifier as poisoned.  After poisoning, the
  /// Preprocessor will emit an error every time this token is used.
  void setIsPoisoned(bool Value = true) {
    IsPoisoned = Value;
    if (Value)
      NeedsHandleIdentifier = true;
    else
      RecomputeNeedsHandleIdentifier();
  }

  /// Return true if this token has been poisoned.
  bool isPoisoned() const { return IsPoisoned; }

  /// isCPlusPlusOperatorKeyword/setIsCPlusPlusOperatorKeyword controls whether
  /// this identifier is a C++ alternate representation of an operator.
  void setIsCPlusPlusOperatorKeyword(bool Val = true) {
    IsCPPOperatorKeyword = Val;
  }
  bool isCPlusPlusOperatorKeyword() const { return IsCPPOperatorKeyword; }

  /// Return true if this token is a keyword in the specified language.
  bool isKeyword(const LangOptions &LangOpts) const;

  /// Return true if this token is a C++ keyword in the specified
  /// language.
  bool isCPlusPlusKeyword(const LangOptions &LangOpts) const;

  /// Get and set FETokenInfo. The language front-end is allowed to associate
  /// arbitrary metadata with this token.
  void *getFETokenInfo() const { return FETokenInfo; }
  void setFETokenInfo(void *T) { FETokenInfo = T; }

  /// Return true if the Preprocessor::HandleIdentifier must be called
  /// on a token of this identifier.
  ///
  /// If this returns false, we know that HandleIdentifier will not affect
  /// the token.
  bool isHandleIdentifierCase() const { return NeedsHandleIdentifier; }

  /// Return true if the identifier in its current state was loaded
  /// from an AST file.
  bool isFromAST() const { return IsFromAST; }

  void setIsFromAST() { IsFromAST = true; }

  /// Determine whether this identifier has changed since it was loaded
  /// from an AST file.
  bool hasChangedSinceDeserialization() const {
    return ChangedAfterLoad;
  }

  /// Note that this identifier has changed since it was loaded from
  /// an AST file.
  void setChangedSinceDeserialization() {
    ChangedAfterLoad = true;
  }

  /// Determine whether the frontend token information for this
  /// identifier has changed since it was loaded from an AST file.
  bool hasFETokenInfoChangedSinceDeserialization() const {
    return FEChangedAfterLoad;
  }

  /// Note that the frontend token information for this identifier has
  /// changed since it was loaded from an AST file.
  void setFETokenInfoChangedSinceDeserialization() {
    FEChangedAfterLoad = true;
  }

  /// Determine whether the information for this identifier is out of
  /// date with respect to the external source.
  bool isOutOfDate() const { return OutOfDate; }

  /// Set whether the information for this identifier is out of
  /// date with respect to the external source.
  void setOutOfDate(bool OOD) {
    OutOfDate = OOD;
    if (OOD)
      NeedsHandleIdentifier = true;
    else
      RecomputeNeedsHandleIdentifier();
  }

  /// Determine whether this is the contextual keyword \c import.
  bool isModulesImport() const { return IsModulesImport; }

  /// Set whether this identifier is the contextual keyword \c import.
  void setModulesImport(bool I) {
    IsModulesImport = I;
    if (I)
      NeedsHandleIdentifier = true;
    else
      RecomputeNeedsHandleIdentifier();
  }

  /// Determine whether this is the mangled name of an OpenMP variant.
  bool isMangledOpenMPVariantName() const { return IsMangledOpenMPVariantName; }

  /// Set whether this is the mangled name of an OpenMP variant.
  void setMangledOpenMPVariantName(bool I) { IsMangledOpenMPVariantName = I; }

  /// Return true if this identifier is an editor placeholder.
  ///
  /// Editor placeholders are produced by the code-completion engine and are
  /// represented as characters between '<#' and '#>' in the source code. An
  /// example of auto-completed call with a placeholder parameter is shown
  /// below:
  /// \code
  ///   function(<#int x#>);
  /// \endcode
  bool isEditorPlaceholder() const {
    return getName().starts_with("<#") && getName().ends_with("#>");
  }

  /// Determine whether \p this is a name reserved for the implementation (C99
  /// 7.1.3, C++ [lib.global.names]).
  ReservedIdentifierStatus isReserved(const LangOptions &LangOpts) const;

  /// Determine whether \p this is a name reserved for future standardization or
  /// the implementation (C++ [usrlit.suffix]).
  ReservedLiteralSuffixIdStatus isReservedLiteralSuffixId() const;

  /// If the identifier is an "uglified" reserved name, return a cleaned form.
  /// e.g. _Foo => Foo. Otherwise, just returns the name.
  StringRef deuglifiedName() const;
  bool isPlaceholder() const {
    return getLength() == 1 && getNameStart()[0] == '_';
  }

  /// Provide less than operator for lexicographical sorting.
  bool operator<(const IdentifierInfo &RHS) const {
    return getName() < RHS.getName();
  }

private:
  /// The Preprocessor::HandleIdentifier does several special (but rare)
  /// things to identifiers of various sorts.  For example, it changes the
  /// \c for keyword token from tok::identifier to tok::for.
  ///
  /// This method is very tied to the definition of HandleIdentifier.  Any
  /// change to it should be reflected here.
  void RecomputeNeedsHandleIdentifier() {
    NeedsHandleIdentifier = isPoisoned() || hasMacroDefinition() ||
                            isExtensionToken() || isFutureCompatKeyword() ||
                            isOutOfDate() || isModulesImport();
  }
};

/// An RAII object for [un]poisoning an identifier within a scope.
///
/// \p II is allowed to be null, in which case objects of this type have
/// no effect.
class PoisonIdentifierRAIIObject {
  IdentifierInfo *const II;
  const bool OldValue;

public:
  PoisonIdentifierRAIIObject(IdentifierInfo *II, bool NewValue)
    : II(II), OldValue(II ? II->isPoisoned() : false) {
    if(II)
      II->setIsPoisoned(NewValue);
  }

  ~PoisonIdentifierRAIIObject() {
    if(II)
      II->setIsPoisoned(OldValue);
  }
};

/// An iterator that walks over all of the known identifiers
/// in the lookup table.
///
/// Since this iterator uses an abstract interface via virtual
/// functions, it uses an object-oriented interface rather than the
/// more standard C++ STL iterator interface. In this OO-style
/// iteration, the single function \c Next() provides dereference,
/// advance, and end-of-sequence checking in a single
/// operation. Subclasses of this iterator type will provide the
/// actual functionality.
class IdentifierIterator {
protected:
  IdentifierIterator() = default;

public:
  IdentifierIterator(const IdentifierIterator &) = delete;
  IdentifierIterator &operator=(const IdentifierIterator &) = delete;

  virtual ~IdentifierIterator();

  /// Retrieve the next string in the identifier table and
  /// advances the iterator for the following string.
  ///
  /// \returns The next string in the identifier table. If there is
  /// no such string, returns an empty \c StringRef.
  virtual StringRef Next() = 0;
};

/// Provides lookups to, and iteration over, IdentiferInfo objects.
class IdentifierInfoLookup {
public:
  virtual ~IdentifierInfoLookup();

  /// Return the IdentifierInfo for the specified named identifier.
  ///
  /// Unlike the version in IdentifierTable, this returns a pointer instead
  /// of a reference.  If the pointer is null then the IdentifierInfo cannot
  /// be found.
  virtual IdentifierInfo* get(StringRef Name) = 0;

  /// Retrieve an iterator into the set of all identifiers
  /// known to this identifier lookup source.
  ///
  /// This routine provides access to all of the identifiers known to
  /// the identifier lookup, allowing access to the contents of the
  /// identifiers without introducing the overhead of constructing
  /// IdentifierInfo objects for each.
  ///
  /// \returns A new iterator into the set of known identifiers. The
  /// caller is responsible for deleting this iterator.
  virtual IdentifierIterator *getIdentifiers();
};

/// Implements an efficient mapping from strings to IdentifierInfo nodes.
///
/// This has no other purpose, but this is an extremely performance-critical
/// piece of the code, as each occurrence of every identifier goes through
/// here when lexed.
class IdentifierTable {
  // Shark shows that using MallocAllocator is *much* slower than using this
  // BumpPtrAllocator!
  using HashTableTy = llvm::StringMap<IdentifierInfo *, llvm::BumpPtrAllocator>;
  HashTableTy HashTable;

  IdentifierInfoLookup* ExternalLookup;

public:
  /// Create the identifier table.
  explicit IdentifierTable(IdentifierInfoLookup *ExternalLookup = nullptr);

  /// Create the identifier table, populating it with info about the
  /// language keywords for the language specified by \p LangOpts.
  explicit IdentifierTable(const LangOptions &LangOpts,
                           IdentifierInfoLookup *ExternalLookup = nullptr);

  /// Set the external identifier lookup mechanism.
  void setExternalIdentifierLookup(IdentifierInfoLookup *IILookup) {
    ExternalLookup = IILookup;
  }

  /// Retrieve the external identifier lookup object, if any.
  IdentifierInfoLookup *getExternalIdentifierLookup() const {
    return ExternalLookup;
  }

  llvm::BumpPtrAllocator& getAllocator() {
    return HashTable.getAllocator();
  }

  /// Return the identifier token info for the specified named
  /// identifier.
  IdentifierInfo &get(StringRef Name) {
    auto &Entry = *HashTable.try_emplace(Name, nullptr).first;

    IdentifierInfo *&II = Entry.second;
    if (II) return *II;

    // No entry; if we have an external lookup, look there first.
    if (ExternalLookup) {
      II = ExternalLookup->get(Name);
      if (II)
        return *II;
    }

    // Lookups failed, make a new IdentifierInfo.
    void *Mem = getAllocator().Allocate<IdentifierInfo>();
    II = new (Mem) IdentifierInfo();

    // Make sure getName() knows how to find the IdentifierInfo
    // contents.
    II->Entry = &Entry;

    return *II;
  }

  IdentifierInfo &get(StringRef Name, tok::TokenKind TokenCode) {
    IdentifierInfo &II = get(Name);
    II.TokenID = TokenCode;
    assert(II.TokenID == (unsigned) TokenCode && "TokenCode too large");
    return II;
  }

  /// Gets an IdentifierInfo for the given name without consulting
  ///        external sources.
  ///
  /// This is a version of get() meant for external sources that want to
  /// introduce or modify an identifier. If they called get(), they would
  /// likely end up in a recursion.
  IdentifierInfo &getOwn(StringRef Name) {
    auto &Entry = *HashTable.insert(std::make_pair(Name, nullptr)).first;

    IdentifierInfo *&II = Entry.second;
    if (II)
      return *II;

    // Lookups failed, make a new IdentifierInfo.
    void *Mem = getAllocator().Allocate<IdentifierInfo>();
    II = new (Mem) IdentifierInfo();

    // Make sure getName() knows how to find the IdentifierInfo
    // contents.
    II->Entry = &Entry;

    // If this is the 'import' contextual keyword, mark it as such.
    if (Name == "import")
      II->setModulesImport(true);

    return *II;
  }

  using iterator = HashTableTy::const_iterator;
  using const_iterator = HashTableTy::const_iterator;

  iterator begin() const { return HashTable.begin(); }
  iterator end() const   { return HashTable.end(); }
  unsigned size() const  { return HashTable.size(); }

  iterator find(StringRef Name) const { return HashTable.find(Name); }

  /// Print some statistics to stderr that indicate how well the
  /// hashing is doing.
  void PrintStats() const;

  /// Populate the identifier table with info about the language keywords
  /// for the language specified by \p LangOpts.
  void AddKeywords(const LangOptions &LangOpts);

  /// Returns the correct diagnostic to issue for a future-compat diagnostic
  /// warning. Note, this function assumes the identifier passed has already
  /// been determined to be a future compatible keyword.
  diag::kind getFutureCompatDiagKind(const IdentifierInfo &II,
                                     const LangOptions &LangOpts);
};

/// A family of Objective-C methods.
///
/// These families have no inherent meaning in the language, but are
/// nonetheless central enough in the existing implementations to
/// merit direct AST support.  While, in theory, arbitrary methods can
/// be considered to form families, we focus here on the methods
/// involving allocation and retain-count management, as these are the
/// most "core" and the most likely to be useful to diverse clients
/// without extra information.
///
/// Both selectors and actual method declarations may be classified
/// into families.  Method families may impose additional restrictions
/// beyond their selector name; for example, a method called '_init'
/// that returns void is not considered to be in the 'init' family
/// (but would be if it returned 'id').  It is also possible to
/// explicitly change or remove a method's family.  Therefore the
/// method's family should be considered the single source of truth.
enum ObjCMethodFamily {
  /// No particular method family.
  OMF_None,

  // Selectors in these families may have arbitrary arity, may be
  // written with arbitrary leading underscores, and may have
  // additional CamelCase "words" in their first selector chunk
  // following the family name.
  OMF_alloc,
  OMF_copy,
  OMF_init,
  OMF_mutableCopy,
  OMF_new,

  // These families are singletons consisting only of the nullary
  // selector with the given name.
  OMF_autorelease,
  OMF_dealloc,
  OMF_finalize,
  OMF_release,
  OMF_retain,
  OMF_retainCount,
  OMF_self,
  OMF_initialize,

  // performSelector families
  OMF_performSelector
};

/// Enough bits to store any enumerator in ObjCMethodFamily or
/// InvalidObjCMethodFamily.
enum { ObjCMethodFamilyBitWidth = 4 };

/// An invalid value of ObjCMethodFamily.
enum { InvalidObjCMethodFamily = (1 << ObjCMethodFamilyBitWidth) - 1 };

/// A family of Objective-C methods.
///
/// These are family of methods whose result type is initially 'id', but
/// but are candidate for the result type to be changed to 'instancetype'.
enum ObjCInstanceTypeFamily {
  OIT_None,
  OIT_Array,
  OIT_Dictionary,
  OIT_Singleton,
  OIT_Init,
  OIT_ReturnsSelf
};

enum ObjCStringFormatFamily {
  SFF_None,
  SFF_NSString,
  SFF_CFString
};

namespace detail {

/// DeclarationNameExtra is used as a base of various uncommon special names.
/// This class is needed since DeclarationName has not enough space to store
/// the kind of every possible names. Therefore the kind of common names is
/// stored directly in DeclarationName, and the kind of uncommon names is
/// stored in DeclarationNameExtra. It is aligned to 8 bytes because
/// DeclarationName needs the lower 3 bits to store the kind of common names.
/// DeclarationNameExtra is tightly coupled to DeclarationName and any change
/// here is very likely to require changes in DeclarationName(Table).
class alignas(IdentifierInfoAlignment) DeclarationNameExtra {
  friend class clang::DeclarationName;
  friend class clang::DeclarationNameTable;

protected:
  /// The kind of "extra" information stored in the DeclarationName. See
  /// @c ExtraKindOrNumArgs for an explanation of how these enumerator values
  /// are used. Note that DeclarationName depends on the numerical values
  /// of the enumerators in this enum. See DeclarationName::StoredNameKind
  /// for more info.
  enum ExtraKind {
    CXXDeductionGuideName,
    CXXLiteralOperatorName,
    CXXUsingDirective,
    ObjCMultiArgSelector
  };

  /// ExtraKindOrNumArgs has one of the following meaning:
  ///  * The kind of an uncommon C++ special name. This DeclarationNameExtra
  ///    is in this case in fact either a CXXDeductionGuideNameExtra or
  ///    a CXXLiteralOperatorIdName.
  ///
  ///  * It may be also name common to C++ using-directives (CXXUsingDirective),
  ///
  ///  * Otherwise it is ObjCMultiArgSelector+NumArgs, where NumArgs is
  ///    the number of arguments in the Objective-C selector, in which
  ///    case the DeclarationNameExtra is also a MultiKeywordSelector.
  unsigned ExtraKindOrNumArgs;

  DeclarationNameExtra(ExtraKind Kind) : ExtraKindOrNumArgs(Kind) {}
  DeclarationNameExtra(unsigned NumArgs)
      : ExtraKindOrNumArgs(ObjCMultiArgSelector + NumArgs) {}

  /// Return the corresponding ExtraKind.
  ExtraKind getKind() const {
    return static_cast<ExtraKind>(ExtraKindOrNumArgs >
                                          (unsigned)ObjCMultiArgSelector
                                      ? (unsigned)ObjCMultiArgSelector
                                      : ExtraKindOrNumArgs);
  }

  /// Return the number of arguments in an ObjC selector. Only valid when this
  /// is indeed an ObjCMultiArgSelector.
  unsigned getNumArgs() const {
    assert(ExtraKindOrNumArgs >= (unsigned)ObjCMultiArgSelector &&
           "getNumArgs called but this is not an ObjC selector!");
    return ExtraKindOrNumArgs - (unsigned)ObjCMultiArgSelector;
  }
};

} // namespace detail

/// One of these variable length records is kept for each
/// selector containing more than one keyword. We use a folding set
/// to unique aggregate names (keyword selectors in ObjC parlance). Access to
/// this class is provided strictly through Selector.
class alignas(IdentifierInfoAlignment) MultiKeywordSelector
    : public detail::DeclarationNameExtra,
      public llvm::FoldingSetNode {
  MultiKeywordSelector(unsigned nKeys) : DeclarationNameExtra(nKeys) {}

public:
  // Constructor for keyword selectors.
  MultiKeywordSelector(unsigned nKeys, const IdentifierInfo **IIV)
      : DeclarationNameExtra(nKeys) {
    assert((nKeys > 1) && "not a multi-keyword selector");

    // Fill in the trailing keyword array.
    const IdentifierInfo **KeyInfo =
        reinterpret_cast<const IdentifierInfo **>(this + 1);
    for (unsigned i = 0; i != nKeys; ++i)
      KeyInfo[i] = IIV[i];
  }

  // getName - Derive the full selector name and return it.
  std::string getName() const;

  using DeclarationNameExtra::getNumArgs;

  using keyword_iterator = const IdentifierInfo *const *;

  keyword_iterator keyword_begin() const {
    return reinterpret_cast<keyword_iterator>(this + 1);
  }

  keyword_iterator keyword_end() const {
    return keyword_begin() + getNumArgs();
  }

  const IdentifierInfo *getIdentifierInfoForSlot(unsigned i) const {
    assert(i < getNumArgs() && "getIdentifierInfoForSlot(): illegal index");
    return keyword_begin()[i];
  }

  static void Profile(llvm::FoldingSetNodeID &ID, keyword_iterator ArgTys,
                      unsigned NumArgs) {
    ID.AddInteger(NumArgs);
    for (unsigned i = 0; i != NumArgs; ++i)
      ID.AddPointer(ArgTys[i]);
  }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, keyword_begin(), getNumArgs());
  }
};

/// Smart pointer class that efficiently represents Objective-C method
/// names.
///
/// This class will either point to an IdentifierInfo or a
/// MultiKeywordSelector (which is private). This enables us to optimize
/// selectors that take no arguments and selectors that take 1 argument, which
/// accounts for 78% of all selectors in Cocoa.h.
class Selector {
  friend class Diagnostic;
  friend class SelectorTable; // only the SelectorTable can create these
  friend class DeclarationName; // and the AST's DeclarationName.

  enum IdentifierInfoFlag {
    // Empty selector = 0. Note that these enumeration values must
    // correspond to the enumeration values of DeclarationName::StoredNameKind
    ZeroArg = 0x01,
    OneArg = 0x02,
    // IMPORTANT NOTE: see comments in InfoPtr (below) about this enumerator
    // value.
    MultiArg = 0x07,
  };

  /// IMPORTANT NOTE: the order of the types in this PointerUnion are
  /// important! The DeclarationName class has bidirectional conversion
  /// to/from Selector through an opaque pointer (void *) which corresponds
  /// to this PointerIntPair. The discriminator bit from the PointerUnion
  /// corresponds to the high bit in the MultiArg enumerator. So while this
  /// PointerIntPair only has two bits for the integer (and we mask off the
  /// high bit in `MultiArg` when it is used), that discrimator bit is
  /// still necessary for the opaque conversion. The discriminator bit
  /// from the PointerUnion and the two integer bits from the
  /// PointerIntPair are also exposed via the DeclarationName::StoredNameKind
  /// enumeration; see the comments in DeclarationName.h for more details.
  /// Do not reorder or add any arguments to this template
  /// without thoroughly understanding how tightly coupled these classes are.
  llvm::PointerIntPair<
      llvm::PointerUnion<const IdentifierInfo *, MultiKeywordSelector *>, 2>
      InfoPtr;

  Selector(const IdentifierInfo *II, unsigned nArgs) {
    assert(nArgs < 2 && "nArgs not equal to 0/1");
    InfoPtr.setPointerAndInt(II, nArgs + 1);
  }

  Selector(MultiKeywordSelector *SI) {
    // IMPORTANT NOTE: we mask off the upper bit of this value because we only
    // reserve two bits for the integer in the PointerIntPair. See the comments
    // in `InfoPtr` for more details.
    InfoPtr.setPointerAndInt(SI, MultiArg & 0b11);
  }

  const IdentifierInfo *getAsIdentifierInfo() const {
    return InfoPtr.getPointer().dyn_cast<const IdentifierInfo *>();
  }

  MultiKeywordSelector *getMultiKeywordSelector() const {
    return InfoPtr.getPointer().get<MultiKeywordSelector *>();
  }

  unsigned getIdentifierInfoFlag() const {
    unsigned new_flags = InfoPtr.getInt();
    // IMPORTANT NOTE: We have to reconstitute this data rather than use the
    // value directly from the PointerIntPair. See the comments in `InfoPtr`
    // for more details.
    if (InfoPtr.getPointer().is<MultiKeywordSelector *>())
      new_flags |= MultiArg;
    return new_flags;
  }

  static ObjCMethodFamily getMethodFamilyImpl(Selector sel);

  static ObjCStringFormatFamily getStringFormatFamilyImpl(Selector sel);

public:
  /// The default ctor should only be used when creating data structures that
  ///  will contain selectors.
  Selector() = default;
  explicit Selector(uintptr_t V) {
    InfoPtr.setFromOpaqueValue(reinterpret_cast<void *>(V));
  }

  /// operator==/!= - Indicate whether the specified selectors are identical.
  bool operator==(Selector RHS) const {
    return InfoPtr.getOpaqueValue() == RHS.InfoPtr.getOpaqueValue();
  }
  bool operator!=(Selector RHS) const {
    return InfoPtr.getOpaqueValue() != RHS.InfoPtr.getOpaqueValue();
  }

  void *getAsOpaquePtr() const { return InfoPtr.getOpaqueValue(); }

  /// Determine whether this is the empty selector.
  bool isNull() const { return InfoPtr.getOpaqueValue() == nullptr; }

  // Predicates to identify the selector type.
  bool isKeywordSelector() const { return InfoPtr.getInt() != ZeroArg; }

  bool isUnarySelector() const { return InfoPtr.getInt() == ZeroArg; }

  /// If this selector is the specific keyword selector described by Names.
  bool isKeywordSelector(ArrayRef<StringRef> Names) const;

  /// If this selector is the specific unary selector described by Name.
  bool isUnarySelector(StringRef Name) const;

  unsigned getNumArgs() const;

  /// Retrieve the identifier at a given position in the selector.
  ///
  /// Note that the identifier pointer returned may be NULL. Clients that only
  /// care about the text of the identifier string, and not the specific,
  /// uniqued identifier pointer, should use \c getNameForSlot(), which returns
  /// an empty string when the identifier pointer would be NULL.
  ///
  /// \param argIndex The index for which we want to retrieve the identifier.
  /// This index shall be less than \c getNumArgs() unless this is a keyword
  /// selector, in which case 0 is the only permissible value.
  ///
  /// \returns the uniqued identifier for this slot, or NULL if this slot has
  /// no corresponding identifier.
  const IdentifierInfo *getIdentifierInfoForSlot(unsigned argIndex) const;

  /// Retrieve the name at a given position in the selector.
  ///
  /// \param argIndex The index for which we want to retrieve the name.
  /// This index shall be less than \c getNumArgs() unless this is a keyword
  /// selector, in which case 0 is the only permissible value.
  ///
  /// \returns the name for this slot, which may be the empty string if no
  /// name was supplied.
  StringRef getNameForSlot(unsigned argIndex) const;

  /// Derive the full selector name (e.g. "foo:bar:") and return
  /// it as an std::string.
  std::string getAsString() const;

  /// Prints the full selector name (e.g. "foo:bar:").
  void print(llvm::raw_ostream &OS) const;

  void dump() const;

  /// Derive the conventional family of this method.
  ObjCMethodFamily getMethodFamily() const {
    return getMethodFamilyImpl(*this);
  }

  ObjCStringFormatFamily getStringFormatFamily() const {
    return getStringFormatFamilyImpl(*this);
  }

  static Selector getEmptyMarker() {
    return Selector(uintptr_t(-1));
  }

  static Selector getTombstoneMarker() {
    return Selector(uintptr_t(-2));
  }

  static ObjCInstanceTypeFamily getInstTypeMethodFamily(Selector sel);
};

/// This table allows us to fully hide how we implement
/// multi-keyword caching.
class SelectorTable {
  // Actually a SelectorTableImpl
  void *Impl;

public:
  SelectorTable();
  SelectorTable(const SelectorTable &) = delete;
  SelectorTable &operator=(const SelectorTable &) = delete;
  ~SelectorTable();

  /// Can create any sort of selector.
  ///
  /// \p NumArgs indicates whether this is a no argument selector "foo", a
  /// single argument selector "foo:" or multi-argument "foo:bar:".
  Selector getSelector(unsigned NumArgs, const IdentifierInfo **IIV);

  Selector getUnarySelector(const IdentifierInfo *ID) {
    return Selector(ID, 1);
  }

  Selector getNullarySelector(const IdentifierInfo *ID) {
    return Selector(ID, 0);
  }

  /// Return the total amount of memory allocated for managing selectors.
  size_t getTotalMemory() const;

  /// Return the default setter name for the given identifier.
  ///
  /// This is "set" + \p Name where the initial character of \p Name
  /// has been capitalized.
  static SmallString<64> constructSetterName(StringRef Name);

  /// Return the default setter selector for the given identifier.
  ///
  /// This is "set" + \p Name where the initial character of \p Name
  /// has been capitalized.
  static Selector constructSetterSelector(IdentifierTable &Idents,
                                          SelectorTable &SelTable,
                                          const IdentifierInfo *Name);

  /// Return the property name for the given setter selector.
  static std::string getPropertyNameFromSetterSelector(Selector Sel);
};

}  // namespace clang

namespace llvm {

/// Define DenseMapInfo so that Selectors can be used as keys in DenseMap and
/// DenseSets.
template <>
struct DenseMapInfo<clang::Selector> {
  static clang::Selector getEmptyKey() {
    return clang::Selector::getEmptyMarker();
  }

  static clang::Selector getTombstoneKey() {
    return clang::Selector::getTombstoneMarker();
  }

  static unsigned getHashValue(clang::Selector S);

  static bool isEqual(clang::Selector LHS, clang::Selector RHS) {
    return LHS == RHS;
  }
};

template<>
struct PointerLikeTypeTraits<clang::Selector> {
  static const void *getAsVoidPointer(clang::Selector P) {
    return P.getAsOpaquePtr();
  }

  static clang::Selector getFromVoidPointer(const void *P) {
    return clang::Selector(reinterpret_cast<uintptr_t>(P));
  }

  static constexpr int NumLowBitsAvailable = 0;
};

} // namespace llvm

#endif // LLVM_CLANG_BASIC_IDENTIFIERTABLE_H

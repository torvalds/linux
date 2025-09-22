//===- IdentifierTable.cpp - Hash table for identifier lookup -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the IdentifierInfo, IdentifierVisitor, and
// IdentifierTable interfaces.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/DiagnosticLex.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Basic/TargetBuiltins.h"
#include "clang/Basic/TokenKinds.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace clang;

// A check to make sure the ObjCOrBuiltinID has sufficient room to store the
// largest possible target/aux-target combination. If we exceed this, we likely
// need to just change the ObjCOrBuiltinIDBits value in IdentifierTable.h.
static_assert(2 * LargestBuiltinID < (2 << (InterestingIdentifierBits - 1)),
              "Insufficient ObjCOrBuiltinID Bits");

//===----------------------------------------------------------------------===//
// IdentifierTable Implementation
//===----------------------------------------------------------------------===//

IdentifierIterator::~IdentifierIterator() = default;

IdentifierInfoLookup::~IdentifierInfoLookup() = default;

namespace {

/// A simple identifier lookup iterator that represents an
/// empty sequence of identifiers.
class EmptyLookupIterator : public IdentifierIterator {
public:
  StringRef Next() override { return StringRef(); }
};

} // namespace

IdentifierIterator *IdentifierInfoLookup::getIdentifiers() {
  return new EmptyLookupIterator();
}

IdentifierTable::IdentifierTable(IdentifierInfoLookup *ExternalLookup)
    : HashTable(8192), // Start with space for 8K identifiers.
      ExternalLookup(ExternalLookup) {}

IdentifierTable::IdentifierTable(const LangOptions &LangOpts,
                                 IdentifierInfoLookup *ExternalLookup)
    : IdentifierTable(ExternalLookup) {
  // Populate the identifier table with info about keywords for the current
  // language.
  AddKeywords(LangOpts);
}

//===----------------------------------------------------------------------===//
// Language Keyword Implementation
//===----------------------------------------------------------------------===//

// Constants for TokenKinds.def
namespace {

  enum TokenKey : unsigned {
    KEYC99        = 0x1,
    KEYCXX        = 0x2,
    KEYCXX11      = 0x4,
    KEYGNU        = 0x8,
    KEYMS         = 0x10,
    BOOLSUPPORT   = 0x20,
    KEYALTIVEC    = 0x40,
    KEYNOCXX      = 0x80,
    KEYBORLAND    = 0x100,
    KEYOPENCLC    = 0x200,
    KEYC23        = 0x400,
    KEYNOMS18     = 0x800,
    KEYNOOPENCL   = 0x1000,
    WCHARSUPPORT  = 0x2000,
    HALFSUPPORT   = 0x4000,
    CHAR8SUPPORT  = 0x8000,
    KEYOBJC       = 0x10000,
    KEYZVECTOR    = 0x20000,
    KEYCOROUTINES = 0x40000,
    KEYMODULES    = 0x80000,
    KEYCXX20      = 0x100000,
    KEYOPENCLCXX  = 0x200000,
    KEYMSCOMPAT   = 0x400000,
    KEYSYCL       = 0x800000,
    KEYCUDA       = 0x1000000,
    KEYHLSL       = 0x2000000,
    KEYFIXEDPOINT = 0x4000000,
    KEYMAX        = KEYFIXEDPOINT, // The maximum key
    KEYALLCXX = KEYCXX | KEYCXX11 | KEYCXX20,
    KEYALL = (KEYMAX | (KEYMAX-1)) & ~KEYNOMS18 &
             ~KEYNOOPENCL // KEYNOMS18 and KEYNOOPENCL are used to exclude.
  };

  /// How a keyword is treated in the selected standard. This enum is ordered
  /// intentionally so that the value that 'wins' is the most 'permissive'.
  enum KeywordStatus {
    KS_Unknown,     // Not yet calculated. Used when figuring out the status.
    KS_Disabled,    // Disabled
    KS_Future,      // Is a keyword in future standard
    KS_Extension,   // Is an extension
    KS_Enabled,     // Enabled
  };

} // namespace

// This works on a single TokenKey flag and checks the LangOpts to get the
// KeywordStatus based exclusively on this flag, so that it can be merged in
// getKeywordStatus. Most should be enabled/disabled, but some might imply
// 'future' versions, or extensions. Returns 'unknown' unless this is KNOWN to
// be disabled, and the calling function makes it 'disabled' if no other flag
// changes it. This is necessary for the KEYNOCXX and KEYNOOPENCL flags.
static KeywordStatus getKeywordStatusHelper(const LangOptions &LangOpts,
                                            TokenKey Flag) {
  // Flag is a single bit version of TokenKey (that is, not
  // KEYALL/KEYALLCXX/etc), so we can check with == throughout this function.
  assert((Flag & ~(Flag - 1)) == Flag && "Multiple bits set?");

  switch (Flag) {
  case KEYC99:
    if (LangOpts.C99)
      return KS_Enabled;
    return !LangOpts.CPlusPlus ? KS_Future : KS_Unknown;
  case KEYC23:
    if (LangOpts.C23)
      return KS_Enabled;
    return !LangOpts.CPlusPlus ? KS_Future : KS_Unknown;
  case KEYCXX:
    return LangOpts.CPlusPlus ? KS_Enabled : KS_Unknown;
  case KEYCXX11:
    if (LangOpts.CPlusPlus11)
      return KS_Enabled;
    return LangOpts.CPlusPlus ? KS_Future : KS_Unknown;
  case KEYCXX20:
    if (LangOpts.CPlusPlus20)
      return KS_Enabled;
    return LangOpts.CPlusPlus ? KS_Future : KS_Unknown;
  case KEYGNU:
    return LangOpts.GNUKeywords ? KS_Extension : KS_Unknown;
  case KEYMS:
    return LangOpts.MicrosoftExt ? KS_Extension : KS_Unknown;
  case BOOLSUPPORT:
    if (LangOpts.Bool)      return KS_Enabled;
    return !LangOpts.CPlusPlus ? KS_Future : KS_Unknown;
  case KEYALTIVEC:
    return LangOpts.AltiVec ? KS_Enabled : KS_Unknown;
  case KEYBORLAND:
    return LangOpts.Borland ? KS_Extension : KS_Unknown;
  case KEYOPENCLC:
    return LangOpts.OpenCL && !LangOpts.OpenCLCPlusPlus ? KS_Enabled
                                                        : KS_Unknown;
  case WCHARSUPPORT:
    return LangOpts.WChar ? KS_Enabled : KS_Unknown;
  case HALFSUPPORT:
    return LangOpts.Half ? KS_Enabled : KS_Unknown;
  case CHAR8SUPPORT:
    if (LangOpts.Char8) return KS_Enabled;
    if (LangOpts.CPlusPlus20) return KS_Unknown;
    if (LangOpts.CPlusPlus) return KS_Future;
    return KS_Unknown;
  case KEYOBJC:
    // We treat bridge casts as objective-C keywords so we can warn on them
    // in non-arc mode.
    return LangOpts.ObjC ? KS_Enabled : KS_Unknown;
  case KEYZVECTOR:
    return LangOpts.ZVector ? KS_Enabled : KS_Unknown;
  case KEYCOROUTINES:
    return LangOpts.Coroutines ? KS_Enabled : KS_Unknown;
  case KEYMODULES:
    return KS_Unknown;
  case KEYOPENCLCXX:
    return LangOpts.OpenCLCPlusPlus ? KS_Enabled : KS_Unknown;
  case KEYMSCOMPAT:
    return LangOpts.MSVCCompat ? KS_Enabled : KS_Unknown;
  case KEYSYCL:
    return LangOpts.isSYCL() ? KS_Enabled : KS_Unknown;
  case KEYCUDA:
    return LangOpts.CUDA ? KS_Enabled : KS_Unknown;
  case KEYHLSL:
    return LangOpts.HLSL ? KS_Enabled : KS_Unknown;
  case KEYNOCXX:
    // This is enabled in all non-C++ modes, but might be enabled for other
    // reasons as well.
    return LangOpts.CPlusPlus ? KS_Unknown : KS_Enabled;
  case KEYNOOPENCL:
    // The disable behavior for this is handled in getKeywordStatus.
    return KS_Unknown;
  case KEYNOMS18:
    // The disable behavior for this is handled in getKeywordStatus.
    return KS_Unknown;
  case KEYFIXEDPOINT:
    return LangOpts.FixedPoint ? KS_Enabled : KS_Disabled;
  default:
    llvm_unreachable("Unknown KeywordStatus flag");
  }
}

/// Translates flags as specified in TokenKinds.def into keyword status
/// in the given language standard.
static KeywordStatus getKeywordStatus(const LangOptions &LangOpts,
                                      unsigned Flags) {
  // KEYALL means always enabled, so special case this one.
  if (Flags == KEYALL) return KS_Enabled;
  // These are tests that need to 'always win', as they are special in that they
  // disable based on certain conditions.
  if (LangOpts.OpenCL && (Flags & KEYNOOPENCL)) return KS_Disabled;
  if (LangOpts.MSVCCompat && (Flags & KEYNOMS18) &&
      !LangOpts.isCompatibleWithMSVC(LangOptions::MSVC2015))
    return KS_Disabled;

  KeywordStatus CurStatus = KS_Unknown;

  while (Flags != 0) {
    unsigned CurFlag = Flags & ~(Flags - 1);
    Flags = Flags & ~CurFlag;
    CurStatus = std::max(
        CurStatus,
        getKeywordStatusHelper(LangOpts, static_cast<TokenKey>(CurFlag)));
  }

  if (CurStatus == KS_Unknown)
    return KS_Disabled;
  return CurStatus;
}

/// AddKeyword - This method is used to associate a token ID with specific
/// identifiers because they are language keywords.  This causes the lexer to
/// automatically map matching identifiers to specialized token codes.
static void AddKeyword(StringRef Keyword,
                       tok::TokenKind TokenCode, unsigned Flags,
                       const LangOptions &LangOpts, IdentifierTable &Table) {
  KeywordStatus AddResult = getKeywordStatus(LangOpts, Flags);

  // Don't add this keyword if disabled in this language.
  if (AddResult == KS_Disabled) return;

  IdentifierInfo &Info =
      Table.get(Keyword, AddResult == KS_Future ? tok::identifier : TokenCode);
  Info.setIsExtensionToken(AddResult == KS_Extension);
  Info.setIsFutureCompatKeyword(AddResult == KS_Future);
}

/// AddCXXOperatorKeyword - Register a C++ operator keyword alternative
/// representations.
static void AddCXXOperatorKeyword(StringRef Keyword,
                                  tok::TokenKind TokenCode,
                                  IdentifierTable &Table) {
  IdentifierInfo &Info = Table.get(Keyword, TokenCode);
  Info.setIsCPlusPlusOperatorKeyword();
}

/// AddObjCKeyword - Register an Objective-C \@keyword like "class" "selector"
/// or "property".
static void AddObjCKeyword(StringRef Name,
                           tok::ObjCKeywordKind ObjCID,
                           IdentifierTable &Table) {
  Table.get(Name).setObjCKeywordID(ObjCID);
}

static void AddNotableIdentifier(StringRef Name,
                                 tok::NotableIdentifierKind BTID,
                                 IdentifierTable &Table) {
  // Don't add 'not_notable' identifier.
  if (BTID != tok::not_notable) {
    IdentifierInfo &Info = Table.get(Name, tok::identifier);
    Info.setNotableIdentifierID(BTID);
  }
}

/// AddKeywords - Add all keywords to the symbol table.
///
void IdentifierTable::AddKeywords(const LangOptions &LangOpts) {
  // Add keywords and tokens for the current language.
#define KEYWORD(NAME, FLAGS) \
  AddKeyword(StringRef(#NAME), tok::kw_ ## NAME,  \
             FLAGS, LangOpts, *this);
#define ALIAS(NAME, TOK, FLAGS) \
  AddKeyword(StringRef(NAME), tok::kw_ ## TOK,  \
             FLAGS, LangOpts, *this);
#define CXX_KEYWORD_OPERATOR(NAME, ALIAS) \
  if (LangOpts.CXXOperatorNames)          \
    AddCXXOperatorKeyword(StringRef(#NAME), tok::ALIAS, *this);
#define OBJC_AT_KEYWORD(NAME)  \
  if (LangOpts.ObjC)           \
    AddObjCKeyword(StringRef(#NAME), tok::objc_##NAME, *this);
#define NOTABLE_IDENTIFIER(NAME)                                               \
  AddNotableIdentifier(StringRef(#NAME), tok::NAME, *this);

#define TESTING_KEYWORD(NAME, FLAGS)
#include "clang/Basic/TokenKinds.def"

  if (LangOpts.ParseUnknownAnytype)
    AddKeyword("__unknown_anytype", tok::kw___unknown_anytype, KEYALL,
               LangOpts, *this);

  if (LangOpts.DeclSpecKeyword)
    AddKeyword("__declspec", tok::kw___declspec, KEYALL, LangOpts, *this);

  if (LangOpts.IEEE128)
    AddKeyword("__ieee128", tok::kw___float128, KEYALL, LangOpts, *this);

  // Add the 'import' contextual keyword.
  get("import").setModulesImport(true);
}

/// Checks if the specified token kind represents a keyword in the
/// specified language.
/// \returns Status of the keyword in the language.
static KeywordStatus getTokenKwStatus(const LangOptions &LangOpts,
                                      tok::TokenKind K) {
  switch (K) {
#define KEYWORD(NAME, FLAGS) \
  case tok::kw_##NAME: return getKeywordStatus(LangOpts, FLAGS);
#include "clang/Basic/TokenKinds.def"
  default: return KS_Disabled;
  }
}

/// Returns true if the identifier represents a keyword in the
/// specified language.
bool IdentifierInfo::isKeyword(const LangOptions &LangOpts) const {
  switch (getTokenKwStatus(LangOpts, getTokenID())) {
  case KS_Enabled:
  case KS_Extension:
    return true;
  default:
    return false;
  }
}

/// Returns true if the identifier represents a C++ keyword in the
/// specified language.
bool IdentifierInfo::isCPlusPlusKeyword(const LangOptions &LangOpts) const {
  if (!LangOpts.CPlusPlus || !isKeyword(LangOpts))
    return false;
  // This is a C++ keyword if this identifier is not a keyword when checked
  // using LangOptions without C++ support.
  LangOptions LangOptsNoCPP = LangOpts;
  LangOptsNoCPP.CPlusPlus = false;
  LangOptsNoCPP.CPlusPlus11 = false;
  LangOptsNoCPP.CPlusPlus20 = false;
  return !isKeyword(LangOptsNoCPP);
}

ReservedIdentifierStatus
IdentifierInfo::isReserved(const LangOptions &LangOpts) const {
  StringRef Name = getName();

  // '_' is a reserved identifier, but its use is so common (e.g. to store
  // ignored values) that we don't warn on it.
  if (Name.size() <= 1)
    return ReservedIdentifierStatus::NotReserved;

  // [lex.name] p3
  if (Name[0] == '_') {

    // Each name that begins with an underscore followed by an uppercase letter
    // or another underscore is reserved.
    if (Name[1] == '_')
      return ReservedIdentifierStatus::StartsWithDoubleUnderscore;

    if ('A' <= Name[1] && Name[1] <= 'Z')
      return ReservedIdentifierStatus::
          StartsWithUnderscoreFollowedByCapitalLetter;

    // This is a bit misleading: it actually means it's only reserved if we're
    // at global scope because it starts with an underscore.
    return ReservedIdentifierStatus::StartsWithUnderscoreAtGlobalScope;
  }

  // Each name that contains a double underscore (__) is reserved.
  if (LangOpts.CPlusPlus && Name.contains("__"))
    return ReservedIdentifierStatus::ContainsDoubleUnderscore;

  return ReservedIdentifierStatus::NotReserved;
}

ReservedLiteralSuffixIdStatus
IdentifierInfo::isReservedLiteralSuffixId() const {
  StringRef Name = getName();

  if (Name[0] != '_')
    return ReservedLiteralSuffixIdStatus::NotStartsWithUnderscore;

  if (Name.contains("__"))
    return ReservedLiteralSuffixIdStatus::ContainsDoubleUnderscore;

  return ReservedLiteralSuffixIdStatus::NotReserved;
}

StringRef IdentifierInfo::deuglifiedName() const {
  StringRef Name = getName();
  if (Name.size() >= 2 && Name.front() == '_' &&
      (Name[1] == '_' || (Name[1] >= 'A' && Name[1] <= 'Z')))
    return Name.ltrim('_');
  return Name;
}

tok::PPKeywordKind IdentifierInfo::getPPKeywordID() const {
  // We use a perfect hash function here involving the length of the keyword,
  // the first and third character.  For preprocessor ID's there are no
  // collisions (if there were, the switch below would complain about duplicate
  // case values).  Note that this depends on 'if' being null terminated.

#define HASH(LEN, FIRST, THIRD)                                                \
  (LEN << 6) + (((FIRST - 'a') - (THIRD - 'a')) & 63)
#define CASE(LEN, FIRST, THIRD, NAME) \
  case HASH(LEN, FIRST, THIRD): \
    return memcmp(Name, #NAME, LEN) ? tok::pp_not_keyword : tok::pp_ ## NAME

  unsigned Len = getLength();
  if (Len < 2) return tok::pp_not_keyword;
  const char *Name = getNameStart();
  switch (HASH(Len, Name[0], Name[2])) {
  default: return tok::pp_not_keyword;
  CASE( 2, 'i', '\0', if);
  CASE( 4, 'e', 'i', elif);
  CASE( 4, 'e', 's', else);
  CASE( 4, 'l', 'n', line);
  CASE( 4, 's', 'c', sccs);
  CASE( 5, 'e', 'b', embed);
  CASE( 5, 'e', 'd', endif);
  CASE( 5, 'e', 'r', error);
  CASE( 5, 'i', 'e', ident);
  CASE( 5, 'i', 'd', ifdef);
  CASE( 5, 'u', 'd', undef);

  CASE( 6, 'a', 's', assert);
  CASE( 6, 'd', 'f', define);
  CASE( 6, 'i', 'n', ifndef);
  CASE( 6, 'i', 'p', import);
  CASE( 6, 'p', 'a', pragma);

  CASE( 7, 'd', 'f', defined);
  CASE( 7, 'e', 'i', elifdef);
  CASE( 7, 'i', 'c', include);
  CASE( 7, 'w', 'r', warning);

  CASE( 8, 'e', 'i', elifndef);
  CASE( 8, 'u', 'a', unassert);
  CASE(12, 'i', 'c', include_next);

  CASE(14, '_', 'p', __public_macro);

  CASE(15, '_', 'p', __private_macro);

  CASE(16, '_', 'i', __include_macros);
#undef CASE
#undef HASH
  }
}

//===----------------------------------------------------------------------===//
// Stats Implementation
//===----------------------------------------------------------------------===//

/// PrintStats - Print statistics about how well the identifier table is doing
/// at hashing identifiers.
void IdentifierTable::PrintStats() const {
  unsigned NumBuckets = HashTable.getNumBuckets();
  unsigned NumIdentifiers = HashTable.getNumItems();
  unsigned NumEmptyBuckets = NumBuckets-NumIdentifiers;
  unsigned AverageIdentifierSize = 0;
  unsigned MaxIdentifierLength = 0;

  // TODO: Figure out maximum times an identifier had to probe for -stats.
  for (llvm::StringMap<IdentifierInfo*, llvm::BumpPtrAllocator>::const_iterator
       I = HashTable.begin(), E = HashTable.end(); I != E; ++I) {
    unsigned IdLen = I->getKeyLength();
    AverageIdentifierSize += IdLen;
    if (MaxIdentifierLength < IdLen)
      MaxIdentifierLength = IdLen;
  }

  fprintf(stderr, "\n*** Identifier Table Stats:\n");
  fprintf(stderr, "# Identifiers:   %d\n", NumIdentifiers);
  fprintf(stderr, "# Empty Buckets: %d\n", NumEmptyBuckets);
  fprintf(stderr, "Hash density (#identifiers per bucket): %f\n",
          NumIdentifiers/(double)NumBuckets);
  fprintf(stderr, "Ave identifier length: %f\n",
          (AverageIdentifierSize/(double)NumIdentifiers));
  fprintf(stderr, "Max identifier length: %d\n", MaxIdentifierLength);

  // Compute statistics about the memory allocated for identifiers.
  HashTable.getAllocator().PrintStats();
}

//===----------------------------------------------------------------------===//
// SelectorTable Implementation
//===----------------------------------------------------------------------===//

unsigned llvm::DenseMapInfo<clang::Selector>::getHashValue(clang::Selector S) {
  return DenseMapInfo<void*>::getHashValue(S.getAsOpaquePtr());
}

bool Selector::isKeywordSelector(ArrayRef<StringRef> Names) const {
  assert(!Names.empty() && "must have >= 1 selector slots");
  if (getNumArgs() != Names.size())
    return false;
  for (unsigned I = 0, E = Names.size(); I != E; ++I) {
    if (getNameForSlot(I) != Names[I])
      return false;
  }
  return true;
}

bool Selector::isUnarySelector(StringRef Name) const {
  return isUnarySelector() && getNameForSlot(0) == Name;
}

unsigned Selector::getNumArgs() const {
  unsigned IIF = getIdentifierInfoFlag();
  if (IIF <= ZeroArg)
    return 0;
  if (IIF == OneArg)
    return 1;
  // We point to a MultiKeywordSelector.
  MultiKeywordSelector *SI = getMultiKeywordSelector();
  return SI->getNumArgs();
}

const IdentifierInfo *
Selector::getIdentifierInfoForSlot(unsigned argIndex) const {
  if (getIdentifierInfoFlag() < MultiArg) {
    assert(argIndex == 0 && "illegal keyword index");
    return getAsIdentifierInfo();
  }

  // We point to a MultiKeywordSelector.
  MultiKeywordSelector *SI = getMultiKeywordSelector();
  return SI->getIdentifierInfoForSlot(argIndex);
}

StringRef Selector::getNameForSlot(unsigned int argIndex) const {
  const IdentifierInfo *II = getIdentifierInfoForSlot(argIndex);
  return II ? II->getName() : StringRef();
}

std::string MultiKeywordSelector::getName() const {
  SmallString<256> Str;
  llvm::raw_svector_ostream OS(Str);
  for (keyword_iterator I = keyword_begin(), E = keyword_end(); I != E; ++I) {
    if (*I)
      OS << (*I)->getName();
    OS << ':';
  }

  return std::string(OS.str());
}

std::string Selector::getAsString() const {
  if (isNull())
    return "<null selector>";

  if (getIdentifierInfoFlag() < MultiArg) {
    const IdentifierInfo *II = getAsIdentifierInfo();

    if (getNumArgs() == 0) {
      assert(II && "If the number of arguments is 0 then II is guaranteed to "
                   "not be null.");
      return std::string(II->getName());
    }

    if (!II)
      return ":";

    return II->getName().str() + ":";
  }

  // We have a multiple keyword selector.
  return getMultiKeywordSelector()->getName();
}

void Selector::print(llvm::raw_ostream &OS) const {
  OS << getAsString();
}

LLVM_DUMP_METHOD void Selector::dump() const { print(llvm::errs()); }

/// Interpreting the given string using the normal CamelCase
/// conventions, determine whether the given string starts with the
/// given "word", which is assumed to end in a lowercase letter.
static bool startsWithWord(StringRef name, StringRef word) {
  if (name.size() < word.size()) return false;
  return ((name.size() == word.size() || !isLowercase(name[word.size()])) &&
          name.starts_with(word));
}

ObjCMethodFamily Selector::getMethodFamilyImpl(Selector sel) {
  const IdentifierInfo *first = sel.getIdentifierInfoForSlot(0);
  if (!first) return OMF_None;

  StringRef name = first->getName();
  if (sel.isUnarySelector()) {
    if (name == "autorelease") return OMF_autorelease;
    if (name == "dealloc") return OMF_dealloc;
    if (name == "finalize") return OMF_finalize;
    if (name == "release") return OMF_release;
    if (name == "retain") return OMF_retain;
    if (name == "retainCount") return OMF_retainCount;
    if (name == "self") return OMF_self;
    if (name == "initialize") return OMF_initialize;
  }

  if (name == "performSelector" || name == "performSelectorInBackground" ||
      name == "performSelectorOnMainThread")
    return OMF_performSelector;

  // The other method families may begin with a prefix of underscores.
  name = name.ltrim('_');

  if (name.empty()) return OMF_None;
  switch (name.front()) {
  case 'a':
    if (startsWithWord(name, "alloc")) return OMF_alloc;
    break;
  case 'c':
    if (startsWithWord(name, "copy")) return OMF_copy;
    break;
  case 'i':
    if (startsWithWord(name, "init")) return OMF_init;
    break;
  case 'm':
    if (startsWithWord(name, "mutableCopy")) return OMF_mutableCopy;
    break;
  case 'n':
    if (startsWithWord(name, "new")) return OMF_new;
    break;
  default:
    break;
  }

  return OMF_None;
}

ObjCInstanceTypeFamily Selector::getInstTypeMethodFamily(Selector sel) {
  const IdentifierInfo *first = sel.getIdentifierInfoForSlot(0);
  if (!first) return OIT_None;

  StringRef name = first->getName();

  if (name.empty()) return OIT_None;
  switch (name.front()) {
    case 'a':
      if (startsWithWord(name, "array")) return OIT_Array;
      break;
    case 'd':
      if (startsWithWord(name, "default")) return OIT_ReturnsSelf;
      if (startsWithWord(name, "dictionary")) return OIT_Dictionary;
      break;
    case 's':
      if (startsWithWord(name, "shared")) return OIT_ReturnsSelf;
      if (startsWithWord(name, "standard")) return OIT_Singleton;
      break;
    case 'i':
      if (startsWithWord(name, "init")) return OIT_Init;
      break;
    default:
      break;
  }
  return OIT_None;
}

ObjCStringFormatFamily Selector::getStringFormatFamilyImpl(Selector sel) {
  const IdentifierInfo *first = sel.getIdentifierInfoForSlot(0);
  if (!first) return SFF_None;

  StringRef name = first->getName();

  switch (name.front()) {
    case 'a':
      if (name == "appendFormat") return SFF_NSString;
      break;

    case 'i':
      if (name == "initWithFormat") return SFF_NSString;
      break;

    case 'l':
      if (name == "localizedStringWithFormat") return SFF_NSString;
      break;

    case 's':
      if (name == "stringByAppendingFormat" ||
          name == "stringWithFormat") return SFF_NSString;
      break;
  }
  return SFF_None;
}

namespace {

struct SelectorTableImpl {
  llvm::FoldingSet<MultiKeywordSelector> Table;
  llvm::BumpPtrAllocator Allocator;
};

} // namespace

static SelectorTableImpl &getSelectorTableImpl(void *P) {
  return *static_cast<SelectorTableImpl*>(P);
}

SmallString<64>
SelectorTable::constructSetterName(StringRef Name) {
  SmallString<64> SetterName("set");
  SetterName += Name;
  SetterName[3] = toUppercase(SetterName[3]);
  return SetterName;
}

Selector
SelectorTable::constructSetterSelector(IdentifierTable &Idents,
                                       SelectorTable &SelTable,
                                       const IdentifierInfo *Name) {
  IdentifierInfo *SetterName =
    &Idents.get(constructSetterName(Name->getName()));
  return SelTable.getUnarySelector(SetterName);
}

std::string SelectorTable::getPropertyNameFromSetterSelector(Selector Sel) {
  StringRef Name = Sel.getNameForSlot(0);
  assert(Name.starts_with("set") && "invalid setter name");
  return (Twine(toLowercase(Name[3])) + Name.drop_front(4)).str();
}

size_t SelectorTable::getTotalMemory() const {
  SelectorTableImpl &SelTabImpl = getSelectorTableImpl(Impl);
  return SelTabImpl.Allocator.getTotalMemory();
}

Selector SelectorTable::getSelector(unsigned nKeys,
                                    const IdentifierInfo **IIV) {
  if (nKeys < 2)
    return Selector(IIV[0], nKeys);

  SelectorTableImpl &SelTabImpl = getSelectorTableImpl(Impl);

  // Unique selector, to guarantee there is one per name.
  llvm::FoldingSetNodeID ID;
  MultiKeywordSelector::Profile(ID, IIV, nKeys);

  void *InsertPos = nullptr;
  if (MultiKeywordSelector *SI =
        SelTabImpl.Table.FindNodeOrInsertPos(ID, InsertPos))
    return Selector(SI);

  // MultiKeywordSelector objects are not allocated with new because they have a
  // variable size array (for parameter types) at the end of them.
  unsigned Size = sizeof(MultiKeywordSelector) + nKeys*sizeof(IdentifierInfo *);
  MultiKeywordSelector *SI =
      (MultiKeywordSelector *)SelTabImpl.Allocator.Allocate(
          Size, alignof(MultiKeywordSelector));
  new (SI) MultiKeywordSelector(nKeys, IIV);
  SelTabImpl.Table.InsertNode(SI, InsertPos);
  return Selector(SI);
}

SelectorTable::SelectorTable() {
  Impl = new SelectorTableImpl();
}

SelectorTable::~SelectorTable() {
  delete &getSelectorTableImpl(Impl);
}

const char *clang::getOperatorSpelling(OverloadedOperatorKind Operator) {
  switch (Operator) {
  case OO_None:
  case NUM_OVERLOADED_OPERATORS:
    return nullptr;

#define OVERLOADED_OPERATOR(Name,Spelling,Token,Unary,Binary,MemberOnly) \
  case OO_##Name: return Spelling;
#include "clang/Basic/OperatorKinds.def"
  }

  llvm_unreachable("Invalid OverloadedOperatorKind!");
}

StringRef clang::getNullabilitySpelling(NullabilityKind kind,
                                        bool isContextSensitive) {
  switch (kind) {
  case NullabilityKind::NonNull:
    return isContextSensitive ? "nonnull" : "_Nonnull";

  case NullabilityKind::Nullable:
    return isContextSensitive ? "nullable" : "_Nullable";

  case NullabilityKind::NullableResult:
    assert(!isContextSensitive &&
           "_Nullable_result isn't supported as context-sensitive keyword");
    return "_Nullable_result";

  case NullabilityKind::Unspecified:
    return isContextSensitive ? "null_unspecified" : "_Null_unspecified";
  }
  llvm_unreachable("Unknown nullability kind.");
}

llvm::raw_ostream &clang::operator<<(llvm::raw_ostream &OS,
                                     NullabilityKind NK) {
  switch (NK) {
  case NullabilityKind::NonNull:
    return OS << "NonNull";
  case NullabilityKind::Nullable:
    return OS << "Nullable";
  case NullabilityKind::NullableResult:
    return OS << "NullableResult";
  case NullabilityKind::Unspecified:
    return OS << "Unspecified";
  }
  llvm_unreachable("Unknown nullability kind.");
}

diag::kind
IdentifierTable::getFutureCompatDiagKind(const IdentifierInfo &II,
                                         const LangOptions &LangOpts) {
  assert(II.isFutureCompatKeyword() && "diagnostic should not be needed");

  unsigned Flags = llvm::StringSwitch<unsigned>(II.getName())
#define KEYWORD(NAME, FLAGS) .Case(#NAME, FLAGS)
#include "clang/Basic/TokenKinds.def"
#undef KEYWORD
      ;

  if (LangOpts.CPlusPlus) {
    if ((Flags & KEYCXX11) == KEYCXX11)
      return diag::warn_cxx11_keyword;

    // char8_t is not modeled as a CXX20_KEYWORD because it's not
    // unconditionally enabled in C++20 mode. (It can be disabled
    // by -fno-char8_t.)
    if (((Flags & KEYCXX20) == KEYCXX20) ||
        ((Flags & CHAR8SUPPORT) == CHAR8SUPPORT))
      return diag::warn_cxx20_keyword;
  } else {
    if ((Flags & KEYC99) == KEYC99)
      return diag::warn_c99_keyword;
    if ((Flags & KEYC23) == KEYC23)
      return diag::warn_c23_keyword;
  }

  llvm_unreachable(
      "Keyword not known to come from a newer Standard or proposed Standard");
}

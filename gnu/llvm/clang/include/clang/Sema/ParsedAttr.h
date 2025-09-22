//======- ParsedAttr.h - Parsed attribute sets ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the ParsedAttr class, which is used to collect
// parsed attributes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_PARSEDATTR_H
#define LLVM_CLANG_SEMA_PARSEDATTR_H

#include "clang/Basic/AttrSubjectMatchRules.h"
#include "clang/Basic/AttributeCommonInfo.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/ParsedAttrInfo.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Sema/Ownership.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/VersionTuple.h"
#include <bitset>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <utility>

namespace clang {

class ASTContext;
class Decl;
class Expr;
class IdentifierInfo;
class LangOptions;
class Sema;
class Stmt;
class TargetInfo;
struct IdentifierLoc;

/// Represents information about a change in availability for
/// an entity, which is part of the encoding of the 'availability'
/// attribute.
struct AvailabilityChange {
  /// The location of the keyword indicating the kind of change.
  SourceLocation KeywordLoc;

  /// The version number at which the change occurred.
  VersionTuple Version;

  /// The source range covering the version number.
  SourceRange VersionRange;

  /// Determine whether this availability change is valid.
  bool isValid() const { return !Version.empty(); }
};

namespace detail {
enum AvailabilitySlot {
  IntroducedSlot, DeprecatedSlot, ObsoletedSlot, NumAvailabilitySlots
};

/// Describes the trailing object for Availability attribute in ParsedAttr.
struct AvailabilityData {
  AvailabilityChange Changes[NumAvailabilitySlots];
  SourceLocation StrictLoc;
  const Expr *Replacement;
  const IdentifierLoc *EnvironmentLoc;

  AvailabilityData(const AvailabilityChange &Introduced,
                   const AvailabilityChange &Deprecated,
                   const AvailabilityChange &Obsoleted, SourceLocation Strict,
                   const Expr *ReplaceExpr, const IdentifierLoc *EnvironmentLoc)
      : StrictLoc(Strict), Replacement(ReplaceExpr),
        EnvironmentLoc(EnvironmentLoc) {
    Changes[IntroducedSlot] = Introduced;
    Changes[DeprecatedSlot] = Deprecated;
    Changes[ObsoletedSlot] = Obsoleted;
  }
};

struct TypeTagForDatatypeData {
  ParsedType MatchingCType;
  LLVM_PREFERRED_TYPE(bool)
  unsigned LayoutCompatible : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned MustBeNull : 1;
};
struct PropertyData {
  IdentifierInfo *GetterId, *SetterId;

  PropertyData(IdentifierInfo *getterId, IdentifierInfo *setterId)
      : GetterId(getterId), SetterId(setterId) {}
};

} // namespace detail

/// Wraps an identifier and optional source location for the identifier.
struct IdentifierLoc {
  SourceLocation Loc;
  IdentifierInfo *Ident;

  static IdentifierLoc *create(ASTContext &Ctx, SourceLocation Loc,
                               IdentifierInfo *Ident);
};

/// A union of the various pointer types that can be passed to an
/// ParsedAttr as an argument.
using ArgsUnion = llvm::PointerUnion<Expr *, IdentifierLoc *>;
using ArgsVector = llvm::SmallVector<ArgsUnion, 12U>;

/// ParsedAttr - Represents a syntactic attribute.
///
/// For a GNU attribute, there are four forms of this construct:
///
/// 1: __attribute__(( const )). ParmName/Args/NumArgs will all be unused.
/// 2: __attribute__(( mode(byte) )). ParmName used, Args/NumArgs unused.
/// 3: __attribute__(( format(printf, 1, 2) )). ParmName/Args/NumArgs all used.
/// 4: __attribute__(( aligned(16) )). ParmName is unused, Args/Num used.
///
class ParsedAttr final
    : public AttributeCommonInfo,
      private llvm::TrailingObjects<
          ParsedAttr, ArgsUnion, detail::AvailabilityData,
          detail::TypeTagForDatatypeData, ParsedType, detail::PropertyData> {
  friend TrailingObjects;

  size_t numTrailingObjects(OverloadToken<ArgsUnion>) const { return NumArgs; }
  size_t numTrailingObjects(OverloadToken<detail::AvailabilityData>) const {
    return IsAvailability;
  }
  size_t
      numTrailingObjects(OverloadToken<detail::TypeTagForDatatypeData>) const {
    return IsTypeTagForDatatype;
  }
  size_t numTrailingObjects(OverloadToken<ParsedType>) const {
    return HasParsedType;
  }
  size_t numTrailingObjects(OverloadToken<detail::PropertyData>) const {
    return IsProperty;
  }

private:
  IdentifierInfo *MacroII = nullptr;
  SourceLocation MacroExpansionLoc;
  SourceLocation EllipsisLoc;

  /// The number of expression arguments this attribute has.
  /// The expressions themselves are stored after the object.
  unsigned NumArgs : 16;

  /// True if already diagnosed as invalid.
  LLVM_PREFERRED_TYPE(bool)
  mutable unsigned Invalid : 1;

  /// True if this attribute was used as a type attribute.
  LLVM_PREFERRED_TYPE(bool)
  mutable unsigned UsedAsTypeAttr : 1;

  /// True if this has the extra information associated with an
  /// availability attribute.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsAvailability : 1;

  /// True if this has extra information associated with a
  /// type_tag_for_datatype attribute.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsTypeTagForDatatype : 1;

  /// True if this has extra information associated with a
  /// Microsoft __delcspec(property) attribute.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsProperty : 1;

  /// True if this has a ParsedType
  LLVM_PREFERRED_TYPE(bool)
  unsigned HasParsedType : 1;

  /// True if the processing cache is valid.
  LLVM_PREFERRED_TYPE(bool)
  mutable unsigned HasProcessingCache : 1;

  /// A cached value.
  mutable unsigned ProcessingCache : 8;

  /// True if the attribute is specified using '#pragma clang attribute'.
  LLVM_PREFERRED_TYPE(bool)
  mutable unsigned IsPragmaClangAttribute : 1;

  /// The location of the 'unavailable' keyword in an
  /// availability attribute.
  SourceLocation UnavailableLoc;

  const Expr *MessageExpr;

  const ParsedAttrInfo &Info;

  ArgsUnion *getArgsBuffer() { return getTrailingObjects<ArgsUnion>(); }
  ArgsUnion const *getArgsBuffer() const {
    return getTrailingObjects<ArgsUnion>();
  }

  detail::AvailabilityData *getAvailabilityData() {
    return getTrailingObjects<detail::AvailabilityData>();
  }
  const detail::AvailabilityData *getAvailabilityData() const {
    return getTrailingObjects<detail::AvailabilityData>();
  }

private:
  friend class AttributeFactory;
  friend class AttributePool;

  /// Constructor for attributes with expression arguments.
  ParsedAttr(IdentifierInfo *attrName, SourceRange attrRange,
             IdentifierInfo *scopeName, SourceLocation scopeLoc,
             ArgsUnion *args, unsigned numArgs, Form formUsed,
             SourceLocation ellipsisLoc)
      : AttributeCommonInfo(attrName, scopeName, attrRange, scopeLoc, formUsed),
        EllipsisLoc(ellipsisLoc), NumArgs(numArgs), Invalid(false),
        UsedAsTypeAttr(false), IsAvailability(false),
        IsTypeTagForDatatype(false), IsProperty(false), HasParsedType(false),
        HasProcessingCache(false), IsPragmaClangAttribute(false),
        Info(ParsedAttrInfo::get(*this)) {
    if (numArgs)
      memcpy(getArgsBuffer(), args, numArgs * sizeof(ArgsUnion));
  }

  /// Constructor for availability attributes.
  ParsedAttr(IdentifierInfo *attrName, SourceRange attrRange,
             IdentifierInfo *scopeName, SourceLocation scopeLoc,
             IdentifierLoc *Parm, const AvailabilityChange &introduced,
             const AvailabilityChange &deprecated,
             const AvailabilityChange &obsoleted, SourceLocation unavailable,
             const Expr *messageExpr, Form formUsed, SourceLocation strict,
             const Expr *replacementExpr, const IdentifierLoc *environmentLoc)
      : AttributeCommonInfo(attrName, scopeName, attrRange, scopeLoc, formUsed),
        NumArgs(1), Invalid(false), UsedAsTypeAttr(false), IsAvailability(true),
        IsTypeTagForDatatype(false), IsProperty(false), HasParsedType(false),
        HasProcessingCache(false), IsPragmaClangAttribute(false),
        UnavailableLoc(unavailable), MessageExpr(messageExpr),
        Info(ParsedAttrInfo::get(*this)) {
    ArgsUnion PVal(Parm);
    memcpy(getArgsBuffer(), &PVal, sizeof(ArgsUnion));
    new (getAvailabilityData())
        detail::AvailabilityData(introduced, deprecated, obsoleted, strict,
                                 replacementExpr, environmentLoc);
  }

  /// Constructor for objc_bridge_related attributes.
  ParsedAttr(IdentifierInfo *attrName, SourceRange attrRange,
             IdentifierInfo *scopeName, SourceLocation scopeLoc,
             IdentifierLoc *Parm1, IdentifierLoc *Parm2, IdentifierLoc *Parm3,
             Form formUsed)
      : AttributeCommonInfo(attrName, scopeName, attrRange, scopeLoc, formUsed),
        NumArgs(3), Invalid(false), UsedAsTypeAttr(false),
        IsAvailability(false), IsTypeTagForDatatype(false), IsProperty(false),
        HasParsedType(false), HasProcessingCache(false),
        IsPragmaClangAttribute(false), Info(ParsedAttrInfo::get(*this)) {
    ArgsUnion *Args = getArgsBuffer();
    Args[0] = Parm1;
    Args[1] = Parm2;
    Args[2] = Parm3;
  }

  /// Constructor for type_tag_for_datatype attribute.
  ParsedAttr(IdentifierInfo *attrName, SourceRange attrRange,
             IdentifierInfo *scopeName, SourceLocation scopeLoc,
             IdentifierLoc *ArgKind, ParsedType matchingCType,
             bool layoutCompatible, bool mustBeNull, Form formUsed)
      : AttributeCommonInfo(attrName, scopeName, attrRange, scopeLoc, formUsed),
        NumArgs(1), Invalid(false), UsedAsTypeAttr(false),
        IsAvailability(false), IsTypeTagForDatatype(true), IsProperty(false),
        HasParsedType(false), HasProcessingCache(false),
        IsPragmaClangAttribute(false), Info(ParsedAttrInfo::get(*this)) {
    ArgsUnion PVal(ArgKind);
    memcpy(getArgsBuffer(), &PVal, sizeof(ArgsUnion));
    detail::TypeTagForDatatypeData &ExtraData = getTypeTagForDatatypeDataSlot();
    new (&ExtraData.MatchingCType) ParsedType(matchingCType);
    ExtraData.LayoutCompatible = layoutCompatible;
    ExtraData.MustBeNull = mustBeNull;
  }

  /// Constructor for attributes with a single type argument.
  ParsedAttr(IdentifierInfo *attrName, SourceRange attrRange,
             IdentifierInfo *scopeName, SourceLocation scopeLoc,
             ParsedType typeArg, Form formUsed, SourceLocation ellipsisLoc)
      : AttributeCommonInfo(attrName, scopeName, attrRange, scopeLoc, formUsed),
        EllipsisLoc(ellipsisLoc), NumArgs(0), Invalid(false),
        UsedAsTypeAttr(false), IsAvailability(false),
        IsTypeTagForDatatype(false), IsProperty(false), HasParsedType(true),
        HasProcessingCache(false), IsPragmaClangAttribute(false),
        Info(ParsedAttrInfo::get(*this)) {
    new (&getTypeBuffer()) ParsedType(typeArg);
  }

  /// Constructor for microsoft __declspec(property) attribute.
  ParsedAttr(IdentifierInfo *attrName, SourceRange attrRange,
             IdentifierInfo *scopeName, SourceLocation scopeLoc,
             IdentifierInfo *getterId, IdentifierInfo *setterId, Form formUsed)
      : AttributeCommonInfo(attrName, scopeName, attrRange, scopeLoc, formUsed),
        NumArgs(0), Invalid(false), UsedAsTypeAttr(false),
        IsAvailability(false), IsTypeTagForDatatype(false), IsProperty(true),
        HasParsedType(false), HasProcessingCache(false),
        IsPragmaClangAttribute(false), Info(ParsedAttrInfo::get(*this)) {
    new (&getPropertyDataBuffer()) detail::PropertyData(getterId, setterId);
  }

  /// Type tag information is stored immediately following the arguments, if
  /// any, at the end of the object.  They are mutually exclusive with
  /// availability slots.
  detail::TypeTagForDatatypeData &getTypeTagForDatatypeDataSlot() {
    return *getTrailingObjects<detail::TypeTagForDatatypeData>();
  }
  const detail::TypeTagForDatatypeData &getTypeTagForDatatypeDataSlot() const {
    return *getTrailingObjects<detail::TypeTagForDatatypeData>();
  }

  /// The type buffer immediately follows the object and are mutually exclusive
  /// with arguments.
  ParsedType &getTypeBuffer() { return *getTrailingObjects<ParsedType>(); }
  const ParsedType &getTypeBuffer() const {
    return *getTrailingObjects<ParsedType>();
  }

  /// The property data immediately follows the object is mutually exclusive
  /// with arguments.
  detail::PropertyData &getPropertyDataBuffer() {
    assert(IsProperty);
    return *getTrailingObjects<detail::PropertyData>();
  }
  const detail::PropertyData &getPropertyDataBuffer() const {
    assert(IsProperty);
    return *getTrailingObjects<detail::PropertyData>();
  }

  size_t allocated_size() const;

public:
  ParsedAttr(const ParsedAttr &) = delete;
  ParsedAttr(ParsedAttr &&) = delete;
  ParsedAttr &operator=(const ParsedAttr &) = delete;
  ParsedAttr &operator=(ParsedAttr &&) = delete;
  ~ParsedAttr() = delete;

  void operator delete(void *) = delete;

  bool hasParsedType() const { return HasParsedType; }

  /// Is this the Microsoft __declspec(property) attribute?
  bool isDeclspecPropertyAttribute() const  {
    return IsProperty;
  }

  bool isInvalid() const { return Invalid; }
  void setInvalid(bool b = true) const { Invalid = b; }

  bool hasProcessingCache() const { return HasProcessingCache; }

  unsigned getProcessingCache() const {
    assert(hasProcessingCache());
    return ProcessingCache;
  }

  void setProcessingCache(unsigned value) const {
    ProcessingCache = value;
    HasProcessingCache = true;
  }

  bool isUsedAsTypeAttr() const { return UsedAsTypeAttr; }
  void setUsedAsTypeAttr(bool Used = true) { UsedAsTypeAttr = Used; }

  /// True if the attribute is specified using '#pragma clang attribute'.
  bool isPragmaClangAttribute() const { return IsPragmaClangAttribute; }

  void setIsPragmaClangAttribute() { IsPragmaClangAttribute = true; }

  bool isPackExpansion() const { return EllipsisLoc.isValid(); }
  SourceLocation getEllipsisLoc() const { return EllipsisLoc; }

  /// getNumArgs - Return the number of actual arguments to this attribute.
  unsigned getNumArgs() const { return NumArgs; }

  /// getArg - Return the specified argument.
  ArgsUnion getArg(unsigned Arg) const {
    assert(Arg < NumArgs && "Arg access out of range!");
    return getArgsBuffer()[Arg];
  }

  bool isArgExpr(unsigned Arg) const {
    return Arg < NumArgs && getArg(Arg).is<Expr*>();
  }

  Expr *getArgAsExpr(unsigned Arg) const {
    return getArg(Arg).get<Expr*>();
  }

  bool isArgIdent(unsigned Arg) const {
    return Arg < NumArgs && getArg(Arg).is<IdentifierLoc*>();
  }

  IdentifierLoc *getArgAsIdent(unsigned Arg) const {
    return getArg(Arg).get<IdentifierLoc*>();
  }

  const AvailabilityChange &getAvailabilityIntroduced() const {
    assert(getParsedKind() == AT_Availability &&
           "Not an availability attribute");
    return getAvailabilityData()->Changes[detail::IntroducedSlot];
  }

  const AvailabilityChange &getAvailabilityDeprecated() const {
    assert(getParsedKind() == AT_Availability &&
           "Not an availability attribute");
    return getAvailabilityData()->Changes[detail::DeprecatedSlot];
  }

  const AvailabilityChange &getAvailabilityObsoleted() const {
    assert(getParsedKind() == AT_Availability &&
           "Not an availability attribute");
    return getAvailabilityData()->Changes[detail::ObsoletedSlot];
  }

  SourceLocation getStrictLoc() const {
    assert(getParsedKind() == AT_Availability &&
           "Not an availability attribute");
    return getAvailabilityData()->StrictLoc;
  }

  SourceLocation getUnavailableLoc() const {
    assert(getParsedKind() == AT_Availability &&
           "Not an availability attribute");
    return UnavailableLoc;
  }

  const Expr * getMessageExpr() const {
    assert(getParsedKind() == AT_Availability &&
           "Not an availability attribute");
    return MessageExpr;
  }

  const Expr *getReplacementExpr() const {
    assert(getParsedKind() == AT_Availability &&
           "Not an availability attribute");
    return getAvailabilityData()->Replacement;
  }

  const IdentifierLoc *getEnvironment() const {
    assert(getParsedKind() == AT_Availability &&
           "Not an availability attribute");
    return getAvailabilityData()->EnvironmentLoc;
  }

  const ParsedType &getMatchingCType() const {
    assert(getParsedKind() == AT_TypeTagForDatatype &&
           "Not a type_tag_for_datatype attribute");
    return getTypeTagForDatatypeDataSlot().MatchingCType;
  }

  bool getLayoutCompatible() const {
    assert(getParsedKind() == AT_TypeTagForDatatype &&
           "Not a type_tag_for_datatype attribute");
    return getTypeTagForDatatypeDataSlot().LayoutCompatible;
  }

  bool getMustBeNull() const {
    assert(getParsedKind() == AT_TypeTagForDatatype &&
           "Not a type_tag_for_datatype attribute");
    return getTypeTagForDatatypeDataSlot().MustBeNull;
  }

  const ParsedType &getTypeArg() const {
    assert(HasParsedType && "Not a type attribute");
    return getTypeBuffer();
  }

  IdentifierInfo *getPropertyDataGetter() const {
    assert(isDeclspecPropertyAttribute() &&
           "Not a __delcspec(property) attribute");
    return getPropertyDataBuffer().GetterId;
  }

  IdentifierInfo *getPropertyDataSetter() const {
    assert(isDeclspecPropertyAttribute() &&
           "Not a __delcspec(property) attribute");
    return getPropertyDataBuffer().SetterId;
  }

  /// Set the macro identifier info object that this parsed attribute was
  /// declared in if it was declared in a macro. Also set the expansion location
  /// of the macro.
  void setMacroIdentifier(IdentifierInfo *MacroName, SourceLocation Loc) {
    MacroII = MacroName;
    MacroExpansionLoc = Loc;
  }

  /// Returns true if this attribute was declared in a macro.
  bool hasMacroIdentifier() const { return MacroII != nullptr; }

  /// Return the macro identifier if this attribute was declared in a macro.
  /// nullptr is returned if it was not declared in a macro.
  IdentifierInfo *getMacroIdentifier() const { return MacroII; }

  SourceLocation getMacroExpansionLoc() const {
    assert(hasMacroIdentifier() && "Can only get the macro expansion location "
                                   "if this attribute has a macro identifier.");
    return MacroExpansionLoc;
  }

  /// Check if the attribute has exactly as many args as Num. May output an
  /// error. Returns false if a diagnostic is produced.
  bool checkExactlyNumArgs(class Sema &S, unsigned Num) const;
  /// Check if the attribute has at least as many args as Num. May output an
  /// error. Returns false if a diagnostic is produced.
  bool checkAtLeastNumArgs(class Sema &S, unsigned Num) const;
  /// Check if the attribute has at most as many args as Num. May output an
  /// error. Returns false if a diagnostic is produced.
  bool checkAtMostNumArgs(class Sema &S, unsigned Num) const;

  bool isTargetSpecificAttr() const;
  bool isTypeAttr() const;
  bool isStmtAttr() const;

  bool hasCustomParsing() const;
  bool acceptsExprPack() const;
  bool isParamExpr(size_t N) const;
  unsigned getMinArgs() const;
  unsigned getMaxArgs() const;
  unsigned getNumArgMembers() const;
  bool hasVariadicArg() const;
  void handleAttrWithDelayedArgs(Sema &S, Decl *D) const;
  bool diagnoseAppertainsTo(class Sema &S, const Decl *D) const;
  bool diagnoseAppertainsTo(class Sema &S, const Stmt *St) const;
  bool diagnoseMutualExclusion(class Sema &S, const Decl *D) const;
  // This function stub exists for parity with the declaration checking code so
  // that checkCommonAttributeFeatures() can work generically on declarations
  // or statements.
  bool diagnoseMutualExclusion(class Sema &S, const Stmt *St) const {
    return true;
  }
  bool appliesToDecl(const Decl *D, attr::SubjectMatchRule MatchRule) const;
  void getMatchRules(const LangOptions &LangOpts,
                     SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>>
                         &MatchRules) const;
  bool diagnoseLangOpts(class Sema &S) const;
  bool existsInTarget(const TargetInfo &Target) const;
  bool isKnownToGCC() const;
  bool isSupportedByPragmaAttribute() const;

  /// Returns whether a [[]] attribute, if specified ahead of a declaration,
  /// should be applied to the decl-specifier-seq instead (i.e. whether it
  /// "slides" to the decl-specifier-seq).
  ///
  /// By the standard, attributes specified before the declaration always
  /// appertain to the declaration, but historically we have allowed some of
  /// these attributes to slide to the decl-specifier-seq, so we need to keep
  /// supporting this behavior.
  ///
  /// This may only be called if isStandardAttributeSyntax() returns true.
  bool slidesFromDeclToDeclSpecLegacyBehavior() const;

  /// If the parsed attribute has a semantic equivalent, and it would
  /// have a semantic Spelling enumeration (due to having semantically-distinct
  /// spelling variations), return the value of that semantic spelling. If the
  /// parsed attribute does not have a semantic equivalent, or would not have
  /// a Spelling enumeration, the value UINT_MAX is returned.
  unsigned getSemanticSpelling() const;

  /// If this is an OpenCL address space attribute, returns its representation
  /// in LangAS, otherwise returns default address space.
  LangAS asOpenCLLangAS() const {
    switch (getParsedKind()) {
    case ParsedAttr::AT_OpenCLConstantAddressSpace:
      return LangAS::opencl_constant;
    case ParsedAttr::AT_OpenCLGlobalAddressSpace:
      return LangAS::opencl_global;
    case ParsedAttr::AT_OpenCLGlobalDeviceAddressSpace:
      return LangAS::opencl_global_device;
    case ParsedAttr::AT_OpenCLGlobalHostAddressSpace:
      return LangAS::opencl_global_host;
    case ParsedAttr::AT_OpenCLLocalAddressSpace:
      return LangAS::opencl_local;
    case ParsedAttr::AT_OpenCLPrivateAddressSpace:
      return LangAS::opencl_private;
    case ParsedAttr::AT_OpenCLGenericAddressSpace:
      return LangAS::opencl_generic;
    default:
      return LangAS::Default;
    }
  }

  /// If this is an OpenCL address space attribute, returns its SYCL
  /// representation in LangAS, otherwise returns default address space.
  LangAS asSYCLLangAS() const {
    switch (getKind()) {
    case ParsedAttr::AT_OpenCLGlobalAddressSpace:
      return LangAS::sycl_global;
    case ParsedAttr::AT_OpenCLGlobalDeviceAddressSpace:
      return LangAS::sycl_global_device;
    case ParsedAttr::AT_OpenCLGlobalHostAddressSpace:
      return LangAS::sycl_global_host;
    case ParsedAttr::AT_OpenCLLocalAddressSpace:
      return LangAS::sycl_local;
    case ParsedAttr::AT_OpenCLPrivateAddressSpace:
      return LangAS::sycl_private;
    case ParsedAttr::AT_OpenCLGenericAddressSpace:
    default:
      return LangAS::Default;
    }
  }

  /// If this is an HLSL address space attribute, returns its representation
  /// in LangAS, otherwise returns default address space.
  LangAS asHLSLLangAS() const {
    switch (getParsedKind()) {
    case ParsedAttr::AT_HLSLGroupSharedAddressSpace:
      return LangAS::hlsl_groupshared;
    default:
      return LangAS::Default;
    }
  }

  AttributeCommonInfo::Kind getKind() const {
    return AttributeCommonInfo::Kind(Info.AttrKind);
  }
  const ParsedAttrInfo &getInfo() const { return Info; }
};

class AttributePool;
/// A factory, from which one makes pools, from which one creates
/// individual attributes which are deallocated with the pool.
///
/// Note that it's tolerably cheap to create and destroy one of
/// these as long as you don't actually allocate anything in it.
class AttributeFactory {
public:
  enum {
    AvailabilityAllocSize =
        ParsedAttr::totalSizeToAlloc<ArgsUnion, detail::AvailabilityData,
                                     detail::TypeTagForDatatypeData, ParsedType,
                                     detail::PropertyData>(1, 1, 0, 0, 0),
    TypeTagForDatatypeAllocSize =
        ParsedAttr::totalSizeToAlloc<ArgsUnion, detail::AvailabilityData,
                                     detail::TypeTagForDatatypeData, ParsedType,
                                     detail::PropertyData>(1, 0, 1, 0, 0),
    PropertyAllocSize =
        ParsedAttr::totalSizeToAlloc<ArgsUnion, detail::AvailabilityData,
                                     detail::TypeTagForDatatypeData, ParsedType,
                                     detail::PropertyData>(0, 0, 0, 0, 1),
  };

private:
  enum {
    /// The number of free lists we want to be sure to support
    /// inline.  This is just enough that availability attributes
    /// don't surpass it.  It's actually very unlikely we'll see an
    /// attribute that needs more than that; on x86-64 you'd need 10
    /// expression arguments, and on i386 you'd need 19.
    InlineFreeListsCapacity =
        1 + (AvailabilityAllocSize - sizeof(ParsedAttr)) / sizeof(void *)
  };

  llvm::BumpPtrAllocator Alloc;

  /// Free lists.  The index is determined by the following formula:
  ///   (size - sizeof(ParsedAttr)) / sizeof(void*)
  SmallVector<SmallVector<ParsedAttr *, 8>, InlineFreeListsCapacity> FreeLists;

  // The following are the private interface used by AttributePool.
  friend class AttributePool;

  /// Allocate an attribute of the given size.
  void *allocate(size_t size);

  void deallocate(ParsedAttr *AL);

  /// Reclaim all the attributes in the given pool chain, which is
  /// non-empty.  Note that the current implementation is safe
  /// against reclaiming things which were not actually allocated
  /// with the allocator, although of course it's important to make
  /// sure that their allocator lives at least as long as this one.
  void reclaimPool(AttributePool &head);

public:
  AttributeFactory();
  ~AttributeFactory();
};

class ParsedAttributesView;
class AttributePool {
  friend class AttributeFactory;
  friend class ParsedAttributes;
  AttributeFactory &Factory;
  llvm::SmallVector<ParsedAttr *> Attrs;

  void *allocate(size_t size) {
    return Factory.allocate(size);
  }

  ParsedAttr *add(ParsedAttr *attr) {
    Attrs.push_back(attr);
    return attr;
  }

  void remove(ParsedAttr *attr) {
    assert(llvm::is_contained(Attrs, attr) &&
           "Can't take attribute from a pool that doesn't own it!");
    Attrs.erase(llvm::find(Attrs, attr));
  }

  void takePool(AttributePool &pool);

public:
  /// Create a new pool for a factory.
  AttributePool(AttributeFactory &factory) : Factory(factory) {}

  AttributePool(const AttributePool &) = delete;
  // The copy assignment operator is defined as deleted pending further
  // motivation.
  AttributePool &operator=(const AttributePool &) = delete;

  ~AttributePool() { Factory.reclaimPool(*this); }

  /// Move the given pool's allocations to this pool.
  AttributePool(AttributePool &&pool) = default;

  // The move assignment operator is defined as deleted pending further
  // motivation.
  AttributePool &operator=(AttributePool &&pool) = delete;

  AttributeFactory &getFactory() const { return Factory; }

  void clear() {
    Factory.reclaimPool(*this);
    Attrs.clear();
  }

  /// Take the given pool's allocations and add them to this pool.
  void takeAllFrom(AttributePool &pool) {
    takePool(pool);
    pool.Attrs.clear();
  }

  /// Removes the attributes from \c List, which are owned by \c Pool, and adds
  /// them at the end of this \c AttributePool.
  void takeFrom(ParsedAttributesView &List, AttributePool &Pool);

  ParsedAttr *create(IdentifierInfo *attrName, SourceRange attrRange,
                     IdentifierInfo *scopeName, SourceLocation scopeLoc,
                     ArgsUnion *args, unsigned numArgs, ParsedAttr::Form form,
                     SourceLocation ellipsisLoc = SourceLocation()) {
    void *memory = allocate(
        ParsedAttr::totalSizeToAlloc<ArgsUnion, detail::AvailabilityData,
                                     detail::TypeTagForDatatypeData, ParsedType,
                                     detail::PropertyData>(numArgs, 0, 0, 0,
                                                           0));
    return add(new (memory) ParsedAttr(attrName, attrRange, scopeName, scopeLoc,
                                       args, numArgs, form, ellipsisLoc));
  }

  ParsedAttr *create(IdentifierInfo *attrName, SourceRange attrRange,
                     IdentifierInfo *scopeName, SourceLocation scopeLoc,
                     IdentifierLoc *Param, const AvailabilityChange &introduced,
                     const AvailabilityChange &deprecated,
                     const AvailabilityChange &obsoleted,
                     SourceLocation unavailable, const Expr *MessageExpr,
                     ParsedAttr::Form form, SourceLocation strict,
                     const Expr *ReplacementExpr,
                     IdentifierLoc *EnvironmentLoc) {
    void *memory = allocate(AttributeFactory::AvailabilityAllocSize);
    return add(new (memory) ParsedAttr(attrName, attrRange, scopeName, scopeLoc,
                                       Param, introduced, deprecated, obsoleted,
                                       unavailable, MessageExpr, form, strict,
                                       ReplacementExpr, EnvironmentLoc));
  }

  ParsedAttr *create(IdentifierInfo *attrName, SourceRange attrRange,
                     IdentifierInfo *scopeName, SourceLocation scopeLoc,
                     IdentifierLoc *Param1, IdentifierLoc *Param2,
                     IdentifierLoc *Param3, ParsedAttr::Form form) {
    void *memory = allocate(
        ParsedAttr::totalSizeToAlloc<ArgsUnion, detail::AvailabilityData,
                                     detail::TypeTagForDatatypeData, ParsedType,
                                     detail::PropertyData>(3, 0, 0, 0, 0));
    return add(new (memory) ParsedAttr(attrName, attrRange, scopeName, scopeLoc,
                                       Param1, Param2, Param3, form));
  }

  ParsedAttr *
  createTypeTagForDatatype(IdentifierInfo *attrName, SourceRange attrRange,
                           IdentifierInfo *scopeName, SourceLocation scopeLoc,
                           IdentifierLoc *argumentKind,
                           ParsedType matchingCType, bool layoutCompatible,
                           bool mustBeNull, ParsedAttr::Form form) {
    void *memory = allocate(AttributeFactory::TypeTagForDatatypeAllocSize);
    return add(new (memory) ParsedAttr(attrName, attrRange, scopeName, scopeLoc,
                                       argumentKind, matchingCType,
                                       layoutCompatible, mustBeNull, form));
  }

  ParsedAttr *createTypeAttribute(IdentifierInfo *attrName,
                                  SourceRange attrRange,
                                  IdentifierInfo *scopeName,
                                  SourceLocation scopeLoc, ParsedType typeArg,
                                  ParsedAttr::Form formUsed,
                                  SourceLocation ellipsisLoc) {
    void *memory = allocate(
        ParsedAttr::totalSizeToAlloc<ArgsUnion, detail::AvailabilityData,
                                     detail::TypeTagForDatatypeData, ParsedType,
                                     detail::PropertyData>(0, 0, 0, 1, 0));
    return add(new (memory) ParsedAttr(attrName, attrRange, scopeName, scopeLoc,
                                       typeArg, formUsed, ellipsisLoc));
  }

  ParsedAttr *
  createPropertyAttribute(IdentifierInfo *attrName, SourceRange attrRange,
                          IdentifierInfo *scopeName, SourceLocation scopeLoc,
                          IdentifierInfo *getterId, IdentifierInfo *setterId,
                          ParsedAttr::Form formUsed) {
    void *memory = allocate(AttributeFactory::PropertyAllocSize);
    return add(new (memory) ParsedAttr(attrName, attrRange, scopeName, scopeLoc,
                                       getterId, setterId, formUsed));
  }
};

class ParsedAttributesView {
  friend class AttributePool;
  using VecTy = llvm::SmallVector<ParsedAttr *>;
  using SizeType = decltype(std::declval<VecTy>().size());

public:
  SourceRange Range;

  static const ParsedAttributesView &none() {
    static const ParsedAttributesView Attrs;
    return Attrs;
  }

  bool empty() const { return AttrList.empty(); }
  SizeType size() const { return AttrList.size(); }
  ParsedAttr &operator[](SizeType pos) { return *AttrList[pos]; }
  const ParsedAttr &operator[](SizeType pos) const { return *AttrList[pos]; }

  void addAtEnd(ParsedAttr *newAttr) {
    assert(newAttr);
    AttrList.push_back(newAttr);
  }

  void remove(ParsedAttr *ToBeRemoved) {
    assert(is_contained(AttrList, ToBeRemoved) &&
           "Cannot remove attribute that isn't in the list");
    AttrList.erase(llvm::find(AttrList, ToBeRemoved));
  }

  void clearListOnly() { AttrList.clear(); }

  struct iterator : llvm::iterator_adaptor_base<iterator, VecTy::iterator,
                                                std::random_access_iterator_tag,
                                                ParsedAttr> {
    iterator() : iterator_adaptor_base(nullptr) {}
    iterator(VecTy::iterator I) : iterator_adaptor_base(I) {}
    reference operator*() const { return **I; }
    friend class ParsedAttributesView;
  };
  struct const_iterator
      : llvm::iterator_adaptor_base<const_iterator, VecTy::const_iterator,
                                    std::random_access_iterator_tag,
                                    ParsedAttr> {
    const_iterator() : iterator_adaptor_base(nullptr) {}
    const_iterator(VecTy::const_iterator I) : iterator_adaptor_base(I) {}

    reference operator*() const { return **I; }
    friend class ParsedAttributesView;
  };

  void addAll(iterator B, iterator E) {
    AttrList.insert(AttrList.begin(), B.I, E.I);
  }

  void addAll(const_iterator B, const_iterator E) {
    AttrList.insert(AttrList.begin(), B.I, E.I);
  }

  void addAllAtEnd(iterator B, iterator E) {
    AttrList.insert(AttrList.end(), B.I, E.I);
  }

  void addAllAtEnd(const_iterator B, const_iterator E) {
    AttrList.insert(AttrList.end(), B.I, E.I);
  }

  iterator begin() { return iterator(AttrList.begin()); }
  const_iterator begin() const { return const_iterator(AttrList.begin()); }
  iterator end() { return iterator(AttrList.end()); }
  const_iterator end() const { return const_iterator(AttrList.end()); }

  ParsedAttr &front() {
    assert(!empty());
    return *AttrList.front();
  }
  const ParsedAttr &front() const {
    assert(!empty());
    return *AttrList.front();
  }
  ParsedAttr &back() {
    assert(!empty());
    return *AttrList.back();
  }
  const ParsedAttr &back() const {
    assert(!empty());
    return *AttrList.back();
  }

  bool hasAttribute(ParsedAttr::Kind K) const {
    return llvm::any_of(AttrList, [K](const ParsedAttr *AL) {
      return AL->getParsedKind() == K;
    });
  }

  const ParsedAttr *getMSPropertyAttr() const {
    auto It = llvm::find_if(AttrList, [](const ParsedAttr *AL) {
      return AL->isDeclspecPropertyAttribute();
    });
    if (It != AttrList.end())
      return *It;
    return nullptr;
  }
  bool hasMSPropertyAttr() const { return getMSPropertyAttr(); }

private:
  VecTy AttrList;
};

struct ParsedAttributeArgumentsProperties {
  ParsedAttributeArgumentsProperties(uint32_t StringLiteralBits)
      : StringLiterals(StringLiteralBits) {}
  bool isStringLiteralArg(unsigned I) const {
    // If the last bit is set, assume we have a variadic parameter
    if (I >= StringLiterals.size())
      return StringLiterals.test(StringLiterals.size() - 1);
    return StringLiterals.test(I);
  }

private:
  std::bitset<32> StringLiterals;
};

/// ParsedAttributes - A collection of parsed attributes.  Currently
/// we don't differentiate between the various attribute syntaxes,
/// which is basically silly.
///
/// Right now this is a very lightweight container, but the expectation
/// is that this will become significantly more serious.
class ParsedAttributes : public ParsedAttributesView {
public:
  ParsedAttributes(AttributeFactory &factory) : pool(factory) {}
  ParsedAttributes(const ParsedAttributes &) = delete;
  ParsedAttributes &operator=(const ParsedAttributes &) = delete;
  ParsedAttributes(ParsedAttributes &&G) = default;

  AttributePool &getPool() const { return pool; }

  void takeAllFrom(ParsedAttributes &Other) {
    assert(&Other != this &&
           "ParsedAttributes can't take attributes from itself");
    addAll(Other.begin(), Other.end());
    Other.clearListOnly();
    pool.takeAllFrom(Other.pool);
  }

  void takeOneFrom(ParsedAttributes &Other, ParsedAttr *PA) {
    assert(&Other != this &&
           "ParsedAttributes can't take attribute from itself");
    Other.getPool().remove(PA);
    Other.remove(PA);
    getPool().add(PA);
    addAtEnd(PA);
  }

  void clear() {
    clearListOnly();
    pool.clear();
    Range = SourceRange();
  }

  /// Add attribute with expression arguments.
  ParsedAttr *addNew(IdentifierInfo *attrName, SourceRange attrRange,
                     IdentifierInfo *scopeName, SourceLocation scopeLoc,
                     ArgsUnion *args, unsigned numArgs, ParsedAttr::Form form,
                     SourceLocation ellipsisLoc = SourceLocation()) {
    ParsedAttr *attr = pool.create(attrName, attrRange, scopeName, scopeLoc,
                                   args, numArgs, form, ellipsisLoc);
    addAtEnd(attr);
    return attr;
  }

  /// Add availability attribute.
  ParsedAttr *addNew(IdentifierInfo *attrName, SourceRange attrRange,
                     IdentifierInfo *scopeName, SourceLocation scopeLoc,
                     IdentifierLoc *Param, const AvailabilityChange &introduced,
                     const AvailabilityChange &deprecated,
                     const AvailabilityChange &obsoleted,
                     SourceLocation unavailable, const Expr *MessageExpr,
                     ParsedAttr::Form form, SourceLocation strict,
                     const Expr *ReplacementExpr,
                     IdentifierLoc *EnvironmentLoc) {
    ParsedAttr *attr =
        pool.create(attrName, attrRange, scopeName, scopeLoc, Param, introduced,
                    deprecated, obsoleted, unavailable, MessageExpr, form,
                    strict, ReplacementExpr, EnvironmentLoc);
    addAtEnd(attr);
    return attr;
  }

  /// Add objc_bridge_related attribute.
  ParsedAttr *addNew(IdentifierInfo *attrName, SourceRange attrRange,
                     IdentifierInfo *scopeName, SourceLocation scopeLoc,
                     IdentifierLoc *Param1, IdentifierLoc *Param2,
                     IdentifierLoc *Param3, ParsedAttr::Form form) {
    ParsedAttr *attr = pool.create(attrName, attrRange, scopeName, scopeLoc,
                                   Param1, Param2, Param3, form);
    addAtEnd(attr);
    return attr;
  }

  /// Add type_tag_for_datatype attribute.
  ParsedAttr *
  addNewTypeTagForDatatype(IdentifierInfo *attrName, SourceRange attrRange,
                           IdentifierInfo *scopeName, SourceLocation scopeLoc,
                           IdentifierLoc *argumentKind,
                           ParsedType matchingCType, bool layoutCompatible,
                           bool mustBeNull, ParsedAttr::Form form) {
    ParsedAttr *attr = pool.createTypeTagForDatatype(
        attrName, attrRange, scopeName, scopeLoc, argumentKind, matchingCType,
        layoutCompatible, mustBeNull, form);
    addAtEnd(attr);
    return attr;
  }

  /// Add an attribute with a single type argument.
  ParsedAttr *addNewTypeAttr(IdentifierInfo *attrName, SourceRange attrRange,
                             IdentifierInfo *scopeName, SourceLocation scopeLoc,
                             ParsedType typeArg, ParsedAttr::Form formUsed,
                             SourceLocation ellipsisLoc = SourceLocation()) {
    ParsedAttr *attr =
        pool.createTypeAttribute(attrName, attrRange, scopeName, scopeLoc,
                                 typeArg, formUsed, ellipsisLoc);
    addAtEnd(attr);
    return attr;
  }

  /// Add microsoft __delspec(property) attribute.
  ParsedAttr *
  addNewPropertyAttr(IdentifierInfo *attrName, SourceRange attrRange,
                     IdentifierInfo *scopeName, SourceLocation scopeLoc,
                     IdentifierInfo *getterId, IdentifierInfo *setterId,
                     ParsedAttr::Form formUsed) {
    ParsedAttr *attr = pool.createPropertyAttribute(
        attrName, attrRange, scopeName, scopeLoc, getterId, setterId, formUsed);
    addAtEnd(attr);
    return attr;
  }

private:
  mutable AttributePool pool;
};

/// Consumes the attributes from `First` and `Second` and concatenates them into
/// `Result`. Sets `Result.Range` to the combined range of `First` and `Second`.
void takeAndConcatenateAttrs(ParsedAttributes &First, ParsedAttributes &Second,
                             ParsedAttributes &Result);

/// These constants match the enumerated choices of
/// err_attribute_argument_n_type and err_attribute_argument_type.
enum AttributeArgumentNType {
  AANT_ArgumentIntOrBool,
  AANT_ArgumentIntegerConstant,
  AANT_ArgumentString,
  AANT_ArgumentIdentifier,
  AANT_ArgumentConstantExpr,
  AANT_ArgumentBuiltinFunction,
};

/// These constants match the enumerated choices of
/// warn_attribute_wrong_decl_type and err_attribute_wrong_decl_type.
enum AttributeDeclKind {
  ExpectedFunction,
  ExpectedUnion,
  ExpectedVariableOrFunction,
  ExpectedFunctionOrMethod,
  ExpectedFunctionMethodOrBlock,
  ExpectedFunctionMethodOrParameter,
  ExpectedVariable,
  ExpectedVariableOrField,
  ExpectedVariableFieldOrTag,
  ExpectedTypeOrNamespace,
  ExpectedFunctionVariableOrClass,
  ExpectedKernelFunction,
  ExpectedFunctionWithProtoType,
};

inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                             const ParsedAttr &At) {
  DB.AddTaggedVal(reinterpret_cast<uint64_t>(At.getAttrName()),
                  DiagnosticsEngine::ak_identifierinfo);
  return DB;
}

inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                             const ParsedAttr *At) {
  DB.AddTaggedVal(reinterpret_cast<uint64_t>(At->getAttrName()),
                  DiagnosticsEngine::ak_identifierinfo);
  return DB;
}

/// AttributeCommonInfo has a non-explicit constructor which takes an
/// SourceRange as its only argument, this constructor has many uses so making
/// it explicit is hard. This constructor causes ambiguity with
/// DiagnosticBuilder &operator<<(const DiagnosticBuilder &DB, SourceRange R).
/// We use SFINAE to disable any conversion and remove any ambiguity.
template <
    typename ACI,
    std::enable_if_t<std::is_same<ACI, AttributeCommonInfo>::value, int> = 0>
inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                             const ACI &CI) {
  DB.AddTaggedVal(reinterpret_cast<uint64_t>(CI.getAttrName()),
                  DiagnosticsEngine::ak_identifierinfo);
  return DB;
}

template <
    typename ACI,
    std::enable_if_t<std::is_same<ACI, AttributeCommonInfo>::value, int> = 0>
inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                             const ACI *CI) {
  DB.AddTaggedVal(reinterpret_cast<uint64_t>(CI->getAttrName()),
                  DiagnosticsEngine::ak_identifierinfo);
  return DB;
}

} // namespace clang

#endif // LLVM_CLANG_SEMA_PARSEDATTR_H

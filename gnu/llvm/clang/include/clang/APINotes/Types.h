//===-- Types.h - API Notes Data Types --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_APINOTES_TYPES_H
#define LLVM_CLANG_APINOTES_TYPES_H

#include "clang/Basic/Specifiers.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include <climits>
#include <optional>
#include <vector>

namespace llvm {
class raw_ostream;
} // namespace llvm

namespace clang {
namespace api_notes {
enum class RetainCountConventionKind {
  None,
  CFReturnsRetained,
  CFReturnsNotRetained,
  NSReturnsRetained,
  NSReturnsNotRetained,
};

/// The payload for an enum_extensibility attribute. This is a tri-state rather
/// than just a boolean because the presence of the attribute indicates
/// auditing.
enum class EnumExtensibilityKind {
  None,
  Open,
  Closed,
};

/// The kind of a swift_wrapper/swift_newtype.
enum class SwiftNewTypeKind {
  None,
  Struct,
  Enum,
};

/// Describes API notes data for any entity.
///
/// This is used as the base of all API notes.
class CommonEntityInfo {
public:
  /// Message to use when this entity is unavailable.
  std::string UnavailableMsg;

  /// Whether this entity is marked unavailable.
  LLVM_PREFERRED_TYPE(bool)
  unsigned Unavailable : 1;

  /// Whether this entity is marked unavailable in Swift.
  LLVM_PREFERRED_TYPE(bool)
  unsigned UnavailableInSwift : 1;

private:
  /// Whether SwiftPrivate was specified.
  LLVM_PREFERRED_TYPE(bool)
  unsigned SwiftPrivateSpecified : 1;

  /// Whether this entity is considered "private" to a Swift overlay.
  LLVM_PREFERRED_TYPE(bool)
  unsigned SwiftPrivate : 1;

public:
  /// Swift name of this entity.
  std::string SwiftName;

  CommonEntityInfo()
      : Unavailable(0), UnavailableInSwift(0), SwiftPrivateSpecified(0),
        SwiftPrivate(0) {}

  std::optional<bool> isSwiftPrivate() const {
    return SwiftPrivateSpecified ? std::optional<bool>(SwiftPrivate)
                                 : std::nullopt;
  }

  void setSwiftPrivate(std::optional<bool> Private) {
    SwiftPrivateSpecified = Private.has_value();
    SwiftPrivate = Private.value_or(0);
  }

  friend bool operator==(const CommonEntityInfo &, const CommonEntityInfo &);

  CommonEntityInfo &operator|=(const CommonEntityInfo &RHS) {
    // Merge unavailability.
    if (RHS.Unavailable) {
      Unavailable = true;
      if (UnavailableMsg.empty())
        UnavailableMsg = RHS.UnavailableMsg;
    }

    if (RHS.UnavailableInSwift) {
      UnavailableInSwift = true;
      if (UnavailableMsg.empty())
        UnavailableMsg = RHS.UnavailableMsg;
    }

    if (!SwiftPrivateSpecified)
      setSwiftPrivate(RHS.isSwiftPrivate());

    if (SwiftName.empty())
      SwiftName = RHS.SwiftName;

    return *this;
  }

  LLVM_DUMP_METHOD void dump(llvm::raw_ostream &OS) const;
};

inline bool operator==(const CommonEntityInfo &LHS,
                       const CommonEntityInfo &RHS) {
  return LHS.UnavailableMsg == RHS.UnavailableMsg &&
         LHS.Unavailable == RHS.Unavailable &&
         LHS.UnavailableInSwift == RHS.UnavailableInSwift &&
         LHS.SwiftPrivateSpecified == RHS.SwiftPrivateSpecified &&
         LHS.SwiftPrivate == RHS.SwiftPrivate && LHS.SwiftName == RHS.SwiftName;
}

inline bool operator!=(const CommonEntityInfo &LHS,
                       const CommonEntityInfo &RHS) {
  return !(LHS == RHS);
}

/// Describes API notes for types.
class CommonTypeInfo : public CommonEntityInfo {
  /// The Swift type to which a given type is bridged.
  ///
  /// Reflects the swift_bridge attribute.
  std::optional<std::string> SwiftBridge;

  /// The NS error domain for this type.
  std::optional<std::string> NSErrorDomain;

public:
  CommonTypeInfo() {}

  const std::optional<std::string> &getSwiftBridge() const {
    return SwiftBridge;
  }

  void setSwiftBridge(std::optional<std::string> SwiftType) {
    SwiftBridge = SwiftType;
  }

  const std::optional<std::string> &getNSErrorDomain() const {
    return NSErrorDomain;
  }

  void setNSErrorDomain(const std::optional<std::string> &Domain) {
    NSErrorDomain = Domain;
  }

  void setNSErrorDomain(const std::optional<llvm::StringRef> &Domain) {
    NSErrorDomain = Domain ? std::optional<std::string>(std::string(*Domain))
                           : std::nullopt;
  }

  friend bool operator==(const CommonTypeInfo &, const CommonTypeInfo &);

  CommonTypeInfo &operator|=(const CommonTypeInfo &RHS) {
    // Merge inherited info.
    static_cast<CommonEntityInfo &>(*this) |= RHS;

    if (!SwiftBridge)
      setSwiftBridge(RHS.getSwiftBridge());
    if (!NSErrorDomain)
      setNSErrorDomain(RHS.getNSErrorDomain());

    return *this;
  }

  LLVM_DUMP_METHOD void dump(llvm::raw_ostream &OS) const;
};

inline bool operator==(const CommonTypeInfo &LHS, const CommonTypeInfo &RHS) {
  return static_cast<const CommonEntityInfo &>(LHS) == RHS &&
         LHS.SwiftBridge == RHS.SwiftBridge &&
         LHS.NSErrorDomain == RHS.NSErrorDomain;
}

inline bool operator!=(const CommonTypeInfo &LHS, const CommonTypeInfo &RHS) {
  return !(LHS == RHS);
}

/// Describes API notes data for an Objective-C class or protocol or a C++
/// namespace.
class ContextInfo : public CommonTypeInfo {
  /// Whether this class has a default nullability.
  LLVM_PREFERRED_TYPE(bool)
  unsigned HasDefaultNullability : 1;

  /// The default nullability.
  LLVM_PREFERRED_TYPE(NullabilityKind)
  unsigned DefaultNullability : 2;

  /// Whether this class has designated initializers recorded.
  LLVM_PREFERRED_TYPE(bool)
  unsigned HasDesignatedInits : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned SwiftImportAsNonGenericSpecified : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned SwiftImportAsNonGeneric : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned SwiftObjCMembersSpecified : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned SwiftObjCMembers : 1;

public:
  ContextInfo()
      : HasDefaultNullability(0), DefaultNullability(0), HasDesignatedInits(0),
        SwiftImportAsNonGenericSpecified(false), SwiftImportAsNonGeneric(false),
        SwiftObjCMembersSpecified(false), SwiftObjCMembers(false) {}

  /// Determine the default nullability for properties and methods of this
  /// class.
  ///
  /// Returns the default nullability, if implied, or std::nullopt if there is
  /// none.
  std::optional<NullabilityKind> getDefaultNullability() const {
    return HasDefaultNullability
               ? std::optional<NullabilityKind>(
                     static_cast<NullabilityKind>(DefaultNullability))
               : std::nullopt;
  }

  /// Set the default nullability for properties and methods of this class.
  void setDefaultNullability(NullabilityKind Kind) {
    HasDefaultNullability = true;
    DefaultNullability = static_cast<unsigned>(Kind);
  }

  bool hasDesignatedInits() const { return HasDesignatedInits; }
  void setHasDesignatedInits(bool Value) { HasDesignatedInits = Value; }

  std::optional<bool> getSwiftImportAsNonGeneric() const {
    return SwiftImportAsNonGenericSpecified
               ? std::optional<bool>(SwiftImportAsNonGeneric)
               : std::nullopt;
  }
  void setSwiftImportAsNonGeneric(std::optional<bool> Value) {
    SwiftImportAsNonGenericSpecified = Value.has_value();
    SwiftImportAsNonGeneric = Value.value_or(false);
  }

  std::optional<bool> getSwiftObjCMembers() const {
    return SwiftObjCMembersSpecified ? std::optional<bool>(SwiftObjCMembers)
                                     : std::nullopt;
  }
  void setSwiftObjCMembers(std::optional<bool> Value) {
    SwiftObjCMembersSpecified = Value.has_value();
    SwiftObjCMembers = Value.value_or(false);
  }

  friend bool operator==(const ContextInfo &, const ContextInfo &);

  ContextInfo &operator|=(const ContextInfo &RHS) {
    // Merge inherited info.
    static_cast<CommonTypeInfo &>(*this) |= RHS;

    // Merge nullability.
    if (!getDefaultNullability())
      if (auto Nullability = RHS.getDefaultNullability())
        setDefaultNullability(*Nullability);

    if (!SwiftImportAsNonGenericSpecified)
      setSwiftImportAsNonGeneric(RHS.getSwiftImportAsNonGeneric());

    if (!SwiftObjCMembersSpecified)
      setSwiftObjCMembers(RHS.getSwiftObjCMembers());

    HasDesignatedInits |= RHS.HasDesignatedInits;

    return *this;
  }

  LLVM_DUMP_METHOD void dump(llvm::raw_ostream &OS);
};

inline bool operator==(const ContextInfo &LHS, const ContextInfo &RHS) {
  return static_cast<const CommonTypeInfo &>(LHS) == RHS &&
         LHS.getDefaultNullability() == RHS.getDefaultNullability() &&
         LHS.HasDesignatedInits == RHS.HasDesignatedInits &&
         LHS.getSwiftImportAsNonGeneric() == RHS.getSwiftImportAsNonGeneric() &&
         LHS.getSwiftObjCMembers() == RHS.getSwiftObjCMembers();
}

inline bool operator!=(const ContextInfo &LHS, const ContextInfo &RHS) {
  return !(LHS == RHS);
}

/// API notes for a variable/property.
class VariableInfo : public CommonEntityInfo {
  /// Whether this property has been audited for nullability.
  LLVM_PREFERRED_TYPE(bool)
  unsigned NullabilityAudited : 1;

  /// The kind of nullability for this property. Only valid if the nullability
  /// has been audited.
  LLVM_PREFERRED_TYPE(NullabilityKind)
  unsigned Nullable : 2;

  /// The C type of the variable, as a string.
  std::string Type;

public:
  VariableInfo() : NullabilityAudited(false), Nullable(0) {}

  std::optional<NullabilityKind> getNullability() const {
    return NullabilityAudited ? std::optional<NullabilityKind>(
                                    static_cast<NullabilityKind>(Nullable))
                              : std::nullopt;
  }

  void setNullabilityAudited(NullabilityKind kind) {
    NullabilityAudited = true;
    Nullable = static_cast<unsigned>(kind);
  }

  const std::string &getType() const { return Type; }
  void setType(const std::string &type) { Type = type; }

  friend bool operator==(const VariableInfo &, const VariableInfo &);

  VariableInfo &operator|=(const VariableInfo &RHS) {
    static_cast<CommonEntityInfo &>(*this) |= RHS;

    if (!NullabilityAudited && RHS.NullabilityAudited)
      setNullabilityAudited(*RHS.getNullability());
    if (Type.empty())
      Type = RHS.Type;

    return *this;
  }

  LLVM_DUMP_METHOD void dump(llvm::raw_ostream &OS) const;
};

inline bool operator==(const VariableInfo &LHS, const VariableInfo &RHS) {
  return static_cast<const CommonEntityInfo &>(LHS) == RHS &&
         LHS.NullabilityAudited == RHS.NullabilityAudited &&
         LHS.Nullable == RHS.Nullable && LHS.Type == RHS.Type;
}

inline bool operator!=(const VariableInfo &LHS, const VariableInfo &RHS) {
  return !(LHS == RHS);
}

/// Describes API notes data for an Objective-C property.
class ObjCPropertyInfo : public VariableInfo {
  LLVM_PREFERRED_TYPE(bool)
  unsigned SwiftImportAsAccessorsSpecified : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned SwiftImportAsAccessors : 1;

public:
  ObjCPropertyInfo()
      : SwiftImportAsAccessorsSpecified(false), SwiftImportAsAccessors(false) {}

  std::optional<bool> getSwiftImportAsAccessors() const {
    return SwiftImportAsAccessorsSpecified
               ? std::optional<bool>(SwiftImportAsAccessors)
               : std::nullopt;
  }
  void setSwiftImportAsAccessors(std::optional<bool> Value) {
    SwiftImportAsAccessorsSpecified = Value.has_value();
    SwiftImportAsAccessors = Value.value_or(false);
  }

  friend bool operator==(const ObjCPropertyInfo &, const ObjCPropertyInfo &);

  /// Merge class-wide information into the given property.
  ObjCPropertyInfo &operator|=(const ContextInfo &RHS) {
    static_cast<CommonEntityInfo &>(*this) |= RHS;

    // Merge nullability.
    if (!getNullability())
      if (auto Nullable = RHS.getDefaultNullability())
        setNullabilityAudited(*Nullable);

    return *this;
  }

  ObjCPropertyInfo &operator|=(const ObjCPropertyInfo &RHS) {
    static_cast<VariableInfo &>(*this) |= RHS;

    if (!SwiftImportAsAccessorsSpecified)
      setSwiftImportAsAccessors(RHS.getSwiftImportAsAccessors());

    return *this;
  }

  LLVM_DUMP_METHOD void dump(llvm::raw_ostream &OS) const;
};

inline bool operator==(const ObjCPropertyInfo &LHS,
                       const ObjCPropertyInfo &RHS) {
  return static_cast<const VariableInfo &>(LHS) == RHS &&
         LHS.getSwiftImportAsAccessors() == RHS.getSwiftImportAsAccessors();
}

inline bool operator!=(const ObjCPropertyInfo &LHS,
                       const ObjCPropertyInfo &RHS) {
  return !(LHS == RHS);
}

/// Describes a function or method parameter.
class ParamInfo : public VariableInfo {
  /// Whether noescape was specified.
  LLVM_PREFERRED_TYPE(bool)
  unsigned NoEscapeSpecified : 1;

  /// Whether the this parameter has the 'noescape' attribute.
  LLVM_PREFERRED_TYPE(bool)
  unsigned NoEscape : 1;

  /// A biased RetainCountConventionKind, where 0 means "unspecified".
  ///
  /// Only relevant for out-parameters.
  unsigned RawRetainCountConvention : 3;

public:
  ParamInfo()
      : NoEscapeSpecified(false), NoEscape(false), RawRetainCountConvention() {}

  std::optional<bool> isNoEscape() const {
    if (!NoEscapeSpecified)
      return std::nullopt;
    return NoEscape;
  }
  void setNoEscape(std::optional<bool> Value) {
    NoEscapeSpecified = Value.has_value();
    NoEscape = Value.value_or(false);
  }

  std::optional<RetainCountConventionKind> getRetainCountConvention() const {
    if (!RawRetainCountConvention)
      return std::nullopt;
    return static_cast<RetainCountConventionKind>(RawRetainCountConvention - 1);
  }
  void
  setRetainCountConvention(std::optional<RetainCountConventionKind> Value) {
    RawRetainCountConvention = Value ? static_cast<unsigned>(*Value) + 1 : 0;
    assert(getRetainCountConvention() == Value && "bitfield too small");
  }

  ParamInfo &operator|=(const ParamInfo &RHS) {
    static_cast<VariableInfo &>(*this) |= RHS;

    if (!NoEscapeSpecified && RHS.NoEscapeSpecified) {
      NoEscapeSpecified = true;
      NoEscape = RHS.NoEscape;
    }

    if (!RawRetainCountConvention)
      RawRetainCountConvention = RHS.RawRetainCountConvention;

    return *this;
  }

  friend bool operator==(const ParamInfo &, const ParamInfo &);

  LLVM_DUMP_METHOD void dump(llvm::raw_ostream &OS) const;
};

inline bool operator==(const ParamInfo &LHS, const ParamInfo &RHS) {
  return static_cast<const VariableInfo &>(LHS) == RHS &&
         LHS.NoEscapeSpecified == RHS.NoEscapeSpecified &&
         LHS.NoEscape == RHS.NoEscape &&
         LHS.RawRetainCountConvention == RHS.RawRetainCountConvention;
}

inline bool operator!=(const ParamInfo &LHS, const ParamInfo &RHS) {
  return !(LHS == RHS);
}

/// API notes for a function or method.
class FunctionInfo : public CommonEntityInfo {
private:
  static constexpr const uint64_t NullabilityKindMask = 0x3;
  static constexpr const unsigned NullabilityKindSize = 2;

  static constexpr const unsigned ReturnInfoIndex = 0;

public:
  // If yes, we consider all types to be non-nullable unless otherwise noted.
  // If this flag is not set, the pointer types are considered to have
  // unknown nullability.

  /// Whether the signature has been audited with respect to nullability.
  LLVM_PREFERRED_TYPE(bool)
  unsigned NullabilityAudited : 1;

  /// Number of types whose nullability is encoded with the NullabilityPayload.
  unsigned NumAdjustedNullable : 8;

  /// A biased RetainCountConventionKind, where 0 means "unspecified".
  unsigned RawRetainCountConvention : 3;

  // NullabilityKindSize bits are used to encode the nullability. The info
  // about the return type is stored at position 0, followed by the nullability
  // of the parameters.

  /// Stores the nullability of the return type and the parameters.
  uint64_t NullabilityPayload = 0;

  /// The result type of this function, as a C type.
  std::string ResultType;

  /// The function parameters.
  std::vector<ParamInfo> Params;

  FunctionInfo()
      : NullabilityAudited(false), NumAdjustedNullable(0),
        RawRetainCountConvention() {}

  static unsigned getMaxNullabilityIndex() {
    return ((sizeof(NullabilityPayload) * CHAR_BIT) / NullabilityKindSize);
  }

  void addTypeInfo(unsigned index, NullabilityKind kind) {
    assert(index <= getMaxNullabilityIndex());
    assert(static_cast<unsigned>(kind) < NullabilityKindMask);

    NullabilityAudited = true;
    if (NumAdjustedNullable < index + 1)
      NumAdjustedNullable = index + 1;

    // Mask the bits.
    NullabilityPayload &=
        ~(NullabilityKindMask << (index * NullabilityKindSize));

    // Set the value.
    unsigned kindValue = (static_cast<unsigned>(kind))
                         << (index * NullabilityKindSize);
    NullabilityPayload |= kindValue;
  }

  /// Adds the return type info.
  void addReturnTypeInfo(NullabilityKind kind) {
    addTypeInfo(ReturnInfoIndex, kind);
  }

  /// Adds the parameter type info.
  void addParamTypeInfo(unsigned index, NullabilityKind kind) {
    addTypeInfo(index + 1, kind);
  }

  NullabilityKind getParamTypeInfo(unsigned index) const {
    return getTypeInfo(index + 1);
  }

  NullabilityKind getReturnTypeInfo() const { return getTypeInfo(0); }

  std::optional<RetainCountConventionKind> getRetainCountConvention() const {
    if (!RawRetainCountConvention)
      return std::nullopt;
    return static_cast<RetainCountConventionKind>(RawRetainCountConvention - 1);
  }
  void
  setRetainCountConvention(std::optional<RetainCountConventionKind> Value) {
    RawRetainCountConvention = Value ? static_cast<unsigned>(*Value) + 1 : 0;
    assert(getRetainCountConvention() == Value && "bitfield too small");
  }

  friend bool operator==(const FunctionInfo &, const FunctionInfo &);

private:
  NullabilityKind getTypeInfo(unsigned index) const {
    assert(NullabilityAudited &&
           "Checking the type adjustment on non-audited method.");

    // If we don't have info about this parameter, return the default.
    if (index > NumAdjustedNullable)
      return NullabilityKind::NonNull;
    auto nullability = NullabilityPayload >> (index * NullabilityKindSize);
    return static_cast<NullabilityKind>(nullability & NullabilityKindMask);
  }

public:
  LLVM_DUMP_METHOD void dump(llvm::raw_ostream &OS) const;
};

inline bool operator==(const FunctionInfo &LHS, const FunctionInfo &RHS) {
  return static_cast<const CommonEntityInfo &>(LHS) == RHS &&
         LHS.NullabilityAudited == RHS.NullabilityAudited &&
         LHS.NumAdjustedNullable == RHS.NumAdjustedNullable &&
         LHS.NullabilityPayload == RHS.NullabilityPayload &&
         LHS.ResultType == RHS.ResultType && LHS.Params == RHS.Params &&
         LHS.RawRetainCountConvention == RHS.RawRetainCountConvention;
}

inline bool operator!=(const FunctionInfo &LHS, const FunctionInfo &RHS) {
  return !(LHS == RHS);
}

/// Describes API notes data for an Objective-C method.
class ObjCMethodInfo : public FunctionInfo {
public:
  /// Whether this is a designated initializer of its class.
  LLVM_PREFERRED_TYPE(bool)
  unsigned DesignatedInit : 1;

  /// Whether this is a required initializer.
  LLVM_PREFERRED_TYPE(bool)
  unsigned RequiredInit : 1;

  ObjCMethodInfo() : DesignatedInit(false), RequiredInit(false) {}

  friend bool operator==(const ObjCMethodInfo &, const ObjCMethodInfo &);

  ObjCMethodInfo &operator|=(const ContextInfo &RHS) {
    // Merge Nullability.
    if (!NullabilityAudited) {
      if (auto Nullable = RHS.getDefaultNullability()) {
        NullabilityAudited = true;
        addTypeInfo(0, *Nullable);
      }
    }
    return *this;
  }

  LLVM_DUMP_METHOD void dump(llvm::raw_ostream &OS);
};

inline bool operator==(const ObjCMethodInfo &LHS, const ObjCMethodInfo &RHS) {
  return static_cast<const FunctionInfo &>(LHS) == RHS &&
         LHS.DesignatedInit == RHS.DesignatedInit &&
         LHS.RequiredInit == RHS.RequiredInit;
}

inline bool operator!=(const ObjCMethodInfo &LHS, const ObjCMethodInfo &RHS) {
  return !(LHS == RHS);
}

/// Describes API notes data for a global variable.
class GlobalVariableInfo : public VariableInfo {
public:
  GlobalVariableInfo() {}
};

/// Describes API notes data for a global function.
class GlobalFunctionInfo : public FunctionInfo {
public:
  GlobalFunctionInfo() {}
};

/// Describes API notes data for a C++ method.
class CXXMethodInfo : public FunctionInfo {
public:
  CXXMethodInfo() {}
};

/// Describes API notes data for an enumerator.
class EnumConstantInfo : public CommonEntityInfo {
public:
  EnumConstantInfo() {}
};

/// Describes API notes data for a tag.
class TagInfo : public CommonTypeInfo {
  LLVM_PREFERRED_TYPE(bool)
  unsigned HasFlagEnum : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsFlagEnum : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned SwiftCopyableSpecified : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned SwiftCopyable : 1;

public:
  std::optional<std::string> SwiftImportAs;
  std::optional<std::string> SwiftRetainOp;
  std::optional<std::string> SwiftReleaseOp;

  std::optional<EnumExtensibilityKind> EnumExtensibility;

  TagInfo()
      : HasFlagEnum(0), IsFlagEnum(0), SwiftCopyableSpecified(false),
        SwiftCopyable(false) {}

  std::optional<bool> isFlagEnum() const {
    if (HasFlagEnum)
      return IsFlagEnum;
    return std::nullopt;
  }
  void setFlagEnum(std::optional<bool> Value) {
    HasFlagEnum = Value.has_value();
    IsFlagEnum = Value.value_or(false);
  }

  std::optional<bool> isSwiftCopyable() const {
    return SwiftCopyableSpecified ? std::optional<bool>(SwiftCopyable)
                                  : std::nullopt;
  }
  void setSwiftCopyable(std::optional<bool> Value) {
    SwiftCopyableSpecified = Value.has_value();
    SwiftCopyable = Value.value_or(false);
  }

  TagInfo &operator|=(const TagInfo &RHS) {
    static_cast<CommonTypeInfo &>(*this) |= RHS;

    if (!SwiftImportAs)
      SwiftImportAs = RHS.SwiftImportAs;
    if (!SwiftRetainOp)
      SwiftRetainOp = RHS.SwiftRetainOp;
    if (!SwiftReleaseOp)
      SwiftReleaseOp = RHS.SwiftReleaseOp;

    if (!HasFlagEnum)
      setFlagEnum(RHS.isFlagEnum());

    if (!EnumExtensibility)
      EnumExtensibility = RHS.EnumExtensibility;

    if (!SwiftCopyableSpecified)
      setSwiftCopyable(RHS.isSwiftCopyable());

    return *this;
  }

  friend bool operator==(const TagInfo &, const TagInfo &);

  LLVM_DUMP_METHOD void dump(llvm::raw_ostream &OS);
};

inline bool operator==(const TagInfo &LHS, const TagInfo &RHS) {
  return static_cast<const CommonTypeInfo &>(LHS) == RHS &&
         LHS.SwiftImportAs == RHS.SwiftImportAs &&
         LHS.SwiftRetainOp == RHS.SwiftRetainOp &&
         LHS.SwiftReleaseOp == RHS.SwiftReleaseOp &&
         LHS.isFlagEnum() == RHS.isFlagEnum() &&
         LHS.isSwiftCopyable() == RHS.isSwiftCopyable() &&
         LHS.EnumExtensibility == RHS.EnumExtensibility;
}

inline bool operator!=(const TagInfo &LHS, const TagInfo &RHS) {
  return !(LHS == RHS);
}

/// Describes API notes data for a typedef.
class TypedefInfo : public CommonTypeInfo {
public:
  std::optional<SwiftNewTypeKind> SwiftWrapper;

  TypedefInfo() {}

  TypedefInfo &operator|=(const TypedefInfo &RHS) {
    static_cast<CommonTypeInfo &>(*this) |= RHS;
    if (!SwiftWrapper)
      SwiftWrapper = RHS.SwiftWrapper;
    return *this;
  }

  friend bool operator==(const TypedefInfo &, const TypedefInfo &);

  LLVM_DUMP_METHOD void dump(llvm::raw_ostream &OS) const;
};

inline bool operator==(const TypedefInfo &LHS, const TypedefInfo &RHS) {
  return static_cast<const CommonTypeInfo &>(LHS) == RHS &&
         LHS.SwiftWrapper == RHS.SwiftWrapper;
}

inline bool operator!=(const TypedefInfo &LHS, const TypedefInfo &RHS) {
  return !(LHS == RHS);
}

/// The file extension used for the source representation of API notes.
static const constexpr char SOURCE_APINOTES_EXTENSION[] = "apinotes";

/// Opaque context ID used to refer to an Objective-C class or protocol or a C++
/// namespace.
class ContextID {
public:
  unsigned Value;

  explicit ContextID(unsigned value) : Value(value) {}
};

enum class ContextKind : uint8_t {
  ObjCClass = 0,
  ObjCProtocol = 1,
  Namespace = 2,
  Tag = 3,
};

struct Context {
  ContextID id;
  ContextKind kind;

  Context(ContextID id, ContextKind kind) : id(id), kind(kind) {}
};

/// A temporary reference to an Objective-C selector, suitable for
/// referencing selector data on the stack.
///
/// Instances of this struct do not store references to any of the
/// data they contain; it is up to the user to ensure that the data
/// referenced by the identifier list persists.
struct ObjCSelectorRef {
  unsigned NumArgs;
  llvm::ArrayRef<llvm::StringRef> Identifiers;
};
} // namespace api_notes
} // namespace clang

#endif

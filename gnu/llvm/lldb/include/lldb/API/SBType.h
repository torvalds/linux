//===-- SBType.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBTYPE_H
#define LLDB_API_SBTYPE_H

#include "lldb/API/SBDefines.h"

namespace lldb_private {
namespace python {
class SWIGBridge;
}
} // namespace lldb_private

namespace lldb {

class SBTypeList;

class LLDB_API SBTypeMember {
public:
  SBTypeMember();

  SBTypeMember(const lldb::SBTypeMember &rhs);

  ~SBTypeMember();

  lldb::SBTypeMember &operator=(const lldb::SBTypeMember &rhs);

  explicit operator bool() const;

  bool IsValid() const;

  const char *GetName();

  lldb::SBType GetType();

  uint64_t GetOffsetInBytes();

  uint64_t GetOffsetInBits();

  bool IsBitfield();

  uint32_t GetBitfieldSizeInBits();

  bool GetDescription(lldb::SBStream &description,
                      lldb::DescriptionLevel description_level);

protected:
  friend class SBType;

  void reset(lldb_private::TypeMemberImpl *);

  lldb_private::TypeMemberImpl &ref();

  const lldb_private::TypeMemberImpl &ref() const;

  std::unique_ptr<lldb_private::TypeMemberImpl> m_opaque_up;
};

class SBTypeMemberFunction {
public:
  SBTypeMemberFunction();

  SBTypeMemberFunction(const lldb::SBTypeMemberFunction &rhs);

  ~SBTypeMemberFunction();

  lldb::SBTypeMemberFunction &operator=(const lldb::SBTypeMemberFunction &rhs);

  explicit operator bool() const;

  bool IsValid() const;

  const char *GetName();

  const char *GetDemangledName();

  const char *GetMangledName();

  lldb::SBType GetType();

  lldb::SBType GetReturnType();

  uint32_t GetNumberOfArguments();

  lldb::SBType GetArgumentTypeAtIndex(uint32_t);

  lldb::MemberFunctionKind GetKind();

  bool GetDescription(lldb::SBStream &description,
                      lldb::DescriptionLevel description_level);

protected:
  friend class SBType;

  void reset(lldb_private::TypeMemberFunctionImpl *);

  lldb_private::TypeMemberFunctionImpl &ref();

  const lldb_private::TypeMemberFunctionImpl &ref() const;

  lldb::TypeMemberFunctionImplSP m_opaque_sp;
};

class LLDB_API SBTypeStaticField {
public:
  SBTypeStaticField();

  SBTypeStaticField(const lldb::SBTypeStaticField &rhs);
  lldb::SBTypeStaticField &operator=(const lldb::SBTypeStaticField &rhs);

  ~SBTypeStaticField();

  explicit operator bool() const;

  bool IsValid() const;

  const char *GetName();

  const char *GetMangledName();

  lldb::SBType GetType();

  lldb::SBValue GetConstantValue(lldb::SBTarget target);

protected:
  friend class SBType;

  explicit SBTypeStaticField(lldb_private::CompilerDecl decl);

  std::unique_ptr<lldb_private::CompilerDecl> m_opaque_up;
};

class SBType {
public:
  SBType();

  SBType(const lldb::SBType &rhs);

  ~SBType();

  explicit operator bool() const;

  bool IsValid() const;

  uint64_t GetByteSize();

  uint64_t GetByteAlign();

  bool IsPointerType();

  bool IsReferenceType();

  bool IsFunctionType();

  bool IsPolymorphicClass();

  bool IsArrayType();

  bool IsVectorType();

  bool IsTypedefType();

  bool IsAnonymousType();

  bool IsScopedEnumerationType();

  bool IsAggregateType();

  lldb::SBType GetPointerType();

  lldb::SBType GetPointeeType();

  lldb::SBType GetReferenceType();

  lldb::SBType GetTypedefedType();

  lldb::SBType GetDereferencedType();

  lldb::SBType GetUnqualifiedType();

  lldb::SBType GetArrayElementType();

  lldb::SBType GetArrayType(uint64_t size);

  lldb::SBType GetVectorElementType();

  lldb::SBType GetCanonicalType();

  lldb::SBType GetEnumerationIntegerType();

  // Get the "lldb::BasicType" enumeration for a type. If a type is not a basic
  // type eBasicTypeInvalid will be returned
  lldb::BasicType GetBasicType();

  // The call below confusing and should really be renamed to "CreateBasicType"
  lldb::SBType GetBasicType(lldb::BasicType type);

  uint32_t GetNumberOfFields();

  uint32_t GetNumberOfDirectBaseClasses();

  uint32_t GetNumberOfVirtualBaseClasses();

  lldb::SBTypeMember GetFieldAtIndex(uint32_t idx);

  lldb::SBTypeMember GetDirectBaseClassAtIndex(uint32_t idx);

  lldb::SBTypeMember GetVirtualBaseClassAtIndex(uint32_t idx);

  lldb::SBTypeStaticField GetStaticFieldWithName(const char *name);

  lldb::SBTypeEnumMemberList GetEnumMembers();

  uint32_t GetNumberOfTemplateArguments();

  lldb::SBType GetTemplateArgumentType(uint32_t idx);

  /// Return the TemplateArgumentKind of the template argument at index idx.
  /// Variadic argument packs are automatically expanded.
  lldb::TemplateArgumentKind GetTemplateArgumentKind(uint32_t idx);

  lldb::SBType GetFunctionReturnType();

  lldb::SBTypeList GetFunctionArgumentTypes();

  uint32_t GetNumberOfMemberFunctions();

  lldb::SBTypeMemberFunction GetMemberFunctionAtIndex(uint32_t idx);

  lldb::SBModule GetModule();

  const char *GetName();

  const char *GetDisplayTypeName();

  lldb::TypeClass GetTypeClass();

  bool IsTypeComplete();

  uint32_t GetTypeFlags();

  bool GetDescription(lldb::SBStream &description,
                      lldb::DescriptionLevel description_level);

  lldb::SBType FindDirectNestedType(const char *name);

  lldb::SBType &operator=(const lldb::SBType &rhs);

  bool operator==(lldb::SBType &rhs);

  bool operator!=(lldb::SBType &rhs);

protected:
  lldb_private::TypeImpl &ref();

  const lldb_private::TypeImpl &ref() const;

  lldb::TypeImplSP GetSP();

  void SetSP(const lldb::TypeImplSP &type_impl_sp);

  lldb::TypeImplSP m_opaque_sp;

  friend class SBFunction;
  friend class SBModule;
  friend class SBTarget;
  friend class SBTypeEnumMember;
  friend class SBTypeEnumMemberList;
  friend class SBTypeNameSpecifier;
  friend class SBTypeMember;
  friend class SBTypeMemberFunction;
  friend class SBTypeStaticField;
  friend class SBTypeList;
  friend class SBValue;
  friend class SBWatchpoint;

  friend class lldb_private::python::SWIGBridge;

  SBType(const lldb_private::CompilerType &);
  SBType(const lldb::TypeSP &);
  SBType(const lldb::TypeImplSP &);
};

class SBTypeList {
public:
  SBTypeList();

  SBTypeList(const lldb::SBTypeList &rhs);

  ~SBTypeList();

  lldb::SBTypeList &operator=(const lldb::SBTypeList &rhs);

  explicit operator bool() const;

  bool IsValid();

  void Append(lldb::SBType type);

  lldb::SBType GetTypeAtIndex(uint32_t index);

  uint32_t GetSize();

private:
  std::unique_ptr<lldb_private::TypeListImpl> m_opaque_up;
  friend class SBModule;
  friend class SBCompileUnit;
};

} // namespace lldb

#endif // LLDB_API_SBTYPE_H

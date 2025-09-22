//===-- SBType.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBType.h"
#include "Utils.h"
#include "lldb/API/SBDefines.h"
#include "lldb/API/SBModule.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBTypeEnumMember.h"
#include "lldb/Core/Mangled.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/Symbol/CompilerDecl.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Instrumentation.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/Stream.h"

#include "llvm/ADT/APSInt.h"
#include "llvm/Support/MathExtras.h"

#include <memory>
#include <optional>

using namespace lldb;
using namespace lldb_private;

SBType::SBType() { LLDB_INSTRUMENT_VA(this); }

SBType::SBType(const CompilerType &type) : m_opaque_sp(new TypeImpl(type)) {}

SBType::SBType(const lldb::TypeSP &type_sp)
    : m_opaque_sp(new TypeImpl(type_sp)) {}

SBType::SBType(const lldb::TypeImplSP &type_impl_sp)
    : m_opaque_sp(type_impl_sp) {}

SBType::SBType(const SBType &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs) {
    m_opaque_sp = rhs.m_opaque_sp;
  }
}

// SBType::SBType (TypeImpl* impl) :
//    m_opaque_up(impl)
//{}
//
bool SBType::operator==(SBType &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (!IsValid())
    return !rhs.IsValid();

  if (!rhs.IsValid())
    return false;

  return *m_opaque_sp.get() == *rhs.m_opaque_sp.get();
}

bool SBType::operator!=(SBType &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (!IsValid())
    return rhs.IsValid();

  if (!rhs.IsValid())
    return true;

  return *m_opaque_sp.get() != *rhs.m_opaque_sp.get();
}

lldb::TypeImplSP SBType::GetSP() { return m_opaque_sp; }

void SBType::SetSP(const lldb::TypeImplSP &type_impl_sp) {
  m_opaque_sp = type_impl_sp;
}

SBType &SBType::operator=(const SBType &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs) {
    m_opaque_sp = rhs.m_opaque_sp;
  }
  return *this;
}

SBType::~SBType() = default;

TypeImpl &SBType::ref() {
  if (m_opaque_sp.get() == nullptr)
    m_opaque_sp = std::make_shared<TypeImpl>();
  return *m_opaque_sp;
}

const TypeImpl &SBType::ref() const {
  // "const SBAddress &addr" should already have checked "addr.IsValid()" prior
  // to calling this function. In case you didn't we will assert and die to let
  // you know.
  assert(m_opaque_sp.get());
  return *m_opaque_sp;
}

bool SBType::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBType::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_sp.get() == nullptr)
    return false;

  return m_opaque_sp->IsValid();
}

uint64_t SBType::GetByteSize() {
  LLDB_INSTRUMENT_VA(this);

  if (IsValid())
    if (std::optional<uint64_t> size =
            m_opaque_sp->GetCompilerType(false).GetByteSize(nullptr))
      return *size;
  return 0;
}

uint64_t SBType::GetByteAlign() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return 0;

  std::optional<uint64_t> bit_align =
      m_opaque_sp->GetCompilerType(/*prefer_dynamic=*/false)
          .GetTypeBitAlign(nullptr);
  return llvm::divideCeil(bit_align.value_or(0), 8);
}

bool SBType::IsPointerType() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return false;
  return m_opaque_sp->GetCompilerType(true).IsPointerType();
}

bool SBType::IsArrayType() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return false;
  return m_opaque_sp->GetCompilerType(true).IsArrayType(nullptr, nullptr,
                                                        nullptr);
}

bool SBType::IsVectorType() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return false;
  return m_opaque_sp->GetCompilerType(true).IsVectorType(nullptr, nullptr);
}

bool SBType::IsReferenceType() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return false;
  return m_opaque_sp->GetCompilerType(true).IsReferenceType();
}

SBType SBType::GetPointerType() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return SBType();

  return SBType(TypeImplSP(new TypeImpl(m_opaque_sp->GetPointerType())));
}

SBType SBType::GetPointeeType() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return SBType();
  return SBType(TypeImplSP(new TypeImpl(m_opaque_sp->GetPointeeType())));
}

SBType SBType::GetReferenceType() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return SBType();
  return SBType(TypeImplSP(new TypeImpl(m_opaque_sp->GetReferenceType())));
}

SBType SBType::GetTypedefedType() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return SBType();
  return SBType(TypeImplSP(new TypeImpl(m_opaque_sp->GetTypedefedType())));
}

SBType SBType::GetDereferencedType() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return SBType();
  return SBType(TypeImplSP(new TypeImpl(m_opaque_sp->GetDereferencedType())));
}

SBType SBType::GetArrayElementType() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return SBType();
  return SBType(TypeImplSP(new TypeImpl(
      m_opaque_sp->GetCompilerType(true).GetArrayElementType(nullptr))));
}

SBType SBType::GetArrayType(uint64_t size) {
  LLDB_INSTRUMENT_VA(this, size);

  if (!IsValid())
    return SBType();
  return SBType(TypeImplSP(
      new TypeImpl(m_opaque_sp->GetCompilerType(true).GetArrayType(size))));
}

SBType SBType::GetVectorElementType() {
  LLDB_INSTRUMENT_VA(this);

  SBType type_sb;
  if (IsValid()) {
    CompilerType vector_element_type;
    if (m_opaque_sp->GetCompilerType(true).IsVectorType(&vector_element_type,
                                                        nullptr))
      type_sb.SetSP(TypeImplSP(new TypeImpl(vector_element_type)));
  }
  return type_sb;
}

bool SBType::IsFunctionType() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return false;
  return m_opaque_sp->GetCompilerType(true).IsFunctionType();
}

bool SBType::IsPolymorphicClass() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return false;
  return m_opaque_sp->GetCompilerType(true).IsPolymorphicClass();
}

bool SBType::IsTypedefType() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return false;
  return m_opaque_sp->GetCompilerType(true).IsTypedefType();
}

bool SBType::IsAnonymousType() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return false;
  return m_opaque_sp->GetCompilerType(true).IsAnonymousType();
}

bool SBType::IsScopedEnumerationType() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return false;
  return m_opaque_sp->GetCompilerType(true).IsScopedEnumerationType();
}

bool SBType::IsAggregateType() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return false;
  return m_opaque_sp->GetCompilerType(true).IsAggregateType();
}

lldb::SBType SBType::GetFunctionReturnType() {
  LLDB_INSTRUMENT_VA(this);

  if (IsValid()) {
    CompilerType return_type(
        m_opaque_sp->GetCompilerType(true).GetFunctionReturnType());
    if (return_type.IsValid())
      return SBType(return_type);
  }
  return lldb::SBType();
}

lldb::SBTypeList SBType::GetFunctionArgumentTypes() {
  LLDB_INSTRUMENT_VA(this);

  SBTypeList sb_type_list;
  if (IsValid()) {
    CompilerType func_type(m_opaque_sp->GetCompilerType(true));
    size_t count = func_type.GetNumberOfFunctionArguments();
    for (size_t i = 0; i < count; i++) {
      sb_type_list.Append(SBType(func_type.GetFunctionArgumentAtIndex(i)));
    }
  }
  return sb_type_list;
}

uint32_t SBType::GetNumberOfMemberFunctions() {
  LLDB_INSTRUMENT_VA(this);

  if (IsValid()) {
    return m_opaque_sp->GetCompilerType(true).GetNumMemberFunctions();
  }
  return 0;
}

lldb::SBTypeMemberFunction SBType::GetMemberFunctionAtIndex(uint32_t idx) {
  LLDB_INSTRUMENT_VA(this, idx);

  SBTypeMemberFunction sb_func_type;
  if (IsValid())
    sb_func_type.reset(new TypeMemberFunctionImpl(
        m_opaque_sp->GetCompilerType(true).GetMemberFunctionAtIndex(idx)));
  return sb_func_type;
}

SBTypeStaticField::SBTypeStaticField() { LLDB_INSTRUMENT_VA(this); }

SBTypeStaticField::SBTypeStaticField(lldb_private::CompilerDecl decl)
    : m_opaque_up(decl ? std::make_unique<CompilerDecl>(decl) : nullptr) {}

SBTypeStaticField::SBTypeStaticField(const SBTypeStaticField &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_opaque_up = clone(rhs.m_opaque_up);
}

SBTypeStaticField &SBTypeStaticField::operator=(const SBTypeStaticField &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_opaque_up = clone(rhs.m_opaque_up);
  return *this;
}

SBTypeStaticField::~SBTypeStaticField() { LLDB_INSTRUMENT_VA(this); }

SBTypeStaticField::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return IsValid();
}

bool SBTypeStaticField::IsValid() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up != nullptr;
}

const char *SBTypeStaticField::GetName() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return "";
  return m_opaque_up->GetName().GetCString();
}

const char *SBTypeStaticField::GetMangledName() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return "";
  return m_opaque_up->GetMangledName().GetCString();
}

SBType SBTypeStaticField::GetType() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return SBType();
  return SBType(m_opaque_up->GetType());
}

SBValue SBTypeStaticField::GetConstantValue(lldb::SBTarget target) {
  LLDB_INSTRUMENT_VA(this, target);

  if (!IsValid())
    return SBValue();

  Scalar value = m_opaque_up->GetConstantValue();
  if (!value.IsValid())
    return SBValue();
  DataExtractor data;
  value.GetData(data);
  auto value_obj_sp = ValueObjectConstResult::Create(
      target.GetSP().get(), m_opaque_up->GetType(), m_opaque_up->GetName(),
      data);
  return SBValue(std::move(value_obj_sp));
}

lldb::SBType SBType::GetUnqualifiedType() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return SBType();
  return SBType(TypeImplSP(new TypeImpl(m_opaque_sp->GetUnqualifiedType())));
}

lldb::SBType SBType::GetCanonicalType() {
  LLDB_INSTRUMENT_VA(this);

  if (IsValid())
    return SBType(TypeImplSP(new TypeImpl(m_opaque_sp->GetCanonicalType())));
  return SBType();
}

SBType SBType::GetEnumerationIntegerType() {
  LLDB_INSTRUMENT_VA(this);

  if (IsValid()) {
    return SBType(
        m_opaque_sp->GetCompilerType(true).GetEnumerationIntegerType());
  }
  return SBType();
}

lldb::BasicType SBType::GetBasicType() {
  LLDB_INSTRUMENT_VA(this);

  if (IsValid())
    return m_opaque_sp->GetCompilerType(false).GetBasicTypeEnumeration();
  return eBasicTypeInvalid;
}

SBType SBType::GetBasicType(lldb::BasicType basic_type) {
  LLDB_INSTRUMENT_VA(this, basic_type);

  if (IsValid() && m_opaque_sp->IsValid())
    if (auto ts = m_opaque_sp->GetTypeSystem(false))
      return SBType(ts->GetBasicTypeFromAST(basic_type));
  return SBType();
}

uint32_t SBType::GetNumberOfDirectBaseClasses() {
  LLDB_INSTRUMENT_VA(this);

  if (IsValid())
    return m_opaque_sp->GetCompilerType(true).GetNumDirectBaseClasses();
  return 0;
}

uint32_t SBType::GetNumberOfVirtualBaseClasses() {
  LLDB_INSTRUMENT_VA(this);

  if (IsValid())
    return m_opaque_sp->GetCompilerType(true).GetNumVirtualBaseClasses();
  return 0;
}

uint32_t SBType::GetNumberOfFields() {
  LLDB_INSTRUMENT_VA(this);

  if (IsValid())
    return m_opaque_sp->GetCompilerType(true).GetNumFields();
  return 0;
}

bool SBType::GetDescription(SBStream &description,
                            lldb::DescriptionLevel description_level) {
  LLDB_INSTRUMENT_VA(this, description, description_level);

  Stream &strm = description.ref();

  if (m_opaque_sp) {
    m_opaque_sp->GetDescription(strm, description_level);
  } else
    strm.PutCString("No value");

  return true;
}

SBTypeMember SBType::GetDirectBaseClassAtIndex(uint32_t idx) {
  LLDB_INSTRUMENT_VA(this, idx);

  SBTypeMember sb_type_member;
  if (IsValid()) {
    uint32_t bit_offset = 0;
    CompilerType base_class_type =
        m_opaque_sp->GetCompilerType(true).GetDirectBaseClassAtIndex(
            idx, &bit_offset);
    if (base_class_type.IsValid())
      sb_type_member.reset(new TypeMemberImpl(
          TypeImplSP(new TypeImpl(base_class_type)), bit_offset));
  }
  return sb_type_member;
}

SBTypeMember SBType::GetVirtualBaseClassAtIndex(uint32_t idx) {
  LLDB_INSTRUMENT_VA(this, idx);

  SBTypeMember sb_type_member;
  if (IsValid()) {
    uint32_t bit_offset = 0;
    CompilerType base_class_type =
        m_opaque_sp->GetCompilerType(true).GetVirtualBaseClassAtIndex(
            idx, &bit_offset);
    if (base_class_type.IsValid())
      sb_type_member.reset(new TypeMemberImpl(
          TypeImplSP(new TypeImpl(base_class_type)), bit_offset));
  }
  return sb_type_member;
}

SBTypeStaticField SBType::GetStaticFieldWithName(const char *name) {
  LLDB_INSTRUMENT_VA(this, name);

  if (!IsValid() || !name)
    return SBTypeStaticField();

  return SBTypeStaticField(m_opaque_sp->GetCompilerType(/*prefer_dynamic=*/true)
                               .GetStaticFieldWithName(name));
}

SBTypeEnumMemberList SBType::GetEnumMembers() {
  LLDB_INSTRUMENT_VA(this);

  SBTypeEnumMemberList sb_enum_member_list;
  if (IsValid()) {
    CompilerType this_type(m_opaque_sp->GetCompilerType(true));
    if (this_type.IsValid()) {
      this_type.ForEachEnumerator([&sb_enum_member_list](
                                      const CompilerType &integer_type,
                                      ConstString name,
                                      const llvm::APSInt &value) -> bool {
        SBTypeEnumMember enum_member(
            lldb::TypeEnumMemberImplSP(new TypeEnumMemberImpl(
                lldb::TypeImplSP(new TypeImpl(integer_type)), name, value)));
        sb_enum_member_list.Append(enum_member);
        return true; // Keep iterating
      });
    }
  }
  return sb_enum_member_list;
}

SBTypeMember SBType::GetFieldAtIndex(uint32_t idx) {
  LLDB_INSTRUMENT_VA(this, idx);

  SBTypeMember sb_type_member;
  if (IsValid()) {
    CompilerType this_type(m_opaque_sp->GetCompilerType(false));
    if (this_type.IsValid()) {
      uint64_t bit_offset = 0;
      uint32_t bitfield_bit_size = 0;
      bool is_bitfield = false;
      std::string name_sstr;
      CompilerType field_type(this_type.GetFieldAtIndex(
          idx, name_sstr, &bit_offset, &bitfield_bit_size, &is_bitfield));
      if (field_type.IsValid()) {
        ConstString name;
        if (!name_sstr.empty())
          name.SetCString(name_sstr.c_str());
        sb_type_member.reset(
            new TypeMemberImpl(TypeImplSP(new TypeImpl(field_type)), bit_offset,
                               name, bitfield_bit_size, is_bitfield));
      }
    }
  }
  return sb_type_member;
}

bool SBType::IsTypeComplete() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return false;
  CompilerType compiler_type = m_opaque_sp->GetCompilerType(false);
  // Only return true if we have a complete type and it wasn't forcefully
  // completed.
  if (compiler_type.IsCompleteType())
    return !compiler_type.IsForcefullyCompleted();
  return false;
}

uint32_t SBType::GetTypeFlags() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return 0;
  return m_opaque_sp->GetCompilerType(true).GetTypeInfo();
}

lldb::SBModule SBType::GetModule() {
  LLDB_INSTRUMENT_VA(this);

  lldb::SBModule sb_module;
  if (!IsValid())
    return sb_module;

  sb_module.SetSP(m_opaque_sp->GetModule());
  return sb_module;
}

const char *SBType::GetName() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return "";
  return m_opaque_sp->GetName().GetCString();
}

const char *SBType::GetDisplayTypeName() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return "";
  return m_opaque_sp->GetDisplayTypeName().GetCString();
}

lldb::TypeClass SBType::GetTypeClass() {
  LLDB_INSTRUMENT_VA(this);

  if (IsValid())
    return m_opaque_sp->GetCompilerType(true).GetTypeClass();
  return lldb::eTypeClassInvalid;
}

uint32_t SBType::GetNumberOfTemplateArguments() {
  LLDB_INSTRUMENT_VA(this);

  if (IsValid())
    return m_opaque_sp->GetCompilerType(false).GetNumTemplateArguments(
        /*expand_pack=*/true);
  return 0;
}

lldb::SBType SBType::GetTemplateArgumentType(uint32_t idx) {
  LLDB_INSTRUMENT_VA(this, idx);

  if (!IsValid())
    return SBType();

  CompilerType type;
  const bool expand_pack = true;
  switch(GetTemplateArgumentKind(idx)) {
    case eTemplateArgumentKindType:
      type = m_opaque_sp->GetCompilerType(false).GetTypeTemplateArgument(
          idx, expand_pack);
      break;
    case eTemplateArgumentKindIntegral:
      type = m_opaque_sp->GetCompilerType(false)
                 .GetIntegralTemplateArgument(idx, expand_pack)
                 ->type;
      break;
    default:
      break;
  }
  if (type.IsValid())
    return SBType(type);
  return SBType();
}

lldb::TemplateArgumentKind SBType::GetTemplateArgumentKind(uint32_t idx) {
  LLDB_INSTRUMENT_VA(this, idx);

  if (IsValid())
    return m_opaque_sp->GetCompilerType(false).GetTemplateArgumentKind(
        idx, /*expand_pack=*/true);
  return eTemplateArgumentKindNull;
}

SBType SBType::FindDirectNestedType(const char *name) {
  LLDB_INSTRUMENT_VA(this, name);

  if (!IsValid())
    return SBType();
  return SBType(m_opaque_sp->FindDirectNestedType(name));
}

SBTypeList::SBTypeList() : m_opaque_up(new TypeListImpl()) {
  LLDB_INSTRUMENT_VA(this);
}

SBTypeList::SBTypeList(const SBTypeList &rhs)
    : m_opaque_up(new TypeListImpl()) {
  LLDB_INSTRUMENT_VA(this, rhs);

  for (uint32_t i = 0, rhs_size = const_cast<SBTypeList &>(rhs).GetSize();
       i < rhs_size; i++)
    Append(const_cast<SBTypeList &>(rhs).GetTypeAtIndex(i));
}

bool SBTypeList::IsValid() {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBTypeList::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return (m_opaque_up != nullptr);
}

SBTypeList &SBTypeList::operator=(const SBTypeList &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs) {
    m_opaque_up = std::make_unique<TypeListImpl>();
    for (uint32_t i = 0, rhs_size = const_cast<SBTypeList &>(rhs).GetSize();
         i < rhs_size; i++)
      Append(const_cast<SBTypeList &>(rhs).GetTypeAtIndex(i));
  }
  return *this;
}

void SBTypeList::Append(SBType type) {
  LLDB_INSTRUMENT_VA(this, type);

  if (type.IsValid())
    m_opaque_up->Append(type.m_opaque_sp);
}

SBType SBTypeList::GetTypeAtIndex(uint32_t index) {
  LLDB_INSTRUMENT_VA(this, index);

  if (m_opaque_up)
    return SBType(m_opaque_up->GetTypeAtIndex(index));
  return SBType();
}

uint32_t SBTypeList::GetSize() {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->GetSize();
}

SBTypeList::~SBTypeList() = default;

SBTypeMember::SBTypeMember() { LLDB_INSTRUMENT_VA(this); }

SBTypeMember::~SBTypeMember() = default;

SBTypeMember::SBTypeMember(const SBTypeMember &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs) {
    if (rhs.IsValid())
      m_opaque_up = std::make_unique<TypeMemberImpl>(rhs.ref());
  }
}

lldb::SBTypeMember &SBTypeMember::operator=(const lldb::SBTypeMember &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs) {
    if (rhs.IsValid())
      m_opaque_up = std::make_unique<TypeMemberImpl>(rhs.ref());
  }
  return *this;
}

bool SBTypeMember::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBTypeMember::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up.get();
}

const char *SBTypeMember::GetName() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_up)
    return m_opaque_up->GetName().GetCString();
  return nullptr;
}

SBType SBTypeMember::GetType() {
  LLDB_INSTRUMENT_VA(this);

  SBType sb_type;
  if (m_opaque_up) {
    sb_type.SetSP(m_opaque_up->GetTypeImpl());
  }
  return sb_type;
}

uint64_t SBTypeMember::GetOffsetInBytes() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_up)
    return m_opaque_up->GetBitOffset() / 8u;
  return 0;
}

uint64_t SBTypeMember::GetOffsetInBits() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_up)
    return m_opaque_up->GetBitOffset();
  return 0;
}

bool SBTypeMember::IsBitfield() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_up)
    return m_opaque_up->GetIsBitfield();
  return false;
}

uint32_t SBTypeMember::GetBitfieldSizeInBits() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_up)
    return m_opaque_up->GetBitfieldBitSize();
  return 0;
}

bool SBTypeMember::GetDescription(lldb::SBStream &description,
                                  lldb::DescriptionLevel description_level) {
  LLDB_INSTRUMENT_VA(this, description, description_level);

  Stream &strm = description.ref();

  if (m_opaque_up) {
    const uint32_t bit_offset = m_opaque_up->GetBitOffset();
    const uint32_t byte_offset = bit_offset / 8u;
    const uint32_t byte_bit_offset = bit_offset % 8u;
    const char *name = m_opaque_up->GetName().GetCString();
    if (byte_bit_offset)
      strm.Printf("+%u + %u bits: (", byte_offset, byte_bit_offset);
    else
      strm.Printf("+%u: (", byte_offset);

    TypeImplSP type_impl_sp(m_opaque_up->GetTypeImpl());
    if (type_impl_sp)
      type_impl_sp->GetDescription(strm, description_level);

    strm.Printf(") %s", name);
    if (m_opaque_up->GetIsBitfield()) {
      const uint32_t bitfield_bit_size = m_opaque_up->GetBitfieldBitSize();
      strm.Printf(" : %u", bitfield_bit_size);
    }
  } else {
    strm.PutCString("No value");
  }
  return true;
}

void SBTypeMember::reset(TypeMemberImpl *type_member_impl) {
  m_opaque_up.reset(type_member_impl);
}

TypeMemberImpl &SBTypeMember::ref() {
  if (m_opaque_up == nullptr)
    m_opaque_up = std::make_unique<TypeMemberImpl>();
  return *m_opaque_up;
}

const TypeMemberImpl &SBTypeMember::ref() const { return *m_opaque_up; }

SBTypeMemberFunction::SBTypeMemberFunction() { LLDB_INSTRUMENT_VA(this); }

SBTypeMemberFunction::~SBTypeMemberFunction() = default;

SBTypeMemberFunction::SBTypeMemberFunction(const SBTypeMemberFunction &rhs)
    : m_opaque_sp(rhs.m_opaque_sp) {
  LLDB_INSTRUMENT_VA(this, rhs);
}

lldb::SBTypeMemberFunction &SBTypeMemberFunction::
operator=(const lldb::SBTypeMemberFunction &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs)
    m_opaque_sp = rhs.m_opaque_sp;
  return *this;
}

bool SBTypeMemberFunction::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBTypeMemberFunction::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp.get();
}

const char *SBTypeMemberFunction::GetName() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_sp)
    return m_opaque_sp->GetName().GetCString();
  return nullptr;
}

const char *SBTypeMemberFunction::GetDemangledName() {
  LLDB_INSTRUMENT_VA(this);

  if (!m_opaque_sp)
    return nullptr;

  ConstString mangled_str = m_opaque_sp->GetMangledName();
  if (!mangled_str)
    return nullptr;

  Mangled mangled(mangled_str);
  return mangled.GetDemangledName().GetCString();
}

const char *SBTypeMemberFunction::GetMangledName() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_sp)
    return m_opaque_sp->GetMangledName().GetCString();
  return nullptr;
}

SBType SBTypeMemberFunction::GetType() {
  LLDB_INSTRUMENT_VA(this);

  SBType sb_type;
  if (m_opaque_sp) {
    sb_type.SetSP(lldb::TypeImplSP(new TypeImpl(m_opaque_sp->GetType())));
  }
  return sb_type;
}

lldb::SBType SBTypeMemberFunction::GetReturnType() {
  LLDB_INSTRUMENT_VA(this);

  SBType sb_type;
  if (m_opaque_sp) {
    sb_type.SetSP(lldb::TypeImplSP(new TypeImpl(m_opaque_sp->GetReturnType())));
  }
  return sb_type;
}

uint32_t SBTypeMemberFunction::GetNumberOfArguments() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_sp)
    return m_opaque_sp->GetNumArguments();
  return 0;
}

lldb::SBType SBTypeMemberFunction::GetArgumentTypeAtIndex(uint32_t i) {
  LLDB_INSTRUMENT_VA(this, i);

  SBType sb_type;
  if (m_opaque_sp) {
    sb_type.SetSP(
        lldb::TypeImplSP(new TypeImpl(m_opaque_sp->GetArgumentAtIndex(i))));
  }
  return sb_type;
}

lldb::MemberFunctionKind SBTypeMemberFunction::GetKind() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_sp)
    return m_opaque_sp->GetKind();
  return lldb::eMemberFunctionKindUnknown;
}

bool SBTypeMemberFunction::GetDescription(
    lldb::SBStream &description, lldb::DescriptionLevel description_level) {
  LLDB_INSTRUMENT_VA(this, description, description_level);

  Stream &strm = description.ref();

  if (m_opaque_sp)
    return m_opaque_sp->GetDescription(strm);

  return false;
}

void SBTypeMemberFunction::reset(TypeMemberFunctionImpl *type_member_impl) {
  m_opaque_sp.reset(type_member_impl);
}

TypeMemberFunctionImpl &SBTypeMemberFunction::ref() {
  if (!m_opaque_sp)
    m_opaque_sp = std::make_shared<TypeMemberFunctionImpl>();
  return *m_opaque_sp.get();
}

const TypeMemberFunctionImpl &SBTypeMemberFunction::ref() const {
  return *m_opaque_sp.get();
}

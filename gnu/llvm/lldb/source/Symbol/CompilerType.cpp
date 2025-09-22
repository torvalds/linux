//===-- CompilerType.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/CompilerType.h"

#include "lldb/Core/Debugger.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StreamString.h"

#include <iterator>
#include <mutex>
#include <optional>

using namespace lldb;
using namespace lldb_private;

// Tests

bool CompilerType::IsAggregateType() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->IsAggregateType(m_type);
  return false;
}

bool CompilerType::IsAnonymousType() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->IsAnonymousType(m_type);
  return false;
}

bool CompilerType::IsScopedEnumerationType() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->IsScopedEnumerationType(m_type);
  return false;
}

bool CompilerType::IsArrayType(CompilerType *element_type_ptr, uint64_t *size,
                               bool *is_incomplete) const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->IsArrayType(m_type, element_type_ptr, size,
                                      is_incomplete);

  if (element_type_ptr)
    element_type_ptr->Clear();
  if (size)
    *size = 0;
  if (is_incomplete)
    *is_incomplete = false;
  return false;
}

bool CompilerType::IsVectorType(CompilerType *element_type,
                                uint64_t *size) const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->IsVectorType(m_type, element_type, size);
  return false;
}

bool CompilerType::IsRuntimeGeneratedType() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->IsRuntimeGeneratedType(m_type);
  return false;
}

bool CompilerType::IsCharType() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->IsCharType(m_type);
  return false;
}

bool CompilerType::IsCompleteType() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->IsCompleteType(m_type);
  return false;
}

bool CompilerType::IsForcefullyCompleted() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->IsForcefullyCompleted(m_type);
  return false;
}

bool CompilerType::IsConst() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->IsConst(m_type);
  return false;
}

unsigned CompilerType::GetPtrAuthKey() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetPtrAuthKey(m_type);
  return 0;
}

unsigned CompilerType::GetPtrAuthDiscriminator() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetPtrAuthDiscriminator(m_type);
  return 0;
}

bool CompilerType::GetPtrAuthAddressDiversity() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetPtrAuthAddressDiversity(m_type);
  return false;
}

bool CompilerType::IsFunctionType() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->IsFunctionType(m_type);
  return false;
}

// Used to detect "Homogeneous Floating-point Aggregates"
uint32_t
CompilerType::IsHomogeneousAggregate(CompilerType *base_type_ptr) const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->IsHomogeneousAggregate(m_type, base_type_ptr);
  return 0;
}

size_t CompilerType::GetNumberOfFunctionArguments() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetNumberOfFunctionArguments(m_type);
  return 0;
}

CompilerType
CompilerType::GetFunctionArgumentAtIndex(const size_t index) const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetFunctionArgumentAtIndex(m_type, index);
  return CompilerType();
}

bool CompilerType::IsFunctionPointerType() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->IsFunctionPointerType(m_type);
  return false;
}

bool CompilerType::IsMemberFunctionPointerType() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->IsMemberFunctionPointerType(m_type);
  return false;
}

bool CompilerType::IsBlockPointerType(
    CompilerType *function_pointer_type_ptr) const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->IsBlockPointerType(m_type, function_pointer_type_ptr);
  return false;
}

bool CompilerType::IsIntegerType(bool &is_signed) const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->IsIntegerType(m_type, is_signed);
  return false;
}

bool CompilerType::IsEnumerationType(bool &is_signed) const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->IsEnumerationType(m_type, is_signed);
  return false;
}

bool CompilerType::IsIntegerOrEnumerationType(bool &is_signed) const {
  return IsIntegerType(is_signed) || IsEnumerationType(is_signed);
}

bool CompilerType::IsPointerType(CompilerType *pointee_type) const {
  if (IsValid()) {
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->IsPointerType(m_type, pointee_type);
  }
  if (pointee_type)
    pointee_type->Clear();
  return false;
}

bool CompilerType::IsPointerOrReferenceType(CompilerType *pointee_type) const {
  if (IsValid()) {
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->IsPointerOrReferenceType(m_type, pointee_type);
  }
  if (pointee_type)
    pointee_type->Clear();
  return false;
}

bool CompilerType::IsReferenceType(CompilerType *pointee_type,
                                   bool *is_rvalue) const {
  if (IsValid()) {
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->IsReferenceType(m_type, pointee_type, is_rvalue);
  }
  if (pointee_type)
    pointee_type->Clear();
  return false;
}

bool CompilerType::ShouldTreatScalarValueAsAddress() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->ShouldTreatScalarValueAsAddress(m_type);
  return false;
}

bool CompilerType::IsFloatingPointType(uint32_t &count,
                                       bool &is_complex) const {
  if (IsValid()) {
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->IsFloatingPointType(m_type, count, is_complex);
  }
  count = 0;
  is_complex = false;
  return false;
}

bool CompilerType::IsDefined() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->IsDefined(m_type);
  return true;
}

bool CompilerType::IsPolymorphicClass() const {
  if (IsValid()) {
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->IsPolymorphicClass(m_type);
  }
  return false;
}

bool CompilerType::IsPossibleDynamicType(CompilerType *dynamic_pointee_type,
                                         bool check_cplusplus,
                                         bool check_objc) const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->IsPossibleDynamicType(m_type, dynamic_pointee_type,
                                                check_cplusplus, check_objc);
  return false;
}

bool CompilerType::IsScalarType() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->IsScalarType(m_type);
  return false;
}

bool CompilerType::IsTemplateType() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->IsTemplateType(m_type);
  return false;
}

bool CompilerType::IsTypedefType() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->IsTypedefType(m_type);
  return false;
}

bool CompilerType::IsVoidType() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->IsVoidType(m_type);
  return false;
}

bool CompilerType::IsPointerToScalarType() const {
  if (!IsValid())
    return false;

  return IsPointerType() && GetPointeeType().IsScalarType();
}

bool CompilerType::IsArrayOfScalarType() const {
  CompilerType element_type;
  if (IsArrayType(&element_type))
    return element_type.IsScalarType();
  return false;
}

bool CompilerType::IsBeingDefined() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->IsBeingDefined(m_type);
  return false;
}

bool CompilerType::IsInteger() const {
  bool is_signed = false; // May be reset by the call below.
  return IsIntegerType(is_signed);
}

bool CompilerType::IsFloat() const {
  uint32_t count = 0;
  bool is_complex = false;
  return IsFloatingPointType(count, is_complex);
}

bool CompilerType::IsEnumerationType() const {
  bool is_signed = false; // May be reset by the call below.
  return IsEnumerationType(is_signed);
}

bool CompilerType::IsUnscopedEnumerationType() const {
  return IsEnumerationType() && !IsScopedEnumerationType();
}

bool CompilerType::IsIntegerOrUnscopedEnumerationType() const {
  return IsInteger() || IsUnscopedEnumerationType();
}

bool CompilerType::IsSigned() const {
  return GetTypeInfo() & lldb::eTypeIsSigned;
}

bool CompilerType::IsNullPtrType() const {
  return GetCanonicalType().GetBasicTypeEnumeration() ==
         lldb::eBasicTypeNullPtr;
}

bool CompilerType::IsBoolean() const {
  return GetCanonicalType().GetBasicTypeEnumeration() == lldb::eBasicTypeBool;
}

bool CompilerType::IsEnumerationIntegerTypeSigned() const {
  if (IsValid())
    return GetEnumerationIntegerType().GetTypeInfo() & lldb::eTypeIsSigned;

  return false;
}

bool CompilerType::IsScalarOrUnscopedEnumerationType() const {
  return IsScalarType() || IsUnscopedEnumerationType();
}

bool CompilerType::IsPromotableIntegerType() const {
  // Unscoped enums are always considered as promotable, even if their
  // underlying type does not need to be promoted (e.g. "int").
  if (IsUnscopedEnumerationType())
    return true;

  switch (GetCanonicalType().GetBasicTypeEnumeration()) {
  case lldb::eBasicTypeBool:
  case lldb::eBasicTypeChar:
  case lldb::eBasicTypeSignedChar:
  case lldb::eBasicTypeUnsignedChar:
  case lldb::eBasicTypeShort:
  case lldb::eBasicTypeUnsignedShort:
  case lldb::eBasicTypeWChar:
  case lldb::eBasicTypeSignedWChar:
  case lldb::eBasicTypeUnsignedWChar:
  case lldb::eBasicTypeChar16:
  case lldb::eBasicTypeChar32:
    return true;

  default:
    return false;
  }

  llvm_unreachable("All cases handled above.");
}

bool CompilerType::IsPointerToVoid() const {
  if (!IsValid())
    return false;

  return IsPointerType() &&
         GetPointeeType().GetBasicTypeEnumeration() == lldb::eBasicTypeVoid;
}

bool CompilerType::IsRecordType() const {
  if (!IsValid())
    return false;

  return GetCanonicalType().GetTypeClass() &
         (lldb::eTypeClassClass | lldb::eTypeClassStruct |
          lldb::eTypeClassUnion);
}

bool CompilerType::IsVirtualBase(CompilerType target_base,
                                 CompilerType *virtual_base,
                                 bool carry_virtual) const {
  if (CompareTypes(target_base))
    return carry_virtual;

  if (!carry_virtual) {
    uint32_t num_virtual_bases = GetNumVirtualBaseClasses();
    for (uint32_t i = 0; i < num_virtual_bases; ++i) {
      uint32_t bit_offset;
      auto base = GetVirtualBaseClassAtIndex(i, &bit_offset);
      if (base.IsVirtualBase(target_base, virtual_base,
                             /*carry_virtual*/ true)) {
        if (virtual_base)
          *virtual_base = base;

        return true;
      }
    }
  }

  uint32_t num_direct_bases = GetNumDirectBaseClasses();
  for (uint32_t i = 0; i < num_direct_bases; ++i) {
    uint32_t bit_offset;
    auto base = GetDirectBaseClassAtIndex(i, &bit_offset);
    if (base.IsVirtualBase(target_base, virtual_base, carry_virtual))
      return true;
  }

  return false;
}

bool CompilerType::IsContextuallyConvertibleToBool() const {
  return IsScalarType() || IsUnscopedEnumerationType() || IsPointerType() ||
         IsNullPtrType() || IsArrayType();
}

bool CompilerType::IsBasicType() const {
  return GetCanonicalType().GetBasicTypeEnumeration() !=
         lldb::eBasicTypeInvalid;
}

std::string CompilerType::TypeDescription() {
  auto name = GetTypeName();
  auto canonical_name = GetCanonicalType().GetTypeName();
  if (name.IsEmpty() || canonical_name.IsEmpty())
    return "''"; // Should not happen, unless the input is broken somehow.

  if (name == canonical_name)
    return llvm::formatv("'{0}'", name);

  return llvm::formatv("'{0}' (canonically referred to as '{1}')", name,
                       canonical_name);
}

bool CompilerType::CompareTypes(CompilerType rhs) const {
  if (*this == rhs)
    return true;

  const ConstString name = GetFullyUnqualifiedType().GetTypeName();
  const ConstString rhs_name = rhs.GetFullyUnqualifiedType().GetTypeName();
  return name == rhs_name;
}

const char *CompilerType::GetTypeTag() {
  switch (GetTypeClass()) {
  case lldb::eTypeClassClass:
    return "class";
  case lldb::eTypeClassEnumeration:
    return "enum";
  case lldb::eTypeClassStruct:
    return "struct";
  case lldb::eTypeClassUnion:
    return "union";
  default:
    return "unknown";
  }
  llvm_unreachable("All cases are covered by code above.");
}

uint32_t CompilerType::GetNumberOfNonEmptyBaseClasses() {
  uint32_t ret = 0;
  uint32_t num_direct_bases = GetNumDirectBaseClasses();

  for (uint32_t i = 0; i < num_direct_bases; ++i) {
    uint32_t bit_offset;
    CompilerType base_type = GetDirectBaseClassAtIndex(i, &bit_offset);
    if (base_type.GetNumFields() > 0 ||
        base_type.GetNumberOfNonEmptyBaseClasses() > 0)
      ret += 1;
  }
  return ret;
}

// Type Completion

bool CompilerType::GetCompleteType() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetCompleteType(m_type);
  return false;
}

// AST related queries
size_t CompilerType::GetPointerByteSize() const {
  if (auto type_system_sp = GetTypeSystem())
    return type_system_sp->GetPointerByteSize();
  return 0;
}

ConstString CompilerType::GetTypeName(bool BaseOnly) const {
  if (IsValid()) {
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetTypeName(m_type, BaseOnly);
  }
  return ConstString("<invalid>");
}

ConstString CompilerType::GetDisplayTypeName() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetDisplayTypeName(m_type);
  return ConstString("<invalid>");
}

uint32_t CompilerType::GetTypeInfo(
    CompilerType *pointee_or_element_compiler_type) const {
  if (IsValid())
  if (auto type_system_sp = GetTypeSystem())
    return type_system_sp->GetTypeInfo(m_type,
                                       pointee_or_element_compiler_type);
  return 0;
}

lldb::LanguageType CompilerType::GetMinimumLanguage() {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetMinimumLanguage(m_type);
  return lldb::eLanguageTypeC;
}

lldb::TypeClass CompilerType::GetTypeClass() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetTypeClass(m_type);
  return lldb::eTypeClassInvalid;
}

void CompilerType::SetCompilerType(lldb::TypeSystemWP type_system,
                                   lldb::opaque_compiler_type_t type) {
  m_type_system = type_system;
  m_type = type;
}

void CompilerType::SetCompilerType(CompilerType::TypeSystemSPWrapper type_system,
                                   lldb::opaque_compiler_type_t type) {
  m_type_system = type_system.GetSharedPointer();
  m_type = type;
}

unsigned CompilerType::GetTypeQualifiers() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetTypeQualifiers(m_type);
  return 0;
}

// Creating related types

CompilerType
CompilerType::GetArrayElementType(ExecutionContextScope *exe_scope) const {
  if (IsValid()) {
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetArrayElementType(m_type, exe_scope);
  }
  return CompilerType();
}

CompilerType CompilerType::GetArrayType(uint64_t size) const {
  if (IsValid()) {
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetArrayType(m_type, size);
  }
  return CompilerType();
}

CompilerType CompilerType::GetCanonicalType() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetCanonicalType(m_type);
  return CompilerType();
}

CompilerType CompilerType::GetFullyUnqualifiedType() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetFullyUnqualifiedType(m_type);
  return CompilerType();
}

CompilerType CompilerType::GetEnumerationIntegerType() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetEnumerationIntegerType(m_type);
  return CompilerType();
}

int CompilerType::GetFunctionArgumentCount() const {
  if (IsValid()) {
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetFunctionArgumentCount(m_type);
  }
  return -1;
}

CompilerType CompilerType::GetFunctionArgumentTypeAtIndex(size_t idx) const {
  if (IsValid()) {
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetFunctionArgumentTypeAtIndex(m_type, idx);
  }
  return CompilerType();
}

CompilerType CompilerType::GetFunctionReturnType() const {
  if (IsValid()) {
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetFunctionReturnType(m_type);
  }
  return CompilerType();
}

size_t CompilerType::GetNumMemberFunctions() const {
  if (IsValid()) {
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetNumMemberFunctions(m_type);
  }
  return 0;
}

TypeMemberFunctionImpl CompilerType::GetMemberFunctionAtIndex(size_t idx) {
  if (IsValid()) {
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetMemberFunctionAtIndex(m_type, idx);
  }
  return TypeMemberFunctionImpl();
}

CompilerType CompilerType::GetNonReferenceType() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetNonReferenceType(m_type);
  return CompilerType();
}

CompilerType CompilerType::GetPointeeType() const {
  if (IsValid()) {
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetPointeeType(m_type);
  }
  return CompilerType();
}

CompilerType CompilerType::GetPointerType() const {
  if (IsValid()) {
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetPointerType(m_type);
  }
  return CompilerType();
}

CompilerType CompilerType::AddPtrAuthModifier(uint32_t payload) const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->AddPtrAuthModifier(m_type, payload);
  return CompilerType();
}

CompilerType CompilerType::GetLValueReferenceType() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetLValueReferenceType(m_type);
  return CompilerType();
}

CompilerType CompilerType::GetRValueReferenceType() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetRValueReferenceType(m_type);
  return CompilerType();
}

CompilerType CompilerType::GetAtomicType() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetAtomicType(m_type);
  return CompilerType();
}

CompilerType CompilerType::AddConstModifier() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->AddConstModifier(m_type);
  return CompilerType();
}

CompilerType CompilerType::AddVolatileModifier() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->AddVolatileModifier(m_type);
  return CompilerType();
}

CompilerType CompilerType::AddRestrictModifier() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->AddRestrictModifier(m_type);
  return CompilerType();
}

CompilerType CompilerType::CreateTypedef(const char *name,
                                         const CompilerDeclContext &decl_ctx,
                                         uint32_t payload) const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->CreateTypedef(m_type, name, decl_ctx, payload);
  return CompilerType();
}

CompilerType CompilerType::GetTypedefedType() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetTypedefedType(m_type);
  return CompilerType();
}

// Create related types using the current type's AST

CompilerType
CompilerType::GetBasicTypeFromAST(lldb::BasicType basic_type) const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetBasicTypeFromAST(basic_type);
  return CompilerType();
}
// Exploring the type

std::optional<uint64_t>
CompilerType::GetBitSize(ExecutionContextScope *exe_scope) const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetBitSize(m_type, exe_scope);
  return {};
}

std::optional<uint64_t>
CompilerType::GetByteSize(ExecutionContextScope *exe_scope) const {
  if (std::optional<uint64_t> bit_size = GetBitSize(exe_scope))
    return (*bit_size + 7) / 8;
  return {};
}

std::optional<size_t>
CompilerType::GetTypeBitAlign(ExecutionContextScope *exe_scope) const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetTypeBitAlign(m_type, exe_scope);
  return {};
}

lldb::Encoding CompilerType::GetEncoding(uint64_t &count) const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetEncoding(m_type, count);
  return lldb::eEncodingInvalid;
}

lldb::Format CompilerType::GetFormat() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetFormat(m_type);
  return lldb::eFormatDefault;
}

llvm::Expected<uint32_t>
CompilerType::GetNumChildren(bool omit_empty_base_classes,
                             const ExecutionContext *exe_ctx) const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetNumChildren(m_type, omit_empty_base_classes,
                                       exe_ctx);
  return llvm::createStringError("invalid type");
}

lldb::BasicType CompilerType::GetBasicTypeEnumeration() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetBasicTypeEnumeration(m_type);
  return eBasicTypeInvalid;
}

void CompilerType::ForEachEnumerator(
    std::function<bool(const CompilerType &integer_type,
                       ConstString name,
                       const llvm::APSInt &value)> const &callback) const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->ForEachEnumerator(m_type, callback);
}

uint32_t CompilerType::GetNumFields() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetNumFields(m_type);
  return 0;
}

CompilerType CompilerType::GetFieldAtIndex(size_t idx, std::string &name,
                                           uint64_t *bit_offset_ptr,
                                           uint32_t *bitfield_bit_size_ptr,
                                           bool *is_bitfield_ptr) const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetFieldAtIndex(m_type, idx, name, bit_offset_ptr,
                                        bitfield_bit_size_ptr, is_bitfield_ptr);
  return CompilerType();
}

uint32_t CompilerType::GetNumDirectBaseClasses() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetNumDirectBaseClasses(m_type);
  return 0;
}

uint32_t CompilerType::GetNumVirtualBaseClasses() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetNumVirtualBaseClasses(m_type);
  return 0;
}

CompilerType
CompilerType::GetDirectBaseClassAtIndex(size_t idx,
                                        uint32_t *bit_offset_ptr) const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetDirectBaseClassAtIndex(m_type, idx,
                                                    bit_offset_ptr);
  return CompilerType();
}

CompilerType
CompilerType::GetVirtualBaseClassAtIndex(size_t idx,
                                         uint32_t *bit_offset_ptr) const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetVirtualBaseClassAtIndex(m_type, idx,
                                                     bit_offset_ptr);
  return CompilerType();
}

CompilerDecl CompilerType::GetStaticFieldWithName(llvm::StringRef name) const {
  if (IsValid())
    return GetTypeSystem()->GetStaticFieldWithName(m_type, name);
  return CompilerDecl();
}

uint32_t CompilerType::GetIndexOfFieldWithName(
    const char *name, CompilerType *field_compiler_type_ptr,
    uint64_t *bit_offset_ptr, uint32_t *bitfield_bit_size_ptr,
    bool *is_bitfield_ptr) const {
  unsigned count = GetNumFields();
  std::string field_name;
  for (unsigned index = 0; index < count; index++) {
    CompilerType field_compiler_type(
        GetFieldAtIndex(index, field_name, bit_offset_ptr,
                        bitfield_bit_size_ptr, is_bitfield_ptr));
    if (strcmp(field_name.c_str(), name) == 0) {
      if (field_compiler_type_ptr)
        *field_compiler_type_ptr = field_compiler_type;
      return index;
    }
  }
  return UINT32_MAX;
}

llvm::Expected<CompilerType> CompilerType::GetChildCompilerTypeAtIndex(
    ExecutionContext *exe_ctx, size_t idx, bool transparent_pointers,
    bool omit_empty_base_classes, bool ignore_array_bounds,
    std::string &child_name, uint32_t &child_byte_size,
    int32_t &child_byte_offset, uint32_t &child_bitfield_bit_size,
    uint32_t &child_bitfield_bit_offset, bool &child_is_base_class,
    bool &child_is_deref_of_parent, ValueObject *valobj,
    uint64_t &language_flags) const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetChildCompilerTypeAtIndex(
          m_type, exe_ctx, idx, transparent_pointers, omit_empty_base_classes,
          ignore_array_bounds, child_name, child_byte_size, child_byte_offset,
          child_bitfield_bit_size, child_bitfield_bit_offset,
          child_is_base_class, child_is_deref_of_parent, valobj,
          language_flags);
  return CompilerType();
}

// Look for a child member (doesn't include base classes, but it does include
// their members) in the type hierarchy. Returns an index path into
// "clang_type" on how to reach the appropriate member.
//
//    class A
//    {
//    public:
//        int m_a;
//        int m_b;
//    };
//
//    class B
//    {
//    };
//
//    class C :
//        public B,
//        public A
//    {
//    };
//
// If we have a clang type that describes "class C", and we wanted to looked
// "m_b" in it:
//
// With omit_empty_base_classes == false we would get an integer array back
// with: { 1,  1 } The first index 1 is the child index for "class A" within
// class C The second index 1 is the child index for "m_b" within class A
//
// With omit_empty_base_classes == true we would get an integer array back
// with: { 0,  1 } The first index 0 is the child index for "class A" within
// class C (since class B doesn't have any members it doesn't count) The second
// index 1 is the child index for "m_b" within class A

size_t CompilerType::GetIndexOfChildMemberWithName(
    llvm::StringRef name, bool omit_empty_base_classes,
    std::vector<uint32_t> &child_indexes) const {
  if (IsValid() && !name.empty()) {
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetIndexOfChildMemberWithName(
        m_type, name, omit_empty_base_classes, child_indexes);
  }
  return 0;
}

CompilerType
CompilerType::GetDirectNestedTypeWithName(llvm::StringRef name) const {
  if (IsValid() && !name.empty()) {
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetDirectNestedTypeWithName(m_type, name);
  }
  return CompilerType();
}

size_t CompilerType::GetNumTemplateArguments(bool expand_pack) const {
  if (IsValid()) {
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetNumTemplateArguments(m_type, expand_pack);
  }
  return 0;
}

TemplateArgumentKind
CompilerType::GetTemplateArgumentKind(size_t idx, bool expand_pack) const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetTemplateArgumentKind(m_type, idx, expand_pack);
  return eTemplateArgumentKindNull;
}

CompilerType CompilerType::GetTypeTemplateArgument(size_t idx,
                                                   bool expand_pack) const {
  if (IsValid()) {
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetTypeTemplateArgument(m_type, idx, expand_pack);
  }
  return CompilerType();
}

std::optional<CompilerType::IntegralTemplateArgument>
CompilerType::GetIntegralTemplateArgument(size_t idx, bool expand_pack) const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetIntegralTemplateArgument(m_type, idx, expand_pack);
  return std::nullopt;
}

CompilerType CompilerType::GetTypeForFormatters() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetTypeForFormatters(m_type);
  return CompilerType();
}

LazyBool CompilerType::ShouldPrintAsOneLiner(ValueObject *valobj) const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->ShouldPrintAsOneLiner(m_type, valobj);
  return eLazyBoolCalculate;
}

bool CompilerType::IsMeaninglessWithoutDynamicResolution() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->IsMeaninglessWithoutDynamicResolution(m_type);
  return false;
}

// Get the index of the child of "clang_type" whose name matches. This function
// doesn't descend into the children, but only looks one level deep and name
// matches can include base class names.

uint32_t
CompilerType::GetIndexOfChildWithName(llvm::StringRef name,
                                      bool omit_empty_base_classes) const {
  if (IsValid() && !name.empty()) {
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->GetIndexOfChildWithName(m_type, name,
                                                     omit_empty_base_classes);
  }
  return UINT32_MAX;
}

// Dumping types

bool CompilerType::DumpTypeValue(Stream *s, lldb::Format format,
                                 const DataExtractor &data,
                                 lldb::offset_t byte_offset, size_t byte_size,
                                 uint32_t bitfield_bit_size,
                                 uint32_t bitfield_bit_offset,
                                 ExecutionContextScope *exe_scope) {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->DumpTypeValue(
          m_type, *s, format, data, byte_offset, byte_size, bitfield_bit_size,
          bitfield_bit_offset, exe_scope);
  return false;
}

void CompilerType::DumpTypeDescription(lldb::DescriptionLevel level) const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      type_system_sp->DumpTypeDescription(m_type, level);
}

void CompilerType::DumpTypeDescription(Stream *s,
                                       lldb::DescriptionLevel level) const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      type_system_sp->DumpTypeDescription(m_type, *s, level);
}

#ifndef NDEBUG
LLVM_DUMP_METHOD void CompilerType::dump() const {
  if (IsValid())
    if (auto type_system_sp = GetTypeSystem())
      return type_system_sp->dump(m_type);
  llvm::errs() << "<invalid>\n";
}
#endif

bool CompilerType::GetValueAsScalar(const lldb_private::DataExtractor &data,
                                    lldb::offset_t data_byte_offset,
                                    size_t data_byte_size, Scalar &value,
                                    ExecutionContextScope *exe_scope) const {
  if (!IsValid())
    return false;

  if (IsAggregateType()) {
    return false; // Aggregate types don't have scalar values
  } else {
    uint64_t count = 0;
    lldb::Encoding encoding = GetEncoding(count);

    if (encoding == lldb::eEncodingInvalid || count != 1)
      return false;

    std::optional<uint64_t> byte_size = GetByteSize(exe_scope);
    if (!byte_size)
      return false;
    lldb::offset_t offset = data_byte_offset;
    switch (encoding) {
    case lldb::eEncodingInvalid:
      break;
    case lldb::eEncodingVector:
      break;
    case lldb::eEncodingUint:
      if (*byte_size <= sizeof(unsigned long long)) {
        uint64_t uval64 = data.GetMaxU64(&offset, *byte_size);
        if (*byte_size <= sizeof(unsigned int)) {
          value = (unsigned int)uval64;
          return true;
        } else if (*byte_size <= sizeof(unsigned long)) {
          value = (unsigned long)uval64;
          return true;
        } else if (*byte_size <= sizeof(unsigned long long)) {
          value = (unsigned long long)uval64;
          return true;
        } else
          value.Clear();
      }
      break;

    case lldb::eEncodingSint:
      if (*byte_size <= sizeof(long long)) {
        int64_t sval64 = data.GetMaxS64(&offset, *byte_size);
        if (*byte_size <= sizeof(int)) {
          value = (int)sval64;
          return true;
        } else if (*byte_size <= sizeof(long)) {
          value = (long)sval64;
          return true;
        } else if (*byte_size <= sizeof(long long)) {
          value = (long long)sval64;
          return true;
        } else
          value.Clear();
      }
      break;

    case lldb::eEncodingIEEE754:
      if (*byte_size <= sizeof(long double)) {
        uint32_t u32;
        uint64_t u64;
        if (*byte_size == sizeof(float)) {
          if (sizeof(float) == sizeof(uint32_t)) {
            u32 = data.GetU32(&offset);
            value = *((float *)&u32);
            return true;
          } else if (sizeof(float) == sizeof(uint64_t)) {
            u64 = data.GetU64(&offset);
            value = *((float *)&u64);
            return true;
          }
        } else if (*byte_size == sizeof(double)) {
          if (sizeof(double) == sizeof(uint32_t)) {
            u32 = data.GetU32(&offset);
            value = *((double *)&u32);
            return true;
          } else if (sizeof(double) == sizeof(uint64_t)) {
            u64 = data.GetU64(&offset);
            value = *((double *)&u64);
            return true;
          }
        } else if (*byte_size == sizeof(long double)) {
          if (sizeof(long double) == sizeof(uint32_t)) {
            u32 = data.GetU32(&offset);
            value = *((long double *)&u32);
            return true;
          } else if (sizeof(long double) == sizeof(uint64_t)) {
            u64 = data.GetU64(&offset);
            value = *((long double *)&u64);
            return true;
          }
        }
      }
      break;
    }
  }
  return false;
}

CompilerType::CompilerType(CompilerType::TypeSystemSPWrapper type_system,
                           lldb::opaque_compiler_type_t type)
    : m_type_system(type_system.GetSharedPointer()), m_type(type) {
  assert(Verify() && "verification failed");
}

CompilerType::CompilerType(lldb::TypeSystemWP type_system,
                           lldb::opaque_compiler_type_t type)
    : m_type_system(type_system), m_type(type) {
  assert(Verify() && "verification failed");
}

#ifndef NDEBUG
bool CompilerType::Verify() const {
  if (!IsValid())
    return true;
  if (auto type_system_sp = GetTypeSystem())
    return type_system_sp->Verify(m_type);
  return true;
}
#endif

CompilerType::TypeSystemSPWrapper CompilerType::GetTypeSystem() const {
  return {m_type_system.lock()};
}

bool CompilerType::TypeSystemSPWrapper::operator==(
    const CompilerType::TypeSystemSPWrapper &other) const {
  if (!m_typesystem_sp && !other.m_typesystem_sp)
    return true;
  if (m_typesystem_sp && other.m_typesystem_sp)
    return m_typesystem_sp.get() == other.m_typesystem_sp.get();
  return false;
}

TypeSystem *CompilerType::TypeSystemSPWrapper::operator->() const {
  assert(m_typesystem_sp);
  return m_typesystem_sp.get();
}

bool lldb_private::operator==(const lldb_private::CompilerType &lhs,
                              const lldb_private::CompilerType &rhs) {
  return lhs.GetTypeSystem() == rhs.GetTypeSystem() &&
         lhs.GetOpaqueQualType() == rhs.GetOpaqueQualType();
}

bool lldb_private::operator!=(const lldb_private::CompilerType &lhs,
                              const lldb_private::CompilerType &rhs) {
  return !(lhs == rhs);
}

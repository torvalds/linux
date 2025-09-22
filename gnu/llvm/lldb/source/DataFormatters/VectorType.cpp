//===-- VectorType.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/DataFormatters/VectorType.h"

#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Target/Target.h"

#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/Log.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

static CompilerType GetCompilerTypeForFormat(lldb::Format format,
                                             CompilerType element_type,
                                             TypeSystemSP type_system) {
  lldbassert(type_system && "type_system needs to be not NULL");
  if (!type_system)
    return {};

  switch (format) {
  case lldb::eFormatAddressInfo:
  case lldb::eFormatPointer:
    return type_system->GetBuiltinTypeForEncodingAndBitSize(
        eEncodingUint, 8 * type_system->GetPointerByteSize());

  case lldb::eFormatBoolean:
    return type_system->GetBasicTypeFromAST(lldb::eBasicTypeBool);

  case lldb::eFormatBytes:
  case lldb::eFormatBytesWithASCII:
  case lldb::eFormatChar:
  case lldb::eFormatCharArray:
  case lldb::eFormatCharPrintable:
    return type_system->GetBasicTypeFromAST(lldb::eBasicTypeChar);

  case lldb::eFormatComplex /* lldb::eFormatComplexFloat */:
    return type_system->GetBasicTypeFromAST(lldb::eBasicTypeFloatComplex);

  case lldb::eFormatCString:
    return type_system->GetBasicTypeFromAST(lldb::eBasicTypeChar)
        .GetPointerType();

  case lldb::eFormatFloat:
    return type_system->GetBasicTypeFromAST(lldb::eBasicTypeFloat);

  case lldb::eFormatHex:
  case lldb::eFormatHexUppercase:
  case lldb::eFormatOctal:
    return type_system->GetBasicTypeFromAST(lldb::eBasicTypeInt);

  case lldb::eFormatHexFloat:
    return type_system->GetBasicTypeFromAST(lldb::eBasicTypeFloat);

  case lldb::eFormatUnicode16:
  case lldb::eFormatUnicode32:

  case lldb::eFormatUnsigned:
    return type_system->GetBasicTypeFromAST(lldb::eBasicTypeUnsignedInt);

  case lldb::eFormatVectorOfChar:
    return type_system->GetBasicTypeFromAST(lldb::eBasicTypeChar);

  case lldb::eFormatVectorOfFloat32:
    return type_system->GetBuiltinTypeForEncodingAndBitSize(eEncodingIEEE754,
                                                            32);

  case lldb::eFormatVectorOfFloat64:
    return type_system->GetBuiltinTypeForEncodingAndBitSize(eEncodingIEEE754,
                                                            64);

  case lldb::eFormatVectorOfSInt16:
    return type_system->GetBuiltinTypeForEncodingAndBitSize(eEncodingSint, 16);

  case lldb::eFormatVectorOfSInt32:
    return type_system->GetBuiltinTypeForEncodingAndBitSize(eEncodingSint, 32);

  case lldb::eFormatVectorOfSInt64:
    return type_system->GetBuiltinTypeForEncodingAndBitSize(eEncodingSint, 64);

  case lldb::eFormatVectorOfSInt8:
    return type_system->GetBuiltinTypeForEncodingAndBitSize(eEncodingSint, 8);

  case lldb::eFormatVectorOfUInt128:
    return type_system->GetBuiltinTypeForEncodingAndBitSize(eEncodingUint, 128);

  case lldb::eFormatVectorOfUInt16:
    return type_system->GetBuiltinTypeForEncodingAndBitSize(eEncodingUint, 16);

  case lldb::eFormatVectorOfUInt32:
    return type_system->GetBuiltinTypeForEncodingAndBitSize(eEncodingUint, 32);

  case lldb::eFormatVectorOfUInt64:
    return type_system->GetBuiltinTypeForEncodingAndBitSize(eEncodingUint, 64);

  case lldb::eFormatVectorOfUInt8:
    return type_system->GetBuiltinTypeForEncodingAndBitSize(eEncodingUint, 8);

  case lldb::eFormatDefault:
    return element_type;

  case lldb::eFormatBinary:
  case lldb::eFormatComplexInteger:
  case lldb::eFormatDecimal:
  case lldb::eFormatEnum:
  case lldb::eFormatInstruction:
  case lldb::eFormatOSType:
  case lldb::eFormatVoid:
  default:
    return type_system->GetBuiltinTypeForEncodingAndBitSize(eEncodingUint, 8);
  }
}

static lldb::Format GetItemFormatForFormat(lldb::Format format,
                                           CompilerType element_type) {
  switch (format) {
  case lldb::eFormatVectorOfChar:
    return lldb::eFormatChar;

  case lldb::eFormatVectorOfFloat32:
  case lldb::eFormatVectorOfFloat64:
    return lldb::eFormatFloat;

  case lldb::eFormatVectorOfSInt16:
  case lldb::eFormatVectorOfSInt32:
  case lldb::eFormatVectorOfSInt64:
  case lldb::eFormatVectorOfSInt8:
    return lldb::eFormatDecimal;

  case lldb::eFormatVectorOfUInt128:
  case lldb::eFormatVectorOfUInt16:
  case lldb::eFormatVectorOfUInt32:
  case lldb::eFormatVectorOfUInt64:
  case lldb::eFormatVectorOfUInt8:
    return lldb::eFormatUnsigned;

  case lldb::eFormatBinary:
  case lldb::eFormatComplexInteger:
  case lldb::eFormatDecimal:
  case lldb::eFormatEnum:
  case lldb::eFormatInstruction:
  case lldb::eFormatOSType:
  case lldb::eFormatVoid:
    return eFormatHex;

  case lldb::eFormatDefault: {
    // special case the (default, char) combination to actually display as an
    // integer value most often, you won't want to see the ASCII characters...
    // (and if you do, eFormatChar is a keystroke away)
    bool is_char = element_type.IsCharType();
    bool is_signed = false;
    element_type.IsIntegerType(is_signed);
    return is_char ? (is_signed ? lldb::eFormatDecimal : eFormatHex) : format;
  } break;

  default:
    return format;
  }
}

/// Calculates the number of elements stored in a container (with
/// element type 'container_elem_type') as if it had elements of type
/// 'element_type'.
///
/// For example, a container of type
/// `uint8_t __attribute__((vector_size(16)))` has 16 elements.
/// But calling `CalculateNumChildren` with an 'element_type'
/// of `float` (4-bytes) will return `4` because we are interpreting
/// the byte-array as a `float32[]`.
///
/// \param[in] container_elem_type The type of the elements stored
/// in the container we are calculating the children of.
///
/// \param[in] num_elements Number of 'container_elem_type's our
/// container stores.
///
/// \param[in] element_type The type of elements we interpret
/// container_type to contain for the purposes of calculating
/// the number of children.
///
/// \returns The number of elements stored in a container of
/// type 'element_type'. Returns a std::nullopt if the
/// size of the container is not a multiple of 'element_type'
/// or if an error occurs.
static std::optional<size_t>
CalculateNumChildren(CompilerType container_elem_type, uint64_t num_elements,
                     CompilerType element_type) {
  std::optional<uint64_t> container_elem_size =
      container_elem_type.GetByteSize(/* exe_scope */ nullptr);
  if (!container_elem_size)
    return {};

  auto container_size = *container_elem_size * num_elements;

  std::optional<uint64_t> element_size =
      element_type.GetByteSize(/* exe_scope */ nullptr);
  if (!element_size || !*element_size)
    return {};

  if (container_size % *element_size)
    return {};

  return container_size / *element_size;
}

namespace lldb_private {
namespace formatters {

class VectorTypeSyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  VectorTypeSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
      : SyntheticChildrenFrontEnd(*valobj_sp), m_child_type() {}

  ~VectorTypeSyntheticFrontEnd() override = default;

  llvm::Expected<uint32_t> CalculateNumChildren() override {
    return m_num_children;
  }

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override {
    auto num_children_or_err = CalculateNumChildren();
    if (!num_children_or_err)
      return ValueObjectConstResult::Create(
          nullptr, Status(num_children_or_err.takeError()));
    if (idx >= *num_children_or_err)
      return {};
    std::optional<uint64_t> size = m_child_type.GetByteSize(nullptr);
    if (!size)
      return {};
    auto offset = idx * *size;
    StreamString idx_name;
    idx_name.Printf("[%" PRIu64 "]", (uint64_t)idx);
    ValueObjectSP child_sp(m_backend.GetSyntheticChildAtOffset(
        offset, m_child_type, true, ConstString(idx_name.GetString())));
    if (!child_sp)
      return child_sp;

    child_sp->SetFormat(m_item_format);

    return child_sp;
  }

  lldb::ChildCacheState Update() override {
    m_parent_format = m_backend.GetFormat();
    CompilerType parent_type(m_backend.GetCompilerType());
    CompilerType element_type;
    uint64_t num_elements;
    parent_type.IsVectorType(&element_type, &num_elements);
    m_child_type = ::GetCompilerTypeForFormat(
        m_parent_format, element_type,
        parent_type.GetTypeSystem().GetSharedPointer());
    m_num_children =
        ::CalculateNumChildren(element_type, num_elements, m_child_type)
            .value_or(0);
    m_item_format = GetItemFormatForFormat(m_parent_format, m_child_type);
    return lldb::ChildCacheState::eRefetch;
  }

  bool MightHaveChildren() override { return true; }

  size_t GetIndexOfChildWithName(ConstString name) override {
    const char *item_name = name.GetCString();
    uint32_t idx = ExtractIndexFromString(item_name);
    if (idx < UINT32_MAX && idx >= CalculateNumChildrenIgnoringErrors())
      return UINT32_MAX;
    return idx;
  }

private:
  lldb::Format m_parent_format = eFormatInvalid;
  lldb::Format m_item_format = eFormatInvalid;
  CompilerType m_child_type;
  size_t m_num_children = 0;
};

} // namespace formatters
} // namespace lldb_private

bool lldb_private::formatters::VectorTypeSummaryProvider(
    ValueObject &valobj, Stream &s, const TypeSummaryOptions &) {
  auto synthetic_children =
      VectorTypeSyntheticFrontEndCreator(nullptr, valobj.GetSP());
  if (!synthetic_children)
    return false;

  synthetic_children->Update();

  s.PutChar('(');
  bool first = true;

  size_t idx = 0,
         len = synthetic_children->CalculateNumChildrenIgnoringErrors();

  for (; idx < len; idx++) {
    auto child_sp = synthetic_children->GetChildAtIndex(idx);
    if (!child_sp)
      continue;
    child_sp = child_sp->GetQualifiedRepresentationIfAvailable(
        lldb::eDynamicDontRunTarget, true);

    const char *child_value = child_sp->GetValueAsCString();
    if (child_value && *child_value) {
      if (first) {
        s.Printf("%s", child_value);
        first = false;
      } else {
        s.Printf(", %s", child_value);
      }
    }
  }

  s.PutChar(')');

  return true;
}

lldb_private::SyntheticChildrenFrontEnd *
lldb_private::formatters::VectorTypeSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  if (!valobj_sp)
    return nullptr;
  return new VectorTypeSyntheticFrontEnd(valobj_sp);
}

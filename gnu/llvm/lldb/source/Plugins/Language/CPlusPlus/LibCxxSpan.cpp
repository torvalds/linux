//===-- LibCxxSpan.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LibCxx.h"

#include "lldb/Core/ValueObject.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/Utility/ConstString.h"
#include "llvm/ADT/APSInt.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

namespace lldb_private {
namespace formatters {

class LibcxxStdSpanSyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  LibcxxStdSpanSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  ~LibcxxStdSpanSyntheticFrontEnd() override = default;

  llvm::Expected<uint32_t> CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override;

  /// Determines properties of the std::span<> associated with this object
  //
  // std::span can either be instantiated with a compile-time known
  // extent or a std::dynamic_extent (this is the default if only the
  // type template argument is provided). The layout of std::span
  // depends on whether the extent is dynamic or not. For static
  // extents (e.g., std::span<int, 9>):
  //
  // (std::__1::span<const int, 9>) s = {
  //   __data = 0x000000016fdff494
  // }
  //
  // For dynamic extents, e.g., std::span<int>, the layout is:
  //
  // (std::__1::span<const int, 18446744073709551615>) s = {
  //   __data = 0x000000016fdff494
  //   __size = 6
  // }
  //
  // This function checks for a '__size' member to determine the number
  // of elements in the span. If no such member exists, we get the size
  // from the only other place it can be: the template argument.
  lldb::ChildCacheState Update() override;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(ConstString name) override;

private:
  ValueObject *m_start = nullptr; ///< First element of span. Held, not owned.
  CompilerType m_element_type{};  ///< Type of span elements.
  size_t m_num_elements = 0;      ///< Number of elements in span.
  uint32_t m_element_size = 0;    ///< Size in bytes of each span element.
};

lldb_private::formatters::LibcxxStdSpanSyntheticFrontEnd::
    LibcxxStdSpanSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp) {
  if (valobj_sp)
    Update();
}

llvm::Expected<uint32_t> lldb_private::formatters::
    LibcxxStdSpanSyntheticFrontEnd::CalculateNumChildren() {
  return m_num_elements;
}

lldb::ValueObjectSP
lldb_private::formatters::LibcxxStdSpanSyntheticFrontEnd::GetChildAtIndex(
    uint32_t idx) {
  if (!m_start)
    return {};

  uint64_t offset = idx * m_element_size;
  offset = offset + m_start->GetValueAsUnsigned(0);
  StreamString name;
  name.Printf("[%" PRIu64 "]", (uint64_t)idx);
  return CreateValueObjectFromAddress(name.GetString(), offset,
                                      m_backend.GetExecutionContextRef(),
                                      m_element_type);
}

lldb::ChildCacheState
lldb_private::formatters::LibcxxStdSpanSyntheticFrontEnd::Update() {
  // Get element type.
  ValueObjectSP data_type_finder_sp = GetChildMemberWithName(
      m_backend, {ConstString("__data_"), ConstString("__data")});
  if (!data_type_finder_sp)
    return lldb::ChildCacheState::eRefetch;

  m_element_type = data_type_finder_sp->GetCompilerType().GetPointeeType();

  // Get element size.
  if (std::optional<uint64_t> size = m_element_type.GetByteSize(nullptr)) {
    m_element_size = *size;

    // Get data.
    if (m_element_size > 0) {
      m_start = data_type_finder_sp.get();
    }

    // Get number of elements.
    if (auto size_sp = GetChildMemberWithName(
            m_backend, {ConstString("__size_"), ConstString("__size")})) {
      m_num_elements = size_sp->GetValueAsUnsigned(0);
    } else if (auto arg =
                   m_backend.GetCompilerType().GetIntegralTemplateArgument(1)) {

      m_num_elements = arg->value.getLimitedValue();
    }
  }

  return lldb::ChildCacheState::eReuse;
}

bool lldb_private::formatters::LibcxxStdSpanSyntheticFrontEnd::
    MightHaveChildren() {
  return true;
}

size_t lldb_private::formatters::LibcxxStdSpanSyntheticFrontEnd::
    GetIndexOfChildWithName(ConstString name) {
  if (!m_start)
    return UINT32_MAX;
  return ExtractIndexFromString(name.GetCString());
}

lldb_private::SyntheticChildrenFrontEnd *
LibcxxStdSpanSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                      lldb::ValueObjectSP valobj_sp) {
  if (!valobj_sp)
    return nullptr;
  CompilerType type = valobj_sp->GetCompilerType();
  if (!type.IsValid() || type.GetNumTemplateArguments() != 2)
    return nullptr;
  return new LibcxxStdSpanSyntheticFrontEnd(valobj_sp);
}

} // namespace formatters
} // namespace lldb_private

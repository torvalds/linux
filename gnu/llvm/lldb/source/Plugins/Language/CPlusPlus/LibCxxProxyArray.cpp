//===-- LibCxxProxyArray.cpp-----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LibCxx.h"

#include "lldb/Core/ValueObject.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

namespace lldb_private {
namespace formatters {

/// Data formatter for libc++'s std::"proxy_array".
///
/// A proxy_array's are created by using:
///   std::gslice_array   operator[](const std::gslice& gslicearr);
///   std::mask_array     operator[](const std::valarray<bool>& boolarr);
///   std::indirect_array operator[](const std::valarray<std::size_t>& indarr);
///
/// These arrays have the following members:
/// - __vp_ points to std::valarray::__begin_
/// - __1d_ an array of offsets of the elements from @a __vp_
class LibcxxStdProxyArraySyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  LibcxxStdProxyArraySyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  ~LibcxxStdProxyArraySyntheticFrontEnd() override;

  llvm::Expected<uint32_t> CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override;

  lldb::ChildCacheState Update() override;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(ConstString name) override;

private:
  /// A non-owning pointer to the array's __vp_.
  ValueObject *m_base = nullptr;
  /// The type of the array's template argument T.
  CompilerType m_element_type;
  /// The sizeof the array's template argument T.
  uint32_t m_element_size = 0;

  /// A non-owning pointer to the array's __1d_.__begin_.
  ValueObject *m_start = nullptr;
  /// A non-owning pointer to the array's __1d_.__end_.
  ValueObject *m_finish = nullptr;
  /// The type of the __1d_ array's template argument T (size_t).
  CompilerType m_element_type_size_t;
  /// The sizeof the __1d_ array's template argument T (size_t)
  uint32_t m_element_size_size_t = 0;
};

} // namespace formatters
} // namespace lldb_private

lldb_private::formatters::LibcxxStdProxyArraySyntheticFrontEnd::
    LibcxxStdProxyArraySyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp), m_element_type() {
  if (valobj_sp)
    Update();
}

lldb_private::formatters::LibcxxStdProxyArraySyntheticFrontEnd::
    ~LibcxxStdProxyArraySyntheticFrontEnd() {
  // these need to stay around because they are child objects who will follow
  // their parent's life cycle
  // delete m_base;
}

llvm::Expected<uint32_t> lldb_private::formatters::
    LibcxxStdProxyArraySyntheticFrontEnd::CalculateNumChildren() {

  if (!m_start || !m_finish)
    return 0;
  uint64_t start_val = m_start->GetValueAsUnsigned(0);
  uint64_t finish_val = m_finish->GetValueAsUnsigned(0);

  if (start_val == 0 || finish_val == 0)
    return 0;

  if (start_val >= finish_val)
    return 0;

  size_t num_children = (finish_val - start_val);
  if (num_children % m_element_size_size_t)
    return 0;
  return num_children / m_element_size_size_t;
}

lldb::ValueObjectSP
lldb_private::formatters::LibcxxStdProxyArraySyntheticFrontEnd::GetChildAtIndex(
    uint32_t idx) {
  if (!m_base)
    return lldb::ValueObjectSP();

  uint64_t offset = idx * m_element_size_size_t;
  offset = offset + m_start->GetValueAsUnsigned(0);

  lldb::ValueObjectSP indirect = CreateValueObjectFromAddress(
      "", offset, m_backend.GetExecutionContextRef(), m_element_type_size_t);
  if (!indirect)
    return lldb::ValueObjectSP();

  const size_t value = indirect->GetValueAsUnsigned(0);
  if (!value)
    return lldb::ValueObjectSP();

  offset = value * m_element_size;
  offset = offset + m_base->GetValueAsUnsigned(0);

  StreamString name;
  name.Printf("[%" PRIu64 "] -> [%zu]", (uint64_t)idx, value);
  return CreateValueObjectFromAddress(name.GetString(), offset,
                                      m_backend.GetExecutionContextRef(),
                                      m_element_type);
}

lldb::ChildCacheState
lldb_private::formatters::LibcxxStdProxyArraySyntheticFrontEnd::Update() {
  m_base = nullptr;
  m_start = nullptr;
  m_finish = nullptr;

  CompilerType type = m_backend.GetCompilerType();
  if (type.GetNumTemplateArguments() == 0)
    return ChildCacheState::eRefetch;

  m_element_type = type.GetTypeTemplateArgument(0);
  if (std::optional<uint64_t> size = m_element_type.GetByteSize(nullptr))
    m_element_size = *size;

  if (m_element_size == 0)
    return ChildCacheState::eRefetch;

  ValueObjectSP vector = m_backend.GetChildMemberWithName("__1d_");
  if (!vector)
    return ChildCacheState::eRefetch;

  type = vector->GetCompilerType();
  if (type.GetNumTemplateArguments() == 0)
    return ChildCacheState::eRefetch;

  m_element_type_size_t = type.GetTypeTemplateArgument(0);
  if (std::optional<uint64_t> size = m_element_type_size_t.GetByteSize(nullptr))
    m_element_size_size_t = *size;

  if (m_element_size_size_t == 0)
    return ChildCacheState::eRefetch;

  ValueObjectSP base = m_backend.GetChildMemberWithName("__vp_");
  ValueObjectSP start = vector->GetChildMemberWithName("__begin_");
  ValueObjectSP finish = vector->GetChildMemberWithName("__end_");
  if (!base || !start || !finish)
    return ChildCacheState::eRefetch;

  m_base = base.get();
  m_start = start.get();
  m_finish = finish.get();

  return ChildCacheState::eRefetch;
}

bool lldb_private::formatters::LibcxxStdProxyArraySyntheticFrontEnd::
    MightHaveChildren() {
  return true;
}

size_t lldb_private::formatters::LibcxxStdProxyArraySyntheticFrontEnd::
    GetIndexOfChildWithName(ConstString name) {
  if (!m_base)
    return std::numeric_limits<size_t>::max();
  return ExtractIndexFromString(name.GetCString());
}

lldb_private::SyntheticChildrenFrontEnd *
lldb_private::formatters::LibcxxStdProxyArraySyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  if (!valobj_sp)
    return nullptr;
  return new LibcxxStdProxyArraySyntheticFrontEnd(valobj_sp);
}

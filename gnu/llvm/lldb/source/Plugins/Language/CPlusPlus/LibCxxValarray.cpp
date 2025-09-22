//===-- LibCxxValarray.cpp ------------------------------------------------===//
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
class LibcxxStdValarraySyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  LibcxxStdValarraySyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  ~LibcxxStdValarraySyntheticFrontEnd() override;

  llvm::Expected<uint32_t> CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override;

  lldb::ChildCacheState Update() override;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(ConstString name) override;

private:
  /// A non-owning pointer to valarray's __begin_ member.
  ValueObject *m_start = nullptr;
  /// A non-owning pointer to valarray's __end_ member.
  ValueObject *m_finish = nullptr;
  /// The type of valarray's template argument T.
  CompilerType m_element_type;
  /// The sizeof valarray's template argument T.
  uint32_t m_element_size = 0;
};

} // namespace formatters
} // namespace lldb_private

lldb_private::formatters::LibcxxStdValarraySyntheticFrontEnd::
    LibcxxStdValarraySyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp), m_element_type() {
  if (valobj_sp)
    Update();
}

lldb_private::formatters::LibcxxStdValarraySyntheticFrontEnd::
    ~LibcxxStdValarraySyntheticFrontEnd() {
  // these need to stay around because they are child objects who will follow
  // their parent's life cycle
  // delete m_start;
  // delete m_finish;
}

llvm::Expected<uint32_t> lldb_private::formatters::
    LibcxxStdValarraySyntheticFrontEnd::CalculateNumChildren() {
  if (!m_start || !m_finish)
    return 0;
  uint64_t start_val = m_start->GetValueAsUnsigned(0);
  uint64_t finish_val = m_finish->GetValueAsUnsigned(0);

  if (start_val == 0 || finish_val == 0)
    return 0;

  if (start_val >= finish_val)
    return 0;

  size_t num_children = (finish_val - start_val);
  if (num_children % m_element_size)
    return 0;
  return num_children / m_element_size;
}

lldb::ValueObjectSP
lldb_private::formatters::LibcxxStdValarraySyntheticFrontEnd::GetChildAtIndex(
    uint32_t idx) {
  if (!m_start || !m_finish)
    return lldb::ValueObjectSP();

  uint64_t offset = idx * m_element_size;
  offset = offset + m_start->GetValueAsUnsigned(0);
  StreamString name;
  name.Printf("[%" PRIu64 "]", (uint64_t)idx);
  return CreateValueObjectFromAddress(name.GetString(), offset,
                                      m_backend.GetExecutionContextRef(),
                                      m_element_type);
}

lldb::ChildCacheState
lldb_private::formatters::LibcxxStdValarraySyntheticFrontEnd::Update() {
  m_start = m_finish = nullptr;

  CompilerType type = m_backend.GetCompilerType();
  if (type.GetNumTemplateArguments() == 0)
    return ChildCacheState::eRefetch;

  m_element_type = type.GetTypeTemplateArgument(0);
  if (std::optional<uint64_t> size = m_element_type.GetByteSize(nullptr))
    m_element_size = *size;

  if (m_element_size == 0)
    return ChildCacheState::eRefetch;

  ValueObjectSP start = m_backend.GetChildMemberWithName("__begin_");
  ValueObjectSP finish = m_backend.GetChildMemberWithName("__end_");

  if (!start || !finish)
    return ChildCacheState::eRefetch;

  m_start = start.get();
  m_finish = finish.get();

  return ChildCacheState::eRefetch;
}

bool lldb_private::formatters::LibcxxStdValarraySyntheticFrontEnd::
    MightHaveChildren() {
  return true;
}

size_t lldb_private::formatters::LibcxxStdValarraySyntheticFrontEnd::
    GetIndexOfChildWithName(ConstString name) {
  if (!m_start || !m_finish)
    return std::numeric_limits<size_t>::max();
  return ExtractIndexFromString(name.GetCString());
}

lldb_private::SyntheticChildrenFrontEnd *
lldb_private::formatters::LibcxxStdValarraySyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  if (!valobj_sp)
    return nullptr;
  return new LibcxxStdValarraySyntheticFrontEnd(valobj_sp);
}

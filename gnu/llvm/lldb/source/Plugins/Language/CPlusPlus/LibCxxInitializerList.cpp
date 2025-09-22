//===-- LibCxxInitializerList.cpp -----------------------------------------===//
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
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

namespace lldb_private {
namespace formatters {
class LibcxxInitializerListSyntheticFrontEnd
    : public SyntheticChildrenFrontEnd {
public:
  LibcxxInitializerListSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  ~LibcxxInitializerListSyntheticFrontEnd() override;

  llvm::Expected<uint32_t> CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override;

  lldb::ChildCacheState Update() override;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(ConstString name) override;

private:
  ValueObject *m_start = nullptr;
  CompilerType m_element_type;
  uint32_t m_element_size = 0;
  size_t m_num_elements = 0;
};
} // namespace formatters
} // namespace lldb_private

lldb_private::formatters::LibcxxInitializerListSyntheticFrontEnd::
    LibcxxInitializerListSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp), m_element_type() {
  if (valobj_sp)
    Update();
}

lldb_private::formatters::LibcxxInitializerListSyntheticFrontEnd::
    ~LibcxxInitializerListSyntheticFrontEnd() {
  // this needs to stay around because it's a child object who will follow its
  // parent's life cycle
  // delete m_start;
}

llvm::Expected<uint32_t> lldb_private::formatters::
    LibcxxInitializerListSyntheticFrontEnd::CalculateNumChildren() {
  m_num_elements = 0;
  ValueObjectSP size_sp(m_backend.GetChildMemberWithName("__size_"));
  if (size_sp)
    m_num_elements = size_sp->GetValueAsUnsigned(0);
  return m_num_elements;
}

lldb::ValueObjectSP lldb_private::formatters::
    LibcxxInitializerListSyntheticFrontEnd::GetChildAtIndex(uint32_t idx) {
  if (!m_start)
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
lldb_private::formatters::LibcxxInitializerListSyntheticFrontEnd::Update() {
  m_start = nullptr;
  m_num_elements = 0;
  m_element_type = m_backend.GetCompilerType().GetTypeTemplateArgument(0);
  if (!m_element_type.IsValid())
    return lldb::ChildCacheState::eRefetch;

  if (std::optional<uint64_t> size = m_element_type.GetByteSize(nullptr)) {
    m_element_size = *size;
    // Store raw pointers or end up with a circular dependency.
    m_start = m_backend.GetChildMemberWithName("__begin_").get();
  }

  return lldb::ChildCacheState::eRefetch;
}

bool lldb_private::formatters::LibcxxInitializerListSyntheticFrontEnd::
    MightHaveChildren() {
  return true;
}

size_t lldb_private::formatters::LibcxxInitializerListSyntheticFrontEnd::
    GetIndexOfChildWithName(ConstString name) {
  if (!m_start)
    return UINT32_MAX;
  return ExtractIndexFromString(name.GetCString());
}

lldb_private::SyntheticChildrenFrontEnd *
lldb_private::formatters::LibcxxInitializerListSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  return (valobj_sp ? new LibcxxInitializerListSyntheticFrontEnd(valobj_sp)
                    : nullptr);
}

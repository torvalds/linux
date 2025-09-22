//===-- LibCxxSliceArray.cpp-----------------------------------------------===//
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

bool LibcxxStdSliceArraySummaryProvider(ValueObject &valobj, Stream &stream,
                                        const TypeSummaryOptions &options) {
  ValueObjectSP obj = valobj.GetNonSyntheticValue();
  if (!obj)
    return false;

  ValueObjectSP ptr_sp = obj->GetChildMemberWithName("__size_");
  if (!ptr_sp)
    return false;
  const size_t size = ptr_sp->GetValueAsUnsigned(0);

  ptr_sp = obj->GetChildMemberWithName("__stride_");
  if (!ptr_sp)
    return false;
  const size_t stride = ptr_sp->GetValueAsUnsigned(0);

  stream.Printf("stride=%zu size=%zu", stride, size);

  return true;
}

/// Data formatter for libc++'s std::slice_array.
///
/// A slice_array is created by using:
///   operator[](std::slice slicearr);
/// and std::slice is created by:
///   slice(std::size_t start, std::size_t size, std::size_t stride);
/// The std::slice_array has the following members:
/// - __vp_ points to std::valarray::__begin_ + @a start
/// - __size_ is @a size
/// - __stride_is @a stride
class LibcxxStdSliceArraySyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  LibcxxStdSliceArraySyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  ~LibcxxStdSliceArraySyntheticFrontEnd() override;

  llvm::Expected<uint32_t> CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override;

  lldb::ChildCacheState Update() override;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(ConstString name) override;

private:
  /// A non-owning pointer to slice_array.__vp_.
  ValueObject *m_start = nullptr;
  /// slice_array.__size_.
  size_t m_size = 0;
  /// slice_array.__stride_.
  size_t m_stride = 0;
  /// The type of slice_array's template argument T.
  CompilerType m_element_type;
  /// The sizeof slice_array's template argument T.
  uint32_t m_element_size = 0;
};

} // namespace formatters
} // namespace lldb_private

lldb_private::formatters::LibcxxStdSliceArraySyntheticFrontEnd::
    LibcxxStdSliceArraySyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp), m_element_type() {
  if (valobj_sp)
    Update();
}

lldb_private::formatters::LibcxxStdSliceArraySyntheticFrontEnd::
    ~LibcxxStdSliceArraySyntheticFrontEnd() {
  // these need to stay around because they are child objects who will follow
  // their parent's life cycle
  // delete m_start;
}

llvm::Expected<uint32_t> lldb_private::formatters::
    LibcxxStdSliceArraySyntheticFrontEnd::CalculateNumChildren() {
  return m_size;
}

lldb::ValueObjectSP
lldb_private::formatters::LibcxxStdSliceArraySyntheticFrontEnd::GetChildAtIndex(
    uint32_t idx) {
  if (!m_start)
    return lldb::ValueObjectSP();

  uint64_t offset = idx * m_stride * m_element_size;
  offset = offset + m_start->GetValueAsUnsigned(0);
  StreamString name;
  name.Printf("[%" PRIu64 "]", (uint64_t)idx);
  return CreateValueObjectFromAddress(name.GetString(), offset,
                                      m_backend.GetExecutionContextRef(),
                                      m_element_type);
}

lldb::ChildCacheState
lldb_private::formatters::LibcxxStdSliceArraySyntheticFrontEnd::Update() {
  m_start = nullptr;

  CompilerType type = m_backend.GetCompilerType();
  if (type.GetNumTemplateArguments() == 0)
    return ChildCacheState::eRefetch;

  m_element_type = type.GetTypeTemplateArgument(0);
  if (std::optional<uint64_t> size = m_element_type.GetByteSize(nullptr))
    m_element_size = *size;

  if (m_element_size == 0)
    return ChildCacheState::eRefetch;

  ValueObjectSP start = m_backend.GetChildMemberWithName("__vp_");
  ValueObjectSP size = m_backend.GetChildMemberWithName("__size_");
  ValueObjectSP stride = m_backend.GetChildMemberWithName("__stride_");

  if (!start || !size || !stride)
    return ChildCacheState::eRefetch;

  m_start = start.get();
  m_size = size->GetValueAsUnsigned(0);
  m_stride = stride->GetValueAsUnsigned(0);

  return ChildCacheState::eRefetch;
}

bool lldb_private::formatters::LibcxxStdSliceArraySyntheticFrontEnd::
    MightHaveChildren() {
  return true;
}

size_t lldb_private::formatters::LibcxxStdSliceArraySyntheticFrontEnd::
    GetIndexOfChildWithName(ConstString name) {
  if (!m_start)
    return std::numeric_limits<size_t>::max();
  return ExtractIndexFromString(name.GetCString());
}

lldb_private::SyntheticChildrenFrontEnd *
lldb_private::formatters::LibcxxStdSliceArraySyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  if (!valobj_sp)
    return nullptr;
  return new LibcxxStdSliceArraySyntheticFrontEnd(valobj_sp);
}
